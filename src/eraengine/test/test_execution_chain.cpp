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
// test_execution_chain.cpp
//
// 验证「拍平 AST 的执行链」：
//   1. 连续执行（信号与槽由 ProcessState 驱动的 onContinueExecution）
//   2. 控制流：IF/ELSE/ENDIF、REPEAT/LOOP、FOR/NEXT、WHILE/WEND、GOTO
//   3. 中心执行状态：只有 IO（INPUT）才挂起；其余连续执行
//   4. 用户自定义函数：CALL/RETURN（语句）、DOUBLE(...)（表达式）、形参别名
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

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) {
        qDebug().noquote() << "  [ok ]" << what;
    } else {
        qDebug().noquote() << "  [FAIL]" << what;
        ++g_failures;
    }
}

static QList<LogicalLine> buildLines(EraParseTable& table, const QStringList& src) {
    const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
    QList<LogicalLine> out;
    for (int i = 0; i < src.size(); ++i) {
        out.append(AstBuilder::build(src.at(i), ScriptPosition("test.ERB", i, 0), resolve));
    }
    return out;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "ScriptRunner execution-chain test";
    qDebug() << "================================";

    VariableStorage storage;
    ProcessState state;
    EraParseTable table(&state);
    ExecutionEngine engine(&storage, nullptr);
    engine.setParseTable(&table);

    ExpressionEvaluator evaluator;
    engine.setExpressionEvaluator(&evaluator);
    table.setExpressionEvaluator(&evaluator);
    table.setVariableStorage(&storage);

    ScriptRunner runner(&table, &engine, &state, &storage);
    runner.setExpressionEvaluator(&evaluator);

    // 信号与槽：控制器 continueExecution() -> 执行链 onContinueExecution()
    QObject::connect(&state, &ProcessState::continueExecution,
                     &runner, &ScriptRunner::onContinueExecution);

    // ---- 主脚本 ---------------------------------------------------------
    const QStringList mainSrc = {
        "@MAIN",              // 0
        "AA = 1 + 2 * 3",     // 1
        "IF AA == 7",         // 2
        "BB = 10",            // 3
        "ELSE",               // 4
        "BB = 20",            // 5
        "ENDIF",              // 6
        "REPEAT 3",           // 7
        "CC = CC + 1",        // 8
        "LOOP",               // 9
        "FOR II, 0, 2",       // 10
        "DD = DD + 1",        // 11
        "NEXT",               // 12
        "EE = 0",             // 13
        "WHILE EE < 2",       // 14
        "EE = EE + 1",        // 15
        "WEND",               // 16
        "CALL SUB(5)",        // 17
        "FF = RESULT",        // 18
        "KK = DOUBLE(21)",    // 19
        "GOTO DONE",          // 20
        "BB = 999",           // 21 (应被跳过)
        "$DONE",              // 22 (GOTO 目标是函数内 $标签；@函数标签会终结函数)
        "HH = 1",             // 23
        "",                   // 24
        "@SUB(X)",            // 25
        "RETURN X + 1",       // 26
        "",                   // 27
        "@DOUBLE(V)",         // 28
        "#FUNCTION",          // 29
        "RETURN V * 2"        // 30
    };

    check(table.loadScript("main", buildLines(table, mainSrc)), "loadScript(main)");
    check(table.userFunction("SUB") != nullptr, "user function SUB registered");
    check(table.userFunction("DOUBLE") && table.userFunction("DOUBLE")->isMethod,
          "user function DOUBLE is method (#FUNCTION)");

    table.setEntryPoint("MAIN");
    const ExecState st = runner.runToCompletion();

    qDebug() << "\n1) 连续执行 + 控制流";
    check(st == ExecState::Halt, "run finished (Halt)");
    check(storage.getGlobalInt1D("AA", 0) == 7, "AA == 1 + 2 * 3 == 7");
    check(storage.getGlobalInt1D("BB", 0) == 10, "IF branch taken -> BB == 10");
    check(storage.getGlobalInt1D("CC", 0) == 3, "REPEAT 3 -> CC == 3");
    check(storage.getGlobalInt1D("DD", 0) == 2, "FOR II,0,2 -> DD == 2");
    check(storage.getGlobalInt1D("EE", 0) == 2, "WHILE EE<2 -> EE == 2");
    check(storage.getGlobalInt1D("HH", 0) == 1, "GOTO DONE -> HH == 1");
    check(storage.getGlobalInt1D("BB", 0) != 999, "line after GOTO skipped");

    qDebug() << "\n2) 用户自定义函数";
    check(storage.getGlobalInt1D("FF", 0) == 6, "CALL SUB(5) -> RETURN 5+1 -> FF == 6");
    check(storage.getGlobalInt1D("KK", 0) == 42, "expression func DOUBLE(21) -> KK == 42");

    // ---- IO 挂起（只有等待输入才中断）---------------------------------
    const QStringList waitSrc = {
        "@WAIT",
        "ONEINPUT",
        "JJ = RESULT"
    };
    check(table.loadScript("wait", buildLines(table, waitSrc)), "loadScript(wait)");
    table.setEntryPoint("WAIT");

    qDebug() << "\n3) 只有等待 IO 才挂起";
    const ExecState st2 = runner.runToCompletion();
    check(st2 == ExecState::WaitInput, "ONEINPUT -> suspended WaitInput");
    check(state.getExecState() == ExecState::WaitInput, "central state == WaitInput");
    check(!state.isRunning(), "central state blocks further execution");

    qDebug() << "\n4) 用户输入恢复";
    runner.onInputProvided(42);   // requestResume -> continueExecution -> onContinueExecution
    check(state.getExecState() == ExecState::Halt, "resumed and finished (Halt)");
    check(storage.getGlobalInt1D("JJ", 0) == 42, "JJ = RESULT == 42");

    // ---- 输出 -> 显示层（consolePrint 信号）----
    QList<QPair<QString, bool>> printed;
    QObject::connect(&engine, &ExecutionEngine::consolePrint,
                     [&printed](const QString& t, bool nl) { printed.append({t.trimmed(), nl}); });

    const QStringList outSrc = {"@OUT", "VA = 3", "PRINTFORML \"hello\"",
                                "PRINT world", "PRINTFORML HP={VA}"};
    check(table.loadScript("out", buildLines(table, outSrc)), "loadScript(out)");
    table.setEntryPoint("OUT");
    runner.runToCompletion();
    check(printed.size() == 3, "PRINTFORML + PRINT + PRINTFORM -> 3 consolePrint");
    if (printed.size() == 3) {
        // 格式串里的引号只是普通字符（对齐 C# AnalyseFormattedString），会一起输出
        check(printed[0].first == "\"hello\"" && printed[0].second,
              "PRINTFORML \"hello\" -> 原样输出 \"hello\"（引号非格式符）");
        check(printed[1].first == "world" && !printed[1].second, "PRINT world newline=false（纯文本打印）");
        check(printed[2].first == "HP=3" && printed[2].second, "PRINTFORML HP={VA} -> StrForm 求值 HP=3");
    }

    printed.clear();
    table.setEntryPoint("OUT");
    state.setExecState(ExecState::Continue);
    runner.runSlice(4096, 1000);
    check(printed.size() == 3, "one execution slice crosses consecutive print instructions");

    // =====================================================================
    // EE 扩展命令的状态语义（ProcessState / ExecutionEngine 侧，可离线断言）
    // =====================================================================
    qDebug() << "\n--- EE 状态语义（FORCE_BEGIN / QUIT 族 / SKIPLOG / FLOWINPUT）---";

    // FORCE_BEGIN：BEGIN 需要 __CAN_BEGIN__ 状态放行，FORCE_BEGIN 跳过该检查
    // （C# Process.State.cs:216 `if (force == true) break;`）。
    // Shop_CallShowShop（@SHOW_SHOP 所在状态）**没有** __CAN_BEGIN__ 位：
    // 这里 BEGIN SHOP 应失败，而 FORCE_BEGIN SHOP 应成功。
    {
        state.setSystemState(SystemStateCode::Shop_CallShowShop);
        check(!state.canBegin(), "Shop_CallShowShop 状态不允许 BEGIN（无 __CAN_BEGIN__）");

        QString err;
        const bool plain = state.setBeginKeyword(QStringLiteral("SHOP"), &err,
                                                 QStringLiteral("@SHOW_SHOP"), false);
        check(!plain && !err.isEmpty(),
              "BEGIN SHOP 在该状态被拒（错误消息：" + err + "）");

        const bool forced = state.setBeginKeyword(QStringLiteral("SHOP"), &err,
                                                  QStringLiteral("@SHOW_SHOP"), true);
        check(forced, "FORCE_BEGIN SHOP 跳过 __CAN_BEGIN__ 检查（成功）");
        state.clearFunctionList();
        state.setSystemState(SystemStateCode::Normal);

        // 关键字合法性先于 force 检查：FORCE_BEGIN 传非法关键字照样报错
        QString err2;
        const bool badKeyword = state.setBeginKeyword(QStringLiteral("0"), &err2,
                                                      QStringLiteral("@X"), true);
        check(!badKeyword && err2.contains(QStringLiteral("未定义")),
              "FORCE_BEGIN 非法关键字仍报错（force 不豁免关键字校验）");
    }

    // QUIT 族：QUIT / FORCE_QUIT 只置 quit；QUIT_AND_RESTART / FORCE_QUIT_AND_RESTART
    // 另置重启标志（宿主据此 reload）。
    {
        ProcessState q;
        q.requestQuit();
        check(q.quitRequested() && !q.restartRequested(), "requestQuit：仅 quit 请求");
        ProcessState r;
        r.requestRestart();
        check(r.quitRequested() && r.restartRequested(),
              "requestRestart：quit + 重启标志（QUIT_AND_RESTART 系）");
        r.clearRestartRequest();
        check(r.restartRequested() == false, "clearRestartRequest 消费重启标志");
    }

    // SKIPLOG -> MesSkip（MESSKIP() 的数据源；引擎默认关闭）
    {
        check(!engine.mesSkip(), "MesSkip 默认关闭");
        engine.setMesSkip(true);
        check(engine.mesSkip(), "SKIPLOG 置位后 mesSkip() = true（MESSKIP() 反映）");
        engine.setMesSkip(false);
        check(!engine.mesSkip(), "SKIPLOG 0 解除跳过状态");
    }

    // FLOWINPUT / FLOWINPUTS：选项落在 ProcessState（不自动复位；由系统层输入消费）
    {
        ProcessState p;
        check(!p.flowInput() && p.flowInputDef() == 0, "flowinput 默认关闭");
        p.setFlowInput(42, true, /*canSkip*/ true, /*forceSkip*/ false);
        check(p.flowInput() && p.flowInputDef() == 42 && p.flowInputCanSkip()
                  && !p.flowInputForceSkip(),
              "FLOWINPUT(42,1,1,0)：缺省 42 / 启用 / 可跳过 / 非强制");
        p.setFlowInputString(true, QStringLiteral("文本"));
        check(p.flowInputString() && p.flowInputDefString() == QStringLiteral("文本"),
              "FLOWINPUTS(1,\"文本\")：字符串模式 + 缺省串");
    }

    qDebug() << "\n================================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] execution-chain tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
