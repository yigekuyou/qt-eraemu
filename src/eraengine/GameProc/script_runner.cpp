/*
 * emuera —— Emuera（ERB 脚本引擎）的 Qt6 + QML/C++ 移植
 * Copyright (C) 2026  yigekuyou
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "script_runner.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "variable_storage.h"
#include "system_state_machine.h"
#include "ast/expression_evaluator.h"
#include <QDebug>

namespace {

// 去掉 Emuera 变量引用可能带的 % 前缀/后缀
QString bareVarName(const QString& raw) {
    QString n = raw;
    if (n.startsWith('%')) n = n.mid(1);
    if (n.endsWith('%')) n.chop(1);
    return n;
}

} // namespace

ScriptRunner::ScriptRunner(EraParseTable* table,
                           ExecutionEngine* engine,
                           ProcessState* state,
                           VariableStorage* storage,
                           QObject* parent)
    : QObject(parent)
    , m_table(table)
    , m_engine(engine)
    , m_state(state)
    , m_storage(storage)
{
}

void ScriptRunner::setExpressionEvaluator(ExpressionEvaluator* evaluator) {
    m_evaluator = evaluator;
    if (m_evaluator) {
        // 表达式中的用户自定义函数经本执行链回调
        m_evaluator->setUserFunctionInvoker(
            [this](const QString& name, const QList<QVariant>& args, QVariant& out) {
                return invokeUserFunction(name, args, out);
            });
    }
}

// ---------------------------------------------------------------------------
// 信号与槽入口
// ---------------------------------------------------------------------------

void ScriptRunner::onContinueExecution() {
    if (m_running) {
        return;   // 重入保护
    }
    m_running = true;
    qint64 steps = 0;
    while (m_state->isRunning()) {
        if (m_stepLimit > 0 && ++steps > m_stepLimit) {
            emit errorOccurred(QStringLiteral("脚本可能陷入死循环：本次连续执行超过 %1 步（@%2 第 %3 行）")
                                   .arg(m_stepLimit)
                                   .arg(m_table->currentFrame().callLabel.isEmpty()
                                            ? QStringLiteral("?") : m_table->currentFrame().callLabel)
                                   .arg(m_table->currentLine()));
            m_state->setErrorState();
            break;
        }
        if (!stepOnce()) {
            break;   // 挂起 / 结束 / 错误
        }
    }
    m_running = false;
}

ExecState ScriptRunner::runToCompletion() {
    // 直接置为 Continue 并手动驱动一次（不依赖信号接线，便于测试/CLI）
    m_state->setExecState(ExecState::Continue);
    onContinueExecution();
    return m_state->getExecState();
}

void ScriptRunner::onInputProvided(qint64 value) {
    if (m_state->getExecState() == ExecState::WaitInput
        || m_state->getExecState() == ExecState::WaitSystemInput) {
        m_storage->setSystemVariable(QStringLiteral("RESULT"), 0, value);
        m_storage->setGlobalInt1D(QStringLiteral("RESULT"), 0, value);
        m_state->requestResume();   // 发 continueExecution -> onContinueExecution
    }
}

void ScriptRunner::onSystemStateChanged(SystemStateCode state) {
    Q_UNUSED(state);
    // 系统状态变化由控制器决定是否恢复；此处保留挂钩。
}

void ScriptRunner::onHaltRequested() {
    m_state->requestHalt();
}

// ---------------------------------------------------------------------------
// 主步进
// ---------------------------------------------------------------------------

bool ScriptRunner::stepOnce() {
    if (!m_table || !m_table->hasPosition()) {
        m_state->requestHalt();
        emit finished();
        return false;
    }

    const QString script = m_table->currentScript();
    const ScriptData* sd = m_table->script(script);
    if (!sd) {
        m_state->setErrorState();
        emit errorOccurred(QStringLiteral("script not found: %1").arg(script));
        return false;
    }

    const int pc = m_table->currentLine();
    if (pc < 0 || pc >= sd->lines.size()) {
        // 脚本结束：能返回就返回调用者，否则停止
        if (m_table->returnFromCall()) {
            return true;
        }
        m_state->requestHalt();
        emit finished();
        return false;
    }

    const LogicalLine& line = sd->lines.at(pc);
    const ExecState r = executeLine(line);

    if (r == ExecState::Continue) {
        emit instructionExecuted(script, pc);
        return true;
    }

    // 挂起 / 结束
    m_state->setExecState(r);
    if (r == ExecState::WaitInput || r == ExecState::WaitSystemInput) {
        emit inputRequested(line.functionName);
    } else if (r == ExecState::Halt) {
        emit finished();
    } else if (r == ExecState::Error) {
        emit errorOccurred(QStringLiteral("execution error"));
    }
    emit suspended(r);
    return false;
}

// ---------------------------------------------------------------------------
// 单行执行（控制流 + 指令）
// ---------------------------------------------------------------------------

ScriptRunner::LoopFrame* ScriptRunner::topLoop(LoopFrame::Kind kind) {
    if (m_loops.isEmpty()) return nullptr;
    if (m_loops.last().kind == kind) return &m_loops.last();
    return nullptr;
}

int ScriptRunner::labelLine(const QString& label) const {
    return m_table->getLabelPosition(m_table->currentScript(), label);
}

GameBaseData* ScriptRunner::baseData() const {
    return m_engine ? m_engine->gameBaseData() : nullptr;
}

bool ScriptRunner::evalInt(const QSharedPointer<ExpressionNode>& ast, const QString& raw, qint64& out) {
    ExpressionEvaluator local;
    ExpressionEvaluator* ev = m_evaluator ? m_evaluator : &local;
    if (ast) {
        out = ev->evaluate(*ast, m_storage, baseData()).toLongLong();
        return true;
    }
    if (raw.trimmed().isEmpty()) {
        out = 0;
        return true;
    }
    out = ev->evaluate(raw, m_storage, baseData()).toLongLong();
    return true;
}

bool ScriptRunner::evalCondition(const LogicalLine& line, bool& out) {
    return m_table->evaluateCondition(m_table->currentScript(), line.lineIndex, out);
}

ExecState ScriptRunner::executeLine(const LogicalLine& line) {
    const QString script = m_table->currentScript();
    const ScriptData* sd = m_table->script(script);
    if (!sd) {
        return ExecState::Error;
    }
    const int pc = m_table->currentLine();

    const auto gotoLine = [&](int npc) {
        m_table->setPosition(script, npc);   // 允许 npc == lines.size()（越过末尾 -> 结束）
    };
    const auto advance = [&]() { m_table->advance(); };

    if (line.kind != LineKind::Instruction) {
        // 函数标签 = 当前函数体结束（对齐 C# runScriptProc：顺序落入 FunctionLabelLine → Return(0)）。
        // 但若当前位置是「跳转/调用/入口」落点，则标签只是跳转目标，跳过即可。
        if (line.kind == LineKind::FunctionLabel) {
            if (m_table->jumpedToCurrent()) {
                advance();
                return ExecState::Continue;
            }
            if (m_table->returnFromCall()) {
                return ExecState::Continue;
            }
            return ExecState::Halt;
        }
        advance();   // 空行 / 注释 / 预处理指令
        return ExecState::Continue;
    }

    const QString& name = line.functionName;

    // ---- 条件 ----
    if (name == QLatin1String("IF")) {
        // C# 语义：依次检查 IF 自身条件 -> ELSEIF 条件 -> ELSE，命中即进入其分支体
        const QList<int> branches = m_table->ifBranches(script, pc);
        bool taken = false;
        bool cond = false;
        if (evalCondition(line, cond) && cond) {
            taken = true;
            advance();               // 命中 IF 自身 -> 进入主体
        } else {
            for (int b : branches) {
                const LogicalLine* bl = m_table->lineAt(script, b);
                if (!bl) continue;
                if (bl->is("ELSE")) {
                    taken = true;
                    gotoLine(b + 1);     // 进入 ELSE 体
                    break;
                }
                bool bc = false;
                if (evalCondition(*bl, bc) && bc) {
                    taken = true;
                    gotoLine(b + 1);     // 进入该 ELSEIF 体
                    break;
                }
            }
            if (!taken) {
                gotoLine(m_table->jumpTarget(script, pc));   // 无分支命中 -> ENDIF 之后
            }
        }
        return ExecState::Continue;
    }
    if (name == QLatin1String("ELSEIF") || name == QLatin1String("ELSE")) {
        // 顺序落入（上一个分支体执行完）-> 跳到 ENDIF 之后
        gotoLine(m_table->jumpTarget(script, pc));
        return ExecState::Continue;
    }
    if (name == QLatin1String("ENDIF")) {
        advance();
        return ExecState::Continue;
    }
    if (name == QLatin1String("SIF")) {
        bool cond = false;
        evalCondition(line, cond);
        if (cond) advance();
        else gotoLine(pc + 2);       // 伪 -> 跳过下一行
        return ExecState::Continue;
    }

    // ---- 循环 ----
    if (name == QLatin1String("REPEAT")) {
        qint64 count = 0;
        evalInt(line.condition, line.raw, count);
        const int endLine = sd->loopEndLines.value(pc, -1);
        if (count <= 0) {
            gotoLine(endLine >= 0 ? endLine + 1 : pc + 1);
            return ExecState::Continue;
        }
        LoopFrame f;
        f.kind = LoopFrame::Kind::Repeat;
        f.startLine = pc;
        f.endLine = endLine;
        f.remaining = count;
        m_loops.append(f);
        advance();
        return ExecState::Continue;
    }
    if (name == QLatin1String("LOOP")) {
        LoopFrame* f = topLoop(LoopFrame::Kind::Repeat);
        if (f) {
            f->remaining--;
            if (f->remaining > 0) {
                gotoLine(f->startLine + 1);   // 回到循环体开头
            } else {
                m_loops.removeLast();
                advance();
            }
        } else {
            advance();
        }
        return ExecState::Continue;
    }
    if (name == QLatin1String("WHILE")) {
        LoopFrame* existing = topLoop(LoopFrame::Kind::While);
        if (existing && existing->startLine == pc) {
            // 由 WEND 回跳而来：重新判断，不重复压栈
            bool cond = false;
            evalCondition(line, cond);
            if (cond) {
                advance();
            } else {
                const int endLine = existing->endLine;
                m_loops.removeLast();
                gotoLine(endLine >= 0 ? endLine + 1 : pc + 1);
            }
            return ExecState::Continue;
        }
        bool cond = false;
        evalCondition(line, cond);
        if (cond) {
            LoopFrame f;
            f.kind = LoopFrame::Kind::While;
            f.startLine = pc;
            f.endLine = sd->loopEndLines.value(pc, -1);
            m_loops.append(f);
            advance();
        } else {
            const int endLine = sd->loopEndLines.value(pc, -1);
            gotoLine(endLine >= 0 ? endLine + 1 : pc + 1);
        }
        return ExecState::Continue;
    }
    if (name == QLatin1String("WEND")) {
        LoopFrame* f = topLoop(LoopFrame::Kind::While);
        if (f) {
            gotoLine(f->startLine);   // 回到 WHILE 重新判断
        } else {
            advance();
        }
        return ExecState::Continue;
    }
    if (name == QLatin1String("FOR")) {
        // FOR var, start, end[, step]
        // 优先使用 AST 已归约的类型化参数（TypedArgument::params / exprs）
        QList<const Operand*> ops;
        if (line.argument.kind == ArgKind::ForNext && line.argument.params.size() >= 3) {
            for (const Operand& a : line.argument.params) ops.append(&a);
        } else {
            for (const Operand& a : line.arguments) {
                if (a.raw != QLatin1String(",")) ops.append(&a);
            }
        }
        const int endLine = sd->loopEndLines.value(pc, -1);
        if (ops.size() >= 3) {
            LoopFrame f;
            f.kind = LoopFrame::Kind::For;
            f.startLine = pc;
            f.endLine = endLine;
            f.varName = bareVarName(ops[0]->raw);
            evalInt(ops[1]->ast, ops[1]->raw, f.value);
            evalInt(ops[2]->ast, ops[2]->raw, f.end);
            f.step = 1;
            if (ops.size() >= 4) evalInt(ops[3]->ast, ops[3]->raw, f.step);

            writeLoopCounter(f.varName, f.value);

            const bool enters = (f.step >= 0) ? (f.value <= f.end) : (f.value >= f.end);
            if (!enters) {
                gotoLine(endLine >= 0 ? endLine + 1 : pc + 1);
            } else {
                m_loops.append(f);
                advance();
            }
        } else {
            advance();
        }
        return ExecState::Continue;
    }
    if (name == QLatin1String("NEXT")) {
        LoopFrame* f = topLoop(LoopFrame::Kind::For);
        if (f) {
            f->value += f->step;
            writeLoopCounter(f->varName, f->value);
            const bool more = (f->step >= 0) ? (f->value <= f->end) : (f->value >= f->end);
            if (more) {
                gotoLine(f->startLine + 1);
            } else {
                m_loops.removeLast();
                advance();
            }
        } else {
            advance();
        }
        return ExecState::Continue;
    }
    if (name == QLatin1String("BREAK")) {
        if (!m_loops.isEmpty()) {
            const LoopFrame f = m_loops.takeLast();
            gotoLine(f.endLine >= 0 ? f.endLine + 1 : pc + 1);
        } else {
            advance();
        }
        return ExecState::Continue;
    }
    if (name == QLatin1String("CONTINUE")) {
        if (!m_loops.isEmpty()) {
            // 跳到**循环末尾行**（NEXT / LOOP / WEND），让「自增 + 条件」正常执行。
            // 对齐 C#：CONTINUE 的目标是循环的 continue 点，而不是循环体开头。
            const LoopFrame& f = m_loops.last();
            gotoLine(f.endLine >= 0 ? f.endLine : f.startLine + 1);
        } else {
            advance();
        }
        return ExecState::Continue;
    }

    // ---- 跳转 / 调用 ----
    if (name == QLatin1String("GOTO")) {
        const QString label = line.arguments.isEmpty() ? QString() : line.arguments.first().raw;
        if (label.isEmpty() || !m_table->jumpToLabel(label)) {
            m_state->setErrorState();
            emit errorOccurred(QStringLiteral("GOTO label not found: %1").arg(label));
            return ExecState::Error;
        }
        return ExecState::Continue;
    }
    if (name == QLatin1String("CALL")) {
        const QString label = line.arguments.isEmpty() ? QString() : line.arguments.first().raw;
        const UserFunctionDecl* info = m_table->userFunction(label);
        m_aliasStack.append(m_storage->localAliases());
        bindArguments(info, line.arguments);
        if (label.isEmpty() || !m_table->callLabel(label)) {
            if (!m_aliasStack.isEmpty()) m_storage->setLocalAliases(m_aliasStack.takeLast());
            m_state->setErrorState();
            emit errorOccurred(QStringLiteral("CALL label not found: %1").arg(label));
            return ExecState::Error;
        }
        // 函数私有变量初值（#DIM X = 7）：每次进入函数时写入
        m_table->applyPrivateVariableDefaults(label);
        return ExecState::Continue;
    }
    if (name == QLatin1String("RETURN") || name == QLatin1String("RETURNF")) {
        qint64 result = 0;
        if (!line.arguments.isEmpty()) {
            const Operand& a = line.arguments.first();
            if (a.isString && line.arguments.size() == 1) {
                m_storage->setLocalStr(0, a.raw);
            } else {
                // 返回值是整行操作数表达式（可能被切成多个 token）
                QString expr;
                for (const Operand& op : line.arguments) {
                    if (!expr.isEmpty()) expr += ' ';
                    expr += op.raw;
                }
                const QSharedPointer<ExpressionNode> ast =
                    (line.arguments.size() == 1) ? a.ast : m_table->expressionAst(expr);
                qint64 v = 0;
                evalInt(ast, expr, v);
                result = v;
                m_storage->setSystemVariable(QStringLiteral("RESULT"), 0, result);
                m_storage->setGlobalInt1D(QStringLiteral("RESULT"), 0, result);
            }
        }
        if (!m_table->returnFromCall()) {
            // 顶层 RETURN：脚本结束
            return ExecState::Halt;
        }
        // 恢复局部别名快照
        if (!m_aliasStack.isEmpty()) {
            m_storage->setLocalAliases(m_aliasStack.takeLast());
        }
        return ExecState::Continue;
    }

    // ---- SWAP / SWAPVAR：交换两个变量的值（对齐 C# SP_SWAP_Instruction）----
    if (name == QLatin1String("SWAP") || name == QLatin1String("SWAPVAR")) {
        QList<const Operand*> ops;
        if (line.argument.kind == ArgKind::Swap && line.argument.params.size() >= 2) {
            for (const Operand& a : line.argument.params) ops.append(&a);
        } else {
            for (const Operand& a : line.arguments) {
                if (a.raw != QLatin1String(",")) ops.append(&a);
            }
        }
        if (ops.size() >= 2) {
            QString na, nb;
            int ia = 0, ib = 0;
            if (extractVarRef(*ops[0], na, ia) && extractVarRef(*ops[1], nb, ib)) {
                const qint64 va = readIntVar(na, ia);
                const qint64 vb = readIntVar(nb, ib);
                writeIntVar(na, ia, vb);
                writeIntVar(nb, ib, va);
            }
        }
        advance();
        return ExecState::Continue;
    }

    // ---- 输入等待（由中心执行状态挂起）----
    if (name == QLatin1String("INPUT") || name == QLatin1String("ONEINPUT")
        || name == QLatin1String("INPUTS")) {
        advance();                       // 指令已消费
        return ExecState::WaitInput;     // 挂起等待用户操作
    }

    // ---- 实时 / 限时输入（对齐 C# INPUTMOUSEKEY / TONEINPUT）----
    if (name == QLatin1String("INPUTMOUSEKEY")) {
        qint64 timeout = 0;
        if (!line.arguments.isEmpty()) evalInt(line.arguments.first().ast, line.arguments.first().raw, timeout);
        advance();
        if (m_machine) {
            m_machine->waitMouseKey(static_cast<int>(timeout));
            return m_state->getExecState();
        }
        return ExecState::WaitInput;
    }
    if (name == QLatin1String("TONEINPUT") || name == QLatin1String("TONEINPUTS")) {
        qint64 timeout = 0;
        if (!line.arguments.isEmpty()) evalInt(line.arguments.first().ast, line.arguments.first().raw, timeout);
        advance();
        if (m_machine) {
            m_machine->waitTimedInput(static_cast<int>(timeout));
            return m_state->getExecState();
        }
        return ExecState::WaitInput;
    }
    // AWAIT n：挂起 n 毫秒后自动继续（不阻塞、不请求输入）
    if (name == QLatin1String("AWAIT")) {
        qint64 ms = 0;
        if (!line.arguments.isEmpty()) evalInt(line.arguments.first().ast, line.arguments.first().raw, ms);
        if (ms < 0) ms = 0;
        advance();
        if (m_machine && ms > 0) {
            m_machine->awaitDelay(static_cast<int>(ms));
            return m_state->getExecState();
        }
        return ExecState::Continue;
    }

    // ---- BEGIN：设置 BEGIN 类型并返回当前函数（对齐 C# BEGIN_Instruction）----
    if (name == QLatin1String("BEGIN")) {
        const QString keyword = line.arguments.isEmpty() ? QString() : line.arguments.first().raw;
        QString error;
        const bool ok = m_machine ? m_machine->beginWithKeyword(keyword, &error)
                                  : m_state->setBeginKeyword(keyword, &error);
        if (!ok) {
            m_state->setErrorState();
            emit errorOccurred(error);
            return ExecState::Error;
        }
        // C# state.Return(0)：BEGIN 之后直接返回，由系统状态机在帧底接管
        if (!m_table->returnFromCall()) {
            return ExecState::Halt;
        }
        return ExecState::Continue;
    }

    // ---- SAVEGAME / LOADGAME：记录返回状态并切换（对齐 C# SAVELOADGAME_Instruction）----
    if (name == QLatin1String("SAVEGAME") || name == QLatin1String("LOADGAME")) {
        if (!m_state->canSave()) {
            m_state->setErrorState();
            emit errorOccurred(QStringLiteral("@%1 中不能执行 SAVEGAME/LOADGAME 命令")
                                   .arg(m_table->currentFrame().callLabel));
            return ExecState::Error;
        }
        if (m_machine) {
            m_machine->requestSaveLoad(name == QLatin1String("SAVEGAME"));
        }
        advance();
        return ExecState::Continue;
    }

    // ---- CALLTRAIN / STOPCALLTRAIN / DOTRAIN ----
    if (m_machine && name == QLatin1String("CALLTRAIN")) {
        qint64 count = 0;
        evalInt(line.arguments.isEmpty() ? QSharedPointer<ExpressionNode>()
                                         : line.arguments.first().ast,
                line.arguments.isEmpty() ? QString() : line.arguments.first().raw, count);
        m_machine->setCommands(count);
        advance();
        return ExecState::Continue;
    }
    if (m_machine && name == QLatin1String("STOPCALLTRAIN")) {
        if (m_machine->isContinuousTrain()) {
            m_machine->clearCommands();
        }
        advance();
        return ExecState::Continue;
    }
    if (m_machine && name == QLatin1String("DOTRAIN")) {
        qint64 train = 0;
        evalInt(line.arguments.isEmpty() ? QSharedPointer<ExpressionNode>()
                                         : line.arguments.first().ast,
                line.arguments.isEmpty() ? QString() : line.arguments.first().raw, train);
        if (train < 0 || train >= m_machine->trainCount()) {
            m_state->setErrorState();
            emit errorOccurred(QStringLiteral("DOTRAIN 的值超出 TRAINNAME 范围"));
            return ExecState::Error;
        }
        m_machine->setDoTrainSelectCom(train);
        m_state->setSystemState(SystemStateCode::Train_DoTrain);
        advance();
        return ExecState::Continue;
    }

    // ---- 其它指令：交给 ExecutionEngine 执行（赋值 / PRINT / …）----
    if (m_engine) {
        m_engine->setCurrentScript(script);
        m_engine->setExecutionPosition(pc);
        m_engine->executeInstruction(line);
    }
    advance();
    return ExecState::Continue;
}

// ---------------------------------------------------------------------------
// 用户自定义函数
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 形参绑定（对齐 C# Process.callFunction / VariableLocal）
//
//   声明的形参表把「第 i 个实参」映射到局部槽：
//     ARG[:k]   -> LOCAL[k]  （整数）
//     ARGS[:k]  -> LOCALS[k] （字符串）
//     其它名字   -> 函数私有变量：本移植把它登记为「名字 -> LOCAL[i]」别名
//   （Emuera 里 ARG 就是 LOCAL 的整数数组、ARGS 是字符串数组，因此语义等价）
// ---------------------------------------------------------------------------
qint64 ScriptRunner::readIntVar(const QString& name, int index) const {
    const QString upper = name.toUpper();
    if (upper == QLatin1String("LOCAL") || upper == QLatin1String("ARG")) {
        return m_storage->getLocalInt(index);
    }
    if (m_storage->hasSystemVariable(name)) {
        return m_storage->getSystemVariable(name, index);
    }
    return m_storage->getGlobalInt1D(name, index);
}

void ScriptRunner::writeIntVar(const QString& name, int index, qint64 value) {
    const QString upper = name.toUpper();
    if (upper == QLatin1String("LOCAL") || upper == QLatin1String("ARG")) {
        m_storage->setLocalInt(index, value);
        return;
    }
    if (m_storage->hasSystemVariable(name)) {
        m_storage->setSystemVariable(name, index, value);
        return;
    }
    m_storage->setGlobalInt1D(name, index, value);
}

bool ScriptRunner::extractVarRef(const Operand& op, QString& name, int& index) {
    index = 0;
    if (op.ast && op.ast->kind() == NodeKind::Variable) {
        const VariableNode& var = static_cast<const VariableNode&>(*op.ast);
        name = var.name();
        if (var.isArray() && !var.indices().isEmpty()) {
            const QSharedPointer<ExpressionNode> idxNode = var.indices().at(0);
            if (idxNode) {
                if (m_evaluator) {
                    index = static_cast<int>(
                        m_evaluator->evaluate(*idxNode, m_storage, baseData()).toLongLong());
                } else {
                    qint64 v = 0;
                    evalInt(idxNode, QString(), v);
                    index = static_cast<int>(v);
                }
            }
        }
        return true;
    }
    // 退化：从 raw 文本解析 "NAME[:下标]"（下标可以是变量/表达式）
    QString raw = op.raw.trimmed();
    const int colon = raw.indexOf(QLatin1Char(':'));
    if (colon > 0) {
        const QString idxText = raw.mid(colon + 1).trimmed();
        bool ok = false;
        const int direct = idxText.toInt(&ok);
        if (ok) {
            index = direct;
        } else if (m_table) {
            const QSharedPointer<ExpressionNode> ast = m_table->expressionAst(idxText);
            if (ast) {
                if (m_evaluator) {
                    index = static_cast<int>(m_evaluator->evaluate(*ast, m_storage, baseData()).toLongLong());
                } else {
                    qint64 v = 0;
                    evalInt(ast, QString(), v);
                    index = static_cast<int>(v);
                }
            }
        }
        raw = raw.left(colon);
    }
    name = bareVarName(raw);
    return !name.isEmpty();
}

void ScriptRunner::writeLoopCounter(const QString& rawName, qint64 value) {
    // 支持 `LOCAL:0` / `LOCAL:1` / `A:3` 这类带下标（数字字面量）的循环变量。
    // —— eraTetris 用 LOCAL:0/LOCAL:1/LOCAL:2 做嵌套循环计数器，若一律写到槽 0
    //    会导致内层循环覆盖外层计数器。
    QString name = rawName;
    int index = 0;
    const int colon = name.indexOf(QLatin1Char(':'));
    if (colon > 0) {
        bool ok = false;
        const int idx = name.mid(colon + 1).trimmed().toInt(&ok);
        if (ok) { index = idx; name = name.left(colon); }
    }
    name = bareVarName(name);
    const QString upper = name.toUpper();
    if (upper == QLatin1String("LOCAL") || upper == QLatin1String("ARG")) {
        m_storage->setLocalInt(index, value);
        return;
    }
    m_storage->setSystemVariable(name, index, value);
    m_storage->setGlobalInt1D(name, index, value);
}

void ScriptRunner::bindArguments(const UserFunctionDecl* info, const QList<Operand>& callArgs) {
    const auto bindOne = [this](const UserParamDecl* p, int position,
                                bool argIsString, const QString& strValue, qint64 intValue) {
        switch (p ? p->target : UserParamTarget::Unknown) {
        case UserParamTarget::Arg:
            m_storage->setLocalInt(p->index, intValue);
            // ARG:0 也可以写成裸 ARG（求值器已知），同时登记别名便于形参名解析
            m_storage->setLocalAlias(p->name, p->index);
            break;
        case UserParamTarget::Args:
            m_storage->setLocalStr(p->index, argIsString ? strValue : QString::number(intValue));
            m_storage->setLocalAlias(p->name, p->index);
            break;
        case UserParamTarget::LocalVar:
            // 私有变量：整型写 LOCAL[position]，字符串写 LOCALS[position]，名字做别名
            if (p->type == OperandType::Str) {
                m_storage->setLocalStr(position, argIsString ? strValue : QString::number(intValue));
            } else {
                m_storage->setLocalInt(position, intValue);
                if (argIsString) m_storage->setLocalStr(position, strValue);
            }
            m_storage->setLocalAlias(p->varName.isEmpty() ? p->name : p->varName, position);
            break;
        case UserParamTarget::Unknown:
        default:
            // 未归类（或无声明）：退化为按位置绑定到 ARG/ARGS
            if (argIsString) m_storage->setLocalStr(position, strValue);
            else             m_storage->setLocalInt(position, intValue);
            break;
        }
    };

    m_storage->clearLocalAliases();
    for (int i = 1; i < callArgs.size(); ++i) {
        const Operand& a = callArgs.at(i);
        const int position = i - 1;
        qint64 intValue = 0;
        if (!a.isString) evalInt(a.ast, a.raw, intValue);

        const UserParamDecl* param = nullptr;
        if (info && position < info->params.size()) param = &info->params.at(position);
        bindOne(param, position, a.isString, a.raw, intValue);
    }
}

bool ScriptRunner::invokeUserFunction(const QString& name, const QList<QVariant>& args, QVariant& out) {
    const UserFunctionDecl* info = m_table->userFunction(name);
    if (!info || !info->isMethod) {
        return false;   // 非用户函数（交给内置函数）
    }
    if (m_running && !m_state->isRunning()) {
        return false;   // 已挂起，无法同步求值
    }

    // 保存当前位置 / 别名快照
    const QString savedScript = m_table->currentScript();
    const int savedPc = m_table->currentLine();
    const int savedDepth = m_table->depth();
    const int savedLoops = m_loops.size();
    const int savedAliasDepth = m_aliasStack.size();

    // 绑定实参（按声明的形参表：ARG/ARGS/私有变量）
    m_aliasStack.append(m_storage->localAliases());
    m_storage->clearLocalAliases();
    for (int i = 0; i < args.size(); ++i) {
        const QVariant& v = args.at(i);
        const bool isStr = (v.typeId() == QMetaType::QString);
        const UserParamDecl* param = (i < info->params.size()) ? &info->params.at(i) : nullptr;
        switch (param ? param->target : UserParamTarget::Unknown) {
        case UserParamTarget::Arg:
            m_storage->setLocalInt(param->index, v.toLongLong());
            m_storage->setLocalAlias(param->name, param->index);
            break;
        case UserParamTarget::Args:
            m_storage->setLocalStr(param->index, v.toString());
            m_storage->setLocalAlias(param->name, param->index);
            break;
        case UserParamTarget::LocalVar:
            m_storage->setLocalInt(i, v.toLongLong());
            if (isStr) m_storage->setLocalStr(i, v.toString());
            m_storage->setLocalAlias(param->varName.isEmpty() ? param->name : param->varName, i);
            break;
        case UserParamTarget::Unknown:
        default:
            m_storage->setLocalInt(i, v.toLongLong());
            if (isStr) m_storage->setLocalStr(i, v.toString());
            break;
        }
    }

    // 以 savedPc 为返回地址压帧并进入函数体
    if (!m_table->callLabelWithReturn(info->name, savedPc)) {
        m_aliasStack.resize(savedAliasDepth);
        return false;
    }

    // 同步跑完该函数（直到返回帧被弹出）
    const bool wasRunning = m_running;
    m_running = true;
    while (m_state->isRunning() && m_table->depth() > savedDepth) {
        if (!stepOnce()) {
            break;   // 函数内挂起（如 INPUT）：无法同步返回
        }
    }
    m_running = wasRunning;

    m_loops.resize(savedLoops);
    // 恢复别名快照（正常 RETURN 已弹出一层；这里兜底）
    while (m_aliasStack.size() > savedAliasDepth) {
        m_storage->setLocalAliases(m_aliasStack.takeLast());
    }

    const bool completed = (m_table->depth() == savedDepth);
    if (!completed) {
        m_table->setPosition(savedScript, savedPc);
        return false;
    }

    out = QVariant(m_storage->getSystemVariable(QStringLiteral("RESULT"), 0));
    return true;
}
