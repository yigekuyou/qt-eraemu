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
#include "erb_analyzer.h"

#include <QSet>
#include <algorithm>

// eraemu 的前端与 AST（**语义唯一来源**）——只在本 .cpp 里出现，不进公共头。
#include "Content/erb_preprocessor.h"       // ErbPreprocessor / ErbSourceLine
#include "GameData/ast/argument_parser.h"   // kInstructionSpecs / findInstructionSpec
#include "GameData/ast/ast_builder.h"       // AstBuilder::build
#include "GameData/ast/function_types.h"    // kBuiltinFunctions / builtinFunction*()
#include "GameData/ast/logical_line.h"      // LogicalLine / LineKind / ScriptPosition
#include "GameData/ast/operand_type.h"      // operandTypeName
#include "GameData/ast/parse_diagnostic.h"  // ParseDiagnostics / DiagCode
#include "GameProc/era_parse_table.h"
#include "GameProc/extension_registry.h"    // 扩展语句/函数表（解析期的「已知指令集」）

namespace eproto {
namespace {

QString fromSv(std::string_view sv) {
    return QString::fromUtf8(sv.data(), static_cast<int>(sv.size()));
}

QString severityText(DiagSeverity s) {
    switch (s) {
    case DiagSeverity::Error:   return QStringLiteral("error");
    case DiagSeverity::Warning: return QStringLiteral("warning");
    case DiagSeverity::Info:    return QStringLiteral("information");
    }
    return QStringLiteral("warning");
}

QString specRange(int minArgs, int maxArgs) {
    const QString hi = (maxArgs < 0) ? QStringLiteral("…") : QString::number(maxArgs);
    return QStringLiteral("%1..%2").arg(minArgs).arg(hi);
}

}  // namespace

namespace {

ErbDiagnostic exportDiagnostic(const ParseDiagnostic& pd) {
    ErbDiagnostic d;
    d.line = qMax(0, pd.line - 1);
    d.startCol = qMax(0, pd.column - 1);
    d.endCol = d.startCol + qMax(0, pd.length);
    d.severity = severityText(pd.severity);
    d.code = pd.code;
    d.message = pd.message;
    d.snippet = pd.snippet;
    return d;
}

QMap<QString, ErbDocument> analyzeSources(const QMap<QString, QString>& sources,
                                         bool complete) {
    static const ExtensionRegistry extensions;
    EraParseTable table(nullptr);
    QMap<QString, ErbDocument> documents;
    ErbPreprocessor pre;
    QStringList names = sources.keys();
    std::stable_sort(names.begin(), names.end(), [](const QString& a, const QString& b) {
        return a.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive)
            && !b.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive);
    });
    if (complete) {
        QSet<QString> macros;
        ErbPreprocessor::MacroTable macroTable;
        for (const QString& name : names) {
            if (!name.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive)) continue;
            const QString& text = sources[name];
            macros.unite(ErbPreprocessor::collectDefines(text));
            const auto definitions = ErbPreprocessor::collectMacroTable(text);
            for (auto it = definitions.constBegin(); it != definitions.constEnd(); ++it)
                macroTable.insert(it.key(), it.value());
        }
        pre.setMacros(macros);
        pre.setMacroTable(macroTable);
    }
    for (const QString& name : names) {
        ErbDocument doc;
        doc.fileName = name;
        doc.text = sources[name];
        const auto source = pre.process(doc.text, &doc.preprocessWarnings, name);
        QList<LogicalLine> lines;
        ParseDiagnostics diagnostics;
        ScriptPosition current;
        // Use the engine's expression frontend and cache, including its providers
        // for constants, variables, user functions and formatted strings.
        const AstResolver resolve = [&](const QString& expression) {
            auto ast = table.expressionAst(expression);
            if (!ast && !expression.trimmed().isEmpty())
                diagnostics.add(DiagSeverity::Warning, DiagCode::kExprParse,
                                name, current.lineNumber, current.column, 0,
                                QStringLiteral("表达式无法归约: %1").arg(expression.left(80)));
            return ast;
        };
        const AstResolver quiet = [&](const QString& expression) {
            return table.expressionAst(expression, true);
        };
        for (const auto& entry : source) {
            current = ScriptPosition(name, entry.physicalLine, 1);
            auto line = AstBuilder::build(entry.text, current, resolve, quiet, &diagnostics);
            line.lineIndex = lines.size();
            if (line.kind == LineKind::FunctionLabel || line.kind == LineKind::GotoLabel)
                doc.symbols.append({line.labelName, qMax(0, current.lineNumber - 1),
                                    line.kind == LineKind::GotoLabel});
            lines.append(line);
        }
        for (const auto& diagnostic : diagnostics.all())
            doc.attributed.append(exportDiagnostic(diagnostic));
        doc.lineCount = lines.size();
        doc.parsed = true;
        // Empty identities are valid for single-document callers; use a private
        // table key while retaining the original diagnostic/source identity.
        table.loadScript(name.isEmpty() ? QStringLiteral("<document>") : name, lines,
                         name.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive), name);
        if (complete) table.addParseWarnings(doc.preprocessWarnings);
        documents.insert(name, doc);
    }
    if (complete) {
        table.finalizeParse();
        const auto appendSemantic = [&](const ParseDiagnostic& diagnostic) {
            auto doc = documents.find(diagnostic.file);
            if (doc == documents.end()) return;
            const auto exported = exportDiagnostic(diagnostic);
            // Argument validation can report the same error as a bare semantic
            // error, a statement warning or a function warning. Their context
            // suffixes are presentation details, not additional occurrences.
            const auto messageKey = [](const ErbDiagnostic& d) {
                if (d.code != QLatin1String(DiagCode::kArgCheck)) return d.message;
                return d.message.section(QStringLiteral(" ["), 0, 0)
                                .section(QStringLiteral(" ("), 0, 0);
            };
            const bool exists = std::any_of(doc->attributed.cbegin(), doc->attributed.cend(),
                                           [&](const ErbDiagnostic& prior) {
                return prior.line == exported.line && prior.code == exported.code
                    && messageKey(prior) == messageKey(exported);
            });
            if (!exists) doc->attributed.append(exported);
        };
        for (const auto& diagnostic : table.parseDiagnostics().all())
            appendSemantic(diagnostic);
        // Engine warnings may be deduplicated across shared expression ASTs.
        // Read finalized calls at every source location so workspace consumers
        // retain each occurrence, without repeating the same line's AST aliases.
        for (const QString& name : names) {
            const auto* script = table.script(name.isEmpty() ? QStringLiteral("<document>") : name);
            if (!script) continue;
            for (const auto& line : script->lines) {
                const auto collect = [&](const QSharedPointer<ExpressionNode>& ast) {
                    if (!ast) return;
                    walkExpression(*ast, [&](ExpressionNode& node) {
                        if (node.kind() != NodeKind::Function) return;
                        const auto& fn = static_cast<const FunctionNode&>(node);
                        if (fn.arityError().isEmpty()) return;
                        ParseDiagnostic diagnostic;
                        diagnostic.severity = DiagSeverity::Warning;
                        diagnostic.code = DiagCode::kArgCheck;
                        diagnostic.file = name;
                        diagnostic.line = line.position.lineNumber;
                        diagnostic.column = line.position.column;
                        diagnostic.message = QStringLiteral("%1 [%2]")
                            .arg(fn.arityError(), fn.toString());
                        appendSemantic(diagnostic);
                    });
                };
                collect(line.condition);
                for (const auto& operand : line.arguments) collect(operand.ast);
                for (const auto& expression : line.argument.exprs) collect(expression);
                for (const auto& operand : line.argument.cases) collect(operand.ast);
            }
        }
    }
    for (auto& doc : documents) {
        std::stable_sort(doc.attributed.begin(), doc.attributed.end(),
                         [](const ErbDiagnostic& a, const ErbDiagnostic& b) {
            if (a.line != b.line) return a.line < b.line;
            return a.startCol < b.startCol;
        });
    }
    return documents;
}

} // namespace

ErbDocument ErbAnalyzer::parse(const QString& text, const QString& fileName) const {
    // Keep the original syntax-only contract for existing protocol consumers.
    return analyzeSources({{fileName, text}}, false).value(fileName);
}

QMap<QString, ErbDocument> ErbAnalyzer::analyzeWorkspace(
    const QMap<QString, QString>& sources) const {
    return analyzeSources(sources, true);
}

QStringList ErbAnalyzer::instructionNames() {
    QStringList names;
    names.reserve(static_cast<int>(kInstructionSpecs.size()));
    for (const InstructionSpec& s : kInstructionSpecs) names << fromSv(s.name);
    std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return names;
}

QStringList ErbAnalyzer::functionNames() {
    QStringList names;
    names.reserve(static_cast<int>(kBuiltinFunctionCount));
    for (std::size_t i = 0; i < kBuiltinFunctionCount; ++i) names << fromSv(kBuiltinFunctions[i].name);
    std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return names;
}

QString ErbAnalyzer::instructionSpec(const QString& name) {
    const InstructionSpec* s = findInstructionSpec(name.toUpper().toStdString());
    if (!s) return {};
    return QStringLiteral("指令 %1：参数 %2").arg(fromSv(s->name), specRange(s->minArgs, s->maxArgs));
}

QString ErbAnalyzer::functionSpec(const QString& name) {
    const BuiltinFunctionSpec* s = findBuiltinFunction(name.toUpper().toStdString());
    if (!s) return {};
    return QStringLiteral("式中函数 %1：返回 %2，参数 %3")
        .arg(fromSv(s->name), fromSv(operandTypeName(s->ret)), specRange(s->minArgs, s->maxArgs));
}

}  // namespace eproto
