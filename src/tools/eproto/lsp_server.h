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
#ifndef EPROTO_LSP_SERVER_H
#define EPROTO_LSP_SERVER_H

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "eproto_export.h"
#include "erb_analyzer.h"
#include "json_rpc.h"

// ---------------------------------------------------------------------------
// LspServer —— ERB 的 Language Server（协议核心，与传输解耦，便于单测）
//
// 语义全部来自 eraemu 的 AST（ErbAnalyzer）与引擎登记表（kInstructionSpecs /
// kBuiltinFunctions），不读任何外部文档。
//
// 覆盖 LSP 3.17 的最小可用面：
//   initialize / initialized / shutdown / exit / $/cancelRequest（忽略）
//   textDocument/{didOpen,didChange,didClose}       → publishDiagnostics（推送）
//   textDocument/{completion,hover,definition,documentSymbol}
// ---------------------------------------------------------------------------

namespace eproto {

class EPROTO_API LspServer {
public:
    LspServer() = default;

    [[nodiscard]] QList<QJsonObject> handle(const Message& msg);

    [[nodiscard]] bool exitRequested() const { return m_exitRequested; }
    [[nodiscard]] bool initialized() const { return m_initialized; }
    [[nodiscard]] int exitCode() const { return m_shutdown ? 0 : 1; }
    [[nodiscard]] QString rootPath() const { return m_rootPath; }

private:
    [[nodiscard]] QJsonObject handleRequest(const Message& msg);
    void handleNotification(const Message& msg, QList<QJsonObject>& out);
    void reparse(const QString& uri, const QString& text);
    void indexWorkspace();
    void analyzeOpenDocuments(QList<QJsonObject>& out);
    void publishDiagnostics(const QString& uri, QList<QJsonObject>& out);

    [[nodiscard]] QJsonObject completion(const Message& msg) const;
    [[nodiscard]] QJsonObject hover(const Message& msg) const;
    [[nodiscard]] QJsonObject definition(const Message& msg) const;
    [[nodiscard]] QJsonObject documentSymbol(const Message& msg) const;

    ErbAnalyzer m_analyzer;
    QHash<QString, ErbDocument> m_docs;      // uri -> 已解析 AST
    QHash<QString, QString>     m_paths;     // uri -> 本地路径（供 ScriptPosition）
    QHash<QString, int>         m_versions; // open buffers
    QMap<QString, QString> m_diskSources;
    // uri -> 读取/解码期间的编码问题（DiagCode::kEncoding）。
    // 读文件层保证「任何编码的文件都能读进来」，但「读干净了没有」必须让编辑器
    // 看得到 —— 以前 indexWorkspace() 里 `if (ok)` 会把读不出来的文件静默丢掉。
    QHash<QString, QString> m_encodingIssues;
    bool    m_initialized = false;
    bool    m_ready = false;
    bool    m_shutdown = false;
    bool    m_hierarchicalSymbols = false;
    bool    m_exitRequested = false;
    QString m_rootPath;
};

}  // namespace eproto

#endif  // EPROTO_LSP_SERVER_H
