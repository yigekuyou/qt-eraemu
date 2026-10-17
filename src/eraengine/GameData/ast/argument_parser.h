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
#ifndef AST_ARGUMENT_PARSER_H
#define AST_ARGUMENT_PARSER_H

#include <array>
#include <string_view>
#include <string_view>
#include "logical_line.h"

// ---------------------------------------------------------------------------
// 语句参数解析 / 校验（对齐 C# ArgumentParser + ArgumentBuilder）
//
//   * 指令规范表（constexpr）：指令名 -> ArgKind + 参数个数范围；
//   * 由已归约的 Operand（含 AST）构造 TypedArgument；
//   * 解析期做「个数 + 类型」校验，失败写入 typeError（不再等到执行期）。
// ---------------------------------------------------------------------------
struct InstructionSpec {
    std::string_view name;
    ArgKind kind;
    qint8 minArgs;
    qint8 maxArgs;   // -1 = 不限
};

// 覆盖当前已处理/常用的指令族；未列出者按 ArgKind::Raw 宽松处理。
inline constexpr auto kInstructionSpecs = std::to_array<InstructionSpec>({
    {"PLAYBGM", ArgKind::StrExpression, 1, 1},
    {"PLAYSOUND", ArgKind::Expressions, 1, 2},
    {"SETBGMVOLUME", ArgKind::IntExpression, 1, 1},
    {"SETSOUNDVOLUME", ArgKind::IntExpression, 1, 1},
    {"STOPBGM", ArgKind::Void, 0, 0},
    {"STOPSOUND", ArgKind::Void, 0, 0},
    {"DT_COLUMN_OPTIONS", ArgKind::Expressions, 3, -1},
    {"IF",           ArgKind::IntExpression, 1,  1},
    {"SIF",          ArgKind::IntExpression, 1,  1},
    {"ELSEIF",       ArgKind::IntExpression, 1,  1},
    {"WHILE",        ArgKind::IntExpression, 1,  1},
    {"REPEAT",       ArgKind::IntExpression, 1,  1},
    {"FOR",          ArgKind::ForNext,       3,  4},
    {"LOOP",         ArgKind::IntExpression, 0,  1},
    {"NEXT",         ArgKind::Raw,           0,  1},
    {"BREAK",        ArgKind::Void,          0,  0},
    {"CONTINUE",     ArgKind::Void,          0,  0},
    {"ENDIF",        ArgKind::Void,          0,  0},
    {"ELSE",         ArgKind::Void,          0,  0},
    {"WEND",         ArgKind::Void,          0,  0},
    {"GOTO",         ArgKind::Raw,           1,  1},   // 标签（非表达式）
    {"CALL",         ArgKind::Call,          1,  -1},
    {"CALLFORM",     ArgKind::CallF,         1,  -1},
    {"JUMP",         ArgKind::Call,          1,  -1},   // 对齐 C#：JUMP 使用 CALL_Instruction（CALL 同族）
    {"FUNC",         ArgKind::CallF,         1,  -1},   // TRY*LIST 体内的条目（C# SP_CALLFORM）
    {"TRYCALLLIST",  ArgKind::Void,          0,  0},
    {"TRYJUMPLIST",  ArgKind::Void,          0,  0},
    {"TRYGOTOLIST",  ArgKind::Void,          0,  0},
    {"GOTOLIST",     ArgKind::Void,          0,  0},   // TRYGOTOLIST 的宽松别名
    {"ENDFUNC",      ArgKind::Void,          0,  0},
    {"RETURN",       ArgKind::Expressions,   0,  -1},   // INT_ANY：逗号分隔的整型表达式序列 -> RESULT:0..n
    {"RETURNF",      ArgKind::Expression,    0,  1},
    {"SELECTCASE",   ArgKind::Expression,    1,  1},
    {"CASE",         ArgKind::Case,          1,  -1},
    {"CASEELSE",     ArgKind::Void,          0,  0},
    {"ENDSELECT",    ArgKind::Void,          0,  0},
    {"=",            ArgKind::Raw,           2,  2},
    {"+=",           ArgKind::Raw,           2,  2},
    {"-=",           ArgKind::Raw,           2,  2},
    {"*=",           ArgKind::Raw,           2,  2},
    {"/=",           ArgKind::Raw,           2,  2},
    {"'=",           ArgKind::Raw,           2,  2},
    {"%=",           ArgKind::Raw,           2,  2},
    // 位复合赋值（C# ReadAssignmentOperator 全集）。缺了这几项时 classify 退化成
    // Raw(0,-1) 也能跑，但显式登记才能和 += 族一起被个数/类型校验覆盖。
    {"&=",           ArgKind::Raw,           2,  2},
    {"|=",           ArgKind::Raw,           2,  2},
    {"^=",           ArgKind::Raw,           2,  2},
    {"<<=",          ArgKind::Raw,           2,  2},
    {">>=",          ArgKind::Raw,           2,  2},
    {"SET",          ArgKind::VarSet,        1,  4},
    {"VARSET",       ArgKind::VarSet,        1,  -1},   // 值可省略
    {"VARSIZE",      ArgKind::Var,           1,   1},
    {"ARRAYCOPY",    ArgKind::ArrayControl,  2,  2},
    {"ARRAYSORT",    ArgKind::ArrayControl,  1,  -1},
    {"SWAP",         ArgKind::Swap,          2,  2},
    {"++",           ArgKind::Var,           1,  1},
    {"--",           ArgKind::Var,           1,  1},
    {"SETBIT",       ArgKind::Bit,           1,  -1},
    {"PRINT",        ArgKind::Raw,           0,  -1},
    {"PRINTL",       ArgKind::Raw,           0,  -1},
    {"PRINTV",       ArgKind::PrintV,        1,  -1},
    {"PRINTS",       ArgKind::PrintV,        1,  -1},
    {"PRINT_IMG",    ArgKind::StrExpression, 1,  1},   // C# PRINT_IMG: resource string expression
    {"PRINTBUTTON",  ArgKind::Button,        2,  3},   // <文字列式>,<数式>(,<tooltip>)
    {"PRINTBUTTONC", ArgKind::Button,        2,  3},   // 同上 + PRINTC 定宽（右对齐）
    {"PRINTBUTTONLC", ArgKind::Button,       2,  3},   // 同上 + PRINTC 定宽（左对齐）
    {"PRINTDATA",    ArgKind::PrintData,     0,  -1},
    {"DRAWLINE",     ArgKind::Void,          0,  0},
    {"CLEARLINE",    ArgKind::IntExpression, 0,  1},
    {"SETFONT",      ArgKind::StrExpression, 0,  1},   // 字体名（可省略=默认）
    {"ALIGNMENT",    ArgKind::Raw,           1,  1},
    {"SETCOLOR",     ArgKind::Color,         1,  3},
    {"RESETCOLOR",   ArgKind::Void,          0,  0},
    {"REDRAW",       ArgKind::IntExpression, 0,  1},
    {"INPUT",        ArgKind::Expressions,   0,  2},   // SP_INPUT
    {"INPUTS",       ArgKind::Expressions,   0,  1},   // SP_INPUTS
    // SP_TINPUT / SP_TINPUTS（ArgumentBuilder.cs:1559/1585）：`minArg = 2`、最多 6 参 ——
    // 超时与缺省值必填，其后是「显示剩余时间 / 超时字符串 / 鼠标输入标志 / 可跳过
    // 标志」（test/data/commands/TINPUT.md / TINPUTS.md 的扩展参数）。此前上限写死 4，
    // FL3.8 的 `TINPUTS wait, "TIME_OVER", 0, "", 1` 被误报「参数过多」。
    {"ONEINPUT",     ArgKind::Expressions,   0,  2},   // SP_ONEINPUT
    {"TONEINPUT",    ArgKind::Expressions,   2,  6},   // SP_TINPUT：2..6 参（超时+缺省必填）
    {"TINPUT",       ArgKind::Expressions,    2,  6},   // SP_TINPUT（ArgumentBuilder.cs:1559）
    {"INPUTMOUSEKEY", ArgKind::IntExpression, 0,  1},  // 可选超时 ms（C# GetNormalArgumentBuilder("I",0)）
    {"ONEINPUTS",    ArgKind::Expressions,   0,  1},   // SP_INPUTS
    {"TINPUTS",      ArgKind::Expressions,   2,  6},   // SP_TINPUTS：2..6 参（见 TINPUTS.md）
    {"TONEINPUTS",   ArgKind::Expressions,   2,  6},   // SP_TINPUTS 的 ONE 版
    {"TWAIT",        ArgKind::Expressions,   0,  2},   // 超时[,跳过]
    {"WAIT",         ArgKind::Void,          0,  0},
    {"WAITANYKEY",   ArgKind::Void,          0,  0},
    {"AWAIT",        ArgKind::IntExpression, 0,  1},
    {"RANDOMIZE",    ArgKind::IntExpression, 1,  1},
    {"BEGIN",        ArgKind::Raw,           1,  1},
    {"QUIT",         ArgKind::Void,          0,  0},
    {"SETS",         ArgKind::VarSet,        1,  2},
    {"CVARSET",      ArgKind::VarSet,        1,  5},   // <角色变量>,<元素>[,<式>[,<初値>,<終値>]]
    {"VAR_SET",      ArgKind::VarSet,        1,  4},
    {"SWAPVAR",      ArgKind::Swap,          2,  2},
    {"TIMES",        ArgKind::Times,         2,  2},
    {"BAR",          ArgKind::Bar,           3,  3},
    {"BARL",         ArgKind::Bar,           3,  3},
    {"POWER",        ArgKind::Power,         3,  3},
    {"CLEARBIT",     ArgKind::Bit,           0,  -1},
    {"INVERTBIT",    ArgKind::Bit,           0,  -1},
    {"SORTCHARA",    ArgKind::SortChara,     0,  2},
    {"ARRAYSHIFT",   ArgKind::ArrayControl,  2,  -1},
    {"ARRAYREMOVE",  ArgKind::ArrayControl,  1,  -1},
    {"SAVEDATA",     ArgKind::SaveData,      2,  -1},
    {"SAVEGAME",     ArgKind::SaveData,      0,  -1},
    {"LOADGAME",     ArgKind::SaveData,      0,  -1},
    {"SAVEVAR",      ArgKind::SaveData,      2,  -1},   // <变量>…,<文件名>（EE）/ <文件名>,<文字列>,<变量>…（C# 原型）
    {"LOADVAR",      ArgKind::SaveData,      1,  -1},
    // SAVENOS / PRINTCPERLINE：语句形式是 SP_GETINT「<数值变量>」——
    // 把配置值**写进**该变量（C# Process.ScriptProc.cs 的 SpGetIntArgument.VarToken.SetValue），
    // 与 0 参式中函数 SAVENOS() / PRINTCPERLINE() 不同形，故按变量槽指令登记。
    // 参数不可省略（C# SP_GETINT 少参会报参数不足）。
    {"SAVENOS",      ArgKind::GetInt,        1,  1},
    {"PRINTCPERLINE",ArgKind::GetInt,        1,  1},
    {"SPLIT",        ArgKind::Split,         1,  -1},
    {"STRDATA",      ArgKind::VarStr,        0,  1},   // VAR_STR：0 实参时目标为 RESULTS:0
    {"ENCODETOUNI",  ArgKind::FormStr,       0,  1},   // statement: FORM_STR_NULLABLE
    {"DATAFORM",     ArgKind::FormStr,       0,  1},   // FORM_STR_NULLABLE
    // DATA：C# 是 STR_NULLABLE，但 era 脚本一律写「DATA <原文文本>」
    // （PRINTDATA/DATALIST/STRDATA 的数据行），而裸标识符在表达式里会被当成
    // 变量（得 0）。这里按 FORMS 语义解析实参（原文即文本，%…%/{…} 仍展开），
    // 与 DATAFORM 一致 —— 否则 `DATA 列表项1` 输出 0。
    {"DATA",         ArgKind::FormStr,       0,  1},   // STR_NULLABLE（按 FORM 语义）
    {"THROW",        ArgKind::FormStr,       0,  1},   // FORM_STR_NULLABLE（THROW <格式化串>）
    {"SETBGCOLOR",   ArgKind::Color,         1,  3},
    // C# 是 argb[FunctionArgType.STR]（STR_ArgumentBuilder：整段是**字面色名**，
    // 裸标识符不做变量解析）。引擎此前按 Color（表达式）分类，于是
    // `SETCOLORBYNAME Red` 的 Red 被当作变量 —— eraMegaten 因此有 39 条
    // 「Red/Blue 在本函数中未定义」的假阳性。运行期本就只读 args[0].raw。
    {"SETCOLORBYNAME", ArgKind::Raw,         1,  1},
    {"GETINT",       ArgKind::GetInt,        1,  1},
    {"REF",          ArgKind::VarStr,        1,  1},
    {"REFBYNAME",    ArgKind::VarStr,        1,  1},
    {"HTML_TAGSPLIT", ArgKind::HtmlSplit,    1,  3},
    {"CALLF",        ArgKind::CallF,         1,  -1},
    {"CALLFORMF",    ArgKind::CallF,         1,  -1},
    {"PRINT_ABL",    ArgKind::PrintV,        1,  -1},
    {"PRINT_TALENT", ArgKind::PrintV,        1,  -1},
    {"PRINT_MARK",   ArgKind::PrintV,        1,  -1},
    {"PRINT_EXP",    ArgKind::PrintV,        1,  -1},
    {"PRINT_PALAM",  ArgKind::PrintV,        1,  -1},
    // C# FunctionIdentifier registers PRINT_ITEM with FunctionArgType.VOID.
    {"PRINT_ITEM",   ArgKind::Void,           0,   0},
    // C# FunctionIdentifier registers PRINT_SHOPITEM with FunctionArgType.VOID.
    {"PRINT_SHOPITEM", ArgKind::Void,         0,   0},
    {"CUSTOMDRAWLINE", ArgKind::Raw,        1,   1},

    // ---- 原版指令补全（2026-10）：此前回落 Raw（无 ArgKind/个数/类型校验）----
    // 来源：C# FunctionIdentifier.cs 的 addFunction / Instraction.Child.cs 的
    // GetArgumentBuilder(FunctionArgType.X)；个数范围取 test/data/commands/*.md 的签名。
    // 注：同时是内置函数的名字（COPYCHARA/PICKUPCHARA/PUTFORM/STRLEN*…）不在此列 ——
    //     那些行由 AstBuilder 转成函数语句（isFunctionCall），加规范会误报「参数过多」。
    //     例外是**语句形式与式中函数不同形**的几个（POWER/TIMES/SORTCHARA/SAVENOS/
    //     PRINTCPERLINE…）：它们既在 AstBuilder 的例外名单里，又必须在此登记语句
    //     形式的 ArgKind，否则语句会退化成「按函数归约」而报参数过多。
    // 角色列表（INT_ANY：可变个整数）——注意必须用 **Expressions**（逗号族）：
    // 本引擎的 IntExpression 是「整行归约为一个表达式」，`ADDCHARA 3, 5` 会被并成
    // 一个操作数而破坏多参；Expressions 按顶层逗号切分，逐参归约（见 ast_builder）。
    {"ADDCHARA",     ArgKind::Expressions,    1, -1},
    {"ADDSPCHARA",   ArgKind::Expressions,    1, -1},
    {"DELCHARA",     ArgKind::Expressions,    1, -1},
    {"ADDVOIDCHARA", ArgKind::Void,          0,  0},
    {"ADDDEFCHARA",  ArgKind::Void,          0,  0},
    {"DELALLCHARA",  ArgKind::Void,          0,  0},
    // 调试/系统
    {"ASSERT",       ArgKind::IntExpression, 1,  1},
    {"DOTRAIN",      ArgKind::IntExpression, 1,  1},
    {"RESTART",      ArgKind::Void,          0,  0},
    {"RESETDATA",    ArgKind::Void,          0,  0},
    {"RESETGLOBAL",  ArgKind::Void,          0,  0},
    {"OUTPUTLOG",    ArgKind::Void,          0,  0},   // 命令形式；式中函数形式另有 1..2
    {"UPCHECK",      ArgKind::Void,          0,  0},
    // 事件：C# STR（裸记号）——eraTW 实际写法是 `CALLEVENT 事件名`（无引号），
    // 若用 StrExpression 会把裸标识符按 Int 误报，故用类型无关的 Expressions 只校验个数。
    {"CALLEVENT",    ArgKind::Expressions,   1,  1},
    // 显示/样式
    {"CLEARTEXTBOX", ArgKind::Void,          0,  0},
    {"FONTBOLD",     ArgKind::Void,          0,  0},
    {"FONTITALIC",   ArgKind::Void,          0,  0},
    {"FONTREGULAR",  ArgKind::Void,          0,  0},
    {"FONTSTYLE",    ArgKind::IntExpression, 0,  1},   // 参数可省略（省略按 0）
    {"FORCEKANA",    ArgKind::IntExpression, 1,  1},
    {"FORCEWAIT",    ArgKind::Void,          0,  0},
    {"DRAWLINEFORM", ArgKind::FormStr,      1,  1},
    {"REUSELASTLINE", ArgKind::FormStr,     0,  1},    // FORM_STR_NULLABLE：可省略
    // C# 同为 argb[FunctionArgType.STR]（STR_ArgumentBuilder）—— 字面色名，
    // 见上面的 SETCOLORBYNAME。
    {"SETBGCOLORBYNAME", ArgKind::Raw, 1, 1},
    {"SKIPDISP",     ArgKind::IntExpression, 1,  1},
    // 存档/训练辅助
    {"DELDATA",      ArgKind::IntExpression, 1,  1},
    {"CUPCHECK",     ArgKind::IntExpression, 1,  1},
    // 工具提示（C# SP_SWAP = <数値>,<数値>；不是「变量交换」，勿映射 ArgKind::Swap）
    {"TOOLTIP_SETCOLOR",    ArgKind::Expressions, 2, 2},
    {"TOOLTIP_SETDELAY",    ArgKind::IntExpression, 1, 1},
    {"TOOLTIP_SETDURATION", ArgKind::IntExpression, 1, 1},
    // C# FunctionIdentifier.cpp:1960-1990 的 TOOLTIP_* 指令
    // （Instraction.Child.cs:2933-3010）：单实参，参数族分别是
    // STR_EXPRESSION / INT_EXPRESSION。此前只有 SETCOLOR/SETDELAY/SETDURATION
    // 在表里，其余四个落到扩展桩上（运行期 [未完成] 跳过）。
    {"TOOLTIP_SETFONT",     ArgKind::StrExpression, 1, 1},   // C# SetToolTipFontName
    {"TOOLTIP_SETFONTSIZE", ArgKind::IntExpression, 1, 1},   // C# SetToolTipFontSize（磅）
    {"TOOLTIP_CUSTOM",      ArgKind::IntExpression, 1, 1},   // C# CustomToolTip（OwnerDraw）
    {"TOOLTIP_FORMAT",      ArgKind::IntExpression, 1, 1},   // C# SetToolTipFormat（TextFormatFlags）
    {"TOOLTIP_IMG",         ArgKind::IntExpression, 1, 1},   // C# SetToolTipImg
    // BREAKBUTTON：C# FunctionArgType.EXPRESSION_NULLABLE（0~1 参，值被忽略）
    {"BREAKBUTTON",         ArgKind::Expression,    0, 1},
});
[[nodiscard]] constexpr const InstructionSpec* findInstructionSpec(std::string_view upperName) noexcept {
    for (const auto& s : kInstructionSpecs) {
        if (s.name == upperName) return &s;
    }
    return nullptr;
}

// 指令分类：显式规范表优先，未命中按名字前缀宽松回退（保证「至少能解析」）
[[nodiscard]] constexpr ArgKind classifyInstructionKind(std::string_view upperName,
                                                        int& minArgs, int& maxArgs) noexcept {
    if (const InstructionSpec* s = findInstructionSpec(upperName)) {
        minArgs = s->minArgs;
        maxArgs = s->maxArgs;
        return s->kind;
    }
    const auto startsWith = [upperName](std::string_view p) noexcept {
        return upperName.size() >= p.size() && upperName.substr(0, p.size()) == p;
    };
    minArgs = 0;
    maxArgs = -1;
    if (startsWith("PRINT") || startsWith("DEBUGPRINT") || startsWith("HTML_PRINT")) {
        return ArgKind::PrintV;
    }
    if (startsWith("SAVE") || startsWith("LOAD")) {
        return ArgKind::SaveData;
    }
    return ArgKind::Raw;
}

class ArgumentParser {
public:
    // 依据指令规范表，把 line 的原始操作数整理为 TypedArgument 并做类型/个数校验。
    static void build(LogicalLine& line);
};

#endif // AST_ARGUMENT_PARSER_H
