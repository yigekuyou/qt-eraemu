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
#ifndef EPROTO_ERB_ANALYZER_H
#define EPROTO_ERB_ANALYZER_H

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "eproto_export.h"

// ---------------------------------------------------------------------------
// ErbAnalyzer —— 语义**只**来自 eraemu 自己的前端（GameData/ast/AstBuilder +
// Content/ErbPreprocessor），本头文件因此**不**包含任何引擎头文件。
//
// 为什么刻意把引擎类型挡在 .cpp 里：引擎的 AST 头（GameData/ast/logical_line.h）
// 直接 `#include <QColor>`，一旦在公共头里出现，任何消费者（编辑器插件、别的
// QML/C++ 应用）都被迫链接 QtGui —— 一个「LSP/MCP 前端」不该拖一整个 GUI 库。
// 这里只对外暴露**派生数据**（诊断/标签/行数），引擎的 LogicalLine 留在实现里。
// ---------------------------------------------------------------------------

namespace eproto {

// LSP 口径的 0 起行列诊断（从引擎的 ParseDiagnostics 派生）
struct EPROTO_API ErbDiagnostic {
    int     line = 0;
    int     startCol = 0;
    int     endCol = 0;
    QString severity;   // "error" | "warning" | "information"
    QString code;       // 引擎 DiagCode（如 unknown-instruction / expr-parse）
    QString message;
    QString snippet;
};

struct EPROTO_API ErbSymbol {
    QString name;
    int     line = 0;                 // 0 起
    bool    isGotoLabel = false;      // $label（GotoLabel）而非 @label
};

// 一份 ERB 文档的解析结果（全部为派生数据，不含引擎类型）。
struct EPROTO_API ErbDocument {
    QString              fileName;             // 解析时用的名字（路径或 uri）
    QString              text;                 // 源文本
    int                  lineCount = 0;        // eraemu AST 的逻辑行数
    QList<ErbDiagnostic> attributed;           // 已归属到物理行的诊断（LSP 口径）
    QList<ErbSymbol>     symbols;              // 从 AST 取的标签
    QStringList          preprocessWarnings;   // ErbPreprocessor 的告警
    bool                 parsed = false;
};

class EPROTO_API ErbAnalyzer {
public:
    ErbAnalyzer() = default;

    // 用 eraemu 自己的前端解析一段 ERB 文本（ErbPreprocessor + AstBuilder）。
    [[nodiscard]] ErbDocument parse(const QString& text, const QString& fileName) const;

    // Analyze an in-memory workspace through EraParseTable::finalizeParse().
    // Keys are document identities (paths or URIs); values are source text.
    // Headers are loaded first, then scripts, with deterministic ordering.
    // No files are read and no script code is executed. Includes cross-file
    // declarations, function binding, argument/type and preprocessing diagnostics.
    [[nodiscard]] QMap<QString, ErbDocument> analyzeWorkspace(
        const QMap<QString, QString>& sources) const;

    // ---- 引擎登记表（来自 eraemu 自己的 constexpr 表）----
    [[nodiscard]] static QStringList instructionNames();   // kInstructionSpecs
    [[nodiscard]] static QStringList functionNames();      // kBuiltinFunctions
    [[nodiscard]] static QString instructionSpec(const QString& name);  // 参数形态
    [[nodiscard]] static QString functionSpec(const QString& name);     // 返回类型/参数
};

}  // namespace eproto

#endif  // EPROTO_ERB_ANALYZER_H
