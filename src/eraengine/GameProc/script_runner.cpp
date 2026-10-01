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
#include "eraengine_log.h"
#include <algorithm>
#include "era_parse_table.h"
#include "execution_engine.h"
#include "variable_storage.h"
#include "system_state_machine.h"
#include "ast/expression_evaluator.h"
#include "ast/strform_parser.h"
#include <QDebug>
#include <QElapsedTimer>
#include <bit>

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
    connect(m_engine, &ExecutionEngine::consolePrint, this, [this] { m_printed = true; });
    connect(m_engine, &ExecutionEngine::consolePrintButton, this, [this] { m_printed = true; });
    connect(m_engine, &ExecutionEngine::consolePrintTemplate, this, [this] { m_printed = true; });
    connect(m_table, &EraParseTable::entryPointReached, this, [this](const QString&) {
        while (!m_callContexts.isEmpty())
            m_storage->setLocalContext(m_callContexts.takeLast().locals);
        m_loops.clear();
        m_lastReturnValue = QVariant::fromValue<qint64>(0);
    });
}

void ScriptRunner::setExpressionEvaluator(ExpressionEvaluator* evaluator) {
    m_evaluator = evaluator;
    if (m_evaluator) {
        // 表达式中的用户自定义函数经本执行链回调
        m_evaluator->setUserFunctionInvoker(
            [this](const QString& name, const QList<QVariant>& args,
                   const QList<const ExpressionNode*>& argNodes, QVariant& out) {
                return invokeUserFunction(name, args, argNodes, out);
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
    m_steps = 0;
    while (m_state->isRunning()) {
        if (!stepOnce()) {
            break;   // 挂起 / 结束 / 错误
        }
    }
    m_running = false;
}

ExecState ScriptRunner::runSlice(int instructionBudget, int timeBudgetMs) {
    if (m_running || !m_state->isRunning()) return m_state->getExecState();
    m_running = true;
    m_printed = false;
    if (!m_continuingSlice) m_steps = 0;
    QElapsedTimer elapsed;
    elapsed.start();
    // 打印只标记本周期有输出；不能把每条 PRINT 当作一次执行暂停，
    // 否则大量菜单文本会把泵浦速度降到一条指令/帧。
    for (int i = 0; i < qMax(1, instructionBudget); ++i) {
        if (!stepOnce() || elapsed.elapsed() >= qMax(1, timeBudgetMs)) break;
    }
    m_continuingSlice = m_state->isRunning();
    m_running = false;
    return m_state->getExecState();
}

ExecState ScriptRunner::runToCompletion() {
    if (m_running) return m_state->getExecState();
    m_continuingSlice = false;
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

void ScriptRunner::enterCall(const QString& function) {
    m_callContexts.append({m_table->depth(), int(m_loops.size()), function, m_storage->localContext()});
    auto locals = m_functionLocals.value(function);
    locals.parameters.clear();
    locals.aliases.clear();
    m_storage->setLocalContext(locals);
    QStringList privateNames;
    for (const auto& decl : m_table->variableTable().localsOf(function))
        if (!decl.isConst) privateNames.append(decl.name);
    m_storage->setPrivateScope(function, privateNames);
}

bool ScriptRunner::returnFromCall() {
    if (!m_table->returnFromCall()) return false;
    if (!m_callContexts.isEmpty() && m_callContexts.last().depth == m_table->depth()) {
        const auto context = m_callContexts.takeLast();
        m_functionLocals.insert(context.function, m_storage->localContext());
        m_storage->setLocalContext(context.locals);
        m_loops.resize(context.loops);
    }
    return true;
}

bool ScriptRunner::stepOnce() {
    if (!m_table || !m_table->hasPosition()) {
        m_state->requestHalt();
        emit finished();
        return false;
    }

    if (m_stepLimit > 0 && ++m_steps > m_stepLimit) {
        emit errorOccurred(QStringLiteral("脚本可能陷入死循环：连续执行超过 %1 步（第 %2 行）")
                               .arg(m_stepLimit).arg(m_table->currentLine()));
        m_state->setErrorState();
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
        // 函数体自然结束（无 RETURN/RETURNF）：#FUNCTIONS 缺省返回**空字符串**
        // （eraTW 大量依赖 `STRLENS(GET_TALENTNAME(...))` 过滤无值素질 ——
        //   若缺省成整数 0，字符串化后是 "0"，会把所有无值素質显示成 [0]）。
        // 其余情况缺省 0 并清 RESULT。
        const UserFunctionDecl* finfo =
            m_table->userFunction(m_table->currentFrame().callLabel);
        if (finfo && finfo->isMethod && finfo->returnsString())
            m_lastReturnValue = QVariant(QString());
        else
            m_lastReturnValue = QVariant::fromValue<qint64>(0);
        m_storage->setSystemVariable("RESULT", 0, 0);
        m_storage->setGlobalInt1D("RESULT", 0, 0);
        // 脚本结束：能返回就返回调用者，否则停止
        if (returnFromCall()) {
            return true;
        }
        m_state->requestHalt();
        emit finished();
        return false;
    }

    const LogicalLine& line = sd->lines.at(pc);
    // [qdbug] 逐行执行跟踪（保留的调试桩）：高频率输出，仅在
    // EMUERA_QDBUG_TRACE=1 时启用；可用 EMUERA_QDBUG_TRACE_FILE 过滤脚本名
    // （如 COMMON.ERB）把开销降到可控 —— 追查「CHOICE 体内 INPUT 未等待、
    // 执行穿透到下一条指令」这类控制流缺陷时打开。
    if (qEnvironmentVariableIsSet("EMUERA_QDBUG_TRACE")
        && (m_qdbugTraceFile.isEmpty() || line.position.filename.contains(m_qdbugTraceFile))) {
        qCDebug(eraTrace) << "[qdbug] line" << line.position.toString()
                          << "|" << line.raw.trimmed().left(60)
                          << "| depth" << m_table->depth();
    }
    // 函数体边界（C# FunctionLabelLine 终止上一个函数体）：
    // 落入的不是本帧入口的 @label = 上一函数已自然结束、无 RETURN/RETURNF。
    // #FUNCTIONS 缺省返回**空字符串**（eraTW 依赖 STRLENS(GET_TALENTNAME(...))
    // 过滤无值素質），其余缺省 0。
    if (line.kind == LineKind::FunctionLabel && m_table->depth() > 0
        && m_table->currentFrame().entryLine != pc) {
        const UserFunctionDecl* finfo = m_table->userFunction(m_table->currentFrame().callLabel);
        if (finfo && finfo->isMethod && finfo->returnsString())
            m_lastReturnValue = QVariant(QString());
        else
            m_lastReturnValue = QVariant::fromValue<qint64>(0);
        m_storage->setSystemVariable("RESULT", 0, 0);
        m_storage->setGlobalInt1D("RESULT", 0, 0);
        if (returnFromCall()) {
            return true;
        }
        m_state->requestHalt();
        emit finished();
        return false;
    }
    // System entry points are not entered through CALL. Resolve private storage
    // from the executing owner as well, including resumed SHOW_SHOP frames.
    QStringList privateNames;
    for (const auto& decl : m_table->variableTable().localsOf(line.ownerFunction))
        if (!decl.isConst) privateNames.append(decl.name);
    m_storage->setPrivateScope(line.ownerFunction, privateNames);
    ExecState r = executeLine(line);
    if (m_state->getExecState() == ExecState::Error) r = ExecState::Error;

    if (r == ExecState::Continue) {
        emit instructionExecuted(script, pc);
        return true;
    }

    // 挂起 / 结束
    m_state->setExecState(r);
    if ((r == ExecState::WaitInput || r == ExecState::WaitSystemInput)
        && line.functionName != QLatin1String("AWAIT")) {
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
    if (m_loops.last().depth != m_table->depth()
        || m_loops.last().script != m_table->currentScript()) return nullptr;
    if (m_loops.last().kind == kind) {
        LoopFrame& f = m_loops.last();
        // 每次「回到循环头」时计数：超过阈值只告警一次，报出循环位置与状态。
        if (++f.iterations == kLoopIterationWarn && !f.warned) {
            f.warned = true;
            qWarning() << "[exec] 循环迭代异常偏多:" << f.script << "行" << f.startLine
                       << "种类" << static_cast<int>(f.kind) << "变量" << f.varName
                       << "当前值" << f.value << "终值" << f.end << "已迭代" << f.iterations;
        }
        return &f;
    }
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
        m_table->setPosition(script, npc, false);   // 允许 npc == lines.size()（越过末尾 -> 结束）
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
            m_lastReturnValue = QVariant::fromValue<qint64>(0);
            m_storage->setSystemVariable("RESULT", 0, 0);
            m_storage->setGlobalInt1D("RESULT", 0, 0);
            if (returnFromCall()) {
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
    // ---- SELECTCASE（对齐 C# SELECTCASE_Instruction / CASE_Instruction）----
    if (name == QLatin1String("SELECTCASE")) {
        // 求值 -> 顺序比较各 CASE 行；命中就跳到该 CASE 的下一行
        // 支持：`CASE v1, v2` / `CASE IS >= n` / `CASE a TO b` / `CASEELSE`
        QVariant valueVar;
        ExpressionEvaluator localEvaluator;
        ExpressionEvaluator& ev = m_evaluator ? *m_evaluator : localEvaluator;
        if (line.condition) {
            valueVar = ev.evaluate(*line.condition, m_storage, baseData());
        } else if (!line.arguments.isEmpty()) {
            const Operand& op = line.arguments.first();
            if (op.isString) {
                valueVar = op.raw;
            } else if (op.ast) {
                valueVar = ev.evaluate(*op.ast, m_storage, baseData());
            } else if (m_table) {
                const QSharedPointer<ExpressionNode> ast = m_table->expressionAst(op.raw);
                valueVar = ast ? ev.evaluate(*ast, m_storage, baseData())
                               : QVariant::fromValue<qint64>(op.raw.toLongLong());
            }
        }
            const bool valueIsStr = (valueVar.typeId() == QMetaType::QString);

        const QList<int> caseLines = m_table->ifBranches(script, pc);
        for (int caseLine : caseLines) {
            const LogicalLine* cl = m_table->lineAt(script, caseLine);
            if (!cl) continue;
            if (cl->is("CASEELSE")) {
                gotoLine(caseLine + 1);          // 默认分支
                return ExecState::Continue;
            }
            if (caseMatches(*cl, valueVar, valueIsStr)) {
                gotoLine(caseLine + 1);
                return ExecState::Continue;
            }
        }
        gotoLine(m_table->jumpTarget(script, pc));   // 无命中 -> ENDSELECT 之后
        return ExecState::Continue;
    }
    if (name == QLatin1String("CASE") || name == QLatin1String("CASEELSE")) {
        // 顺序落入（上一个 CASE 体执行完）-> 跳到 ENDSELECT 之后
        gotoLine(m_table->jumpTarget(script, pc));
        return ExecState::Continue;
    }
    if (name == QLatin1String("ENDSELECT")) {
        advance();
        return ExecState::Continue;
    }

    // ---- CATCH / ENDCATCH（TRYC 系异常块的标记，对齐 C# CATCH_Instruction）----
    // 顺序落入 CATCH 说明 TRYC 体的目标函数**找到了**（没跳走），此时异常体不能执行
    // -> 跳到配对的 ENDCATCH 之后。
    // 「TRYC 失败」的落地在 doCallLine 里直接落到 CATCH 的下一行，不会命中这里。
    if (name == QLatin1String("CATCH")) {
        const int endCatch = m_table->endCatchTarget(script, pc);
        if (endCatch >= 0) gotoLine(endCatch + 1);
        else advance();
        return ExecState::Continue;
    }
    if (name == QLatin1String("ENDCATCH")) {
        advance();
        return ExecState::Continue;
    }

    // ---- THROW（C# THROW_Instruction -> throw new CodeEE）----
    // 语义要点一：Emuera 的 CATCH **不是**通用异常捕获 ——
    //   文档《异常分支：TRYC / CATCH / ENDCATCH》：「用于捕获『函数不存在』的情况」。
    //   所以 THROW 不会被 CATCH 接住（C# 里 JumpToEndCatch 只在「找不到函数」时用）。
    // 语义要点二：THROW 中断本次执行（C# 抛 CodeEE -> 进 Error 状态）。
    //   曾经这里「只告警不中断」以便观察上游求值缺陷；CALLF 字符串实参丢引号、
    //   省略实参整数化 "0"、缺省形参未绑定、ARRAYREMOVE 未实现、CALLF 返回值
    //   覆盖 LOCALS[0] 这一串上游缺陷修复后，eraTW 冒烟已无 THROW，恢复中断语义。
    if (name == QLatin1String("THROW")) {
        QString message;
        if (!line.arguments.isEmpty()) {
            ExpressionEvaluator fallback;
            ExpressionEvaluator& ev = m_evaluator ? *m_evaluator : fallback;
            const Operand& op = line.arguments.first();
            message = op.ast ? ev.evaluate(*op.ast, m_storage, baseData()).toString() : op.raw;
        }
        m_state->setErrorState();
        emit errorOccurred(QStringLiteral("THROW: %1（%2）").arg(message, line.position.toString()));
        return ExecState::Error;
    }

    // ---- RESTART：回到当前函数的第一行（Emuera 的 RESTART 指令）----
    // eraTW 的各类菜单（角色自定义、服装选择、商店…）靠它重绘并重新等待输入；
    // 此前未实现，导致「改完一项后菜单不再刷新」。
    if (name == QLatin1String("RESTART")) {
        const QString fn = line.ownerFunction;
        const int labelLine = fn.isEmpty() ? -1 : m_table->getLabelPosition(script, fn);
        if (labelLine < 0) {
            m_state->setErrorState();
            emit errorOccurred(QStringLiteral("RESTART 找不到当前函数的入口标签（%1）").arg(fn));
            return ExecState::Error;
        }
        qCDebug(eraTrace) << "[exec] RESTART" << fn << "->" << labelLine + 1;
        gotoLine(labelLine + 1);       // 跳过标签行，从函数体第一行重新开始
        return ExecState::Continue;
    }

    if (name == QLatin1String("SIF")) {
        bool cond = false;
        evalCondition(line, cond);
        if (cond) advance();
        else gotoLine(m_table->jumpTarget(script, pc));       // 伪 -> 跳过下一行
        return ExecState::Continue;
    }

    // ---- 循环 ----
    if (name == QLatin1String("REPEAT")) {
        qint64 count = 0;
        writeLoopCounter(QStringLiteral("COUNT"), 0);
        evalInt(line.condition, line.raw, count);
        const int endLine = sd->loopEndLines.value(pc, -1);
        if (count <= 0) {
            gotoLine(endLine >= 0 ? endLine + 1 : pc + 1);
            return ExecState::Continue;
        }
        LoopFrame f;
        f.kind = LoopFrame::Kind::Repeat;
        f.depth = m_table->depth();
        f.script = script;
        f.varName = QStringLiteral("COUNT");
        f.end = count;
        f.startLine = pc;
        f.endLine = endLine;
        m_loops.append(f);
        advance();
        return ExecState::Continue;
    }
    if (name == QLatin1String("DO")) {
        LoopFrame f;
        f.kind = LoopFrame::Kind::Do;
        f.depth = m_table->depth();
        f.script = script;
        f.startLine = pc;
        f.endLine = sd->loopEndLines.value(pc, -1);
        m_loops.append(f);
        advance();
        return ExecState::Continue;
    }
    if (name == QLatin1String("LOOP") && topLoop(LoopFrame::Kind::Do)) {
        const LoopFrame f = *topLoop(LoopFrame::Kind::Do);
        bool cond = false;
        evalCondition(line, cond);
        if (cond) gotoLine(f.startLine + 1);
        else { m_loops.removeLast(); advance(); }
        return ExecState::Continue;
    }
    if (name == QLatin1String("LOOP") || name == QLatin1String("REND")) {
        LoopFrame* f = topLoop(LoopFrame::Kind::Repeat);
        if (f) {
            f->value = std::bit_cast<qint64>(quint64(readIntVar("COUNT", 0)) + 1);
            writeLoopCounter("COUNT", f->value);
            if (f->value < f->end) {
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
            const int endLine = existing->endLine;
            bool cond = false;
            evalCondition(line, cond);
            if (cond) {
                advance();
            } else {
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
            f.depth = m_table->depth();
            f.script = script;
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
            f.depth = m_table->depth();
            f.script = script;
            f.startLine = pc;
            f.endLine = endLine;
            f.varName = bareVarName(ops[0]->raw);
            evalInt(ops[1]->ast, ops[1]->raw, f.value);
            writeLoopCounter(f.varName, f.value);
            evalInt(ops[2]->ast, ops[2]->raw, f.end);
            f.step = 1;
            if (ops.size() >= 4) evalInt(ops[3]->ast, ops[3]->raw, f.step);

            const bool enters = (f.step > 0 && f.value < f.end) || (f.step < 0 && f.value > f.end);
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
            // The body may change the counter (e.g. recheck a shifted Tetris row).
            Operand counter;
            counter.raw = f->varName;
            QString counterName;
            QList<int> counterIndices;
            extractVarRef(counter, counterName, counterIndices);
            f->value = std::bit_cast<qint64>(quint64(readIntVar(counterName, counterIndices)) + quint64(f->step));
            writeLoopCounter(f->varName, f->value);
            const bool more = (f->step > 0 && f->value < f->end) || (f->step < 0 && f->value > f->end);
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
        if (!m_loops.isEmpty() && m_loops.last().depth == m_table->depth()
            && m_loops.last().script == script) {
            const LoopFrame f = m_loops.takeLast();
            if (f.kind == LoopFrame::Kind::Repeat || f.kind == LoopFrame::Kind::For) {
                Operand counter(f.varName);
                QString counterName;
                QList<int> indices;
                extractVarRef(counter, counterName, indices);
                writeIntVar(counterName, indices, std::bit_cast<qint64>(
                    quint64(readIntVar(counterName, indices)) + quint64(f.step)));
            }
            gotoLine(f.endLine >= 0 ? f.endLine + 1 : pc + 1);
        } else {
            advance();
        }
        return ExecState::Continue;
    }
    if (name == QLatin1String("CONTINUE")) {
        if (!m_loops.isEmpty() && m_loops.last().depth == m_table->depth()
            && m_loops.last().script == script) {
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
        // [qdbug] GOTO 跳转跟踪（保留的调试桩）：追查 eraTW 口上选择之后
        // 「ADD_ALL_CHARACTERS 的 FOR 循环未继续、执行直接落到 CHARA_STATE@69」
        // 之类的跳转异常；QT_LOGGING_RULES="era.trace.debug=true" 打开。
        qCDebug(eraTrace) << "[qdbug] GOTO" << label << "from" << line.position.toString();
        return ExecState::Continue;
    }
    // ---- 调用族：CALL / TRYCALL / CALLFORM / TRYCALLFORM / TRYCCALLFORM ----
    // 以前这里**只认 CALL**：CALLFORM / TRYCALL / TRYCALLFORM 都落到
    // 「其它指令」被静默跳过。后果（eraTW 实测）：
    //   * `CALLFORM CUSTOM_%ARGS%_MENU(ARG)` 不执行 -> 「能力/素質/経験の編集」
    //     菜单一片空白（点按钮像没反应）；
    //   * 大量 `TRYCALLFORM 口上_%…%`（956 处）口上全都不显示。
    // 语义对齐 C# CALL_Instruction(form, isJump, isTry, isTryCatch)。
    if (name == QLatin1String("CALL") || name == QLatin1String("TRYCALL")
        || name == QLatin1String("CALLFORM") || name == QLatin1String("TRYCALLFORM")
        || name == QLatin1String("TRYCCALL") || name == QLatin1String("TRYCCALLFORM")) {
        const bool isForm = name.contains(QLatin1String("FORM"));
        const bool isTry  = name.startsWith(QLatin1String("TRY"));
        return doCallLine(line, isForm, isTry);
    }

    // ---- CALLF / CALLFORMF：调用「式中関数」并把返回值写进 RESULT / RESULTS:0 ----
    //   CALLF MAKE_EXIST(CLASS_NAME)        （eraTW 的 EXISTOBJ 系全靠它）
    //   CALLFORMF FUNC_%X%(A, B)
    // 对齐 C# CALLF_Instruction：目标是 function-method（不是 CALL 的标签），
    // 实参照旧求值；**返回值直接丢弃**（C# DoInstruction 只有 mToken.GetValue(exm)）。
    // 此前把字符串返回值写进 LOCALS[0]、整数写进 RESULT —— LOCALS[0] 是局部槽，
    // 会把调用者刚写入的 LOCALS 覆盖掉（eraTW 的 TEMP_RE_STR 因此恒返回空串，
    // GET_STR/OBJ 系全链失效）。
    // 以前这条完全没有实现 -> EXIST 系列函数形同虚设。
    if (name == QLatin1String("CALLF") || name == QLatin1String("CALLFORMF")) {
        QString funcName = line.arguments.isEmpty() ? QString() : line.arguments.first().raw;
        if (name == QLatin1String("CALLFORMF")) funcName = expandCallFormLabel(funcName);
        QStringList argTexts;
        for (int i = 1; i < line.arguments.size(); ++i) {
            const Operand& op = line.arguments.at(i);
            if (op.raw == QLatin1String(",")) continue;
            // 装载期把字符串实参剥掉引号存进 raw（isString 标记）。
            // 这里必须把引号**加回去**再拼成调用表达式，否则 `"VARX"` 会变成
            // 裸标识符按变量求值（未知变量 -> 0），TEMPVAR/OBJ 系全链断掉。
            argTexts << (op.isString
                             ? QLatin1Char('"') + op.raw + QLatin1Char('"')
                             : op.raw);
        }
        const QString callText = funcName + QLatin1Char('(')
                                 + argTexts.join(QLatin1Char(',')) + QLatin1Char(')');
        ExpressionEvaluator localEv;
        ExpressionEvaluator& ev = m_evaluator ? *m_evaluator : localEv;
        const QSharedPointer<ExpressionNode> ast =
            m_table ? m_table->expressionAst(callText) : QSharedPointer<ExpressionNode>();
        const QVariant value = ast ? ev.evaluate(*ast, m_storage, baseData())
                                   : ev.evaluate(callText, m_storage, baseData());
        Q_UNUSED(value);   // 对齐 C#：CALLF 的返回值不落任何寄存器
        qCDebug(eraTrace) << "[callf]" << callText << "->" << value
                          << "行" << line.position.toString();
        advance();
        return ExecState::Continue;
    }

    // ---- RETURNFORM（对齐 C# FunctionCode.RETURNFORM：字符串 FORM 返回）----
    //   RETURNFORM <格式串> -> RESULTS:0 = 展开结果（RESULTS 全局，跨函数共享）
    //   然后与 RETURN 相同地弹栈返回。此前未实现 -> 落「其它指令」静默跳过，
    //   函数返回值丢失。eraTW 基础版枚举（BuiltInFunctionCode.cs）含 RETURNFORM。
    if (name == QLatin1String("RETURNFORM")) {
        ExpressionEvaluator fallback;
        ExpressionEvaluator* ev = m_evaluator ? m_evaluator : &fallback;
        m_lastReturnValue = QVariant::fromValue<qint64>(0);
        if (!line.arguments.isEmpty()) {
            const Operand& op = line.arguments.first();
            QString text;
            if (op.isString) text = op.raw;
            else if (op.ast) {
                const auto resolve = [this](const QString& e) {
                    return m_table ? m_table->expressionAst(e)
                                   : QSharedPointer<ExpressionNode>(); };
                const QSharedPointer<ExpressionNode> form = StrFormParser::parse(op.raw, resolve);
                text = form ? ev->evaluate(*form.staticCast<ExpressionNode>(), m_storage, baseData()).toString()
                            : ev->evaluate(op.raw, m_storage, baseData()).toString();
            } else {
                text = ev->evaluate(op.raw, m_storage, baseData()).toString();
            }
            // RESULTS 全局（C# VariableData.cs:202，跨函数共享）
            m_storage->setGlobalStr1D(QStringLiteral("RESULTS"), 0, text);
            m_storage->setLocalStr(0, text);
        }
        if (!returnFromCall()) return ExecState::Halt;
        return ExecState::Continue;
    }
    if (name == QLatin1String("RETURN") || name == QLatin1String("RETURNF")) {
        m_lastReturnValue = QVariant::fromValue<qint64>(0);
        ExpressionEvaluator fallback;
        ExpressionEvaluator* ev = m_evaluator ? m_evaluator : &fallback;
        if (name == QLatin1String("RETURNF")) {
            if (!line.arguments.isEmpty()) {
                const Operand& op = line.arguments.first();
                m_lastReturnValue = op.isString ? QVariant(op.raw)
                    : op.ast ? ev->evaluate(*op.ast, m_storage, baseData())
                             : ev->evaluate(op.raw, m_storage, baseData());
            }
        } else {
            // Evaluate all return expressions before changing RESULT; later
            // expressions may read the previous RESULT values.
            QList<qint64> values;
            for (const Operand& op : line.arguments) {
                if (op.raw == QLatin1String(",")) continue;
                qint64 value = 0;
                evalInt(op.ast, op.raw, value);
                values.append(value);
            }
            if (values.isEmpty()) values.append(0);
            for (int i = 0; i < values.size(); ++i) {
                m_storage->setSystemVariable("RESULT", i, values[i]);
                m_storage->setGlobalInt1D("RESULT", i, values[i]);
            }
            m_lastReturnValue = QVariant::fromValue<qint64>(values.first());
        }
        // [qdbug] RETURN 跟踪（保留的调试桩）：追查 eraTW「KOJO_MULTIPLE 返回 2 后
        // ADD_ALL_CHARACTERS 的 FOR 循环未继续」类返回/弹栈异常；
        // QT_LOGGING_RULES="era.trace.debug=true" 打开。
        qCDebug(eraTrace) << "[qdbug] RETURN from" << line.position.toString()
                          << "ret =" << m_lastReturnValue;
        if (!returnFromCall()) {
            // 顶层 RETURN：脚本结束
            return ExecState::Halt;
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
            QList<int> ia, ib;
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

    // ---- ARRAYCOPY / ARRAYSHIFT / ARRAYREMOVE / ARRAYSORT（对齐 C# ArrayControl 系）----
    // 此前完全没实现：解析通过但执行期静默跳过。eraTW 的 TEMPVAR.OBJ 系靠
    // ARRAYREMOVE 从临时变量数组里删除元素 —— 空操作导致元素残留、VAR_CNT
    // 涨到上限后 THROW「保持変数が上限に達しています」。
    // 这里实现 1 次元（整数/字符串）数组；角色变量暂不支持（C# 也仅限 1D）。
    if (name == QLatin1String("ARRAYCOPY") || name == QLatin1String("ARRAYSHIFT")
        || name == QLatin1String("ARRAYREMOVE") || name == QLatin1String("ARRAYSORT")) {
        advance();
        QList<Operand> ops;
        for (const Operand& a : line.arguments) {
            if (a.raw != QLatin1String(",")) ops.append(a);
        }
        auto evalOp = [&](int i, qint64 def) -> qint64 {
            if (i >= ops.size()) return def;
            qint64 v = def;
            evalInt(ops.at(i).ast, ops.at(i).raw, v);
            return v;
        };
        // 数组逻辑长度：优先声明长度，其次实际存储长度
        auto length1D = [&](const QString& var) -> int {
            if (m_table) {
                const VariableDecl* decl =
                    m_table->variableTable().find(var, line.ownerFunction);
                if (decl && decl->dimension == 1 && !decl->lengths.isEmpty()
                    && decl->lengths.first() > 0)
                    return decl->lengths.first();
            }
            return qMax(0, m_storage->arraySize(var));
        };
        // 是否字符串数组（按声明类型）
        auto isStrArray = [&](const QString& var) -> bool {
            if (m_table) {
                const VariableDecl* decl =
                    m_table->variableTable().find(var, line.ownerFunction);
                if (decl) return decl->type == OperandType::Str;
            }
            return false;
        };

        if (name == QLatin1String("ARRAYREMOVE")) {
            // ARRAYREMOVE var, start, num：左移 num 个元素，尾部补 0/""（长度不变）
            if (ops.isEmpty()) return ExecState::Continue;
            const QString var = ops.first().raw.trimmed();
            const int len = length1D(var);
            const int start = static_cast<int>(evalOp(1, 0));
            qint64 numN = evalOp(2, -1);
            if (len <= 0 || start < 0 || start >= len || numN == 0) return ExecState::Continue;
            const int num = (numN < 0) ? (len - start) : static_cast<int>(qMin<qint64>(numN, len - start));
            if (isStrArray(var)) {
                QStringList vals;
                for (int i = 0; i < len; ++i) vals << m_storage->getGlobalStr1D(var, i);
                for (int i = start; i < len; ++i)
                    m_storage->setGlobalStr1D(var, i, (i + num < len) ? vals.at(i + num) : QString());
            } else {
                QList<qint64> vals;
                for (int i = 0; i < len; ++i) vals << m_storage->getGlobalInt1D(var, i);
                for (int i = start; i < len; ++i)
                    m_storage->setGlobalInt1D(var, i, (i + num < len) ? vals.at(i + num) : 0);
            }
            return ExecState::Continue;
        }
        if (name == QLatin1String("ARRAYSHIFT")) {
            // ARRAYSHIFT var, shift, def[, start, num]：区间内元素平移，空出的格子填 def
            if (ops.isEmpty()) return ExecState::Continue;
            const QString var = ops.first().raw.trimmed();
            const int len = length1D(var);
            const int shift = static_cast<int>(evalOp(1, 0));
            if (len <= 0 || shift == 0) return ExecState::Continue;
            const int start = static_cast<int>(evalOp(3, 0));
            if (start < 0 || start >= len) return ExecState::Continue;
            qint64 numN = evalOp(4, -1);
            int num = (numN < 0) ? (len - start) : static_cast<int>(qMin<qint64>(numN, len - start));
            if (num <= 0) return ExecState::Continue;
            const bool strArr = isStrArray(var);
            const QString defS = ops.size() > 2 ? (ops.at(2).isString ? ops.at(2).raw : QString())
                                                : QString();
            const qint64 defI = evalOp(2, 0);
            if (strArr) {
                QStringList vals;
                for (int i = 0; i < len; ++i) vals << m_storage->getGlobalStr1D(var, i);
                for (int i = start; i < start + num; ++i) {
                    const int src = i - shift;
                    m_storage->setGlobalStr1D(var, i,
                        (src >= start && src < start + num) ? vals.at(src) : defS);
                }
            } else {
                QList<qint64> vals;
                for (int i = 0; i < len; ++i) vals << m_storage->getGlobalInt1D(var, i);
                for (int i = start; i < start + num; ++i) {
                    const int src = i - shift;
                    m_storage->setGlobalInt1D(var, i,
                        (src >= start && src < start + num) ? vals.at(src) : defI);
                }
            }
            return ExecState::Continue;
        }
        if (name == QLatin1String("ARRAYSORT")) {
            // ARRAYSORT var[, FORWARD|BACK[, start[, num]]]
            if (ops.isEmpty()) return ExecState::Continue;
            const QString var = ops.first().raw.trimmed();
            const int len = length1D(var);
            if (len <= 0) return ExecState::Continue;
            const bool back = (ops.size() > 1
                               && ops.at(1).raw.trimmed().toUpper() == QLatin1String("BACK"));
            const int start = static_cast<int>(evalOp(2, 0));
            qint64 numN = evalOp(3, -1);
            if (start < 0 || start >= len) return ExecState::Continue;
            const int num = (numN < 0) ? (len - start) : static_cast<int>(qMin<qint64>(numN, len - start));
            if (num <= 1) return ExecState::Continue;
            if (isStrArray(var)) {
                QStringList vals;
                for (int i = 0; i < len; ++i) vals << m_storage->getGlobalStr1D(var, i);
                std::sort(vals.begin() + start, vals.begin() + start + num,
                          [&](const QString& a, const QString& b) {
                              return back ? (a > b) : (a < b);
                          });
                for (int i = 0; i < len; ++i) m_storage->setGlobalStr1D(var, i, vals.at(i));
            } else {
                QList<qint64> vals;
                for (int i = 0; i < len; ++i) vals << m_storage->getGlobalInt1D(var, i);
                std::sort(vals.begin() + start, vals.begin() + start + num,
                          [&](qint64 a, qint64 b) { return back ? (a > b) : (a < b); });
                for (int i = 0; i < len; ++i) m_storage->setGlobalInt1D(var, i, vals.at(i));
            }
            return ExecState::Continue;
        }
        // ARRAYCOPY dest, src：整体拷贝（C# CopyArray：src[0..len) -> dest[0..len)）
        if (ops.size() >= 2) {
            const QString dest = ops.at(0).raw.trimmed();
            const QString src = ops.at(1).raw.trimmed();
            const int len = length1D(src);
            const bool strArr = isStrArray(src);
            for (int i = 0; i < len; ++i) {
                if (strArr) {
                    m_storage->setGlobalStr1D(dest, i, m_storage->getGlobalStr1D(src, i));
                } else {
                    m_storage->setGlobalInt1D(dest, i, m_storage->getGlobalInt1D(src, i));
                }
            }
        }
        return ExecState::Continue;
    }

    // ---- WAIT / WAITANYKEY / FORCEWAIT（对齐 C# WAIT_Instruction）----
    //   WAIT       -> Console.ReadAnyKey()        等任意键（可被跳过功能略过）
    //   WAITANYKEY -> Console.ReadAnyKey(true,false)
    //   FORCEWAIT  -> Console.ReadAnyKey(false,true) 跳过功能不能略过的 WAIT
    // 三者都挂起等输入；「跳过模式下的差异」属于输入层能力，此处统一等任意键。
    if (name == QLatin1String("WAIT") || name == QLatin1String("WAITANYKEY")
        || name == QLatin1String("FORCEWAIT")) {
        advance();
        if (m_machine) {
            m_machine->waitAnyKey();
            return m_state->getExecState();
        }
        return ExecState::WaitInput;
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

    // ---- TWAIT <时间ms>[, <跳过标记>]（对齐 C# TWAIT_Instruction）----
    //   暂停 <时间>ms 后自动继续；<跳过标记>!=0 时任意键/点击可提前结束等待。
    //   计时路径完整（挂起 -> 计时器到点恢复）；「按键提前跳过」属于输入层能力，
    //   尚未接线（仅影响能否点掉动画，不影响流程正确性）。
    if (name == QLatin1String("TWAIT")) {
        QSharedPointer<ExpressionNode> timeNode;
        QSharedPointer<ExpressionNode> skipNode;
        if (!line.arguments.isEmpty() && line.arguments.first().ast
            && line.arguments.first().ast->kind() == NodeKind::Function) {
            const auto& fn = static_cast<const FunctionNode&>(*line.arguments.first().ast);
            if (!fn.arguments().isEmpty()) timeNode = fn.arguments().at(0);
            if (fn.arguments().size() >= 2) skipNode = fn.arguments().at(1);
        }
        qint64 ms = 0, skip = 0;
        evalInt(timeNode, QString(), ms);
        evalInt(skipNode, QString(), skip);
        if (ms < 0) ms = 0;
        qCDebug(eraTrace) << "[twait] 等待" << ms << "ms 跳过标记" << skip
                          << "行" << line.position.toString();
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
        if (!returnFromCall()) {
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
// 调用族的统一实现（CALL / TRYCALL / CALLFORM / TRYCALLFORM）
//
// 对齐 C# CALL_Instruction.DoInstruction：
//   * isForm：标签名由格式化串求值（`CUSTOM_%ARGS%_MENU` -> `CUSTOM_ABL_MENU`）
//   * isTry ：找不到函数时静默跳过（不回退、不报错）
// 其余（实参求值、引用形参、固定下标数组展开、私有变量初值）与旧 CALL 一致。
// ---------------------------------------------------------------------------
QString ScriptRunner::expandCallFormLabel(const QString& raw)
{
    QString text = raw.trimmed();
    if (text.isEmpty()) return text;
    if (!StrFormParser::hasForm(text)) return text;   // 纯文本标签：不展开
    const auto resolve = [this](const QString& e) -> QSharedPointer<ExpressionNode> {
        return m_table ? m_table->expressionAst(e) : QSharedPointer<ExpressionNode>();
    };
    const QSharedPointer<StrFormNode> form = StrFormParser::parse(text, resolve);
    if (!form) return text;
    ExpressionEvaluator fallback;
    ExpressionEvaluator& ev = m_evaluator ? *m_evaluator : fallback;
    const QString out = ev.evaluate(*form.staticCast<ExpressionNode>(), m_storage, baseData()).toString();
    return out.isEmpty() ? text : out;
}

ExecState ScriptRunner::doCallLine(const LogicalLine& line, bool isForm, bool isTry)
{
    QString label = line.arguments.isEmpty() ? QString() : line.arguments.first().raw;
    if (isForm) label = expandCallFormLabel(label);
    // [qdbug]（保留的调试桩）：CALL 增加脚本名与源码位置。currentLine() 是
    // 0 基内部行号（比 ERB 源码行号小 1），line.position 才是源码位置；
    // 两者并列便于对照 eraTW 原始代码。
    qDebug() << "[exec] CALL" << (isForm ? "(form)" : "") << label
             << (isTry ? "(try)" : "") << "line" << m_table->currentLine()
             << "src" << line.position.toString()
             << "script" << m_table->currentScript();

    if (label.isEmpty()) {
        m_state->setErrorState();
        emit errorOccurred(QStringLiteral("CALL label not found: (空)"));
        return ExecState::Error;
    }
    // TRY 系：目标不存在就静默跳过（对齐 C# isTry）。
    // 注意用 hasLabel（能命中任意 @/$ 标签）而不是 userFunction，
    // 因为 eraTW 里存在「无 #FUNCTION 的 @label」也照常 CALL。
    // TRYC 系失败的落地：有配对的 CATCH 就从「CATCH 的下一行」开始跑异常体，
    // 没有 CATCH 的话就是普通的 TRY（静默跳过）。
    // 对齐 C# ProcessState 的语义：主循环先 ShiftNextLine 再执行，所以
    // JumpTo(CATCH) 真正的落点是 CATCH 的下一行。
    const auto tryFail = [&]() -> ExecState {
        const QString sc = m_table->currentScript();
        const int catchLine = m_table->catchTarget(sc, m_table->currentLine());
        if (catchLine >= 0) {
            qCDebug(eraTrace) << "[call] TRYC 目标不存在 -> 进入 CATCH" << catchLine + 1 << label;
            m_table->setPosition(sc, catchLine + 1, false);
        } else {
            qDebug() << "[call] TRY* 目标不存在，跳过：" << label;
            m_table->advance();
        }
        return ExecState::Continue;
    };
    if (isTry && !m_table->hasLabel(label)) {
        return tryFail();
    }

    const UserFunctionDecl* info = m_table->userFunction(label);
    QList<Operand> evaluated;
    QHash<QString, QString> references;
    evaluated.reserve(line.arguments.size());
    evaluated.append(line.arguments.value(0));
    for (int i = 1; i < line.arguments.size(); ++i) {
        const Operand source = line.arguments.at(i);
        const UserParamDecl* param = info && i - 1 < info->params.size()
                                         ? &info->params.at(i - 1) : nullptr;
        if (param && param->isReference) {
            const QString actualName = bareVarName(source.raw);
            if (!actualName.isEmpty()) {
                references.insert(param->name.toUpper(), m_storage->resolvedStorageName(actualName));
            }
            evaluated.append(source);
            continue;
        }
        ExpressionEvaluator fallback;
        const QVariant value = source.isString ? QVariant(source.raw)
            : source.ast
                ? (m_evaluator ? m_evaluator : &fallback)->evaluate(*source.ast, m_storage, baseData())
                : (m_evaluator ? m_evaluator : &fallback)->evaluate(source.raw, m_storage, baseData());
        Operand arg(value.toString());
        arg.isString = value.typeId() == QMetaType::QString;
        evaluated.append(arg);
    }
    // Emuera 允许 CALL F(array) 对应 F(array:0, array:1, ...)。
    // 在绑定前按固定下标形参展开，避免数组被错误求值为单个标量。
    if (evaluated.size() == 2 && info && !info->params.isEmpty()) {
        QString actualName = bareVarName(line.arguments.value(1).raw);
        bool expandable = !actualName.isEmpty();
        for (const UserParamDecl& p : info->params)
            expandable = expandable && p.fixedIndex >= 0 && p.varName.compare(actualName, Qt::CaseInsensitive) == 0;
        if (expandable) {
            QList<Operand> expanded;
            const QString storageName = m_storage->resolvedStorageName(actualName);
            expanded.append(evaluated.first());
            for (const UserParamDecl& p : info->params) {
                Operand item;
                item.isString = p.type == OperandType::Str;
                item.raw = item.isString
                    ? m_storage->getGlobalStr1D(storageName, p.fixedIndex)
                    : QString::number(m_storage->getGlobalInt1D(storageName, p.fixedIndex));
                expanded.append(item);
            }
            evaluated = expanded;
        }
    }
    enterCall(label);
    // 顺序对齐 C#：先初始化函数私有变量的初值（#DIM X = 7），再写实参
    m_table->applyPrivateVariableDefaults(label);
    bindArguments(info, evaluated, references);
    if (!m_table->callLabel(label)) {
        if (!m_callContexts.isEmpty()) m_storage->setLocalContext(m_callContexts.takeLast().locals);
        if (isTry) {
            return tryFail();
        }
        m_state->setErrorState();
        emit errorOccurred(QStringLiteral("CALL label not found: %1").arg(label));
        return ExecState::Error;
    }
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
// ---------------------------------------------------------------------------
// SELECTCASE 的 CASE 匹配（对齐 C# CASE_ArgumentBuilder / SelectCaseExpression）
//   CASE v1, v2        -> 任一相等即命中
//   CASE IS <op> expr  -> 用 value <op> expr 判定（op ∈ == != < > <= >=）
//   CASE a TO b        -> a <= value <= b
//   CASEELSE           -> 由调用方处理（默认分支）
// 字符串 SELECTCASE 走字符串比较；数值走整数比较。
// ---------------------------------------------------------------------------
namespace {
// 顶层逗号切分（跳过引号与括号嵌套）—— 供 CASE 的参数列表使用
QStringList splitCaseArgs(const QString& text)
{
    QStringList out;
    QString current;
    int depth = 0;
    QChar quote;
    for (const QChar c : text) {
        if (!quote.isNull()) {
            current += c;
            if (c == quote) quote = QChar();
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; current += c; continue; }
        if (c == QLatin1Char('(') || c == QLatin1Char('[')) { ++depth; current += c; continue; }
        if (c == QLatin1Char(')') || c == QLatin1Char(']')) { --depth; current += c; continue; }
        if (c == QLatin1Char(',') && depth == 0) { out.append(current); current.clear(); continue; }
        current += c;
    }
    if (!current.trimmed().isEmpty()) out.append(current);
    return out;
}
} // namespace

bool ScriptRunner::caseMatches(const LogicalLine& caseLine,
                               const QVariant& valueVar, bool valueIsStr)
{
    QString spec = caseLine.raw.trimmed();
    // 去掉前导 'CASE'
    if (spec.left(4).compare(QLatin1String("CASE"), Qt::CaseInsensitive) == 0) {
        spec = spec.mid(4);
    }
    spec = spec.trimmed();
    if (spec.isEmpty()) return false;

    ExpressionEvaluator localEvaluator;
    ExpressionEvaluator& ev = m_evaluator ? *m_evaluator : localEvaluator;
    const auto evalText = [&](const QString& text) -> QVariant {
        if (m_table) {
            const QSharedPointer<ExpressionNode> ast = m_table->expressionAst(text);
            if (ast) return ev.evaluate(*ast, m_storage, baseData());
        }
        return ev.evaluate(text, m_storage, baseData());
    };

    const auto compare = [&](const QVariant& lhs, const QVariant& rhs) {
        if (valueIsStr) return QString::compare(lhs.toString(), rhs.toString(), Qt::CaseSensitive);
        const qint64 l = lhs.toLongLong(), r = rhs.toLongLong();
        return l < r ? -1 : l > r ? 1 : 0;
    };
    // Each comma-separated item has its own IS / TO grammar. Only recognize
    // keywords outside strings and nested expressions.
    for (const QString& part : splitCaseArgs(spec)) {
        const QString t = part.trimmed();
        if (t.isEmpty()) continue;
        if (t.startsWith("IS ", Qt::CaseInsensitive)) {
            const QString rest = t.mid(3).trimmed();
            static const QStringList ops = {"<=", ">=", "==", "!=", "<", ">"};
            for (const QString& op : ops) {
                if (!rest.startsWith(op)) continue;
                const int cmp = compare(valueVar, evalText(rest.mid(op.size()).trimmed()));
                if ((op == "<=" && cmp <= 0) || (op == ">=" && cmp >= 0)
                    || (op == "==" && cmp == 0) || (op == "!=" && cmp != 0)
                    || (op == "<" && cmp < 0) || (op == ">" && cmp > 0)) return true;
                break;
            }
            continue;
        }
        int to = -1, depth = 0;
        QChar quote;
        for (int i = 0; i < t.size(); ++i) {
            const QChar c = t[i];
            if (!quote.isNull()) {
                if (c == '\\') { ++i; continue; }
                if (c == quote) quote = QChar();
                continue;
            }
            if (c == '\"' || c == '\'') { quote = c; continue; }
            if (c == '(' || c == '[') ++depth;
            if (c == ')' || c == ']') --depth;
            if (depth == 0 && i > 0 && i + 2 < t.size() && t[i-1].isSpace()
                && t.mid(i, 2).compare("TO", Qt::CaseInsensitive) == 0 && t[i+2].isSpace()) {
                to = i;
                break;
            }
        }
        if (to >= 0) {
            if (compare(evalText(t.left(to).trimmed()), valueVar) <= 0
                && compare(valueVar, evalText(t.mid(to + 2).trimmed())) <= 0) return true;
        } else if (compare(valueVar, evalText(t)) == 0) return true;
    }
    return false;
}

qint64 ScriptRunner::readIntVar(const QString& name, int index) const {
    return readIntVar(name, QList<int>{index});
}

qint64 ScriptRunner::readIntVar(const QString& name, const QList<int>& indices) const {
    if (m_storage->hasParameter(name)) return m_storage->parameter(name).toLongLong();
    // 角色数据变量：按 (角色号, 元素下标) 读取（与求值器一致）
    if (m_storage->isCharaDataVariable(name)) {
        if (m_storage->isCharaDataString(name)) return 0;
        int charaId = 0;
        QList<int> elems;
        m_storage->reduceCharaArgs(name, indices, charaId, elems);
        if (m_storage->charaDataDimension(name) >= 2)
            return m_storage->getCharaInt3D(name, charaId, elems.value(0), elems.value(1));
        return m_storage->getCharaInt(name, charaId, elems.value(0));
    }
    const QString upper = name.toUpper();
    if (upper == QLatin1String("ARG")) return m_storage->getArgInt(indices.value(0));
    if (upper == QLatin1String("LOCAL")) return m_storage->getLocalInt(indices.value(0));
    if (m_storage->hasSystemVariable(name)) {
        return m_storage->getSystemVariable(name, indices.value(0));
    }
    return m_storage->getGlobalInt1D(name, indices.value(0));
}

void ScriptRunner::writeIntVar(const QString& name, int index, qint64 value) {
    writeIntVar(name, QList<int>{index}, value);
}

void ScriptRunner::writeIntVar(const QString& name, const QList<int>& indices, qint64 value) {
    if (m_storage->hasParameter(name)) { m_storage->setParameter(name, value); return; }
    // 角色数据变量：按 (角色号, 元素下标) 写入（否则同一角色的元素互相覆盖）
    if (m_storage->isCharaDataVariable(name)) {
        if (m_storage->isCharaDataString(name)) return;
        int charaId = 0;
        QList<int> elems;
        m_storage->reduceCharaArgs(name, indices, charaId, elems);
        if (m_storage->charaDataDimension(name) >= 2)
            m_storage->setCharaInt3D(name, charaId, elems.value(0), elems.value(1), value);
        else
            m_storage->setCharaInt(name, charaId, elems.value(0), value);
        return;
    }
    const QString upper = name.toUpper();
    if (upper == QLatin1String("ARG")) {
        m_storage->setArgInt(indices.value(0), value);
        return;
    }
    if (upper == QLatin1String("LOCAL")) {
        m_storage->setLocalInt(indices.value(0), value);
        return;
    }
    if (m_storage->hasSystemVariable(name)) {
        m_storage->setSystemVariable(name, indices.value(0), value);
        return;
    }
    m_storage->setGlobalInt1D(name, indices.value(0), value);
}

// 顶层 ':' 切分（尊重括号/引号），把 `VAR:a:b` 拆成 [VAR, a, b]
static QStringList splitColonTopLevel(const QString& text) {
    QStringList out;
    QString cur;
    int depth = 0;
    QChar quote;
    for (const QChar c : text) {
        if (!quote.isNull()) { cur += c; if (c == quote) quote = QChar(); continue; }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; cur += c; continue; }
        if (c == QLatin1Char('(') || c == QLatin1Char('[')) { ++depth; cur += c; continue; }
        if (c == QLatin1Char(')') || c == QLatin1Char(']')) { --depth; cur += c; continue; }
        if (c == QLatin1Char(':') && depth == 0) { out.append(cur); cur.clear(); continue; }
        cur += c;
    }
    out.append(cur);
    return out;
}

bool ScriptRunner::extractVarRef(const Operand& op, QString& name, QList<int>& indices) {
    indices.clear();
    if (op.ast && op.ast->kind() == NodeKind::Variable) {
        const VariableNode& var = static_cast<const VariableNode&>(*op.ast);
        name = var.name();
        for (const QSharedPointer<ExpressionNode>& idxNode : var.indices()) {
            if (!idxNode) { indices.append(0); continue; }
            if (m_evaluator) {
                indices.append(static_cast<int>(
                    m_evaluator->evaluate(*idxNode, m_storage, baseData()).toLongLong()));
            } else {
                qint64 v = 0;
                evalInt(idxNode, QString(), v);
                indices.append(static_cast<int>(v));
            }
        }
        return true;
    }
    // 退化：从 raw 文本解析 "NAME[:下标[:下标…]]"（下标可以是变量/表达式）
    QString raw = op.raw.trimmed();
    const QStringList parts = splitColonTopLevel(raw);
    if (parts.size() > 1) {
        for (int i = 1; i < parts.size(); ++i) {
            const QString idxText = parts.at(i).trimmed();
            bool ok = false;
            const int direct = idxText.toInt(&ok);
            if (ok) { indices.append(direct); continue; }
            int value = 0;
            if (m_table) {
                const QSharedPointer<ExpressionNode> ast = m_table->expressionAst(idxText);
                if (ast) {
                    if (m_evaluator) {
                        value = static_cast<int>(
                            m_evaluator->evaluate(*ast, m_storage, baseData()).toLongLong());
                    } else {
                        qint64 v = 0;
                        evalInt(ast, QString(), v);
                        value = static_cast<int>(v);
                    }
                }
            }
            indices.append(value);
        }
        raw = parts.first();
    }
    name = bareVarName(raw);
    return !name.isEmpty();
}

bool ScriptRunner::extractVarRef(const Operand& op, QString& name, int& index) {
    QList<int> indices;
    const bool ok = extractVarRef(op, name, indices);
    index = indices.value(0);
    return ok;
}

void ScriptRunner::writeLoopCounter(const QString& rawName, qint64 value) {
    Operand counter(rawName);
    QString name;
    QList<int> indices;
    if (extractVarRef(counter, name, indices)) writeIntVar(name, indices, value);
}

void ScriptRunner::bindArguments(const UserFunctionDecl* info, const QList<Operand>& callArgs,
                                 const QHash<QString, QString>& references) {
    const auto bindOne = [this, &references](const UserParamDecl* p, int position,
                                bool argIsString, const QString& strValue, qint64 intValue) {
        switch (p ? p->target : UserParamTarget::Unknown) {
        case UserParamTarget::Arg:
            m_storage->setArgInt(p->index, intValue);
            // ARG:0 也可以写成裸 ARG（求值器已知），同时登记别名便于形参名解析
            m_storage->setLocalAlias(p->name, p->index);
            break;
        case UserParamTarget::Args:
            m_storage->setArgStr(p->index, argIsString ? strValue : QString::number(intValue));
            m_storage->setLocalAlias(p->name, p->index);
            break;
        case UserParamTarget::LocalVar:
            if (p && p->isReference && references.contains(p->name.toUpper())) {
                m_storage->setReference(p->varName, references.value(p->name.toUpper()));
                break;
            }
            if (p && p->fixedIndex >= 0) {
                // 元素形参（`@F(A:0, A:1)`）：实参写进 A 的这个元素
                const QString vn = p->varName;
                const int dim = (m_table && m_table->variableTable().find(vn))
                                    ? m_table->variableTable().find(vn)->dimension : 1;
                if (p->type == OperandType::Str) {
                    m_storage->setGlobalStr1D(vn, p->fixedIndex,
                                              argIsString ? strValue : QString::number(intValue));
                } else if (dim >= 2 && p->fixedIndices.size() >= 2) {
                    m_storage->setGlobalInt2D(vn, p->fixedIndices.at(0), p->fixedIndices.at(1),
                                              intValue);
                } else {
                    m_storage->setGlobalInt1D(vn, p->fixedIndex, intValue);
                }
                break;
            }
            m_storage->setParameter(p->varName.isEmpty() ? p->name : p->varName,
                p->type == OperandType::Str ? QVariant(argIsString ? strValue : QString::number(intValue))
                                            : QVariant(intValue));
            break;
        case UserParamTarget::Unknown:
        default:
            // 未归类（或无声明）：退化为按位置绑定到 ARG/ARGS
            if (argIsString) m_storage->setArgStr(position, strValue);
            else             m_storage->setArgInt(position, intValue);
            break;
        }
    };

    m_storage->clearLocalAliases();
    const int supplied = qMax(0, callArgs.size() - 1);
    for (int position = 0; position < supplied; ++position) {
        const Operand& a = callArgs.at(position + 1);
        qint64 intValue = 0;
        if (!a.isString) evalInt(a.ast, a.raw, intValue);

        const UserParamDecl* param = nullptr;
        if (info && position < info->params.size()) param = &info->params.at(position);
        bindOne(param, position, a.isString, a.raw, intValue);
    }
    if (!info) return;
    for (int position = supplied; position < info->params.size(); ++position) {
        const UserParamDecl& param = info->params.at(position);
        if (!param.hasDefault) continue;
        const bool stringDefault = param.type == OperandType::Str && !param.defaultStr.isEmpty();
        QString text = param.defaultStr;
        if (stringDefault && text.size() >= 2 && text.startsWith(QLatin1Char('"'))
            && text.endsWith(QLatin1Char('"'))) text = text.mid(1, text.size() - 2);
        bindOne(&param, position, stringDefault, text, param.defaultInt);
    }
}

bool ScriptRunner::invokeUserFunction(const QString& name, const QList<QVariant>& args,
                                      const QList<const ExpressionNode*>& argNodes, QVariant& out) {
    const UserFunctionDecl* info = m_table->userFunction(name);
    if (!info || !info->isMethod) {
        return false;   // 非用户函数（交给内置函数）
    }

    // 暂停期间的同步求值：调用方（如调试命令 `:e`）在状态机停下时要求一个表达式的值。
    // 此时 ExecState 不是 Continue，下面的驱动循环会一行都不执行而直接返回 0
    // —— 表现为「用户函数恒为 0」。临时置回 Continue，跑完函数体后恢复现场。
    const bool wasSuspended = !m_state->isRunning();
    const ExecState savedExecState = m_state->getExecState();
    if (wasSuspended) m_state->setExecState(ExecState::Continue);

    // 保存当前位置 / 别名快照
    const QString savedScript = m_table->currentScript();
    const int savedPc = m_table->currentLine();
    const int savedDepth = m_table->depth();
    const int savedLoops = m_loops.size();
    const int savedContextDepth = m_callContexts.size();

    // 绑定实参（按声明的形参表：ARG/ARGS/私有变量）
    enterCall(name);
    m_table->applyPrivateVariableDefaults(name);
    m_storage->clearLocalAliases();
    for (int i = 0; i < args.size(); ++i) {
        const QVariant& v = args.at(i);
        const bool isStr = (v.typeId() == QMetaType::QString);
        const bool omitted = !v.isValid();   // 省略实参（evaluateFunction 记为无效 QVariant）
        const UserParamDecl* param = (i < info->params.size()) ? &info->params.at(i) : nullptr;
        // `#DIM REF` 引用形参：实参必须是裸变量名（对齐语句 CALL 路径的
        // references 机制）—— 把形参名别名到调用方变量的存储键，函数体内
        // 对形参的读写直接落到调用方变量。eraTW 的 画像合成(グラフィックID
        // 为 REF) 全靠这个把分配到的 G 编号写回调用者。
        if (param && param->isReference && param->target == UserParamTarget::LocalVar
            && argNodes.value(i)) {
            const auto* argVarNode = dynamic_cast<const VariableNode*>(argNodes.at(i));
            if (argVarNode && argVarNode->indices().isEmpty()) {
                const QString actualName = argVarNode->name();
                if (!actualName.isEmpty()) {
                    m_storage->setReference(param->varName,
                                            m_storage->resolvedStorageName(actualName));
                    continue;
                }
            }
        }
        switch (param ? param->target : UserParamTarget::Unknown) {
        case UserParamTarget::Arg:
            m_storage->setArgInt(param->index, omitted ? 0 : v.toLongLong());
            m_storage->setLocalAlias(param->name, param->index);
            break;
        case UserParamTarget::Args:
            // 省略 -> 空串（对齐 C# 缺省值）；此前整数化成 "0" 污染字符串形参
            m_storage->setArgStr(param->index, omitted ? QString() : v.toString());
            m_storage->setLocalAlias(param->name, param->index);
            break;
        case UserParamTarget::LocalVar:
            if (param->fixedIndex >= 0) {
                m_storage->setGlobalInt1D(param->varName, param->fixedIndex,
                                          omitted ? 0 : v.toLongLong());
                break;
            }
            m_storage->setParameter(param->varName.isEmpty() ? param->name : param->varName,
                omitted ? QVariant(param->type == OperandType::Str ? QString() : QVariant::fromValue<qint64>(0))
                        : v);
            break;
        case UserParamTarget::Unknown:
        default:
            m_storage->setArgInt(i, omitted ? 0 : v.toLongLong());
            if (omitted || isStr) m_storage->setArgStr(i, omitted ? QString() : v.toString());
            break;
        }
    }
    // 缺省形参（`@F(A, OP = "NORMAL")`）：实参没给到的位置按声明缺省值绑定。
    // 此前只在语句 CALL 路径（bindArguments）处理，表达式调用（CALLF/式中调用）
    // 漏掉了 —— eraTW 的 TEMP_VARCLEAR(ARGS:0, OP = "NORMAL") 的 OP 恒为空串，
    // SELECTCASE 一个 CASE 都不命中，TEMPVAR 清除链整体失效。
    for (int position = args.size(); position < info->params.size(); ++position) {
        const UserParamDecl& param = info->params.at(position);
        if (!param.hasDefault) continue;
        const bool stringDefault = param.type == OperandType::Str && !param.defaultStr.isEmpty();
        QString text = param.defaultStr;
        if (stringDefault && text.size() >= 2 && text.startsWith(QLatin1Char('"'))
            && text.endsWith(QLatin1Char('"'))) text = text.mid(1, text.size() - 2);
        switch (param.target) {
        case UserParamTarget::Arg:
            m_storage->setArgInt(param.index, param.defaultInt);
            m_storage->setLocalAlias(param.name, param.index);
            break;
        case UserParamTarget::Args:
            m_storage->setArgStr(param.index, stringDefault ? text : QString());
            m_storage->setLocalAlias(param.name, param.index);
            break;
        case UserParamTarget::LocalVar:
            if (param.fixedIndex >= 0) {
                m_storage->setGlobalInt1D(param.varName, param.fixedIndex, param.defaultInt);
                break;
            }
            m_storage->setParameter(param.varName.isEmpty() ? param.name : param.varName,
                stringDefault ? QVariant(text) : QVariant::fromValue<qint64>(param.defaultInt));
            break;
        case UserParamTarget::Unknown:
        default:
            m_storage->setArgInt(position, param.defaultInt);
            if (stringDefault) m_storage->setArgStr(position, text);
            break;
        }
    }

    // 以 savedPc 为返回地址压帧并进入函数体
    if (!m_table->callLabelWithReturn(info->name, savedPc)) {
        m_storage->setLocalContext(m_callContexts.takeLast().locals);
        return false;
    }

    // 同步跑完该函数（直到返回帧被弹出）
    const bool wasRunning = m_running;
    m_running = true;
    if (!wasRunning) m_steps = 0;
    while (m_state->isRunning() && m_table->depth() > savedDepth) {
        if (!stepOnce()) {
            break;   // 函数内挂起（如 INPUT）：无法同步返回
        }
    }
    m_running = wasRunning;
    // 恢复挂起前的执行状态（函数体已跑完，调用方仍需保持暂停）
    if (wasSuspended) m_state->setExecState(savedExecState);

    m_loops.resize(savedLoops);
    // 恢复别名快照（正常 RETURN 已弹出一层；这里兜底）
    while (m_callContexts.size() > savedContextDepth) {
        m_storage->setLocalContext(m_callContexts.takeLast().locals);
    }

    const bool completed = (m_table->depth() == savedDepth);
    if (!completed) {
        while (m_table->depth() > savedDepth) m_table->returnFromCall();
        m_table->setPosition(savedScript, savedPc, false);
        return false;
    }

    out = m_lastReturnValue;
    return true;
}
