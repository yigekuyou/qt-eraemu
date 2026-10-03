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
#include <QColor>
#include <QHash>

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

// 通用打印属性：HTML_PRINT/PRINTFORM 的标签解析结果附着在 ERB 打印模板上，
// 不创建 Html AST 类型。无效颜色/空字体名表示系统默认样式。
struct PrintStyle {
    QColor color;
    QColor buttonColor;
    QString fontName;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
};

struct PrintTemplatePart {
    enum class Kind : quint8 { Text, Expression, Break, Image, Shape, Button, EndButton, Alignment, EndAlignment, NoWrap, EndNoWrap, Style, EndStyle };
    Kind kind = Kind::Text;
    QString text;
    QSharedPointer<ExpressionNode> expression;
    PrintStyle style;
    QString buttonValue;
    QSharedPointer<ExpressionNode> buttonValueExpression;
    QHash<QString, QSharedPointer<ExpressionNode>> attributes;
    QStringList attributeOrder;
    QString tooltip;
    int width = 0;
    int height = 0;
    QString shapeType;
    QList<int> shapeParams;
    bool lockedPosition = false;
    int x = -1;
    int y = -1;
};

struct PrintTemplate {
    QList<PrintTemplatePart> parts;
    int alignment = 0; // 0=left, 1=center, 2=right
    bool noWrap = false;
};

// ---------------------------------------------------------------------------
// SELECTCASE 的 CASE 臂（perf 缓存）
//
// 此前 caseMatches 每次执行都重新 splitCaseArgs + expressionAst —— eraTW
// 地图这类「每字符一个 SELECTCASE × 25 个 CASE」的热路径上是最大热点。
// CASE 行装载后不可变，解析结果在**首次匹配时**填充进 LogicalLine::caseCache，
// 之后直接复用（表达式 AST 解析需要 ownerFunction 作用域，装载期无法完全
// 预解析，所以惰性做一次）。
// ---------------------------------------------------------------------------
struct CaseClause {
    enum class Kind : quint8 { Equal, IsOp, Range };
    Kind kind = Kind::Equal;
    QString op;                                   // IsOp：<= >= == != < >
    QString textA;                                // Equal：比较文本；IsOp：操作数；Range：左端
    QString textB;                                // Range：右端
    QSharedPointer<ExpressionNode> astA;          // 预解析结果（可空 -> 运行期按文本求值）
    QSharedPointer<ExpressionNode> astB;
    // perf：CASE 臂是**纯字符串字面量**（如 "林"）时预存字面值 —— 逐字符
    // SELECTCASE 时免去每臂 AST 求值 + QVariant 分配（eraTW 地图最大 CASE 热点）。
    bool    isSimpleStr = false;
    QString strLiteral;
};

// ---------------------------------------------------------------------------
// 命令式字符串内置函数（STRLENS/STRLENSU/SUBSTRING/SUBSTRINGU 的**语句形式**）
// 实参解析缓存。这些指令（尤其 SUBSTRINGU）此前每次执行都要：line.raw 扫描 +
// mid/trimmed + split(',') + trimmed + expressionAst(QString) 查表 —— eraTW 地图
// 逐字符时是最大的热点之一（一次绘制上万次）。行内容装载后不可变，故首次执行
// 解析一次进 LogicalLine::strArgs，之后直接复用 AST。
// ---------------------------------------------------------------------------
struct StrBuiltinArgs {
    enum class Mode : quint8 { None, Substring, StrLen } mode = Mode::None;
    enum class Src : quint8 { Expr, Results, PlainName } src = Src::Expr;
    QString plainName;                            // Src::PlainName 的变量名 / 下标
    int     plainIndex = -1;
    QString sourceText;                           // Src::Expr 的文本回退（AST 为空时）
    QString startText;
    QString lengthText;
    QString exprText;                             // Mode::StrLen 的文本回退
    QSharedPointer<ExpressionNode> sourceAst;
    QSharedPointer<ExpressionNode> startAst;
    QSharedPointer<ExpressionNode> lengthAst;
    QSharedPointer<ExpressionNode> lenAst;
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
    QStringList    labelDefaults;    // 同位置的默认值；无默认值为空串

    // 指令（Instruction）
    QString        functionName;     // 大写指令名
    QString        assignOperator;   // "=" / "+=" …（C# InstructionLine.AssignOperator）
    // 「函数语句」标记（对齐 C# LogicalLineType.Function）：
    // 一行以**内置函数名**开头、又不是已知指令/赋值时，整行是一次函数调用，
    // 返回值写 RESULT（整型）/ RESULTS:0（字符串）。eraTW 里大量存在：
    //   GETMILLISECOND / CURRENTREDRAW / GETTIME / REPLACE LOCALS, "a", "b" …
    bool           isFunctionCall = false;
    QList<Operand> arguments;
    TypedArgument  argument;                    // 类型化参数（C# InstructionLine.Argument）
    QSharedPointer<ExpressionNode> condition;   // IF/SIF/ELSEIF/WHILE/REPEAT 条件

    // 原始 token 与文本（C# argprimitive / WordCollection）
    QString        raw;
    QSharedPointer<PrintTemplate> printTemplate;

    // 扁平控制流（行号索引，替代 C# 的 NextLine / JumpTo 指针）
    int  lineIndex      = -1;
    int  nextLine       = -1;
    int  jumpTo         = -1;
    QString ownerFunction;      // 所属函数标签（变量作用域判定，装载期填充）

    // 诊断
    bool    isError = false;
    QString errMes;

    // CASE 臂解析缓存（见 CaseClause 注释；执行单线程，行内容装载后不可变）
    mutable bool caseCacheReady = false;
    mutable QList<CaseClause> caseCache;

    // 命令式字符串内置函数实参缓存（见 StrBuiltinArgs 注释）
    mutable bool strArgsReady = false;
    mutable StrBuiltinArgs strArgs;

    // PRINTDATA 段的惰性解析缓存（见 execution_engine.cpp printDataFormLine 说明）：
    // printDataGroups = 各「段」的行号列表（DATAFORM/DATA 各自成段；DATALIST..ENDLIST
    // 之间的 DATA 并成一段）；printDataEndLine = 对应 ENDDATA 的行号（跳转目标 = +1）。
    mutable bool printDataReady = false;
    mutable QList<QList<int>> printDataGroups;
    mutable int printDataEndLine = -1;

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
