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
// eramcp —— ERB 的 Model Context Protocol 服务器（stdio 传输）
//
// 传输：stdin/stdout，一行一个 JSON-RPC 消息（见 eproto/json_rpc.h）。
// 工具：era_lookup / era_search / era_validate / era_stats（语义来自 eraemu 的 AST）。
#include <QCoreApplication>
#include <QFile>
#include <QString>

#include <cstdio>

#include "json_rpc.h"
#include "mcp_server.h"

int main(int argc, char** argv) {
    QFile in;
    QFile out;
    if (!eproto::openProtocolStdio(in, out)) return 2;

    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("eramcp"));
    app.setApplicationVersion(QStringLiteral("0.2.0"));

    eproto::McpServer server;
    for (;;) {
        eproto::Message msg;
        QString err;
        if (!eproto::readMcpMessage(in, msg, &err)) {
            if (!err.isEmpty()) std::fprintf(stderr, "eramcp: %s\n", qPrintable(err));
            if (msg.readErrorCode != 0) {
                eproto::writeMcpMessage(out, eproto::makeError(QJsonValue::Null, msg.readErrorCode, err));
                out.flush();
                if (msg.recoverable) continue;
                return 1;
            }
            break;
        }
        for (const QJsonObject& message : server.handle(msg)) {
            eproto::writeMcpMessage(out, message);
        }
        out.flush();
    }
    return 0;
}
