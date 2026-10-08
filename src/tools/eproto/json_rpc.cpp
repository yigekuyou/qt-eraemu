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
#include "json_rpc.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <cmath>
#include <cstdio>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace eproto {
namespace {
bool fail(Message& out, QString* err, const QString& text,
          int code = kParseError, bool recoverable = false) {
    out.readErrorCode = code;
    out.recoverable = recoverable;
    if (err) *err = text;
    return false;
}

bool decodeObject(const QByteArray& bytes, Message& out, QString* err) {
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
    if (pe.error != QJsonParseError::NoError) {
        // QJsonDocument only represents objects/arrays. A valid JSON scalar is
        // nevertheless an invalid request, rather than a JSON parse error.
        const auto wrapped = QJsonDocument::fromJson("[" + bytes + "]", &pe);
        if (pe.error == QJsonParseError::NoError && wrapped.isArray() && wrapped.array().size() == 1)
            return fail(out, err, QStringLiteral("expected JSON-RPC object"), kInvalidRequest, true);
        return fail(out, err, QStringLiteral("invalid JSON: ") + pe.errorString(), kParseError, true);
    }
    if (!doc.isObject())
        return fail(out, err, QStringLiteral("expected JSON-RPC object"), kInvalidRequest, true);
    const auto obj = doc.object();
    const auto invalid = [&]() {
        return fail(out, err, QStringLiteral("invalid JSON-RPC 2.0 message"), kInvalidRequest, true);
    };
    if (obj.value(QStringLiteral("jsonrpc")) != QJsonValue(QStringLiteral("2.0"))) return invalid();
    const auto id = obj.value(QStringLiteral("id"));
    if (!id.isUndefined() && !id.isNull() && !id.isString() && !id.isDouble()) return invalid();
    const bool hasMethod = obj.contains(QStringLiteral("method"));
    const bool hasResult = obj.contains(QStringLiteral("result"));
    const bool hasError = obj.contains(QStringLiteral("error"));
    if (hasMethod) {
        if (!obj.value(QStringLiteral("method")).isString() || hasResult || hasError) return invalid();
        const auto params = obj.value(QStringLiteral("params"));
        if (!params.isUndefined() && !params.isObject() && !params.isArray()) return invalid();
    } else {
        if (id.isUndefined() || hasResult == hasError || obj.contains(QStringLiteral("params"))) return invalid();
        if (hasError) {
            const auto error = obj.value(QStringLiteral("error"));
            if (!error.isObject()) return invalid();
            const auto code = error.toObject().value(QStringLiteral("code"));
            if (!code.isDouble() || std::floor(code.toDouble()) != code.toDouble()
                || !error.toObject().value(QStringLiteral("message")).isString()) return invalid();
        }
    }
    out.hasMethod = hasMethod;
    out.method = obj.value(QStringLiteral("method")).toString();
    out.hasId = !id.isUndefined();
    out.id = id;
    out.paramsValue = obj.value(QStringLiteral("params"));
    out.params = out.paramsValue.toObject();
    out.resultValue = obj.value(QStringLiteral("result"));
    out.result = out.resultValue.toObject();
    out.hasError = hasError;
    out.error = obj.value(QStringLiteral("error")).toObject();
    return true;
}

void writeAll(QIODevice& out, const QByteArray& bytes) {
    qint64 offset = 0;
    while (offset < bytes.size()) {
        const qint64 n = out.write(bytes.constData() + offset, bytes.size() - offset);
        if (n <= 0) break;
        offset += n;
    }
}
} // namespace

bool openProtocolStdio(QFile& in, QFile& out) {
    std::fflush(stdout);
#ifdef Q_OS_WIN
    const int input = _fileno(stdin);
    const int output = _fileno(stdout);
    const int saved = _dup(output);
    if (saved < 0) return false;
    if (_setmode(input, _O_BINARY) < 0 || _setmode(saved, _O_BINARY) < 0
        || _dup2(_fileno(stderr), output) < 0) { _close(saved); return false; }
#else
    const int input = fileno(stdin);
    const int output = fileno(stdout);
    const int saved = dup(output);
    if (saved < 0) return false;
    if (dup2(fileno(stderr), output) < 0) { close(saved); return false; }
#endif
    // Use descriptors, not FILE*: stdio fread can wait for the entire requested
    // buffer on a pipe. Qt buffering can likewise read ahead past the frame.
    if (!out.open(saved, QIODevice::WriteOnly | QIODevice::Unbuffered, QFileDevice::AutoCloseHandle)) {
#ifdef Q_OS_WIN
        _close(saved);
#else
        close(saved);
#endif
        return false;
    }
    return in.open(input, QIODevice::ReadOnly | QIODevice::Unbuffered);
}

bool readLspMessage(QIODevice& in, Message& out, QString* err) {
    out = Message{};
    if (err) err->clear();
    qint64 contentLength = -1;
    qint64 headerBytes = 0;
    for (;;) {
        const QByteArray line = in.readLine(kMaxHeaderBytes - headerBytes + 1);
        if (line.isEmpty()) {
            if (headerBytes == 0) return false;
            return fail(out, err, QStringLiteral("unexpected EOF in headers"));
        }
        headerBytes += line.size();
        if (headerBytes > kMaxHeaderBytes || !line.endsWith("\r\n"))
            return fail(out, err, QStringLiteral("invalid or oversized LSP headers"));
        if (line == "\r\n") break;
        const auto colon = line.indexOf(':');
        if (colon <= 0) return fail(out, err, QStringLiteral("invalid LSP header"));
        const auto name = line.left(colon);
        const auto value = line.mid(colon + 1).trimmed();
        if (name.compare("Content-Length", Qt::CaseInsensitive) == 0) {
            if (contentLength >= 0 || value.isEmpty())
                return fail(out, err, QStringLiteral("duplicate or empty Content-Length"));
            qint64 length = 0;
            for (const char c : value) {
                if (c < '0' || c > '9') return fail(out, err, QStringLiteral("invalid Content-Length"));
                length = length * 10 + (c - '0');
                if (length > kMaxMessageBytes) return fail(out, err, QStringLiteral("message exceeds size limit"));
            }
            contentLength = length;
        }
    }
    if (contentLength < 0) return fail(out, err, QStringLiteral("missing Content-Length"));
    QByteArray body;
    body.reserve(contentLength);
    while (body.size() < contentLength) {
        const auto chunk = in.read(contentLength - body.size());
        if (chunk.isEmpty()) return fail(out, err, QStringLiteral("unexpected EOF in body"));
        body += chunk;
    }
    return decodeObject(body, out, err);
}

void writeLspMessage(QIODevice& out, const QJsonObject& message) {
    const auto body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    writeAll(out, "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
}

bool readMcpMessage(QIODevice& in, Message& out, QString* err) {
    out = Message{};
    if (err) err->clear();
    const auto line = in.readLine(kMaxMessageBytes + 2);
    if (line.isEmpty()) return false;
    if (line.size() > kMaxMessageBytes + 1)
        return fail(out, err, QStringLiteral("message exceeds size limit"));
    if (!line.endsWith('\n'))
        return fail(out, err, QStringLiteral("unterminated MCP message"));
    return decodeObject(line, out, err);
}

void writeMcpMessage(QIODevice& out, const QJsonObject& message) {
    writeAll(out, QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

QJsonObject makeResponse(const QJsonValue& id, const QJsonValue& result) {
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id.isUndefined() ? QJsonValue(QJsonValue::Null) : id},
            {QStringLiteral("result"), result.isUndefined() ? QJsonValue(QJsonValue::Null) : result}};
}

QJsonObject makeError(const QJsonValue& id, int code, const QString& message) {
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id.isUndefined() ? QJsonValue(QJsonValue::Null) : id},
            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code},
                                                 {QStringLiteral("message"), message}}}};
}
} // namespace eproto
