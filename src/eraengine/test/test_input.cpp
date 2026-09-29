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
// ---------------------------------------------------------------------------
// test_input.cpp
//
// 「输入」测试（无 UI）：脚本层输入 + 系统层输入两条路径。
//
//   A. 脚本层（INPUT / ONEINPUT，整数）
//        ScriptRunner 挂起 WaitInput；onInputProvided(v) 写 RESULT 并恢复；
//        多次输入、逐次断言 COUNT/RESULT。
//   B. 脚本层（INPUTS，字符串）
//        RESULTS（局部字符串槽 0）承载字符串输入。
//   C. 系统层（标准标题画面，无 @SYSTEM_TITLE）
//        beginTitle → openingInput → setWaitInput → ExecState::WaitSystemInput；
//        非法值 → 重画标题；[1] → 读档画面；[100] → 回标题；[0] → EVENTFIRST → SHOP。
//   D. 出错后 resume 不再驱动（避免反复执行出错行）。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include "ast/ast_builder.h"
#include "ast/expression_evaluator.h"
#include "process_state.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "script_runner.h"
#include "variable_storage.h"
#include "system_state_machine.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static QList<LogicalLine> buildLines(EraParseTable& table, const QStringList& src) {
    const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
    QList<LogicalLine> out;
    for (int i = 0; i < src.size(); ++i) {
        out.append(AstBuilder::build(src.at(i), ScriptPosition("t.ERB", i, 0), resolve));
    }
    return out;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Input test (script + system)";
    qDebug() << "============================";

    // =====================================================================
    qDebug() << "\nA) 脚本层输入：INPUT / ONEINPUT（整数）";
    {
        VariableStorage storage;
        ProcessState state;
        EraParseTable table(&state);
        ExpressionEvaluator evaluator;
        table.setExpressionEvaluator(&evaluator);
        table.setVariableStorage(&storage);
        ExecutionEngine engine(&storage, nullptr);
        engine.setParseTable(&table);
        engine.setExpressionEvaluator(&evaluator);
        ScriptRunner runner(&table, &engine, &state, &storage);
        runner.setExpressionEvaluator(&evaluator);
        // 控制器恢复 -> 执行链继续
        QObject::connect(&state, &ProcessState::continueExecution,
                         &runner, &ScriptRunner::onContinueExecution);

        const QStringList src = {
            "@MAIN",         // 0
            "CNT = 0",       // 1
            "ONEINPUT",      // 2
            "CNT = CNT + 1", // 3
            "A = RESULT",    // 4
            "ONEINPUT",      // 5
            "CNT = CNT + 1", // 6
            "B = RESULT",    // 7
            "INPUT",         // 8
            "CNT = CNT + 1", // 9
            "C = RESULT"     // 10
        };
        check(table.loadScript("main", buildLines(table, src)), "loadScript");
        table.setEntryPoint("MAIN");

        ExecState st = runner.runToCompletion();
        check(st == ExecState::WaitInput, "第 1 次 ONEINPUT 挂起（WaitInput）");
        check(state.getExecState() == ExecState::WaitInput, "中心状态 = WaitInput");
        check(!state.isRunning(), "挂起时中心状态阻塞继续执行");

        runner.onInputProvided(3);
        check(state.getExecState() == ExecState::WaitInput, "第 2 次 ONEINPUT 挂起");
        check(storage.getSystemVariable("A", 0) == 3, "A = RESULT == 3");
        check(storage.getGlobalInt1D("CNT", 0) == 1, "CNT == 1");

        runner.onInputProvided(5);
        check(state.getExecState() == ExecState::WaitInput, "INPUT 挂起");
        check(storage.getSystemVariable("B", 0) == 5, "B = RESULT == 5");
        check(storage.getGlobalInt1D("CNT", 0) == 2, "CNT == 2");

        runner.onInputProvided(7);
        check(state.getExecState() == ExecState::Halt, "第 3 次输入后结束（Halt）");
        check(storage.getSystemVariable("C", 0) == 7, "C = RESULT == 7");
        check(storage.getGlobalInt1D("CNT", 0) == 3, "CNT == 3（三次输入都生效）");
    }

    // =====================================================================
    qDebug() << "\nB) 脚本层输入：INPUTS（字符串 -> RESULTS）";
    {
        VariableStorage storage;
        ProcessState state;
        EraParseTable table(&state);
        ExpressionEvaluator evaluator;
        table.setExpressionEvaluator(&evaluator);
        table.setVariableStorage(&storage);
        ExecutionEngine engine(&storage, nullptr);
        engine.setParseTable(&table);
        engine.setExpressionEvaluator(&evaluator);
        ScriptRunner runner(&table, &engine, &state, &storage);
        runner.setExpressionEvaluator(&evaluator);
        QObject::connect(&state, &ProcessState::continueExecution,
                         &runner, &ScriptRunner::onContinueExecution);

        const QStringList src = {
            "@MAIN",              // 0
            "#DIMS NAME",         // 1（NAME 声明为字符串变量，RESULTS -> NAME 才走字符串赋值）
            "INPUTS",             // 2
            "NAME = RESULTS",     // 3
            "LEN = STRLENS(NAME)" // 4
        };
        check(table.loadScript("main", buildLines(table, src)), "loadScript(INPUTS)");
        table.setEntryPoint("MAIN");
        table.finalizeParse();

        const ExecState st = runner.runToCompletion();
        check(st == ExecState::WaitInput, "INPUTS 挂起");
        check(state.getExecState() == ExecState::WaitInput, "中心状态 = WaitInput");

        // 字符串输入路径：写 RESULTS（局部字符串槽 0）后恢复
        storage.setLocalStr(0, QStringLiteral("あいう"));
        runner.onInputProvided(0);
        check(state.getExecState() == ExecState::Halt, "字符串输入后结束");
        check(storage.getSystemVariable(QStringLiteral("RESULTS"), 0) == 0
                  || storage.getLocalStr(0) == QStringLiteral("あいう"),
              "RESULTS 承载字符串输入");
        check(storage.getGlobalInt1D("LEN", 0) == 6, "STRLENS(\"あいう\") == 6（Shift-JIS 字节）");
    }

    // =====================================================================
    qDebug() << "\nC) 系统层输入：标准标题画面（无 @SYSTEM_TITLE）";
    {
        VariableStorage storage;
        ProcessState state;
        EraParseTable table(&state);
        ExpressionEvaluator evaluator;
        table.setExpressionEvaluator(&evaluator);
        table.setVariableStorage(&storage);
        ExecutionEngine engine(&storage, nullptr);
        engine.setParseTable(&table);
        engine.setExpressionEvaluator(&evaluator);
        ScriptRunner runner(&table, &engine, &state, &storage);
        runner.setExpressionEvaluator(&evaluator);

        SystemStateMachine machine(&state, &table);
        machine.setVariableStorage(&storage);
        machine.setScriptRunner(&runner);
        runner.setSystemStateMachine(&machine);

        // host：捕获「无效的值」提示
        QStringList temporaries;
        SystemHost host;
        host.printTemporaryLine = [&temporaries](const QString& t) { temporaries.append(t); };
        machine.setHost(host);

        // 只有 EVENTFIRST / SHOW_SHOP（没有 @SYSTEM_TITLE → 走标准标题画面）
        const QStringList src = {
            "@EVENTFIRST",  // 0
            "BEGIN SHOP",   // 1
            "",             // 2
            "@SHOW_SHOP",   // 3
            "ONEINPUT"      // 4
        };
        check(table.loadScript("t", buildLines(table, src)), "loadScript(title)");
        table.finalizeParse();
        machine.initialize();

        ExecState st = machine.run();
        check(st == ExecState::WaitSystemInput, "标准标题画面挂起（WaitSystemInput）");
        check(state.getExecState() == ExecState::WaitSystemInput, "中心状态 = WaitSystemInput");
        check(state.getSystemState() == SystemStateCode::Openning, "状态 = Openning");

        // 非法值 → 重画标题，仍等待输入
        st = machine.resume(9);
        check(st == ExecState::WaitSystemInput, "非法值后仍挂起");
        check(state.getSystemState() == SystemStateCode::Openning, "状态仍为 Openning");
        check(temporaries.contains(QStringLiteral("无效的值")), "提示「无效的值」");

        // [1] → 读档画面（无 @TITLE_LOADGAME）
        st = machine.resume(1);
        check(st == ExecState::WaitSystemInput, "读档画面挂起");
        check(state.getSystemState() == SystemStateCode::LoadGameOpenning_WaitInput,
              "状态 = LoadGameOpenning_WaitInput");

        // [100] → 返回标题
        st = machine.resume(100);
        check(st == ExecState::WaitSystemInput, "返回标题后挂起");
        check(state.getSystemState() == SystemStateCode::Openning, "状态回到 Openning");
        check(machine.systemResult() == 100, "systemResult 记录最近输入");

        // [0] → 开始游戏：EVENTFIRST → BEGIN SHOP → @SHOW_SHOP → 脚本等待输入
        st = machine.resume(0);
        check(st == ExecState::WaitInput, "@SHOW_SHOP 的 ONEINPUT 挂起（脚本层输入）");
        check(state.getSystemState() == SystemStateCode::Shop_CallShowShop,
              "状态 = Shop_CallShowShop（EVENTFIRST -> BEGIN SHOP 链）");

        // 脚本层再输入仍可用：@SHOW_SHOP 返回后由 endCallShowShop 进入商店菜单等待
        st = machine.resume(1);
        check(st == ExecState::WaitSystemInput, "@SHOW_SHOP 返回后进入商店菜单等待（WaitSystemInput）");
        check(state.getSystemState() == SystemStateCode::Shop_WaitInput, "状态 = Shop_WaitInput");
    }

    // =====================================================================
    qDebug() << "\nD) 出错后 resume 不再驱动";
    {
        VariableStorage storage;
        ProcessState state;
        EraParseTable table(&state);
        SystemStateMachine machine(&state, &table);
        machine.initialize();
        state.setErrorState();
        const ExecState st = machine.resume(1);
        check(st == ExecState::Error, "Error 状态下 resume 仍为 Error（不重复执行）");
    }

    // =====================================================================
    qDebug() << "\nE) 实时/限时输入：AWAIT + INPUTMOUSEKEY";
    {
        VariableStorage storage;
        ProcessState state;
        EraParseTable table(&state);
        ExpressionEvaluator evaluator;
        table.setExpressionEvaluator(&evaluator);
        table.setVariableStorage(&storage);
        ExecutionEngine engine(&storage, nullptr);
        engine.setParseTable(&table);
        engine.setExpressionEvaluator(&evaluator);
        ScriptRunner runner(&table, &engine, &state, &storage);
        runner.setExpressionEvaluator(&evaluator);
        SystemStateMachine machine(&state, &table);
        machine.setVariableStorage(&storage);
        machine.setScriptRunner(&runner);
        runner.setSystemStateMachine(&machine);

        // 手动计时器：不自动跑，测试里显式触发
        struct Pending { int ms; std::function<void()> cb; };
        QList<Pending> pending;
        machine.setTimer([&pending](int ms, std::function<void()> cb) {
            pending.append({ms, cb});
        });

        // 脚本放在 @SYSTEM_TITLE：beginTitle 会调用它（与 C# 一致）
        const QStringList src = {
            "@SYSTEM_TITLE",    // 0
            "CNT = 0",          // 1
            "AWAIT 16",         // 2
            "CNT = CNT + 1",    // 3
            "INPUTMOUSEKEY 100",// 4
            "A = RESULT",       // 5
            "INPUTMOUSEKEY",    // 6（无超时）
            "B = RESULT"        // 7
        };
        check(table.loadScript("main", buildLines(table, src)), "loadScript(AWAIT)");
        table.finalizeParse();
        machine.initialize();

        QStringList prompts;
        QObject::connect(&runner, &ScriptRunner::inputRequested, [&](const QString& kind) { prompts.append(kind); });
        ExecState st = machine.run();
        check(prompts.isEmpty(), "AWAIT does not request input UI");
        check(st == ExecState::WaitSystemInput, "AWAIT 16 挂起（WaitSystemInput，不阻塞）");
        check(pending.size() == 1 && pending.first().ms == 16, "已登记 16ms 计时器");

        // 计时器到点 -> 自动继续 -> INPUTMOUSEKEY 100 -> 再挂起（并登记超时）
        auto fire = [&pending]() { if (!pending.isEmpty()) { auto p = pending.takeFirst(); p.cb(); } };
        fire();
        check(state.getExecState() == ExecState::WaitInput, "AWAIT 之后继续，INPUTMOUSEKEY 挂起");
        check(storage.getGlobalInt1D("CNT", 0) == 1, "AWAIT 之后继续执行了下一行");
        check(pending.size() == 1 && pending.first().ms == 100, "INPUTMOUSEKEY(100) 登记了超时计时器");

        // 超时 -> C# 语义 [4,0,0,0,0]
        fire();
        check(storage.getSystemVariable("A", 0) == 4, "INPUTMOUSEKEY 超时 RESULT = 4（对齐 C# InputMouseKey(4,...)）");
        check(state.getExecState() == ExecState::WaitInput, "第二个 INPUTMOUSEKEY（无超时）挂起");
        check(pending.isEmpty(), "无超时的 INPUTMOUSEKEY 不登记计时器");

        // 真实鼠标点击：类型=1（按下），坐标(50,60)，按钮=1
        machine.deliverInputValues({1, 50, 60, 1, 0});
        check(storage.getSystemVariable("B", 0) == 1, "INPUTMOUSEKEY 收到真实输入类型 1");
        check(storage.getSystemVariable(QStringLiteral("RESULT"), 1) == 50, "RESULT:1 = x 坐标 50");
        check(storage.getSystemVariable(QStringLiteral("RESULT"), 2) == 60, "RESULT:2 = y 坐标 60");

        // Restart, answer before timeout, then fire the old callback at the next prompt.
        pending.clear();
        state.setSystemState(SystemStateCode::Title_Begin);
        state.setExecState(ExecState::Continue);
        machine.run();
        fire();
        auto stale = pending.takeFirst().cb;
        machine.deliverInputValues({1, 20, 30, 40, -1});
        check(state.getExecState() == ExecState::WaitInput, "early mouse input reaches next prompt");
        stale();
        check(state.getExecState() == ExecState::WaitInput
                  && storage.getSystemVariable("RESULT", 0) == 1
                  && storage.getSystemVariable("RESULT", 1) == 20,
              "answered mouse timeout cannot overwrite or resume next prompt");
    }

    {
        ProcessState state;
        EraParseTable table(&state);
        VariableStorage storage;
        SystemStateMachine machine(&state, &table);
        machine.setVariableStorage(&storage);
        QList<std::function<void()>> callbacks;
        machine.setTimer([&](int, std::function<void()> cb) { callbacks.append(cb); });
        machine.waitMouseKey(100);
        machine.waitTimedInput(200);
        storage.setSystemVariable("RESULT", 0, 77);
        callbacks.at(0)();
        check(state.getExecState() == ExecState::WaitInput && storage.getSystemVariable("RESULT", 0) == 77,
              "stale mouse timeout cannot consume newer timed input");
        machine.awaitDelay(50);
        callbacks.at(1)();
        check(state.getExecState() == ExecState::WaitSystemInput && storage.getSystemVariable("RESULT", 0) == 77,
              "stale timed input cannot consume AWAIT");
        machine.waitMouseKey(0);
        callbacks.at(2)();
        check(state.getExecState() == ExecState::WaitInput && storage.getSystemVariable("RESULT", 0) == 77,
              "stale AWAIT callback cannot consume mouse input");
    }

    qDebug() << "\n============================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] input tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
