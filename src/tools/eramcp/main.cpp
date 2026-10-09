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
//
// 命令行：QCommandLineParser（Qt 文档《QCommandLineParser》）—— --help / --version。
// 用 parse() 而非 process()：MCP 主机（AI 客户端）会带自己的参数，未知参数只提示、
// 不退出，协议循环照常运行。
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QString>

#include <cstdio>

#include "json_rpc.h"
#include "mcp_server.h"

namespace {

// 命令行文本走 QCoreApplication::translate（context = "eramcp"），便于 lupdate 提取。
QString cmdTr(const char* source) {
    return QCoreApplication::translate("eramcp", source);
}

// QCommandLineParser 不可复制（Q_DISABLE_COPY），所以按引用配置而非按值返回。
void setupParser(QCommandLineParser& parser) {
    parser.setApplicationDescription(
        cmdTr("ERB 的 Model Context Protocol 服务器（stdio 传输）。"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(
        QStringLiteral("stdio"),
        cmdTr("通过标准输入/输出通信（默认；兼容 MCP 主机的 --stdio 约定）。")));
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("eramcp"));
    app.setApplicationVersion(QStringLiteral("0.2.0"));

    // 先解析命令行，再动标准流（openProtocolStdio 会把 stdout 重定向到 stderr）。
    QCommandLineParser parser;
    setupParser(parser);
    const bool parsed = parser.parse(QCoreApplication::arguments());
    if (parser.isSet(QStringLiteral("help"))) {
        std::fputs(qPrintable(parser.helpText()), stdout);
        return 0;
    }
    if (parser.isSet(QStringLiteral("version"))) {
        std::fprintf(stdout, "%s %s\n", qPrintable(app.applicationName()),
                     qPrintable(app.applicationVersion()));
        return 0;
    }
    if (!parsed) {
        std::fprintf(stderr, "eramcp: %s\n", qPrintable(parser.errorText()));
    }

    QFile in;
    QFile out;
    if (!eproto::openProtocolStdio(in, out)) return 2;

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
