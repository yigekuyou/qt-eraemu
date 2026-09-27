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

    // ---- PRINT 族参数形态（对齐 C# PRINT_Instruction 的后缀扫描）----
    //   PRINT…(V)     -> PrintV        逗号分隔的整数值，直接拼接
    //   PRINT…(S)     -> StrExpression 字符串表达式
    //   PRINT…(FORMS) -> StrExpression 字符串表达式（结果再按格式串展开）
    //   PRINT…(FORM)  -> FormStr       格式化串（文本 + {…}/%…%）
    //   PRINT…(其它)  -> Literal       整行**字面文本**（不是表达式！）
    enum class PrintArgMode { NotPrint, Literal, StrExpression, FormStr, PrintV };
    struct PrintArgInfo {
        PrintArgMode mode = PrintArgMode::NotPrint;
        bool newline   = false;   // L（或 W）
        bool waitInput = false;   // W：换行后等待输入
        bool clearPad  = false;   // C / LC：按 PRINTC 的定宽列布局打印
        bool padLeft   = false;   // C（右对齐补左）；LC 时 false（左对齐补右）
        bool forms     = false;   // FORMS：求值结果再当格式串展开
        bool debug     = false;   // D 后缀
    };
    static PrintArgInfo printInfo(const QString& upperName);

    // 指令名是否属于 PRINT 族（含 PRINTPLAIN*）
    static bool isPrintFamily(const QString& upperName) {
        return printInfo(upperName).mode != PrintArgMode::NotPrint;
    }

    // 「这是不是一条指令名」——用于区分
    //   `ステージ:(ステージ幅 - 1):(LOCAL:0) = 1`（赋值，允许 LHS 含空白）
    //   `PRINTFORML a = b`            （指令，LHS 里那个 = 只是文本）
    // 对齐 C# LogicalLineParser：先查函数名表，命中即为命令文，否则按赋值解。
    static bool isKnownInstructionName(const QString& upperName);

private:
    // 顶层赋值切分（"="/"+="/...，含 "'="）。命中返回 true 并输出 lhs/op/rhs。
    static bool splitAssignment(const QString& line, QString& lhs, QString& op, QString& rhs);

    // 在顶层按空白/逗号切分操作数（尊重引号与括号嵌套）。
    // splitWhitespace=true 时按空白+逗号切分；false 时仅按顶层逗号切分
    static QStringList splitOperands(const QString& text, bool splitWhitespace = true);

    // 指令是否使用整行操作数作为条件表达式。
    static bool isConditionInstruction(const QString& upperName);

    static PrintArgMode classifyPrintArg(const QString& upperName);

    // 指令的操作数是否为格式化串（StrForm）：整行按文本 + {expr}/%expr% 解析。
    static bool isStrFormInstruction(const QString& upperName);

    // 首操作数是否为「标签名」（CALL/CALLFORM/CALLF/TRYCALL*/JUMP*/BEGIN）：
    // 目标不做表达式归约（否则 NAME_%X%_K30(...) 会被误判为函数调用）。
    static bool isCallFamilyInstruction(const QString& upperName);
};

#endif // AST_AST_BUILDER_H
