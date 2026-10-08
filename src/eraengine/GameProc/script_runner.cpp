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
#include "ast/ast_builder.h"
#include <QDebug>
#include <QElapsedTimer>
#include <bit>

namespace {
// 逗号形态的限时输入指令（`TINPUT 300,1234,0,""`）里，AstBuilder 会把**分隔用的
// 逗号本身**也放进操作数列表：
//     [300] [,] [1234] [,] [0] [,] [""]     （7 个操作数）
// 于是按位置取参会整体错位 —— 缺省值读到的其实是 `,`（表达式求值失败 -> 保持 0），
// 超时就交付 0 / 空串，而不是脚本写的 1234 / "abc"。
// 实测（最小脚本 `TINPUT 300,1234,0,""`）：修复前 RESULT=0，应为 1234。
// C# 没有这个问题：TINPUT 的参数是 SpTInputsArgument 的具名字段 Time/Def/Disp/Timeout。
// 这里按位置取参前先把「纯逗号」操作数剔除。
QList<Operand> inputArgs(const LogicalLine& line) {
    QList<Operand> out;
    out.reserve(line.arguments.size());
    for (const Operand& o : line.arguments) {
        if (o.raw == QLatin1String(",")) continue;
        out.append(o);
    }
    return out;
}

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
        m_lastPrivateScope.clear();   // 上下文被恢复 -> 强制下一步重算私有作用域
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

// 求值器统一入口：注入的优先，否则惰性创建 fallback（只服务未接线的裸测环境）
ExpressionEvaluator& ScriptRunner::getEvaluator() {
    if (!m_evaluator && !m_fallbackEvaluator)
        m_fallbackEvaluator = std::make_unique<ExpressionEvaluator>();
    return m_evaluator ? *m_evaluator : *m_fallbackEvaluator;
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
    m_eventChain.active = false;   // 停止时放弃未完成的 CALLEVENT 事件链
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
    // #LOCALSIZE：本函数声明的 LOCAL/LOCALS 尺寸（只扩不缩，静态值保留）
    const UserFunctionDecl& decl = m_table->userFunctions().value(function.toUpper());
    if (decl.localSize > 0) m_storage->ensureLocalSize(decl.localSize);
    // perf：私有名字表按函数缓存（localNamesOfRef），免去每次调用重建 QStringList
    m_storage->setPrivateScope(function,
                               m_table->variableTable().localNamesOfRef(function));
    m_lastPrivateScope = function;   // 与 stepOnce 的缓存 guard 同步
}

bool ScriptRunner::returnFromCall() {
    // C# ProcessState.Return：JUMP 帧弹出后**立刻递归 Return** —— JUMP 不产生
    // 新的返回层，被跳转函数 RETURN 时连同「被替换」的当前帧一起弹出。
    // 此前只弹一层，被 JUMP 替换的帧残留在栈上，后续 $ 标签/返回地址全部错位。
    while (m_table->depth() > 0 && m_table->currentFrame().isJump) {
        if (!m_callContexts.isEmpty()
            && m_callContexts.last().depth == m_table->depth() - 1) {
            const auto context = m_callContexts.takeLast();
            m_functionLocals.insert(context.function, m_storage->localContext());
            m_storage->setLocalContext(context.locals);
            m_lastPrivateScope.clear();   // 上下文被恢复 -> 强制重算私有作用域
            m_loops.resize(context.loops);
        }
        m_table->returnFromCall();
    }
    if (!m_table->returnFromCall()) return false;
    if (!m_callContexts.isEmpty() && m_callContexts.last().depth == m_table->depth()) {
        const auto context = m_callContexts.takeLast();
        m_functionLocals.insert(context.function, m_storage->localContext());
        m_storage->setLocalContext(context.locals);
        m_lastPrivateScope.clear();   // 上下文被恢复 -> 强制重算私有作用域
        m_loops.resize(context.loops);
    }
    return true;
}

bool ScriptRunner::stepOnce() {
    // ---- CALLEVENT 事件链推进：前一个事件函数返回到续点（对齐 C# Return 后的
    //      事件函数序列）。链未空 -> 调下一个事件函数；链跑完 -> 续点行照常执行。
    if (maybeContinueEventChain()) {
        return true;
    }
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
        // 其余情况缺省 0。对齐 C# Process.ScriptProc.cs:67 state.Return(0)：
        // 自然结束**不写 RESULT**（Return/ReturnF 本身不落 RESULT，eraTW 依赖
        // `SIF 用户函数(RESULT)` 后继续读 RESULT —— NOUMIN 选地主的
        // `FLAG:地主 = RESULT` 在此被清 0 导致地主无法选择）。
        const UserFunctionDecl* finfo =
            m_table->userFunction(m_table->currentFrame().callLabel);
        if (finfo && finfo->isMethod && finfo->returnsString())
            m_lastReturnValue = QVariant(QString());
        else
            m_lastReturnValue = QVariant::fromValue<qint64>(0);
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
    if (m_qdbugTrace
        && (m_qdbugTraceFile.isEmpty() || line.position.filename.contains(m_qdbugTraceFile))) {
        qCDebug(eraTrace) << "[qdbug] line" << line.position.toString()
                          << "|" << line.raw.trimmed().left(60)
                          << "| depth" << m_table->depth();
    }
    // 函数体边界（C# FunctionLabelLine 终止上一个函数体）：
    // 落入的不是本帧入口的 @label = 上一函数已自然结束、无 RETURN/RETURNF。
    // #FUNCTIONS 缺省返回**空字符串**（eraTW 依赖 STRLENS(GET_TALENTNAME(...))
    // 过滤无值素質），其余缺省 0。同上：自然结束不写 RESULT（对齐 C#）。
    if (line.kind == LineKind::FunctionLabel && m_table->depth() > 0
        && m_table->currentFrame().entryLine != pc) {
        const UserFunctionDecl* finfo = m_table->userFunction(m_table->currentFrame().callLabel);
        if (finfo && finfo->isMethod && finfo->returnsString())
            m_lastReturnValue = QVariant(QString());
        else
            m_lastReturnValue = QVariant::fromValue<qint64>(0);
        if (returnFromCall()) {
            return true;
        }
        m_state->requestHalt();
        emit finished();
        return false;
    }
    // System entry points are not entered through CALL. Resolve private storage
    // from the executing owner as well, including resumed SHOW_SHOP frames.
    // perf：本函数每条指令都会进来 —— 私有作用域只在**换函数**时才需要重算
    // （声明表装载后不可变；enterCall/returnFromCall 换上下文时 owner 必然变化）。
    if (line.ownerFunction != m_lastPrivateScope) {
        m_storage->setPrivateScope(
            line.ownerFunction,
            m_table->variableTable().localNamesOfRef(line.ownerFunction));
        m_lastPrivateScope = line.ownerFunction;
    }
    ExecState r = executeLine(line);
    if (m_state->getExecState() == ExecState::Error) r = ExecState::Error;

    if (r == ExecState::Continue) {
        emit instructionExecuted(script, pc);
        return true;
    }

    // 挂起 / 结束
    m_state->setExecState(r);
    if ((r == ExecState::WaitInput || r == ExecState::WaitSystemInput)
        && m_waitNotifiesUser) {
        // m_waitKind：任意键系等待（打印系 W 后缀）统一报 ANYKEY（点击任意处/回车）
        emit inputRequested(m_waitKind.isEmpty() ? line.functionName : m_waitKind,
                            m_waitDefault);
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
    ExpressionEvaluator* ev = &getEvaluator();
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

// ---------------------------------------------------------------------------
// PRINTDATA 段（C# PRINT_DATA_Instruction / ErbLoader.cs 的 dataList 构建）
//   * PRINTDATA(|K)(|D)(|L)(|W)：语句形式，从随机的「段」里选一段打印；可选整型
//     变量实参写入选中下标；…L/…W 额外换行（W 再等一次按键）。
//   * 每个 DATAFORM/DATA 行自成一个段；DATALIST..ENDLIST 之间的 DATA 并成一段。
//   * ENDDATA 之后继续（跳转目标 = ENDDATA 下一行）。
// ---------------------------------------------------------------------------
namespace {

bool isPrintDataName(const QString& n) {
    static const char* const kNames[] = {"PRINTDATA",  "PRINTDATAL",  "PRINTDATAW",
                                         "PRINTDATAD", "PRINTDATADL", "PRINTDATADW",
                                         "PRINTDATAK", "PRINTDATAKL", "PRINTDATAKW"};
    for (const char* s : kNames) {
        if (n == QLatin1String(s)) return true;
    }
    return false;
}

// STRDATA <字符串变量>：与 PRINTDATA 共用段结构，但**不显示**：把被选中段的
// 文本写进变量（C# STRDATA = PRINTDATA 的不显示版，ecd 文档「未整理项目」）。
// 之前 STRDATA 不在名单里 -> 整段被当普通行逐条执行 -> DATA/ENDDATA 报 [未完成]。
bool isStrDataName(const QString& n) {
    return n == QLatin1String("STRDATA") || n == QLatin1String("STRDATAL")
           || n == QLatin1String("STRDATAW");
}

// 首次执行 PRINTDATA 时扫描其数据段（行内容装载后不可变，缓存进 LogicalLine）
void buildPrintDataBlock(const LogicalLine& line, int pc, const ScriptData* sd) {
    QList<QList<int>> groups;
    int endLine = -1;
    bool inList = false;
    if (sd) {
        for (int i = pc + 1; i < sd->lines.size(); ++i) {
            const LogicalLine& l = sd->lines.at(i);
            const QString& fn = l.functionName;
            if (fn == QLatin1String("ENDDATA")) {
                endLine = i;
                break;
            }
            if (fn == QLatin1String("DATALIST")) {
                inList = true;
                groups.append(QList<int>());
                continue;
            }
            if (fn == QLatin1String("ENDLIST")) {
                inList = false;
                continue;
            }
            if (fn == QLatin1String("DATA") || fn == QLatin1String("DATAFORM")) {
                if (inList) {
                    if (groups.isEmpty()) groups.append(QList<int>());
                    groups.last().append(i);
                } else {
                    groups.append(QList<int>{i});
                }
                continue;
            }
            break;   // 非数据段成员：结束扫描
        }
    }
    line.printDataGroups = groups;
    line.printDataEndLine = endLine;
    line.printDataReady = true;
}

} // namespace

ExecState ScriptRunner::executeLine(const LogicalLine& line) {
    const QString script = m_table->currentScript();
    const ScriptData* sd = m_table->script(script);
    if (!sd) {
        return ExecState::Error;
    }
    const int pc = m_table->currentLine();
    // 每行重置：只有 AWAIT / TWAIT skip!=0（纯计时，C# InputType.Void）会覆盖为 false
    m_waitNotifiesUser = true;
    m_waitKind.clear();
    m_waitDefault = QVariant();

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
            // 自然结束不写 RESULT（对齐 C# Process.ScriptProc.cs:67 Return(0)）；
            // #FUNCTIONS 缺省返回空串（其余同上）。此前在此清 RESULT 会把
            // `SIF 用户函数(RESULT)` 之后的 `FLAG:地主 = RESULT` 清成 0。
            {
                const UserFunctionDecl* finfo =
                    m_table->userFunction(m_table->currentFrame().callLabel);
                if (finfo && finfo->isMethod && finfo->returnsString())
                    m_lastReturnValue = QVariant(QString());
            }
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
        ExpressionEvaluator& ev = getEvaluator();
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

    // ---- ASSERT（C# ASSERT_Instruction / FunctionCode.ASSERT）----
    // 运行期断言：条件为 0 时抛 CodeEE（「ASSERT の条件が不正です」），
    // 非 0 静默通过。此前只被「已知指令名」兜住、运行期落到「其它指令」被忽略
    // —— 断言形同虚设（组 17 的 `ASSERT 1 == 1` 一度是「不报错」而非「通过」）。
    if (name == QLatin1String("ASSERT")) {
        // 实参形态是 INT_EXPRESSION（argument_parser.h:180），条件就是第一个操作数
        // （不是 IF 系的 line.condition —— 此前误用 evalCondition 恒为假）。
        qint64 v = 0;
        if (!line.arguments.isEmpty()) {
            evalInt(line.arguments.first().ast, line.arguments.first().raw, v);
        }
        if (v == 0) {
            m_state->setErrorState();
            emit errorOccurred(QStringLiteral("ASSERT 的条件不成立：%1（%2）")
                                   .arg(line.raw.trimmed(), line.position.toString()));
            return ExecState::Error;
        }
        advance();
        return ExecState::Continue;
    }

    // ---- SKIPLOG（EE）：设置「消息跳过中」状态（C# console.MesSkip = n != 0）----
    // 非 0 -> 进入跳过：可跳过的任意键等待（WAIT/WAITANYKEY/PRINTW 系）自动放行；
    // 0    -> 强制解除。INPUT 族（需要输入值）与 FORCEWAIT（不可跳过）会把跳过
    // 状态清掉（对齐 C# EmueraConsole 的 MesSkip 循环语义）。
    if (name == QLatin1String("SKIPLOG")) {
        qint64 v = 0;
        if (!line.arguments.isEmpty()) {
            evalInt(line.arguments.first().ast, line.arguments.first().raw, v);
        }
        if (m_engine) m_engine->setMesSkip(v != 0);
        qCDebug(eraTrace) << "[skiplog]" << v << "行" << line.position.toString();
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
            ExpressionEvaluator& ev = getEvaluator();
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

    // ---- QUIT（对齐 C# FunctionCode.QUIT：结束本次执行）
    // 此前 QUIT 只被识别为指令名、没有任何执行分支 —— 脚本里的 QUIT
    // 被静默忽略，程序不会退出（自动化跑测试时表现为「菜单被反复重跑」）。
    if (name == QLatin1String("QUIT")) {
        m_state->requestQuit();   // 结束程序（Halt 会被系统层当成「回到底层」）
        emit finished();
        return ExecState::Halt;
    }

    // ---- EE QUIT 族（v11）：QUIT_AND_RESTART / FORCE_QUIT / FORCE_QUIT_AND_RESTART ----
    //   QUIT_AND_RESTART       = QUIT + 重启标志（C# Program.rebootFlag；
    //                            退出流程收到回车后 Reboot() 重载本目录）
    //   FORCE_QUIT             = 不等待输入立即退出（C# Console.ForceQuit()）
    //   FORCE_QUIT_AND_RESTART = 不等待输入立即退出 + 重启标志
    // 与 C# 的差别（有意为之，注释存档）：C# 的 QUIT 会先停在一个「按回车继续」
    // 的等待上，FORCE_* 跳过该等待。本移植的 QUIT 本就是同步结束（不插等待），
    // 因此 QUIT 与 FORCE_QUIT 在此等价；重启请求由 restartRequested 标志区分，
    // 宿主收到 restartRequestedByScript 后 reload()（等价 C# Reboot）。
    if (name == QLatin1String("QUIT_AND_RESTART") || name == QLatin1String("FORCE_QUIT")
        || name == QLatin1String("FORCE_QUIT_AND_RESTART")) {
        if (name == QLatin1String("QUIT_AND_RESTART")
            || name == QLatin1String("FORCE_QUIT_AND_RESTART")) {
            m_state->requestRestart();   // = requestQuit() + 重启标志
        } else {
            m_state->requestQuit();
        }
        qCDebug(eraTrace) << "[quit]" << name << "重启标志" << m_state->restartRequested()
                          << "行" << line.position.toString();
        emit finished();
        return ExecState::Halt;
    }

    // ---- 跳转 / 调用 ----
    if (name == QLatin1String("GOTO")) {
        QString label = line.arguments.isEmpty() ? QString() : line.arguments.first().raw;
        label = label.trimmed();
        if (label.startsWith(QLatin1Char('$'))) label.remove(0, 1);
        // $ 标签只在**当前函数**内解析（对齐 C# LabelDictionary.GetLabelDollar）。
        // 此前走 jumpToLabel（脚本级 labelPositions 表）：同一脚本里多个函数
        // 都有同名 $ 标签时（eraTW COMMON.ERB 有 4 个 $INPUT_LOOP），后注册的
        // 覆盖先注册的 —— GOTO 跳进**别的函数**继续执行（「进入了错误的内存」，
        // 强くてニューゲーム.ERB 的 @CHOICE 非法输入路径实测触发）。
        const int pos = label.isEmpty() ? -1 : findGotoLabelInFunction(label, line.ownerFunction);
        if (pos < 0) {
            m_state->setErrorState();
            emit errorOccurred(QStringLiteral("GOTO label not found: %1").arg(label));
            return ExecState::Error;
        }
        m_table->setPosition(script, pos, true);
        // [qdbug] GOTO 跳转跟踪（保留的调试桩）：追查 eraTW 口上选择之后
        // 「ADD_ALL_CHARACTERS 的 FOR 循环未继续、执行直接落到 CHARA_STATE@69」
        // 之类的跳转异常；QT_LOGGING_RULES="era.trace.debug=true" 打开。
        qCDebug(eraTrace) << "[qdbug] GOTO" << label << "from" << line.position.toString();
        return ExecState::Continue;
    }
    // ---- CALLEVENT / TRYCALLEVENT（对齐 C# CALLEVENT_Instruction）----
    // 依次调用同名事件函数（#PRI -> 普通 -> #LATER；#SINGLE 且 RETURN 1 跳过
    // 剩余；#ONLY 返回后终止），全部返回后回到 CALLEVENT 的下一行。
    // 目标不存在 -> 静默跳过；目标是普通函数 -> 报错（C# CodeEE）。
    if (name == QLatin1String("CALLEVENT") || name == QLatin1String("TRYCALLEVENT")) {
        const QString label = line.arguments.isEmpty()
                                  ? QString() : line.arguments.first().raw.trimmed();
        if (startEventCallChain(label)) {
            return ExecState::Continue;
        }
        advance();
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

    // ---- JUMP 系：JUMP / JUMPFORM / TRYJUMP / TRYJUMPFORM ----
    // 对齐 C# CALL_Instruction(isJump=true)：跳转但**不产生新的返回地址**，
    // 目标函数 RETURN 时直接回到当前函数的调用者。此前这四条完全未实现，
    // 落到「其它指令」被静默跳过。
    if (name == QLatin1String("JUMP") || name == QLatin1String("TRYJUMP")
        || name == QLatin1String("JUMPFORM") || name == QLatin1String("TRYJUMPFORM")) {
        const bool isForm = name.contains(QLatin1String("FORM"));
        const bool isTry  = name.startsWith(QLatin1String("TRY"));
        return doCallLine(line, isForm, isTry, true);
    }

    // ---- GOTOFORM / TRYGOTO / TRYGOTOFORM（C# GOTO_Instruction：$ 标签跳转）----
    // GOTO 家族的变体：FORM 系的标签名可以是格式化串；目标只在**当前函数内**
    // 的 $ 标签里找（C# LabelDictionary.GetLabelDollar(func.ParentLabelLine)）。
    // TRY 系找不到目标时静默跳过（有配对 CATCH 则落到 CATCH 下一行）。
    // 跳转不压栈 —— 没有返回地址。
    if (name == QLatin1String("GOTOFORM") || name == QLatin1String("TRYGOTO")
        || name == QLatin1String("TRYGOTOFORM")) {
        QString label = line.arguments.isEmpty() ? QString() : line.arguments.first().raw;
        if (name != QLatin1String("TRYGOTO")) label = expandCallFormLabel(label);
        label = label.trimmed();
        if (label.startsWith(QLatin1Char('$'))) label.remove(0, 1);
        const int pos = label.isEmpty() ? -1 : findGotoLabelInFunction(label);
        if (pos >= 0) {
            m_table->setPosition(script, pos, true);
            return ExecState::Continue;
        }
        const int catchLine = m_table->catchTarget(script, pc);
        if (catchLine >= 0) {
            m_table->setPosition(script, catchLine + 1, false);
        } else if (name == QLatin1String("GOTOFORM")) {
            m_state->setErrorState();
            emit errorOccurred(QStringLiteral("GOTO label not found: %1").arg(label));
            return ExecState::Error;
        } else {
            advance();
        }
        return ExecState::Continue;
    }

    // ---- TRYCJUMP / TRYCJUMPFORM / TRYCGOTO / TRYCGOTOFORM 的 C 变体 ----
    // 对齐 C# CALL_Instruction(isJump/isTry=true, isTryCatch=true)：JUMP/GOTO 的
    // TRYC 版本，目标不存在时落到配对 CATCH。此前未分发、被静默跳过。
    if (name == QLatin1String("TRYCJUMP") || name == QLatin1String("TRYCJUMPFORM")) {
        const bool isForm = name.contains(QLatin1String("FORM"));
        return doCallLine(line, isForm, true, true);
    }
    if (name == QLatin1String("TRYCGOTO") || name == QLatin1String("TRYCGOTOFORM")) {
        // 语法/跳转语义与 TRYGOTO(FORM) 相同（TRYCGOTO 的 CATCH 配对在
        // findGotoLabelInFunction 失败分支里处理）。
        QString label = line.arguments.isEmpty() ? QString() : line.arguments.first().raw;
        if (name == QLatin1String("TRYCGOTOFORM")) label = expandCallFormLabel(label);
        label = label.trimmed();
        if (label.startsWith(QLatin1Char('$'))) label.remove(0, 1);
        const int pos = label.isEmpty() ? -1 : findGotoLabelInFunction(label);
        if (pos >= 0) {
            m_table->setPosition(script, pos, true);
            return ExecState::Continue;
        }
        const int catchLine = m_table->catchTarget(script, pc);
        if (catchLine >= 0) {
            m_table->setPosition(script, catchLine + 1, false);
        } else {
            advance();
        }
        return ExecState::Continue;
    }

    // ---- TRYCALLLIST / TRYJUMPLIST / TRYGOTOLIST（GOTOLIST 为宽松别名）----
    // 依次尝试体内 FUNC 条目，命中即去；全部失败 -> ENDFUNC 之后
    // （对齐 C# doFlowControlFunction 的 callList 遍历 + state.JumpTo(func.JumpTo)）。
    if (name == QLatin1String("TRYCALLLIST") || name == QLatin1String("TRYJUMPLIST")
        || name == QLatin1String("TRYGOTOLIST") || name == QLatin1String("GOTOLIST")) {
        return doTryListLine(line);
    }

    // ---- FUNC / ENDFUNC ----
    // 只允许出现在 TRY*LIST 体内（C# ErbLoader 装载期校验）；正常执行流经
    // TRY*LIST 分派不会顺序落入这两行。顺序落入 = 装载期结构异常或 ENDFUNC
    // 落点，一律空过（不报错，保持与三游戏数据兼容）。
    if (name == QLatin1String("FUNC") || name == QLatin1String("ENDFUNC")) {
        advance();
        return ExecState::Continue;
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
    // ---- TRYCALLF / TRYCALLFORMF（EE，C# TRYCALLF_Instruction）----
    // CALLF 的 TRY 版：目标是**式中関数**（#FUNCTION/#FUNCTIONS）时正常调用并
    // **丢弃返回值**；目标不存在（未定义 / 不是函数而是普通标签）时**静默跳过**，
    // 不报错、不触发 CATCH（EE readme：想捕获请改用 EXISTFUNCTION 判断）。
    // C#：GetFunctionMethod(..., try=true) 返回 null 则直接 return。
    if (name == QLatin1String("TRYCALLF") || name == QLatin1String("TRYCALLFORMF")
        || name == QLatin1String("CALLF") || name == QLatin1String("CALLFORMF")) {
        const bool isTry = name.startsWith(QLatin1String("TRY"));
        const bool isForm = name.endsWith(QLatin1String("FORMF"));
        QString funcName = line.arguments.isEmpty() ? QString() : line.arguments.first().raw;
        if (isForm) funcName = expandCallFormLabel(funcName);
        // TRY：先做存在性判定（0=未定义 1=普通関数(标签) 2=#FUNCTION 3=#FUNCTIONS）。
        // 只有 2/3 才是 CALLF 系列的目标 —— 与 C# 的 null 判定等价。
        if (isTry && (!m_table || m_table->functionExistsKind(funcName, true) < 2)) {
            qCDebug(eraTrace) << "[trycallf]" << funcName << "不存在（TRYCALLF 静默跳过）行"
                              << line.position.toString();
            advance();
            return ExecState::Continue;
        }
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
        ExpressionEvaluator& ev = getEvaluator();
        const QSharedPointer<ExpressionNode> ast =
            m_table ? m_table->expressionAst(callText) : QSharedPointer<ExpressionNode>();
        const QVariant value = ast ? ev.evaluate(*ast, m_storage, baseData())
                                   : ev.evaluate(callText, m_storage, baseData());
        Q_UNUSED(value);   // 对齐 C#：CALLF / TRYCALLF 的返回值不落任何寄存器
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
        ExpressionEvaluator* ev = &getEvaluator();
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
            // 返回值：字符串型函数的调用方读的是 m_lastReturnValue（与 RETURNF 同路径）
            m_lastReturnValue = QVariant(text);
        }
        if (!returnFromCall()) return ExecState::Halt;
        return ExecState::Continue;
    }
    if (name == QLatin1String("RETURN") || name == QLatin1String("RETURNF")) {
        m_lastReturnValue = QVariant::fromValue<qint64>(0);
        ExpressionEvaluator* ev = &getEvaluator();
        if (name == QLatin1String("RETURNF")) {
            if (!line.arguments.isEmpty()) {
                const Operand& op = line.arguments.first();
                m_lastReturnValue = op.isString ? QVariant(op.raw)
                    : op.ast ? ev->evaluate(*op.ast, m_storage, baseData())
                             : ev->evaluate(op.raw, m_storage, baseData());
            } else {
                // 裸 RETURNF：对齐 C# ReturnF(null) —— MethodReturnValue=null，
                // 调用方按函数类型读缺省值。#FUNCTIONS 必须取**空串**而非把整数
                // 0 字符串化成 "0"（eraTW GET_OBJ 的 `SIF TEMP_GET(...)==\"0\"`
                // 拦截后 RETURNF 缺省值成了 "0"，无農民数据的角色全显示 0）。
                const UserFunctionDecl* finfo =
                    m_table->userFunction(m_table->currentFrame().callLabel);
                if (finfo && finfo->isMethod && finfo->returnsString())
                    m_lastReturnValue = QVariant(QString());
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
            // ARRAYREMOVE var, start, num：左移 num 个元素，尾部补 0/""（长度不变）。
            // eraTW 私家改造版 v39：num <= 0 时从 start 删到末尾。
            if (ops.isEmpty()) return ExecState::Continue;
            const QString var = ops.first().raw.trimmed();
            const int len = length1D(var);
            const int start = static_cast<int>(evalOp(1, 0));
            qint64 numN = evalOp(2, 0);
            if (len <= 0 || start < 0 || start >= len) return ExecState::Continue;
            const int num = (numN <= 0) ? (len - start) : static_cast<int>(qMin<qint64>(numN, len - start));
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
        // SKIPLOG 跳过中（C# MesSkip）：
        //   WAIT / WAITANYKEY（ReadAnyKey 可跳过）-> 直接放行，保持跳过状态；
        //   FORCEWAIT（ReadAnyKey(false,true) 的 StopMesskip）-> 不可跳过，
        //   但会把跳过状态清掉（C# 循环 break 后 MesSkip = false）。
        if (m_engine && m_engine->mesSkip()) {
            if (name == QLatin1String("FORCEWAIT")) {
                m_engine->setMesSkip(false);
            } else {
                qCDebug(eraTrace) << "[skiplog]" << name << "跳过中自动放行，行"
                                  << line.position.toString();
                advance();
                return ExecState::Continue;
            }
        }
        advance();
        if (m_machine) {
            m_machine->waitAnyKey();
            return m_state->getExecState();
        }
        return ExecState::WaitInput;
    }

    // ---- 输入等待（由中心执行状态挂起）----
    // 非分支数不在这里拦：C# 语义是任何输入都写 RESULT 继续执行，
    // 落空分支由脚本自己的流程兜底（SELECTCASE 落空 -> RESTART/GOTO
    // 重画菜单再等输入），引擎只负责把 RESTART/GOTO 跑对。
    if (name == QLatin1String("INPUT") || name == QLatin1String("ONEINPUT")
        || name == QLatin1String("INPUTS") || name == QLatin1String("ONEINPUTS")) {
        const bool isStr = (name == QLatin1String("INPUTS")
                            || name == QLatin1String("ONEINPUTS"));
        // C# INPUT_Instruction：**给了实参**才 HasDefValue=true（DefIntValue=实参）。
        // 这个缺省值决定「空回车」的语义 —— 有缺省交缺省、没有则忽略输入继续等
        //（C# doInputToEmueraProgram）。eraTW 的外出列表用的是无参 `INPUT`：
        // 空回车必须**什么都不发生**；以前 QML 把空输入当 0 交出去，恰好命中
        // `ELSEIF RESULT == MAIN_MAP` -> 「从外面回家」，表现就是「没操作就自动返回」。
        m_waitDefault = QVariant();
        if (!line.arguments.isEmpty()) {
            const Operand& a0 = line.arguments.first();
            ExpressionEvaluator& ev = getEvaluator();
            if (isStr) {
                m_waitDefault = a0.ast
                    ? ev.evaluate(*a0.ast, m_storage, baseData()).toString()
                    : ev.evaluate(a0.raw, m_storage, baseData()).toString();
            } else {
                qint64 v = 0;
                if (a0.ast) v = ev.evaluate(*a0.ast, m_storage, baseData()).toLongLong();
                else if (a0.isString) v = a0.raw.toLongLong();
                else v = ev.evaluate(a0.raw, m_storage, baseData()).toLongLong();
                m_waitDefault = v;
            }
        }
        advance();                       // 指令已消费
        // 需要输入值的等待（NeedValue）会把 SKIPLOG 跳过状态清掉
        // （C# MesSkip 循环：`if (inputReq.NeedValue) break;` 后 MesSkip = false）
        if (m_engine) m_engine->setMesSkip(false);
        return ExecState::WaitInput;     // 挂起等待用户操作
    }
    // ---- INPUTANY（EE v22）----
    //   同时接受整数与字符串的 INPUT（C# INPUTANY_Instruction -> InputType.AnyValue）：
    //   整数输入写 RESULT，字符串输入写 RESULTS（另一者**保持原值**，C# 语义）。
    //   无缺省值（与 INPUT 一样必须等玩家输入；空回车由输入层忽略）。
    //   画面上 PRINTBUTTON 生成的整数/字符串按钮都可以点。
    if (name == QLatin1String("INPUTANY")) {
        m_waitDefault = QVariant();
        if (m_engine) m_engine->setMesSkip(false);
        advance();
        return ExecState::WaitInput;
    }
    // ---- BINPUT / BINPUTS（EE v31fix）----
    //   実行時点でボタン化されている値のみを受け付ける INPUT(S)。ボタンが
    //   一つも無い状態なら、デフォルト値があれば**入力待ちをせずに**
    //   RESULT(S) にデフォルト値を入れる（缺省值も無ければエラー）。
    //   有按钮时退化为普通 INPUT/INPUTS 等待（按钮值白名单校验暂不强制）。
    if (name == QLatin1String("BINPUT") || name == QLatin1String("BINPUTS")
        || name == QLatin1String("ONEBINPUT") || name == QLatin1String("ONEBINPUTS")) {
        const bool isStr = (name == QLatin1String("BINPUTS")
                            || name == QLatin1String("ONEBINPUTS"));
        // ONEBINPUT/ONEBINPUTS：BINPUT 系的「单字符输入」版（C# OneInput：
        // 输入框只接受首字符、按键即提交）。引擎侧等待/缺省/按钮白名单语义与
        // BINPUT 完全相同；「只取首字符」是输入控件的约束（QML 输入栏），
        // 这里不做截断 —— 与 C# 一致（C# 也在控件层限定）。
        const bool anyButton = m_buttonAvailable && m_buttonAvailable();
        // 需要输入值的等待 -> 清掉 SKIPLOG 跳过状态（C# NeedValue -> break -> MesSkip=false）
        if (m_engine) m_engine->setMesSkip(false);
        if (anyButton) {
            advance();
            return ExecState::WaitInput;
        }
        if (line.arguments.isEmpty()) {
            // 无按钮、无缺省值：按 EE 输入族桩的容错语义**留痕跳过**，不报错
            // （组15 断言 `BINPUT`/`BINPUTS` 裸执行不报错）。
            qCDebug(eraTrace) << "[ee-input]" << name
                              << "无按钮且无缺省值，跳过。行:" << line.position.toString();
            advance();
            return ExecState::Continue;
        }
        const Operand& def = line.arguments.first();
        if (isStr) {
            const QString v = def.ast
                ? getEvaluator().evaluate(*def.ast, m_storage, baseData()).toString()
                : (def.isString ? def.raw
                                : getEvaluator().evaluate(def.raw, m_storage, baseData()).toString());
            if (m_storage) m_storage->setGlobalStr1D(QStringLiteral("RESULTS"), 0, v);
        } else {
            qint64 v = 0;
            if (def.ast) v = getEvaluator().evaluate(*def.ast, m_storage, baseData()).toLongLong();
            else if (def.isString) v = def.raw.toLongLong();
            else v = getEvaluator().evaluate(def.raw, m_storage, baseData()).toLongLong();
            if (m_storage) m_storage->setSystemVariable(QStringLiteral("RESULT"), 0, v);
        }
        advance();
        return ExecState::Continue;
    }
    // ---- 实时 / 限时输入（对齐 C# INPUTMOUSEKEY / TONEINPUT）----
    if (name == QLatin1String("INPUTMOUSEKEY")) {
        qint64 timeout = 0;
        if (!line.arguments.isEmpty()) evalInt(line.arguments.first().ast, line.arguments.first().raw, timeout);
        advance();
        // 需要输入值 -> 清掉 SKIPLOG 跳过状态（C# NeedValue）
        if (m_engine) m_engine->setMesSkip(false);
        if (m_machine) {
            m_machine->waitMouseKey(static_cast<int>(timeout));
            return m_state->getExecState();
        }
        return ExecState::WaitInput;
    }
    // ---- TINPUT <超时ms>, <缺省值>[, <跳过标记>] / TINPUTS（对齐 C# TINPUT_Instruction）----
    //   暂停 <超时>ms 等输入；到点没输入则按缺省值交付（RESULT/RESULTS）；
    //   有输入则取输入。eraMegaten 的 SYSTEM_TITLE.erb:62 TINPUTS 100, "-1", 0
    //   此前被静默忽略。TINPUTS 的缺省值为字符串（写 RESULTS）。
    if (name == QLatin1String("TINPUT") || name == QLatin1String("TINPUTS")) {
        // 参数是**独立操作数**（AstBuilder 按顶层逗号/空白切分）：
        //   TINPUT  <超时ms>, <缺省整数>[, <跳过标记>]
        //   TINPUTS <超时ms>, <缺省字符串>[, <跳过标记>]
        // eraMegaten SYSTEM_TITLE.erb:62 的渐入循环 `TINPUTS 100, "-1", 0` 正是
        // 靠这个超时逐步推进（SETCOLOR 从黑渐亮）。旧实现只在「首操作数恰好是
        // Function 节点」时取参；而该行会被切成 [100]["-1"][0] 三个普通操作数，
        // 于是 ms 恒为 0 -> waitTimedStringInput(0) 不装计时器 -> 标题画面永远
        // 停在首帧（SETCOLOR 0,0,0 = 黑字），看起来就是「没有文字/整屏黑」。
        qint64 ms = 0, def = 0;
        QString defStr;
        const QList<Operand> args = inputArgs(line);   // 见 inputArgs：剔除逗号占位
        if (!args.isEmpty()) {
            const Operand& a0 = args.at(0);
            const bool fnForm = a0.ast && a0.ast->kind() == NodeKind::Function;
            // 字符串字面量（`"abc"`）在操作数里已被剥成 raw="abc" + isString —— **不能**
            // 再拿去当表达式求值（那样 `abc` 会被当成变量名 -> 0）。整数同理优先 ast。
            const auto readStr = [&](const Operand& o) -> QString {
                if (o.isString) return o.raw;
                if (o.ast) return getEvaluator().evaluate(*o.ast, m_storage, baseData()).toString();
                return getEvaluator().evaluate(o.raw, m_storage, baseData()).toString();
            };
            if (fnForm) {
                // 兼容 `NAME(args)` 形式（整行被归约成一个调用节点）
                const auto& fn = static_cast<const FunctionNode&>(*a0.ast);
                evalInt(fn.arguments().value(0), QString(), ms);
                if (fn.arguments().size() >= 2) {
                    if (name == QLatin1String("TINPUTS")) {
                        ExpressionEvaluator* ev = &getEvaluator();
                        defStr = ev->evaluate(*fn.arguments().at(1), m_storage, baseData()).toString();
                    } else {
                        evalInt(fn.arguments().at(1), QString(), def);
                    }
                }
            } else {
                evalInt(a0.ast, a0.raw, ms);
                if (args.size() >= 2) {
                    const Operand& a1 = args.at(1);
                    if (name == QLatin1String("TINPUTS")) {
                        defStr = readStr(a1);
                    } else {
                        evalInt(a1.ast, a1.raw, def);
                    }
                }
            }
        }
        if (ms < 0) ms = 0;
        qDebug() << "[tinput]" << name << "超时" << ms << "ms 缺省"
                          << (name == QLatin1String("TINPUTS") ? defStr
                                                               : QString::number(def))
                          << "操作数" << line.arguments.size();
        advance();
        // 需要输入值 -> 清掉 SKIPLOG 跳过状态（C# NeedValue）
        if (m_engine) m_engine->setMesSkip(false);
        if (name == QLatin1String("TINPUTS")) {
            m_machine->waitTimedStringInput(static_cast<int>(ms), defStr);
            return m_state->getExecState();
        }
        if (m_machine) {
            m_machine->waitTimedInput(static_cast<int>(ms), def);
            return m_state->getExecState();
        }
        return ExecState::WaitInput;
    }
    if (name == QLatin1String("TONEINPUT") || name == QLatin1String("TONEINPUTS")) {
        const QList<Operand> args = inputArgs(line);   // 逗号同样是独立操作数
        qint64 timeout = 0;
        if (!args.isEmpty()) evalInt(args.first().ast, args.first().raw, timeout);
        // TONEINPUTS 是**字符串型**限时输入（ONEINPUTS + 超时，写 RESULTS）：
        // 超时交付第 2 参缺省值（eraTW BATTLE.ERB ASK_BATTLE 的
        // `TONEINPUTS 制限時間, "p", 1` -> SELECTCASE RESULTS CASEELSE）。
        // 此前误走整数 waitTimedInput：超时写 RESULT、RESULTS 保持陈旧值，
        // 跟在后面的 SELECTCASE RESULTS 全部判错（上一局 QTE 的 w/a/d/s）。
        QString defStr;
        if (name == QLatin1String("TONEINPUTS") && args.size() >= 2) {
            const Operand& def = args.at(1);
            // 同上：字符串字面量（"p"）直接用 raw，别再当表达式求值
            if (def.isString) {
                defStr = def.raw;
            } else if (def.ast) {
                defStr = getEvaluator().evaluate(*def.ast, m_storage, baseData()).toString();
            } else {
                defStr = getEvaluator().evaluate(def.raw, m_storage, baseData()).toString();
            }
        }
        if (timeout < 0) timeout = 0;
        advance();
        // 需要输入值 -> 清掉 SKIPLOG 跳过状态（C# NeedValue）
        if (m_engine) m_engine->setMesSkip(false);
        if (m_machine) {
            if (name == QLatin1String("TONEINPUTS"))
                m_machine->waitTimedStringInput(static_cast<int>(timeout), defStr);
            else
                m_machine->waitTimedInput(static_cast<int>(timeout));
            return m_state->getExecState();
        }
        return ExecState::WaitInput;
    }
    // AWAIT n：挂起 n 毫秒后自动继续（不阻塞、不请求输入；C# InputType.Void）
    if (name == QLatin1String("AWAIT")) {
        const QList<Operand> args = inputArgs(line);
        qint64 ms = 0;
        if (!args.isEmpty()) evalInt(args.first().ast, args.first().raw, ms);
        if (ms < 0) ms = 0;
        m_waitNotifiesUser = false;
        advance();
        if (m_machine && ms > 0) {
            m_machine->awaitDelay(static_cast<int>(ms));
            return m_state->getExecState();
        }
        return ExecState::Continue;
    }

    // ---- TWAIT <时间ms>[, <跳过标记>]（对齐 C# TWAIT_Instruction）----
    //   暂停 <时间>ms 后自动继续。C# TWAIT_Instruction：
    //     flag != 0 -> InputType.Void   纯计时等待，输入不能跳过（DQPRINT 逐字动画）
    //     flag == 0 -> InputType.EnterKey + Timelimit 点击/回车可提前结束
    //   计时路径完整（挂起 -> 计时器到点恢复）。
    if (name == QLatin1String("TWAIT")) {
        // 两种写法都要认：`TWAIT(1000,0)`（整行被归约成 Function 节点）与
        // `TWAIT 1000,0`（独立操作数，且逗号也在列表里）。以前只认前者，
        // 逗号形态下 ms 恒为 0 -> 直接 Continue，等于**没有等待**。
        QSharedPointer<ExpressionNode> timeNode;
        QSharedPointer<ExpressionNode> skipNode;
        if (!line.arguments.isEmpty() && line.arguments.first().ast
            && line.arguments.first().ast->kind() == NodeKind::Function) {
            const auto& fn = static_cast<const FunctionNode&>(*line.arguments.first().ast);
            if (!fn.arguments().isEmpty()) timeNode = fn.arguments().at(0);
            if (fn.arguments().size() >= 2) skipNode = fn.arguments().at(1);
        }
        qint64 ms = 0, skip = 0;
        if (timeNode) {
            evalInt(timeNode, QString(), ms);
            evalInt(skipNode, QString(), skip);
        } else {
            const QList<Operand> args = inputArgs(line);
            if (args.size() >= 1) evalInt(args.at(0).ast, args.at(0).raw, ms);
            if (args.size() >= 2) evalInt(args.at(1).ast, args.at(1).raw, skip);
        }
        if (ms < 0) ms = 0;
        qCDebug(eraTrace) << "[twait] 等待" << ms << "ms 跳过标记" << skip
                          << "行" << line.position.toString();
        // SKIPLOG 跳过中：TWAIT 的「可跳过」形态（skip == 0，C# EnterKey+Timelimit）
        // 直接放行；skip != 0 是纯计时 Void 等待，与跳过状态无关。
        if (skip == 0 && m_engine && m_engine->mesSkip()) {
            qCDebug(eraTrace) << "[skiplog] TWAIT 跳过中自动放行，行"
                              << line.position.toString();
            m_waitNotifiesUser = false;
            advance();
            return ExecState::Continue;
        }
        m_waitNotifiesUser = (skip == 0);
        advance();
        if (m_machine && ms > 0) {
            if (skip != 0) {
                m_machine->awaitDelay(static_cast<int>(ms));
            } else {
                m_machine->waitTimedAnyKey(static_cast<int>(ms));
            }
            return m_state->getExecState();
        }
        return ExecState::Continue;
    }

    // ---- BEGIN：设置 BEGIN 类型并返回当前函数（对齐 C# BEGIN_Instruction）----
    // FORCE_BEGIN（EE v12）走同一条分支，只是 force=true —— 跳过 __CAN_BEGIN__
    // 状态检查（强制切换流程，C# SetBegin(keyword, true)）。
    // 非法关键字两者都报错（关键字校验先于 force 检查）。
    if (name == QLatin1String("BEGIN") || name == QLatin1String("FORCE_BEGIN")) {
        const bool force = (name == QLatin1String("FORCE_BEGIN"));
        const QString keyword = line.arguments.isEmpty() ? QString() : line.arguments.first().raw;
        QString error;
        const bool ok = m_machine ? m_machine->beginWithKeyword(keyword, &error, force)
                                  : m_state->setBeginKeyword(keyword, &error, QString(), force);
        if (!ok) {
            m_state->setErrorState();
            emit errorOccurred(error);
            return ExecState::Error;
        }
        // 对齐 C# BEGIN_Instruction：state.SetBegin() 会清空函数列表（忘记 CALL
        // 调用方），随后 state.Return(0) 直接回到系统层 —— BEGIN **不返回调用方**。
        // 此前只 processBegin 清了 ProcessState 的列表，却仍 returnFromCall() 回到
        // 调用函数（菜单继续执行到 ONEINPUT），导致 BEGIN FIRST 不触发 @EVENTFIRST
        // 而是停在标题菜单（组29 回归）。
        if (m_table) m_table->resetPosition();
        m_state->clearFunctionList();
        return ExecState::Halt;
    }

    // ---- SAVEGAME / LOADGAME：记录返回状态并切换（对齐 C# SAVELOADGAME_Instruction）----
    if (name == QLatin1String("SAVEGAME") || name == QLatin1String("LOADGAME")) {
        if (!m_state->canSave()) {
            m_state->setErrorState();
            emit errorOccurred(QStringLiteral("@%1 中不能执行 SAVEGAME/LOADGAME 命令")
                                   .arg(m_table->currentFrame().callLabel));
            return ExecState::Error;
        }
        advance();
        if (m_machine) {
            m_machine->requestSaveLoad(name == QLatin1String("SAVEGAME"));
            // 对齐 C# SaveLoadData：进程状态切换后由**系统层**接管（存/读档画面）。
            // 脚本不能继续跑当前函数的剩余部分 —— 否则（eraTW 标题「[1] 存檔再開」）
            // 标题循环回到 INPUT 再挂起一次，用户再次输入时又执行 LOADGAME，
            // 而状态已切到 LoadGame_Begin（无 __CAN_SAVE__）→ 误报
            // 「@SYSTEM_TITLE 中不能执行 SAVEGAME/LOADGAME 命令」。
            // WaitEvent：让 pump 交还系统层（区别于 Halt 的「脚本终结」）。
            return ExecState::WaitEvent;
        }
        return ExecState::WaitInput;
    }

    // ---- STRDATA 系（C# STRDATA_Instruction：PRINTDATA 的不显示版）----
    // 段结构与 PRINTDATA 完全相同，区别：**不显示**，把被选中段的文本写进
    // `STRDATA <字符串变量>` 的变量里。之前没有这个分支，整段被逐行当普通
    // 指令执行，于是 `DATA`/`ENDDATA` 报 [未完成]（02_PRINT 的回归）。
    if (isStrDataName(name)) {
        if (!line.printDataReady) buildPrintDataBlock(line, pc, sd);
        const int next = line.printDataEndLine >= 0 ? line.printDataEndLine + 1 : pc + 1;
        if (sd && !line.printDataGroups.isEmpty() && !line.arguments.isEmpty() && m_engine) {
            const int count = line.printDataGroups.size();
            const int choice = static_cast<int>(getEvaluator().random().nextInt(count));
            const QList<int>& grp = line.printDataGroups.at(choice);
            QString value;
            for (int k = 0; k < grp.size(); ++k) {
                if (k > 0) value += QLatin1Char('\n');
                value += m_engine->printDataFormText(sd->lines.at(grp.at(k)));
            }
            m_engine->assignPrintDataString(line.arguments.first().raw, value);
        }
        m_table->setPosition(script, next, false);
        return ExecState::Continue;
    }

    // ---- PRINTDATA 系（C# PRINT_DATA_Instruction）----
    if (isPrintDataName(name)) {
        if (!line.printDataReady) buildPrintDataBlock(line, pc, sd);
        const int next = line.printDataEndLine >= 0 ? line.printDataEndLine + 1 : pc + 1;
        if (sd && !line.printDataGroups.isEmpty()) {
            const int count = line.printDataGroups.size();
            const int choice = static_cast<int>(getEvaluator().random().nextInt(count));
            // 可选整型变量实参（如 `PRINTDATAW LOCAL:0`）：写入被选中的段下标
            if (!line.arguments.isEmpty() && m_engine) {
                m_engine->assignPrintDataIndex(line.arguments.first().raw, choice);
            }
            const QList<int>& grp = line.printDataGroups.at(choice);
            for (int k = 0; k < grp.size(); ++k) {
                if (m_engine) m_engine->printDataFormLine(sd->lines.at(grp.at(k)));
                if (k + 1 < grp.size() && m_engine) m_engine->printDataNewline();
            }
            const bool newline =
                name.endsWith(QLatin1Char('L')) || name.endsWith(QLatin1Char('W'));
            if (newline && m_engine) m_engine->printDataNewline();
            const bool waitKey = name.endsWith(QLatin1Char('W'));
            if (waitKey && m_engine) m_engine->requestPrintDataWaitKey();
        }
        m_table->setPosition(script, next, false);   // 跳过整段（endLine 可能是 lines.size()）
        // PRINTDATAW：打印后等任意键（C# PRINT_DATA_Instruction 的 W 后缀 ->
        // Console.ReadAnyKey）；此前 requestAnyKey 无人接线，等待被忽略。
        // SKIPLOG 跳过中：ReadAnyKey 可跳过 -> 不等待（保持跳过状态）。
        if (m_engine && m_engine->consumePrintWaitKey()) {
            m_waitKind = QStringLiteral("ANYKEY");
            if (m_engine->mesSkip()) {
                return ExecState::Continue;
            }
            if (m_machine) {
                m_machine->waitAnyKey();
                return m_state->getExecState();
            }
            return ExecState::WaitInput;
        }
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
        // 位置守卫（对齐 C# DOTRAIN_Instruction）：仅 @EVENTTRAIN / @SHOW_STATUS /
        // @SHOW_USERCOM / @USERCOM / @EVENTCOMEND 上下文合法，其余位置报错。
        // 文档 Command.html：「只能在 @EVENTTRAIN、@SHOW_STATUS、@SHOW_USERCOM、
        // @USERCOM、@EVENTCOMEND 及从这些函数中调用的函数内使用」。
        const SystemStateCode sst = m_state->getSystemState();
        const bool inTrainFlow = sst == SystemStateCode::Train_CallEventTrain
                                 || sst == SystemStateCode::Train_CallShowStatus
                                 || sst == SystemStateCode::Train_CallShowUserCom
                                 || sst == SystemStateCode::Train_CallEventComEnd;
        if (!inTrainFlow) {
            m_state->setErrorState();
            emit errorOccurred(QStringLiteral("DOTRAIN 命令不能在此位置执行"));
            return ExecState::Error;
        }
        qint64 train = 0;
        evalInt(line.arguments.isEmpty() ? QSharedPointer<ExpressionNode>()
                                         : line.arguments.first().ast,
                line.arguments.isEmpty() ? QString() : line.arguments.first().raw, train);
        if (train < 0 || train >= m_machine->trainCount()) {
            m_state->setErrorState();
            emit errorOccurred(QStringLiteral("DOTRAIN 的值超出 TRAINNAME 范围"));
            return ExecState::Error;
        }
        // CALLTRAIN 处理途中执行 DOTRAIN -> CALLTRAIN 剩余部分作废（文档/C# 明确）
        m_machine->abortCallTrain();
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
        // PRINTW / PRINTFORMW / PRINTFORMLW…（W 后缀）：打印后等任意键。
        // C# PRINT_WAITINPUT -> Console.ReadAnyKey（阻塞）—— 此前 requestAnyKey
        // 信号无人接线，等待被完全忽略（eraTW 教学的段落不会停顿）。
        // SKIPLOG 跳过中：可跳过 -> 不等待（保持跳过状态）。
        if (m_engine->consumePrintWaitKey()) {
            m_waitKind = QStringLiteral("ANYKEY");
            advance();
            if (m_engine->mesSkip()) {
                return ExecState::Continue;
            }
            if (m_machine) {
                m_machine->waitAnyKey();
                return m_state->getExecState();
            }
            return ExecState::WaitInput;
        }
    }
    advance();
    return ExecState::Continue;
}

// ---------------------------------------------------------------------------
// CALLEVENT 事件链（对齐 C# CalledFunction.CallEventFunction + Process.Return）
//
// 事件函数 = 带 #PRI/#SINGLE/#LATER/#ONLY 标记（或事件名表登记）的 @函数，
// 同名可有多份。CALLEVENT <名> 依次调用：#PRI 组 -> 普通组 -> #LATER 组；
// 全部返回后回到 CALLEVENT 的下一行（续点）。
// ---------------------------------------------------------------------------
bool ScriptRunner::startEventCallChain(const QString& label)
{
    if (label.isEmpty() || !m_table) return false;
    const QList<LabelRef> refs = m_table->labels(label);
    QList<LabelRef> events;
    bool anyNonEvent = false;
    for (const LabelRef& r : refs) {
        if (r.isEvent) events.append(r);
        else anyNonEvent = true;
    }
    if (events.isEmpty()) {
        if (anyNonEvent) {
            // C#：对普通函数做 EVENT 调用是错误（CompatiCallEvent 兼容项）
            m_state->setErrorState();
            emit errorOccurred(QStringLiteral("イベント関数でない関数@%1に対しEVENT呼び出しが行われました").arg(label));
        }
        return false;
    }
    EventChainCall chain;
    chain.active = true;
    chain.label = label;
    chain.groups = QList<QList<LabelRef>>(3);
    for (const LabelRef& r : events) {
        if (r.isPri) chain.groups[0].append(r);
        else if (r.isLater) chain.groups[2].append(r);
        else chain.groups[1].append(r);
    }
    chain.group = 0;
    chain.counter = -1;
    chain.returnScript = m_table->currentScript();
    chain.returnLine = m_table->currentLine() + 1;
    chain.depth = m_table->depth();
    m_eventChain = chain;
    return advanceEventChain();
}

bool ScriptRunner::advanceEventChain()
{
    if (!m_eventChain.active || !m_table) return false;
    if (m_eventChain.counter >= 0) {
        // 前一个事件函数已返回：按 C# Process.Return 的分组规则推进
        const LabelRef done = m_eventChain.groups.at(m_eventChain.group).at(m_eventChain.counter);
        if (done.isOnly) {
            m_eventChain.active = false;
            return false;
        }
        if (done.isSingle && m_storage->getResult(0) == 1) {
            // #SINGLE 且 RETURN 1：跳过剩余（C# ShiftNext 跳到下一组）
            m_eventChain.group++;
            m_eventChain.counter = -1;
        } else {
            m_eventChain.counter++;
        }
    } else {
        m_eventChain.counter = 0;   // 首次：组内第一个
    }
    // 组耗尽则推进组；全部耗尽 -> 链结束
    while (true) {
        if (m_eventChain.group >= m_eventChain.groups.size()) {
            m_eventChain.active = false;
            return false;
        }
        if (m_eventChain.counter < m_eventChain.groups.at(m_eventChain.group).size()) break;
        m_eventChain.group++;
        m_eventChain.counter = 0;
    }
    const LabelRef ref = m_eventChain.groups.at(m_eventChain.group).at(m_eventChain.counter);
    qCDebug(eraTrace) << "[exec] CALLEVENT @" << m_eventChain.label
                      << "组" << m_eventChain.group << "条目" << m_eventChain.counter
                      << ref.script << ref.line;
    enterCall(m_eventChain.label);
    m_table->applyPrivateVariableDefaults(m_table->labelNameAt(ref.script, ref.line));
    // 每个事件函数的返回地址都是续点：返回后 maybeContinueEventChain 在续点接手
    if (!m_table->callLabelAt(ref.script, ref.line,
                              m_eventChain.returnScript, m_eventChain.returnLine)) {
        m_eventChain.active = false;
        if (!m_callContexts.isEmpty()) m_storage->setLocalContext(m_callContexts.takeLast().locals);
        return false;
    }
    return true;
}

bool ScriptRunner::maybeContinueEventChain()
{
    if (!m_eventChain.active || !m_table) return false;
    if (m_table->depth() != m_eventChain.depth) return false;
    if (m_table->currentScript() != m_eventChain.returnScript) return false;
    if (m_table->currentLine() != m_eventChain.returnLine) return false;
    return advanceEventChain();
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
    ExpressionEvaluator& ev = getEvaluator();
    const QString out = ev.evaluate(*form.staticCast<ExpressionNode>(), m_storage, baseData()).toString();
    return out.isEmpty() ? text : out;
}

ExecState ScriptRunner::doCallLine(const LogicalLine& line, bool isForm, bool isTry,
                                   bool isJump, int returnLine)
{
    QString label = line.arguments.isEmpty() ? QString() : line.arguments.first().raw;
    if (isForm) label = expandCallFormLabel(label);
    // [qdbug]（保留的调试桩）：CALL 增加脚本名与源码位置。currentLine() 是
    // 0 基内部行号（比 ERB 源码行号小 1），line.position 才是源码位置；
    // 两者并列便于对照 eraTW 原始代码。
    // 注意用 eraTrace 而不是 qDebug()：这条是**每条 CALL** 都走的（eraTW 一帧
    // 几万次），无条件 qDebug 会把 GUI 日志刷成 70%（每次还要走 Qt Creator 的
    // QML/调试连接），既看不见重点也拖慢执行。要看时用
    //   QT_LOGGING_RULES="era.trace.debug=true"
    // 或运行期 D-Bus：setLoggingRules("era.trace.debug=true")。
    qCDebug(eraTrace) << "[exec] CALL" << (isForm ? "(form)" : "") << label
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
            qCDebug(eraTrace) << "[call] TRY* 目标不存在，跳过：" << label;
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
        const QVariant value = source.isString ? QVariant(source.raw)
            : source.ast
                ? getEvaluator().evaluate(*source.ast, m_storage, baseData())
                : getEvaluator().evaluate(source.raw, m_storage, baseData());
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
    bool entered = false;
    if (isJump) {
        entered = m_table->jumpLabel(label);            // JUMP 系：继承当前帧返回地址
    } else if (returnLine >= 0) {
        entered = m_table->callLabelWithReturn(label, returnLine);   // TRY*LIST：ENDFUNC 之后
    } else {
        entered = m_table->callLabel(label);
    }
    if (!entered) {
        if (!m_callContexts.isEmpty()) m_storage->setLocalContext(m_callContexts.takeLast().locals);
        if (returnLine >= 0) {
            // TRY*LIST 候选（前面已确认 hasLabel，正常不会走到）：
            // 不能走 tryFail 的 advance() —— 那会顺序落入 FUNC 条目；回到 ENDFUNC 之后
            m_table->setPosition(m_table->currentScript(), returnLine, false);
            return ExecState::Continue;
        }
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
// TRYCALLLIST / TRYJUMPLIST / TRYGOTOLIST（GOTOLIST 为宽松别名）
//
// 对齐 C# doFlowControlFunction（Process.ScriptProc.cs:782）：
//   * 体内只有 `FUNC <名称式>(<实参>)` 单行条目（装载期已配好 funcEntries）；
//   * 逐个求值名称，第一个存在的目标即被采用：
//       - TRYCALLLIST -> CALL（带实参，返回地址 = ENDFUNC 之后）
//       - TRYJUMPLIST -> JUMP（带实参，不产生新的返回地址）
//       - TRYGOTOLIST -> GOTO $标签（无实参，只在本函数体内找）
//   * 全部失败 -> 跳到配对 ENDFUNC 之后（C# state.JumpTo(func.JumpTo)）。
// ---------------------------------------------------------------------------
ExecState ScriptRunner::doTryListLine(const LogicalLine& line) {
    const QString& name = line.functionName;
    const bool isGotoList = (name == QLatin1String("TRYGOTOLIST")
                             || name == QLatin1String("GOTOLIST"));
    const bool isJumpList = (name == QLatin1String("TRYJUMPLIST"));

    const QString script = m_table->currentScript();
    const int listLine = m_table->currentLine();
    const QList<int> entries = m_table->funcEntryLines(script, listLine);
    const int endLine = m_table->endFuncTarget(script, listLine);
    const int afterEnd = (endLine >= 0) ? endLine + 1 : listLine + 1;

    for (int entryLine : entries) {
        const LogicalLine* entry = m_table->lineAt(script, entryLine);
        if (!entry || entry->arguments.isEmpty()) continue;
        QString target = entry->arguments.first().raw;
        if (target.startsWith(QLatin1Char('@')) || target.startsWith(QLatin1Char('$')))
            target.remove(0, 1);
        target = expandCallFormLabel(target).trimmed();
        if (target.isEmpty()) continue;

        if (isGotoList) {
            const int labelPos = findGotoLabelInFunction(target);
            if (labelPos < 0) continue;             // 候选不存在 -> 下一个
            m_table->setPosition(script, labelPos, true);
            return ExecState::Continue;
        }
        // CALL/JUMP 候选：目标不存在就静默试下一个（对齐 C# callto == null -> continue）
        if (!m_table->hasLabel(target)) continue;
        return doCallLine(*entry, false, /*isTry=*/true, isJumpList, afterEnd);
    }
    // 没有候选命中（或体内无 FUNC 条目）-> ENDFUNC 之后
    m_table->setPosition(script, afterEnd, false);
    return ExecState::Continue;
}

// $ 标签只在本函数体内有效（对齐 C# state.CurrentCalled.CallLabel 的查找范围）。
// ownerFunction：没有调用帧（depth==0 的系统入口）时用它定位函数入口行。
int ScriptRunner::findGotoLabelInFunction(const QString& label, const QString& ownerFunction) const {
    const QString script = m_table->currentScript();
    const ScriptData* sd = m_table->script(script);
    if (!sd) return -1;
    int start = -1;
    if (m_table->depth() > 0) start = m_table->currentFrame().entryLine;
    if (start < 0 && !ownerFunction.isEmpty())
        start = m_table->getLabelPosition(script, ownerFunction);
    if (start < 0 || start >= sd->lines.size()) start = 0;
    for (int i = start; i < sd->lines.size(); ++i) {
        const LogicalLine& l = sd->lines.at(i);
        if (i > start && l.kind == LineKind::FunctionLabel) break;   // 离开本函数体
        if (l.kind == LineKind::GotoLabel
            && l.labelName.compare(label, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
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

bool ScriptRunner::caseMatches(const LogicalLine& caseLine,
                               const QVariant& valueVar, bool valueIsStr)
{
    if (!caseLine.caseCacheReady)
        AstBuilder::buildCaseClauses(caseLine, [this](const QString& text) {
            return m_table ? m_table->expressionAst(text) : QSharedPointer<ExpressionNode>();
        });

    ExpressionEvaluator& ev = getEvaluator();
    // perf：SELECTCASE 值只转一次字符串（此前每个 CASE 臂都 toString 一次）
    const QString valueStr = valueIsStr ? valueVar.toString() : QString();
    const auto evalClause = [&](const CaseClause& clause, bool right) -> QVariant {
        const QString& text = right ? clause.textB : clause.textA;
        const auto& ast = right ? clause.astB : clause.astA;
        if (ast) return ev.evaluate(*ast, m_storage, baseData());
        return ev.evaluate(text, m_storage, baseData());
    };

    const auto compare = [&](const QVariant& lhs, const QVariant& rhs) {
        if (valueIsStr) return QString::compare(lhs.toString(), rhs.toString(), Qt::CaseSensitive);
        const qint64 l = lhs.toLongLong(), r = rhs.toLongLong();
        return l < r ? -1 : l > r ? 1 : 0;
    };
    for (const CaseClause& clause : caseLine.caseCache) {
        switch (clause.kind) {
        case CaseClause::Kind::IsOp: {
            const int cmp = compare(valueVar, evalClause(clause, false));
            if ((clause.op == "<=" && cmp <= 0) || (clause.op == ">=" && cmp >= 0)
                || (clause.op == "==" && cmp == 0) || (clause.op == "!=" && cmp != 0)
                || (clause.op == "<" && cmp < 0) || (clause.op == ">" && cmp > 0)) return true;
            break;
        }
        case CaseClause::Kind::Range:
            if (compare(evalClause(clause, false), valueVar) <= 0
                && compare(valueVar, evalClause(clause, true)) <= 0) return true;
            break;
        case CaseClause::Kind::Equal:
            if (valueIsStr) {
                // 纯字符串字面量臂：不经 AST/ QVariant，直接比较（eraTW 地图热点）
                if (clause.isSimpleStr) {
                    if (valueStr == clause.strLiteral) return true;
                } else if (valueStr == evalClause(clause, false).toString()) {
                    return true;
                }
            } else if (compare(valueVar, evalClause(clause, false)) == 0) {
                return true;
            }
            break;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// INPUT 分支候选的静态分析（AST）
//
// 【已移除】此前在数值 INPUT/ONEINPUT 等待点前方小窗口里做 AST 静态分析，
// 收集前方 SELECTCASE 的 CASE 字面常量并在交付时拒绝「非分支值」。
// 该方案错误判断了分支结构（eraTW NEWGAME_CUSTOM.ERB 的 `CASE 0 TO 999`
// 是 TO 区间，字面常量收集不到 -> 合法输入 0 被引擎误杀）。
// C# 语义本就不拦输入：任何输入都写 RESULT 继续执行，落空分支由脚本
// 自己的流程兜底（SELECTCASE 落空 -> RESTART/GOTO 重画菜单再等输入）。
// 引擎只需把 RESTART / GOTO 跑对，不需要静态分析。
// ---------------------------------------------------------------------------

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
        if (param.hasDefault) {
            const bool stringDefault = param.type == OperandType::Str && !param.defaultStr.isEmpty();
            QString text = param.defaultStr;
            if (stringDefault && text.size() >= 2 && text.startsWith(QLatin1Char('"'))
                && text.endsWith(QLatin1Char('"'))) text = text.mid(1, text.size() - 2);
            bindOne(&param, position, stringDefault, text, param.defaultInt);
            continue;
        }
        // 省略且无缺省值：按类型绑 0 / 空串（C# 在函数入口把私有形参初始化为
        // 零值；不写回的话形参名会解析到同名的**全局**变量 —— eraTW 的
        // PARAM_DEF 组测试里 A 读到了系统变量 A:0 的残值）
        switch (param.target) {
        case UserParamTarget::Arg:
            m_storage->setArgInt(param.index, 0);
            m_storage->setLocalAlias(param.name, param.index);
            break;
        case UserParamTarget::Args:
            m_storage->setArgStr(param.index, QString());
            m_storage->setLocalAlias(param.name, param.index);
            break;
        case UserParamTarget::LocalVar:
            if (param.fixedIndex >= 0) {
                if (param.type == OperandType::Str)
                    m_storage->setGlobalStr1D(param.varName, param.fixedIndex, QString());
                else
                    m_storage->setGlobalInt1D(param.varName, param.fixedIndex, 0);
                break;
            }
            m_storage->setParameter(param.varName.isEmpty() ? param.name : param.varName,
                param.type == OperandType::Str ? QVariant(QString())
                                               : QVariant::fromValue<qint64>(0));
            break;
        case UserParamTarget::Unknown:
        default:
            m_storage->setArgInt(position, 0);
            break;
        }
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
        if (param.hasDefault) {
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
            continue;
        }
        // 省略且无缺省值：按类型绑 0 / 空串（同 bindArguments 的省略语义）
        switch (param.target) {
        case UserParamTarget::Arg:
            m_storage->setArgInt(param.index, 0);
            m_storage->setLocalAlias(param.name, param.index);
            break;
        case UserParamTarget::Args:
            m_storage->setArgStr(param.index, QString());
            m_storage->setLocalAlias(param.name, param.index);
            break;
        case UserParamTarget::LocalVar:
            if (param.fixedIndex >= 0) {
                if (param.type == OperandType::Str)
                    m_storage->setGlobalStr1D(param.varName, param.fixedIndex, QString());
                else
                    m_storage->setGlobalInt1D(param.varName, param.fixedIndex, 0);
                break;
            }
            m_storage->setParameter(param.varName.isEmpty() ? param.name : param.varName,
                param.type == OperandType::Str ? QVariant(QString())
                                               : QVariant::fromValue<qint64>(0));
            break;
        case UserParamTarget::Unknown:
        default:
            m_storage->setArgInt(position, 0);
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
