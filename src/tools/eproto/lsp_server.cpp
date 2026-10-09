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
#include "lsp_server.h"

#include <QJsonArray>
#include <QDirListing>
#include <QFileInfo>
#include "Config/config_loader.h"
#include "Content/encoding/text_encoding.h"
#include "GameData/ast/parse_diagnostic.h"   // DiagCode::kEncoding
#include <QUrl>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace eproto {
namespace {

QJsonObject makePosition(int line, int character) {
    QJsonObject o;
    o.insert(QStringLiteral("line"), line);
    o.insert(QStringLiteral("character"), character);
    return o;
}

QJsonObject makeRange(int l0, int c0, int l1, int c1) {
    QJsonObject o;
    o.insert(QStringLiteral("start"), makePosition(l0, c0));
    o.insert(QStringLiteral("end"), makePosition(l1, c1));
    return o;
}

int severityNumber(const QString& s) {
    if (s == QLatin1String("error")) return 1;
    if (s == QLatin1String("warning")) return 2;
    if (s == QLatin1String("information")) return 3;
    return 4;
}

bool isWordChar(QChar c) {
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

QStringList docLines(const QString& text) {
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return normalized.split(QLatin1Char('\n'));
}

QString wordAt(const QString& text, int line, int character) {
    const QStringList lines = docLines(text);
    if (line < 0 || line >= lines.size()) return {};
    const QString& l = lines.at(line);
    const int i = qBound(0, character, l.size());
    int start = i;
    while (start > 0 && isWordChar(l.at(start - 1))) --start;
    int end = i;
    while (end < l.size() && isWordChar(l.at(end))) ++end;
    return (end <= start) ? QString() : l.mid(start, end - start);
}

QString prefixBefore(const QString& text, int line, int character) {
    const QStringList lines = docLines(text);
    if (line < 0 || line >= lines.size()) return {};
    const QString& l = lines.at(line);
    const int i = qBound(0, character, l.size());
    int start = i;
    while (start > 0 && isWordChar(l.at(start - 1))) --start;
    return l.mid(start, i - start);
}

QString uriToPath(const QString& uri) {
    const QUrl url(uri);
    return url.isLocalFile() ? url.toLocalFile() : uri;
}

bool isInteger(const QJsonValue& value, bool nonnegative = false) {
    if (!value.isDouble()) return false;
    const double n = value.toDouble();
    return std::isfinite(n) && std::floor(n) == n
        && n >= (nonnegative ? 0 : std::numeric_limits<int>::min())
        && n <= std::numeric_limits<int>::max();
}

bool validUri(const QString& uri) {
    const QUrl url(uri, QUrl::StrictMode);
    return !uri.isEmpty() && url.isValid() && !url.scheme().isEmpty();
}

// Function boundaries come from parsed AST labels, including preprocessing.
int scopeAt(const ErbDocument& doc, int line) {
    int scope = -1;
    for (const auto& symbol : doc.symbols)
        if (!symbol.isGotoLabel && symbol.line <= line) scope = qMax(scope, symbol.line);
    return scope;
}

bool gotoContext(const QString& text, int line) {
    const QString source = docLines(text).value(line).trimmed();
    if (source.startsWith(QLatin1Char('$'))) return true;
    int end = 0;
    while (end < source.size() && isWordChar(source.at(end))) ++end;
    const QString command = source.left(end).toUpper();
    return command == QLatin1String("GOTO") || command == QLatin1String("TRYGOTO")
        || command == QLatin1String("TRYCGOTO");
}

QJsonObject symbolRange(const ErbDocument& doc, const ErbSymbol& symbol) {
    const QString line = docLines(doc.text).value(symbol.line);
    const int start = qMax(0, int(line.indexOf(symbol.name, 0, Qt::CaseInsensitive)));
    return makeRange(symbol.line, start, symbol.line,
                     qMin(int(line.size()), start + int(symbol.name.size())));
}

QJsonObject notification(const QString& method, const QJsonObject& params) {
    QJsonObject n;
    n.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    n.insert(QStringLiteral("method"), method);
    n.insert(QStringLiteral("params"), params);
    return n;
}

}  // namespace

QList<QJsonObject> LspServer::handle(const Message& msg) {
    QList<QJsonObject> out;
    if (m_exitRequested) return out;
    if (msg.isNotification() && msg.method == QLatin1String("exit")) {
        m_exitRequested = true;
        return out;
    }
    if (msg.isNotification()) {
        handleNotification(msg, out);
        return out;
    }
    if (msg.isRequest()) {
        out.append(handleRequest(msg));
    }
    return out;
}

QJsonObject LspServer::handleRequest(const Message& msg) {
    const QString& method = msg.method;

    if (m_shutdown)
        return makeError(msg.id, kInvalidRequest, QStringLiteral("Server has shut down"));
    if (method == QLatin1String("initialize")) {
        if (m_initialized)
            return makeError(msg.id, kInvalidRequest, QStringLiteral("Already initialized"));
        m_initialized = true;
        m_hierarchicalSymbols = msg.params.value(QStringLiteral("capabilities")).toObject()
            .value(QStringLiteral("textDocument")).toObject()
            .value(QStringLiteral("documentSymbol")).toObject()
            .value(QStringLiteral("hierarchicalDocumentSymbolSupport")).toBool();
        const QString rootUri = msg.params.value(QStringLiteral("rootUri")).toString();
        if (!rootUri.isEmpty()) m_rootPath = uriToPath(rootUri);
        if (m_rootPath.isEmpty()) m_rootPath = msg.params.value(QStringLiteral("rootPath")).toString();

        QJsonObject sync;
        sync.insert(QStringLiteral("openClose"), true);
        sync.insert(QStringLiteral("change"), 1);  // Full

        QJsonObject completionOptions;
        completionOptions.insert(QStringLiteral("triggerCharacters"),
                                 QJsonArray{QStringLiteral("@")});
        completionOptions.insert(QStringLiteral("resolveProvider"), false);

        QJsonObject capabilities;
        capabilities.insert(QStringLiteral("positionEncoding"), QStringLiteral("utf-16"));
        capabilities.insert(QStringLiteral("textDocumentSync"), sync);
        capabilities.insert(QStringLiteral("completionProvider"), completionOptions);
        capabilities.insert(QStringLiteral("hoverProvider"), true);
        capabilities.insert(QStringLiteral("definitionProvider"), true);
        capabilities.insert(QStringLiteral("documentSymbolProvider"), true);

        QJsonObject serverInfo;
        serverInfo.insert(QStringLiteral("name"), QStringLiteral("eralsp"));
        serverInfo.insert(QStringLiteral("version"), QStringLiteral("0.2.0"));

        QJsonObject result;
        result.insert(QStringLiteral("capabilities"), capabilities);
        result.insert(QStringLiteral("serverInfo"), serverInfo);
        return makeResponse(msg.id, result);
    }
    if (!m_initialized)
        return makeError(msg.id, -32002, QStringLiteral("Server not initialized"));
    if (method == QLatin1String("shutdown")) {
        m_shutdown = true;
        m_ready = false;
        return makeResponse(msg.id, QJsonValue(QJsonValue::Null));
    }

    if (!m_ready)
        return makeError(msg.id, -32002, QStringLiteral("Waiting for initialized notification"));
    if (method == QLatin1String("textDocument/completion")
        || method == QLatin1String("textDocument/hover")
        || method == QLatin1String("textDocument/definition")
        || method == QLatin1String("textDocument/documentSymbol")) {
        const QString uri = msg.params.value(QStringLiteral("textDocument")).toObject()
            .value(QStringLiteral("uri")).toString();
        if (!validUri(uri) || !m_docs.contains(uri))
            return makeError(msg.id, kInvalidParams, QStringLiteral("Document is not open"));
        if (method != QLatin1String("textDocument/documentSymbol")) {
            const QJsonObject pos = msg.params.value(QStringLiteral("position")).toObject();
            if (!isInteger(pos.value(QStringLiteral("line")), true)
                || !isInteger(pos.value(QStringLiteral("character")), true))
                return makeError(msg.id, kInvalidParams, QStringLiteral("Invalid position"));
        }
    }
    if (method == QLatin1String("textDocument/completion"))     return completion(msg);
    if (method == QLatin1String("textDocument/hover"))          return hover(msg);
    if (method == QLatin1String("textDocument/definition"))     return definition(msg);
    if (method == QLatin1String("textDocument/documentSymbol")) return documentSymbol(msg);

    return makeError(msg.id, kMethodNotFound, QStringLiteral("eralsp: 未实现的方法 %1").arg(method));
}

void LspServer::handleNotification(const Message& msg, QList<QJsonObject>& out) {
    const QString& method = msg.method;
    if (m_shutdown || !m_initialized) return;
    if (method == QLatin1String("initialized")) {
        m_ready = true;
        indexWorkspace();
        analyzeOpenDocuments(out);
        return;
    }
    if (!m_ready) return;
    if (method == QLatin1String("workspace/didChangeWatchedFiles")) {
        indexWorkspace();
        analyzeOpenDocuments(out);
        return;
    }
    const QJsonObject td = msg.params.value(QStringLiteral("textDocument")).toObject();
    const QString uri = td.value(QStringLiteral("uri")).toString();
    if (!validUri(uri)) return;

    if (method == QLatin1String("textDocument/didOpen")) {
        if (m_versions.contains(uri) || !td.value(QStringLiteral("text")).isString()
            || !td.value(QStringLiteral("languageId")).isString()
            || !isInteger(td.value(QStringLiteral("version")))) return;
        m_paths.insert(uri, uriToPath(uri));
        m_versions.insert(uri, td.value(QStringLiteral("version")).toInt());
        reparse(uri, td.value(QStringLiteral("text")).toString());
        analyzeOpenDocuments(out);
        return;
    }
    if (!m_versions.contains(uri)) return;
    if (method == QLatin1String("textDocument/didChange")) {
        if (!isInteger(td.value(QStringLiteral("version")))) return;
        const int version = td.value(QStringLiteral("version")).toInt();
        if (version <= m_versions.value(uri)
            || !msg.params.value(QStringLiteral("contentChanges")).isArray()) return;
        const QJsonArray changes = msg.params.value(QStringLiteral("contentChanges")).toArray();
        // Validate the complete batch before committing text or version. Full sync
        // cannot safely apply a range, even if a later event is a full replacement.
        for (const auto& value : changes) {
            if (!value.isObject()) return;
            const QJsonObject change = value.toObject();
            if (!change.value(QStringLiteral("text")).isString()
                || change.contains(QStringLiteral("range"))
                || change.contains(QStringLiteral("rangeLength"))) return;
        }
        m_versions.insert(uri, version);
        if (!changes.isEmpty())
            reparse(uri, changes.last().toObject().value(QStringLiteral("text")).toString());
        analyzeOpenDocuments(out);
        return;
    }
    if (method == QLatin1String("textDocument/didClose")) {
        m_docs.remove(uri);
        m_versions.remove(uri);
        indexWorkspace();
        m_paths.remove(uri);
        QJsonObject params;
        params.insert(QStringLiteral("uri"), uri);
        params.insert(QStringLiteral("diagnostics"), QJsonArray{});
        out.append(notification(QStringLiteral("textDocument/publishDiagnostics"), params));
        analyzeOpenDocuments(out);
    }
}

void LspServer::reparse(const QString& uri, const QString& text) {
    ErbDocument doc;
    doc.fileName = uri;
    doc.text = text;
    m_docs.insert(uri, doc);
}

namespace {

// 工作区「声明的编码」：emuera.config / CSV/_fixed.config 里的
// `TextEncoding`/`テキストエンコーディング`/`文字コード`，或 `内部で使用する東アジア言語`。
// Auto = 没声明（此时不做「编码不符」判定）。
TextEncoding declaredEncodingOf(const QString& root) {
    return TextCodecUtil::fromName(ErbAnalyzer::declaredEncodingName(root));
}

} // namespace

void LspServer::indexWorkspace() {
    m_diskSources.clear();
    m_encodingIssues.clear();
    if (m_rootPath.isEmpty() || !QFileInfo(m_rootPath).isDir()) return;
    const TextEncoding declared = declaredEncodingOf(m_rootPath);
    using F = QDirListing::IteratorFlag;
    for (const auto& entry : QDirListing(m_rootPath, {QStringLiteral("*.erb"), QStringLiteral("*.erh")},
                                         F::FilesOnly | F::Recursive)) {
        const QString uri = QUrl::fromLocalFile(entry.fileInfo().absoluteFilePath()).toString();
        TextEncoding detected = TextEncoding::Auto;
        bool ok = false;
        const QString text = TextCodecUtil::readFile(entry.filePath(), TextEncoding::Auto,
                                                     &detected, &ok);
        if (ok) m_diskSources.insert(uri, text);
        // 唯一要报的编码类问题：**偏离声明的编码**。解码细节（回退到哪个编码、
        // 会不会乱码）不构成需要上报的问题 —— 装载期间就已经转成 Qt 原生字符了。
        const QString message = TextCodecUtil::describeEncodingMismatch(detected, declared);
        if (!message.isEmpty()) m_encodingIssues.insert(uri, message);
    }
}

void LspServer::analyzeOpenDocuments(QList<QJsonObject>& out) {
    QMap<QString, QString> sources = m_diskSources;
    for (auto it = m_versions.constBegin(); it != m_versions.constEnd(); ++it)
        sources.insert(it.key(), m_docs.value(it.key()).text);
    const auto workspace = m_analyzer.analyzeWorkspace(sources);
    m_docs.clear();
    for (auto it = workspace.constBegin(); it != workspace.constEnd(); ++it) {
        ErbDocument doc = it.value();
        // 读取/解码期间的编码问题（DiagCode::kEncoding）：挂在文档上一起发布 ——
        // 这些问题在装载/读取期间就已确定，运行期拿到的一律是 Qt 原生字符。
        const QString issue = m_encodingIssues.value(it.key());
        if (!issue.isEmpty()) {
            ErbDiagnostic d;
            d.line = 0;
            d.startCol = 0;
            d.endCol = 0;
            d.severity = QStringLiteral("warning");
            d.code = QString::fromLatin1(DiagCode::kEncoding);
            d.message = issue;
            doc.attributed.append(d);
        }
        m_docs.insert(it.key(), doc);
        // 有编码问题的文件即使没被打开也要发布：否则「读不出来的文件」在编辑器里
        // 等同于不存在（原实现就是这样静默丢掉的）。
        if (m_versions.contains(it.key()) || !issue.isEmpty()) publishDiagnostics(it.key(), out);
    }
}

void LspServer::publishDiagnostics(const QString& uri, QList<QJsonObject>& out) {
    const ErbDocument doc = m_docs.value(uri);
    QJsonArray arr;
    for (const ErbDiagnostic& d : doc.attributed) {
        QJsonObject diag;
        diag.insert(QStringLiteral("range"), makeRange(d.line, d.startCol, d.line, d.endCol));
        diag.insert(QStringLiteral("severity"), severityNumber(d.severity));
        diag.insert(QStringLiteral("code"), d.code);
        diag.insert(QStringLiteral("source"), QStringLiteral("eralsp"));
        diag.insert(QStringLiteral("message"), d.message);
        arr.append(diag);
    }
    QJsonObject params;
    params.insert(QStringLiteral("uri"), uri);
    if (m_versions.contains(uri)) params.insert(QStringLiteral("version"), m_versions.value(uri));
    params.insert(QStringLiteral("diagnostics"), arr);
    out.append(notification(QStringLiteral("textDocument/publishDiagnostics"), params));
}

QJsonObject LspServer::completion(const Message& msg) const {
    const QString uri = msg.params.value(QStringLiteral("textDocument")).toObject()
                            .value(QStringLiteral("uri")).toString();
    const QJsonObject pos = msg.params.value(QStringLiteral("position")).toObject();
    const int line = pos.value(QStringLiteral("line")).toInt();
    const int character = pos.value(QStringLiteral("character")).toInt();
    const QString prefix = prefixBefore(m_docs.value(uri).text, line, character);

    QJsonArray items;
    QSet<QString> seen;
    const auto add = [&](const QStringList& names, int kind, const QString& detail) {
        for (const QString& n : names) {
            if (!prefix.isEmpty() && !n.startsWith(prefix, Qt::CaseInsensitive)) continue;
            if (seen.contains(n.toUpper())) continue;
            seen.insert(n.toUpper());
            QJsonObject item;
            item.insert(QStringLiteral("label"), n);
            item.insert(QStringLiteral("kind"), kind);
            item.insert(QStringLiteral("detail"), detail);
            items.append(item);
        }
    };
    const ErbDocument current = m_docs.value(uri);
    const bool localGoto = gotoContext(current.text, line);
    QStringList uris = m_docs.keys();
    std::sort(uris.begin(), uris.end());
    for (const QString& candidateUri : uris) {
        const ErbDocument& doc = m_docs[candidateUri];
        for (const auto& symbol : doc.symbols) {
            if (symbol.isGotoLabel) {
                if (!localGoto || candidateUri != uri
                    || scopeAt(doc, symbol.line) != scopeAt(current, line)) continue;
            } else if (localGoto) continue;
            add(QStringList{symbol.name}, symbol.isGotoLabel ? 18 : 3,
                symbol.isGotoLabel ? QStringLiteral("局部跳转标签") : QStringLiteral("ERB 函数"));
        }
    }
    if (!localGoto) {
        add(ErbAnalyzer::instructionNames(), 14, QStringLiteral("eraemu 指令"));   // Keyword
        add(ErbAnalyzer::functionNames(), 3, QStringLiteral("eraemu 式中函数"));   // Function
    }
    QJsonObject result;
    result.insert(QStringLiteral("isIncomplete"), false);
    result.insert(QStringLiteral("items"), items);
    return makeResponse(msg.id, result);
}

QJsonObject LspServer::hover(const Message& msg) const {
    const QString uri = msg.params.value(QStringLiteral("textDocument")).toObject()
                            .value(QStringLiteral("uri")).toString();
    const QJsonObject pos = msg.params.value(QStringLiteral("position")).toObject();
    const int line = pos.value(QStringLiteral("line")).toInt();
    const int character = pos.value(QStringLiteral("character")).toInt();

    const ErbDocument doc = m_docs.value(uri);
    const QString word = wordAt(doc.text, line, character);
    if (word.isEmpty()) return makeResponse(msg.id, QJsonValue(QJsonValue::Null));

    QString md = ErbAnalyzer::instructionSpec(word);
    if (md.isEmpty()) md = ErbAnalyzer::functionSpec(word);
    if (md.isEmpty()) {
        for (const ErbSymbol& s : doc.symbols) {
            if (s.isGotoLabel && (!gotoContext(doc.text, line)
                || scopeAt(doc, s.line) != scopeAt(doc, line))) continue;
            if (s.name.compare(word, Qt::CaseInsensitive) == 0) {
                md = QStringLiteral("%1标签 %2（本文件）")
                         .arg(s.isGotoLabel ? QStringLiteral("$") : QStringLiteral("@"), s.name);
                break;
            }
        }
    }
    if (md.isEmpty()) return makeResponse(msg.id, QJsonValue(QJsonValue::Null));

    QJsonObject contents;
    contents.insert(QStringLiteral("kind"), QStringLiteral("plaintext"));
    contents.insert(QStringLiteral("value"), md);
    QJsonObject result;
    result.insert(QStringLiteral("contents"), contents);
    return makeResponse(msg.id, result);
}

QJsonObject LspServer::definition(const Message& msg) const {
    const QString uri = msg.params.value(QStringLiteral("textDocument")).toObject()
                            .value(QStringLiteral("uri")).toString();
    const QJsonObject pos = msg.params.value(QStringLiteral("position")).toObject();
    const int line = pos.value(QStringLiteral("line")).toInt();
    const int character = pos.value(QStringLiteral("character")).toInt();
    const QString word = wordAt(m_docs.value(uri).text, line, character);

    QJsonArray locations;
    const ErbDocument current = m_docs.value(uri);
    const bool localGoto = gotoContext(current.text, line);
    if (!word.isEmpty()) {
        QStringList uris = m_docs.keys();
        std::sort(uris.begin(), uris.end());
        for (const QString& candidateUri : uris) {
            const auto it = m_docs.constFind(candidateUri);
            for (const ErbSymbol& s : it.value().symbols) {
                if (s.isGotoLabel != localGoto) continue;
                if (s.isGotoLabel && (it.key() != uri
                    || scopeAt(it.value(), s.line) != scopeAt(current, line))) continue;
                if (s.name.compare(word, Qt::CaseInsensitive) == 0) {
                    QJsonObject loc;
                    loc.insert(QStringLiteral("uri"), it.key());
                    loc.insert(QStringLiteral("range"), symbolRange(it.value(), s));
                    locations.append(loc);
                }
            }
        }
    }
    return makeResponse(msg.id, locations);
}

QJsonObject LspServer::documentSymbol(const Message& msg) const {
    const QString uri = msg.params.value(QStringLiteral("textDocument")).toObject()
                            .value(QStringLiteral("uri")).toString();
    const ErbDocument doc = m_docs.value(uri);
    const QStringList lines = docLines(doc.text);

    QJsonArray arr;
    for (const ErbSymbol& s : doc.symbols) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), s.name);
        o.insert(QStringLiteral("kind"), s.isGotoLabel ? 13 : 12);
        if (m_hierarchicalSymbols) {
            // A flat DocumentSymbol array is valid; function ranges still include
            // their entire body rather than ending at the first local label.
            int endLine = int(lines.size()) - 1;
            for (const auto& next : doc.symbols)
                if (next.line > s.line && (s.isGotoLabel || !next.isGotoLabel)) {
                    endLine = next.line - 1;
                    break;
                }
            endLine = qMax(s.line, endLine);
            o.insert(QStringLiteral("range"),
                     makeRange(s.line, 0, endLine, int(lines.value(endLine).size())));
            o.insert(QStringLiteral("selectionRange"), symbolRange(doc, s));
        } else {
            o.insert(QStringLiteral("location"), QJsonObject{
                {QStringLiteral("uri"), uri}, {QStringLiteral("range"), symbolRange(doc, s)}});
        }
        arr.append(o);
    }
    return makeResponse(msg.id, arr);
}

}  // namespace eproto
