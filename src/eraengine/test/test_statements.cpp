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
// test_statements.cpp
//
// 语句/变量语义回归（都是真实游戏暴露出来的缺陷）：
//   1. `A = BAG:(I++)`：自增的**副作用**与后置语义（返回旧值）
//   2. `SWAP a, b`：交换两个变量（含变量下标）
//   3. 变量下标赋值：`BAG:I = x`、`BAG:I + 1 = x` 形式
//   4. `#DIM CONST C = 5`：常量参与表达式（此前读作 0）
//   5. `#DIM N = 7`（函数私有初值）：每次进入函数时写入
//   6. FOR + CONTINUE 必须能终止（此前 CONTINUE 跳回循环体开头 → 死循环）
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

static QList<LogicalLine> buildLines(EraParseTable& table, const QStringList& src) {
    const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
    QList<LogicalLine> out;
    for (int i = 0; i < src.size(); ++i) {
        out.append(AstBuilder::build(src.at(i), ScriptPosition("s.ERB", i, 0), resolve));
    }
    return out;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Statement/variable semantics regression";
    qDebug() << "======================================";

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
    runner.setStepLimit(200000);   // 死循环保护（回归用）

    const QStringList src = {
        "@MAIN",                          // 0
        "#DIM CONST BLK_O = 3",           // 1
        "#DIM CONST BLK_S = 4",           // 2
        "#DIM BAG, 8",                    // 3
        "I = 0",                          // 4
        "BAG:0 = 10",                     // 5
        "BAG:1 = 20",                     // 6
        "A = BAG:(I++)",                  // 7  后置自增：A=10, I=1
        "B = BAG:(I++)",                  // 8  后置自增：B=20, I=2
        "SWAP BAG:0, BAG:1",              // 9  交换：BAG0=20, BAG1=10
        "J = 1",                          // 10
        "BAG:J = 99",                     // 11 变量下标赋值
        "CONST_SUM = BLK_O + BLK_S",      // 12 常量参与表达式 = 7
        "CALL F",                         // 13
        "TOTAL = 0",                      // 14
        "FOR K, 0, 5",                    // 15
        "	SIF K < 3",                   // 16
        "		CONTINUE",                // 17
        "	TOTAL = TOTAL + 1",           // 18
        "NEXT",                           // 19
        "",                               // 20
        "@F",                             // 21
        "#DIM N = 7",                     // 22 函数私有初值
        "NVAL = N",                       // 23
        "RETURN"                          // 24
    };
    check(table.loadScript("s", buildLines(table, src)), "loadScript");
    table.setEntryPoint("MAIN");
    table.finalizeParse();

    const ExecState st = runner.runToCompletion();
    check(st == ExecState::Halt, "脚本正常结束（无死循环）");

    qDebug() << "\n1) 自增副作用与后置语义";
    check(storage.getSystemVariable("A", 0) == 10, "A = BAG:(I++) -> 10（旧值）");
    check(storage.getSystemVariable("B", 0) == 20, "B = BAG:(I++) -> 20（旧值）");
    check(storage.getGlobalInt1D("I", 0) == 2, "I 自增两次 -> 2（副作用生效）");

    qDebug() << "\n2) SWAP / 变量下标赋值";
    // BAG is private to MAIN; inspect it in its owning scope after execution.
    storage.setPrivateScope("MAIN", {"BAG"});
    check(storage.getGlobalInt1D("BAG", 1) == 99 || storage.getGlobalInt1D("BAG", 1) == 10,
          "BAG:J = 99（变量下标 J=1）");
    check(storage.getGlobalInt1D("BAG", 1) == 99, "BAG:1 == 99");
    check(storage.getGlobalInt1D("BAG", 0) == 20, "SWAP 后 BAG:0 == 20");

    qDebug() << "\n3) #DIM CONST 常量";
    check(storage.getGlobalInt1D("CONST_SUM", 0) == 7, "BLK_O + BLK_S == 7");

    qDebug() << "\n4) 函数私有初值（每次进入函数）";
    check(storage.getGlobalInt1D("NVAL", 0) == 7, "NVAL == 7（N 初值生效）");

    qDebug() << "\n5) FOR + CONTINUE 能终止";
    check(storage.getGlobalInt1D("TOTAL", 0) == 2,
          "K=0,1,2 CONTINUE；K=3,4 计数 -> TOTAL == 2");


    // ecd/docs/translation/Command.html: PRINTBUTTON accepts integer or string values.
    {
        QString text, stringValue;
        qint64 integerValue = -1;
        bool isString = false;
        int emitted = 0;
        const auto connection = QObject::connect(&engine, &ExecutionEngine::consolePrintButton,
            [&](const QString& t, qint64 i, const QString& s, bool str) {
                text = t; integerValue = i; stringValue = s; isString = str; ++emitted;
            });
        auto execute = [&](const QString& source) {
            engine.executeInstruction(buildLines(table, {source}).first());
        };
        execute("PRINTBUTTON \"A\", \"123\"");
        check(text == "A" && isString && stringValue == "123",
              "button literals retain text and string input type");
        execute("PRINTBUTTON \"[穗月]\", \"穗月\"");
        check(text == "[穗月]" && isString && stringValue == "穗月",
              "documented name button preserves string value");
        execute("PRINTBUTTON \"\", 40 + 2");
        check(text.isEmpty() && !isString && integerValue == 42,
              "empty button text and numeric expression");
        execute("PRINTBUTTON \"a\" + \"b\", 7");
        check(text == "ab" && !isString && integerValue == 7,
              "button string concatenation is an expression");
        check(emitted == 4, "each button instruction emits exactly once");
        QObject::disconnect(connection);
    }

    // ecd/docs/reference/ERB_Statements.html / C# PRINT_IMG: one string
    // expression is forwarded as an inline resource image.
    {
        QString imageName;
        int imageWidth = -1, imageHeight = -1, imageY = -1;
        const auto connection = QObject::connect(&engine, &ExecutionEngine::consolePrintImage,
            [&](const QString& name, int width, int height, int ypos) {
                imageName = name; imageWidth = width; imageHeight = height; imageY = ypos;
            });
        engine.executeInstruction(buildLines(table, {"PRINT_IMG \"face_01\""}).first());
        check(imageName == "face_01" && imageWidth == 0 && imageHeight == 0 && imageY == 0,
              "PRINT_IMG forwards the resource expression with inline defaults");
        QObject::disconnect(connection);
    }
    qDebug() << "\n======================================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] statement tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
