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
// eralsp —— ERB 的 Language Server（LSP over stdio）
//
// 传输：stdin/stdout，Content-Length 分帧（见 eproto/json_rpc.h）。
// 语义：eraemu 自己的 AST（ErbPreprocessor + AstBuilder）+ 引擎登记表。
//
// 命令行：QCommandLineParser（Qt 文档《QCommandLineParser》）。
//   用例：eralsp --help / --version；无参数即按 stdio 协议启动。
//   这里刻意用 parse() 而非 process()：LSP 客户端会带自己的参数（常见 --stdio），
//   未知参数只提示、不退出，保证协议循环照常运行（文档 dnslookup 范例同理）。
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QString>

#include <cstdio>

#include "json_rpc.h"
#include "lsp_server.h"

namespace {

// 命令行文本走 QCoreApplication::translate（context = "eralsp"），便于 lupdate 提取。
QString cmdTr(const char* source) {
    return QCoreApplication::translate("eralsp", source);
}

// QCommandLineParser 不可复制（Q_DISABLE_COPY），所以按引用配置而非按值返回。
void setupParser(QCommandLineParser& parser) {
    parser.setApplicationDescription(
        cmdTr("ERB 语言服务器（LSP over stdio）。"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(
        QStringLiteral("stdio"),
        cmdTr("通过标准输入/输出通信（默认；兼容 LSP 客户端的 --stdio 约定）。")));
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("eralsp"));
    app.setApplicationVersion(QStringLiteral("0.2.0"));

    // 先解析命令行，再动标准流：--help/--version 必须打在**真正的 stdout** 上，
    // 而 openProtocolStdio() 会把 stdout 重定向到 stderr（保护协议流）。
    QCommandLineParser parser;
    setupParser(parser);
    //
    // 宽容未知参数：LSP 客户端会带自己的启动参数（如 --clientProcessId、
    // --node-ipc），它们不是本程序的选项。这里只提示到 stderr，不退出 ——
    // 协议循环照常按 stdio 运行（与 QCommandLineParser 文档里 process() 的
    // 「未知选项即退出」刻意不同，原因在此）。
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
        std::fprintf(stderr, "eralsp: %s\n", qPrintable(parser.errorText()));
    }

    QFile in;
    QFile out;
    if (!eproto::openProtocolStdio(in, out)) return 2;

    eproto::LspServer server;
    for (;;) {
        eproto::Message msg;
        QString err;
        if (!eproto::readLspMessage(in, msg, &err)) {
            if (!err.isEmpty()) std::fprintf(stderr, "eralsp: %s\n", qPrintable(err));
            if (msg.readErrorCode != 0) {
                eproto::writeLspMessage(out, eproto::makeError(QJsonValue::Null, msg.readErrorCode, err));
                out.flush();
                if (msg.recoverable) continue;
                return 1;
            }
            break;
        }
        for (const QJsonObject& message : server.handle(msg)) {
            eproto::writeLspMessage(out, message);
        }
        out.flush();
        if (server.exitRequested()) return server.exitCode();
    }
    return 0;
}
