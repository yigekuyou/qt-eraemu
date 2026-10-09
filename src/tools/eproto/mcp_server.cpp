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
#include "mcp_server.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <cmath>

#include "Content/encoding/text_encoding.h"
#include "GameData/ast/parse_diagnostic.h"   // DiagCode::kEncoding

namespace eproto {
namespace {

QJsonObject toolResult(const QJsonObject& data, bool isError = false) {
    // The text representation also makes structured results available to older clients.
    return {{"content", QJsonArray{QJsonObject{
                {"type", "text"},
                {"text", QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Indented))}}}},
            {"structuredContent", data}, {"isError", isError}};
}

QJsonObject toolError(const QString& message) {
    return toolResult({{"error", message}}, true);
}

QJsonObject objectSchema(const QJsonObject& properties, const QJsonArray& required = {}) {
    return {{"type", "object"}, {"properties", properties}, {"required", required},
            {"additionalProperties", false}};
}

QJsonObject stringSchema(const QString& description, bool nonempty = false) {
    QJsonObject result{{"type", "string"}, {"description", description}};
    if (nonempty) {
        result.insert("minLength", 1);
        result.insert("pattern", "\\S");
    }
    return result;
}

QJsonObject sourceSchema() {
    auto schema = objectSchema({
        {"source", stringSchema(QStringLiteral("ERB source text; empty documents are valid"))},
        {"path", stringSchema(QStringLiteral("Explicit file to read using the engine encoding layer"), true)},
        {"fileName", stringSchema(QStringLiteral("Logical source name (does not read a file)"), true)}});
    schema.insert("oneOf", QJsonArray{
        QJsonObject{{"required", QJsonArray{"source"}}, {"not", QJsonObject{{"required", QJsonArray{"path"}}}}},
        QJsonObject{{"required", QJsonArray{"path"}}, {"not", QJsonObject{{"required", QJsonArray{"source"}}}}}});
    return schema;
}

QJsonArray toolDefinitions() {
    QJsonArray tools;
    const auto add = [&tools](const QString& name, const QString& description, const QJsonObject& schema) {
        tools.append(QJsonObject{{"name", name}, {"description", description}, {"inputSchema", schema},
            {"annotations", QJsonObject{{"readOnlyHint", true}, {"destructiveHint", false},
                                         {"idempotentHint", true}, {"openWorldHint", false}}}});
    };
    add("era_lookup", "Look up ERB instruction/function signatures in the engine registry.",
        objectSchema({{"name", stringSchema("Instruction or function name", true)}}, {"name"}));
    add("era_search", "Search engine instruction/function names by case-insensitive substring.",
        objectSchema({{"query", stringSchema("Search text", true)},
                      {"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}, {"default", 40}}}}, {"query"}));
    add("era_validate", "Parse ERB using the engine; return diagnostics, warnings and AST symbols. Does not execute scripts.", sourceSchema());
    add("era_stats", "Return engine registry sizes.", objectSchema({}));
    add("era_symbols", "Summarize completed engine AST labels, logical line count and diagnostics. Does not execute scripts.", sourceSchema());
    add("era_validate_workspace", "Validate explicit source documents using the engine. Resolves cross-file declarations and expression functions over the completed engine AST; does not traverse the filesystem.",
        objectSchema({{"documents", QJsonObject{{"type", "array"}, {"minItems", 1}, {"maxItems", 256},
            {"items", objectSchema({{"fileName", stringSchema("Unique logical document name", true)},
                                     {"source", stringSchema("ERB source text")}}, {"fileName", "source"})}}}}, {"documents"}));
    return tools;
}

// Validate the small JSON Schema vocabulary used above, keeping advertised schemas
// and runtime checks together. This is intentionally not a general schema engine.
QString validate(const QJsonValue& value, const QJsonObject& schema, const QString& location) {
    const QString type = schema.value("type").toString();
    if (type == QLatin1String("object")) {
        if (!value.isObject()) return location + " must be an object";
        const auto object = value.toObject();
        const auto properties = schema.value("properties").toObject();
        for (const auto& required : schema.value("required").toArray())
            if (!object.contains(required.toString())) return location + "." + required.toString() + " is required";
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!properties.contains(it.key())) return location + "." + it.key() + " is not allowed";
            const QString error = validate(it.value(), properties.value(it.key()).toObject(), location + "." + it.key());
            if (!error.isEmpty()) return error;
        }
        if (schema.contains("oneOf") && object.contains("source") == object.contains("path"))
            return location + " requires exactly one of source or path";
    } else if (type == QLatin1String("string")) {
        if (!value.isString()) return location + " must be a string";
        if (schema.contains("minLength") && value.toString().trimmed().isEmpty()) return location + " must not be blank";
    } else if (type == QLatin1String("integer")) {
        const double number = value.toDouble();
        if (!value.isDouble() || !std::isfinite(number) || std::floor(number) != number ||
            number < schema.value("minimum").toDouble() || number > schema.value("maximum").toDouble())
            return location + " must be an integer between 1 and 1000";
    } else if (type == QLatin1String("array")) {
        if (!value.isArray()) return location + " must be an array";
        const auto array = value.toArray();
        if (array.size() < schema.value("minItems").toInt() || array.size() > schema.value("maxItems").toInt())
            return location + " must contain between 1 and 256 documents";
        for (qsizetype i = 0; i < array.size(); ++i) {
            const auto error = validate(array.at(i), schema.value("items").toObject(), location + QStringLiteral("[%1]").arg(i));
            if (!error.isEmpty()) return error;
        }
    }
    return {};
}

QJsonObject documentSummary(const ErbDocument& doc) {
    QJsonArray diagnostics;
    for (const auto& d : doc.attributed)
        diagnostics.append(QJsonObject{{"line", d.line}, {"startCol", d.startCol}, {"endCol", d.endCol},
            {"severity", d.severity}, {"code", d.code}, {"message", d.message}, {"snippet", d.snippet}});
    QJsonArray symbols;
    for (const auto& symbol : doc.symbols)
        symbols.append(QJsonObject{{"name", symbol.name}, {"line", symbol.line},
            {"kind", symbol.isGotoLabel ? "gotoLabel" : "function"}});
    return {{"fileName", doc.fileName}, {"parsed", doc.parsed}, {"logicalLineCount", doc.lineCount},
            {"positionEncoding", "zero-based UTF-16"}, {"diagnostics", diagnostics},
            {"preprocessWarnings", QJsonArray::fromStringList(doc.preprocessWarnings)}, {"symbols", symbols}};
}

} // namespace

QList<QJsonObject> McpServer::handle(const Message& msg) {
    if (msg.isNotification() && msg.method == QLatin1String("notifications/initialized") &&
        m_state == State::Initializing)
        m_state = State::Ready;
    if (msg.isRequest()) return {handleRequest(msg)};
    return {};
}

QJsonObject McpServer::handleRequest(const Message& msg) {
    if (msg.method == QLatin1String("ping")) return makeResponse(msg.id, QJsonObject{});
    if (msg.method == QLatin1String("initialize")) {
        if (m_state != State::Uninitialized)
            return makeError(msg.id, kInvalidRequest, "Session is already initialized");
        const auto info = msg.params.value("clientInfo").toObject();
        if (!msg.params.value("protocolVersion").isString() || msg.params.value("protocolVersion").toString().isEmpty() ||
            !msg.params.value("capabilities").isObject() || !info.value("name").isString() ||
            !info.value("version").isString())
            return makeError(msg.id, kInvalidParams, "initialize requires protocolVersion, capabilities and clientInfo {name, version}");
        const QStringList supported{"2024-11-05", "2025-03-26", "2025-06-18", "2025-11-25"};
        const QString requested = msg.params.value("protocolVersion").toString();
        m_state = State::Initializing;
        return makeResponse(msg.id, QJsonObject{
            {"protocolVersion", supported.contains(requested) ? requested : supported.last()},
            {"capabilities", QJsonObject{{"tools", QJsonObject{{"listChanged", false}}}}},
            {"serverInfo", QJsonObject{{"name", "eramcp"}, {"version", "0.3.0"}}},
            {"instructions", "Read-only ERB engine analysis. Diagnostics do not execute scripts. Source positions are zero-based UTF-16."}});
    }
    if (m_state != State::Ready)
        return makeError(msg.id, kInvalidRequest, "Complete initialize and notifications/initialized before calling tools");
    if (msg.method == QLatin1String("tools/list")) return toolsList(msg);
    if (msg.method == QLatin1String("tools/call")) return toolsCall(msg);
    return makeError(msg.id, kMethodNotFound, "Unknown method: " + msg.method);
}

QJsonObject McpServer::toolsList(const Message& msg) const {
    // This fixed list has a single page; no opaque cursor is ever issued.
    if (msg.params.contains("cursor")) return makeError(msg.id, kInvalidParams, "tools/list does not issue cursors");
    return makeResponse(msg.id, QJsonObject{{"tools", toolDefinitions()}});
}

QJsonObject McpServer::toolsCall(const Message& msg) {
    if (!msg.params.value("name").isString() ||
        (msg.params.contains("arguments") && !msg.params.value("arguments").isObject()))
        return makeError(msg.id, kInvalidParams, "tools/call requires a string name and object arguments");
    const QString name = msg.params.value("name").toString();
    const auto args = msg.params.value("arguments").toObject();
    QJsonObject schema;
    for (const auto& definition : toolDefinitions()) {
        const auto tool = definition.toObject();
        if (tool.value("name").toString() == name) schema = tool.value("inputSchema").toObject();
    }
    if (schema.isEmpty()) return makeError(msg.id, kInvalidParams, "Unknown tool: " + name);
    const QString error = validate(args, schema, "arguments");
    if (!error.isEmpty()) return makeResponse(msg.id, toolError(error));
    if (name == QLatin1String("era_lookup")) return makeResponse(msg.id, toolLookup(args));
    if (name == QLatin1String("era_search")) return makeResponse(msg.id, toolSearch(args));
    if (name == QLatin1String("era_validate") || name == QLatin1String("era_symbols")) return makeResponse(msg.id, toolValidate(args));
    if (name == QLatin1String("era_validate_workspace")) return makeResponse(msg.id, toolWorkspace(args));
    return makeResponse(msg.id, toolStats());
}

QJsonObject McpServer::toolLookup(const QJsonObject& args) const {
    const QString name = args.value("name").toString().trimmed();
    const QString instruction = ErbAnalyzer::instructionSpec(name);
    const QString function = ErbAnalyzer::functionSpec(name);
    if (instruction.isEmpty() && function.isEmpty()) return toolError("Engine name not found: " + name);
    return toolResult({{"name", name}, {"instruction", instruction}, {"function", function}});
}

QJsonObject McpServer::toolSearch(const QJsonObject& args) const {
    const QString query = args.value("query").toString().trimmed();
    const int limit = args.value("limit").toInt(40);
    QJsonArray hits;
    int total = 0;
    const auto append = [&](const QStringList& names, const QString& kind) {
        for (const QString& name : names) {
            if (!name.contains(query, Qt::CaseInsensitive)) continue;
            ++total;
            if (hits.size() < limit) hits.append(QJsonObject{{"name", name}, {"kind", kind}});
        }
    };
    append(ErbAnalyzer::instructionNames(), "instruction");
    append(ErbAnalyzer::functionNames(), "function");
    return toolResult({{"query", query}, {"total", total}, {"limit", limit}, {"matches", hits}, {"truncated", total > hits.size()}});
}

QJsonObject McpServer::toolValidate(const QJsonObject& args) const {
    QString source = args.value("source").toString();
    const QString path = args.value("path").toString();
    TextEncoding detected = TextEncoding::Auto;
    if (args.contains("path")) {
        bool ok = false;
        source = TextCodecUtil::readFile(path, TextEncoding::Auto, &detected, &ok);
        if (!ok) return toolError("Unable to read file: " + path);
    }
    const QString fileName = args.value("fileName").toString(path.isEmpty() ? QStringLiteral("<source>") : path);
    ErbDocument doc = m_analyzer.analyzeWorkspace({{fileName, source}}).value(fileName);
    // 唯一的编码类告警：**实际按哪个编码读出** != 游戏声明的编码（DiagCode::encoding）。
    // 转换在装载/读取期间完成，运行期不再有任何编码处理；这里只是把装载期的事实报给 agent。
    if (!path.isEmpty()) {
        const TextEncoding declared = TextCodecUtil::fromName(
            ErbAnalyzer::declaredEncodingName(QFileInfo(path).absolutePath()));
        const QString message = TextCodecUtil::describeEncodingMismatch(detected, declared);
        if (!message.isEmpty()) {
            ErbDiagnostic d;
            d.line = 0;
            d.startCol = 0;
            d.endCol = 0;
            d.severity = QStringLiteral("warning");
            d.code = QString::fromLatin1(DiagCode::kEncoding);
            d.message = message;
            doc.attributed.append(d);
        }
    }
    return toolResult(documentSummary(doc), !doc.parsed);
}

QJsonObject McpServer::toolWorkspace(const QJsonObject& args) const {
    const auto inputs = args.value("documents").toArray();
    QSet<QString> names;
    for (const auto& input : inputs) {
        const QString name = input.toObject().value("fileName").toString();
        if (names.contains(name)) return toolError("Duplicate document name: " + name);
        names.insert(name);
    }
    QJsonArray documents;
    int diagnosticCount = 0;
    bool parsed = true;
    QMap<QString, QString> sources;
    for (const auto& input : inputs) {
        const auto object = input.toObject();
        sources.insert(object.value("fileName").toString(), object.value("source").toString());
    }
    const auto workspace = m_analyzer.analyzeWorkspace(sources);
    for (const auto& doc : workspace) {
        documents.append(documentSummary(doc));
        diagnosticCount += int(doc.attributed.size());
        parsed = parsed && doc.parsed;
    }
    return toolResult({{"analysisScope", "workspace"}, {"crossFileResolution", true},
        {"documentCount", int(documents.size())}, {"diagnosticCount", diagnosticCount}, {"parsed", parsed},
        {"documents", documents}}, !parsed);
}

QJsonObject McpServer::toolStats() const {
    return toolResult({{"instructionCount", int(ErbAnalyzer::instructionNames().size())},
                       {"functionCount", int(ErbAnalyzer::functionNames().size())}});
}

} // namespace eproto
