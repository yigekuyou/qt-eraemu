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
#ifndef SCRIPT_RUNNER_H
#define SCRIPT_RUNNER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QSharedPointer>
#include "process_state.h"
#include "variable_storage.h"
#include "ast/logical_line.h"

class EraParseTable;
class ExecutionEngine;
class VariableStorage;
class GameBaseData;
class ExpressionEvaluator;
class SystemStateMachine;
struct UserFunctionDecl;

// ---------------------------------------------------------------------------
// ScriptRunner —— 拍平 AST 的执行链
//
// 控制模型（信号与槽，由系统状态控制器主导）：
//   * ProcessState（程序状态控制器）发出 continueExecution() 信号；
//   * ScriptRunner::onContinueExecution() 槽被触发，循环执行拍平后的 AST；
//   * 每一步执行后查询中心执行状态 ProcessState::ExecState（Continue/WaitInput/…）；
//     非 Continue 时挂起并发出 suspended()/inputRequested()/finished()；
//   * 用户输入由控制器投递：ProcessState::requestResume() → continueExecution() 再次触发；
//   * 系统状态变化（SystemStateCode）经 onSystemStateChanged() 槽影响执行。
//
// 对应 C# Emuera：Process.DoScript（门控循环）+ EmueraConsole.IsRunning/WaitInput。
// 与旧实现不同：执行链自带循环，不依赖 Qt 事件循环即可推进整段脚本。
// ---------------------------------------------------------------------------
class ScriptRunner : public QObject {
    Q_OBJECT

public:
    ScriptRunner(EraParseTable* table,
                 ExecutionEngine* engine,
                 ProcessState* state,
                 VariableStorage* storage,
                 QObject* parent = nullptr);

    // 供测试/无事件循环场景：直接同步跑到挂起或结束。
    ExecState runToCompletion();
    // One event-loop slice. Continue means the caller must schedule another slice.
    ExecState runSlice(int instructionBudget = 256, int timeBudgetMs = 4);
    bool printedInSlice() const { return m_printed; }

    // 是否正在执行
    bool isRunning() const { return m_running; }

    // 用户函数回调（表达式 `NAME(args)`），由 ExpressionEvaluator 使用。
    bool invokeUserFunction(const QString& name, const QList<QVariant>& args, QVariant& out);

    // 设置表达式求值器（用于挂用户函数回调 + 条件求值）
    void setExpressionEvaluator(ExpressionEvaluator* evaluator);

    // 单次连续执行的步数上限（0 = 不限）；超限报错，用于把「脚本死循环」变成可定位的错误
    void setStepLimit(qint64 limit) { m_stepLimit = limit; }
    [[nodiscard]] qint64 stepLimit() const { return m_stepLimit; }

    // 系统状态机（BEGIN / CALLTRAIN / DOTRAIN / SAVEGAME 等指令需要它）
    void setSystemStateMachine(SystemStateMachine* machine) { m_machine = machine; }

signals:
    void suspended(ExecState state);          // 挂起（等待输入等）
    void finished();                          // 脚本结束
    void errorOccurred(const QString& message);
    void inputRequested(const QString& kind);// 需要用户输入（INPUT/ONEINPUT/…）
    void instructionExecuted(const QString& script, int line);

public slots:
    // 控制器请求继续执行（主循环入口）
    void onContinueExecution();

    // 用户输入已就绪：写入 RESULT 并请求继续
    void onInputProvided(qint64 value);

    // 系统状态发生变化（由系统状态控制器驱动）
    void onSystemStateChanged(SystemStateCode state);

    // 请求停止
    void onHaltRequested();

private:
    // 运行时循环状态（循环计数是运行期数据，不放进只读 AST）
    struct LoopFrame {
        enum class Kind { Repeat, For, While, Do } kind = Kind::Repeat;
        int    startLine = -1;   // REPEAT / FOR / WHILE 行
        int    endLine   = -1;   // LOOP / NEXT / WEND 行
        int depth = 0;
        QString script;
        QString varName;         // FOR 变量
        qint64 value = 0;        // FOR 当前值
        qint64 end = 0;          // FOR 终值
        qint64 step = 1;         // FOR 步长
        // 迭代计数与超限告警（诊断）：脚本里的循环若跑到异常次数，直接报出
        // 「哪个脚本的哪一行 + 循环变量当前值/终值」，而不是等全局步数上限
        // 在无关的位置报错（eraTW 曾出现单个循环吞掉数千万步的情况）。
        qint64 iterations = 0;
        bool   warned = false;
    };
    static constexpr qint64 kLoopIterationWarn = 100000;   // 单循环迭代告警阈值

    // 执行一步（取 PC 处的一行）；返回 false 表示已挂起/结束/出错
    bool stepOnce();
    bool returnFromCall();
    void enterCall(const QString& function);

    // 执行一行；返回中心执行状态（Continue 表示可继续）
    ExecState executeLine(const LogicalLine& line);

    // 循环帧辅助
    LoopFrame* topLoop(LoopFrame::Kind kind);

    // 表达式求值（优先缓存 AST）
    bool evalInt(const QSharedPointer<ExpressionNode>& ast, const QString& raw, qint64& out);
    bool evalCondition(const LogicalLine& line, bool& out);

    // 标签/函数
    int  labelLine(const QString& label) const;
    // GAMEBASE_* 等需要 GameBase 数据（经 ExecutionEngine 取得）
    [[nodiscard]] GameBaseData* baseData() const;
    // 绑定实参到 LOCAL，并把形参名注册为局部别名（供表达式解析）
    void bindArguments(const UserFunctionDecl* info, const QList<Operand>& callArgs,
                       const QHash<QString, QString>& references = {});
    // FOR/NEXT 的循环变量写入：LOCAL/ARG 写局部槽（脚本读法一致），其余写系统/全局
    void writeLoopCounter(const QString& name, qint64 value);

    // 整型变量统一读写（LOCAL/ARG -> 局部槽；系统变量 -> 系统槽；角色数据变量 ->
    // 角色存储；其余 -> 全局）。QList<int> 重载保留全部下标（角色变量需要）。
    [[nodiscard]] qint64 readIntVar(const QString& name, int index) const;
    [[nodiscard]] qint64 readIntVar(const QString& name, const QList<int>& indices) const;
    void writeIntVar(const QString& name, const QList<int>& indices, qint64 value);
    // SELECTCASE 的 CASE 匹配：支持 `v1, v2` / `IS >= n` / `a TO b`
    [[nodiscard]] bool caseMatches(const LogicalLine& caseLine,
                                   const QVariant& valueVar, bool valueIsStr);
    void writeIntVar(const QString& name, int index, qint64 value);
    // 从实参取「变量名 + 下标」（SWAP 用；下标可为表达式）
    bool extractVarRef(const Operand& op, QString& name, int& index);
    bool extractVarRef(const Operand& op, QString& name, QList<int>& indices);

    EraParseTable*   m_table;
    ExecutionEngine* m_engine;
    ProcessState*    m_state;
    VariableStorage* m_storage;
    ExpressionEvaluator* m_evaluator = nullptr;
    qint64 m_steps = 0;
    qint64 m_stepLimit = 0;   // 0 = 不限
    SystemStateMachine*  m_machine = nullptr;

    QList<LoopFrame> m_loops;
    struct CallContext { int depth; int loops; QString function; VariableStorage::LocalContext locals; };
    QHash<QString, VariableStorage::LocalContext> m_functionLocals;
    QList<CallContext> m_callContexts;   // Caller locals and loop depth, restored on every return
    bool m_running = false;
    bool m_printed = false;
    bool m_continuingSlice = false;
    QVariant m_lastReturnValue;
};

#endif // SCRIPT_RUNNER_H
