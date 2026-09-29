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
    {"SET",          ArgKind::VarSet,        1,  4},
    {"VARSET",       ArgKind::VarSet,        1,  -1},   // 值可省略
    {"VARSIZE",      ArgKind::Expressions,   1,  -1},
    {"ARRAYCOPY",    ArgKind::ArrayControl,  2,  -1},
    {"ARRAYSORT",    ArgKind::ArrayControl,  1,  -1},
    {"SWAP",         ArgKind::Swap,          2,  2},
    {"SETBIT",       ArgKind::Bit,           1,  -1},
    {"PRINT",        ArgKind::Raw,           0,  -1},
    {"PRINTL",       ArgKind::Raw,           0,  -1},
    {"PRINTV",       ArgKind::PrintV,        1,  -1},
    {"PRINTS",       ArgKind::PrintV,        1,  -1},
    {"PRINT_IMG",    ArgKind::StrExpression, 1,  1},   // C# PRINT_IMG: resource string expression
    {"PRINTBUTTON",  ArgKind::Button,        2,  3},   // <文字列式>,<数式>(,<tooltip>)
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
    {"ONEINPUT",     ArgKind::Expressions,   0,  2},   // SP_ONEINPUT
    {"TONEINPUT",    ArgKind::Expressions,   1,  4},   // SP_TINPUT
    {"TINPUT",       ArgKind::Input,         1,  4},
    {"WAIT",         ArgKind::Void,          0,  0},
    {"WAITANYKEY",   ArgKind::Void,          0,  0},
    {"AWAIT",        ArgKind::IntExpression, 0,  1},
    {"RANDOMIZE",    ArgKind::IntExpression, 1,  1},
    {"BEGIN",        ArgKind::Raw,           1,  1},
    {"QUIT",         ArgKind::Void,          0,  0},
    {"SETS",         ArgKind::VarSet,        1,  2},
    {"CVARSET",      ArgKind::VarSet,        1,  4},
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
    {"SAVEVAR",      ArgKind::SaveData,      2,  -1},
    {"LOADVAR",      ArgKind::SaveData,      2,  -1},
    {"SAVENOS",      ArgKind::SaveData,      0,  1},
    {"SPLIT",        ArgKind::Split,         1,  -1},
    {"STRDATA",      ArgKind::VarStr,        0,  1},   // VAR_STR：0 实参时目标为 RESULTS:0
    {"DATAFORM",     ArgKind::FormStr,       0,  1},   // FORM_STR_NULLABLE
    {"SETBGCOLOR",   ArgKind::Color,         1,  3},
    {"SETCOLORBYNAME", ArgKind::Color,       1,  1},
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
    {"PRINT_ITEM",   ArgKind::PrintV,        1,  -1},
    {"PRINT_SHOPITEM", ArgKind::PrintV,      0,  -1},
    {"CUSTOMDRAWLINE", ArgKind::Raw,        0,  -1},
    {"TINPUTS",      ArgKind::Input,         1,  4},
    {"ONEINPUTS",    ArgKind::Input,         0,  2},
    {"TONEINPUTS",   ArgKind::Input,         1,  4},
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
