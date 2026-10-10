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
// LSP 3.17 protocol regressions. Semantic data comes from the shared ErbAnalyzer.
#include <QtTest/qtest.h>
#include <QBuffer>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QFile>
#include <QUrl>

#include "lsp_server.h"

using namespace eproto;

namespace {
const QString uri = QStringLiteral("file:///tmp/lsp-main.erb");

QList<QJsonObject> send(LspServer& server, const QString& method,
                       const QJsonObject& params = {}, bool request = false) {
    // Exercise the actual transport envelope, including JSON arrays in results.
    QJsonObject wire{{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
    if (request) wire.insert(QStringLiteral("id"), QStringLiteral("test-id"));
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    writeLspMessage(buffer, wire);
    buffer.seek(0);
    Message message;
    QString error;
    if (!readLspMessage(buffer, message, &error)) return {};
    return server.handle(message);
}

QJsonObject request(LspServer& server, const QString& method, const QJsonObject& params = {}) {
    const auto replies = send(server, method, params, true);
    return replies.isEmpty() ? QJsonObject{} : replies.first();
}

void initialize(LspServer& server, bool hierarchical = false) {
    request(server, QStringLiteral("initialize"), QJsonObject{
        {"rootUri", QJsonValue::Null},
        {"capabilities", QJsonObject{{"textDocument", QJsonObject{
            {"documentSymbol", QJsonObject{{"hierarchicalDocumentSymbolSupport", hierarchical}}}}}}}});
    send(server, QStringLiteral("initialized"));
}

QList<QJsonObject> open(LspServer& server, const QString& text, int version = 1,
                       const QString& documentUri = uri) {
    return send(server, QStringLiteral("textDocument/didOpen"), QJsonObject{
        {"textDocument", QJsonObject{{"uri", documentUri}, {"languageId", "erb"},
                                     {"version", version}, {"text", text}}}});
}

QList<QJsonObject> change(LspServer& server, QJsonValue version, QJsonValue changes,
                         const QString& documentUri = uri) {
    return send(server, QStringLiteral("textDocument/didChange"), QJsonObject{
        {"textDocument", QJsonObject{{"uri", documentUri}, {"version", version}}},
        {"contentChanges", changes}});
}

QJsonObject position(int line, int character, const QString& documentUri = uri) {
    return QJsonObject{{"textDocument", QJsonObject{{"uri", documentUri}}},
                       {"position", QJsonObject{{"line", line}, {"character", character}}}};
}

QJsonArray symbols(LspServer& server) {
    return request(server, QStringLiteral("textDocument/documentSymbol"),
                   QJsonObject{{"textDocument", QJsonObject{{"uri", uri}}}})
        .value(QStringLiteral("result")).toArray();
}

QStringList completions(LspServer& server, int line, int character) {
    QStringList labels;
    const auto items = request(server, QStringLiteral("textDocument/completion"), position(line, character))
        .value(QStringLiteral("result")).toObject().value(QStringLiteral("items")).toArray();
    for (const auto& item : items) labels.append(item.toObject().value(QStringLiteral("label")).toString());
    return labels;
}

int errorCode(const QJsonObject& reply) {
    return reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt();
}
}

class TestLsp : public QObject {
    Q_OBJECT
private slots:
    void numericConversionDiagnostics() {
        LspServer server;
        initialize(server);
        const auto replies = open(server, "@MAIN\nA = 9223372036854775808\nPRINTV 1e309\nPRINT 1e309\n");
        QVERIFY(!replies.isEmpty());
        int errors = 0;
        for (const auto& value : replies.first().value("params").toObject().value("diagnostics").toArray()) {
            const auto d = value.toObject();
            if (d.value("code") != QJsonValue("numeric-conversion")) continue;
            QCOMPARE(d.value("severity").toInt(), 1);
            const auto start = d.value("range").toObject().value("start").toObject();
            QVERIFY(start.value("line").toInt() == 1 || start.value("line").toInt() == 2);
            QCOMPARE(start.value("character").toInt(), start.value("line").toInt() == 1 ? 4 : 7);
            ++errors;
        }
        QCOMPARE(errors, 2);
    }
    void diskWorkspaceOverlay() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("callee.erb");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("@TEXT\n#FUNCTIONS\nRETURNF \"ok\"\n");
        file.close();
        LspServer server;
        request(server, "initialize", {{"rootUri", QUrl::fromLocalFile(dir.path()).toString()}});
        send(server, "initialized");
        const auto initial = open(server, "@MAIN\nPRINTS TEXT()\n");
        QCOMPARE(initial.size(), 1);
        QVERIFY(initial.first().value("params").toObject().value("diagnostics").toArray().isEmpty());
        const auto locations = request(server, "textDocument/definition", position(1, 9)).value("result").toArray();
        QCOMPARE(locations.size(), 1);
        const QString calleeUri = QUrl::fromLocalFile(path).toString();
        QCOMPARE(locations.first().toObject().value("uri").toString(), calleeUri);
        open(server, "@OTHER\nRETURN\n", 1, calleeUri);
        const auto closed = send(server, "textDocument/didClose", {{"textDocument", QJsonObject{{"uri", calleeUri}}}});
        QCOMPARE(closed.size(), 2);
        QVERIFY(closed.last().value("params").toObject().value("diagnostics").toArray().isEmpty());
        QVERIFY(QFile::remove(path));
        const auto changed = send(server, "workspace/didChangeWatchedFiles", {{"changes", QJsonArray{}}});
        QCOMPARE(changed.size(), 1);
        QVERIFY(!changed.first().value("params").toObject().value("diagnostics").toArray().isEmpty());
    }
    void workspaceDiagnostics() {
        LspServer server;
        initialize(server);
        const auto hasMissing = [](const QList<QJsonObject>& messages) {
            for (const auto& message : messages) {
                const auto params = message.value("params").toObject();
                if (params.value("uri").toString() != uri) continue;
                for (const auto& diagnostic : params.value("diagnostics").toArray())
                    if (diagnostic.toObject().value("message").toString().contains("TEXT")) return true;
            }
            return false;
        };
        QVERIFY(hasMissing(open(server, "@MAIN\nPRINTS TEXT()\n")));
        const QString callee = QStringLiteral("file:///tmp/callee.erb");
        const auto linked = open(server, "@TEXT\n#FUNCTIONS\nRETURNF \"ok\"\n", 1, callee);
        QCOMPARE(linked.size(), 2);
        QVERIFY(!hasMissing(linked));
        const auto closed = send(server, "textDocument/didClose", {{"textDocument", QJsonObject{{"uri", callee}}}});
        QVERIFY(hasMissing(closed));
    }
    void workspaceEncodingDiagnostics() {
        // 读文件层不严格绑定 `Config.Encode`（要求每个文件都读得进来），但**偏离**声明
        // 编码时必须报给编辑器 —— 否则「读进来了」和「不存在」看起来一样。
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile config(dir.filePath(QStringLiteral("emuera.config")));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write(QByteArray("TextEncoding:SHIFT-JIS\n"));
        config.close();
        QFile erb(dir.filePath(QStringLiteral("main.erb")));
        QVERIFY(erb.open(QIODevice::WriteOnly));
        erb.write(QString::fromUtf8("@MAIN\nPRINTL 你好\n").toUtf8());   // 实际是 UTF-8
        erb.close();

        LspServer server;
        request(server, QStringLiteral("initialize"),
                {{"rootUri", QUrl::fromLocalFile(dir.path()).toString()}});
        // `initialized` 通知里会 indexWorkspace() + analyzeOpenDocuments()，
        // 返回的就是本次发布出去的诊断。
        const auto messages = send(server, QStringLiteral("initialized"));
        bool found = false;
        for (const auto& message : messages) {
            const auto params = message.value(QStringLiteral("params")).toObject();
            if (!params.value(QStringLiteral("uri")).toString().contains(QLatin1String("main.erb"))) continue;
            for (const auto& diagnostic : params.value(QStringLiteral("diagnostics")).toArray()) {
                if (diagnostic.toObject().value(QStringLiteral("code")).toString()
                    == QLatin1String("encoding")) {
                    found = true;
                }
            }
        }
        QVERIFY(found);
    }

    void lifecycle() {
        LspServer server;
        QCOMPARE(errorCode(request(server, "shutdown")), -32002);
        QVERIFY(open(server, "@IGNORED\n").isEmpty());
        QVERIFY(send(server, "initialized").isEmpty());
        const auto init = request(server, "initialize");
        QCOMPARE(init.value("id").toString(), QStringLiteral("test-id"));
        const auto caps = init.value("result").toObject().value("capabilities").toObject();
        QCOMPARE(caps.value("positionEncoding").toString(), QStringLiteral("utf-16"));
        QCOMPARE(caps.value("textDocumentSync").toObject().value("change").toInt(), 1);
        QCOMPARE(errorCode(request(server, "initialize")), kInvalidRequest);
        QVERIFY(open(server, "@IGNORED\n").isEmpty());
        QCOMPARE(errorCode(request(server, "textDocument/hover", position(0, 0))), -32002);
        send(server, "initialized");
        QCOMPARE(open(server, "@ACTIVE\n").size(), 1);
        QCOMPARE(errorCode(request(server, "unknown/method")), kMethodNotFound);
        QCOMPARE(errorCode(request(server, "exit")), kMethodNotFound);
        QVERIFY(!server.exitRequested());
        QVERIFY(request(server, "shutdown").value("result").isNull());
        QCOMPARE(errorCode(request(server, "shutdown")), kInvalidRequest);
        QCOMPARE(errorCode(request(server, "initialize")), kInvalidRequest);
        QVERIFY(change(server, 2, QJsonArray{QJsonObject{{"text", "@LATE"}}}).isEmpty());
        send(server, "exit");
        QVERIFY(server.exitRequested());
        QCOMPARE(server.exitCode(), 0);
        QVERIFY(send(server, "initialize", {}, true).isEmpty());
        LspServer abrupt;
        send(abrupt, "exit");
        QCOMPARE(abrupt.exitCode(), 1);
    }

    void fullSyncAndVersions() {
        LspServer server;
        initialize(server);
        QCOMPARE(open(server, "@ORIGINAL\n", 5).size(), 1);
        QVERIFY(open(server, "@DUPLICATE\n", 6).isEmpty());
        const QJsonArray replacement{QJsonObject{{"text", "@NEW\n"}}};
        QVERIFY(change(server, 5, replacement).isEmpty());
        QVERIFY(change(server, 4, replacement).isEmpty());
        QVERIFY(change(server, 6.5, replacement).isEmpty());
        QVERIFY(change(server, "6", replacement).isEmpty());
        QVERIFY(change(server, 6, replacement, "file:///tmp/unopened.erb").isEmpty());
        QVERIFY(change(server, 6, QJsonArray{QJsonObject{{"text", "@BAD"},
            {"range", QJsonObject{}}}, QJsonObject{{"text", "@NEW"}}}).isEmpty());
        QVERIFY(change(server, 6, QJsonArray{QJsonObject{{"text", "@BAD"}, {"rangeLength", 0}}}).isEmpty());
        QVERIFY(change(server, 6, QJsonArray{QJsonObject{{"text", "@BAD"}}, 42}).isEmpty());
        QVERIFY(change(server, 6, QJsonObject{}).isEmpty());
        QCOMPARE(symbols(server).first().toObject().value("name").toString(), QStringLiteral("ORIGINAL"));
        const auto noop = change(server, 6, QJsonArray{});
        QCOMPARE(noop.first().value("params").toObject().value("version").toInt(), 6);
        QCOMPARE(symbols(server).first().toObject().value("name").toString(), QStringLiteral("ORIGINAL"));
        const auto update = change(server, 7, QJsonArray{
            QJsonObject{{"text", "@FIRST"}}, QJsonObject{{"text", "@LAST\n"}}});
        QCOMPARE(update.first().value("params").toObject().value("version").toInt(), 7);
        QCOMPARE(symbols(server).first().toObject().value("name").toString(), QStringLiteral("LAST"));
        const auto close = send(server, "textDocument/didClose",
                                QJsonObject{{"textDocument", QJsonObject{{"uri", uri}}}});
        QVERIFY(close.first().value("params").toObject().value("diagnostics").toArray().isEmpty());
        QCOMPARE(errorCode(request(server, "textDocument/hover", position(0, 0))), kInvalidParams);
        QVERIFY(change(server, 8, replacement).isEmpty());
        QCOMPARE(open(server, "@REOPENED\n", -1).size(), 1);
    }

    void arrayShapesAndScopes() {
        LspServer server;
        initialize(server);
        open(server, "@FIRST\nGOTO AGAIN\n$AGAIN\nRETURN\n@SECOND\nGOTO AGAIN\n$AGAIN\nRETURN\nCALL REMOTE\n");
        open(server, "@REMOTE\n$AGAIN\nRETURN\n", 1, "file:///tmp/remote.erb");
        const auto definition = request(server, "textDocument/definition", position(5, 7)).value("result");
        QVERIFY(definition.isArray());
        QCOMPARE(definition.toArray().size(), 1);
        const auto loc = definition.toArray().first().toObject();
        QCOMPARE(loc.value("uri").toString(), uri);
        QCOMPARE(loc.value("range").toObject().value("start").toObject().value("line").toInt(), 6);
        QCOMPARE(loc.value("range").toObject().value("start").toObject().value("character").toInt(), 1);
        const auto remote = request(server, "textDocument/definition", position(8, 8)).value("result").toArray();
        QCOMPARE(remote.size(), 1);
        QCOMPARE(remote.first().toObject().value("uri").toString(), QStringLiteral("file:///tmp/remote.erb"));
        const auto outline = symbols(server);
        QCOMPARE(outline.size(), 4);
        QVERIFY(outline.first().toObject().value("location").isObject());
        QVERIFY(!outline.first().toObject().contains("range"));
        QCOMPARE(completions(server, 5, 7), QStringList{QStringLiteral("AGAIN")});
        QVERIFY(completions(server, 8, 8).contains(QStringLiteral("REMOTE")));
        QVERIFY(!completions(server, 8, 5).contains(QStringLiteral("AGAIN")));
    }

    void hierarchicalSymbolsAndUtf16() {
        LspServer server;
        initialize(server, true);
        open(server, QString::fromUtf8("@函数\r\nPRINT 😀\r\n$目标\r\nRETURN\r\n@NEXT\r\n"));
        const auto outline = symbols(server);
        QCOMPARE(outline.size(), 3);
        const auto first = outline.first().toObject();
        QVERIFY(first.value("selectionRange").isObject());
        QCOMPARE(first.value("range").toObject().value("end").toObject().value("line").toInt(), 3);
        QCOMPARE(first.value("selectionRange").toObject().value("end").toObject().value("character").toInt(), 3);
        const auto none = request(server, "textDocument/definition", position(100, 0)).value("result");
        QVERIFY(none.isArray());
        QVERIFY(none.toArray().isEmpty());
        QVERIFY(request(server, "textDocument/hover", position(100, 0)).value("result").isNull());
        QCOMPARE(errorCode(request(server, "textDocument/hover", position(-1, 0))), kInvalidParams);
        auto invalid = position(0, 0);
        invalid.insert("position", QJsonObject{{"line", 0}, {"character", 1.5}});
        QCOMPARE(errorCode(request(server, "textDocument/hover", invalid)), kInvalidParams);
    }

    void diagnosticVersions() {
        LspServer server;
        initialize(server);
        const auto out = open(server, "    FOOBARBAZ 1\n", 10);
        QCOMPARE(out.size(), 1);
        const auto params = out.first().value("params").toObject();
        QCOMPARE(params.value("version").toInt(), 10);
        const auto diagnostics = params.value("diagnostics").toArray();
        QVERIFY(!diagnostics.isEmpty());
        const auto range = diagnostics.first().toObject().value("range").toObject();
        QCOMPARE(range.value("start").toObject().value("character").toInt(), 4);
        const auto clean = change(server, 11, QJsonArray{QJsonObject{{"text", "PRINT ok\n"}}});
        QVERIFY(clean.first().value("params").toObject().value("diagnostics").toArray().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestLsp)
#include "test_lsp.moc"
