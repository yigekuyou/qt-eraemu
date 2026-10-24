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
#include "declaration_scan.h"

#include <QCoreApplication>
#include <QSet>
#include <algorithm>
#include <exception>

#include "ast/ast_builder.h"          // labelNameEnd（@标签名的唯一判据）
#include "ast/name_interner.h"        // eravar::segmentKeyText
#include "ast/parse_diagnostic.h"
#include "user_defined_variable_data.h"
#include "ast/variable_table.h"
#include "load_passes.h"              // stripDirectiveComment / parseScopedVarSpec
#include "variable_storage.h"

namespace declscan {
namespace {

// `#DIM CONST NAME = <初值>` 的「初值」部分（按顶层逗号切分）。
// 空初值返回空表 —— eraTW 用它表示「初值写在其后的 { … } 块里」。
QStringList constValueExpressions(const QString& declRest) {
    QChar quote;
    int depth = 0;
    for (int i = 0; i < declRest.size(); ++i) {
        const QChar c = declRest.at(i);
        if (!quote.isNull()) { if (c == quote) quote = QChar(); continue; }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; continue; }
        if (c == QLatin1Char('(') || c == QLatin1Char('[')) { ++depth; continue; }
        if (c == QLatin1Char(')') || c == QLatin1Char(']')) { if (depth > 0) --depth; continue; }
        if (c == QLatin1Char('=') && depth == 0) {
            const QString tail = declRest.mid(i + 1).trimmed();
            if (tail.isEmpty()) return QStringList();
            QStringList out;
            QString current;
            int d2 = 0;
            QChar q2;
            for (const QChar ch : tail) {
                if (!q2.isNull()) { current += ch; if (ch == q2) q2 = QChar(); continue; }
                if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) { q2 = ch; current += ch; continue; }
                if (ch == QLatin1Char('(') || ch == QLatin1Char('[')) { ++d2; current += ch; continue; }
                if (ch == QLatin1Char(')') || ch == QLatin1Char(']')) { if (d2 > 0) --d2; current += ch; continue; }
                if (d2 == 0 && ch == QLatin1Char(',')) { out << current.trimmed(); current.clear(); continue; }
                current += ch;
            }
            if (!current.trimmed().isEmpty()) out << current.trimmed();
            return out;
        }
    }
    return QStringList();
}

// 行是否是「变量声明指令」，以及它的作用域关键字（`#DIM(S) GLOBAL X` 的
// GLOBAL 写在变量名之前，与 `#GLOBAL(S)` 指令是两种写法）。
// **唯一**的指令 -> 语义映射：registerDeclarationText 用它决定类型/作用域。
struct SharpDirective {
    bool isDeclaration = false;
    bool isStr = false;
    bool isGlobal = false;
    bool isPrivate = false;
};

SharpDirective classifySharpDirective(const QString& directive) {
    SharpDirective out;
    if (directive == QLatin1String("DIM")) {
        // 默认：函数内私有 / 文件级全局
        out.isDeclaration = true;
    } else if (directive == QLatin1String("DIMS")) {
        out.isDeclaration = true;
        out.isStr = true;
    } else if (directive == QLatin1String("GLOBAL")) {
        out.isDeclaration = true;
        out.isGlobal = true;
    } else if (directive == QLatin1String("GLOBALS")) {
        out.isDeclaration = true;
        out.isGlobal = true;
        out.isStr = true;
    } else if (directive == QLatin1String("PRIVATE")) {
        out.isDeclaration = true;
        out.isGlobal = true;
        out.isPrivate = true;
    }
    return out;
}

// 段身份：声明只在**函数段内**才是私有的：头文件 / 文件头段 / 函数外的 # 行
// 一律广域（与 loadScript 同一判据）。
QString scopeKeyOf(const QString& scriptName, const QString& function, int segmentStart,
                   bool isHeaderFile) {
    if (isHeaderFile || function.isEmpty() || segmentStart < 0) return QString();
    return eravar::segmentKeyText(scriptName, segmentStart);
}

}  // namespace

// ---------------------------------------------------------------------------
// 扫描：预处理后的行序列 -> 声明站点
// ---------------------------------------------------------------------------
namespace {

// 唯一的扫描循环。三种输入形态（SourceLine / LogicalLine / QStringList）共用它 ——
// 「哪一行是声明」「段起点是哪个下标」「函数名是什么」只有这一份实现。
template <typename TextOf, typename PosOf>
Scan scanImpl(int count, const QString& scriptName, bool isHeaderFile,
              const TextOf& textOf, const PosOf& posOf) {
    Scan out;
    out.segmentStartOfLine.reserve(count);
    QString currentFunction;   // 函数名事实（形参/展示/段内私有判定）
    int segmentStart = -1;     // 当前段起点（@ 所在行；文件头段 = -1）
    bool afterLabel = false;   // .ERB 的 # 行必须紧跟函数标签（对齐 C# ErbLoader）
    for (int i = 0; i < count; ++i) {
        const QString& raw = textOf(i);
        const QString t = raw.trimmed();
        // 顺序与 AstBuilder::build 的行分类同序：先空/注释，再 '#'，再 '@'/'$'。
        if (t.isEmpty() || t.startsWith(QLatin1Char(';'))) {
            out.segmentStartOfLine.append(segmentStart);
            continue;   // 空行/注释不结束声明区
        }
        if (t.startsWith(QLatin1Char('#'))) {
            out.segmentStartOfLine.append(segmentStart);
            if (!isHeaderFile && !afterLabel) {
                out.orphanSharpLines.insert(i, raw);
                continue;   // 函数声明之外：loadScript 告警并忽略
            }
            QString s = t.mid(1);
            int k = 0;
            while (k < s.size() && !s.at(k).isSpace()) ++k;
            if (!classifySharpDirective(s.left(k).toUpper()).isDeclaration) continue;
            DeclarationSite site;
            site.kind = DeclarationKind::SharpDirective;
            site.rawLine = raw;
            site.position = posOf(i);
            site.function = currentFunction;
            site.scriptName = scriptName;
            site.segmentStart = segmentStart;
            site.scopeKey = scopeKeyOf(scriptName, currentFunction, segmentStart, isHeaderFile);
            site.isHeaderFile = isHeaderFile;
            out.declarations.append(site);
            continue;
        }
        if (t.startsWith(QLatin1Char('@'))) {
            // 标签名 = 首个标识符（与 AstBuilder 同一判据/同一实现）。
            const QString labelText = t.mid(1).trimmed();
            const QString name = labelText.left(AstBuilder::labelNameEnd(labelText)).trimmed();
            if (!name.isEmpty()) currentFunction = name;
            segmentStart = i;
            afterLabel = true;
            out.segmentStartOfLine.append(i);
            continue;
        }
        if (t.startsWith(QLatin1Char('$'))) {
            // GOTO 标签：不是段起点（段身份只由 @函数标签决定）。
            out.segmentStartOfLine.append(segmentStart);
            continue;
        }
        if (loadpass::isScopedVariableLine(t)) {
            out.segmentStartOfLine.append(segmentStart);
            DeclarationSite site;
            site.kind = DeclarationKind::ScopedVariable;
            site.rawLine = raw;
            site.position = posOf(i);
            site.function = currentFunction;
            site.scriptName = scriptName;
            site.segmentStart = segmentStart;
            site.scopeKey = scopeKeyOf(scriptName, currentFunction, segmentStart, isHeaderFile);
            site.isHeaderFile = isHeaderFile;
            out.declarations.append(site);
            afterLabel = false;
            continue;
        }
        out.segmentStartOfLine.append(segmentStart);
        afterLabel = false;   // 普通指令行：声明区结束
    }
    return out;
}

}  // namespace

Scan scanScript(const QList<SourceLine>& lines, const QString& scriptName, bool isHeaderFile) {
    return scanImpl(lines.size(), scriptName, isHeaderFile,
                    [&lines](int i) -> const QString& { return lines.at(i).text; },
                    [&lines](int i) { return lines.at(i).position; });
}

Scan scanScript(const QList<LogicalLine>& lines, const QString& scriptName, bool isHeaderFile) {
    return scanImpl(lines.size(), scriptName, isHeaderFile,
                    [&lines](int i) -> const QString& { return lines.at(i).raw; },
                    [&lines](int i) { return lines.at(i).position; });
}

Scan scanScript(const QStringList& lines, const QString& scriptName, const QString& fileName,
                bool isHeaderFile) {
    return scanImpl(lines.size(), scriptName, isHeaderFile,
                    [&lines](int i) -> const QString& { return lines.at(i); },
                    [&lines, &fileName](int i) { return ScriptPosition(fileName, i + 1, 1); });
}

// ---------------------------------------------------------------------------
// 声明 -> 变量表（唯一实现）
// ---------------------------------------------------------------------------
void registerDeclarationText(VariableTable& variables, ParseDiagnostics& diagnostics,
                             VariableStorage* storage, const QString& rawLine,
                             const ScriptPosition& position, const QString& currentFunction,
                             const QString& scopeKey, bool isHeaderFile) {
    QString s = rawLine.trimmed();
    if (!s.startsWith(QLatin1Char('#'))) return;
    s = s.mid(1);

    int i = 0;
    while (i < s.size() && !s.at(i).isSpace()) ++i;
    const QString directive = s.left(i).toUpper();
    const QString rest = loadpass::stripDirectiveComment(s.mid(i)).trimmed();

    // 指令 -> 语义：唯一映射（classifySharpDirective，扫描器也用同一份）。
    const SharpDirective kind = classifySharpDirective(directive);
    if (!kind.isDeclaration) return;   // 其它 # 行（#FUNCTION/#SINGLE/…）不在此处理
    const bool isStr = kind.isStr;
    const bool isGlobal = kind.isGlobal;
    const bool isPrivate = kind.isPrivate;

    if (rest.isEmpty()) {
        diagnostics.add(DiagSeverity::Warning, DiagCode::kSharpLine,
                        position.filename, position.lineNumber, position.column, 0,
                        QCoreApplication::translate("ParseDiagnostics", "#%1 缺少变量名").arg(directive));
        return;
    }

    const int priorDiagnostics = diagnostics.size();
    // 去重键是本次调用的局部集合（同一位置只报一次；不读共享容器）。
    QSet<QString> reportedNumbers;
    diagnoseIntegerLiterals(rest, position.filename, position.lineNumber,
                            position.column + qMax(0, int(rawLine.indexOf(rest))), diagnostics,
                            reportedNumbers);
    if (diagnostics.size() != priorDiagnostics) return; // 错误初值不得登记成 0。
    try {
        const UserDefinedVariableData d =
            UserDefinedVariableData::create(rest, isStr, /*isPrivate=*/true, position);

        // CONST 声明的初值即常数（供 #DIM 维数引用）。
        // **归属作用域**：与 decl.scope 同一判据（GLOBAL 关键字 / 头文件文件级 /
        // 不在任何函数内 = 广域；否则是该函数的**私有常量**）。
        // 私有常量只在声明它的函数内可见（对齐 C#：函数内 #DIM 走
        // LogicalLine.AddPrivateVariable，查询经 GetVariableToken 只查当前函数）。
        const QString constOwner = (isGlobal || d.global || isHeaderFile || scopeKey.isEmpty())
                                       ? QString() : scopeKey;
        if (d.isConst) {
            // 非常量字面量的初值（`#DIM CONST OBJ_ID_LAST = 人物数量上限`）：
            // 按字面量解析只会得到 0（曾因此 INRANGE(…, OBJ_ID_LAST) 恒假，
            // eraTW 的 EXISTOBJ 抛 THROW），而声明顺序又不可靠（跨文件！），
            // 所以保留表达式原文，交给求值期**惰性**计算。
            const QStringList valueExprs = constValueExpressions(rest);
            const bool allLiteral = !valueExprs.isEmpty()
                && std::all_of(valueExprs.begin(), valueExprs.end(), [](const QString& v) {
                       qint64 n = 0;
                       return parseIntegerLiteral(v.trimmed(), n);
                   });
            if (d.typeIsStr) {
                if (d.defaultStr.size() > 1) {
                    // 字符串常数**数组**（`#DIMS CONST X, N = s0, s1, …`，
                    // eraTW 的 DISP_MEMO）：逐下标提供，此前只存第一个值。
                    variables.setConstStrArray(d.name, d.defaultStr, constOwner);
                } else if (!d.defaultStr.isEmpty()) {
                    variables.setConstStr(d.name, d.defaultStr.first(), constOwner);
                }
            } else if (!allLiteral && !valueExprs.isEmpty()) {
                variables.setConstExprs(d.name, valueExprs, constOwner);
            } else if (valueExprs.isEmpty()) {
                // `#DIM CONST X, N =` + 紧随其后的 `{ … }` 多行初值块：
                // 本移植尚未支持该写法（常量数组的初值会丢），明确留痕。
                if (d.dimension > 1)
                    qWarning() << "[未完成] #DIM CONST 的多行初值块（{ }）尚未实现:"
                               << d.name << position.toString();
            } else if (!d.defaultInt.isEmpty()) {
                // 常数**数组**（`#DIM CONST NAME, N = v0, v1, …`）也要存下来，
                // 供 `NAME:i` 的下标访问使用；标量常数取第一个值
                variables.setConstArray(d.name, d.defaultInt, constOwner);
            }
        }

        VariableDecl decl;
        decl.name = d.name;
        decl.type = d.typeIsStr ? OperandType::Str : OperandType::Int;
        // 全局判定：GLOBAL 关键字 / 头文件文件级 / 不在任何函数内
        decl.scope = (isGlobal || d.global || isHeaderFile || scopeKey.isEmpty())
                         ? VarScope::Global : VarScope::Local;
        // function 只保留「函数名」这个事实（形参/事件/RETURNF/展示）；
        // 私有域的唯一键是 scopeKey（段身份文本）。
        decl.function = (decl.scope == VarScope::Local) ? currentFunction : QString();
        decl.scopeKey = (decl.scope == VarScope::Local) ? scopeKey : QString();
        decl.dimension = d.dimension;
        decl.lengths = d.lengths;
        decl.lengthExprs = d.lengthExprs;
        decl.isPrivate = isPrivate || !isGlobal;
        decl.isConst = d.isConst;
        decl.isReference = d.reference;
        decl.isCharaData = d.charaData;
        // 随 SAVEGLOBAL/LOADGLOBAL 持久化到 save_global.dat 的用户变量：
        // `#DIM SAVEDATA GLOBAL X`（C# IsGlobal && IsSavedata）与 `#DIM GLOBAL X`
        //（ecd/docs「全局变量随 SAVEGLOBAL/LOADGLOBAL 往返」）都算。
        // 注意 `#DIM GLOBAL X` 的 GLOBAL 写在变量名前（d.global），与
        // `#GLOBAL X` 指令（isGlobal）两种写法都要认。
        decl.isSavedata = d.save;
        decl.isGlobal = d.global || isGlobal;
        decl.isGlobalSave = decl.isSavedata && decl.isGlobal;

        // 用户 `#DIM(S) CHARADATA`：登记为角色数据变量，供读写路径按
        // (角色号, 元素下标) 存取（否则元素互相覆盖，表现为「变量似乎不可变」）。
        if (decl.isCharaData && !decl.isReference && storage) {
            storage->registerCharaDataVariable(
                decl.name, decl.type == OperandType::Str, decl.dimension);
        }
        if (!d.isConst) {                 // 初值（非 CONST）：进入函数/装载时写入
            decl.defaultInt = d.defaultInt;
            decl.defaultStr = d.defaultStr;
        }

        // 重复声明（first-wins）：与 C# 一样，全局变量重名一律告警（类型/维度是否
        // 一致都不影响后续使用，但重名通常意味着脚本有误）；函数局部重名在真实
        // 游戏里很常见（同名 @label 被口上补丁覆盖），保持静默。
        const VariableTable::DeclStatus status = variables.addChecked(decl);
        if (status != VariableTable::DeclStatus::Added && decl.scope == VarScope::Global) {
            diagnostics.add(DiagSeverity::Warning, DiagCode::kDeclError,
                        position.filename, position.lineNumber, position.column, 0,
                              QCoreApplication::translate("ParseDiagnostics", "全局变量 %1 重复定义").arg(decl.name));
        }
    } catch (const std::exception& e) {
        diagnostics.add(DiagSeverity::Error, DiagCode::kDeclError,
                        position.filename, position.lineNumber, position.column, 0,
                          QCoreApplication::translate("ParseDiagnostics", "#%1 声明错误：%2 (%3)")
                              .arg(directive, QString::fromUtf8(e.what()), rest));
    }
}

QString scopedVariableDeclText(const QString& rawLine) {
    const loadpass::ScopedVarSpec spec = loadpass::parseScopedVarSpec(rawLine);
    if (!spec.ok) return QString();
    QString declText = (spec.isStr ? QStringLiteral("#DIMS ") : QStringLiteral("#DIM ")) + spec.name;
    for (const QString& l : spec.lengths) declText += QStringLiteral(", ") + l;
    return declText;
}

void registerDeclaration(VariableTable& variables, ParseDiagnostics& diagnostics,
                         VariableStorage* storage, const DeclarationSite& site) {
    if (site.kind == DeclarationKind::SharpDirective) {
        registerDeclarationText(variables, diagnostics, storage, site.rawLine, site.position,
                                site.function, site.scopeKey, site.isHeaderFile);
        return;
    }
    // VARI/VARS：语句位置的声明。函数外没有可绑的标签（C# 的 ParentLabelLine），
    // 按「不登记」处理 —— 告警由预登记路径给出（见 EraParseTable::registerScopedVariableLine）。
    if (site.function.isEmpty()) return;
    const QString declText = scopedVariableDeclText(site.rawLine);
    if (declText.isEmpty()) return;
    registerDeclarationText(variables, diagnostics, storage, declText, site.position, site.function,
                            site.scopeKey, /*isHeaderFile=*/false);
}

Scan declareScriptVariables(VariableTable& variables, ParseDiagnostics& diagnostics,
                            const QList<SourceLine>& lines, const QString& scriptName,
                            bool isHeaderFile) {
    Scan scan = scanScript(lines, scriptName, isHeaderFile);
    for (const DeclarationSite& site : scan.declarations)
        registerDeclaration(variables, diagnostics, nullptr, site);
    return scan;
}

}  // namespace declscan
