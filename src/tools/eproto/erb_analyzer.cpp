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

#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <algorithm>

// eraemu 的前端与 AST（**语义唯一来源**）——只在本 .cpp 里出现，不进公共头。
#include "Config/config_loader.h"           // declaredEncodingName（读游戏配置声明）
#include "Content/encoding/text_encoding.h" // TextCodecUtil::fromName/fromLanguageName
#include "Content/erb_preprocessor.h"       // ErbPreprocessor / ErbSourceLine
#include "GameData/ast/argument_parser.h"   // kInstructionSpecs / findInstructionSpec
#include "GameData/ast/ast_builder.h"       // AstBuilder::build
#include "GameData/ast/function_types.h"    // kBuiltinFunctions / builtinFunction*()
#include "GameData/ast/logical_line.h"      // LogicalLine / LineKind / ScriptPosition
#include "GameData/ast/operand_type.h"      // operandTypeName
#include "GameData/ast/parse_diagnostic.h"  // ParseDiagnostics / DiagCode
#include "GameData/file_system_io.h"        // FileSystem::collectTree / listFilesFromTree（复用引擎文件 IO）
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
                                         bool complete,
                                         const ErbPreprocessor::RenameMap* rename,
                                         bool notCalledWarning = false) {
    static const ExtensionRegistry extensions;
    EraParseTable table(nullptr);
    QMap<QString, ErbDocument> documents;
    ErbPreprocessor pre;
    // [[名前]] -> 換字（CSV/_Rename.csv）：与装载器的 ErbLoader::tryAutoLoadRename 同源。
    // 不做这一步，`[[鈴仙・優曇華院・イナバ]]`（实际是角色番号）会被当成字符串字面量，
    // 于是 `勝負内衣判定([[…]])` 报「第 1 个实参需要Int，实得字符串」、
    // `1 + 2 * [[…]]` 报「二元运算类型错误」——eraTW 上这类噪声占绝大多数。
    if (rename) pre.setRenameMap(*rename);
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
        // 解析**必须带所属函数作用域**（对齐装载器 ErbLoader::resolveExpr 的 scope）：
        //   1) 函数私有变量 / `#DIM CONST` 要能在本函数内解析 —— 否则 `X ++`
        //      这种「与别处同名常量重名的私有变量」会被折成那个常量的字面量；
        //   2) 作用域为空时，前面文件登记过的**别函数**私有常量会污染本文件的解析。
        // 装载器的 AST 走的是同一套判据，这里不传作用域就会出现「app 装载 0 告警、
        // LSP/MCP 却报一堆」的口径差。
        QString scope;
        // Use the engine's expression frontend and cache, including its providers
        // for constants, variables, user functions and formatted strings.
        const AstResolver resolve = [&](const QString& expression) {
            auto ast = table.expressionAstInScope(expression, scope);
            if (!ast && !expression.trimmed().isEmpty())
                diagnostics.add(DiagSeverity::Warning, DiagCode::kExprParse,
                                name, current.lineNumber, current.column, 0,
                                QStringLiteral("表达式无法归约: %1").arg(expression.left(80)));
            return ast;
        };
        const AstResolver quiet = [&](const QString& expression) {
            return table.expressionAstInScope(expression, scope, true);
        };
        for (const auto& entry : source) {
            current = ScriptPosition(name, entry.physicalLine, 1);
            auto line = AstBuilder::build(entry.text, current, resolve, quiet, &diagnostics);
            line.lineIndex = lines.size();
            if (line.kind == LineKind::FunctionLabel || line.kind == LineKind::GotoLabel)
                doc.symbols.append({line.labelName, qMax(0, current.lineNumber - 1),
                                    line.kind == LineKind::GotoLabel});
            if (line.kind == LineKind::FunctionLabel) scope = line.labelName;
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
        // 游戏配置要求「関数が呼び出されなかった警告」时才开启该步（C# 默认 IGNORE）。
        table.setFunctionNotCalledWarning(notCalledWarning);
        // 全部文档装载完成：声明定稿 + 全体脚本就地定型/重绑 + 统一出诊断
        // （loadScript 只登记脚本与函数/标签，绑定一律在此处单趟完成）。
        table.markLoadComplete();
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

QString ErbAnalyzer::declaredEncodingName(const QString& dir) {
    if (dir.isEmpty()) return {};
    ConfigLoader loader;
    bool any = false;
    for (const QString& rel : {QStringLiteral("emuera.config"), QStringLiteral("CSV/_fixed.config"),
                               QStringLiteral("_fixed.config")}) {
        const QString path = dir + QLatin1Char('/') + rel;
        if (!QFileInfo::exists(path)) continue;
        loader.mergeConfig(path);
        any = true;
    }
    if (!any) return {};
    for (const QString& key : {QStringLiteral("TextEncoding"), QStringLiteral("TextCodec"),
                               QString::fromUtf8("テキストエンコーディング"),
                               QString::fromUtf8("文字コード")}) {
        if (!loader.hasConfig(key)) continue;
        const TextEncoding e = TextCodecUtil::fromName(loader.getConfig(key));
        if (e != TextEncoding::Auto) return QString::fromLatin1(TextCodecUtil::name(e));
    }
    const QString langKey = QString::fromUtf8("内部で使用する東アジア言語");
    if (loader.hasConfig(langKey)) {
        const TextEncoding e = TextCodecUtil::fromLanguageName(loader.getConfig(langKey));
        if (e != TextEncoding::Auto) return QString::fromLatin1(TextCodecUtil::name(e));
    }
    return {};
}

ErbDocument ErbAnalyzer::parse(const QString& text, const QString& fileName) const {
    // Keep the original syntax-only contract for existing protocol consumers.
    return analyzeSources({{fileName, text}}, false, nullptr, false).value(fileName);
}

QMap<QString, ErbDocument> ErbAnalyzer::analyzeWorkspace(
    const QMap<QString, QString>& sources,
    const QHash<QString, QString>& renameMap,
    bool notCalledWarning) const {
    return analyzeSources(sources, true, renameMap.isEmpty() ? nullptr : &renameMap,
                          notCalledWarning);
}

QHash<QString, QString> ErbAnalyzer::readRenameMap(const QString& gameRoot) {
    if (gameRoot.isEmpty()) return {};
    // 与 ErbLoader::tryAutoLoadRename 同一批候选位置：<游戏>/CSV/_Rename.csv 优先，
    // 其次 <游戏>/ERB/../_Rename.csv（旧布局把 CSV 直接放在 ERB 旁边）。
    const QStringList candidates = {
        gameRoot + QStringLiteral("/CSV/_Rename.csv"),
        gameRoot + QStringLiteral("/_Rename.csv"),
    };
    for (const QString& path : candidates) {
        if (!QFileInfo::exists(path)) continue;
        bool ok = false;
        const QString text = TextCodecUtil::readFile(path, TextEncoding::Auto, nullptr, &ok);
        if (!ok) continue;
        const auto map = ErbPreprocessor::parseRenameCsv(text);
        return QHash<QString, QString>(map);
    }
    return {};
}

bool ErbAnalyzer::readFunctionNotCalledWarning(const QString& gameRoot) {
    if (gameRoot.isEmpty()) return false;
    ConfigLoader loader;
    bool any = false;
    for (const QString& rel : {QStringLiteral("emuera.config"), QStringLiteral("CSV/_fixed.config"),
                               QStringLiteral("_fixed.config")}) {
        const QString path = gameRoot + QLatin1Char('/') + rel;
        if (!QFileInfo::exists(path)) continue;
        loader.mergeConfig(path);
        any = true;
    }
    if (!any) return false;
    // C# 的键名就是 ConfigData 里那句日语（中文 fork 也沿用同一键名，见 eraTW）。
    const QString key = QString::fromUtf8("関数が呼び出されなかった警告の扱い");
    if (!loader.hasConfig(key)) return false;   // 未声明 -> 用 C# 默认值 IGNORE
    const QString value = loader.getConfig(key).trimmed().toUpper();
    return value == QLatin1String("DISPLAY") || value == QLatin1String("ONCE");
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

// ---------------------------------------------------------------------------
// 整局游戏工作区（LSP/MCP 的「全量定义」上下文）
// ---------------------------------------------------------------------------

QString ErbAnalyzer::detectWorkspaceDir(const QString& filePath) {
    if (filePath.isEmpty()) return {};
    QDir dir = QFileInfo(filePath).absoluteDir();
    for (;;) {
        if (dir.dirName().compare(QLatin1String("erb"), Qt::CaseInsensitive) == 0)
            return dir.absolutePath();
        // cdUp() 到根后再向上返回 false；根目录 dirName 为空串，不会误判。
        if (!dir.cdUp() || dir.isRoot()) return {};
    }
}

WorkspaceSources ErbAnalyzer::readWorkspace(const QString& dir,
                                                        const QString& declaredEncodingDir) {
    WorkspaceSources out;
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) return out;
    const TextEncoding declared = declaredEncodingDir.isEmpty()
        ? TextEncoding::Auto
        : TextCodecUtil::fromName(declaredEncodingName(declaredEncodingDir));
    // 复用引擎装载的那套文件 IO：collectTree 一趟路径树 + 纯内存后缀过滤，
    // 与常量表/角色 CSV 装载完全同源（Android SAF 下也只用一次 provider 往返/目录）。
    const DirNode tree = FileSystem::collectTree(dir);
    const QStringList files = FileSystem::listFilesFromTree(
        tree, {QStringLiteral("*.erb"), QStringLiteral("*.erh")}, true);
    for (const QString& file : files) {
        TextEncoding detected = TextEncoding::Auto;
        bool ok = false;
        const QString text = TextCodecUtil::readFile(file, TextEncoding::Auto, &detected, &ok);
        if (!ok) continue;   // 读不出的文件跳过，不静默当成空文件
        const QString key = QFileInfo(file).absoluteFilePath();
        out.sources.insert(key, text);
        // 空文件没有可误读的内容，编码偏差对它是空话（eraTW 有两个 0 字节 ERB）。
        if (text.isEmpty()) continue;
        const QString note = TextCodecUtil::describeEncodingMismatch(detected, declared);
        if (!note.isEmpty()) out.encodingNotes.insert(key, note);
    }
    return out;
}

QMap<QString, ErbDocument> ErbAnalyzer::analyzeGameWorkspace(const QString& erbDir) const {
    if (erbDir.isEmpty() || !QFileInfo(erbDir).isDir()) return {};
    // ---- 签名：文件集 + 大小 + mtime（本地 stat 很便宜；变化 = 立即重新分析）----
    const DirNode tree = FileSystem::collectTree(erbDir);
    const QStringList files = FileSystem::listFilesFromTree(
        tree, {QStringLiteral("*.erb"), QStringLiteral("*.erh")}, true);
    QStringList parts;
    parts.reserve(files.size() * 3);
    for (const QString& file : files) {
        const QFileInfo info(file);
        parts << file << QString::number(info.size())
              << QString::number(info.lastModified().toMSecsSinceEpoch());
    }
    const QString signature = parts.join(QChar(u'\x1'));
    const auto cached = m_gameCache.find(erbDir);
    if (cached != m_gameCache.end() && cached->signature == signature)
        return cached->documents;

    // ---- 未命中：全量读盘 + 一次性跨文件分析（ERB 目录的父目录 = 游戏根）----
    const QString gameRoot = QFileInfo(erbDir).absolutePath();
    const WorkspaceSources raw = readWorkspace(erbDir, gameRoot);
    // CSV/_Rename.csv 的換字表：与装载器同一数据源（`[[名前]]` -> 番号/文本）。
    // 不装载它，eraTW 的 `勝負内衣判定([[鈴仙…]])` 会整类误报。
    const QHash<QString, QString> rename = readRenameMap(gameRoot);
    // 「関数が呼び出されなかった警告」按游戏配置决定（C# 默认 IGNORE -> 关）。
    const bool notCalledWarning = readFunctionNotCalledWarning(gameRoot);
    QMap<QString, ErbDocument> documents =
        analyzeWorkspace(raw.sources, rename, notCalledWarning);
    for (auto it = raw.encodingNotes.constBegin(); it != raw.encodingNotes.constEnd(); ++it) {
        auto doc = documents.find(it.key());
        if (doc == documents.end()) continue;
        ErbDiagnostic d;
        d.line = 0;
        d.startCol = 0;
        d.endCol = 0;
        d.severity = QStringLiteral("warning");
        d.code = QString::fromLatin1(DiagCode::kEncoding);
        d.message = it.value();
        doc->attributed.append(d);
    }
    if (m_gameCache.size() >= 4) m_gameCache.clear();   // 简单容量上限：整局 AST 不小
    m_gameCache.insert(erbDir, {signature, documents});
    return documents;
}

}  // namespace eproto
