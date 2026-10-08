// SPDX-License-Identifier: GPL-3.0-or-later
// MCP protocol and read-only engine integration regressions.
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryFile>
#include <QtTest>

#include "mcp_server.h"

using namespace eproto;

namespace {
QJsonObject request(McpServer& server, const QString& method, const QJsonObject& params = {}) {
    Message message;
    message.method = method;
    message.params = params;
    message.id = 42;
    message.hasId = true;
    const auto replies = server.handle(message);
    return replies.isEmpty() ? QJsonObject{} : replies.first();
}
QJsonObject initialize(McpServer& server, const QString& version = QStringLiteral("2025-11-25")) {
    return request(server, "initialize", {{"protocolVersion", version}, {"capabilities", QJsonObject{}},
        {"clientInfo", QJsonObject{{"name", "regression"}, {"version", "1"}}}});
}
void ready(McpServer& server) {
    initialize(server);
    Message notification;
    notification.method = "notifications/initialized";
    const auto replies = server.handle(notification);
    Q_ASSERT(replies.isEmpty());
}
QJsonObject call(McpServer& server, const QString& name, const QJsonObject& args = {}) {
    return request(server, "tools/call", {{"name", name}, {"arguments", args}});
}
QJsonObject result(const QJsonObject& response) { return response.value("result").toObject(); }
QJsonObject data(const QJsonObject& response) { return result(response).value("structuredContent").toObject(); }
int errorCode(const QJsonObject& response) { return response.value("error").toObject().value("code").toInt(); }
}

class TestMcp : public QObject {
    Q_OBJECT
private slots:
    void lifecycle();
    void discoveryAndStructuredResults();
    void argumentValidation_data();
    void argumentValidation();
    void protocolErrors();
    void completedAst();
    void workspaceDocuments();
    void encodedFileAndEmptySource();
};

void TestMcp::lifecycle() {
    McpServer server;
    Message notification;
    notification.method = "notifications/initialized";
    QVERIFY(server.handle(notification).isEmpty());
    QCOMPARE(errorCode(request(server, "tools/list")), kInvalidRequest);
    QVERIFY(request(server, "ping").contains("result"));
    QCOMPARE(errorCode(request(server, "initialize")), kInvalidParams);
    QCOMPARE(result(initialize(server, "unknown-version")).value("protocolVersion").toString(), QString("2025-11-25"));
    QCOMPARE(errorCode(request(server, "tools/list")), kInvalidRequest);
    QCOMPARE(errorCode(initialize(server)), kInvalidRequest);
    QVERIFY(server.handle(notification).isEmpty());
    QVERIFY(request(server, "tools/list").contains("result"));
    QCOMPARE(errorCode(request(server, "missing-method")), kMethodNotFound);
    notification.method = "tools/call";
    notification.params = {{"name", "era_stats"}};
    QVERIFY(server.handle(notification).isEmpty());
}

void TestMcp::discoveryAndStructuredResults() {
    McpServer server;
    ready(server);
    const auto tools = result(request(server, "tools/list")).value("tools").toArray();
    QCOMPARE(tools.size(), 6);
    for (const auto& value : tools) {
        const auto tool = value.toObject();
        QVERIFY(tool.value("annotations").toObject().value("readOnlyHint").toBool());
        QVERIFY(!tool.value("inputSchema").toObject().value("additionalProperties").toBool(true));
    }
    const auto response = call(server, "era_stats");
    QVERIFY(data(response).value("instructionCount").toInt() > 0);
    QVERIFY(data(response).value("functionCount").toInt() > 0);
    const auto text = result(response).value("content").toArray().first().toObject().value("text").toString();
    QCOMPARE(QJsonDocument::fromJson(text.toUtf8()).object(), data(response));
    QVERIFY(!result(response).value("isError").toBool());
    const auto lookup = call(server, "era_lookup", {{"name", "MAX"}});
    QVERIFY(data(lookup).value("function").toString().contains("MAX"));
    const auto search = call(server, "era_search", {{"query", "print"}, {"limit", 1}});
    QCOMPARE(data(search).value("matches").toArray().size(), 1);
    QVERIFY(data(search).value("total").toInt() > 1);
    QVERIFY(data(search).value("truncated").toBool());
}

void TestMcp::argumentValidation_data() {
    QTest::addColumn<QString>("tool");
    QTest::addColumn<QJsonObject>("args");
    QTest::newRow("missing-name") << QString("era_lookup") << QJsonObject{};
    QTest::newRow("wrong-type") << QString("era_lookup") << QJsonObject{{"name", 12}};
    QTest::newRow("blank-name") << QString("era_lookup") << QJsonObject{{"name", "  "}};
    QTest::newRow("unknown-property") << QString("era_stats") << QJsonObject{{"execute", true}};
    QTest::newRow("fractional-limit") << QString("era_search") << QJsonObject{{"query", "print"}, {"limit", 1.5}};
    QTest::newRow("string-limit") << QString("era_search") << QJsonObject{{"query", "print"}, {"limit", "2"}};
    QTest::newRow("large-limit") << QString("era_search") << QJsonObject{{"query", "print"}, {"limit", 1001}};
    QTest::newRow("negative-limit") << QString("era_search") << QJsonObject{{"query", "print"}, {"limit", -1}};
    QTest::newRow("missing-source") << QString("era_validate") << QJsonObject{};
    QTest::newRow("exclusive-source") << QString("era_validate") << QJsonObject{{"source", ""}, {"path", "unused.erb"}};
    QTest::newRow("null-source") << QString("era_symbols") << QJsonObject{{"source", QJsonValue::Null}};
    QTest::newRow("no-documents") << QString("era_validate_workspace") << QJsonObject{{"documents", QJsonArray{}}};
    QTest::newRow("bad-document") << QString("era_validate_workspace") << QJsonObject{{"documents", QJsonArray{QJsonObject{{"fileName", "a"}}}}};
}

void TestMcp::argumentValidation() {
    QFETCH(QString, tool);
    QFETCH(QJsonObject, args);
    McpServer server;
    ready(server);
    const auto response = call(server, tool, args);
    QVERIFY(!response.contains("error"));
    QVERIFY(result(response).value("isError").toBool());
    QVERIFY(!data(response).value("error").toString().isEmpty());
}

void TestMcp::protocolErrors() {
    McpServer server;
    ready(server);
    QCOMPARE(errorCode(call(server, "unknown")), kInvalidParams);
    QCOMPARE(errorCode(request(server, "tools/call", {{"name", "era_stats"}, {"arguments", QJsonArray{}}})), kInvalidParams);
    QCOMPARE(errorCode(request(server, "tools/call", {{"name", 12}})), kInvalidParams);
    QCOMPARE(errorCode(request(server, "tools/list", {{"cursor", "unused"}})), kInvalidParams);
    QVERIFY(data(request(server, "tools/call", {{"name", "era_stats"}})).contains("instructionCount"));
}

void TestMcp::completedAst() {
    McpServer server;
    ready(server);
    const auto response = call(server, "era_symbols", {{"source", "@MAIN\n$again\n    FOOBARBAZ 1\nRETURN\n"}, {"fileName", "memory.erb"}});
    const auto summary = data(response);
    QVERIFY(summary.value("parsed").toBool());
    QCOMPARE(summary.value("fileName").toString(), QString("memory.erb"));
    const auto symbols = summary.value("symbols").toArray();
    QCOMPARE(symbols.size(), 2);
    QCOMPARE(symbols.first().toObject().value("name").toString(), QString("MAIN"));
    QCOMPARE(symbols.at(1).toObject().value("kind").toString(), QString("gotoLabel"));
    const auto diagnostics = summary.value("diagnostics").toArray();
    QVERIFY(!diagnostics.isEmpty());
    bool found = false;
    for (const auto& value : diagnostics) {
        const auto diagnostic = value.toObject();
        if (diagnostic.value("code") != QJsonValue("unknown-instruction")) continue;
        found = true;
        QCOMPARE(diagnostic.value("line").toInt(), 2);
        QCOMPARE(diagnostic.value("startCol").toInt(), 4);
    }
    QVERIFY(found);
    QVERIFY(!result(response).value("isError").toBool()); // diagnostics are successful analysis
}

void TestMcp::workspaceDocuments() {
    McpServer server;
    ready(server);
    const QJsonArray documents{
        QJsonObject{{"fileName", "memory:a.erb"}, {"source", "@A\nRETURN\n"}},
        QJsonObject{{"fileName", "/does/not/exist/b.erb"}, {"source", "FOOBARBAZ 1\n"}}};
    const auto summary = data(call(server, "era_validate_workspace", {{"documents", documents}}));
    QCOMPARE(summary.value("documentCount").toInt(), 2);
    QVERIFY(summary.value("diagnosticCount").toInt() > 0);
    QCOMPARE(summary.value("documents").toArray().at(0).toObject().value("fileName").toString(), QString("/does/not/exist/b.erb"));
    QVERIFY(summary.value("crossFileResolution").toBool());
    const QJsonArray linked{
        QJsonObject{{"fileName", "caller.erb"}, {"source", "@MAIN\nPRINTS TEXT()\n"}},
        QJsonObject{{"fileName", "callee.erb"}, {"source", "@TEXT\n#FUNCTIONS\nRETURNF \"ok\"\n"}}};
    const auto isolated = data(call(server, "era_validate_workspace", {{"documents", QJsonArray{linked.first()}}}));
    QVERIFY(isolated.value("diagnosticCount").toInt() > 0);
    const auto resolved = data(call(server, "era_validate_workspace", {{"documents", linked}}));
    QCOMPARE(resolved.value("diagnosticCount").toInt(), 0);
    const auto duplicate = call(server, "era_validate_workspace", {{"documents", QJsonArray{documents.first(), documents.first()}}});
    QVERIFY(result(duplicate).value("isError").toBool());
}

void TestMcp::encodedFileAndEmptySource() {
    McpServer server;
    ready(server);
    const auto empty = call(server, "era_validate", {{"source", ""}});
    QVERIFY(!result(empty).value("isError").toBool());
    QVERIFY(data(empty).value("parsed").toBool());
    QTemporaryFile file;
    QVERIFY(file.open());
    const QString source = QString::fromUtf8("@日本語\nRETURN\n");
    QByteArray encoded = QByteArray::fromHex("fffe");
    for (const QChar c : source) {
        encoded.append(char(c.unicode() & 0xff));
        encoded.append(char(c.unicode() >> 8));
    }
    QCOMPARE(file.write(encoded), encoded.size());
    QVERIFY(file.flush());
    const auto fromFile = call(server, "era_symbols", {{"path", file.fileName()}});
    QVERIFY(!result(fromFile).value("isError").toBool());
    const auto fromSource = call(server, "era_symbols", {{"source", source}});
    QCOMPARE(data(fromFile).value("symbols"), data(fromSource).value("symbols"));
    QVERIFY(!data(fromFile).value("symbols").toArray().isEmpty());
    QVERIFY(result(call(server, "era_validate", {{"path", "/does/not/exist.erb"}})).value("isError").toBool());
}

QTEST_GUILESS_MAIN(TestMcp)
#include "test_mcp.moc"
