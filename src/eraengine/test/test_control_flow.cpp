#include <QCoreApplication>
#include <algorithm>
#include <QDebug>
#include "ast/ast_builder.h"
#include "ast/expression_evaluator.h"
#include "process_state.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "script_runner.h"
#include "variable_storage.h"
#include "ast/print_template.h"

static int failures = 0;
static void run(const QString& name, const QStringList& source, qint64 expected, ExecState expectedState = ExecState::Halt, bool restart = false) {
    VariableStorage storage;
    ProcessState state;
    EraParseTable table(&state);
    ExecutionEngine engine(&storage, nullptr);
    ExpressionEvaluator evaluator;
    engine.setParseTable(&table);
    engine.setExpressionEvaluator(&evaluator);
    table.setExpressionEvaluator(&evaluator);
    table.setVariableStorage(&storage);
    ScriptRunner runner(&table, &engine, &state, &storage);
    runner.setExpressionEvaluator(&evaluator);
    runner.setStepLimit(1000);
    QList<LogicalLine> lines;
    QStringList script = {"@MAIN"};
    script.append(source);
    for (int i = 0; i < script.size(); ++i)
        lines.append(AstBuilder::build(script[i], ScriptPosition("flow.ERB", i, 0),
            [&table](const QString& text) { return table.expressionAst(text); }));
    table.loadScript("flow", lines);
    table.markLoadComplete();
    for (int i = 0; i < lines.size(); ++i) {
        const auto* loaded = table.lineAt("flow", i);
        if (!loaded || loaded->functionName != "FUNC") continue;
        bool compiled = !loaded->isError && loaded->argument.typeOk;
        for (const auto& operand : loaded->arguments) compiled &= bool(operand.ast);
        if (!compiled) {
            qCritical() << name << "FUNC not compiled at load time" << i << loaded->errMes;
            ++failures;
            return;
        }
    }
    table.setEntryPoint("MAIN");
    auto result = runner.runToCompletion();
    if (restart) {
        table.setEntryPoint("MAIN");
        result = runner.runToCompletion();
    }
    const qint64 actual = storage.getGlobalInt1D("CHECK", 0);
    if (result != expectedState || actual != expected) {
        qCritical() << name << "state" << int(result) << "actual" << actual << "expected" << expected;
        ++failures;
    } else qInfo() << "PASS" << name;
}
// Build with a global CONST already visible, then merge a private declaration.
// Finalization must restore the private variable in both FORM and actual args.
static void tryListLoadTime() {
    VariableStorage storage;
    ProcessState state;
    EraParseTable table(&state);
    ExpressionEvaluator evaluator;
    table.setExpressionEvaluator(&evaluator);
    table.setVariableStorage(&storage);
    table.loadScript("globals", {AstBuilder::build("#DIM CONST LIST_NAME = 99",
        ScriptPosition("globals.ERH", 0, 0),
        [&table](const QString& text) { return table.expressionAst(text); })});
    const QStringList source = {"@MAIN", "#DIMS LIST_NAME", "TRYCALLLIST",
        "FUNC %LIST_NAME%(LIST_NAME)", "ENDFUNC", "RETURN"};
    QList<LogicalLine> lines;
    for (int i = 0; i < source.size(); ++i)
        lines.append(AstBuilder::build(source[i], ScriptPosition("list.ERB", i, 0),
            [&table](const QString& text) { return table.expressionAst(text, true); }));
    table.loadScript("list", lines);
    table.markLoadComplete();
    storage.setGlobalStr1D("LIST_NAME", 0, "CHOSEN");
    const auto* entry = table.lineAt("list", 3);
    bool good = entry && !entry->isError && entry->argument.typeOk
        && entry->arguments.size() == 2 && entry->arguments[0].ast && entry->arguments[1].ast;
    if (good) {
        good = entry->arguments[0].ast->kind() == NodeKind::StrForm
            && entry->arguments[1].ast->kind() == NodeKind::Variable
            && entry->arguments[1].ast->valueType() == OperandType::Str
            && stringText(evaluator.evaluate(*entry->arguments[0].ast, &storage)) == "CHOSEN"
            && stringText(evaluator.evaluate(*entry->arguments[1].ast, &storage)) == "CHOSEN";
    }
    // A missing expression in an unreachable candidate must still fail loading.
    VariableStorage invalidStorage;
    ProcessState invalidState;
    EraParseTable invalid(&invalidState);
    invalid.setExpressionEvaluator(&evaluator);
    invalid.setVariableStorage(&invalidStorage);
    const QStringList badSource = {"@MAIN", "TRYCALLLIST", "FUNC NEVER_{1 +}",
        "FUNC NEVER(1 +)", "ENDFUNC", "RETURN"};
    QList<LogicalLine> badLines;
    for (int i = 0; i < badSource.size(); ++i)
        badLines.append(AstBuilder::build(badSource[i], ScriptPosition("bad.ERB", i, 0),
            [&invalid](const QString& text) { return invalid.expressionAst(text, true); }));
    invalid.loadScript("bad", badLines);
    invalid.markLoadComplete();
    good &= invalid.lineAt("bad", 2)->isError && invalid.lineAt("bad", 3)->isError;
    if (!good) { qCritical() << "TRY list load-time scope/invalid AST regression failed"; ++failures; }
    else qInfo() << "PASS TRY list load-time scope/invalid AST regression";
}
static void htmlFlow() {
    VariableStorage storage;
    ProcessState state;
    EraParseTable table(&state);
    ExecutionEngine engine(&storage, nullptr);
    ExpressionEvaluator evaluator;
    engine.setParseTable(&table); engine.setExpressionEvaluator(&evaluator);
    table.setExpressionEvaluator(&evaluator); table.setVariableStorage(&storage);
    ScriptRunner runner(&table, &engine, &state, &storage);
    runner.setExpressionEvaluator(&evaluator); runner.setStepLimit(1000);
    QList<PrintTemplate> printed;
    QObject::connect(&engine, &ExecutionEngine::consolePrintTemplate,
        [&](const PrintTemplate& output) { printed.append(output); });
    const QStringList script = {
        "@MAIN", "#DIMS html", "#DIMS copy", "html '= \"\"",
        "FOR LOCAL, 0, 2", "html += HTML_SPACE(2) + HTML_BUTTON(LOCALS, LOCAL)", "NEXT",
        "copy '= html", "HTML_PRINT copy", "HTML_PRINT html", "html '= LOCALS", "HTML_PRINT html",
        "RETURN", "@HTML_SPACE(ARG)", "#FUNCTIONS",
        "RETURNF @\"<shape type='space' param='{50*ARG}'>\"",
        "@HTML_BUTTON(ARGS, ARG)", "#FUNCTIONS",
        "RETURNF @\"<button value='{ARG}' title='tip{ARG}'>\" + @\"[{ARG}]\" + HTML_SPACE(1) + ARGS + \"</button>\""};
    QList<LogicalLine> lines;
    for (int i = 0; i < script.size(); ++i)
        lines.append(AstBuilder::build(script[i], ScriptPosition("html.ERB", i, 0),
            [&table](const QString& text) { return table.expressionAst(text); }));
    const auto ast = table.expressionAst("\"<button value='7'>same</button>\"");
    table.loadScript("html", lines); table.markLoadComplete(); table.setEntryPoint("MAIN");
    // Identical markup obtained through an ordinary runtime write stays opaque.
    storage.setLocalStr(0, "<button value='99'>opaque</button>");
    const auto result = runner.runToCompletion();
    bool good = result == ExecState::Halt && printed.size() == 3;
    if (good) {
        for (int output = 0; output < 2; ++output) {
            QStringList values, tips; int spaces = 0; QString text;
            for (const auto& part : printed[output].parts) {
                if (part.kind == PrintTemplatePart::Kind::Button) { values.append(part.buttonValue); tips.append(part.tooltip); }
                if (part.kind == PrintTemplatePart::Kind::Shape) ++spaces;
                if (part.kind == PrintTemplatePart::Kind::Text) text += part.text;
            }
            good &= values == QStringList({"0", "1"}) && tips == QStringList({"tip0", "tip1"})
                && spaces == 4 && text.contains("<button value='99'>opaque</button>");
        }
        good &= printed[2].parts.size() == 1 && printed[2].parts[0].kind == PrintTemplatePart::Kind::Text;
    }
    if (!good) { qCritical() << "HTML function/accumulation provenance failed" << int(result) << printed.size(); ++failures; }
    else qInfo() << "PASS HTML function/accumulation provenance";
    const auto tracked = evaluator.evaluate(*ast, &storage);
    const auto hasButton = [](const FlatStringValue& value) {
        return std::any_of(value.markup.parts.cbegin(), value.markup.parts.cend(),
            [](const PrintTemplatePart& part) { return part.kind == PrintTemplatePart::Kind::Button; });
    };
    storage.setStringSlot("RESULTS", {0}, flatString(tracked));
    good = hasButton(storage.getStringSlot("RESULTS", {0}));
    storage.setGlobalStr1D("RESULTS", 0, stringText(tracked));
    good &= !hasButton(storage.getStringSlot("RESULTS", {0}))
        && storage.getStringSlot("RESULTS", {0}).text == stringText(tracked);
    // RESETGLOBAL clears user globals; built-in RESULTS is preserved.
    storage.setStringSlot("HTML_GLOBAL", {0}, flatString(tracked));
    storage.resetGlobals();
    good &= storage.getStringSlot("HTML_GLOBAL", {0}).text.isEmpty()
        && !hasButton(storage.getStringSlot("HTML_GLOBAL", {0}));
    if (!good) { qCritical() << "HTML provenance invalidation failed"; ++failures; }
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    htmlFlow();
    tryListLoadTime();
    run("TRYCALLLIST first match and return after ENDFUNC", {
        "TRYCALLLIST", "FUNC MISSING(++CHECK:1)", "FUNC SUB(2, 3)", "FUNC OTHER",
        "ENDFUNC", "CHECK += 1 + CHECK:1 * 100", "RETURN",
        "@SUB(A, B)", "CHECK = A * 10 + B", "RETURN",
        "@OTHER", "CHECK = 999", "RETURN"}, 24);
    run("TRYCALLLIST scoped FORM target and typed arguments", {
        "#DIMS DEST", "#DIMS TEXT", "#DIM VALUE", "DEST '= \"SUB\"", "TEXT '= \"a,b\"", "VALUE = 7",
        "TRYCALLLIST", "FUNC NOPE_{VALUE}(++CHECK:1)", "FUNC %DEST%_{VALUE}(TEXT, VALUE + 2)",
        "ENDFUNC", "CHECK += 1 + CHECK:1 * 100", "RETURN",
        "@SUB_7(ARGS, ARG)", "CHECK = STRLENS(ARGS) * 10 + ARG", "RETURN"}, 40);
    run("TRYCALLLIST FORM and literal arguments", {
        "#DIM VALUE", "VALUE = 4", "TRYCALLLIST", "FUNC SUB(@\"value={VALUE}\", \"x,y\")",
        "ENDFUNC", "RETURN", "@SUB(TEXT, SUFFIX)", "#DIMS TEXT", "#DIMS SUFFIX",
        "CHECK = STRLENS(TEXT) * 10 + STRLENS(SUFFIX)", "RETURN"}, 73);
    run("TRYCALLLIST target side effects evaluated once", {
        "TRYCALLLIST", "FUNC %LIST_TARGET()%(++CHECK:2)", "ENDFUNC",
        "CHECK += CHECK:1 * 10", "RETURN", "@SUB(ARG)", "CHECK = ARG", "RETURN",
        "@LIST_TARGET", "#FUNCTIONS", "CHECK:1 += 1", "RETURNF \"SUB\""}, 11);
    run("TRYJUMPLIST dynamic target inherits return address", {
        "CALL DISPATCH", "CHECK += 1", "RETURN", "@DISPATCH", "#DIMS DEST", "DEST '= \"SUB\"",
        "TRYJUMPLIST", "FUNC MISSING", "FUNC %DEST%(6)", "ENDFUNC", "CHECK = 999", "RETURN",
        "@SUB(ARG)", "CHECK = ARG * 10", "RETURN"}, 61);
    run("TRYGOTOLIST scoped dynamic label first match", {
        "#DIMS DEST", "DEST '= \"FOUND\"", "TRYGOTOLIST", "FUNC $MISSING", "FUNC $%DEST%",
        "FUNC $LATER", "ENDFUNC", "CHECK = 999", "RETURN", "$FOUND", "CHECK = 7", "RETURN",
        "$LATER", "CHECK = 888", "RETURN"}, 7);
    run("TRYGOTOLIST excludes labels in another function", {
        "TRYGOTOLIST", "FUNC FOREIGN", "ENDFUNC", "CHECK = 5", "RETURN",
        "@OTHER", "$FOREIGN", "CHECK = 999", "RETURN"}, 5);
    for (const QString& instruction : {QString("TRYCALLLIST"), QString("TRYJUMPLIST"), QString("TRYGOTOLIST")}) {
        run(instruction + " exhausted skips ENDFUNC", {
            instruction, "FUNC MISSING(++CHECK:1)", "ENDFUNC", "CHECK = 2 + CHECK:1 * 100", "RETURN"}, 2);
        run(instruction + " empty list", {instruction, "ENDFUNC", "CHECK = 3", "RETURN"}, 3);
    }
    run("CALL spaced parentheses", {"CALL SUB (2,3)", "RETURN", "@SUB(A,B)", "CHECK = A * 10 + B", "RETURN"}, 23);
    run("ENCODETOUNI expands FORM into RESULT array", {"RESULTS '= \"あA\"", "ENCODETOUNI %RESULTS%",
        "CHECK = (RESULT:0 == 2) && (RESULT:1 == 12354) && (RESULT:2 == 65)"}, 1);
    run("ENCODETOUNI literal punctuation", {"ENCODETOUNI a,b,", "CHECK = RESULT:0 * 1000 + RESULT:4"}, 4044);
    run("ENCODETOUNI literal quotes", {"ENCODETOUNI \"A\"", "CHECK = RESULT:0 * 100 + RESULT:1"}, 334);
    run("ENCODETOUNI empty FORM", {"RESULT = 9", "ENCODETOUNI", "CHECK = RESULT"}, 0);
    run("ENCODETOUNI expression scalar", {"CHECK = ENCODETOUNI(\"A\")"}, 65);
    for (int value : {1100, 1101, 1106, 1107})
        run(QString("CASE inclusive boundary %1").arg(value),
            {QString("SELECTCASE %1").arg(value), "CASE 1101 TO 1106", "CHECK = 1",
             "CASEELSE", "CHECK = 2", "ENDSELECT"}, value >= 1101 && value <= 1106 ? 1 : 2);
    run("CASE mixed IS/range and comment", {"SELECTCASE 4512", "CASE IS < 0, 4510 TO 4512 ; comment",
        "CHECK = 7", "CASEELSE", "CHECK = 99", "ENDSELECT"}, 7);
    run("CASE strings with TO text", {"SELECTCASE \"A TO B\"", "CASE \"A TO B\"", "CHECK = 3", "ENDSELECT"}, 3);
    run("64 bit IF/SIF/ELSEIF", {"IF 4294967296", "CHECK += 1", "ENDIF",
        "SIF -4294967296", "CHECK += 2", "IF 0", "CHECK = 999", "ELSEIF 4294967296", "CHECK += 4", "ENDIF"}, 7);
    run("nested branches and loops", {"FOR A, 0, 3", "IF A == 1", "CONTINUE", "ELSE", "WHILE 1",
        "SELECTCASE A", "CASE 0", "CHECK += 2", "CASEELSE", "CHECK += 3", "ENDSELECT", "BREAK", "WEND", "ENDIF", "NEXT"}, 5);
    run("DO LOOP and CONTINUE", {"DO", "A += 1", "SIF A == 2", "CONTINUE", "CHECK += A", "LOOP A < 3"}, 4);
    run("DO BREAK", {"DO", "CHECK += 1", "BREAK", "LOOP 1"}, 1);
    run("REPEAT COUNT REND", {"REPEAT 3", "CHECK += COUNT + 1", "REND"}, 6);
    run("REPEAT counter mutation", {"REPEAT 5", "CHECK += 1", "COUNT += 1", "REND"}, 3);
    run("REPEAT zero resets COUNT", {"COUNT = 9", "REPEAT 0", "CHECK = 999", "REND", "CHECK = COUNT"}, 0);
    run("BREAK increments FOR counter", {"FOR A, 2, 10, 3", "BREAK", "NEXT", "CHECK = A"}, 5);
    run("FOR initialization before end", {"A = 99", "FOR A, 0, A + 3", "CHECK += 1", "NEXT"}, 3);
    run("FOR expression index", {"B = 2", "FOR A:B, 0, 3", "CHECK += A:2 + 1", "NEXT"}, 6);
    run("FOR descending/empty/zero step", {"FOR A, 3, 0, -1", "CHECK += A", "NEXT", "FOR A, 1, 1", "CHECK = 999", "NEXT",
        "FOR A, 0, 9, 0", "CHECK = 999", "NEXT", "WHILE 0", "CHECK = 999", "WEND"}, 6);
    // 调用族：以前只实现了 CALL，CALLFORM/TRYCALL/TRYCALLFORM 全被静默跳过
    // （eraTW `CALLFORM CUSTOM_%ARGS%_MENU(ARG)` 不执行 -> 菜单空白）。
    run("CALLFORM 格式化标签", {
        "A = 7",
        "CALLFORM SUB_{A}(A)",
        "RETURN",
        "@SUB_7(NUM)",
        "CHECK = NUM * 10",
        "RETURN"}, 70);
    run("TRYCALL 存在则调用", {
        "TRYCALL SUB_9",
        "CHECK += 1",
        "RETURN",
        "@SUB_9",
        "CHECK = 5",
        "RETURN"}, 6);
    run("TRYCALLFORM 不存在则静默跳过", {
        "A = 3",
        "CHECK = 1",
        "TRYCALLFORM NOPE_{A}",
        "CHECK += 1"}, 2);
    // TRYC 系 + CATCH + THROW（eraTW 的 EXISTOBJ：TRYCCALLFORM EXIST_xxx + CALLF SET_EXIST）
    //   * 目标存在   -> 执行 TRY 体，落到 CATCH 时跳到 ENDCATCH 之后
    //   * 目标不存在 -> 从 CATCH 的**下一行**开始执行异常体
    run("TRYCCALLFORM 命中 = 正常调用", {
        "A = 1",
        "TRYCCALLFORM SUB_{A}",
        "CHECK = 10",
        "CATCH",
        "CHECK = 999",
        "ENDCATCH",
        "RETURN",
        "@SUB_1",
        "CHECK += 1",
        "RETURN"}, 10);
    run("TRYCCALLFORM 未命中 = 进入 CATCH", {
        "A = 2",
        "TRYCCALLFORM SUB_{A}",
        "CHECK = 10",
        "CATCH",
        "CHECK = 7",
        "ENDCATCH",
        "RETURN",
        "@SUB_1",
        "CHECK = 999",
        "RETURN"}, 7);
    run("TRY 无 CATCH 未命中 = 静默跳过", {
        "A = 3",
        "TRYCCALLFORM SUB_{A}",
        "CHECK = 1",
        "RETURN",
        "@SUB_1",
        "CHECK = 999",
        "RETURN"}, 1);
    // THROW **不会**被 CATCH 接住：Emuera 文档《异常分支：TRYC / CATCH / ENDCATCH》
    // THROW 中断执行（C# THROW_Instruction -> throw new CodeEE）：
    // TRYC 系的 CATCH 只捕获「函数不存在」，不捕获被调函数里抛出的 THROW。
    // 曾有一段时期这里「只告警不中断」以便观察上游求值缺陷；上游缺陷修完
    // 后已恢复 C# 语义 —— THROW 所在脚本以 Error 状态终止（CHECK = 999）。
    run("THROW 中断执行（CATCH 不捕获，异常体不执行）", {
        "TRYCCALLFORM THROWER",
        "CHECK = 10",
        "CATCH",
        "CHECK = 3",
        "ENDCATCH",
        "RETURN",
        "@THROWER",
        "CHECK = 999",
        "THROW \"boom\"",
        "RETURN"}, 999, ExecState::Error);
    // CALLF：调用式中関数；对齐 C# CALLF_Instruction（mToken.GetValue(exm)），
    // **返回值被丢弃**，不写 RESULT —— 此前把返回值写进 RESULT / LOCALS:0，
    // LOCALS:0 会覆盖调用者的局部槽（eraTW 的 TEMP_RE_STR 因此恒返回空串）。
    // 这里用副作用验证函数确实被调用。
    run("CALLF 调用式中関数（返回值丢弃，副作用生效）", {
        "CALLF DOUBLE(21)",
        "CHECK = CHECK:1",
        "RETURN",
        "@DOUBLE(N)",
        "#FUNCTION",
        "CHECK:1 = N * 2",
        "RETURNF N * 2"}, 42);
    run("CALLFORMF 格式化函数名（返回值丢弃，副作用生效）", {
        "A = 7",
        "CALLFORMF TRIPLE_{A}(A)",
        "CHECK = CHECK:1",
        "RETURN",
        "@TRIPLE_7(N)",
        "#FUNCTION",
        "CHECK:1 = N * 3",
        "RETURNF N * 3"}, 21);
    run("CALLF quoted and nested comma arguments", {
        "CALLF TAKE_TEXT, \"a,b\"",
        "CALLF TAKE_PAIR(\"x,y\", 7)",
        "RETURN",
        "@TAKE_TEXT(ARGS)",
        "#FUNCTIONS",
        "CHECK = STRLENS(ARGS)",
        "RETURNF ARGS",
        "@TAKE_PAIR(ARGS, ARG)",
        "#FUNCTIONS",
        "CHECK += STRLENS(ARGS) * 10 + ARG",
        "RETURNF TOSTR(ARG)"}, 40);
    run("CALLF dynamic variable argument", {
        "LOCALS '= \"dynamic\"",
        "CALLF TAKE_TEXT, LOCALS",
        "RETURN",
        "@TAKE_TEXT(ARGS)",
        "#FUNCTIONS",
        "CHECK = STRLENS(ARGS)",
        "RETURNF ARGS"}, 7);
    run("CASE mixed forms", {"SELECTCASE 5", "CASE 1, 4 TO 6, IS > 10", "CHECK = 1", "CASEELSE", "CHECK = 999", "ENDSELECT"}, 1);
    run("CASE reversed range", {"SELECTCASE 5", "CASE 6 TO 4", "CHECK = 999", "CASEELSE", "CHECK = 1", "ENDSELECT"}, 1);
    run("CASE string relation", {"SELECTCASE \"abc\"", "CASE IS > \"zzz\"", "CHECK = 999", "CASE IS < \"def\"", "CHECK = 1", "ENDSELECT"}, 1);
    run("CASE quoted TO", {"SELECTCASE \"A TO B\"", "CASE \"X TO Y\"", "CHECK = 999", "CASE \"A TO B\"", "CHECK = 1", "ENDSELECT"}, 1);
    run("CASE string range", {"SELECTCASE \"m\"", "CASE \"a\" TO \"c\"", "CHECK = 999", "CASE \"j\" TO \"z\"", "CHECK = 1", "ENDSELECT"}, 1);
    run("call restores loops and locals", {"FOR LOCAL:0, 0, 3", "CALL SUB", "CHECK += LOCAL:0 + 1", "NEXT", "RETURN", "@SUB",
        "FOR LOCAL:0, 0, 10", "RETURN", "NEXT"}, 6);
    run("bare RETURN clears RESULT", {"CALL FIRST", "CALL SECOND", "CHECK = RESULT", "RETURN", "@FIRST", "RETURN 9", "@SECOND", "RETURN"}, 0);
    // 对齐 C#：自然结束走 state.Return(0)，Return/ReturnF 不写 RESULT ——
    // 只有显式 RETURN 语句写。eraTW 选地主的 `FLAG:地主 = RESULT` 靠此语义。
    run("implicit RETURN preserves RESULT", {"CALL FIRST", "CALL SECOND", "CHECK = RESULT", "RETURN", "@FIRST", "RETURN 9", "@SECOND", "A = 1", "@THIRD", "RETURN 99"}, 9);
    run("SIF skips next logical instruction", {"SIF 0", "", "; comment", "CHECK = 999", "CHECK += 1"}, 1);
    run("branch ending at function boundary", {"IF 0", "CHECK = 999", "ENDIF", "@SECOND", "CHECK = 777"}, 0);
    run("short circuit side effects", {"IF 0 && ++A", "CHECK = 999", "ENDIF", "IF 1 || ++A", "CHECK = A + 1", "ENDIF"}, 1);
    run("CALL return at function boundary", {"CALL SUB", "@OTHER", "CHECK = 999", "RETURN", "@SUB", "CHECK = 1", "RETURN"}, 1);
    run("nested SELECT", {"SELECTCASE 1", "CASE 1", "SELECTCASE 2", "CASE 2", "CHECK = 1", "CASEELSE", "CHECK = 999", "ENDSELECT", "CASEELSE", "CHECK = 999", "ENDSELECT"}, 1);
    run("REPEAT continue and BREAK counter", {"REPEAT 4", "SIF COUNT < 2", "CONTINUE", "CHECK += COUNT", "BREAK", "REND", "CHECK += COUNT"}, 5);
    run("FOR body changes counter", {"FOR A, 0, 6", "CHECK += 1", "A += 1", "NEXT"}, 3);
    run("callee cannot break caller loop", {"FOR A, 0, 3", "CALL SUB", "CHECK += 1", "NEXT", "RETURN", "@SUB", "BREAK", "RETURN"}, 3);
    run("method loop shares step budget", {"CHECK = SPIN()", "RETURN", "@SPIN", "#FUNCTION", "WHILE 1", "WEND", "RETURNF 0"}, 0, ExecState::Error);
    run("RETURN list", {"CALL SUB", "CHECK = RESULT:0 + RESULT:1 + RESULT:2", "RETURN", "@SUB", "RETURN 1, 2, 4"}, 7);
    run("method RETURNF preserves RESULT", {"RESULT = 9", "CHECK = VALUE()", "CHECK += RESULT", "RETURN", "@VALUE", "#FUNCTION", "RETURNF 2"}, 11);
    run("restart clears unfinished loops", {"CHECK = 0", "WHILE 1", "CHECK += 1", "RETURN", "WEND"}, 1, ExecState::Halt, true);
    run("64 bit WHILE and DO condition", {"A = 4294967296", "WHILE A", "CHECK += 1", "A = 0", "WEND", "DO", "CHECK += 1", "A += 1", "LOOP (A < 2) * 4294967296"}, 3);
    return failures ? 1 : 0;
}
