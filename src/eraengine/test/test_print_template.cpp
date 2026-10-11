#include <QCoreApplication>
#include <QDebug>
#include "print_template.h"
#include "ast_builder.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "expression_evaluator.h"
#include "console_backend.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char* message) { if (!ok) { qCritical() << message; ++failures; } };
    ProcessState state;
    EraParseTable table(&state);
    VariableStorage storage;
    ExpressionEvaluator evaluator;
    ExecutionEngine engine(&storage);
    engine.setParseTable(&table); engine.setExpressionEvaluator(&evaluator);
    const auto resolve = [&](const QString& e) { return table.expressionAst(e); };
    for (const QString& source : QStringList{
        QString::fromUtf8(R"ERB(CALL F, @"前缀/\@1 ? 哥哥 # 姐姐\@后缀")ERB"),
        QString::fromUtf8(R"ERB(SPLIT @"前缀/\@1 ? 哥哥 # 姐姐\@后缀", ",", LOCALS)ERB")}) {
        const auto line = AstBuilder::build(source, {}, resolve);
        const auto& operand = line.arguments.at(source.startsWith("CALL") ? 1 : 0);
        check(operand.ast && evaluator.evaluate(*operand.ast, &storage).toString()
              == QString::fromUtf8("前缀/哥哥后缀"), "load-time FORM AST removes delimiters and evaluates parts");
    }
    for (const QString& command : QStringList{"REUSELASTLINE", "DATAFORM", "THROW"}) {
        const QString source = command + QString::fromUtf8(R"ERB( \@ LOCAL ? yes # no \@tail.%"." * LOCAL%)ERB");
        int wholeSourceResolutions = 0;
        const auto formResolve = [&](const QString& text) {
            if (text.startsWith("\\@")) ++wholeSourceResolutions;
            return resolve(text);
        };
        auto formLine = AstBuilder::build(source, {}, formResolve);
        storage.setLocalInt(0, 2);
        check(formLine.arguments.first().ast
              && evaluator.evaluate(*formLine.arguments.first().ast, &storage).toString() == "yestail..."
              && wholeSourceResolutions == 0, "FORM command parses conditional plus trailing text at load time");
        storage.setLocalInt(0, 0);
        check(formLine.arguments.first().ast
              && evaluator.evaluate(*formLine.arguments.first().ast, &storage).toString() == "notail.",
              "FORM command evaluates false branch and trailing interpolation");
    }
    const auto quoted = AstBuilder::build(QStringLiteral("CALL F, \"literal {1} % text\""), {}, resolve);
    check(quoted.arguments.at(1).ast && quoted.arguments.at(1).ast->kind() == NodeKind::Literal,
          "plain quoted string has Literal AST and no FORM interpolation");
    const auto modulo = AstBuilder::build("CALL F, 10 % 3 % 2", {}, resolve);
    check(modulo.arguments.at(1).ast && modulo.arguments.at(1).ast->valueType() == OperandType::Int,
          "percent operators remain integer expressions");
    // Quoted strings in a yen-at condition do not end the outer @"...".
    for (int truth : {0, 1}) {
        storage.setLocalStr(0, truth ? "变身形態" : "通常");
        auto ast = table.expressionAst(QString::fromUtf8(R"ERB(@"\@LOCALS=="变身形態" ? 惡魔變身 # \@技能ロック設定")ERB"));
        check(bool(ast), "quoted conditional form parses");
        if (ast) check(evaluator.evaluate(*ast, &storage).toString() ==
            (truth ? QString::fromUtf8("惡魔變身技能ロック設定") : QString::fromUtf8("技能ロック設定")),
            "conditional prefix selects correct branch, including empty false branch");
    }
    auto indexed = table.expressionAst(QString::fromUtf8(R"ERB(CFLAG:ARG:@"\@形態=="变身形態" ? 惡魔變身 # \@技能ロック設定")ERB"));
    check(indexed && indexed->kind() == NodeKind::Variable &&
          static_cast<const VariableNode&>(*indexed).indices().size() == 2,
          "Megaten formatted CFLAG index retains both indices");
    auto spaces = table.expressionAst(QString::fromUtf8(R"ERB(@"\@1 ? 　x　 # y\@")ERB"));
    check(spaces && evaluator.evaluate(*spaces, &storage).toString() == QString::fromUtf8("　x　"),
          "conditional branch preserves full-width spaces");
    for (const auto& sample : QList<QPair<QString, QString>>{
        {QString::fromUtf8(R"ERB(@"\@1 ? " # 否\@")ERB"), "\""},
        {QString::fromUtf8(R"ERB(@"\@0 ? " # 否\@")ERB"), QString::fromUtf8("否")},
        {QString::fromUtf8(R"ERB(@"%"a%b",6,LEFT%")ERB"), "a%b   "},
        {QString::fromUtf8(R"ERB(@"{STRLEN("}"),3}")ERB"), "  1"},
        {QString::fromUtf8(R"ERB(@"\@1 ? {1 ? 2 # 3} # no\@")ERB"), "2"}}) {
        auto ast = table.expressionAst(sample.first);
        check(ast && evaluator.evaluate(*ast, &storage).toString() == sample.second,
              "FORM separators respect expression spans and literal branch quotes");
    }
    storage.setLocalInt(0, 7);
    engine.executeInstruction(AstBuilder::build("TIMES LOCAL, -1.5", {}, resolve));
    check(storage.getLocalInt(0) == -11, "TIMES defaults to round half away from zero");
    storage.setLocalInt(0, 1000);
    engine.executeInstruction(AstBuilder::build("TIMES LOCAL, 1e-2", {}, resolve));
    check(storage.getLocalInt(0) == 10, "TIMES accepts scientific notation");
    auto rounding = ExecutionEngine::RealRounding::Round;
    engine.setRealRoundingProvider([&rounding] { return rounding; });
    auto times = AstBuilder::build("TIMES LOCAL, 0.75", {}, resolve);
    // 原始文本变化不应改变装载期保存的 double 倍率。
    times.arguments.last().raw = "invalid";
    times.argument.params.last().raw = "invalid";
    for (const auto mode : {ExecutionEngine::RealRounding::Round,
                           ExecutionEngine::RealRounding::Floor,
                           ExecutionEngine::RealRounding::Ceil}) {
        rounding = mode;
        storage.setLocalInt(0, 10);
        engine.executeInstruction(times);
        check(storage.getLocalInt(0) == (mode == ExecutionEngine::RealRounding::Floor ? 7 : 8),
              "positive product uses selected rounding and cached double");
        storage.setLocalInt(0, -10);
        engine.executeInstruction(times);
        check(storage.getLocalInt(0) == (mode == ExecutionEngine::RealRounding::Ceil ? -7 : -8),
              "negative product uses selected rounding");
        storage.setLocalInt(0, 4294967296LL);
        engine.executeInstruction(AstBuilder::build("TIMES LOCAL, 1.25", {}, resolve));
        check(storage.getLocalInt(0) == 5368709120LL, "rounding keeps values beyond 32-bit range");
    }
    QString timesError;
    const auto timesErrorConnection = QObject::connect(&engine, &ExecutionEngine::errorOccurred,
        [&timesError](const QString& text) { timesError = text; });
    storage.setLocalInt(0, 10);
    engine.executeInstruction(AstBuilder::build("TIMES LOCAL, 1e308", {}, resolve));
    check(!timesError.isEmpty() && storage.getLocalInt(0) == 10,
          "overflow reports error and keeps target unchanged");
    QObject::disconnect(timesErrorConnection);
    ConsoleBackend console;
    QObject::connect(&engine, &ExecutionEngine::consolePrintTemplate, &console, &ConsoleBackend::printTemplate);
    QString drawn;
    const auto drawConnection = QObject::connect(&engine, &ExecutionEngine::consolePrint,
        [&drawn](const QString& text, bool) { drawn = text; });
    engine.executeInstruction(AstBuilder::build("CUSTOMDRAWLINE =", {}, resolve));
    check(!drawn.isEmpty() && drawn == QString(drawn.size(), '='), "literal equals draws separator");
    engine.executeInstruction(AstBuilder::build("CUSTOMDRAWLINE a,b", {}, resolve));
    check(drawn.startsWith("a,ba,b"), "CUSTOMDRAWLINE repeats entire comma-containing pattern");
    QObject::disconnect(drawConnection);
    auto line = AstBuilder::build(QStringLiteral("HTML_PRINT @\"<button value='{LOCAL++}' title='a > b'><b>{LOCAL++}</b><i>%LOCALS%</i></button><br>\""), {}, resolve);
    check(bool(line.printTemplate), "formatted literal compiles at load time");
    storage.setLocalInt(0, 7); storage.setLocalStr(0, "<b>&amp;</b>");
    engine.executeInstruction(line);
    check(storage.getLocalInt(0) == 9, "each interpolation evaluated once");
    check(console.buffer().count() == 1, "br without attributes recognized");
    if (console.buffer().count()) {
        const auto& row = console.buffer().at(0);
        check(row.plainText() == "8<b>&amp;</b>", "expression result stays text, not markup or entities");
        check(row.segments.size() == 1 && row.segments.first().isButton && row.segments.first().intValue == 7, "button expression value retained");
        const auto spans = row.flatSpans();
        check(spans.size() == 2 && spans[0].style.bold && !spans[0].style.italic && spans[1].style.italic && !spans[1].style.bold, "nested button styles restored");
        check(row.segments.first().tooltip == "a > b", "quoted greater-than scanner");
    }
    const auto printHtmlFixture = [&](const QString& text) {
        const auto compiled = PrintTemplateCompiler::compile(QSharedPointer<LiteralNode>::create(text));
        console.printTemplate(PrintTemplateCompiler::evaluate(*compiled, [](const ExpressionNode&) { return QString(); }));
    };
    console.clearAll();
    printHtmlFixture("<shape type = 'space' param = '100'><button value = '0' title = 'start'>START</button><br>");
    check(console.buffer().count() == 1
          && console.buffer().at(0).segments.size() == 2
          && !console.buffer().at(0).segments.first().isButton
          && console.buffer().at(0).segments.last().isButton
          && console.buffer().at(0).segments.last().intValue == 0
          && console.buffer().at(0).segments.last().tooltip == "start",
          "HTML button retains value and tooltip after a nonbutton spacer");
    console.notifyInputRequested("INPUT");
    qint64 submittedButton = -1;
    const auto inputConnection = QObject::connect(&console, &ConsoleBackend::inputSubmitted,
        [&](qint64 value) { submittedButton = value; });
    const auto blocks = console.lineBlocks(0);
    bool clickableStart = false;
    for (const auto& block : blocks) {
        const auto data = block.toMap();
        if (data.value("text").toString() != "START") continue;
        clickableStart = data.value("clickable").toBool();
        console.clickAt(data.value("lineIndex").toInt(), data.value("segmentIndex").toInt());
    }
    check(clickableStart && submittedButton == 0, "HTML button zero submits through flattened block coordinates");
    QObject::disconnect(inputConnection);
    console.notifyInputDone();
    console.clearAll();
    printHtmlFixture("<font color='red'><img src='a&amp;b' width='200'><shape type='rect' param='100,50'></font><b>x</b>y &amp;lt; &#65; &#x1F600;<br/>");
    check(console.buffer().count() == 1, "self closing break");
    if (console.buffer().count()) {
        const auto& row = console.buffer().at(0); const auto spans = row.flatSpans();
        check(row.plainText() == QString::fromUtf8("xy &lt; A 😀"), "entities decoded once including numeric Unicode");
        check(spans.size() >= 4 && spans[0].kind == ConsoleSpanKind::Image && spans[0].text == "a&b" && spans[0].imageSize.width() == 200 && spans[0].style.color == QColor("red") && spans[1].kind == ConsoleSpanKind::Shape && spans[1].style.color == QColor("red"), "image and shape inherit style and attributes");
        check(spans.size() >= 4 && !spans.last().style.bold && !spans.last().style.color.isValid(), "closing styles restored");
    }
    console.clearAll();
    storage.setLocalStr(0, "<u>runtime</u><br>");
    line = AstBuilder::build("HTML_PRINT LOCALS", {}, resolve);
    check(line.arguments.first().ast && line.printTemplate, "HTML string is compiled during load");
    engine.executeInstruction(line);
    check(console.buffer().count() == 1 && console.buffer().at(0).plainText() == "<u>runtime</u><br>" && !console.buffer().at(0).flatSpans().first().style.underline, "variable HTML value remains raw text");
    console.clearAll();
    storage.setLocalStr(0, "blue");
    line = AstBuilder::build("HTML_PRINT @\"<font color='%LOCALS%'><b>blue</b></font><br>\"", {}, resolve);
    engine.executeInstruction(line);
    check(console.buffer().count() == 1 && console.buffer().at(0).flatSpans().first().style.color == QColor("blue"), "dynamic font attribute");

    console.clearAll(); storage.setLocalInt(0, 1);
    line = AstBuilder::build("HTML_PRINT @\"[{LOCAL, 3}] [{LOCAL, 3, LEFT}]<br>\"", {}, resolve);
    engine.executeInstruction(line);
    check(console.buffer().count() == 1 && console.buffer().at(0).plainText() == "[  1] [1  ]", "format width and alignment preserve actual value");
    console.clearAll();
    console.setGridColumns(4);          // 36px ÷ 9px = 4 列
    console.setWrappingEnabled(true);
    printHtmlFixture("<p align='right'><nobr>abcdefghijk</nobr></p>");
    check(console.buffer().count() == 1 && console.buffer().at(0).align == ConsoleAlign::Right,
          "nobr keeps long paragraph together with requested alignment");
    check(console.wrappingEnabled(), "wrapping restored after template");
    console.print("x"); console.newline();
    check(console.buffer().at(console.buffer().count()-1).align == ConsoleAlign::Left,
          "paragraph alignment does not leak into ordinary print");
    console.setWrappingEnabled(false);

    console.clearAll();
    storage.setLocalInt(0, 4);
    auto titleImage = AstBuilder::build(R"ERB(HTML_PRINT "<p align='center'><img src='TW_title" + TOSTR(LOCAL, "000") + "'></p>")ERB", {}, resolve);
    engine.executeInstruction(titleImage);
    check(console.buffer().count() > 0
          && console.buffer().at(0).flatSpans().first().kind == ConsoleSpanKind::Image
          && console.buffer().at(0).flatSpans().first().text == "TW_title004",
          "concatenated HTML compiles image structure at load time with dynamic src");
    // Truncated markup must preserve its text and the next complete tag.
    for (const QString& source : QStringList{
        "prefix<font color='red'", "prefix<font title='a > b", "prefix<"}) {
        console.clearAll();
        printHtmlFixture(source);
        console.flush();
        check(console.buffer().count() == 1 && console.buffer().at(0).plainText() == source,
              "unfinished tag and quoted attribute preserve the complete tail");
    }
    console.clearAll();
    printHtmlFixture("prefix<font color=red<b>bold</b>tail<br>");
    check(console.buffer().count() == 1
          && console.buffer().at(0).plainText() == "prefix<font color=redboldtail"
          && console.buffer().at(0).flatSpans().at(2).style.bold,
          "unfinished tag does not swallow the next complete tag");
    console.clearAll();
    storage.setLocalStr(0, "suffix");
    line = AstBuilder::build(R"ERB(HTML_PRINT "<font title='a > " + @"b' color='red'>%LOCALS%" + "</font><br>")ERB", {}, resolve);
    engine.executeInstruction(line);
    check(console.buffer().count() == 1
          && console.buffer().at(0).plainText() == "suffix"
          && console.buffer().at(0).flatSpans().first().style.color == QColor("red"),
          "FORM inside concatenation completes a quoted tag across AST boundaries");
    console.clearAll();
    line = AstBuilder::build(R"ERB(HTML_PRINT @"<but" + @"ton value='{LOCAL++}' title='a > b'>" + @"[%LOCALS,8,LEFT%]</button><br>")ERB", {}, resolve);
    storage.setLocalInt(0, 12);
    storage.setLocalStr(0, "<b>");
    engine.executeInstruction(line);
    check(console.buffer().count() == 1 && console.buffer().at(0).plainText() == "[<b>     ]"
          && console.buffer().at(0).segments.first().isButton
          && console.buffer().at(0).segments.first().intValue == 12
          && console.buffer().at(0).segments.first().tooltip == "a > b"
          && storage.getLocalInt(0) == 13,
          "FORM composite retains structure, alignment and exactly-once interpolation");
    console.clearAll();
    line = AstBuilder::build(R"ERB(HTML_PRINT @"%"<b>literal</b>&amp;"%<br>")ERB", {}, resolve);
    engine.executeInstruction(line);
    check(console.buffer().count() == 1
          && console.buffer().at(0).plainText() == "<b>literal</b>&amp;"
          && !console.buffer().at(0).flatSpans().first().style.bold,
          "literal interpolation stays opaque text including markup and entities");
    console.clearAll();
    line = AstBuilder::build(R"ERB(HTML_PRINT @"<b%LOCALS%>tail<br>")ERB", {}, resolve);
    storage.setLocalStr(0, "r");
    engine.executeInstruction(line);
    check(console.buffer().count() == 1 && console.buffer().at(0).plainText() == "<br>tail",
          "dynamic tag name cannot discard an interpolation or generate structure");
    QList<LogicalLine> lines;
    for (const QString& text : QStringList{"@STR", "#DIMS SHARED", "PRINTS SHARED", "@INT", "#DIM SHARED", "PRINTV SHARED"}) lines.append(AstBuilder::build(text, {}, resolve));
    table.loadScript("scope", lines); table.finalizeParse();
    const auto str = table.lineAt("scope", 2)->arguments.first().ast;
    const auto num = table.lineAt("scope", 5)->arguments.first().ast;
    check(str && num && str != num && str->valueType() == OperandType::Str && num->valueType() == OperandType::Int, "same text bound independently in distinct scopes");
    check(!table.expressionAst("SHARED"), "finalized table rejects source compilation");
    EraParseTable loadCompiler(nullptr);
    const auto compile = [&](const QString& source) { return loadCompiler.expressionAst(source); };
    // The statement's AST is authoritative; raw text must not be reparsed.
    storage.setLocalInt(0, 10);
    auto assignment = AstBuilder::build("RESULT = LOCAL++", {}, compile);
    assignment.arguments[1].raw = "999";
    engine.executeInstruction(assignment);
    check(storage.getLocalInt(0) == 11 && storage.getSystemVariable("RESULT", 0) == 10, "assignment consumes AST exactly once");
    auto staticForm = compile("STRFORM(\"value={LOCAL}\")");
    storage.setLocalInt(0, 23);
    check(staticForm && staticForm->kind() == NodeKind::StrForm
          && static_cast<const StrFormNode&>(*staticForm).rawText() == "value={LOCAL}"
          && evaluator.evaluate(*staticForm, &storage).toString() == "value=23",
          "STRFORM keeps raw text while evaluating its load-time FORM node");
    auto rawConsumer = compile("STRLEN(STRFORM(\"value={LOCAL}\"))");
    check(rawConsumer && evaluator.evaluate(*rawConsumer, &storage).toLongLong() == 13,
          "non-FORM string consumer reads STRFORM raw text");
    storage.setLocalStr(0, "raw {LOCAL}");
    auto rawForm = compile("STRFORM(LOCALS)");
    check(rawForm && evaluator.evaluate(*rawForm, &storage).toString() == "raw {LOCAL}",
          "STRFORM variable reads raw text without runtime parsing");
    QString formsOutput;
    const auto formsConnection = QObject::connect(&engine, &ExecutionEngine::consolePrint,
        [&formsOutput](const QString& text, bool) { formsOutput = text; });
    storage.setLocalInt(0, 42);
    storage.setLocalStr(0, "value {LOCAL} %LOCALS%");
    auto forms = AstBuilder::build("PRINTFORMSL \"score={LOCAL}; \" + LOCALS", {}, compile);
    check(forms.arguments.first().ast && forms.arguments.first().ast->kind() == NodeKind::StrForm,
          "FORMS concatenation compiles to static parts");
    forms.arguments.first().raw = "invalid source";
    engine.executeInstruction(forms);
    check(formsOutput == "score=42; value {LOCAL} %LOCALS%",
          "FORMS literal interpolates once and variable result remains text");
    storage.setLocalInt(0, 9);
    storage.setLocalStr(0, "changed");
    engine.executeInstruction(forms);
    check(formsOutput == "score=9; changed", "FORMS parts read current variable values without parsing");
    engine.executeInstruction(AstBuilder::build("PRINTFORMS \"{LOCAL}\"", {}, compile));
    check(formsOutput == "9", "FORMS quoted literal compiles interpolation at load time");
    engine.executeInstruction(AstBuilder::build("PRINTFORMS LOCALS", {}, compile));
    check(formsOutput == "changed", "FORMS variable is an independent AST operation");
    QObject::disconnect(formsConnection);
    auto preloaded = compile("123 + 456");
    check(preloaded && evaluator.evaluate(*preloaded, &storage).toLongLong() == 579,
          "runtime evaluator consumes compiled node");
    storage.setLocalInt(1, 0); // allocate the second local slot before bounded list assignment
    auto listAssignment = AstBuilder::build("LOCAL = 11, 22", {}, compile);
    listAssignment.arguments[1].raw = "999, LOCAL++";
    engine.executeInstruction(listAssignment);
    check(storage.getLocalInt(0) == 11 && storage.getLocalInt(1) == 22,
          "array assignment evaluates stored list ASTs without splitting raw source");
    auto lhsAssignment = AstBuilder::build("LOCAL:2 = 77", {}, compile);
    lhsAssignment.arguments[0].raw = "LOCAL:999";
    engine.executeInstruction(lhsAssignment);
    check(storage.getLocalInt(2) == 77, "assignment uses VariableNode instead of raw colon parsing");
    storage.setLocalInt(0, 0);
    storage.setLocalStr(0, "left");
    auto compoundString = AstBuilder::build("LOCALS:(LOCAL++) += \"+\"", {}, compile);
    engine.executeInstruction(compoundString);
    check(storage.getLocalInt(0) == 1 && storage.getLocalStr(0) == "left+",
          "string compound assignment evaluates lvalue index exactly once");
    int missingErrors = 0;
    const auto errorConnection = QObject::connect(&engine, &ExecutionEngine::errorOccurred, [&] { ++missingErrors; });
    auto missingLhs = AstBuilder::build("LOCAL:2 = 99", {}, compile);
    missingLhs.arguments[0].ast.clear();
    check(!engine.executeInstruction(missingLhs) && missingErrors > 0 && storage.getLocalInt(2) == 77,
          "missing lvalue AST fails without modifying slot zero");
    QObject::disconnect(errorConnection);
    auto caseLine = AstBuilder::build("CASE 1 TO 3, IS >= 8", {}, compile);
    check(caseLine.caseCacheReady && caseLine.caseCache.size() == 2,
          "CASE clauses are ready before execution");
    qInfo() << "print template failures:" << failures;
    return failures ? 1 : 0;
}
