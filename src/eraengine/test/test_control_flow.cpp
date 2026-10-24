#include <QCoreApplication>
#include <QTemporaryDir>
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

[[maybe_unused]] static QString scopeKeyOfFunction(const VariableTable& vt, const QString& function) {
    for (const VariableDecl& d : vt.declarations()) {
        if (!d.scopeKey.isEmpty() && d.function.compare(function, Qt::CaseInsensitive) == 0)
            return d.scopeKey;
    }
    return QString();
}

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
    QStringList script = {"@MAIN"};
    script.append(source);
    // 脚本级构建（唯一入口）：声明是前置步骤，字符串赋值在构建期即定成 FORM。
    // BuiltScript = 行 + 它**唯一**的那份声明扫描：装载时不再重扫。
    const declscan::BuiltScript built = table.buildScriptAst("flow", script, "flow.ERB");
    const QList<LogicalLine>& lines = built.lines;
    table.loadScript("flow", built);
    table.markLoadComplete();
    for (int i = 0; i < lines.size(); ++i) {
        const auto* loaded = table.lineAt("flow", i);
        if (!loaded || loaded->functionName != "FUNC") continue;
        bool compiled = !loaded->isError && loaded->argument.typeOk;
        for (const auto& operand : loaded->arguments) compiled &= bool(operand.ast);
        if (!compiled) {
            qCritical() << name << "FUNC not compiled at load time" << i << loaded->errMes();
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
    const qint64 actual = storage.getGlobalInt1DInCurrentFrame("CHECK", 0);
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
    // LIST_NAME 声明在 @MAIN 内（#DIMS），写入与求值都必须用同一个显式作用域，
    // 否则数据落到顶层槽、读取按 MAIN 私有键 → 空串。
    const eravar::ScopeId mainScope = eravar::internName(QStringLiteral("MAIN"));
    storage.setPrivateScope(eravar::internName(scopeKeyOfFunction(table.variableTable(), "MAIN")), {"LIST_NAME"});
    storage.setGlobalStr1DInCurrentFrame("LIST_NAME", 0, "CHOSEN");
    const auto* entry = table.lineAt("list", 3);
    bool good = entry && !entry->isError && entry->argument.typeOk
        && entry->arguments.size() == 2 && entry->arguments[0].ast && entry->arguments[1].ast;
    if (good) {
        good = entry->arguments[0].ast->kind() == NodeKind::StrForm
            && entry->arguments[1].ast->kind() == NodeKind::Variable
            && entry->arguments[1].ast->valueType() == OperandType::Str
            && stringText(evaluator.evaluate(*entry->arguments[0].ast, &storage, nullptr, mainScope)) == "CHOSEN"
            && stringText(evaluator.evaluate(*entry->arguments[1].ast, &storage, nullptr, mainScope)) == "CHOSEN";
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
// `#DIM(S) CONST X, N = …` 常数数组的**数组内省**：VARSIZE / FINDELEMENT 必须
// 能读到。常量的元素存在常数表里（不在 VariableStorage），只按存储长度取会得到
// 0 / 空数组 —— FL3.8 的 FLPEDIA 正是用
//   `FINDELEMENT(FLPEDIA_MAIN_CATEGORY, MAIN_CATEGORY) == -1`
// 判类别是否存在，读不到就把每个类别都判成「未定義のカテゴリ」。
static void constArrayIntrospection() {
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
    // 文件级（= 广域）常数数组：`CATS:0..2 = a,b,c`
    {
        QList<LogicalLine> globals;
        globals.append(AstBuilder::build("#DIMS CONST CATS = \"a\", \"b\", \"c\"",
            ScriptPosition("g.ERH", 0, 0),
            [&table](const QString& text) { return table.expressionAst(text); }));
        table.loadScript("g", globals, /*isHeaderFile=*/true);   // .ERH 文件级 = 广域
    }
    const QStringList source = {"@MAIN",
        // VARSIZE=3、FINDELEMENT(CATS,\"b\")=1 -> 301；未修复时是 0*100 + (-1) = -1
        "CHECK = VARSIZE(\"CATS\") * 100 + FINDELEMENT(CATS, \"b\")",
        "RETURN"};
    QList<LogicalLine> lines;
    for (int i = 0; i < source.size(); ++i)
        lines.append(AstBuilder::build(source[i], ScriptPosition("cf.ERB", i, 0),
            [&table](const QString& text) { return table.expressionAst(text); }));
    table.loadScript("cf", lines);
    table.markLoadComplete();
    table.setEntryPoint("MAIN");
    runner.runToCompletion();
    const qint64 actual = storage.getGlobalInt1DInCurrentFrame("CHECK", 0);
    if (actual != 301) {
        qCritical() << "常数数组的 VARSIZE/FINDELEMENT" << "actual" << actual << "expected" << 301;
        ++failures;
    } else qInfo() << "PASS 常数数组的 VARSIZE/FINDELEMENT（元素在常数表里）";
}

// 广域常量（.ERH 文件级）与**同名私有变量**的遮蔽。
// eraTW 就是这种组合：`CASINO_SLOT_HELP.ERB:17` 的 `#DIM CONST 色_RED7 = 0xFF00FF`
// 与 `:832` 的 `#DIM 色_RED7`（可写）—— 后者必须遮蔽前者，否则 `SETCOLOR 色_RED7`
// 恒读常量值，「暗転」变暗失效。常数表按归属作用域分离之前，运行期求值是按名字
// 直接命中扁平常数表的，因此这里读到的会是 51。
static void globalConstShadowedByPrivate() {
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

    const AstResolver hdr = [&table](const QString& text) { return table.expressionAst(text); };
    table.loadScript("consts.ERH",
        {AstBuilder::build("#DIM CONST SHADOW = 51", ScriptPosition("consts.ERH", 0, 0), hdr)},
        /*isHeaderFile=*/true);

    const QStringList source = {"@MAIN", "#DIM SHADOW", "SHADOW = 7", "SHADOW ++",
                                "CHECK = SHADOW", "RETURN"};
    // 脚本级构建（唯一入口）：#DIM SHADOW 是构建的**前置步骤**，于是构建期的
    // 常量折叠就看得见这个可写私有变量（同名广域 CONST 被遮蔽，不折），
    // 左值与右值在同一次构建里定型 —— 收尾不再按最终表重解。
    const declscan::BuiltScript built = table.buildScriptAst("shadow", source, "shadow.ERB");
    table.loadScript("shadow", built);
    table.markLoadComplete();
    table.setEntryPoint("MAIN");
    const ExecState state_ = runner.runToCompletion();
    const qint64 actual = storage.getGlobalInt1DInCurrentFrame("CHECK", 0);
    if (state_ == ExecState::Halt && actual == 8) {
        qInfo() << "PASS 广域 CONST 被同名私有变量遮蔽（运行期读到变量值）";
    } else {
        qCritical() << "global CONST shadowed by private variable: state" << int(state_)
                    << "actual" << actual << "expected" << 8;
        ++failures;
    }
}

// ---------------------------------------------------------------------------
// 存档系四种指令（SAVEDATA/LOADDATA/DELDATA/CHKDATA）的「调用形态」：
//   `CHKDATA L_I` 整行会被归约为 Function(CHKDATA, Variable(L_I))（语句形式的
//   内部函数，见 executeFunctionCall），line.arguments 只有一个「整条调用」。
//   此前 handleChkData 直接把它当实参求值 → 变成 CHKDATA(CHKDATA(L_I))，
//   实参恒为「0/1 的存在判定」：eraH 的读档界面把每个槽位都报成「存在」，
//   空档位也显示 [N] ＯＫ，选中去 LOADDATA 才失败。
// ---------------------------------------------------------------------------
static void chkDataCallForm() {
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

    // 只准备 7 号档：7 号应「存在」(RESULT=0)，0/1 号应「不存在」(RESULT=1)
    QTemporaryDir gameDir;
    engine.setGameDataDir(gameDir.path());
    if (!engine.writeSaveSnapshot(7, QStringLiteral("chkdata 调用形态"))) {
        qCritical() << "CHKDATA call form cannot prepare slot 7";
        ++failures;
        return;
    }

    const QStringList script = {
        "@MAIN", "L_I = 7", "CHKDATA L_I", "CHECK += RESULT * 10",
        "L_I = 0", "CHKDATA L_I", "CHECK += RESULT * 100",
        "L_I = 1", "CHKDATA L_I", "CHECK += RESULT * 1000"};
    QList<LogicalLine> lines;
    for (int i = 0; i < script.size(); ++i)
        lines.append(AstBuilder::build(script[i], ScriptPosition("chkdata.ERB", i, 0),
            [&table](const QString& text) { return table.expressionAst(text); }));
    table.loadScript("chkdata", lines);
    table.markLoadComplete();
    table.setEntryPoint("MAIN");
    const auto result = runner.runToCompletion();
    const qint64 actual = storage.getGlobalInt1DInCurrentFrame("CHECK", 0);
    // 7 号存在 -> RESULT=0（+0）；0 号缺失 -> RESULT=1（+100）；1 号缺失 -> +1000
    if (result != ExecState::Halt || actual != 1100) {
        qCritical() << "CHKDATA call form failed" << int(result) << actual;
        ++failures;
    } else qInfo() << "PASS CHKDATA call form uses the slot value, not CHKDATA(CHKDATA())";
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
        // 普通运行期写入（`html '= LOCALS`）的值：装载期无法为其构筑标签，
        // 但 C# PrintHtml 本就是**打印时**从最终字符串解析（Html2DisplayLine）——
        // 现与 C# 对齐：coversText 失败 -> parseRuntimeMarkup 补解析，标签生效。
        //（此前是移植特有的「按文本输出」行为。）
        QStringList opaque; for (const auto& part : printed[2].parts)
            if (part.kind == PrintTemplatePart::Kind::Button) opaque.append(part.buttonValue);
            else if (part.kind == PrintTemplatePart::Kind::Text) opaque.append(part.text);
        good &= opaque.size() == 2 && opaque.at(0) == "99" && opaque.at(1) == "opaque";
    }
    if (!good) { qCritical() << "HTML function/accumulation provenance failed" << int(result) << printed.size(); ++failures; }
    else qInfo() << "PASS HTML function/accumulation provenance";
    const auto tracked = evaluator.evaluate(*ast, &storage);
    const auto hasButton = [](const FlatStringValue& value) {
        return std::any_of(value.markup.parts.cbegin(), value.markup.parts.cend(),
            [](const PrintTemplatePart& part) { return part.kind == PrintTemplatePart::Kind::Button; });
    };
    storage.setStringSlotInCurrentFrame("RESULTS", {0}, flatString(tracked));
    good = hasButton(storage.getStringSlotInCurrentFrame("RESULTS", {0}));
    storage.setGlobalStr1DInCurrentFrame("RESULTS", 0, stringText(tracked));
    good &= !hasButton(storage.getStringSlotInCurrentFrame("RESULTS", {0}))
        && storage.getStringSlotInCurrentFrame("RESULTS", {0}).text == stringText(tracked);
    // RESETGLOBAL clears user globals; built-in RESULTS is preserved.
    storage.setStringSlotInCurrentFrame("HTML_GLOBAL", {0}, flatString(tracked));
    storage.resetGlobals();
    good &= storage.getStringSlotInCurrentFrame("HTML_GLOBAL", {0}).text.isEmpty()
        && !hasButton(storage.getStringSlotInCurrentFrame("HTML_GLOBAL", {0}));
    if (!good) { qCritical() << "HTML provenance invalidation failed"; ++failures; }
}

// ---------------------------------------------------------------------------
// EM 私family「变量名字符串化」（GETVAR / GETVARS / SETVAR / EXISTVAR）与
// REGEXPMATCH：**全部住扩展侧**（ExtensionRegistry::regExpr，见 GameProc/fork_em.cpp），
// 因此端到端测试必须像引擎那样把三项服务接上：变量名解析 / 变量属性 / 变量写回
// （见 extension_registry.h 的 Services 说明）。
// 每个断言单独占一个 CHECK 槽（**不叠加**），失败时能一眼看出是哪一项。
// ---------------------------------------------------------------------------
static void emExtensionFunctions() {
    VariableStorage storage;
    ProcessState state;
    EraParseTable table(&state);
    ExecutionEngine engine(&storage, nullptr);
    ExpressionEvaluator evaluator;
    engine.setParseTable(&table);
    engine.setExpressionEvaluator(&evaluator);
    table.setExpressionEvaluator(&evaluator);
    table.setVariableStorage(&storage);
    // 扩展式中函数的求值回调（引擎在装配期接线）
    evaluator.setExtensionFunctionInvoker(
        [&engine](const QString& name, const QList<QVariant>& args,
                  const QList<const ExpressionNode*>& nodes, QVariant& out) {
            return engine.extensions().runExpression(name, args, nodes, out);
        });
    // GETVAR/GETVARS/SETVAR/EXISTVAR 的三项服务
    engine.extensions().setVariableNameServices(
        [&table](const QString& text) { return table.runtimeVariableReference(text); },
        [&table](const QString& name) { return table.variableFlags(name); },
        [&evaluator, &storage](const ExpressionNode& target, const QVariant& value) {
            return evaluator.assignNodeValue(target, &storage, nullptr, value);
        });
    ScriptRunner runner(&table, &engine, &state, &storage);
    runner.setExpressionEvaluator(&evaluator);
    runner.setStepLimit(1000);

    const QStringList source = {"@MAIN",
        "#DIM CONST CONSTANT = 3",
        "#DIM COUNT, 4",
        "#DIMS TEXT",
        "COUNT:1 = 7",
        "TEXT '= \"ok\"",
        "CHECK:0 = EXISTVAR(\"CONSTANT\")",     // 常量 -> 1|4 = 5
        "CHECK:1 = EXISTVAR(\"COUNT\")",        // 可写变量 -> 1
        "CHECK:2 = EXISTVAR(\"TEXT\")",         // 字符串变量 -> 2
        "CHECK:3 = EXISTVAR(\"NOSUCHVAR\")",    // 不存在 -> 0
        "CHECK:4 = GETVAR(\"CONSTANT\")",       // 常量按常数表取值 -> 3
        "CHECK:5 = GETVAR(\"COUNT:1\")",        // 带下标的可写变量 -> 7
        "CHECK:6 = STRLENS(GETVARS(\"TEXT\"))", // 字符串变量 -> 2
        "COUNT:2 = 0",
        "CHECK:7 = SETVAR(\"COUNT:2\", 9)",     // 成功返回 1
        "CHECK:8 = COUNT:2",                    // 真的写进去了 -> 9
        "CHECK:9 = REGEXPMATCH(\"a1 b2 c3\", \"[a-z][0-9]\")",                 // 3 个匹配
        "CHECK:10 = REGEXPMATCH(\"2026-10-07\", \"([0-9]{4})-([0-9]{2})\", 1)", // 1 个匹配
        "CHECK:11 = RESULT:1",                  // 形式 1 的分组个数（0 组 + 2 组 = 3）
        "CHECK:12 = STRLENS(RESULTS:0)",        // RESULTS:0 = 整个匹配 "2026-10"（模式只吃到月）
        // 扩展语句的实参必须是**逗号分隔的表达式项**（C# METHOD_Instruction ->
        // ReduceArguments）：`V + 1` 这类带空白的实参此前被 Raw 按空白切碎，
        // DT_CELL_SET 收到的实参整个错位（FL3.8 的 DT_CELL_SET 全是这种写法）。
        "DT_CREATE \"T\"",
        "DT_COLUMN_ADD \"T\", \"c\", \"int32\"",   // 列类型名须为 int8/16/32/64/string
        "DT_ROW_ADD \"T\"",
        "DT_CELL_SET \"T\", 0, \"c\", 7 + 1",   // 第 5 参是 asId（=1 会按 id 找行）
        "CHECK:13 = DT_CELL_GET(\"T\", 0, \"c\")",
        // REGEXPMATCH 的**语句形态**：返回值按 C# METHOD_Instruction 写 RESULT
        "REGEXPMATCH \"a1 b2\", \"[a-z][0-9]\"",
        "CHECK:14 = RESULT",
        "RETURN"};
    QList<LogicalLine> lines;
    for (int i = 0; i < source.size(); ++i)
        lines.append(AstBuilder::build(source[i], ScriptPosition("em.ERB", i, 0),
            [&table](const QString& text) { return table.expressionAst(text); }));
    table.loadScript("em", lines);
    table.markLoadComplete();
    table.setEntryPoint("MAIN");
    runner.runToCompletion();
    // RESULTS:0 = 整个匹配 "2026-10"（7 字符：模式只覆盖到月）
    const QList<qint64> expect = {5, 1, 2, 0, 3, 7, 2, 1, 9, 3, 1, 3, 7, 8, 2};
    bool ok = true;
    for (int i = 0; i < expect.size(); ++i) {
        const qint64 actual = storage.getGlobalInt1DInCurrentFrame("CHECK", i);
        if (actual != expect.at(i)) {
            qCritical() << QString("EM 扩展式中函数 CHECK:%1").arg(i) << "actual" << actual
                        << "expected" << expect.at(i);
            ok = false;
        }
    }
    if (ok) qInfo() << "PASS EM 的 GETVAR/GETVARS/SETVAR/EXISTVAR/REGEXPMATCH（扩展侧实现）";
    else ++failures;
}

// LOADTEXT / SAVETEXT 的**路径形态**（EM 私家版拡張）与 EXISTFILE、HTML_STRING*：
// 路径相对**游戏目录**解析（Utils.GetValidPath：'/'->'\\'、删 "..\\"、绝对路径拒绝），
// 扩展名须在 Config.ValidExtension 内（默认 txt；LOADTEXT 超出 -> ""，
// SAVETEXT 超出 -> 强制改 .txt）。
static void scriptPathFunctions() {
    QTemporaryDir tmp;
    if (!tmp.isValid()) { qCritical() << "无法创建临时目录"; ++failures; return; }
    QDir().mkpath(tmp.path() + "/xml");
    {
        QFile f(tmp.path() + "/xml/data.xml");
        if (!f.open(QIODevice::WriteOnly)) { qCritical() << "无法写出测试文件"; ++failures; return; }
        // BOM + "<root>a</root>" + CRLF：LOADTEXT 保留 BOM（不判编码），删掉 '\r'
        f.write("\xEF\xBB\xBF<root>a</root>\r\n");
    }
    VariableStorage storage;
    ProcessState state;
    EraParseTable table(&state);
    ExecutionEngine engine(&storage, nullptr);
    ExpressionEvaluator evaluator;
    engine.setParseTable(&table);
    engine.setExpressionEvaluator(&evaluator);
    table.setExpressionEvaluator(&evaluator);
    table.setVariableStorage(&storage);
    evaluator.setExtensionFunctionInvoker(
        [&engine](const QString& name, const QList<QVariant>& args,
                  const QList<const ExpressionNode*>& nodes, QVariant& out) {
            return engine.extensions().runExpression(name, args, nodes, out);
        });
    // 引擎装配期注入：基准目录（= 游戏目录）+ 扩展名白名单（Config.ValidExtension）
    evaluator.setBaseDirectory(tmp.path());
    evaluator.setValidExtensionsProvider([] { return QStringList{QStringLiteral("txt"), QStringLiteral("xml")}; });
    engine.extensions().setForkServices(
        [] { return QStringList(); }, [] { return QStringList(); }, [] { return QStringList(); },
        [&tmp] { return tmp.path(); });
    engine.extensions().setFontSizeProvider([] { return 14; });
    ScriptRunner runner(&table, &engine, &state, &storage);
    runner.setExpressionEvaluator(&evaluator);
    runner.setStepLimit(1000);

    const QStringList source = {"@MAIN",
        "#DIMS DOC",
        "DOC '= LOADTEXT(\"xml/data.xml\")",
        "CHECK:0 = STRLENS(DOC)",                                       // BOM + 14 字符（'\r' 已删）
        "CHECK:1 = STRLENS(LOADTEXT(\"xml/data.bin\"))",                // 白名单外 -> ""
        "CHECK:2 = STRLENS(LOADTEXT(\"/etc/hostname\"))",               // 绝对路径 -> ""
        "CHECK:3 = STRLENS(LOADTEXT(\"../xml/data.xml\"))",             // "..\" 被删 -> 仍读得到
        "CHECK:4 = EXISTFILE(\"xml/data.xml\")",                        // 文件 -> 1
        "CHECK:5 = EXISTFILE(\"xml\")",                                 // **目录 -> 0**（File.Exists 语义）
        "CHECK:6 = EXISTFILE(\"no_such.txt\")",                         // 不存在 -> 0
        "CHECK:7 = SAVETEXT(\"hello\", \"xml/out.txt\")",               // 写盘成功 -> 1
        "DOC '= LOADTEXT(\"xml/out.txt\")",
        "CHECK:8 = STRLENS(DOC)",                                       // round-trip -> 5
        "CHECK:9 = SAVETEXT(\"x\", \"xml/other.bin\")",                 // 扩展名不合法 -> 改 .txt 后仍成功
        "CHECK:10 = HTML_STRINGLEN(\"AB<b>CD</b>\", 0)",                // 标签零宽 -> 4
        "CHECK:11 = HTML_STRINGLEN(\"あい\", 0)",                        // 全角 2 个 -> 4
        "CHECK:12 = HTML_STRINGLINES(\"ABCDE\", 2)",                    // 宽 2 -> 3 行
        "CHECK:13 = HTML_STRINGLINES(\"A<br>B\", 50)",                  // <br> 也断行 -> 2
        // **语句形态**：C# METHOD_Instruction 把返回值写 RESULTS（字符串）/ RESULT（整型）
        // —— FL3.8 的 `LOADTEXT \"XML/SKILL_LIFE.xml\"` + `XML_DOCUMENT ID, RESULTS`
        // 就是靠这一步把读到的 XML 交给 XML_DOCUMENT 的。
        "LOADTEXT \"xml/data.xml\"",
        "CHECK:14 = STRLENS(RESULTS)",
        "SAVETEXT \"xyz\", \"xml/stmt.txt\"",
        "CHECK:15 = RESULT",
        "RETURN"};
    QList<LogicalLine> lines;
    for (int i = 0; i < source.size(); ++i)
        lines.append(AstBuilder::build(source[i], ScriptPosition("path.ERB", i, 0),
            [&table](const QString& text) { return table.expressionAst(text); }));
    table.loadScript("path", lines);
    table.markLoadComplete();
    table.setEntryPoint("MAIN");
    runner.runToCompletion();
    const QList<qint64> expect = {15, 0, 0, 15, 1, 0, 0, 1, 5, 1, 4, 4, 3, 2, 15, 1};
    bool ok = true;
    for (int i = 0; i < expect.size(); ++i) {
        const qint64 actual = storage.getGlobalInt1DInCurrentFrame("CHECK", i);
        if (actual != expect.at(i)) {
            qCritical() << QString("脚本路径/HTML 函数 CHECK:%1").arg(i) << "actual" << actual
                        << "expected" << expect.at(i);
            ok = false;
        }
    }
    // SAVETEXT 的「扩展名不合法 -> 改 .txt」也要验证落在盘上
    ok = ok && QFile::exists(tmp.path() + "/xml/other.txt")
            && !QFile::exists(tmp.path() + "/xml/other.bin");
    if (ok) qInfo() << "PASS LOADTEXT/SAVETEXT 路径形态 + EXISTFILE + HTML_STRING*";
    else ++failures;
}




int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    chkDataCallForm();
    htmlFlow();
    tryListLoadTime();
    constArrayIntrospection();
    globalConstShadowedByPrivate();
    emExtensionFunctions();
    scriptPathFunctions();
    // 私有 `#DIM CONST` 只属于声明它的函数：别处的同名私有变量必须仍然读到自己的值。
    // 此前常数表是扁平的 name→value 表、运行期求值按名字命中，`SHADOW` 会读成 51。
    run("私有的 #DIM CONST 不泄漏到别的函数的同名私有变量", {
        "CALL USERFN", "RETURN",
        "@HOLDER", "#DIM CONST SHADOW = 51", "RETURN",
        "@USERFN", "#DIM SHADOW", "SHADOW = 7", "SHADOW ++", "CHECK = SHADOW", "RETURN"}, 8);
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
    // 「以指令前缀开头的变量名」自增：C# 命令判定是**精确查表**（GetFunctionIdentifier
    // 查不到即赋值行，ReadAssignmentOperator 把 `X ++` 归约为 Increment）。
    // 此前前缀启发式把 `PRINT_SKILL_NUM ++` 当成 PRINT 族指令，整行失效
    //（FL3.8 的技能计数正是这么写的）。
    run("PRINT 前缀变量名的自增语句", {"#DIM PRINT_SKILL_NUM", "PRINT_SKILL_NUM = 4",
        "PRINT_SKILL_NUM ++", "PRINT_CNT = 1", "PRINT_CNT ++", "PRINT_CNT ++",
        "CHECK = PRINT_SKILL_NUM * 10 + PRINT_CNT"}, 53);   // 5*10 + 3
    // CALL 族（含 TRYCCALLFORM）传**整数组**给 REF 形参：2 维数组的裸名不该被
    // requireIndices 误报「缺少参数或下标维数错误」（C# ConvertArg 对 REF 形参
    // 收的就是整数组变量；FL3.8 _FUNCTION_WEAPON.ERB:106 正是这种写法）。
    run("TRYCCALLFORM 把 2 维整数组传给 REF 形参", {
        "#DIM DYNAMIC WPN_MATERIAL_NEEDS, 2, 3",
        "WPN_MATERIAL_NEEDS:1:2 = 7",
        "TRYCCALLFORM GET_WEAPON_MATERIAL_{1}(WPN_MATERIAL_NEEDS)",
        "CATCH", "ENDCATCH", "RETURN",
        "@GET_WEAPON_MATERIAL_1(NEEDS)", "#DIM REF NEEDS, 0, 0",
        "CHECK = NEEDS:1:2", "RETURN"}, 7);
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
    // 形参名必须按**段身份**的形参名表判定（paramNames 的唯一键是段身份文本）：
    // 与同名私有声明共存时，定型仍须把形参判成 Param（运行期读 m_parameters），
    // 不能被收尾重解析按「函数名 -> 段键」查空后退化成私有数组的全局读。
    run("CALL 形参遮蔽同名私有变量", {
        "CALL SUB(5)",
        "RETURN",
        "@SUB(NUM)",
        "#DIM NUM, 3",
        "CHECK = NUM",
        "RETURN"}, 5);
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
    // 逗号形式 CALL 的前导省略实参：`CALL F, , 7` 是「第 1 实参省略、第 2 实参 = 7」，
    // 不能把首个逗号之后的空位吃掉导致实参整体左移（对齐 C# ReduceArguments 的 null 占位）。
    run("CALL 逗号形式 前导省略实参不左移", {
        "CALL F, , 7",
        "CHECK = RESULT",
        "RETURN",
        "@F(A, B)", "#DIM A", "#DIM B", "A = 100", "RETURN B"}, 7);
    run("CALL 逗号形式 两个前导省略实参", {
        "CALL F, , , 7",
        "CHECK = RESULT",
        "RETURN",
        "@F(A, B, C)", "#DIM A", "#DIM B", "#DIM C", "A = 1", "B = 2", "RETURN C"}, 7);
    // VARI / VARS：EM（emuera.net）的**语句位置私有变量声明**
    //（FL3.8 的 setting.json 把 UseScopedVariableInstruction 设为 true）。
    // 声明走 #DIM/#DIMS 同一条登记路径，初值在运行期写到元素 0。
    run("VARS 声明并写初值（标量）", {
        "VARS TITLE = \"hello\"",
        "CHECK = STRLENS(TITLE)",
        "RETURN"}, 5);
    run("VARI 声明并写初值（标量）", {
        "VARI COUNT = 7",
        "CHECK = COUNT",
        "RETURN"}, 7);
    run("VARI 无初值 -> 0，声明本身生效", {
        "VARI X",
        "CHECK = X + 1",
        "RETURN"}, 1);
    run("VARI 数组形式（不赋初值）", {
        "VARI BUF, 3",
        "BUF:1 = 9",
        "CHECK = BUF:1 * 10 + BUF:0 + BUF:2",
        "RETURN"}, 90);
    run("VARS 在别的函数内声明（CALL 进入后声明生效）", {
        "CALL F",
        "CHECK = RESULT",
        "RETURN",
        "@F", "VARS T = \"xyz\"", "RETURN STRLENS(T)"}, 3);
    // `=` 的字符串赋值取**原始文本**（引号算在内），而 VARS 的初值取引号**之间**
    // 的内容 —— 两者不同，正是 C# VARS_Instruction 的语义（不走 FORM 解析）。
    run("VARS 初值去引号，而 = 保留原始文本", {
        "VARS A = \"abc\"",
        "VARS B",
        "B = \"abc\"",
        "CHECK = STRLENS(A) * 10 + STRLENS(B)",
        "RETURN"}, 35);
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
