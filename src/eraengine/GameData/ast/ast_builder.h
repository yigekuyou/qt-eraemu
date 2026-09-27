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
#ifndef AST_AST_BUILDER_H
#define AST_AST_BUILDER_H

#include <functional>
#include <QString>
#include <QStringList>
#include "logical_line.h"

// 把表达式文本归约为 AST 的回调（由 EraParseTable 的 m_astCache 提供，
// 保证同一表达式只解析一次并跨脚本共享）。
using AstResolver = std::function<QSharedPointer<ExpressionNode>(const QString&)>;

// ---------------------------------------------------------------------------
// AstBuilder —— 行 -> 完整 AST（LogicalLine）
//
// 对齐 C# Emuera 的前端两步：
//   1) LexicalAnalyzer.Analyse         —— 行 -> WordCollection    (tokenize)
//   2) LogicalLineParser.ParseLine/... —— 归类 + 归约实参 -> LogicalLine (build)
//
// 归约后的实参即表达式 AST（IOperandTerm 等价），整个程序由此得到一棵
// 完整、扁平的 AST（逻辑行数组 + 行号控制流）。
// ---------------------------------------------------------------------------
class AstBuilder {
public:
    // 行 -> token 序列（对齐 C# LexicalAnalyzer）。
    static WordCollection tokenize(const QString& line);

    // 行 -> LogicalLine（归类、归约实参、预解析条件）。
    static LogicalLine build(const QString& rawLine,
                             const ScriptPosition& position,
                             const AstResolver& resolve);

private:
    // 顶层赋值切分（"="/"+="/...，含 "'="）。命中返回 true 并输出 lhs/op/rhs。
    static bool splitAssignment(const QString& line, QString& lhs, QString& op, QString& rhs);

    // 在顶层按空白/逗号切分操作数（尊重引号与括号嵌套）。
    // splitWhitespace=true 时按空白+逗号切分；false 时仅按顶层逗号切分
    static QStringList splitOperands(const QString& text, bool splitWhitespace = true);

    // 指令是否使用整行操作数作为条件表达式。
    static bool isConditionInstruction(const QString& upperName);

    // PRINT 族参数形态（对齐 C# PRINT_Instruction 后缀扫描）
    enum class PrintArgMode { NotPrint, Literal, StrExpression, FormStr, PrintV };
    static PrintArgMode classifyPrintArg(const QString& upperName);

    // 指令的操作数是否为格式化串（StrForm）：整行按文本 + {expr}/%expr% 解析。
    static bool isStrFormInstruction(const QString& upperName);

    // 首操作数是否为「标签名」（CALL/CALLFORM/CALLF/TRYCALL*/JUMP*/BEGIN）：
    // 目标不做表达式归约（否则 NAME_%X%_K30(...) 会被误判为函数调用）。
    static bool isCallFamilyInstruction(const QString& upperName);
};

#endif // AST_AST_BUILDER_H
