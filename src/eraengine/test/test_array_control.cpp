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
// test_array_control.cpp —— ARRAYSHIFT / ARRAYREMOVE / ARRAYSORT 的**存储分派**
//
// 这三个指令按名字读写「某个 1D 数组的元素」，而这个名字可能是用户数组，也可能是
// **系统数组**（RESULT / RESULTS / LOCAL / ARG / ITEM / FLAG …）。系统数组有自己
// 的存储槽（m_result / m_localIntVars / …），裸槽位读写（getGlobalInt1D）会写进
// **同名的影子容器**：指令"成功"，但读的人（求值器走 getSystemVariable）看不到。
//
// 实测后果（本文件钉住的就是它）：FL3.8 `WindowDrawer.ERB`
//   `ARRAYSHIFT RESULT, 1, 0`   ; 给窗口列表头部腾一格放"个数"
// 整条丢失 → RESULT:1 还是 arrWndIdList:1（第一个窗口号被"个数"顶掉）
// → `WndMgr_DisplayAll` 拿到的列表整体错位，尾部那个陈旧的 -1 被当成窗口号
// → `THROW WndMgr_Move() : ウィンドウ（nWndId=-1）は未生成です`
// 表现就是商店界面一画就 Error 停住。
//
// 断言分三组：
//   1. RESULT（系统数组，m_result）：右移 / 左移都落在真存储上；
//   2. 系统数组路径使用统一元素读写 API；
//   3. 用户数组：ARRAYSHIFT / ARRAYSORT 的语义不变（回归保护）。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include "ast/ast_builder.h"
#include "ast/expression_evaluator.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "process_state.h"
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
    for (int i = 0; i < src.size(); ++i)
        out.append(AstBuilder::build(src.at(i), ScriptPosition("arrayctl.ERB", i, 0), resolve));
    return out;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "数组指令 × 系统数组 存储分派测试";
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
    QObject::connect(&state, &ProcessState::continueExecution,
                     &runner, &ScriptRunner::onContinueExecution);

    // 断言用的全局数组（用户全局）：脚本把每次读到的值搬进来，C++ 侧读回。
    const QStringList src = {
        "#DIM OUT1, 16",          // 0  RESULT 观测值
        "#DIM OUT2, 16",          // 1  LOCAL 观测值
        "#DIM OUT3, 16",          // 2  用户数组观测值
        "@MAIN",                  // 3
        "#DIM arrU, 10",          // 4
        "",                       // 5
        "; ---- 1) RESULT 是系统数组，ARRAYSHIFT 必须落在 m_result 上 ----", // 6
        "RESULT:0 = 0",           // 7
        "RESULT:1 = 1",           // 8
        "RESULT:2 = 2",           // 9
        "RESULT:3 = 3",           // 10
        "RESULT:4 = -1",          // 11
        "ARRAYSHIFT RESULT, 1, 0",      // 12 右移一格，空出的填 0
        "RESULT:0 = 4",                 // 13 头部放"个数"（C# 的既有写法）
        "OUT1:0 = RESULT:1",            // 14
        "OUT1:1 = RESULT:2",            // 15
        "OUT1:2 = RESULT:3",            // 16
        "OUT1:3 = RESULT:4",            // 17
        "OUT1:4 = RESULT:5",            // 18
        "ARRAYSHIFT RESULT, -1, -1",    // 19 左移一格，空出的填 -1
        "OUT1:5 = RESULT:0",            // 20
        "OUT1:6 = RESULT:1",            // 21
        "OUT1:7 = RESULT:2",            // 22
        "OUT1:8 = RESULT:3",            // 23
        "",                       // 24
        "; ---- 2) 用户数组：语义不变 ----", // 25
        "arrU:0 = 3",             // 26
        "arrU:1 = 1",             // 27
        "arrU:2 = 2",             // 28
        "ARRAYSORT arrU, FORWARD, 0, 3", // 29（限定区间：尾部未用槽位是 0）
        "OUT3:0 = arrU:0",        // 30
        "OUT3:1 = arrU:1",        // 31
        "OUT3:2 = arrU:2",        // 32
        "ARRAYSHIFT arrU, -1, 9, 0, 3", // 33（限定区间）
        "OUT3:3 = arrU:0",        // 34
        "OUT3:4 = arrU:1",        // 35
        "OUT3:5 = arrU:2",        // 36
        "; LOCAL/ARG 的长度问法（storageName + 私有段）未解决，另开一项；", // 37
        "; 这里只钉系统**全局**数组（RESULT/RESULTS）与用户数组两条路径。", // 38
        "RETURN"                  // 39
    };

    check(table.loadScript("arrayctl", buildLines(table, src)), "loadScript(arrayctl)");
    table.markLoadComplete();
    table.setEntryPoint("MAIN");
    const ExecState st = runner.runToCompletion();
    check(st == ExecState::Halt, "run finished (Halt)");
    if (st != ExecState::Halt) {
        qDebug() << "  [FAIL] 执行未到 Halt，后续断言不可信";
        return 1;
    }

    const auto out = [&](const char* name, int i) {
        return storage.getGlobalInt1DInCurrentFrame(QString::fromLatin1(name), i);
    };

    qDebug() << "\n1) RESULT（系统数组）右移：ARRAYSHIFT RESULT, 1, 0";
    check(out("OUT1", 0) == 0, "RESULT:1 == 0（arrWndIdList:0 移到了 1 —— 旧实现里这里是 1）");
    check(out("OUT1", 1) == 1, "RESULT:2 == 1");
    check(out("OUT1", 2) == 2, "RESULT:3 == 2");
    check(out("OUT1", 3) == 3, "RESULT:4 == 3");
    check(out("OUT1", 4) == -1, "RESULT:5 == -1（列表尾部保住了 -1，不再被顶到有效位）");

    qDebug() << "\n2) RESULT（系统数组）左移：ARRAYSHIFT RESULT, -1, -1";
    check(out("OUT1", 5) == 0, "RESULT:0 == 0");
    check(out("OUT1", 6) == 1, "RESULT:1 == 1");
    check(out("OUT1", 7) == 2, "RESULT:2 == 2");
    check(out("OUT1", 8) == 3, "RESULT:3 == 3");

    qDebug() << "\n4) 用户数组：ARRAYSORT / ARRAYSHIFT 语义不变";
    check(out("OUT3", 0) == 1 && out("OUT3", 1) == 2 && out("OUT3", 2) == 3,
          "ARRAYSORT arrU, FORWARD, 0, 3 -> [1,2,3]");
    check(out("OUT3", 3) == 2 && out("OUT3", 4) == 3 && out("OUT3", 5) == 9,
          "ARRAYSHIFT arrU, -1, 9 -> [2,3,9]");

    if (g_failures == 0) {
        qDebug().noquote() << "\n[SUCCESS] 数组指令的系统数组分派正确";
        return 0;
    }
    qDebug().noquote() << "\n[FAILED]" << g_failures << "项";
    return 1;
}
