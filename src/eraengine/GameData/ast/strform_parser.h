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
#ifndef AST_STRFORM_PARSER_H
#define AST_STRFORM_PARSER_H

#include <functional>
#include <QString>
#include <QSharedPointer>
#include "expression_ast.h"

// ---------------------------------------------------------------------------
// StrForm 解析（对齐 C# Sub/LexicalAnalyzer.AnalyseFormattedString / StrForm）
//
//   "HP={A}/{B}%"  ->  StrForm( Text("HP=") Expr(A) Text("/") Expr(B) Text("%") )
//   \@ C ? A # B \@ -> If(C, StrForm("A"), StrForm("B"))
//
// 内嵌表达式用 ExprResolver 归约（复用 EraParseTable 的 AST 缓存）。
// ---------------------------------------------------------------------------
class StrFormParser {
public:
    using ExprResolver = std::function<QSharedPointer<ExpressionNode>(const QString&)>;

    // 解析整段格式化串；无内嵌表达式时也可返回（普通文本）
    [[nodiscard]] static QSharedPointer<StrFormNode> parse(const QString& text,
                                                           const ExprResolver& resolve, bool ignoreTripleSymbols = false);

    // \@ cond ? left # right \@ —— 传入不含 \@ 的内部文本，返回 If 节点
    // （C# LexicalAnalyzer.AnalyseYenAt / YenAtSubWord）
    [[nodiscard]] static QSharedPointer<ExpressionNode> parseYenAt(const QString& inner,
                                                                  const ExprResolver& resolve, bool ignoreTripleSymbols = false);

    // 是否含有内嵌表达式（用于判断要不要走 StrForm）
    [[nodiscard]] static bool hasForm(const QString& text);

    // 在 text[from, end) 中查找顶层（跳过引号/({[]嵌套）的字符 c；找不到返回 -1
    [[nodiscard]] static int findTopLevel(const QString& text, QChar c, int from, int end);
    // Exclusive end of a quoted string, @"FORM", or \@ conditional at start.
    // Returns start for ordinary characters. Shared by argument/clause scanners.
    [[nodiscard]] static int expressionSpanEnd(const QString& text, int start);
    // `%expr%` 的右端 `%`（跳过表达式中的括号、引号及 FORM）
    [[nodiscard]] static int findPercentEnd(const QString& text, int from);
};

#endif // AST_STRFORM_PARSER_H
