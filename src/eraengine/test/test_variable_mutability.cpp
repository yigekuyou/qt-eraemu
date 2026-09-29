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
// test_variable_mutability.cpp
//
// 回归「全局变量 / 部分变量 在游戏中似乎不可变」：
//
//   1. 用户全局变量（#DIM/#DIMS，1D/2D/3D）赋值后读得回来（含大小写不敏感）
//   2. 系统变量 TIME：写入后读得回来（此前写落到用户全局槽，读走系统通道恒 0）
//   3. 角色数据变量 CFLAG/TALENT/…：不同「元素下标」互不覆盖；
//      省略角色维时取 TARGET
//   4. 用户 #DIM(S) CHARADATA 变量：按 (角色号, 元素下标) 存取，元素互不覆盖
//   5. 标识符大小写不敏感贯穿读写（ICVariable）
//
// 断言直接读 VariableStorage（与脚本执行解耦），脚本只负责赋值。
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
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static QList<LogicalLine> buildLines(EraParseTable& table, const QStringList& src,
                                     const QString& file) {
    const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
    QList<LogicalLine> out;
    for (int i = 0; i < src.size(); ++i)
        out.append(AstBuilder::build(src.at(i), ScriptPosition(file, i, 0), resolve));
    return out;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Variable mutability regression";
    qDebug() << "==============================";

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
    runner.setStepLimit(200000);

    // ---- 头文件（.ERH：文件级 #DIM = 全局）------------------------------
    const QStringList header = {
        "#DIM G_ONE, 4",
        "#DIMS G_SNAME, 3",
        "#DIM G_TWO, 3, 3",
        "#DIM G_THREE, 2, 2, 2",
        "#DIM CHARADATA SAVEDATA U_CI, 4",
        "#DIMS CHARADATA SAVEDATA U_CS, 3",
    };

    const QStringList main = {
        "@MAIN",
        // 1) 用户全局 int（1D）：大小写不敏感读写
        "G_ONE:0 = 10",
        "g_one:1 = 20",
        "R_ONE_A = G_ONE:0",
        "R_ONE_B = g_one:1",
        // 1) 用户全局 str（1D）
        "G_SNAME:0 = hello",
        // 1) 用户全局 2D / 3D
        "G_TWO:1:2 = 77",
        "R_TWO = G_TWO:1:2",
        "G_THREE:1:1:1 = 5",
        "R_THREE = g_three:1:1:1",
        // 2) 系统变量 TIME（写 -> 读；系统通道）
        "TIME:3 = 420",
        "R_TIME_3 = TIME:3",
        "time:0 = 55",
        "R_TIME_BARE = TIME",
        "R_TIME_0 = time:0",
        "time:2 = 9",
        "R_TIME_2 = TIME:2",
        // 3) 角色数据变量：元素下标不得互相覆盖
        "TARGET = 5",
        "CFLAG:3:10 = 111",
        "CFLAG:3:11 = 222",
        "R_CF_3_10 = CFLAG:3:10",
        "R_CF_3_11 = CFLAG:3:11",
        "CFLAG:20 = 333",             // 省略角色维 -> TARGET(5)
        "R_CF_T_20 = CFLAG:5:20",
        "R_CF_T_21 = CFLAG:5:21",
        "TALENT:7:2 = 77",
        "R_TAL_7_2 = TALENT:7:2",
        "NO:3 = 9",                    // 角色标量（元素维 0）
        "R_NO_3 = NO:3",
        // 4) 用户 #DIM(S) CHARADATA：元素互不覆盖
        "U_CI:2:1 = 9",
        "U_CI:2:2 = 8",
        "R_UCI_2_1 = U_CI:2:1",
        "R_UCI_2_2 = U_CI:2:2",
        "U_CS:2:1 = text",
        "U_CS:2:2 = other",
        // 5) SWAP 角色变量：必须交换 (角色号, 元素下标) 两侧
        "CFLAG:4:1 = 7",
        "CFLAG:4:2 = 9",
        "SWAP CFLAG:4:1, CFLAG:4:2",
        "R_SWAP_1 = CFLAG:4:1",
        "R_SWAP_2 = CFLAG:4:2",
        "RETURN",
    };

    check(table.loadScript("DIM", buildLines(table, header, "DIM.ERH"),
                           /*isHeaderFile=*/true, "DIM.ERH"),
          "loadScript(DIM.ERH)");
    check(table.loadScript("main", buildLines(table, main, "main.ERB")), "loadScript(main)");
    table.setEntryPoint("MAIN");
    table.finalizeParse();

    const ExecState st = runner.runToCompletion();
    check(st == ExecState::Halt, "run finished (Halt)");

    const auto g = [&](const QString& n) { return storage.getGlobalInt1D(n, 0); };

    qDebug() << "\n1) 用户全局变量（1D/2D/3D，大小写不敏感）";
    check(storage.getGlobalInt1D("G_ONE", 0) == 10, "G_ONE:0 == 10");
    check(storage.getGlobalInt1D("G_ONE", 1) == 20, "g_one:1 == 20");
    check(g("R_ONE_A") == 10, "R_ONE_A = G_ONE:0 == 10");
    check(g("R_ONE_B") == 20, "R_ONE_B = g_one:1 == 20（小写读）");
    check(storage.getGlobalStr1D("G_SNAME", 0) == QStringLiteral("hello"),
          "G_SNAME:0 == \"hello\"");
    check(storage.getGlobalInt2D("G_TWO", 1, 2) == 77, "G_TWO:1:2 == 77");
    check(g("R_TWO") == 77, "R_TWO = G_TWO:1:2 == 77");
    check(storage.getGlobalInt3D("G_THREE", 1, 1, 1) == 5, "g_three:1:1:1 == 5");
    check(g("R_THREE") == 5, "R_THREE == 5");

    qDebug() << "\n2) 系统变量 TIME（此前写入后读回恒为 0）";
    check(storage.getSystemVariable("TIME", 3) == 420, "TIME:3 == 420");
    check(g("R_TIME_3") == 420, "R_TIME_3 = TIME:3 == 420");
    check(storage.getSystemVariable("TIME", 0) == 55, "time:0 == 55（小写写）");
    check(g("R_TIME_BARE") == 55, "R_TIME_BARE = TIME == 55（裸 TIME）");
    check(g("R_TIME_0") == 55, "R_TIME_0 = time:0 == 55（小写读）");
    check(storage.getSystemVariable("TIME", 2) == 9, "TIME:2 == 9");
    check(g("R_TIME_2") == 9, "R_TIME_2 == 9");

    qDebug() << "\n3) 角色数据变量：元素互不覆盖 / 省略角色维取 TARGET";
    check(storage.getCharaInt("CFLAG", 3, 10) == 111, "CFLAG:3:10 == 111");
    check(storage.getCharaInt("CFLAG", 3, 11) == 222, "CFLAG:3:11 == 222（不与 :10 冲突）");
    check(g("R_CF_3_10") == 111, "R_CF_3_10 = CFLAG:3:10 == 111（未被 :11 覆盖）");
    check(g("R_CF_3_11") == 222, "R_CF_3_11 == 222");
    check(storage.getCharaInt("CFLAG", 5, 20) == 333, "CFLAG:20 -> CFLAG:TARGET(5):20 == 333");
    check(g("R_CF_T_20") == 333, "R_CF_T_20 == 333");
    check(g("R_CF_T_21") == 0, "R_CF_T_21 == 0（未写的元素仍为 0）");
    check(storage.getCharaInt("TALENT", 7, 2) == 77, "TALENT:7:2 == 77");
    check(g("R_TAL_7_2") == 77, "R_TAL_7_2 == 77");
    check(storage.getCharaInt("NO", 3, 0) == 9 && g("R_NO_3") == 9, "NO:3 == 9（角色标量）");

    qDebug() << "\n4) 用户 #DIM(S) CHARADATA";
    check(storage.getCharaInt("U_CI", 2, 1) == 9, "U_CI:2:1 == 9");
    check(storage.getCharaInt("U_CI", 2, 2) == 8, "U_CI:2:2 == 8（不与 :1 冲突）");
    check(g("R_UCI_2_1") == 9, "R_UCI_2_1 == 9（未被 :2 覆盖）");
    check(g("R_UCI_2_2") == 8, "R_UCI_2_2 == 8");
    check(storage.getCharaStr("U_CS", 2, 1) == QStringLiteral("text"), "U_CS:2:1 == \"text\"");
    check(storage.getCharaStr("U_CS", 2, 2) == QStringLiteral("other"), "U_CS:2:2 == \"other\"");

    qDebug() << "\n5) SWAP 角色变量（按完整下标交换）";
    check(storage.getCharaInt("CFLAG", 4, 1) == 9, "SWAP CFLAG:4:1,CFLAG:4:2 -> :1 == 9");
    check(storage.getCharaInt("CFLAG", 4, 2) == 7, "SWAP CFLAG:4:1,CFLAG:4:2 -> :2 == 7");
    check(g("R_SWAP_1") == 9 && g("R_SWAP_2") == 7, "SWAP 后脚本读回 :1==9, :2==7");

    qDebug() << "\n==============================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] variable-mutability tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
