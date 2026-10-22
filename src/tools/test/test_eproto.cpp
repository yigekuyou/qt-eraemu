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
// eproto 的单元测试：分帧、eraemu AST 分析器、引擎登记表、LSP/MCP 协议核心。
#include <QBuffer>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QtTest>

#include "erb_analyzer.h"
#include "json_rpc.h"
#include "lsp_server.h"
#include "mcp_server.h"

// 引擎侧（同一份夹具的对照物）：段身份的唯一构造点 + 解析表/变量表。
#include "ast/ast_builder.h"
#include "ast/logical_line.h"
#include "ast/name_interner.h"
#include "ast/operand_type.h"
#include "ast/variable_table.h"
#include "era_parse_table.h"
#include "process_state.h"

using namespace eproto;

namespace {

QJsonObject makeMsg(const QString& method, const QJsonValue& id, const QJsonObject& params) {
    QJsonObject o;
    o.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    o.insert(QStringLiteral("method"), method);
    if (!id.isNull()) o.insert(QStringLiteral("id"), id);
    if (!params.isEmpty()) o.insert(QStringLiteral("params"), params);
    return o;
}

QList<QJsonObject> driveLsp(LspServer& server, const QJsonObject& request) {
    QBuffer buf;
    buf.open(QIODevice::ReadWrite);
    writeLspMessage(buf, request);
    buf.seek(0);
    Message msg;
    QString err;
    if (!readLspMessage(buf, msg, &err)) return {};
    return server.handle(msg);
}

QJsonObject driveMcp(McpServer& server, const QJsonObject& request) {
    QBuffer buf;
    buf.open(QIODevice::ReadWrite);
    writeMcpMessage(buf, request);
    buf.seek(0);
    Message msg;
    QString err;
    if (!readMcpMessage(buf, msg, &err)) return {};
    const QList<QJsonObject> out = server.handle(msg);
    return out.isEmpty() ? QJsonObject{} : out.first();
}

QJsonObject textDocument(const QString& uri, const QString& text, int version) {
    QJsonObject td;
    td.insert(QStringLiteral("uri"), uri);
    td.insert(QStringLiteral("languageId"), QStringLiteral("erb"));
    td.insert(QStringLiteral("version"), version);
    td.insert(QStringLiteral("text"), text);
    return td;
}

QJsonObject posParams(const QString& uri, int line, int character) {
    QJsonObject p;
    p.insert(QStringLiteral("textDocument"),
             QJsonObject{{QStringLiteral("uri"), uri}});
    p.insert(QStringLiteral("position"),
             QJsonObject{{QStringLiteral("line"), line}, {QStringLiteral("character"), character}});
    return p;
}

void initializeLsp(LspServer& server) {
    driveLsp(server, makeMsg(QStringLiteral("initialize"), 100,
                           QJsonObject{{"capabilities", QJsonObject{}}}));
    driveLsp(server, makeMsg(QStringLiteral("initialized"), QJsonValue(), {}));
}

void initializeMcp(McpServer& server) {
    driveMcp(server, makeMsg(QStringLiteral("initialize"), 100,
        QJsonObject{{"protocolVersion", "2025-11-25"}, {"capabilities", QJsonObject{}},
                    {"clientInfo", QJsonObject{{"name", "test"}, {"version", "1"}}}}));
    driveMcp(server, makeMsg(QStringLiteral("notifications/initialized"), QJsonValue(), {}));
}

QString toolText(const QJsonObject& response) {
    return response.value(QStringLiteral("result")).toObject()
        .value(QStringLiteral("content")).toArray().at(0).toObject()
        .value(QStringLiteral("text")).toString();
}

}  // namespace

class TestEproto : public QObject {
    Q_OBJECT

private slots:
    void lspFramingRoundTrip();
    void mcpFramingRoundTrip();
    void engineTablesExposed();
    void analyzerFlagsUnknownInstruction();
    void analyzerReportsIndentedInstructionColumn();
    void analyzerNoFalsePositiveOnCommonCode();
    void analyzerExtractsLabels();
    void analyzerPrivateScopeUsesSegmentIdentity();
    void lspInitializeCapabilities();
    void lspDidOpenPublishesDiagnostics();
    void lspCompletionAndHover();
    void mcpToolsList();
    void mcpCallLookupAndValidate();
    void mcpStats();
};

void TestEproto::lspFramingRoundTrip() {
    QBuffer buf;
    QVERIFY(buf.open(QIODevice::ReadWrite));
    writeLspMessage(buf, makeMsg(QStringLiteral("initialize"), 7, QJsonObject{}));
    QVERIFY(buf.data().startsWith("Content-Length: "));
    QVERIFY(buf.data().contains("\r\n\r\n"));
    buf.seek(0);

    Message msg;
    QString err;
    QVERIFY2(readLspMessage(buf, msg, &err), qPrintable(err));
    QCOMPARE(msg.method, QStringLiteral("initialize"));
    QVERIFY(msg.hasId);
    QCOMPARE(msg.id.toInt(), 7);
    QVERIFY(msg.isRequest());
}

void TestEproto::mcpFramingRoundTrip() {
    QBuffer buf;
    QVERIFY(buf.open(QIODevice::ReadWrite));
    writeMcpMessage(buf, makeMsg(QStringLiteral("ping"), 1, QJsonObject{}));
    QVERIFY(!buf.data().contains("Content-Length"));
    QVERIFY(buf.data().endsWith('\n'));
    buf.seek(0);

    Message msg;
    QString err;
    QVERIFY2(readMcpMessage(buf, msg, &err), qPrintable(err));
    QCOMPARE(msg.method, QStringLiteral("ping"));
    QCOMPARE(msg.id.toInt(), 1);
}

void TestEproto::engineTablesExposed() {
    // 语义来自引擎自己的 constexpr 表。
    const QStringList inst = ErbAnalyzer::instructionNames();
    const QStringList func = ErbAnalyzer::functionNames();
    QVERIFY(inst.size() > 50);
    QVERIFY(func.size() > 100);
    QVERIFY(inst.contains(QStringLiteral("IF")));
    QVERIFY(inst.contains(QStringLiteral("CALL")));
    QVERIFY(func.contains(QStringLiteral("MAX")));
    QVERIFY(!ErbAnalyzer::instructionSpec(QStringLiteral("CALL")).isEmpty());
    QVERIFY(!ErbAnalyzer::functionSpec(QStringLiteral("MAX")).isEmpty());
    QVERIFY(ErbAnalyzer::instructionSpec(QStringLiteral("NOT_A_REAL_INSTRUCTION")).isEmpty());
}

void TestEproto::analyzerFlagsUnknownInstruction() {
    ErbAnalyzer analyzer;
    const ErbDocument doc = analyzer.parse(QStringLiteral("PRINT hello\nFOOBARBAZ 1\n"),
                                           QStringLiteral("t.erb"));
    QVERIFY(doc.parsed);
    QCOMPARE(doc.lineCount, 2);
    QCOMPARE(int(doc.attributed.size()), 1);
    QCOMPARE(doc.attributed.at(0).code, QStringLiteral("unknown-instruction"));
    QCOMPARE(doc.attributed.at(0).line, 1);       // 0 起 -> 第 2 行
    QCOMPARE(doc.attributed.at(0).severity, QStringLiteral("warning"));
}

void TestEproto::analyzerReportsIndentedInstructionColumn() {
    // AstBuilder 现在带结构化定位（真实列 + 跨度）—— 缩进行也要给出正确列号，
    // 这正是 LSP 高亮 / MCP 工具输出所需的。
    ErbAnalyzer analyzer;
    const ErbDocument doc =
        analyzer.parse(QStringLiteral("    FOOBARBAZ 1\n"), QStringLiteral("t.erb"));
    QCOMPARE(int(doc.attributed.size()), 1);
    QCOMPARE(doc.attributed.at(0).line, 0);
    QCOMPARE(doc.attributed.at(0).startCol, 4);
    QCOMPARE(doc.attributed.at(0).endCol, 13);  // 4 + len("FOOBARBAZ") = 4 + 9
}

void TestEproto::analyzerNoFalsePositiveOnCommonCode() {    ErbAnalyzer analyzer;
    const QString src = QStringLiteral(
        "PRINT hello\n"
        "X = 1\n"
        "IF X == 1\n"
        "  PRINTFORM ok\n"
        "ENDIF\n"
        "; a comment\n"
        "#DIM Y\n");
    const ErbDocument doc = analyzer.parse(src, QStringLiteral("t.erb"));
    QCOMPARE(int(doc.attributed.size()), 0);
}

void TestEproto::analyzerExtractsLabels() {
    ErbAnalyzer analyzer;
    const QByteArray src =
        "PRINT hello\n"
        "@MYFUNC\n"
        "RETURN\n"
        "$MYGOTO\n"
        "PRINT done\n";
    const ErbDocument doc = analyzer.parse(QString::fromUtf8(src), QStringLiteral("t.erb"));
    const QList<ErbSymbol> syms = doc.symbols;
    QCOMPARE(int(syms.size()), 2);
    QCOMPARE(syms.at(0).name, QStringLiteral("MYFUNC"));
    QVERIFY(!syms.at(0).isGotoLabel);
    QCOMPARE(syms.at(1).name, QStringLiteral("MYGOTO"));
    QVERIFY(syms.at(1).isGotoLabel);
}

// ---------------------------------------------------------------------------
// 私有变量作用域 = **段身份 (脚本, 段起点行)**，不是函数名。
// 分析器（LSP/MCP 的语义来源）与 EraParseTable（引擎）是同一事实的两侧副本：
// 同名 @F 的两段各自一份 #DIM，任何一侧退回「函数名当作用域」都会让两份声明
// 互相冒充。本夹具同时锁住：
//   1) 分析器给 @标签 的 segmentKey = eravar::segmentKeyText(文档标识, 段起点行)
//      —— 同名两段不同、可还原成 (脚本, 段起点行)；
//   2) 引擎侧逐行 scopeKey / segmentStart 与之一致，且类型/维度按段各一份；
//   3) 分析器的越界诊断落在正确的段（段 A 的 ARR 尺寸 3 越界、段 B 的 10 不越界）；
//   4) 文件头段（段起点行 = -1，.ERH 级别）的声明是广域，不进任何函数段。
// ---------------------------------------------------------------------------
void TestEproto::analyzerPrivateScopeUsesSegmentIdentity() {
    const QString script = QStringLiteral("ERB/scope.erb");
    const QString header = QStringLiteral("ERB/scope.erh");
    const QStringList source = {
        QStringLiteral("@F"),            // 0 <- 段 A 起点
        QStringLiteral("#DIM ARR, 3"),   // 1
        QStringLiteral("#DIM X"),        // 2  段 A：Int
        QStringLiteral("PRINTV ARR:5"),  // 3  越界（段 A 的 ARR 尺寸 3）
        QStringLiteral("@F"),            // 4 <- 段 B 起点（同名函数）
        QStringLiteral("#DIMS X"),       // 5  段 B：Str（同名、不同段、不同类型）
        QStringLiteral("#DIM ARR, 10"),  // 6
        QStringLiteral("PRINTV ARR:5"),  // 7  不越界（段 B 的 ARR 尺寸 10）
    };
    const QString body = source.join(QLatin1Char('\n')) + QLatin1Char('\n');
    const QString headerBody = QStringLiteral("#DIM HDRV, 3\n");   // 头文件声明 = 广域

    // ---- 分析器侧 ----
    ErbAnalyzer analyzer;
    const QMap<QString, ErbDocument> docs =
        analyzer.analyzeWorkspace({{script, body}, {header, headerBody}});
    const ErbDocument doc = docs.value(script);
    QVERIFY(doc.parsed);
    QCOMPARE(int(doc.symbols.size()), 2);
    const ErbSymbol segA = doc.symbols.at(0);
    const ErbSymbol segB = doc.symbols.at(1);
    QCOMPARE(segA.name, QStringLiteral("F"));
    QCOMPARE(segB.name, QStringLiteral("F"));   // 函数名事实：同名（保持不变）
    QCOMPARE(segA.line, 0);
    QCOMPARE(segB.line, 4);
    // 段身份：两段必须不同（旧实现两个都是 "F"）；且 = 唯一构造点算出来的文本。
    QVERIFY(segA.segmentKey != segB.segmentKey);
    QCOMPARE(segA.segmentKey, eravar::segmentKeyText(script, 0));
    QCOMPARE(segB.segmentKey, eravar::segmentKeyText(script, 4));
    QString keyScript;
    int keyLine = -1;
    QVERIFY(eravar::parseSegmentKey(segA.segmentKey, keyScript, keyLine));
    QCOMPARE(keyScript, script);
    QCOMPARE(keyLine, 0);
    QVERIFY(eravar::parseSegmentKey(segB.segmentKey, keyScript, keyLine));
    QCOMPARE(keyScript, script);
    QCOMPARE(keyLine, 4);
    // 段身份不可能与变量名撞车（恒含 ':'）—— 旧实现的 "F" 可被当成变量名。
    QVERIFY(segA.segmentKey.contains(QLatin1Char(':')));
    // 分析器诊断：只有段 A 的 ARR 越界（段 B 的 ARR 尺寸 10 不该报）。
    int oob = 0;
    for (const ErbDiagnostic& d : doc.attributed) {
        if (!d.message.contains(QStringLiteral("越界"))) continue;
        QCOMPARE(d.line, 3);
        ++oob;
    }
    QCOMPARE(oob, 1);

    // ---- 引擎侧（同一份文本、同一个脚本标识）----
    ProcessState state;
    EraParseTable table(&state);
    const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
    const auto buildLines = [&resolve](const QString& file, const QStringList& lines) {
        QList<LogicalLine> out;
        for (int i = 0; i < lines.size(); ++i)
            out.append(AstBuilder::build(lines.at(i), ScriptPosition(file, i + 1, 1), resolve));
        return out;
    };
    QVERIFY(table.loadScript(header, buildLines(header, {QStringLiteral("#DIM HDRV, 3")}),
                             /*isHeaderFile=*/true));
    QVERIFY(table.loadScript(script, buildLines(script, source)));
    table.markLoadComplete();

    const ScriptData* engine = table.script(script);
    QVERIFY(engine != nullptr);
    QVERIFY(engine->lines.size() == source.size());
    // 引擎逐行作用域 = 分析器给符号的段身份（同一构造点、同一份事实）。
    QCOMPARE(engine->lines.at(0).scopeKey, segA.segmentKey);
    QCOMPARE(engine->lines.at(0).segmentStart, 0);
    QCOMPARE(engine->lines.at(4).scopeKey, segB.segmentKey);
    QCOMPARE(engine->lines.at(4).segmentStart, 4);
    QCOMPARE(engine->lines.at(7).scopeKey, segB.segmentKey);
    // 同名 @F 的 #DIM 互不冒充：类型/维度按段各一份，正好解释上面的越界诊断。
    const VariableTable& vt = table.variableTable();
    QVERIFY(vt.typeOf(QStringLiteral("X"), segA.segmentKey) == OperandType::Int);
    QVERIFY(vt.typeOf(QStringLiteral("X"), segB.segmentKey) == OperandType::Str);
    const QList<VariableDecl> localsA = vt.localsOf(segA.segmentKey);
    const QList<VariableDecl> localsB = vt.localsOf(segB.segmentKey);
    const auto arrLengths = [](const QList<VariableDecl>& ds) {
        for (const VariableDecl& d : ds)
            if (d.name.compare(QStringLiteral("ARR"), Qt::CaseInsensitive) == 0)
                return d.lengths;
        return QList<int>{};
    };
    QCOMPARE(arrLengths(localsA), QList<int>{3});
    QCOMPARE(arrLengths(localsB), QList<int>{10});

    // ---- 文件头段（段起点行 = -1，.ERH 级别）----
    const QString headerKey = eravar::segmentKeyText(header, -1);
    // 头文件身份独立存在（不是空串 = 广域上下文），但声明是广域、不归属任何段。
    QVERIFY(!headerKey.isEmpty());
    QVERIFY(vt.localsOf(headerKey).isEmpty());
    const auto ownsHeaderVar = [&vt](const QString& key) {
        for (const VariableDecl& d : vt.localsOf(key))
            if (d.name.compare(QStringLiteral("HDRV"), Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    QVERIFY(!ownsHeaderVar(headerKey));
    QVERIFY(!ownsHeaderVar(segA.segmentKey));   // 文件头段声明不进任何函数段
    QVERIFY(!ownsHeaderVar(segB.segmentKey));
    // 但作为广域声明，任何段都能查到（回退到全局，不是私有）。
    QVERIFY(vt.typeOf(QStringLiteral("HDRV"), segA.segmentKey) == OperandType::Int);
    QVERIFY(vt.typeOf(QStringLiteral("HDRV"), segB.segmentKey) == OperandType::Int);
    // 分析器侧：.ERH 文档没有函数符号，因此没有任何段认领头文件声明。
    const ErbDocument hdrDoc = docs.value(header);
    QVERIFY(hdrDoc.parsed);
    QCOMPARE(int(hdrDoc.symbols.size()), 0);
    for (const ErbSymbol& s : doc.symbols)
        QVERIFY(s.segmentKey != headerKey);
}

void TestEproto::lspInitializeCapabilities() {
    LspServer server;
    const QList<QJsonObject> out = driveLsp(server, makeMsg(QStringLiteral("initialize"), 1, QJsonObject{}));
    QCOMPARE(int(out.size()), 1);
    const QJsonObject caps = out.first().value(QStringLiteral("result")).toObject()
                                 .value(QStringLiteral("capabilities")).toObject();
    QCOMPARE(caps.value(QStringLiteral("hoverProvider")).toBool(), true);
    QCOMPARE(caps.value(QStringLiteral("definitionProvider")).toBool(), true);
    QVERIFY(server.initialized());
}

void TestEproto::lspDidOpenPublishesDiagnostics() {
    LspServer server;
    initializeLsp(server);
    QJsonObject params;
    params.insert(QStringLiteral("textDocument"),
                  textDocument(QStringLiteral("file:///tmp/era_t.erb"),
                               QStringLiteral("FOOBARBAZ 1\n"), 1));
    const QList<QJsonObject> out = driveLsp(server,
        makeMsg(QStringLiteral("textDocument/didOpen"), QJsonValue(), params));
    QCOMPARE(int(out.size()), 1);
    QCOMPARE(out.first().value(QStringLiteral("method")).toString(),
             QStringLiteral("textDocument/publishDiagnostics"));
    const QJsonArray diags = out.first().value(QStringLiteral("params")).toObject()
                                 .value(QStringLiteral("diagnostics")).toArray();
    QCOMPARE(int(diags.size()), 1);
    QCOMPARE(diags.first().toObject().value(QStringLiteral("source")).toString(),
             QStringLiteral("eralsp"));
}

void TestEproto::lspCompletionAndHover() {
    LspServer server;
    initializeLsp(server);
    const QString uri = QStringLiteral("file:///tmp/era_c.erb");
    QJsonObject openParams;
    openParams.insert(QStringLiteral("textDocument"),
                      textDocument(uri, QStringLiteral("CALL FOO\n@FOO\nRETURN\n"), 1));
    driveLsp(server, makeMsg(QStringLiteral("textDocument/didOpen"), QJsonValue(), openParams));

    const QList<QJsonObject> c = driveLsp(server,
        makeMsg(QStringLiteral("textDocument/completion"), 2, posParams(uri, 0, 4)));
    QCOMPARE(int(c.size()), 1);
    const QJsonArray items = c.first().value(QStringLiteral("result")).toObject()
                                 .value(QStringLiteral("items")).toArray();
    QVERIFY(!items.isEmpty());
    bool hasCall = false;
    for (const QJsonValue& v : items) {
        if (v.toObject().value(QStringLiteral("label")).toString() == QLatin1String("CALL")) hasCall = true;
    }
    QVERIFY(hasCall);

    const QList<QJsonObject> h = driveLsp(server,
        makeMsg(QStringLiteral("textDocument/hover"), 3, posParams(uri, 0, 1)));
    QCOMPARE(int(h.size()), 1);
    const QJsonObject contents = h.first().value(QStringLiteral("result")).toObject()
                                     .value(QStringLiteral("contents")).toObject();
    QVERIFY(contents.value(QStringLiteral("value")).toString().contains(QStringLiteral("指令")));
}

void TestEproto::mcpToolsList() {
    McpServer server;
    QJsonObject initParams;
    initParams.insert(QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18"));
    initParams.insert("capabilities", QJsonObject{});
    initParams.insert("clientInfo", QJsonObject{{"name", "test"}, {"version", "1"}});
    const QJsonObject init = driveMcp(server, makeMsg(QStringLiteral("initialize"), 1, initParams));
    QCOMPARE(init.value(QStringLiteral("result")).toObject()
                 .value(QStringLiteral("protocolVersion")).toString(),
             QStringLiteral("2025-06-18"));

    driveMcp(server, makeMsg(QStringLiteral("notifications/initialized"), QJsonValue(), {}));
    const QJsonObject tl = driveMcp(server, makeMsg(QStringLiteral("tools/list"), 2, QJsonObject{}));
    const QJsonArray tools = tl.value(QStringLiteral("result")).toObject()
                                 .value(QStringLiteral("tools")).toArray();
    QCOMPARE(int(tools.size()), 6);
    QCOMPARE(tools.first().toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("era_lookup"));
}

void TestEproto::mcpCallLookupAndValidate() {
    McpServer server;
    initializeMcp(server);
    QJsonObject args;
    args.insert(QStringLiteral("name"), QStringLiteral("MAX"));
    QJsonObject call;
    call.insert(QStringLiteral("name"), QStringLiteral("era_lookup"));
    call.insert(QStringLiteral("arguments"), args);
    const QJsonObject r = driveMcp(server, makeMsg(QStringLiteral("tools/call"), 3, call));
    QVERIFY(toolText(r).contains(QStringLiteral("MAX")));
    QVERIFY(toolText(r).contains(QStringLiteral("式中函数")));

    QJsonObject vargs;
    vargs.insert(QStringLiteral("source"), QStringLiteral("FOOBARBAZ 1\nPRINT ok\n"));
    QJsonObject vcall;
    vcall.insert(QStringLiteral("name"), QStringLiteral("era_validate"));
    vcall.insert(QStringLiteral("arguments"), vargs);
    const QJsonObject vr = driveMcp(server, makeMsg(QStringLiteral("tools/call"), 4, vcall));
    QVERIFY(toolText(vr).contains(QStringLiteral("FOOBARBAZ")));
}

void TestEproto::mcpStats() {
    McpServer server;
    initializeMcp(server);
    const QJsonObject r = driveMcp(server,
        makeMsg(QStringLiteral("tools/call"), 5,
                QJsonObject{{QStringLiteral("name"), QStringLiteral("era_stats")}}));
    const auto stats = r.value("result").toObject().value("structuredContent").toObject();
    QVERIFY(stats.value("instructionCount").toInt() > 50);
    QVERIFY(stats.value("functionCount").toInt() > 100);
}

QTEST_GUILESS_MAIN(TestEproto)
#include "test_eproto.moc"
