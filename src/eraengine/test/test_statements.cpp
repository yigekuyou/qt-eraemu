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

        // 字符串变量形态（eraTW 惯用法：资源名几乎总是变量）：
        // 此前 PRINT_IMG 一律走整数上下文 evaluate() -> 字符串变量得到 0
        // -> QML 去找 image://emuera/0（provider 失败日志刷屏）。
        // 现在按 C# 的 Term.GetStrValue 走字符串求值。
        //（RESULTS = 引擎里现成的全局字符串变量）
        storage.setGlobalStr1D(QStringLiteral("RESULTS"), 0, QStringLiteral("face_02"));
        engine.executeInstruction(buildLines(table, {"PRINT_IMG RESULTS"}).first());
        check(imageName == "face_02",
              "PRINT_IMG <字符串变量> 取变量内容（不是 0）");
        QObject::disconnect(connection);
    }
    // ecd/docs/reference/ERB_Commands.html：BAR / BARL / SETCOLORBYNAME /
    // CLEARTEXTBOX 此前只在解析表登记，运行期落到「未完成」被静默忽略。
    {
        QString printed;
        bool printedNewline = false;
        int colorEmits = 0;
        QString lastName;
        int clearEmits = 0;
        const auto c1 = QObject::connect(&engine, &ExecutionEngine::consolePrint,
            [&](const QString& t, bool nl) { printed = t; printedNewline = nl; });
        const auto c2 = QObject::connect(&engine, &ExecutionEngine::consoleColor,
            [&](const QString& n) { ++colorEmits; lastName = n; });
        const auto c3 = QObject::connect(&engine, &ExecutionEngine::clearTextBox,
            [&] { ++clearEmits; });
        evaluator.setBarChars(QLatin1Char('*'), QLatin1Char('.'));

        engine.executeInstruction(buildLines(table, {"BAR 50, 100, 10"}).first());
        check(printed == QStringLiteral("[*****.....]") && !printedNewline,
              QStringLiteral("BAR <值>,<最大>,<长度> 画进度条（不换行） got=%1").arg(printed));
        engine.executeInstruction(buildLines(table, {"BARL 0, 100, 4"}).first());
        check(printed == QStringLiteral("[....]") && printedNewline, "BARL = BAR + 换行");
        engine.executeInstruction(buildLines(table, {"BAR 1, 0, 4"}).first());
        check(printed.isEmpty(), "BAR 最大值 0 -> 空串（C# 是 CodeEE，这里保守不报错）");

        engine.executeInstruction(buildLines(table, {"SETCOLORBYNAME \"RED\""}).first());
        check(colorEmits == 1 && lastName == QStringLiteral("RED"),
              "SETCOLORBYNAME 按色名设置颜色（C# Color.FromName）");
        engine.executeInstruction(buildLines(table, {"CLEARTEXTBOX"}).first());
        check(clearEmits == 1, "CLEARTEXTBOX 发出清空输入栏请求");
        QObject::disconnect(c1);
        QObject::disconnect(c2);
        QObject::disconnect(c3);
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

        // --- STRLENFORM：实参是 FORM_STR（C# STRLEN_Instruction(argisform=true) ->
        //     FunctionArgType.FORM_STR_NULLABLE）。**无 %…%/{…} 标记时整个实参是
        //     字面量**（IsConst/ConstStr），不是变量引用；要取值须写 %…%
        //     （eraTW：`STRLENFORM %ForagePlaceName(SpotID)%`）。
        storage.setGlobalStr1D(QStringLiteral("RESULTS"), 0, QStringLiteral("abcd"));
        execOne(QStringLiteral("STRLENFORMU RESULTS"));
        check(storage.getSystemVariable("RESULT", 0) == 7,
              "STRLENFORMU RESULTS -> 字面量 \"RESULTS\" 的长度 == 7");
        execOne(QStringLiteral("STRLENFORMU %RESULTS%"));
        check(storage.getSystemVariable("RESULT", 0) == 4,
              "STRLENFORMU %RESULTS% -> RESULTS 展开后长度 == 4");
        execOne(QStringLiteral("STRLENFORM %RESULTS%"));
        check(storage.getSystemVariable("RESULT", 0) == 4,
              "STRLENFORM %RESULTS% -> 语言编码字节数 == 4");

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

    // =====================================================================
    // 字符串 `=` 赋值的右值 = 格式化串（裸文本一律字面量，即使与**整数**
    // 变量/常量同名）；只有裸**字符串**变量名才按变量引用求值。
    // 回归：eraTW `DIM.ERH:91 #DIM CONST 斜角的竹林 = 430`（地图地点编号）
    // 与 `@ForagePlaceName` 的 `LOCALS = 斜角的竹林`（地点名字符串）**同名
    // 碰撞** —— 旧实现把任何「已知变量」都当引用，于是「採集場所一覧」整列
    // 显示成 430/460/470… 数字，且把 PLACE 名判断 `!= ""` 永远当真，
    // 输入 99 无法返回。
    qDebug() << "\n9) 字符串 = 赋值：整数名当字面量，字符串名当引用";
    {
        VariableStorage vs;
        ProcessState ps;
        EraParseTable pt(&ps);
        ExecutionEngine ex(&vs, nullptr);
        ExpressionEvaluator ev;
        ex.setParseTable(&pt); ex.setExpressionEvaluator(&ev);
        pt.setVariableStorage(&vs); pt.setExpressionEvaluator(&ev);
        ScriptRunner run(&pt, &ex, &ps, &vs);
        run.setExpressionEvaluator(&ev);
        run.setStepLimit(10000);
        const QStringList program = {
            QStringLiteral("@MAIN"),
            QStringLiteral("#DIM CONST 斜角的竹林 = 430"),   // 与地点名字符串同名的整数常量
            QStringLiteral("#DIMS OUT_LIT"),
            QStringLiteral("#DIMS OUT_SYS"),
            QStringLiteral("#DIMS OUT_REF"),
            QStringLiteral("#DIMS SRC_STR"),
            QStringLiteral("SRC_STR = \"值\""),
            QStringLiteral("OUT_LIT = 斜角的竹林"),          // 整数常量名 -> 字面量
            QStringLiteral("OUT_SYS = FLAG"),                // 整数系统变量名 -> 字面量
            QStringLiteral("OUT_REF = SRC_STR"),             // 字符串变量名 -> 取当前值
            QStringLiteral("RETURN")
        };
        check(pt.loadScript("strap", buildLines(pt, program)), "load string-assignment script");
        pt.finalizeParse();
        pt.setEntryPoint("MAIN");
        check(run.runToCompletion() == ExecState::Halt, "string-assignment script completes");
        vs.setPrivateScope(QStringLiteral("MAIN"),
                           {QStringLiteral("OUT_LIT"), QStringLiteral("OUT_SYS"),
                            QStringLiteral("OUT_REF"), QStringLiteral("SRC_STR")});
        check(vs.getGlobalStr1D(QStringLiteral("OUT_LIT"), 0) == QStringLiteral("斜角的竹林"),
              "OUT_LIT = 斜角的竹林（整数常量名）-> 字面量，而非常量值 430");
        check(vs.getGlobalStr1D(QStringLiteral("OUT_SYS"), 0) == QStringLiteral("FLAG"),
              "OUT_SYS = FLAG（整数系统变量名）-> 字面量 \"FLAG\"");
        check(vs.getGlobalStr1D(QStringLiteral("OUT_REF"), 0) == QStringLiteral("值"),
              "OUT_REF = SRC_STR（字符串变量名）-> 取该变量当前值");
    }

    // =====================================================================
    // 字符串 = 赋值：带引号的字符串字面量（"…" / @"…"）引号是定界符，要剥掉。
    // 回归：eraTW `LOCALS = @"[目瞳:…][表情:…]"`（精灵名）此前落到**格式化串**
    // 路径，`@"` 与引号原样落进变量；同一变量在别处又用 `LOCALS = 倒錯的`
    // （裸文本）/ `LOCALS = "Ｃ感度"`（引号）——三者必须等价才自洽。
    qDebug() << "\n10) 字符串 = 赋值：\"…\" 与 @\"…\" 都剥引号（@\"…\" 展开 %…%）";
    {
        VariableStorage vs;
        ProcessState ps;
        EraParseTable pt(&ps);
        ExecutionEngine ex(&vs, nullptr);
        ExpressionEvaluator ev;
        ex.setParseTable(&pt);
        ex.setExpressionEvaluator(&ev);
        pt.setVariableStorage(&vs);
        pt.setExpressionEvaluator(&ev);
        ScriptRunner run(&pt, &ex, &ps, &vs);
        run.setExpressionEvaluator(&ev);
        run.setStepLimit(10000);
        const QStringList program = {
            QStringLiteral("@MAIN"),
            QStringLiteral("#DIM N"),
            QStringLiteral("#DIMS Q"),
            QStringLiteral("#DIMS F"),
            QStringLiteral("#DIMS B"),
            QStringLiteral("#DIMS E"),
            QStringLiteral("N = 7"),
            QStringLiteral("Q = \"quote\""),          // 字符串字面量 -> quote
            QStringLiteral("F = @\"lit_%N%\""),        // 格式化字符串字面量 -> lit_7
            QStringLiteral("B = bare_%N%"),            // 裸格式化串 -> bare_7
            QStringLiteral("E = @\"[PN:%N%]\""),       // eraTW 精灵名样式 -> [PN:7]
            QStringLiteral("RETURN")
        };
        check(pt.loadScript("qstr", buildLines(pt, program)), "load quoted string-assignment script");
        pt.finalizeParse();
        pt.setEntryPoint("MAIN");
        check(run.runToCompletion() == ExecState::Halt, "quoted string-assignment script completes");
        vs.setPrivateScope(QStringLiteral("MAIN"),
                           {QStringLiteral("Q"), QStringLiteral("F"), QStringLiteral("B"),
                            QStringLiteral("E")});
        check(vs.getGlobalStr1D(QStringLiteral("Q"), 0) == QStringLiteral("quote"),
              "Q = \"quote\" -> quote（引号是定界符）");
        check(vs.getGlobalStr1D(QStringLiteral("F"), 0) == QStringLiteral("lit_7"),
              "F = @\"lit_%N%\" -> lit_7（剥 @\"…\" 并展开 %…%）");
        check(vs.getGlobalStr1D(QStringLiteral("B"), 0) == QStringLiteral("bare_7"),
              "B = bare_%N% -> bare_7（裸格式化串）");
        check(vs.getGlobalStr1D(QStringLiteral("E"), 0) == QStringLiteral("[PN:7]"),
              "E = @\"[PN:%N%]\" -> [PN:7]（eraTW 精灵名样式）");
    }

    // 8) eraMegaten 解析回归（本次修复；原报 705 条「Expected #」+ 19 条「未识别的指令」）
    qDebug() << "\n8) eraMegaten 解析回归（'= 空格 LHS / 全角空格断词 / 括号内 == 的自增）";
    {
        const AstResolver resolveOne = [&table](const QString& e) { return table.expressionAst(e); };

        // (a) '= 的 LHS 允许含空格：`LOCALS:(LOCAL + 1) '= …` 此前整行被丢弃
        const LogicalLine s1 = AstBuilder::build(
            QStringLiteral("LOCALS:(LOCAL + 1) '= \" \" * 8 + \"x\""), {}, resolveOne);
        check(s1.functionName == QLatin1String("'="),
              "LOCALS:(LOCAL + 1) '= … 解析为 '= 赋值（LHS 含空格）");
        check(s1.arguments.size() == 2
                  && s1.arguments.at(0).raw.trimmed() == QStringLiteral("LOCALS:(LOCAL + 1)"),
              "LHS 原样保留（含表达式下标）");
        const LogicalLine s1b = AstBuilder::build(
            QStringLiteral("CSTR:ARG:(29 + RESULT) '= RESULTS"), {}, resolveOne);
        check(s1b.functionName == QLatin1String("'="),
              "CSTR:ARG:(29 + RESULT) '= RESULTS 解析为 '= 赋值");

        // (b) 全角空格 U+3000 断词：`IF　絶頂変動値:…` 此前把指令名吞成 "IF　絶頂変動値"
        const LogicalLine s2 = AstBuilder::build(
            QStringLiteral("IF\u3000絶頂変動値:0:(LOCAL+1)"), {}, resolveOne);
        check(s2.functionName == QLatin1String("IF"),
              "IF<U+3000>… 按 IF 指令解析（全角空格断词）");
        check(s2.condition != nullptr, "IF 的条件表达式已归约");

        // (c) 后缀自增：括号内的 == 不应否掉整条语句
        const LogicalLine s3 = AstBuilder::build(
            QStringLiteral("LOCAL:(CFLAG:(FLAG:LOCALS):ゲスト加入フラグ == 0)++"), {}, resolveOne);
        check(s3.functionName == QLatin1String("++"),
              "LOCAL:(… == 0)++ 仍是自增语句（== 在括号内）");
        // 负向：顶层 == 的 `IF A == B++` 不是自增语句
        const LogicalLine s4 = AstBuilder::build(QStringLiteral("IF A == B++"), {}, resolveOne);
        check(s4.functionName != QLatin1String("++"), "IF A == B++ 不是自增语句");

        // (d) 赋值右值走**静默** resolver（临时解析不刷语法错误；AST 与原来一致）
        bool loudCalled = false, quietCalled = false;
        const AstResolver loudR = [&](const QString&) -> QSharedPointer<ExpressionNode> {
            loudCalled = true; return {};
        };
        const AstResolver quietR = [&](const QString&) -> QSharedPointer<ExpressionNode> {
            quietCalled = true; return {};
        };
        const LogicalLine s5 = AstBuilder::build(
            QStringLiteral("CSTR:ARG:(29 + RESULT) '= RESULTS"), {}, loudR, quietR);
        check(s5.functionName == QLatin1String("'=") && quietCalled && !loudCalled,
              "赋值右值走静默 resolver（loud 未被调用）");
        // 负向：非赋值行（如 IF 的条件）仍走 loud resolver
        loudCalled = quietCalled = false;
        AstBuilder::build(QStringLiteral("IF A == B"), {}, loudR, quietR);
        check(loudCalled && !quietCalled, "非赋值行（IF 条件）仍走 loud resolver");
    }

    qDebug() << "\n======================================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] statement tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
