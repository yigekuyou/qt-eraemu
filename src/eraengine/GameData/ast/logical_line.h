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
#include <memory>
#include <optional>

#include "expression_ast.h"
#include "runner_instruction.h"
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
        // **不能用链式 .arg()**：filename 在 Android 上是带 %2F/%3A 转义的 content://
        // URI（脚本名=URI），.arg(lineNumber)/.arg(column) 会把 URI 里的 %2/%3 当
        // 占位符吞掉（真机实测：%2F→"<行号>F"、%3A→"<列号>A"）；桌面路径里若有
        // 字面 %2/%3 同样中招。见 parse_diagnostic.h 的 positionText() 同因。
        return filename + QLatin1Char(':') + QString::number(lineNumber) + QLatin1Char(':')
               + QString::number(column);
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
    // 写成空的实参位（`CALLF F(, "x", y)` / `CALL F, a,,b`）。
    // 必须**占位**而不是丢弃：丢弃后后续实参会整体左移，形参全部错位
    //（eraTW 的 `CALLF OBJNAME_TO_ID(, "RESET", CLASS_NAME)` 因此把 "RESET"
    // 当成 ARG 传进去，RESET 分支永不执行）。语义 = C# 的实参 null
    //（UserDefinedFunctionArgument.SetTransporter 对 null 直接 continue）。
    bool                           omitted = false;
    QSharedPointer<ExpressionNode> ast;          // 归约后的表达式 AST（可空）

    // 命令 AST 专用实数常量；装载期校验/转换一次，不参与整数表达式。
    std::optional<double> realValue;
    // 注意：CALL 的「调用者上下文已求值」运输值**不在 Operand 里**。
    // 它只属于 doCallLine 的一次性传输（见 ScriptRunner::bindArguments 的
    // snapshots 参数）：装载后的 AST 操作数永远不会有值，把 QVariant 内联在
    // 每个 Operand（eraTW 约 377 万个槽位）上会白占 32 B/槽 ≈ 120 MB。

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
    // <img srcb='...'>：按钮选中态替换图（C# ConsoleImagePart.ButtonResourceName）
    QString imageAlt;
    PrintStyle style;
    QString buttonValue;
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

// Only load-time templates own interpolation ASTs. Runtime print data above is pure data.
struct CompiledPrintTemplatePart : PrintTemplatePart {
    QSharedPointer<ExpressionNode> expression;
    int occurrenceId = -1;
    QHash<QString, QSharedPointer<ExpressionNode>> attributes;
    QStringList attributeOrder;
};
struct CompiledPrintTemplate {
    QList<CompiledPrintTemplatePart> parts;
    QList<StrFormPart> rawParts;
    int alignment = 0;
    bool noWrap = false;
};

// ---------------------------------------------------------------------------
// SELECTCASE 的 CASE 臂（perf 缓存）
//
// 此前 caseMatches 每次执行都重新 splitCaseArgs + expressionAst —— eraTW
// 地图这类「每字符一个 SELECTCASE × 25 个 CASE」的热路径上是最大热点。
// CASE 结构在装载期构建，markLoadComplete 按 ownerFunction 重绑；执行只消费节点。
// ---------------------------------------------------------------------------
struct CaseClause {
    enum class Kind : quint8 { Equal, IsOp, Range };
    Kind kind = Kind::Equal;
    QString op;                                   // IsOp：<= >= == != < >
    QString textA;                                // Equal：比较文本；IsOp：操作数；Range：左端
    QString textB;                                // Range：右端
    QSharedPointer<ExpressionNode> astA;          // 装载期表达式节点（缺失为错误）
    QSharedPointer<ExpressionNode> astB;
    // perf：CASE 臂是**纯字符串字面量**（如 "林"）时预存字面值 —— 逐字符
    // SELECTCASE 时免去每臂 AST 求值 + QVariant 分配（eraTW 地图最大 CASE 热点）。
    bool    isSimpleStr = false;
    QString strLiteral;
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

// 冷字段（按需分配）
//
// 这三个字段在 eraTW（2,111,136 行）上的实测使用行数：
//   columnDefaults  0 行；labelArgs / labelDefaults 各 10,166 行（仅带形参的 @label）
// 而它们内联在每一行上要付 3 × 24 B = 72 B（约 152 MB @2.11M 行）。
// 搬进 LineExtras 后，不用这些字段的行只付一个空 shared_ptr（16 B）。
//
// 副本语义：extras 是 shared_ptr + **写时复制**（见 ensureExtras）——
// 拷贝 LogicalLine 时共享同一份 extras，任一副本一旦「写」就各自分离。
// 这与原本 QString/QList 的 COW 语义一致：拷贝后互不影响。
struct LineExtras {
    QList<QSharedPointer<ExpressionNode>> columnDefaults; // ordered DEFAULT expressions
    QStringList labelArgs;        // 形参名 @NAME(a, b)（用户自定义函数）
    QStringList labelDefaults;    // 同位置的默认值；无默认值为空串
    // 装载期错误文本：总使用行数约 0（eraTW 实测 0 行），内联在每行上要付 24 B
    //（≈48 MiB @2.11M 行）。写侧只在**错误/告警分支**里发生，不会懒建成灾。
    QString errMes;
    // PRINTDATA/STRDATA 段的装载期索引（EraParseTable 一次构建，运行期只读）：
    // 各「段」的行号列表 + 对应 ENDDATA 行号。eraTW 只有 2,510 行用得上，
    // 但内联要付 24 B/行。写侧按「先收集到局部容器，最后只对有段的行写入」。
    QList<QList<int>> printDataGroups;
    int printDataEndLine = -1;
};

struct LogicalLine {
    // ---------------------------------------------------------------------
    // 字段排列 = 「按 sizeof 从大到小聚簇」，只为消 padding（无聚合初始化，
    // 无顺序依赖）。改前 520 B，其中 46 B 是纯对齐空洞；本排列落到 440 B。
    // 新增字段请按同样的聚簇插入（24 B 容器 → 16 B 指针 → int → bool）。
    // ---------------------------------------------------------------------
    ScriptPosition position;                    // 32

    // 标签（FunctionLabel / GotoLabel）
    QString        labelName;                   // 24
    // 形参名 / 默认值：冷字段，见 LineExtras（labelArgs() / labelDefaults()）

    // 指令（Instruction）
    QString        functionName;     // 大写指令名
    QString        assignOperator;   // "=" / "+=" …（C# InstructionLine.AssignOperator）
    // 原始 token 与文本（C# argprimitive / WordCollection）
    QString        raw;
    QString        ownerFunction;    // 所属函数标签名（形参/事件分组/RETURNF；**不是**作用域键）
    // 私有作用域身份 = 段身份 (脚本, 段起点行) 的规范化文本（装载期唯一写入，
    // 见 eravar::segmentKeyText）。空串 = 顶层（无私有域）。
    QString        scopeKey;
    // 段起点行（@ 标签行；文件头段 = -1）。运行期 GOTO/段判定只读这个字段。
    int            segmentStart = -1;

    QList<Operand> arguments;
    // DT_COLUMN_OPTIONS 的 DEFAULT 表达式表：冷字段，见 LineExtras（columnDefaults()）
    QList<QSharedPointer<ExpressionNode>> assignmentAsts; // load-time RHS list
    // CASE 臂解析缓存（见 CaseClause 注释；执行单线程，行内容装载后不可变）
    mutable QList<CaseClause> caseCache;

    TypedArgument  argument;                    // 类型化参数（C# InstructionLine.Argument）
    QSharedPointer<ExpressionNode> condition;   // IF/SIF/ELSEIF/WHILE/REPEAT 条件
    QSharedPointer<CompiledPrintTemplate> printTemplate;

    // 扁平控制流（行号索引，替代 C# 的 NextLine / JumpTo 指针）
    int  lineIndex      = -1;
    int  nextLine       = -1;
    int  jumpTo         = -1;

    LineKind       kind = LineKind::Null;
    RunnerInstruction runnerInstruction = RunnerInstruction::Other; // 装载期指令分派
    // 「函数语句」标记（对齐 C# LogicalLineType.Function）：
    // 一行以**内置函数名**开头、又不是已知指令/赋值时，整行是一次函数调用，
    // 返回值写 RESULT（整型）/ RESULTS:0（字符串）。eraTW 里大量存在：
    //   GETMILLISECOND / CURRENTREDRAW / GETTIME / REPLACE LOCALS, "a", "b" …
    bool           isFunctionCall = false;

    // 条件 AST（condition）的变量引用是否需要执行位置（作用域）。
    // **唯一写入点**：收尾定型 applyVariableTypes 对 line.condition 的同一趟
    // walkApply（与 accessKind 同趟）。运行期 evaluateCondition 只读：
    //   true  -> 取 executionScopeId()（Param/Chara/Const/Dynamic 按名解析要 scope）
    //   false -> 直接用 eravar::kGlobalScope（键自足/忽略 scope 的 kind）
    bool conditionNeedsScope = false;

    // 诊断（errMes 是冷字段，见 LineExtras；isError 常与热路径判定同行，留内联）
    bool    isError = false;

    // 构建期未能定型的标记（收尾只处理这些，避免全量重解析）：
    //   needsCompletion —— 行内含类型未知/解析失败的表达式（收尾时再补）。
    // 「左值类型未知 ⇒ RHS 待收尾定型」那第三态（rhsPending）已删：声明视图是
    // **构建入口的前置步骤**（ErbLoader 的预扫描 / EraParseTable::buildScriptAst
    // 的扫描，判据与登记实现各只有一份），所以脚本路径上每个赋值的左值类型在
    // 构建期就有答案，FORM 与表达式在构建期一次定死，收尾不再重解。
    bool    needsCompletion = false;

    // CASE 臂解析缓存就绪标记（见 caseCache 注释）
    mutable bool caseCacheReady = false;

    // ---- 冷字段（见 LineExtras）：空 extras 与旧的空容器完全等价 ----
    // 读访问器返回引用；无 extras 时返回进程级静态空容器（只读，勿写入）。
    const QList<QSharedPointer<ExpressionNode>>& columnDefaults() const {
        static const QList<QSharedPointer<ExpressionNode>> kEmpty;
        return m_extras ? m_extras->columnDefaults : kEmpty;
    }
    const QStringList& labelArgs() const {
        static const QStringList kEmpty;
        return m_extras ? m_extras->labelArgs : kEmpty;
    }
    const QStringList& labelDefaults() const {
        static const QStringList kEmpty;
        return m_extras ? m_extras->labelDefaults : kEmpty;
    }
    const QString& errMes() const {
        static const QString kEmpty;
        return m_extras ? m_extras->errMes : kEmpty;
    }
    const QList<QList<int>>& printDataGroups() const {
        static const QList<QList<int>> kEmpty;
        return m_extras ? m_extras->printDataGroups : kEmpty;
    }
    int printDataEndLine() const { return m_extras ? m_extras->printDataEndLine : -1; }

    // 写访问器：懒建 extras；若 extras 被其它副本共享（拷贝后），先分离再写，
    // 保持与旧结构（QString/QList 的 COW）一致的值语义。
    LineExtras& ensureExtras() {
        if (!m_extras) {
            m_extras = std::make_shared<LineExtras>();
        } else if (m_extras.use_count() > 1) {
            m_extras = std::make_shared<LineExtras>(*m_extras);
        }
        return *m_extras;
    }
    QList<QSharedPointer<ExpressionNode>>& mutableColumnDefaults() {
        return ensureExtras().columnDefaults;
    }
    QStringList& mutableLabelArgs() { return ensureExtras().labelArgs; }
    QStringList& mutableLabelDefaults() { return ensureExtras().labelDefaults; }
    QString& mutableErrMes() { return ensureExtras().errMes; }
    QList<QList<int>>& mutablePrintDataGroups() { return ensureExtras().printDataGroups; }
    int& mutablePrintDataEndLine() { return ensureExtras().printDataEndLine; }

    // 「清空」不能懒建：无 extras 时本来就没有值，直接 no-op。
    void clearErrMes() {
        if (m_extras) m_extras->errMes.clear();
    }
    void clearPrintDataIndex() {
        if (m_extras) {
            m_extras->printDataGroups.clear();
            m_extras->printDataEndLine = -1;
        }
    }

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

private:
    // 冷字段的按需存储；空指针 = 该行完全不用 columnDefaults / labelArgs /
    // labelDefaults / errMes / printDataGroups。拷贝 LogicalLine 时共享
    //（写时分离，见 ensureExtras）。
    std::shared_ptr<LineExtras> m_extras;
};

Q_DECLARE_METATYPE(LogicalLine)

#endif // AST_LOGICAL_LINE_H
