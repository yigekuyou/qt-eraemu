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
// test_system_flow.cpp
//
// 系统状态机 -> 脚本 的门控闭环回归测试（对齐 C# DoScript）：
//
//   Title_Begin ─beginTitle→ @SYSTEM_TITLE ─ONEINPUT→ (挂起)
//        └─ 输入 → BEGIN FIRST → First_Begin ─beginFirst→ @EVENTFIRST
//                └─ BEGIN SHOP → Shop_Begin ─beginShop→ @EVENTSHOP(无)
//                        └─ @SHOW_SHOP ─ONEINPUT→ (挂起)
//
// 关键回归点（曾出错）：@SYSTEM_TITLE 里的 `BEGIN FIRST` 必须被允许
// （状态机须从 Title_Begin 起步、由 beginTitle 调用 @SYSTEM_TITLE，
//  而不是把 @SYSTEM_TITLE 当普通入口脚本直接执行）。
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

    qDebug() << "SystemStateMachine flow test";
    qDebug() << "===========================";

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

    // 收集控制台输出（PRINTL）与错误
    QStringList printed;
    QObject::connect(&engine, &ExecutionEngine::consolePrint,
                     [&printed](const QString& t, bool) { printed.append(t.trimmed()); });
    QStringList errors;
    QObject::connect(&machine, &SystemStateMachine::errorOccurred,
                     [&errors](const QString& m) { errors.append(m); });

    // 一个最小的 eraTetris 风格流程
    const QStringList src = {
        "@SYSTEM_TITLE",     // 0
        "PRINTL title",      // 1
        "ONEINPUT",          // 2
        "IF RESULT == 0",    // 3
        "BEGIN FIRST",       // 4
        "ENDIF",             // 5
        "",                  // 6
        "@EVENTFIRST",       // 7
        "BEGIN SHOP",        // 8
        "",                  // 9
        "@SHOW_SHOP",        // 10
        "PRINTL shop",       // 11
        "ONEINPUT"           // 12
    };
    check(table.loadScript("t", buildLines(table, src)), "loadScript");
    table.finalizeParse();
    machine.initialize();

    qDebug() << "\n1) 从 Title_Begin 起步 → 调用 @SYSTEM_TITLE → 等待输入";
    state.setSystemState(SystemStateCode::Title_Begin);
    ExecState st = machine.run();
    check(st == ExecState::WaitInput, "标题 ONEINPUT 挂起（WaitInput）");
    check(state.getSystemState() == SystemStateCode::Normal,
          "beginTitle 后状态 = Normal");
    check(printed.contains(QStringLiteral("title")), "打印了标题");
    check(errors.isEmpty(), "无错误");

    qDebug() << "\n2) 输入 0 → BEGIN FIRST → @EVENTFIRST → BEGIN SHOP → @SHOW_SHOP";
    st = machine.resume(0);
    check(st == ExecState::WaitInput, "@SHOW_SHOP 的 ONEINPUT 挂起");
    check(state.getSystemState() == SystemStateCode::Shop_CallShowShop,
          "状态推进到 Shop_CallShowShop（BEGIN 链生效）");
    check(printed.contains(QStringLiteral("shop")), "打印了商店");
    check(errors.isEmpty(), "无 BEGIN/状态机错误");
    if (!errors.isEmpty()) {
        qDebug() << "  errors:" << errors;
    }

    qDebug() << "\n3) BEGIN 校验（错误消息带函数名）";
    {
        // 在 Normal 之外、不允许 BEGIN 的状态下发起 BEGIN TRAIN → 应报错
        state.setSystemState(SystemStateCode::Title_Begin);
        QString err;
        const bool ok = state.setBeginKeyword(QStringLiteral("TRAIN"), &err,
                                              QStringLiteral("MY_FUNC"));
        check(!ok, "Title_Begin 下 BEGIN TRAIN 被拒绝");
        check(err.contains(QStringLiteral("MY_FUNC")), "错误消息含发起函数名（@MY_FUNC）");
        check(err.contains(QStringLiteral("BEGIN")), "错误消息说明是 BEGIN");
    }

    qDebug() << "\n===========================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] system-flow tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
