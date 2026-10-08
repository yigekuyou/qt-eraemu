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
#include <QSet>
#include <QString>
#include <QStringList>
#include "logical_line.h"
#include "function_types.h"   // 运行期扩展式中函数表（registerExtensionFunction）
#include "parse_diagnostic.h" // 结构化诊断（装载期分级告警）

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
    // resolveQuiet：赋值右值**临时**归约用的静默 resolver（可选；为空时回落
    // 到 resolve）。临时解析的失败是噪音 —— 字符串赋值随后由
    // applyStringAssignments() 以 StrFormParser 重新解释。
    // diagnostics：结构化诊断出口（可选）。未登记指令等以 DiagSeverity 分级 + code
    //              记录，供调用方统一汇总/按类统计（见 parse_diagnostic.h）。
    static LogicalLine build(const QString& rawLine,
                             const ScriptPosition& position,
                             const AstResolver& resolve,
                             const AstResolver& resolveQuiet = {},
                             ParseDiagnostics* diagnostics = nullptr);

    // CASE has its own IS / TO grammar, shared by loading and lazy cache restore.
    static void buildCaseClauses(const LogicalLine& line, const AstResolver& resolve);

    // ExtensionRegistry::reg() 登记的扩展语句名：解析期不再报「未识别的指令」
    // （扩展默认全启用，登记过的名字是已认识的扩展；只消警告，
    // 不影响命令文/赋值分类）。
    static void registerExtensionStatement(const QString& upperName) {
        s_extensionStatements.insert(upperName);
    }

    // ExtensionRegistry::regExpr() 登记的扩展式中函数：注入运行期「扩展函数表」
    // （function_types.h），解析期即可拿到返回类型/参数个数；求值 opcode 统一
    // BuiltinOp::Extension，实际求值住扩展侧（注册类持有的回调）。
    static void registerExtensionFunction(const QString& upperName, OperandType ret,
                                          int minArgs, int maxArgs) {
        registerExtensionFunctionSpec(upperName.toStdString(), ret, minArgs, maxArgs);
    }

    // 放宽**核心命令**的实参个数区间（EM 私家版拡張：GCLEAR 2/6 参等）——
    // 对齐 C# `argumentTypeArrayEx` 给同一个方法补第二个实参形态。
    // 原生表 constexpr 不变；放宽区间住扩展侧的 CoreArgWidenStore，
    // 校验（validateBuiltinCall / builtinFunctionArgRange）与原生声明合并。
    static void registerCoreArgWiden(const QString& upperName, int minArgs, int maxArgs) {
        registerCoreArgWidenSpec(upperName.toStdString(), minArgs, maxArgs);
    }

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

    // 指令名是否属于 PRINT 族（含 PRINTPLAIN*）——**不含 DEBUGPRINT 族**。
    //
    // DEBUGPRINT* 的实参形态与 PRINT 族同规则（printInfo 里一并识别，C# 的
    // DEBUGPRINT 用的也是 STR_NULLABLE / FORM_STR_NULLABLE），但它属于**另一条
    // 执行路径**：C# DEBUGPRINT_Instruction -> Exm.Console.DebugPrint ->
    // dConsoleLog（调试窗口日志），且 `if (!Program.DebugMode) return;` ——
    // 也就是说**永远不进游戏画面**。若把它也算 PRINT 族，执行链会先命中
    // handlePrintInstruction，把调试文本刷进控制台：eraTW 实测一屏 1458 行
    //（ERB/MOVEMENTS/SLEEP.ERB:172 的 `DEBUGPRINTFORML %CALLNAME:CHARA%は
    //  {TIME}に{CFLAG:CHARA:初期位置}で寝たよ` 会对每个角色打一行）。
    static bool isPrintFamily(const QString& upperName) {
        if (upperName.startsWith(QLatin1String("DEBUGPRINT"))) return false;
        return printInfo(upperName).mode != PrintArgMode::NotPrint;
    }

    // 「这是不是一条指令名」——用于区分
    //   `ステージ:(ステージ幅 - 1):(LOCAL:0) = 1`（赋值，允许 LHS 含空白）
    //   `PRINTFORML a = b`            （指令，LHS 里那个 = 只是文本）
    // 对齐 C# LogicalLineParser：先查函数名表，命中即为命令文，否则按赋值解。
    static bool isKnownInstructionName(const QString& upperName);

    // ---- 注册类插入点（注册类可以插入 AST）----
    // ExtensionRegistry::regForm() 声明「该函数的实参是 StrForm（文本 +
    // {…}/%…%）」。AstBuilder 自身零函数名 —— 实参形态声明全部由注册类
    // 经本插入点注入，装载期查表按格式串解析（不做表达式归约）。
    static void registerFormArgFunction(const QString& upperName) {
        s_formArgFunctions.insert(upperName);
    }
    // 该函数的实参是否为 StrForm（注册类注入的声明；未注入按表达式归约）
    [[nodiscard]] static bool isFormArgFunction(const QString& upperName) {
        return s_formArgFunctions.contains(upperName);
    }

private:
    // 实参形态 = StrForm 的函数名（ExtensionRegistry::regForm 注入；本文件零函数名）
    inline static QSet<QString> s_formArgFunctions;
    // 扩展语句名（ExtensionRegistry::reg 注入；解析期警告豁免用）
    inline static QSet<QString> s_extensionStatements;
    // 精确登记（不含前缀启发式）/ 仅前缀命中 —— 用于甄别「同前缀的变量名赋值」。
    static bool isExactInstructionName(const QString& upperName);
    static bool hasInstructionPrefix(const QString& upperName);
    // 赋值左值的形状检查：单个变量引用（无顶层空白）。
    static bool isBareVariableLhs(const QString& lhs);

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
