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
    console.clearAll();
    console.printHtml("<font color='red'><img src='a&amp;b' width='200'><shape type='rect' param='100,50'></font><b>x</b>y &amp;lt; &#65; &#x1F600;<br/>");
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
    check(!line.printTemplate, "runtime string remains expression until execution");
    engine.executeInstruction(line);
    check(console.buffer().count() == 1 && console.buffer().at(0).plainText() == "runtime" && console.buffer().at(0).flatSpans().first().style.underline, "runtime string uses same template compiler");
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
    console.printHtml("<p align='right'><nobr>abcdefghijk</nobr></p>");
    check(console.buffer().count() == 1 && console.buffer().at(0).align == ConsoleAlign::Right,
          "nobr keeps long paragraph together with requested alignment");
    check(console.wrappingEnabled(), "wrapping restored after template");
    console.print("x"); console.newline();
    check(console.buffer().at(console.buffer().count()-1).align == ConsoleAlign::Left,
          "paragraph alignment does not leak into ordinary print");
    console.setWrappingEnabled(false);

    QList<LogicalLine> lines;
    for (const QString& text : QStringList{"@STR", "#DIMS SHARED", "PRINTS SHARED", "@INT", "#DIM SHARED", "PRINTV SHARED"}) lines.append(AstBuilder::build(text, {}, resolve));
    table.loadScript("scope", lines); table.finalizeParse();
    const auto str = table.lineAt("scope", 2)->arguments.first().ast;
    const auto num = table.lineAt("scope", 5)->arguments.first().ast;
    check(str && num && str != num && str->valueType() == OperandType::Str && num->valueType() == OperandType::Int, "same text bound independently in distinct scopes");
    table.setPosition("scope", 2); auto cachedStr = table.expressionAst("SHARED");
    table.setPosition("scope", 5); auto cachedInt = table.expressionAst("SHARED");
    check(cachedStr && cachedInt && cachedStr != cachedInt && cachedStr->isString() && cachedInt->isInteger(), "runtime expression cache includes scope");
    // The statement's AST is authoritative; raw text must not be reparsed.
    storage.setLocalInt(0, 10);
    auto assignment = AstBuilder::build("RESULT = LOCAL++", {}, resolve);
    assignment.arguments[1].raw = "999";
    engine.executeInstruction(assignment);
    check(storage.getLocalInt(0) == 11 && storage.getSystemVariable("RESULT", 0) == 10, "assignment consumes AST exactly once");
    qInfo() << "print template failures:" << failures;
    return failures ? 1 : 0;
}
