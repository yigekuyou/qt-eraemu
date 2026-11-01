#include <QCoreApplication>
#include <QDebug>
#include <type_traits>

#include "print_template.h"
#include "ast_builder.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "expression_evaluator.h"
#include "console_backend.h"

// 段 -> span 摊平（行结构是「行 -> 段 -> span」两层；断言按最小单位读）
static QList<ConsoleSpan> flatSpansOf(const ConsoleDisplayLine& line) {
    QList<ConsoleSpan> flat;
    for (const ConsoleSegment& seg : line.segments) flat += seg.spans;
    return flat;
}

static_assert(!std::is_convertible_v<QString, FlatStringValue>);

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
    {
        // Two source occurrences intentionally use exactly the same AST pointer.
        const auto increment = table.expressionAst("LOCAL++");
        const auto repeated = QSharedPointer<StrFormNode>::create(QList<StrFormPart>{
            StrFormPart::makeText("<button value='"), StrFormPart::makeExpr(increment),
            StrFormPart::makeText("'>"), StrFormPart::makeExpr(increment),
            StrFormPart::makeText("</button>")});
        PrintTemplateCompiler::prepare(repeated);
        storage.setLocalInt(0, 7);
        auto value = evaluator.evaluateFlat(*repeated, &storage);
        check(value.text == "<button value='7'>8</button>" && storage.getLocalInt(0) == 9
              && value.markup.parts.at(0).buttonValue == "7" && value.markup.parts.at(1).text == "8",
              "shared AST pointer executes once for each source occurrence, raw/markup reuse its value");
        const auto failing = table.expressionAst("1 / 0");
        const auto aborting = QSharedPointer<StrFormNode>::create(QList<StrFormPart>{
            StrFormPart::makeText("<b>"), StrFormPart::makeExpr(increment),
            StrFormPart::makeExpr(failing), StrFormPart::makeExpr(increment), StrFormPart::makeText("</b>")});
        PrintTemplateCompiler::prepare(aborting);
        storage.setLocalInt(0, 10);
        check(!evaluator.evaluate(*aborting, &storage).isValid() && storage.getLocalInt(0) == 11,
              "failed occurrence aborts flattening before later side effects");
        storage.setStringSlotInCurrentFrame("LOCALS", {0}, value);
        storage.setStringSlotInCurrentFrame("ARGS", {0}, value);
        auto frame = storage.localContext();
        storage.setLocalStr(0, "replacement"); storage.setArgStr(0, "replacement");
        storage.setLocalContext(frame);
        check(storage.getStringSlotInCurrentFrame("LOCALS", {0}).markup.parts.at(0).buttonValue == "7"
              && storage.getStringSlotInCurrentFrame("ARGS", {0}).text == value.text,
              "local and argument frames preserve eager values after evaluation");
        storage.setStringSlotInCurrentFrame("FLAT3D", {1, 2, 3}, value);
        storage.registerCharaDataVariable("FLATCHARA", true, 2);
        storage.addChara(20); storage.addChara(30);
        storage.setStringSlotInCurrentFrame("FLATCHARA", {0, 1, 2}, value);
        storage.swapChara(0, 1);
        check(storage.getStringSlotInCurrentFrame("FLAT3D", {1, 2, 3}).markup.parts.at(0).buttonValue == "7"
              && storage.getStringSlotInCurrentFrame("FLATCHARA", {1, 1, 2}).text == value.text,
              "multidimensional and reordered character slots preserve flattened structure");
        storage.setStringSlotInCurrentFrame("FLAT3D", {1, 2, 3}, FlatStringValue(value.text));
        check(storage.getStringSlotInCurrentFrame("FLAT3D", {1, 2, 3}).markup.parts.size() == 1,
              "plain multidimensional writes replace structure with opaque text");
    }
    {
        FlatStringValue detached;
        QWeakPointer<ExpressionNode> sourceLifetime;
        {
            auto source = QSharedPointer<LiteralNode>::create(QStringLiteral("<b>retained</b>"));
            sourceLifetime = source;
            PrintTemplateCompiler::prepare(source);
            detached = evaluator.evaluateFlat(*source, &storage);
            storage.setStringSlotInCurrentFrame("ARGS", {1}, detached);
        }
        check(sourceLifetime.isNull() && detached.text == "<b>retained</b>"
              && detached.markup.parts.first().kind == PrintTemplatePart::Kind::Style
              && storage.getStringSlotInCurrentFrame("ARGS", {1}).text == detached.text,
              "flat result and argument slot release all source AST references on return");
        check(!toVariant(detached).canConvert<QString>() && toVariant(detached).toString().isEmpty(),
              "flat values require explicit text conversion");
    }
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
    const auto moduloTextForm = AstBuilder::build(R"ERB(CALL F, @"A %= 2")ERB", {}, resolve);
    check(moduloTextForm.argument.typeOk && moduloTextForm.arguments.at(1).ast
          && evaluator.evaluate(*moduloTextForm.arguments.at(1).ast, &storage).toString() == "A %= 2",
          "FORM keeps an unpaired %= operator as load-time text");
    const auto invalidFormCall = AstBuilder::build(R"ERB(CALL F, @"A % 2")ERB", {}, resolve);
    check(!invalidFormCall.argument.typeOk && !invalidFormCall.arguments.at(1).ast,
          "an unpaired percent remains a load-time FORM error");
    const auto escapedFormCall = AstBuilder::build(R"ERB(CALL F, @"A \%= 2")ERB", {}, resolve);
    check(escapedFormCall.argument.typeOk && escapedFormCall.arguments.at(1).ast
          && evaluator.evaluate(*escapedFormCall.arguments.at(1).ast, &storage).toString() == "A %= 2",
          "escaped FORM percent compiles and evaluates without runtime parsing");
    const auto parenthesizedStatement = AstBuilder::build("ABS(-7)", {}, resolve);
    check(parenthesizedStatement.arguments.first().ast
          && evaluator.evaluate(*parenthesizedStatement.arguments.first().ast, &storage).toLongLong() == 7,
          "parenthesized function statement is not wrapped twice");
    {
        ProcessState scopedState;
        EraParseTable scoped(&scopedState);
        scoped.setVariableStorage(&storage);
        ExpressionEvaluator scopedEvaluator;
        scoped.setExpressionEvaluator(&scopedEvaluator);
        const auto localResolve = [&](const QString& e) { return scoped.expressionAst(e); };
        QList<LogicalLine> localLines;
        for (const auto& source : QStringList{"@FIRST", "#DIMS SHARED", R"ERB(PRINTS @"%SHARED%")ERB",
                                              "@SECOND", "#DIM SHARED", "RETURN"})
            localLines.append(AstBuilder::build(source, {}, localResolve));
        scoped.loadScript("private_form", localLines);
        scoped.markLoadComplete();
        const auto* loaded = scoped.lineAt("private_form", 2);
        check(loaded && loaded->arguments.first().ast && !loaded->isError
              && loaded->argument.typeOk && validateExpression(*loaded->arguments.first().ast).isEmpty(),
              "FORM recompiles at load time with owner private string type");
    }
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
        // 变量读取是**取变量节点**：值在打印期抽取成结构（`<b>&amp;</b>` →
        // 粗体 "&"），不再是转义后的一整段字面量文本。
        check(row.plainText() == "8&", "variable read is extracted and tokenized at print time");
        check(row.segments.size() == 1 && row.segments.first().isButton && row.segments.first().intValue == 7, "button expression value retained");
        const auto spans = flatSpansOf(row);
        check(spans.size() == 2 && spans[0].style.bold && !spans[0].style.italic
              && spans[1].style.italic && spans[1].style.bold,
              "nested button styles restored (value tag nests inside the outer one)");
        check(row.segments.first().tooltip == "a > b", "quoted greater-than scanner");
    }
    const auto printHtmlFixture = [&](const QString& text) {
        const auto compiled = PrintTemplateCompiler::compile(QSharedPointer<LiteralNode>::create(text));
        console.printTemplate(PrintTemplateCompiler::evaluateOccurrences(*compiled, [](const ExpressionNode&, int) { return QVariant(QString()); }));
    };
    // FL3.8 WindowDrawer overlays a background, border and content on one row.
    console.clearAll();
    console.setFontSize(16);
    printHtmlFixture("<nobr><button pos='0' value='_'><shape type='rect' param='0,0,8500,100' color='#000000'></button>"
                     "<nonbutton pos='100' title='label'><font color='red'>A</font><b>B</b></nonbutton>"
                     "<button pos='3300' value='1000'>Quest</button>tail</nobr>");
    check(console.buffer().count() == 1, "positioned window layers stay on one row");
    if (console.buffer().count() == 1) {
        const auto& row = console.buffer().at(0);
        check(row.segments.size() == 4, "nonbutton owns a separate non-clickable segment");
        if (row.segments.size() == 4) {
            const auto& label = row.segments.at(1);
            const auto& quest = row.segments.at(2);
            check(!label.isButton && label.tooltip == "label" && label.spans.size() == 2
                      && label.spans[0].positionedXpx == 16 && label.spans[1].positionedXpx == 24,
                  "nonbutton pos uses font percentages and styled children advance within it");
            check(quest.isButton && quest.intValue == 1000 && quest.spans.first().positionedXpx == 528
                      && row.segments.last().relCol == quest.relCol + quest.cols,
                  "button pos resets X after a wide background and following text continues at its end");
        }
    }
    console.clearAll();
    printHtmlFixture("<nobr><button pos='0'>plain</button><nonbutton pos='50'>x</nonbutton></nobr>");
    check(console.buffer().count() == 1 && console.buffer().at(0).segments.size() == 2
              && !console.buffer().at(0).segments.first().isButton
              && !console.buffer().at(0).segments.last().isButton,
          "button without value and nonbutton are both non-clickable");
    // FORM attributes and runtime-generated HTML take the same positioning path.
    console.clearAll();
    storage.setLocalInt(0, 125);
    engine.executeInstruction(AstBuilder::build(
        "HTML_PRINT @\"<nobr><nonbutton pos='{LOCAL++}'>X</nonbutton></nobr>\"", {}, resolve));
    check(storage.getLocalInt(0) == 126 && console.buffer().count() == 1
              && flatSpansOf(console.buffer().at(0)).first().positionedXpx == 20,
          "FORM pos evaluates once and retains pixel coordinates between grid columns");
    console.clearAll();
    console.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(
        "<nobr><nonbutton pos='200'>runtime</nonbutton></nobr>"));
    check(console.buffer().count() == 1
              && flatSpansOf(console.buffer().at(0)).first().positionedXpx == 32,
          "runtime nonbutton markup preserves pos");
    check(PrintTemplateCompiler::parseRuntimeMarkup("<nonbutton pos='0'>x</nonbutton>")
              .problems.contains("CanNotUsePosWithoutNobr"), "pos without nobr is diagnosed");
    check(PrintTemplateCompiler::parseRuntimeMarkup("<p align='right'><nobr><nonbutton pos='0'>x</nonbutton></nobr></p>")
              .problems.contains("CanOnlyUsePosAssignmentLR"), "pos with non-left alignment is diagnosed");
    check(PrintTemplateCompiler::parseRuntimeMarkup("<nobr><nonbutton pos='bad'>x</nonbutton></nobr>")
              .problems.contains("AttributeCanNotInterpretNum: pos"), "invalid pos is diagnosed");
    check(PrintTemplateCompiler::parseRuntimeMarkup("<button value='1'><nonbutton>x</nonbutton></button>")
              .problems.contains("NestedButtonTag"), "nested button/nonbutton is diagnosed");
    console.clearAll();
    printHtmlFixture("<nobr><button pos='0' value='7'>click</button><nonbutton pos='500'>label</nonbutton></nobr>");
    console.notifyInputRequested("INPUT");
    qint64 positionedInput = -1;
    const auto positionedConnection = QObject::connect(&console, &ConsoleBackend::inputSubmitted,
        [&](qint64 value) { positionedInput = value; });
    console.clickAt(0, 1);
    check(positionedInput == -1, "positioned nonbutton does not submit input");
    console.clickAt(0, 0);
    check(positionedInput == 7, "positioned button retains its input value");
    QObject::disconnect(positionedConnection);
    console.notifyInputDone();
    const auto background = PrintTemplateCompiler::parseRuntimeMarkup(
        "<shape type='rect' param='0,0,100,100' color='#000000' bcolor='#ffffff'>");
    check(background.parts.first().style.color == QColor("#000000")
              && background.parts.first().style.buttonColor == QColor("#ffffff"),
          "window background shape preserves explicit normal and focus colors");
    const int savedFontSize = console.fontSize();
    const int savedLineHeight = console.lineHeight();
    // eraTW stores each layer in TEMP_HTML, then prepends a spacer before <p>.
    // Exercise structured values and plain-text replacement through the same slot.
    for (const bool plainReplacement : {false, true}) {
        console.clearAll();
        console.setFontSize(16);
        console.setLineHeight(16);
        const QString faceHtml = QStringLiteral(
            "<shape type='space' param='0'><p align='left'>"
            "<img src='face' width='400' height='400'>");
        const QString frameHtml = QStringLiteral(
            "<shape type='space' param='0'><p align='left'>"
            "<img src='frame' width='400' height='400' ypos='-100'>");
        for (int layer = 0; layer < 2; ++layer) {
            const QString html = layer == 0 ? faceHtml : frameHtml;
            storage.setStringSlotInCurrentFrame("LOCALS", {layer},
                FlatStringValue(html, PrintTemplateCompiler::parseRuntimeMarkup(html)));
            if (plainReplacement) {
                // Text-only reads (including save-file restoration) must replace old IR.
                storage.setLocalStr(layer, "stale");
                storage.setLocalStr(layer, html);
            }
        }
        engine.executeInstruction(AstBuilder::build("HTML_PRINT LOCALS:0", {}, resolve));
        engine.executeInstruction(AstBuilder::build("HTML_PRINT LOCALS:1", {}, resolve));
        check(storage.getStringSlotInCurrentFrame("LOCALS", {1}).text == frameHtml,
              "layer slot preserves ypos through storage and replacement");
        check(console.buffer().count() == 2,
              "stored layer markup does not submit spacer as an extra line");
        if (console.buffer().count() == 2) {
            const auto face = flatSpansOf(console.buffer().at(0)).last();
            const auto frameImage = flatSpansOf(console.buffer().at(1)).last();
            check(face.kind == ConsoleSpanKind::Image && face.text == "face"
                  && frameImage.kind == ConsoleSpanKind::Image && frameImage.text == "frame"
                  && frameImage.yposRaw == -100 && frameImage.top == -16,
                  "structured and text-only readback keep frame image and pixel compensation");
        }
    }
    console.setFontSize(savedFontSize);
    console.setLineHeight(savedLineHeight);
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
        const auto& row = console.buffer().at(0); const auto spans = flatSpansOf(row);
        check(row.plainText() == QString::fromUtf8("xy &lt; A 😀"), "entities decoded once including numeric Unicode");
        check(spans.size() >= 4 && spans[0].kind == ConsoleSpanKind::Image && spans[0].text == "a&b" && spans[0].imageSize.width() == 200 && spans[0].style.color == QColor("red") && spans[1].kind == ConsoleSpanKind::Shape && spans[1].style.color == QColor("red"), "image and shape inherit style and attributes");
        check(spans.size() >= 4 && !spans.last().style.bold && !spans.last().style.color.isValid(), "closing styles restored");
    }
    // 结构问题（C# 的 UnexpectedCloseTag / 未闭合栈）由唯一词法器记录在模板上：
    // 装载期只能对 verified（整串一次分词）的静态模板报诊断，动态值必须等到
    // 打印期整串重新分词那一次。这里分别验证两条路径都拿得到问题清单。
    {
        const auto staticLine = AstBuilder::build(
            QStringLiteral("HTML_PRINT @\"<font>x</font></b>\""), {}, resolve);
        check(staticLine.printTemplate && !staticLine.printTemplate->problems.isEmpty()
                  && staticLine.printTemplate->problems.first().contains(QLatin1String("</b>")),
              "load-time template records UnexpectedCloseTag");
        check(PrintTemplateCompiler::parseRuntimeMarkup(QStringLiteral("<font>x")).problems.size() == 1
                  && PrintTemplateCompiler::parseRuntimeMarkup(QStringLiteral("<font>x"))
                         .problems.first().contains(QLatin1String("UnclosedTag")),
              "runtime re-lex records UnclosedTag");
        // 结构标签不进样式栈：`</p>`/`</button>`/`</nobr>` 不得被误报。
        check(PrintTemplateCompiler::parseRuntimeMarkup(
                  QStringLiteral("<p align='left'>[x]</p><button value='1'>b</button><nobr></nobr>"))
                  .problems.isEmpty(),
              "structural closing tags are not style-stack problems");
    }
    console.clearAll();
    printHtmlFixture("<font color='red'><b>inner</font>after</b>");
    if (console.buffer().count()) {
        const auto spans = flatSpansOf(console.buffer().at(0));
        check(console.buffer().at(0).plainText() == "innerafter"
              && spans.size() >= 2
              && spans.at(0).style.bold && spans.at(0).style.color == QColor("red")
              && spans.at(1).style.bold && spans.at(1).style.color == QColor("red"),
              "mismatched closing tag does not shift the style stack");
    } else check(false, "mismatched closing tag produces a line");
    console.clearAll();
    // eraTW IMAGE.ERB:98-110 的原样复刻（画像表示設定 的角色条）：
    //   开标签走已分词的片段，闭标签 `sPrint_Text += @"</font>"` 是**单独**一段。
    // `@"</font>"` 自身没有结构化片段（装载期编译结果为空），运行期就是普通字符串；
    // 一旦拼进已分词的值，整条 IR 的结构就不再可证明（verified=false），
    // 打印期必须按同一词法器重新分词 —— 否则闭标签作为字面量留在行里，
    // 实测把行打成 `--------[霊夢]</font>--------`（宽度多出 7 格，后续按钮错位）。
    {
        const auto runAssignment = [&](const QString& source) {
            const auto assignment = AstBuilder::build(source, {}, resolve);
            // 装载器在收尾步骤做的事：给每条字符串右值准备 Template IR
            for (const auto& operand : assignment.arguments)
                PrintTemplateCompiler::prepare(operand.ast);
            engine.executeInstruction(assignment);
        };
        runAssignment(QStringLiteral("LOCALS =  <p align='left'><nobr>"));
        runAssignment(QStringLiteral("LOCALS += @\"<font color='#66ffff'>\""));
        runAssignment(QStringLiteral("LOCALS += @\"<button value='1001' title='t1'>[x]</button>\""));
        runAssignment(QStringLiteral("LOCALS += @\"</font>\""));
        runAssignment(QStringLiteral("LOCALS += @\"<shape type='space' param='10'>\""));
        runAssignment(QStringLiteral("HTML_PRINT LOCALS"));
        if (console.buffer().count() == 1) {
            const auto& row = console.buffer().at(0);
            const auto spans = flatSpansOf(row);
            bool button = false;
            for (const auto& segment : row.segments)
                if (segment.isButton && segment.intValue == 1001) button = true;
            // 闭标签之后的 <shape> 不得继承 #66ffff：这就是「高亮/错位」的第二种形态
            // （EndStyle 缺失时尾部 spacer 吃掉按钮颜色，测量宽度也跟着漂）。
            bool trailingLeaksColor = false;
            for (const auto& span : spans)
                if (span.kind == ConsoleSpanKind::Shape && span.style.color.isValid()) trailingLeaksColor = true;
            check(!row.plainText().contains(QLatin1String("</font>"))
                  && row.plainText() == "[x]"
                  && button && !spans.isEmpty() && spans.first().style.color == QColor("#66ffff")
                  && !trailingLeaksColor,
                  "closing tag spliced in as a plain string is re-parsed, not printed literally");
        } else {
            check(false, "mixed compiled/plain += chain produces one line");
        }
    }
    console.clearAll();
    // 更严格的一档：两个片段**各自**都是结构化 IR（开标签一段、`</font><shape>` 一段），
    // 单独分词的栈状态都自洽，但跨片段的配对证明不了。拼接一律作废 IR，
    // 否则闭标签会被当成空栈上的噪声消费掉，尾部 <shape> 继承红色。
    {
        const auto runAssignment = [&](const QString& source) {
            const auto assignment = AstBuilder::build(source, {}, resolve);
            for (const auto& operand : assignment.arguments)
                PrintTemplateCompiler::prepare(operand.ast);
            engine.executeInstruction(assignment);
        };
        runAssignment(QStringLiteral("LOCALS =  <font color='#ff0000'>"));
        runAssignment(QStringLiteral("LOCALS += @\"</font><shape type='space' param='5'>\""));
        runAssignment(QStringLiteral("HTML_PRINT LOCALS"));
        if (console.buffer().count() == 1) {
            bool leaked = false;
            for (const auto& span : flatSpansOf(console.buffer().at(0)))
                if (span.kind == ConsoleSpanKind::Shape && span.style.color.isValid()) leaked = true;
            check(!leaked && console.buffer().at(0).plainText().isEmpty(),
                  "tags straddling two separately compiled pieces are re-paired at print time");
        } else {
            check(false, "straddling compiled pieces produce one line");
        }
    }
    console.clearAll();
    storage.setLocalStr(0, "<u>runtime</u><br>");
    line = AstBuilder::build("HTML_PRINT LOCALS", {}, resolve);
    check(line.arguments.first().ast && line.printTemplate, "HTML string is compiled during load");
    engine.executeInstruction(line);
    // 装入变量的裸文本在装载期无法构筑标签（值的来源与 AST 无关）—— 但 C#
    // PrintHtml 本来就是**打印时**从最终字符串解析（Html2DisplayLine）。
    // 现在与 C# 对齐：coversText 失败 -> parseRuntimeMarkup 补解析，
    // 标签生效（此前按文本输出 + [未完成] 留痕；FL3.8 立绘框 div 正是这种写法）。
    // 尾部 <br> 只冲刷其前的内容（不留空行）—— 与正文 PRINT 的换行语义一致。
    check(console.buffer().count() == 1
          && console.buffer().at(0).plainText() == "runtime"
          && flatSpansOf(console.buffer().at(0)).first().style.underline,
          "运行期拼出的标签在打印时补解析（<u> 生效、<br> 冲刷成行）");
    console.clearAll();
    // eraTW INFO_CLOCK_IMAGE 的原样复刻：`=` 裸文本 + 三次 `+=`，其中 <img> 被
    // 拆成两段（第一段以未闭合的 '<' 结尾）。装载期把整条写链拼成一棵 PLUS 树
    //（EraParseTable::stitchStringFragmentRuns）后，标签在装载期即闭合、生成
    // Image 片段；运行期只求值这条链，不再解析任何标签。
    storage.setLocalStr(0, QString());
    {
        EraParseTable clockTable(&state);
        clockTable.setExpressionEvaluator(&evaluator);
        clockTable.setVariableStorage(&storage);
        ExecutionEngine clockEngine(&storage);
        clockEngine.setParseTable(&clockTable);
        clockEngine.setExpressionEvaluator(&evaluator);
        QObject::connect(&clockEngine, &ExecutionEngine::consolePrintTemplate,
                         &console, &ConsoleBackend::printTemplate);
        QList<LogicalLine> clockLines;
        int clockLine = 0;
        for (const QString& source : QStringList{
                 QStringLiteral("LOCALS =  <p align='right'><nobr>"),
                 QStringLiteral("LOCALS += @\"<img src='clock_07_00' height='500'\""),
                 QStringLiteral("LOCALS += @\"ypos='201'>\""),
                 QStringLiteral("LOCALS += @\"</p>\""),
                 QStringLiteral("HTML_PRINT LOCALS")}) {
            clockLines.append(AstBuilder::build(source,
                ScriptPosition(QStringLiteral("clock.ERB"), ++clockLine, 1),
                [&clockTable](const QString& text) { return clockTable.expressionAst(text); }));
        }
        clockTable.loadScript(QStringLiteral("CLOCK.ERB"), clockLines);
        // 装载器在装载期做的同一步：HTML 字符串反向追踪（把写链拼成一棵 PLUS 树）
        clockTable.traceHtmlStringArguments();
        clockTable.markLoadComplete();
        console.clearAll();
        for (int i = 0; i < clockLines.size(); ++i)
            clockEngine.executeInstruction(*clockTable.lineAt(QStringLiteral("CLOCK.ERB"), i));
        if (console.buffer().count() == 1) {
            bool image = false;
            for (const auto& span : flatSpansOf(console.buffer().at(0))) {
                if (span.kind == ConsoleSpanKind::Image && span.text == "clock_07_00"
                    && span.imageSize.height() == 500 && span.yposRaw == 201) image = true;
            }
            check(image && console.buffer().at(0).plainText().isEmpty(),
                  "split tag is closed during load by stitching the += chain (eraTW clock)");
        } else {
            check(false, "stitched clock line did not print a single line");
        }
        // provider 挂在**共享** evaluator 上并捕获本表（维数 provider / 常量 provider）：
        // 离开作用域前必须摘掉并交还给外层表，否则留下悬空捕获。
        evaluator.setConstProvider({});
        evaluator.setConstArrayProvider({});
        evaluator.setConstArrayChecker({});
        table.setExpressionEvaluator(&evaluator);
    }
    console.clearAll();
    storage.setLocalStr(0, "blue");
    line = AstBuilder::build("HTML_PRINT @\"<font color='%LOCALS%'><b>blue</b></font><br>\"", {}, resolve);
    engine.executeInstruction(line);
    check(console.buffer().count() == 1 && flatSpansOf(console.buffer().at(0)).first().style.color == QColor("blue"), "dynamic font attribute");

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
          && flatSpansOf(console.buffer().at(0)).first().kind == ConsoleSpanKind::Image
          && flatSpansOf(console.buffer().at(0)).first().text == "TW_title004",
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
          && flatSpansOf(console.buffer().at(0)).at(2).style.bold,
          "unfinished tag does not swallow the next complete tag");
    console.clearAll();
    storage.setLocalStr(0, "suffix");
    line = AstBuilder::build(R"ERB(HTML_PRINT "<font title='a > " + @"b' color='red'>%LOCALS%" + "</font><br>")ERB", {}, resolve);
    engine.executeInstruction(line);
    check(console.buffer().count() == 1
          && console.buffer().at(0).plainText() == "suffix"
          && flatSpansOf(console.buffer().at(0)).first().style.color == QColor("red"),
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
          && !flatSpansOf(console.buffer().at(0)).first().style.bold,
          "literal interpolation stays opaque text including markup and entities");
    console.clearAll();
    line = AstBuilder::build(R"ERB(HTML_PRINT @"<b%LOCALS%>tail<br>")ERB", {}, resolve);
    storage.setLocalStr(0, "r");
    engine.executeInstruction(line);
    // 不做运行期解析：插值拼出来的 `>` 不会把前面的 `<b` 收成标签（装载期单向编译）。
    // C# 对齐（此前是「装载期单向编译」的移植特有行为）：插值拼出来的 `<b%LOCALS%>`
    // 在装载期无法收成标签（Text 部件留着未闭合的 '<'）→ coversText 失败 →
    // 打印时从**最终字符串** `<br>tail<br>` 补解析（Html2DisplayLine 同款）。
    // 插值仍然只求值一次（OccurrenceContext 先算文本，再走 parseRuntimeMarkup）。
    check(console.buffer().count() == 2
          && console.buffer().at(0).plainText().isEmpty()
          && console.buffer().at(1).plainText() == "tail",
          "动态拼出的标签名（<b%LOCALS%> -> <br>）按 C# 在打印时解析");
    QList<LogicalLine> lines;
    for (const QString& text : QStringList{"@STR", "#DIMS SHARED", "PRINTS SHARED", "@INT", "#DIM SHARED", "PRINTV SHARED"}) lines.append(AstBuilder::build(text, {}, resolve));
    table.loadScript("scope", lines); table.markLoadComplete();
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
          && evaluator.evaluate(*staticForm, &storage).toString() == "value=23",
          "STRFORM evaluates its load-time FORM node");
    // 字符串实参只有「求值」一条取值路径：STRLEN 拿到的是展开后的 "value=23"
    //（8 个字符），不是 FORM 原文（13 个字符）。原文副本已从 StrFormNode 删除。
    auto expandedConsumer = compile("STRLEN(STRFORM(\"value={LOCAL}\"))");
    check(expandedConsumer && evaluator.evaluate(*expandedConsumer, &storage).toLongLong() == 8,
          "string consumers read the expanded form (single evaluation path)");
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
    {
        // 标签**语法**位置的洞：`<div %LOCALS%>`。标签身份与属性名单在装载期就
        // 定了，运行期只把这段值拆成 `名字 = 值` 对并按名单筛（不重新分词）。
        PrintTemplate result;
        auto tagAst = compile(QStringLiteral("STRFORM(\"<div %LOCALS% border='1px'>x</div>\")"));
        const auto compiled = PrintTemplateCompiler::compile(tagAst);
        if (compiled) {
            result = PrintTemplateCompiler::evaluateOccurrences(*compiled, [](const ExpressionNode&, int) {
                return QVariant(QStringLiteral("rect = '600, 0, 60px, 60px'"));
            });
        }
        bool div = false, foreignDropped = true;
        for (const auto& part : result.parts)
            if (part.kind == PrintTemplatePart::Kind::Division && part.x == 600 && part.width == 60) div = true;
        check(compiled && div, "bare attribute fragment supplies the attribute list of a load-time div");
        auto lockedAst = compile(QStringLiteral("STRFORM(\"<div %LOCALS%></div>\")"));
        const auto lockedCompiled = PrintTemplateCompiler::compile(lockedAst);
        if (lockedCompiled) {
            const auto locked = PrintTemplateCompiler::evaluateOccurrences(*lockedCompiled, [](const ExpressionNode&, int) {
                return QVariant(QStringLiteral("rect = '1, 0, 9px, 9px' nonsense = 'x'"));
            });
            for (const auto& part : locked.parts)
                if (part.kind == PrintTemplatePart::Kind::Division)
                    foreignDropped = part.x == 1 && part.width == 9;
            check(foreignDropped, "attributes outside the div set are ignored (locked attribute names)");
        } else check(false, "locked fragment compiles");
    }
    qInfo() << "print template failures:" << failures;
    return failures ? 1 : 0;
}
