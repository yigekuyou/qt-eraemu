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

    qDebug() << "\n6) '=（単一代入）与「同指令前缀的变量名」";
    // eraTW：改名用 `NAME:ARG '= RESULTS`，称呼用 `CALLNAME:ARG '= RESULTS`。
    // 两处都曾经失效：
    //   * `'=` 没有被执行链处理 -> 落到「未实现指令静默跳过」；
    //   * `CALLNAME…` 命中 isKnownInstructionName 的 "CALL" 前缀启发式
    //     -> 整行被当成 CALL 族指令，连赋值都不是。
    // 后果：点了改名/称呼没反应；函数内静态串（#DIMS html）不再重置，重绘时越接越长。
    {
        const AstResolver resolveOne = [&table](const QString& e) { return table.expressionAst(e); };
        const auto execOne = [&](const QString& src) {
            LogicalLine l = AstBuilder::build(src, ScriptPosition("t.ERB", 0, 0), resolveOne);
            engine.executeInstruction(l);
            return l;
        };
        const LogicalLine assign = execOne(QStringLiteral("CALLNAME:3 '= \"新称呼\""));
        check(assign.functionName == QLatin1String("'="),
              "CALLNAME:3 '= … 解析为赋值（不再被 CALL 前缀误判为指令）");
        check(storage.getCharaStr(QStringLiteral("CALLNAME"), 3, 0) == QStringLiteral("新称呼"),
              "称呼写入生效");

        // RESULTS 是**全局**字符串数组（C# VariableData.cs:202），不是 LOCALS:0；
        // 这里经全局槽设置，与引擎（求值器 / 赋值 / 函数返回）保持一致。
        storage.setGlobalStr1D(QStringLiteral("RESULTS"), 0, QStringLiteral("新名字"));
        execOne(QStringLiteral("NAME:3 '= RESULTS"));
        check(storage.getCharaStr(QStringLiteral("NAME"), 3, 0) == QStringLiteral("新名字"),
              "NAME:3 '= RESULTS 写入生效");

        // `=` 也有同样的前缀假阳性（eraTW：CALLNAME:MASTER = %CALLNAME:nNo%）
        const LogicalLine eqAssign = execOne(QStringLiteral("CALLNAME:4 = \"克勞恩皮絲\""));
        check(eqAssign.functionName == QLatin1String("="),
              "CALLNAME:4 = … 解析为赋值");
        check(storage.getCharaStr(QStringLiteral("CALLNAME"), 4, 0) == QStringLiteral("克勞恩皮絲"),
              "= 写入生效");

        // 负向：真指令不能被误判成赋值
        check(AstBuilder::build(QStringLiteral("PRINTFORML a = b"), {}, resolveOne)
                  .functionName != QLatin1String("="),
              "PRINTFORML a = b 仍是指令（左值含空白）");
        check(AstBuilder::build(QStringLiteral("CALL FOO, 1"), {}, resolveOne)
                  .functionName == QLatin1String("CALL"),
              "CALL FOO, 1 仍是 CALL 指令");
        check(AstBuilder::build(QStringLiteral("PRINTFORM  ({BASE:MASTER:体力,5})"), {}, resolveOne)
                  .functionName == QLatin1String("PRINTFORM"),
              "PRINTFORM  ({…}) 仍是指令");
    }

    // eraTW TRACHECK_ORGASM.ERB:92 `POWER Multiplier, 2, MultipleEc`：
    // POWER 的**语句形式**是 SP_POWER 指令（<变量>, X, Y -> 变量 = X^Y，3 参），
    // 与式中函数 POWER(X, Y)（2 参）并存。此前语句被「内置函数名开头 -> 整行
    // 按函数调用归约」劫持：既触发双重参数告警（指令侧 3 参只见 1 个操作数、
    // 函数侧 2 参收到 3 个），又丢掉对变量的赋值。
    qDebug() << "\n7) POWER 语句（变量 = X^Y）";
    {
        const AstResolver resolveOne = [&table](const QString& e) { return table.expressionAst(e); };
        LogicalLine p = AstBuilder::build(QStringLiteral("POWER BAG:2, 2, 10"),
                                          ScriptPosition("t.ERB", 0, 0), resolveOne);
        check(p.functionName == QLatin1String("POWER") && !p.isFunctionCall,
              "POWER 语句按指令解析（不再被归约为函数调用）");
        check(!p.argument.hasError(),
              "POWER 语句 3 参通过校验（" + p.argument.typeError + "）");
        storage.setPrivateScope("MAIN", {"BAG"});
        engine.executeInstruction(p);
        check(storage.getGlobalInt1D("BAG", 2) == 1024,
              "POWER BAG:2, 2, 10 -> BAG:2 == 1024（变量被赋值）");
    }


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
    qDebug() << "\n7) VARSET / SETS / CVARSET 族";
    // 以前 ArgKind::VarSet 只在 argument_parser 里登记，执行期没有任何分支：
    // `PRINT_STATE.ERB:336 VARSET TLNT_CNT` 被静默跳过 -> 计数器不清零 ->
    // 素質/性的特徴/身体的特徴 列表越叠越长。这里覆盖各目标类型与范围语义。
    {
        const AstResolver resolveOne = [&table](const QString& e) { return table.expressionAst(e); };
        // 全局声明（头文件级 -> 全局作用域）：1D 整型 / 2D 整型 / 1D 字符串
        table.loadScript("v.ERH",
                         buildLines(table, {"#DIM VSCNT, 6",
                                            "#DIM VSNO, 6, 100",
                                            "#DIMS VSSARR, 4"}),
                         true);
        table.finalizeParse();
        const auto execV = [&](const QString& src) {
            LogicalLine l = AstBuilder::build(src, ScriptPosition("v.ERB", 0, 0), resolveOne);
            engine.executeInstruction(l);
            return l;
        };

        // --- 无范围：end 取 #DIM 长度 ---
        storage.setGlobalInt1D("VSCNT", 0, 9);
        storage.setGlobalInt1D("VSCNT", 5, 9);
        execV(QStringLiteral("VARSET VSCNT"));
        check(storage.getGlobalInt1D("VSCNT", 0) == 0 && storage.getGlobalInt1D("VSCNT", 5) == 0,
              "VARSET VSCNT（无范围）按 #DIM 长度 6 清空整条 1D 数组");

        // --- 2D：忽略范围，整体清空（对齐 C# Int2DVariableToken.SetValueAll）---
        storage.setGlobalInt2D("VSNO", 3, 7, 9);
        execV(QStringLiteral("VARSET VSNO"));
        check(storage.getGlobalInt2D("VSNO", 3, 7) == 0,
              "VARSET VSNO（2D 无范围）整体清空");

        // --- 字符串变量 ---
        storage.setGlobalStr1D("VSSARR", 2, QStringLiteral("q"));
        execV(QStringLiteral("VARSET VSSARR, \"x\""));
        check(storage.getGlobalStr1D("VSSARR", 2) == QStringLiteral("x"),
              "VARSET 字符串数组赋 \"x\"");

        // --- 显式范围 + start>end 自动交换 ---
        for (int i = 1; i <= 4; ++i) storage.setGlobalInt1D("VSCNT", i, 7);
        execV(QStringLiteral("VARSET VSCNT, 5, 3, 1"));   // start=3,end=1 -> 交换 -> [1,3)
        check(storage.getGlobalInt1D("VSCNT", 1) == 5 && storage.getGlobalInt1D("VSCNT", 2) == 5,
              "VARSET 显式范围 [1,3) 赋值，且 start>end 自动交换");
        check(storage.getGlobalInt1D("VSCNT", 3) == 7 && storage.getGlobalInt1D("VSCNT", 4) == 7,
              "范围外元素不受影响");

        // --- 角色变量：VARSET TEQUIP:1:0, 7, 10, 13 ---
        storage.addChara(0);
        storage.addChara(1);
        storage.addChara(2);
        storage.setCharaInt("TEQUIP", 1, 10, 1);
        storage.setCharaInt("TEQUIP", 1, 12, 1);
        execV(QStringLiteral("VARSET TEQUIP:1:0, 7, 10, 13"));
        check(storage.getCharaInt("TEQUIP", 1, 10) == 7 && storage.getCharaInt("TEQUIP", 1, 12) == 7,
              "VARSET 角色变量 TEQUIP:1 [10,13) = 7");

        // --- 系统变量：VARSET ITEM, 0, 101, 110 ---
        storage.setSystemVariable("ITEM", 101, 1);
        storage.setSystemVariable("ITEM", 109, 1);
        execV(QStringLiteral("VARSET ITEM, 0, 101, 110"));
        check(storage.getSystemVariable("ITEM", 101) == 0 && storage.getSystemVariable("ITEM", 109) == 0,
              "VARSET 系统变量 ITEM [101,110) 清空");

        // --- LOCAL 数组 ---
        storage.setLocalInt(0, 3);
        storage.setLocalInt(7, 3);
        execV(QStringLiteral("VARSET LOCAL, 5, 0, 8"));
        check(storage.getLocalInt(0) == 5 && storage.getLocalInt(7) == 5,
              "VARSET LOCAL [0,8) = 5");

        // --- FINDELEMENT 字符串数组：必须按字符串比较 ---
        // eraTW 的 BASE_BAR 用 `FINDELEMENT(BASENAME, "体力")` 取元素下标；
        // 以前被 readIntArray 当整数读 -> 恒 0，于是体力/気力 都指向同一槽，
        // 两根条「一起变」。这里用 #DIMS 字符串数组复现。
        storage.setGlobalStr1D("VSSARR", 0, QStringLiteral("体力"));
        storage.setGlobalStr1D("VSSARR", 1, QStringLiteral("気力"));
        {
            const QSharedPointer<ExpressionNode> ast =
                table.expressionAst(QStringLiteral("FINDELEMENT(VSSARR, \"気力\")"));
            const qint64 idx = ast ? evaluator.evaluate(*ast, &storage, nullptr).toLongLong() : -999;
            check(idx == 1, "FINDELEMENT 字符串数组按字符串比较（気力 -> 1）");

            // 空实参 = 「省略」，不是显式 0：eraTW 的
            //   FINDELEMENT(CLASS_NAME, ESCAPE(ARGS:0), 1, , 1)
            // 第 4 实参省略时取**数组末尾**；若被当 0 则区间 [1,0) 恒空 -> -1
            // -> eraTW 的 EXISTOBJ 抛「未設定の変数です」并卡住新开局。
            storage.setGlobalStr1D("VSSARR", 2, QStringLiteral("Ｃ感"));
            const QSharedPointer<ExpressionNode> ast2 =
                table.expressionAst(QStringLiteral("FINDELEMENT(VSSARR, ESCAPE(\"気力\"), 1, , 1)"));
            const qint64 idx2 = ast2 ? evaluator.evaluate(*ast2, &storage, nullptr).toLongLong() : -999;
            check(idx2 == 1, "FINDELEMENT 第4实参省略 = 数组末尾，且 ESCAPE 模式按正则整串匹配");

            const QSharedPointer<ExpressionNode> ast3 =
                table.expressionAst(QStringLiteral("FINDELEMENT(VSSARR, \"^Ｃ感$\", 1, , 1)"));
            const qint64 idx3 = ast3 ? evaluator.evaluate(*ast3, &storage, nullptr).toLongLong() : -999;
            check(idx3 == 2, "FINDELEMENT 字符串分支按正则匹配（^Ｃ感$ -> 2）");
        }

        // --- CVARSET：逐角色设置同一元素 ---
        storage.setCharaInt("CFLAG", 0, 5, 1);
        storage.setCharaInt("CFLAG", 2, 5, 1);
        execV(QStringLiteral("CVARSET CFLAG, 5, 9, 0, 3"));
        check(storage.getCharaInt("CFLAG", 0, 5) == 9 && storage.getCharaInt("CFLAG", 1, 5) == 9
                  && storage.getCharaInt("CFLAG", 2, 5) == 9,
              "CVARSET CFLAG, 5, 9, 0, 3 逐角色写入元素 5");
    }

    qDebug() << "\n8) 函数语句（一行以内置函数名开头）+ SETBIT 族";
    // 对齐 C# LogicalLineType.Function / METHOD_Instruction：
    //   返回值是整型 -> RESULT，字符串 -> RESULTS:0。
    // eraTW 里 GETMILLISECOND / CURRENTREDRAW / GETTIME / REPLACE / SUBSTRING …
    // 全都是这种写法，以前整行落到「未知指令」被静默丢弃。
    {
        const AstResolver resolveOne = [&table](const QString& e) { return table.expressionAst(e); };
        const auto execOne = [&](const QString& src) {
            LogicalLine l = AstBuilder::build(src, ScriptPosition("f.ERB", 0, 0), resolveOne);
            engine.executeInstruction(l);
            return l;
        };

        // --- 解析定性 ---
        const LogicalLine ts = AstBuilder::build(QStringLiteral("GETMILLISECOND"), {},
                                                 resolveOne);
        check(ts.isFunctionCall && ts.functionName == QLatin1String("GETMILLISECOND"),
              "GETMILLISECOND 解析为「函数语句」");
        check(AstBuilder::build(QStringLiteral("RAND:3 = 5"), {}, resolveOne).functionName
                  == QLatin1String("="),
              "`RAND:3 = 5` 仍是赋值（函数名不得抢走变量赋值）");
        check(!AstBuilder::build(QStringLiteral("PRINTL GETMILLISECOND"), {}, resolveOne)
                   .isFunctionCall,
              "PRINTL … 不被误判为函数语句");

        // --- 整型返回值写 RESULT ---
        execOne(QStringLiteral("GETMILLISECOND"));
        check(storage.getSystemVariable("RESULT", 0) > 0,
              "GETMILLISECOND -> RESULT（毫秒时间戳 > 0）");

        // --- 系统变量查询 ---
        execOne(QStringLiteral("CURRENTREDRAW"));
        check(storage.getSystemVariable("RESULT", 0) == 1, "CURRENTREDRAW -> RESULT == 1");
        execOne(QStringLiteral("GETTIME"));
        check(storage.getSystemVariable("RESULT", 0) > 0, "GETTIME -> RESULT > 0");

        // --- 带实参 + 字符串返回值写 RESULTS:0 ---
        // REPLACE 的第 2 引数是**正则**（C# ReplaceMethod 用 new Regex()）；
        // eraTW 用它去首尾空格：REPLACE LOCALS, "(^ +| +$)", ""
        // RESULTS 全局槽（见上）；REPLACE/SUBSTRING 语句形式读写同一槽。
        storage.setGlobalStr1D(QStringLiteral("RESULTS"), 0, QStringLiteral("  x  "));
        execOne(QStringLiteral("REPLACE RESULTS, \"(^ +| +$)\", \"\""));
        check(storage.getGlobalStr1D(QStringLiteral("RESULTS"), 0) == QStringLiteral("x"),
              "REPLACE RESULTS, 正则去首尾空格 -> RESULTS:0");
        storage.setGlobalStr1D(QStringLiteral("RESULTS"), 0, QStringLiteral("a//b"));
        execOne(QStringLiteral("REPLACE RESULTS, \"/+\", \"/\""));
        check(storage.getGlobalStr1D(QStringLiteral("RESULTS"), 0) == QStringLiteral("a/b"),
              "REPLACE 正则 /+ -> /（斜杠压缩）");

        storage.setGlobalStr1D(QStringLiteral("RESULTS"), 0, QStringLiteral("  x  "));
        execOne(QStringLiteral("SUBSTRING RESULTS, 2, 2"));
        check(storage.getGlobalStr1D(QStringLiteral("RESULTS"), 0) == QStringLiteral("x "),
              "SUBSTRING RESULTS, 2, 2 -> RESULTS:0");

        // --- STRLENFORM：实参按格式化串展开再取长度 ---
        storage.setGlobalStr1D(QStringLiteral("RESULTS"), 0, QStringLiteral("abcd"));
        execOne(QStringLiteral("STRLENFORMU RESULTS"));
        check(storage.getSystemVariable("RESULT", 0) == 4,
              "STRLENFORMU RESULTS -> RESULT == 4");

        // --- SETBIT / CLEARBIT / INVERTBIT ---
        storage.setGlobalInt1D("BITS", 0, 0);
        execOne(QStringLiteral("SETBIT BITS, 3"));
        check(storage.getGlobalInt1D("BITS", 0) == 8, "SETBIT BITS, 3 -> 8");
        execOne(QStringLiteral("SETBIT BITS, 0, 1"));
        check(storage.getGlobalInt1D("BITS", 0) == 11, "SETBIT BITS, 0, 1 -> 11");
        execOne(QStringLiteral("CLEARBIT BITS, 0"));
        check(storage.getGlobalInt1D("BITS", 0) == 10, "CLEARBIT BITS, 0 -> 10");
        execOne(QStringLiteral("INVERTBIT BITS, 1"));
        check(storage.getGlobalInt1D("BITS", 0) == 8, "INVERTBIT BITS, 1 -> 8");

        // --- 角色变量 + 下标表达式（eraTW：SETBIT CFLAG:C_ID:口上実装状況, N）---
        // 上面的 CVARSET 用例已把 CFLAG:0:5 写成 9（位 0 与位 3 已置位），
        // 再置位 4 -> 9 | 16 == 25。
        execOne(QStringLiteral("SETBIT CFLAG:0:5, 4"));
        check(storage.getCharaInt("CFLAG", 0, 5) == 25,
              "SETBIT CFLAG:0:5, 4 -> 25（角色变量 + 下标，位运算叠加）");
    }

    qDebug() << "\n======================================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] statement tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
