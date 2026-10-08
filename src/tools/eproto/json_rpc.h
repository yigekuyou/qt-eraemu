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
#ifndef EPROTO_JSON_RPC_H
#define EPROTO_JSON_RPC_H

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "eproto_export.h"

QT_BEGIN_NAMESPACE
class QIODevice;
class QFile;
QT_END_NAMESPACE

// ---------------------------------------------------------------------------
// JSON-RPC 2.0 传输层（LSP 与 MCP 共用）
//
// 两种协议都是 JSON-RPC 2.0，只有**分帧（framing）**不同：
//   * LSP  —— HTTP 风格头部："Content-Length: N\r\n\r\n" + N 字节 UTF-8 JSON。
//             依据 LSP 规范《Base Protocol / Header Part》。
//   * MCP  —— stdio 传输：一行一个 JSON 对象（消息内不得含换行）。
//             依据 MCP 规范 2025-06-18《Transports / stdio》。
//
// 本文件只做「字节流 ⇄ Message」；协议语义（initialize/…）在 lsp_server /
// mcp_server 里。整个实现只依赖 QtCore（QJsonDocument），不需要 QtNetwork
// ——两种传输都是标准输入/输出，不是 socket。
// ---------------------------------------------------------------------------

namespace eproto {

struct EPROTO_API Message {
    QString     method;        // 请求/通知的方法名；响应为空
    QJsonObject params;
    QJsonValue  id;            // JSON-RPC id（数字或字符串）
    bool        hasMethod = false;
    bool        hasId = false; // 出现 "id" 字段即为请求；通知没有 id
    QJsonObject result;        // 仅对「响应」有意义
    bool        hasError = false;
    QJsonObject error;
    // Preserve JSON-RPC array parameters and arbitrary response values while
    // keeping the original object accessors source-compatible.
    QJsonValue paramsValue;
    QJsonValue resultValue;
    int readErrorCode = 0;      // zero on success or clean EOF
    bool recoverable = false;  // failure consumed a complete frame


    [[nodiscard]] bool isRequest() const { return (hasMethod || !method.isEmpty()) && hasId; }
    [[nodiscard]] bool isNotification() const { return (hasMethod || !method.isEmpty()) && !hasId; }
    [[nodiscard]] bool isResponse() const { return !hasMethod && method.isEmpty(); }
};

// Blocking streams only. false + empty err/readErrorCode == 0 means clean EOF;
// otherwise consult readErrorCode and recoverable before reading again.
// Limits bound allocations even for untrusted Content-Length and unterminated lines.
inline constexpr qint64 kMaxMessageBytes = 16 * 1024 * 1024;
inline constexpr qint64 kMaxHeaderBytes = 8 * 1024;

// Opens binary, unbuffered descriptor-based stdin and reserves a protocol output
// descriptor; ordinary stdout (including dependency diagnostics) goes to stderr.
EPROTO_API bool openProtocolStdio(QFile& in, QFile& out);

// 读取一条 LSP 消息。
EPROTO_API bool readLspMessage(QIODevice& in, Message& out, QString* err = nullptr);
EPROTO_API void writeLspMessage(QIODevice& out, const QJsonObject& message);

// 读取一条 MCP 消息（换行分隔）。返回 false 表示 EOF。
EPROTO_API bool readMcpMessage(QIODevice& in, Message& out, QString* err = nullptr);
EPROTO_API void writeMcpMessage(QIODevice& out, const QJsonObject& message);

// 组包辅助
EPROTO_API QJsonObject makeResponse(const QJsonValue& id, const QJsonValue& result);
EPROTO_API QJsonObject makeError(const QJsonValue& id, int code, const QString& message);

// JSON-RPC 标准错误码
inline constexpr int kParseError     = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams  = -32602;
inline constexpr int kInternalError  = -32603;

}  // namespace eproto

#endif  // EPROTO_JSON_RPC_H
