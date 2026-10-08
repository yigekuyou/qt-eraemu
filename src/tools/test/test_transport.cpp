// SPDX-License-Identifier: GPL-3.0-or-later
#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <cstdio>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QtTest>
#include "json_rpc.h"

using namespace eproto;
namespace {
QByteArray frame(const QByteArray& body) {
    return "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}
QByteArray receive(QProcess& process, bool lsp) {
    QByteArray data;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        data += process.readAllStandardOutput();
        if (!lsp && data.endsWith('\n')) return data;
        const auto split = data.indexOf("\r\n\r\n");
        if (lsp && split >= 0) {
            const auto colon = data.indexOf(':');
            const auto end = data.indexOf("\r\n");
            bool ok = false;
            const auto n = data.mid(colon + 1, end - colon - 1).trimmed().toLongLong(&ok);
            if (ok && data.size() >= split + 4 + n) return data;
        }
        if (process.state() == QProcess::NotRunning) break;
        process.waitForReadyRead(100);
    }
    return data;
}
QJsonObject payload(const QByteArray& bytes, bool lsp) {
    return QJsonDocument::fromJson(lsp ? bytes.mid(bytes.indexOf("\r\n\r\n") + 4) : bytes).object();
}
}
class TestTransport : public QObject {
    Q_OBJECT
private slots:
    void cleanEof() {
        for (bool lsp : {false, true}) {
            QBuffer input;
            QVERIFY(input.open(QIODevice::ReadOnly));
            Message msg;
            msg.hasId = true;
            QString error = QStringLiteral("stale");
            QVERIFY(!(lsp ? readLspMessage(input, msg, &error) : readMcpMessage(input, msg, &error)));
            QVERIFY(error.isEmpty());
            QCOMPARE(msg.readErrorCode, 0);
            QVERIFY(!msg.hasId);
        }
    }
    void invalidEnvelopes_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("code");
        QTest::newRow("broken") << QByteArray("{") << kParseError;
        QTest::newRow("empty") << QByteArray("") << kParseError;
        QTest::newRow("batch") << QByteArray("[]") << kInvalidRequest;
        QTest::newRow("scalar") << QByteArray("null") << kInvalidRequest;
        QTest::newRow("version") << QByteArray(R"({"method":"ping","id":1})") << kInvalidRequest;
        QTest::newRow("method") << QByteArray(R"({"jsonrpc":"2.0","method":3})") << kInvalidRequest;
        QTest::newRow("id") << QByteArray(R"({"jsonrpc":"2.0","method":"ping","id":true})") << kInvalidRequest;
        QTest::newRow("params") << QByteArray(R"({"jsonrpc":"2.0","method":"ping","params":null})") << kInvalidRequest;
        QTest::newRow("both") << QByteArray(R"({"jsonrpc":"2.0","id":1,"result":1,"error":{}})") << kInvalidRequest;
        QTest::newRow("error") << QByteArray(R"({"jsonrpc":"2.0","id":1,"error":{"code":1.5,"message":"bad"}})") << kInvalidRequest;
    }
    void invalidEnvelopes() {
        QFETCH(QByteArray, body);
        QFETCH(int, code);
        for (bool lsp : {false, true}) {
            QBuffer input;
            input.setData(lsp ? frame(body) : body + '\n');
            QVERIFY(input.open(QIODevice::ReadOnly));
            Message msg;
            QString error;
            QVERIFY(!(lsp ? readLspMessage(input, msg, &error) : readMcpMessage(input, msg, &error)));
            QCOMPARE(msg.readErrorCode, code);
            QVERIFY(msg.recoverable);
            QVERIFY(!error.isEmpty());
        }
    }
    void framesAndValues() {
        const QByteArray request = R"({"jsonrpc":"2.0","method":"","id":null,"params":[1,2]})";
        QBuffer input;
        input.setData(frame(request).replace("Content-Length", "content-length")
                      + frame(R"({"jsonrpc":"2.0","id":"x","result":[1,"中文"]})"));
        QVERIFY(input.open(QIODevice::ReadOnly));
        Message msg;
        QVERIFY(readLspMessage(input, msg));
        QVERIFY(msg.isRequest());
        QVERIFY(msg.id.isNull());
        QCOMPARE(msg.paramsValue.toArray().size(), 2);
        QVERIFY(readLspMessage(input, msg));
        QVERIFY(msg.isResponse());
        QCOMPARE(msg.resultValue.toArray().size(), 2);
        QVERIFY(!readLspMessage(input, msg));
        QCOMPARE(msg.readErrorCode, 0);
    }
    void invalidFrames_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("duplicate") << QByteArray("Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}");
        QTest::newRow("negative") << QByteArray("Content-Length: -1\r\n\r\n");
        QTest::newRow("plus") << QByteArray("Content-Length: +2\r\n\r\n{}");
        QTest::newRow("overflow") << QByteArray("Content-Length: 99999999999999999999\r\n\r\n");
        QTest::newRow("missing") << QByteArray("Other: 1\r\n\r\n");
        QTest::newRow("partial") << QByteArray("Content-Length: 3\r\n\r\n{}");
        QTest::newRow("header-eof") << QByteArray("Content-Length: 3\r\n");
        QTest::newRow("large-header") << QByteArray(kMaxHeaderBytes + 1, 'x');
    }
    void invalidFrames() {
        QFETCH(QByteArray, bytes);
        QBuffer input(&bytes);
        QVERIFY(input.open(QIODevice::ReadOnly));
        Message msg;
        QString error;
        QVERIFY(!readLspMessage(input, msg, &error));
        QVERIFY(!msg.recoverable);
        QCOMPARE(msg.readErrorCode, kParseError);
        QVERIFY(!error.isEmpty());
    }
    void mcpLimits() {
        for (const auto& bytes : {QByteArray(kMaxMessageBytes + 2, 'x'), QByteArray("{}")}) {
            QBuffer input;
            input.setData(bytes);
            QVERIFY(input.open(QIODevice::ReadOnly));
            Message msg;
            QVERIFY(!readMcpMessage(input, msg));
            QVERIFY(!msg.recoverable);
            QCOMPARE(msg.readErrorCode, kParseError);
        }
    }
    void stdoutIsolation() {
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--stdio-probe")});
        QVERIFY(process.waitForStarted());
        QVERIFY(process.waitForFinished());
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(process.readAllStandardOutput(), QByteArray("{\"id\":1,\"jsonrpc\":\"2.0\",\"result\":null}\n"));
        QVERIFY(process.readAllStandardError().contains("ordinary diagnostic"));
    }
    void subprocessEof() {
        for (const auto& name : {QStringLiteral("/eralsp"), QStringLiteral("/eramcp")}) {
            QProcess process;
            process.start(QCoreApplication::applicationDirPath() + name, QStringList{});
            QVERIFY(process.waitForStarted());
            process.closeWriteChannel();
            QVERIFY(process.waitForFinished());
            QCOMPARE(process.exitCode(), 0);
            QVERIFY(process.readAllStandardOutput().isEmpty());
            QVERIFY(process.readAllStandardError().isEmpty());
        }
    }
    void interactivePipes_data() {
        QTest::addColumn<bool>("lsp");
        QTest::newRow("lsp") << true;
        QTest::newRow("mcp") << false;
    }
    void interactivePipes() {
        QFETCH(bool, lsp);
        QProcess process;
        process.setProgram(QCoreApplication::applicationDirPath() + (lsp ? "/eralsp" : "/eramcp"));
        process.start();
        QVERIFY(process.waitForStarted());
        const auto send = [&](const QByteArray& body) {
            const auto bytes = lsp ? frame(body) : body + '\n';
            return process.write(bytes) == bytes.size() && process.waitForBytesWritten();
        };
        // Keep stdin OPEN while waiting: closing it masks read-ahead deadlocks.
        QVERIFY(send("{"));
        auto response = payload(receive(process, lsp), lsp);
        QCOMPARE(response.value("error").toObject().value("code").toInt(), kParseError);
        QVERIFY(response.value("id").isNull());
        QVERIFY(send(R"({"jsonrpc":"1.0","method":"ping","id":7})"));
        response = payload(receive(process, lsp), lsp);
        QCOMPARE(response.value("error").toObject().value("code").toInt(), kInvalidRequest);
        QVERIFY(send(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})"));
        response = payload(receive(process, lsp), lsp);
        QCOMPARE(response.value("id").toInt(), 1);
        QVERIFY(response.value("result").isObject());
        if (lsp) {
            QVERIFY(send(R"({"jsonrpc":"2.0","method":"initialized","params":{}})"));
            QVERIFY(send(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})"));
            response = payload(receive(process, true), true);
            QCOMPARE(response.value("id").toInt(), 2);
            QVERIFY(send(R"({"jsonrpc":"2.0","method":"exit"})"));
        } else {
            QVERIFY(send(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));
            QVERIFY(send(R"({"jsonrpc":"2.0","id":2,"method":"ping"})"));
            response = payload(receive(process, false), false);
            QCOMPARE(response.value("id").toInt(), 2);
        }
        process.closeWriteChannel();
        QVERIFY(process.waitForFinished());
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        QVERIFY(process.readAllStandardOutput().isEmpty());
    }
};
int main(int argc, char** argv) {
    if (argc == 2 && QByteArray(argv[1]) == "--stdio-probe") {
        QFile in, out;
        if (!openProtocolStdio(in, out)) return 2;
        std::printf("ordinary diagnostic\n");
        std::fflush(stdout);
        writeMcpMessage(out, makeResponse(1, QJsonValue::Null));
        return out.flush() ? 0 : 3;
    }
    QCoreApplication app(argc, argv);
    TestTransport test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_transport.moc"
