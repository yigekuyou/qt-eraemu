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
// test_tag_print_chain.cpp
//
// FL3.8 `TAG_PRINT` → `PRINTBUTTON` 链条的可执行回归。
//
// 现场：颜色屏（ポートレイト制御.ERB:329）
//     CALLF TAG_PRINT(@"@B:999999999@[决定是这个颜色]@/B@", 5)
// 文字打出来了、按钮没有 → ONEINPUTS 永远等不到 999999999，界面卡死。
// 分支级定位（Function_TagSetText.ERB:879-884）已确认实际走 ELSE
// （`PRINTPLAINFORM %strText%`），即 `arrTagInfo:1` 为空或非数字。
//
// 本测试把该分支依赖的每个引擎原语单独钉住，再接整条链：
//   1) SUBSTRINGU(str, 0, 1) / SUBSTRINGU(str, 1, -1)   —— 标签解除判定
//   2) SPLIT str, ":", LOCALS                           —— 标签拆分（写 LOCALS 槽）
//   3) SELECTCASE <字符串> / CASE "B"                    —— 标签类型分派
//   4) `#DIMS REF arr, 0` 形参写回调用方                 —— arrTagInfo 的传递
//   5) `@"…"` 書式付き文字列リテラル                      —— `{…}`/`%…%` 在进 TAG_PRINT 前展开
//   6) 整链：TAG_PRINT(@"@B:999999999@[X]@/B@", 5) 必须产出
//      文本 "[X]" + 整数按钮 999999999
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
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

// FL3.8 用到的全局观测槽（#DIMS 在 ERH 里 = 大域字符串数组）
static const QStringList kHeader = {
    QStringLiteral("#DIMS OBS_PRIM_B, 4"),
    QStringLiteral("#DIMS OBS_PRIM_SLASH, 4"),
    QStringLiteral("#DIMS OBS_SPLIT, 4"),
    QStringLiteral("#DIMS OBS_SEL, 4"),
    QStringLiteral("#DIMS OBS_REF, 4"),
    QStringLiteral("#DIMS OBS_FORM, 4"),
    QStringLiteral("#DIMS O, 8"),
    QStringLiteral("#DIMS OA, 8"),
    QStringLiteral("#DIM IA, 8"),
    QStringLiteral("#DIMS IV, 4"),
    QStringLiteral("#DIMS OBS_REF_A, 4"),
    QStringLiteral("#DIMS OBS_REF_B, 4"),
};

// 被测函数：除「搬运」以外与 FL3.8 逐行同形。
//   P_PRIM_B / P_PRIM_SLASH —— SUBSTRINGU（Function_TagSetText.ERB:402/404）
//   P_SPLIT                 —— SPLIT strText, ":", LOCALS（:409-411）
//   P_SEL                   —— SELECTCASE strTagType / CASE "B"（:419-425）
//   P_REF                   —— #DIMS REF arr, 0 写回
//   P_FORM                  —— @"…" 書式付き文字列
//   FTAG_PRINT / FTAG_TAGSETTEXT / FTAG_SETTAGINFO
//                           —— FL3.8 三函数原样副本（只改名加前缀）
static const QStringList kScript = {
    QStringLiteral("@MAIN"),
    QStringLiteral("CALLF P_PRIM_B()"),
    QStringLiteral("CALLF P_PRIM_SLASH()"),
    QStringLiteral("CALLF P_SPLIT()"),
    QStringLiteral("CALLF P_SEL(\"B\")"),
    QStringLiteral("CALLF P_REF(O)"),
    QStringLiteral("OA:0 '= \"preset\""),
    QStringLiteral("CALLF P_REF_A(OA)"),
    QStringLiteral("CALLF P_REF_B()"),
    QStringLiteral("CALLF P_REF_I(IV)"),
    QStringLiteral("CALLF P_REF_IA(IA)"),
    QStringLiteral("CALLF P_FORM()"),
    QStringLiteral("CALLF FTAG_PRINT(@\"@B:999999999@[X]@/B@\", 5)"),
    QStringLiteral("RETURN"),

    // ---- 1) SUBSTRINGU ----
    QStringLiteral("@P_PRIM_B()"),
    QStringLiteral("#FUNCTION"),
    QStringLiteral("OBS_PRIM_B:0 '= SUBSTRINGU(\"B:999999999\", 0, 1)"),
    QStringLiteral("OBS_PRIM_B:1 '= SUBSTRINGU(\"B:999999999\", 1, -1)"),
    QStringLiteral("OBS_PRIM_B:2 = STRLENS(SUBSTRINGU(\"B:999999999\", 0, 1))"),
    QStringLiteral("RETURNF 0"),

    QStringLiteral("@P_PRIM_SLASH()"),
    QStringLiteral("#FUNCTION"),
    QStringLiteral("OBS_PRIM_SLASH:0 '= SUBSTRINGU(\"/B\", 0, 1)"),
    QStringLiteral("OBS_PRIM_SLASH:1 '= SUBSTRINGU(\"/B\", 1, -1)"),
    QStringLiteral("RETURNF 0"),

    // ---- 2) SPLIT → LOCALS ----
    QStringLiteral("@P_SPLIT()"),
    QStringLiteral("#FUNCTION"),
    QStringLiteral("VARSET LOCALS, \"\""),
    QStringLiteral("SPLIT \"B:999999999\", \":\", LOCALS"),
    QStringLiteral("OBS_SPLIT:0 '= LOCALS:0"),
    QStringLiteral("OBS_SPLIT:1 '= LOCALS:1"),
    QStringLiteral("OBS_SPLIT:2 '= TOSTR(RESULT)"),
    QStringLiteral("RETURNF 0"),

    // ---- 3) 字符串 SELECTCASE ----
    QStringLiteral("@P_SEL(T)"),
    QStringLiteral("#FUNCTION"),
    QStringLiteral("#DIMS T"),
    QStringLiteral("SELECTCASE T"),
    QStringLiteral("CASE \"B\""),
    QStringLiteral("OBS_SEL:0 '= \"hit\""),
    QStringLiteral("CASEELSE"),
    QStringLiteral("OBS_SEL:0 '= \"miss\""),
    QStringLiteral("ENDSELECT"),
    QStringLiteral("RETURNF 0"),

    // ---- 4) REF 数组形参写回 ----
    QStringLiteral("@P_REF(OUT)"),
    QStringLiteral("#FUNCTION"),
    QStringLiteral("#DIMS REF OUT, 0"),
    QStringLiteral("OUT:0 '= \"written\""),
    QStringLiteral("OUT:1 = 42"),
    QStringLiteral("OBS_REF:2 '= OUT:0"),
    QStringLiteral("RETURNF 0"),

    // 变体 A：REF 且不写长度
    QStringLiteral("@P_REF_A(OUT)"),
    QStringLiteral("#FUNCTION"),
    QStringLiteral("#DIMS REF OUT"),
    QStringLiteral("OBS_REF_A:1 '= OUT:0"),
    QStringLiteral("OUT:0 '= \"no-size\""),
    QStringLiteral("OBS_REF_A:0 '= OUT:0"),
    QStringLiteral("RETURNF 0"),

    // 变体 B：非 REF，私有数组声明长度 4（对照）
    QStringLiteral("@P_REF_B()"),
    QStringLiteral("#FUNCTION"),
    QStringLiteral("#DIMS OUT, 4"),
    QStringLiteral("OUT:0 '= \"plain\""),
    QStringLiteral("OBS_REF_B:0 '= OUT:0"),
    QStringLiteral("RETURNF 0"),

    // REF 标量（eraTW 画像合成 / FL3.8 的 `#DIM REF` 整数写法）
    QStringLiteral("@P_REF_I(V)"),
    QStringLiteral("#FUNCTION"),
    QStringLiteral("#DIM REF V"),
    QStringLiteral("V = 77"),
    QStringLiteral("RETURNF 0"),

    // REF 到整数数组（`#DIM REF` 的数组写法）
    QStringLiteral("@P_REF_IA(OUT)"),
    QStringLiteral("#FUNCTION"),
    QStringLiteral("#DIM REF OUT"),
    QStringLiteral("OUT:0 = 55"),
    QStringLiteral("RETURNF 0"),

    // ---- 5) @"…" 書式付き文字列 ----
    QStringLiteral("@P_FORM()"),
    QStringLiteral("#FUNCTION"),
    QStringLiteral("OBS_FORM:0 '= @\"@B:{1+2}@[X]@/B@\""),
    QStringLiteral("OBS_FORM:1 '= @\"@C:{3*4}@@/C@\""),
    QStringLiteral("RETURNF 0"),

    // ---- 6) FL3.8 三函数原样副本 ----
    QStringLiteral("@FTAG_PRINT( strText, nExFlag )"),
    QStringLiteral("#FUNCTION"),
    QStringLiteral("#DIMS strText"),
    QStringLiteral("#DIM  nExFlag"),
    QStringLiteral("#DIMS arrTagInfoDefault, 20"),
    QStringLiteral("#DIMS arrTagInfo, 20"),
    QStringLiteral("#DIMS strSplitBuf, 500"),
    QStringLiteral("#DIM  nSplitCount"),
    QStringLiteral("#DIM  nLoop"),
    QStringLiteral("#DIM  nBtnIsNum"),
    QStringLiteral("VARSET arrTagInfoDefault, \"\""),
    QStringLiteral("arrTagInfoDefault:0 '= \"0x\" + TOSTR( GETCOLOR(), \"X6\" )"),
    QStringLiteral("arrTagInfoDefault:2 '= GETFONT()"),
    QStringLiteral("arrTagInfoDefault:4 '= TOSTR( GETSTYLE() )"),
    QStringLiteral("VARSET arrTagInfo, \"\""),
    QStringLiteral("VARSET strSplitBuf, \"\""),
    QStringLiteral("nBtnIsNum = ((nExFlag & 0x04) ? 1 # 0)"),
    QStringLiteral("SPLIT strText, \"@\", strSplitBuf"),
    QStringLiteral("nSplitCount = RESULT"),
    QStringLiteral("FOR nLoop, 0, nSplitCount"),
    QStringLiteral("	IF nLoop % 2 == 0"),
    QStringLiteral("		CALLF FTAG_TAGSETTEXT( strSplitBuf:nLoop, arrTagInfo, nBtnIsNum )"),
    QStringLiteral("	ELSE"),
    QStringLiteral("		SIF (nExFlag & 0x02) > 0"),
    QStringLiteral("			CONTINUE"),
    QStringLiteral("		CALLF FTAG_SETTAGINFO( strSplitBuf:nLoop, arrTagInfo )"),
    QStringLiteral("		CALLF FTAG_TAGSETTEXT( \"\", arrTagInfoDefault, nBtnIsNum )"),
    QStringLiteral("	ENDIF"),
    QStringLiteral("NEXT"),
    QStringLiteral("CALLF FTAG_TAGSETTEXT( \"\", arrTagInfoDefault, nBtnIsNum )"),
    QStringLiteral("IF (nExFlag & 0x01) > 0"),
    QStringLiteral("	PRINTL"),
    QStringLiteral("ENDIF"),
    QStringLiteral("RETURNF 0"),

    QStringLiteral("@FTAG_TAGSETTEXT( strText, arrTagInfo, nBtnIsNum )"),
    QStringLiteral("#FUNCTION"),
    QStringLiteral("#DIMS strText"),
    QStringLiteral("#DIMS REF arrTagInfo, 0"),
    QStringLiteral("#DIM  nBtnIsNum"),
    QStringLiteral("#DIM  nBuf"),
    QStringLiteral("IF STRLENS( arrTagInfo:0 ) > 0"),
    QStringLiteral("	IF ISNUMERIC( arrTagInfo:0 ) == 1"),
    QStringLiteral("		nBuf = TOINT( arrTagInfo:0 )"),
    QStringLiteral("		IF nBuf >= 0"),
    QStringLiteral("			SETCOLOR nBuf"),
    QStringLiteral("		ELSE"),
    QStringLiteral("			RESETCOLOR"),
    QStringLiteral("		ENDIF"),
    QStringLiteral("	ENDIF"),
    QStringLiteral("ENDIF"),
    QStringLiteral("IF STRLENS( arrTagInfo:2 ) > 0"),
    QStringLiteral("	IF CHKFONT( arrTagInfo:2 ) == 1"),
    QStringLiteral("		SETFONT arrTagInfo:2"),
    QStringLiteral("	ENDIF"),
    QStringLiteral("ENDIF"),
    QStringLiteral("IF STRLENS( arrTagInfo:4 ) > 0"),
    QStringLiteral("	nBuf = TOINT( arrTagInfo:4 )"),
    QStringLiteral("	IF nBuf >= 0"),
    QStringLiteral("		FONTSTYLE nBuf"),
    QStringLiteral("	ELSE"),
    QStringLiteral("		FONTSTYLE 0"),
    QStringLiteral("	ENDIF"),
    QStringLiteral("ENDIF"),
    QStringLiteral("IF STRLENS( arrTagInfo:3 ) > 0"),
    QStringLiteral("	VARSET LOCALS, \"\""),
    QStringLiteral("	SPLIT arrTagInfo:3, \",\", LOCALS"),
    QStringLiteral("	PRINT_IMG @\"%LOCALS:0%\""),
    QStringLiteral("ELSEIF STRLENS( arrTagInfo:1 ) > 0"),
    QStringLiteral("	IF nBtnIsNum == 1 && ISNUMERIC( arrTagInfo:1 )"),
    QStringLiteral("		PRINTBUTTON strText, TOINT( arrTagInfo:1 )"),
    QStringLiteral("	ELSE"),
    QStringLiteral("		PRINTBUTTON strText, arrTagInfo:1"),
    QStringLiteral("	ENDIF"),
    QStringLiteral("ELSE"),
    QStringLiteral("	PRINTPLAINFORM %strText%"),
    QStringLiteral("ENDIF"),
    QStringLiteral("RETURNF 0"),

    QStringLiteral("@FTAG_SETTAGINFO( strText, arrTagInfo )"),
    QStringLiteral("#FUNCTION"),
    QStringLiteral("#DIMS strText"),
    QStringLiteral("#DIMS REF arrTagInfo, 0"),
    QStringLiteral("#DIMS strTagType"),
    QStringLiteral("#DIMS strTagValue"),
    QStringLiteral("IF SUBSTRINGU( strText, 0, 1 ) == \"/\""),
    QStringLiteral("	strTagType  '= SUBSTRINGU( strText, 1, -1 )"),
    QStringLiteral("	strTagValue '= \"\""),
    QStringLiteral("ELSE"),
    QStringLiteral("	VARSET LOCALS, \"\""),
    QStringLiteral("	SPLIT strText, \":\", LOCALS"),
    QStringLiteral("	strTagType  '= LOCALS:0"),
    QStringLiteral("	strTagValue '= LOCALS:1"),
    QStringLiteral("	IF RESULT > 1"),
    QStringLiteral("		FOR LOCAL ,2 ,RESULT"),
    QStringLiteral("			strTagValue += \":\" + LOCALS:LOCAL"),
    QStringLiteral("		NEXT"),
    QStringLiteral("	ENDIF"),
    QStringLiteral("ENDIF"),
    QStringLiteral("SELECTCASE strTagType"),
    QStringLiteral("	CASE \"C\""),
    QStringLiteral("		arrTagInfo:0 '= strTagValue"),
    QStringLiteral("	CASE \"B\""),
    QStringLiteral("		arrTagInfo:1 '= strTagValue"),
    QStringLiteral("	CASE \"F\""),
    QStringLiteral("		arrTagInfo:2 '= strTagValue"),
    QStringLiteral("	CASE \"I\""),
    QStringLiteral("		arrTagInfo:3 '= strTagValue"),
    QStringLiteral("	CASE \"S\""),
    QStringLiteral("		arrTagInfo:4 '= strTagValue"),
    QStringLiteral("ENDSELECT"),
    QStringLiteral("RETURNF 0"),
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "TAG_PRINT -> PRINTBUTTON chain test";
    qDebug() << "=================================";

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
    QObject::connect(&runner, &ScriptRunner::errorOccurred, [](const QString& message) {
        qWarning().noquote() << "[engine]" << message;
    });

    // 记录 PRINTBUTTON 的产出（文本 + 值 + 是否字符串按钮）
    struct Button { QString text; qint64 intValue; QString strValue; bool isString; };
    QList<Button> buttons;
    QObject::connect(&engine, &ExecutionEngine::consolePrintButton,
                     [&buttons](const QString& text, qint64 intValue,
                                const QString& strValue, bool isString) {
        buttons.append({text, intValue, strValue, isString});
    });

    const bool okHeader = table.loadScript(QStringLiteral("tag.ERH"),
                                           buildLines(table, kHeader, QStringLiteral("tag.ERH")),
                                           /*isHeaderFile=*/true);
    check(okHeader, "loadScript(tag.ERH)");
    check(table.loadScript(QStringLiteral("tag.ERB"),
                           buildLines(table, kScript, QStringLiteral("tag.ERB"))),
          "loadScript(tag.ERB)");
    table.setEntryPoint(QStringLiteral("MAIN"));
    table.markLoadComplete();

    const ExecState st = runner.runToCompletion();
    check(st == ExecState::Halt, "run finished (Halt)");

    const auto gstr = [&storage](const QString& name, int idx) {
        return storage.getStringSlot(name, QList<int>{idx}, eravar::kGlobalScope).text;
    };

    qDebug() << "\n1) SUBSTRINGU（Function_TagSetText.ERB:402/404）";
    check(gstr(QStringLiteral("OBS_PRIM_B"), 0) == QLatin1String("B"),
          "SUBSTRINGU(\"B:999999999\", 0, 1) == \"B\"");
    check(gstr(QStringLiteral("OBS_PRIM_B"), 1) == QLatin1String(":999999999"),
          "SUBSTRINGU(\"B:999999999\", 1, -1) == \":999999999\"");
    check(gstr(QStringLiteral("OBS_PRIM_SLASH"), 0) == QLatin1String("/"),
          "SUBSTRINGU(\"/B\", 0, 1) == \"/\"（解除标签判定）");
    check(gstr(QStringLiteral("OBS_PRIM_SLASH"), 1) == QLatin1String("B"),
          "SUBSTRINGU(\"/B\", 1, -1) == \"B\"");

    qDebug() << "\n2) SPLIT 写入 LOCALS（:409-411）";
    check(gstr(QStringLiteral("OBS_SPLIT"), 0) == QLatin1String("B"),
          "LOCALS:0 == \"B\"（标签类型）");
    check(gstr(QStringLiteral("OBS_SPLIT"), 1) == QLatin1String("999999999"),
          "LOCALS:1 == \"999999999\"（按钮值）");
    check(gstr(QStringLiteral("OBS_SPLIT"), 2) == QLatin1String("2"),
          "RESULT == 2（元素个数）");

    qDebug() << "\n3) 字符串 SELECTCASE / CASE（:419-425）";
    check(gstr(QStringLiteral("OBS_SEL"), 0) == QLatin1String("hit"),
          "SELECTCASE \"B\" 命中 CASE \"B\"");

    qDebug() << "\n4) #DIMS REF 数组形参写回（arrTagInfo 传递）";
    check(gstr(QStringLiteral("O"), 0) == QLatin1String("written"),
          "#DIMS REF 数组的字符串写入对调用方可见");
    check(gstr(QStringLiteral("OBS_REF"), 2) == QLatin1String("written"),
          "被调函数内读回自己写入的值");
    check(gstr(QStringLiteral("OA"), 0) == QLatin1String("no-size"),
          "#DIMS REF（不写长度）同样写回调用方");
    check(storage.getGlobalInt1DInCurrentFrame(QStringLiteral("IV"), 0) == 77,
          "#DIM REF 标量写回调用方");
    check(storage.getGlobalInt1DInCurrentFrame(QStringLiteral("IA"), 0) == 55,
          "#DIM REF 整数数组元素写回调用方");
    check(gstr(QStringLiteral("OBS_REF_A"), 1) == QLatin1String("preset"),
          "REF 别名让被调函数读到实参的既有内容（读方向）");
    check(gstr(QStringLiteral("OBS_REF_B"), 0) == QLatin1String("plain"),
          "对照：非 REF 的私有数组照旧独立");

    qDebug() << "\n5) @\"…\" 書式付き文字列（{…} 在进 TAG_PRINT 前展开）";
    check(gstr(QStringLiteral("OBS_FORM"), 0) == QLatin1String("@B:3@[X]@/B@"),
          "@\"@B:{1+2}@[X]@/B@\" -> \"@B:3@[X]@/B@\"");
    check(gstr(QStringLiteral("OBS_FORM"), 1) == QLatin1String("@C:12@@/C@"),
          "@\"@C:{3*4}@@/C@\" -> \"@C:12@@/C@\"");

    qDebug() << "\n6) 整链：TAG_PRINT(@\"@B:999999999@[X]@/B@\", 5)";
    check(!buttons.isEmpty(), "至少产出一个按钮（不是只打出文字）");
    if (!buttons.isEmpty()) {
        const Button& b = buttons.last();
        check(b.text == QLatin1String("[X]"), "按钮文本 == \"[X]\"");
        check(!b.isString, "整数按钮（nExFlag & 0x04 + ISNUMERIC）");
        check(b.intValue == 999999999, "按钮值 == 999999999");
    }

    // =====================================================================
    // 7) 真实 FL3.8 源文件端到端（存在才跑；CI 里没有 FL3.8 就跳过）
    //    FL3.8/ERB/SYSTEM/LIBRARY/Function_TagSetText.ERB 是 TAG_PRINT 的实现，
    //    颜色屏（ポートレイト制御.ERB:329）就是用它生成出口按钮的。
    // =====================================================================
    qDebug() << "\n7) 真实 FL3.8 Function_TagSetText.ERB";
    QStringList candidates = {
        QStringLiteral("FL3.8/ERB/SYSTEM/LIBRARY/Function_TagSetText.ERB"),
        QStringLiteral("../FL3.8/ERB/SYSTEM/LIBRARY/Function_TagSetText.ERB"),
    };
    const QByteArray rootEnv = qgetenv("ERA_FL38_ROOT");   // 持有临时对象，别取 constData()
    if (!rootEnv.isEmpty())
        candidates.prepend(QString::fromLocal8Bit(rootEnv)
                           + QStringLiteral("/ERB/SYSTEM/LIBRARY/Function_TagSetText.ERB"));
    QString realPath;
    for (const QString& c : candidates)
        if (QFile::exists(c)) { realPath = c; break; }
    if (realPath.isEmpty()) {
        qDebug().noquote() << "  [skip] 未找到 FL3.8 源文件（设置 ERA_FL38_ROOT 可启用）";
    } else {
        QFile file(realPath);
        check(file.open(QIODevice::ReadOnly), "打开 " + realPath);
        const QString text = QString::fromUtf8(file.readAll());
        QStringList src = text.split(QLatin1Char('\n'));
        for (QString& line : src)
            if (line.endsWith(QLatin1Char('\r'))) line.chop(1);

        VariableStorage realStorage;
        ProcessState realState;
        EraParseTable realTable(&realState);
        ExecutionEngine realEngine(&realStorage, nullptr);
        realEngine.setParseTable(&realTable);
        ExpressionEvaluator realEvaluator;
        realEngine.setExpressionEvaluator(&realEvaluator);
        realTable.setExpressionEvaluator(&realEvaluator);
        realTable.setVariableStorage(&realStorage);
        ScriptRunner realRunner(&realTable, &realEngine, &realState, &realStorage);
        realRunner.setExpressionEvaluator(&realEvaluator);
        QObject::connect(&realState, &ProcessState::continueExecution,
                         &realRunner, &ScriptRunner::onContinueExecution);

        QList<Button> realButtons;
        QObject::connect(&realEngine, &ExecutionEngine::consolePrintButton,
                         [&realButtons](const QString& t, qint64 i, const QString& sv, bool isStr) {
            realButtons.append({t, i, sv, isStr});
        });

        check(realTable.loadScript(QStringLiteral("Function_TagSetText.ERB"),
                                   buildLines(realTable, src, realPath)),
              "loadScript(Function_TagSetText.ERB)");
        // 颜色屏那条调用（ポートレイト制御.ERB:329）原样搬过来
        const QStringList callSrc = {
            QStringLiteral("@REAL_MAIN"),
            QStringLiteral("CALLF TAG_PRINT(@\"@B:999999999@[X]@/B@\", 5)"),
            QStringLiteral("RETURN"),
        };
        check(realTable.loadScript(QStringLiteral("caller.ERB"),
                                   buildLines(realTable, callSrc, QStringLiteral("caller.ERB"))),
              "loadScript(caller.ERB)");
        realTable.setEntryPoint(QStringLiteral("REAL_MAIN"));
        realTable.markLoadComplete();
        const ExecState realState2 = realRunner.runToCompletion();
        check(realState2 == ExecState::Halt, "real run finished (Halt)");
        bool found = false;
        for (const Button& b : realButtons)
            if (b.text == QLatin1String("[X]") && !b.isString && b.intValue == 999999999) found = true;
        check(found, "真实 ERB：TAG_PRINT(@\"@B:999999999@[X]@/B@\", 5) 产出 999999999 按钮");
    }

    qDebug() << (g_failures == 0 ? "\nALL OK" : "\nFAILURES: " + QString::number(g_failures));
    return g_failures == 0 ? 0 : 1;
}
