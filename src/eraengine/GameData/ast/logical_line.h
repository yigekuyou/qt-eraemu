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
#ifndef AST_LOGICAL_LINE_H
#define AST_LOGICAL_LINE_H

#include <QString>
#include <QList>
#include <QStringList>
#include <QMetaType>
#include <QSharedPointer>

#include "expression_ast.h"
#include "word.h"

// ---------------------------------------------------------------------------
// 完整 AST 的结果类型：LogicalLine
//
// 对齐 C# Emuera `GameProc/LogicalLine.cs`：
//   InstructionLine / FunctionLabelLine / GotoLabelLine / NullLine / InvalidLine
// 在 C++ 里拍平为一个带标签(kind)的值类型，字段语义与 C# 对应：
//
//   C# InstructionLine.Function      -> LogicalLine::functionName
//   C# InstructionLine.Argument      -> LogicalLine::arguments（IOperandTerm[] 等价）
//   C# InstructionLine.AssignOperator-> LogicalLine::assignOperator
//   C# InstructionLine.JumpTo        -> LogicalLine::jumpTo        （扁平：行号而非指针）
//   C# InstructionLine.JumpToEndCatch-> LogicalLine::jumpToEndCatch
//   C# LogicalLine.NextLine          -> LogicalLine::nextLine      （扁平：行号）
//   C# LogicalLine.ParentLabelLine    -> LogicalLine::parentLabelLine（扁平：行号）
//   C# FunctionLabelLine.LabelName   -> LogicalLine::labelName
//   C# FunctionLabelLine.PopRowArgs  -> LogicalLine::arguments
//
// 「拍平」体现为：不再有指针树 / 双向链表，取而代之的是行号索引
// （lineIndex / nextLine / jumpTo），仍保持与 C# 相同的字段语义。
// ---------------------------------------------------------------------------

// 脚本位置（原 GameProc/script_line.h 迁入）
struct ScriptPosition {
    QString filename;
    int lineNumber = 0;
    int column = 0;

    ScriptPosition() = default;
    ScriptPosition(const QString& file, int line, int col = 0)
        : filename(file), lineNumber(line), column(col) {}

    QString toString() const {
        return QString("%1:%2:%3").arg(filename).arg(lineNumber).arg(column);
    }
};
Q_DECLARE_METATYPE(ScriptPosition)

// 逻辑行的类别（对应 C# 的 LogicalLine 子类）
enum class LineKind {
    Null,           // 空行 / 注释（C# NullLine / 被跳过的行）
    Invalid,        // 无法解析（C# InvalidLine）
    Instruction,    // 命令文（C# InstructionLine）
    FunctionLabel,  // @label（C# FunctionLabelLine）
    GotoLabel,      // $label（C# GotoLabelLine）
    Preprocessor    // # 指令（C# LogicalLineParser.ParseSharpLine 的输入）
};
Q_DECLARE_METATYPE(LineKind)

// 一个已归约的操作数（对应 C# ExpressionParser 归约出的 IOperandTerm，
// 同时保留原始文本以兼容执行侧的字符串用法）。
struct Operand {
    QString                        raw;          // 原始文本
    bool                           isString = false;   // 字符串字面量
    bool                           isVariable = false; // %VAR% / $VAR 形式
    QSharedPointer<ExpressionNode> ast;          // 归约后的表达式 AST（可空）

    Operand() = default;
    explicit Operand(const QString& text) : raw(text) {}

    bool hasAst() const { return !ast.isNull(); }
};

// ---------------------------------------------------------------------------
// 语句参数类型（对齐 C# GameProc/Function/Argument*.cs 的 40 个 Argument 子类）
//
// 这里用「一种参数种类 + 归约后的表达式」表达同样的信息：
//   ExpressionArgument   -> Expression / Expressions
//   INT_EXPRESSION       -> IntExpression
//   SP_CALL / SP_CALLFORM-> Call / CallForm
//   SP_FOR_NEXT          -> ForNext
//   CASE                 -> Case
//   PRINTDATA            -> PrintData
//   VOID                 -> Void
// ---------------------------------------------------------------------------
enum class ArgKind : quint8 {
    Void,           // 无参数
    IntExpression,  // 单整型表达式（IF/SIF/ELSEIF/WHILE/REPEAT/…）
    StrExpression,  // 单字符串表达式
    Expression,     // 单表达式（任意类型）
    Expressions,    // 逗号/空白分隔的多个表达式
    Call,           // CALL name(args...)
    CallForm,       // CALLFORM 表达式名
    ForNext,        // FOR var, start, end[, step]
    Case,           // SELECTCASE 的 CASE 参数
    PrintData,      // PRINTDATA 多段
    // ---- 其余 C# FunctionArgType 族（至少完成解析/分类）----
    Var,            // SP_VAR: <可変変数>
    VarSet,         // SP_SET/SP_SETS/SP_VAR_SET: <可変変数>, <式>[, 范围]
    Swap,           // SP_SWAP/SP_SWAPVAR
    Times,          // SP_TIMES: <数值变量>, <実数>
    Bar,            // SP_BAR: <数値>,<数値>,<数値>
    Power,          // SP_POWER: <可変変数>,<数値>,<数値>
    Bit,            // BIT_ARG: <可変変数>, <数値>*
    SortChara,      // SP_SORTCHARA
    ArrayControl,   // ARRAYCOPY/ARRAYSHIFT/ARRAYREMOVE/ARRAYSORT
    SaveData,       // SP_SAVEDATA: <数値>,<文字列式>
    Button,         // SP_BUTTON: <文字列式>,<数式>
    Split,          // SP_SPLIT: <文字列式>
    Color,          // SP_COLOR: <数値>[,<数値>,<数値>]
    GetInt,         // SP_GETINT: <可変数値変数>
    VarStr,         // VAR_STR: <可変変数>
    HtmlSplit,      // SP_HTMLSPLIT
    CallF,          // SP_CALLF/SP_CALLFORMF
    PrintV,         // SP_PRINTV: 复数数值/字符串
    Input,          // SP_INPUT/SP_INPUTS/SP_ONEINPUT(S)/SP_TINPUT(S)
    FormStr,        // FORM_STR(_NULLABLE)
    Raw             // 宽松：原样 token（未列入规范的指令）
};

struct TypedArgument {
    ArgKind kind = ArgKind::Raw;
    QList<Operand> operands;                          // 原始操作数（raw + ast）
    QList<Operand> params;                            // Call: name+实参；ForNext: var/start/end/step
    QList<QSharedPointer<ExpressionNode>> exprs;      // 归约后的表达式
    QList<Operand> cases;                             // Case
    int  minArgs = 0;
    int  maxArgs = -1;                                // -1 表示不限
    bool typeOk = true;                               // 解析期类型/个数校验结果
    QString typeError;

    [[nodiscard]] bool hasError() const { return !typeOk; }
};

struct LogicalLine {
    LineKind       kind = LineKind::Null;
    ScriptPosition position;

    // 标签（FunctionLabel / GotoLabel）
    QString        labelName;
    QStringList    labelArgs;        // 形参名 @NAME(a, b)（用户自定义函数）

    // 指令（Instruction）
    QString        functionName;     // 大写指令名
    QString        assignOperator;   // "=" / "+=" …（C# InstructionLine.AssignOperator）
    QList<Operand> arguments;
    TypedArgument  argument;                    // 类型化参数（C# InstructionLine.Argument）
    QSharedPointer<ExpressionNode> condition;   // IF/SIF/ELSEIF/WHILE/REPEAT 条件

    // 原始 token 与文本（C# argprimitive / WordCollection）
    WordCollection words;
    QString        raw;

    // 扁平控制流（行号索引，替代 C# 的 NextLine / JumpTo 指针）
    int  lineIndex      = -1;
    int  nextLine       = -1;
    int  jumpTo         = -1;
    int  jumpToEndCatch = -1;
    int  parentLabelLine = -1;
    QString ownerFunction;      // 所属函数标签（变量作用域判定，装载期填充）

    // 诊断
    bool    isError = false;
    QString errMes;

    bool isNull() const { return kind == LineKind::Null; }
    bool isInstruction() const { return kind == LineKind::Instruction; }
    bool isLabel() const {
        return kind == LineKind::FunctionLabel || kind == LineKind::GotoLabel;
    }

    // 大小写不敏感比较指令名（functionName 已大写）
    bool is(const char* upperName) const {
        return kind == LineKind::Instruction && functionName == QLatin1String(upperName);
    }

    QString describe() const {
        return QString("LogicalLine(%1 @ %2)").arg(raw).arg(position.toString());
    }
};
Q_DECLARE_METATYPE(LogicalLine)

#endif // AST_LOGICAL_LINE_H
