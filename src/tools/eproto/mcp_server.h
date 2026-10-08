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
#ifndef EPROTO_MCP_SERVER_H
#define EPROTO_MCP_SERVER_H

#include <QJsonObject>
#include <QList>
#include <QString>

#include "eproto_export.h"
#include "erb_analyzer.h"
#include "json_rpc.h"

// ---------------------------------------------------------------------------
// McpServer —— 面向 LLM 的 ERB 语义分析 MCP 服务器（协议核心，与传输解耦）
//
// 依据 MCP 规范 2025-11-25：JSON-RPC 2.0；stdio 传输时一行一个消息。
// 语义全部来自 eraemu 的 AST（ErbAnalyzer）与引擎登记表。
//
// 工具：
//   era_lookup   —— 查指令/式中函数的引擎登记（参数形态、返回类型）
//   era_search   —— 在引擎登记表里检索名字
//   era_validate —— 用 eraemu 的 AST 解析一段 ERB，列出引擎诊断
//   era_stats    —— 引擎登记表规模
//   era_symbols  —— 已完成 AST 的标签与诊断摘要
//   era_validate_workspace —— 显式源码文档集合的引擎诊断
// ---------------------------------------------------------------------------

namespace eproto {

class EPROTO_API McpServer {
public:
    McpServer() = default;

    [[nodiscard]] QList<QJsonObject> handle(const Message& msg);

private:
    [[nodiscard]] QJsonObject handleRequest(const Message& msg);
    [[nodiscard]] QJsonObject toolsList(const Message& msg) const;
    [[nodiscard]] QJsonObject toolsCall(const Message& msg);

    [[nodiscard]] QJsonObject toolLookup(const QJsonObject& args) const;
    [[nodiscard]] QJsonObject toolSearch(const QJsonObject& args) const;
    [[nodiscard]] QJsonObject toolValidate(const QJsonObject& args) const;
    [[nodiscard]] QJsonObject toolWorkspace(const QJsonObject& args) const;
    [[nodiscard]] QJsonObject toolStats() const;

    enum class State { Uninitialized, Initializing, Ready };
    State m_state = State::Uninitialized;
    ErbAnalyzer m_analyzer;
};

}  // namespace eproto

#endif  // EPROTO_MCP_SERVER_H
