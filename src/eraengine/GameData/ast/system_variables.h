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
#ifndef AST_SYSTEM_VARIABLES_H
#define AST_SYSTEM_VARIABLES_H

#include <array>
#include <string_view>
#include <algorithm>
#include "operand_type.h"

// ---------------------------------------------------------------------------
// 系统变量类型表（对齐 C# GameData/Variable/VariableCode.cs 的 VariableCode）
//
// Emuera 的内建变量分数值型（__INTEGER__）与字符串型（__STRING__）。
// 把它们登记下来，能使表达式在**解析期**就得到正确的强类型：
//   SIF RESULTS:1 == ""        -> RESULTS 是字符串数组，== 得到 Int   （此前误判 Str）
//   SETFONT GLOBALS:99         -> GLOBALS 是字符串数组，SETFONT 参数合法（此前误判 Int）
//   LOCALS / ARGS / STR / TSTR / SAVESTR / GLOBAL(int) / LOCAL(int) / ARG(int)…
//
// 说明：
//   · 用户 #DIM/#DIMS 声明的同名变量优先级更高（见 VariableTable::typeOf）。
//   · 单字母 A–Z 与 DAY/MONEY/… 都是数值型系统变量（generic 变量）。
//   · 只登记「类型」，不登记维度/长度（维度由 VariableSize 配置决定，暂不校验）。
// ---------------------------------------------------------------------------
namespace sysvar {

struct SystemVariableDef {
    std::string_view name;
    OperandType type;
};

// 数值型系统变量（VariableCode.__INTEGER__）
inline constexpr std::string_view kIntegerNames[] = {
    // --- 一般数値変数 ---
    "DAY", "MONEY", "ITEM", "FLAG", "TFLAG", "UP", "PALAMLV", "EXPLV", "EJAC", "DOWN",
    "RESULT", "COUNT", "TARGET", "ASSI", "MASTER", "NOITEM", "LOSEBASE", "SELECTCOM",
    "ASSIPLAY", "PREVCOM", "TIME", "ITEMSALES", "PLAYER", "NEXTCOM", "PBAND", "BOUGHT",
    "ITEMPRICE", "LOCAL", "ARG", "GLOBAL", "RANDDATA", "BAND", "NOTUSE_38",
    // 汎用変数 A–Z（数值型）
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    // --- キャラクタ数値変数（第一引数省略可）---
    "ISASSI", "NO", "BASE", "MAXBASE", "ABL", "TALENT", "EXP", "MARK", "PALAM", "SOURCE",
    "EX", "CFLAG", "JUEL", "RELATION", "EQUIP", "TEQUIP", "STAIN", "GOTJUEL", "NOWEX",
    "DOWNBASE", "CUP", "CDOWN", "TCVAR",
    // --- 2D / 3D ---
    "DITEMTYPE", "DA", "DB", "DC", "DD", "DE", "CDFLAG", "TA", "TB",
    // --- 計算値（CALC）---
    "RAND", "CHARANUM",
    "GAMEBASE_GAMECODE", "GAMEBASE_VERSION", "GAMEBASE_ALLOWVERSION",
    "GAMEBASE_DEFAULTCHARA", "GAMEBASE_NOITEM",
    "LASTLOAD_VERSION", "LASTLOAD_NO", "__LINE__", "LINECOUNT", "ISTIMEOUT",
    "__INT_MAX__", "__INT_MIN__",
};

// 文字列型系统变量（VariableCode.__STRING__）
inline constexpr std::string_view kStringNames[] = {
    // --- 文字列変数 ---
    "SAVESTR", "STR", "RESULTS", "LOCALS", "ARGS", "GLOBALS", "TSTR", "SAVEDATA_TEXT",
    // --- キャラクタ文字列変数 ---
    "NAME", "CALLNAME", "NICKNAME", "MASTERNAME", "CSTR",
    // --- CSV 常数名（～NAME 系）---
    "ABLNAME", "EXPNAME", "TALENTNAME", "PALAMNAME", "TRAINNAME", "MARKNAME", "ITEMNAME",
    "BASENAME", "SOURCENAME", "EXNAME", "EQUIPNAME", "TEQUIPNAME", "FLAGNAME", "TFLAGNAME",
    "CFLAGNAME", "TCVARNAME", "CSTRNAME", "STAINNAME", "CDFLAGNAME1", "CDFLAGNAME2",
    "STRNAME", "TSTRNAME", "SAVESTRNAME", "GLOBALNAME", "GLOBALSNAME",
    // --- GAMEBASE 系（CALC/文字列）---
    "GAMEBASE_AUTHER", "GAMEBASE_AUTHOR", "GAMEBASE_INFO", "GAMEBASE_YEAR", "GAMEBASE_TITLE",
    "WINDOW_TITLE", "__FILE__", "__FUNCTION__", "MONEYLABEL", "DRAWLINESTR",
    "EMUERA_VERSION", "LASTLOAD_TEXT",
};

[[nodiscard]] constexpr bool containsName(const auto& table, std::string_view name) noexcept {
    for (std::string_view n : table) {
        if (n.size() != name.size()) continue;
        bool same = true;
        for (std::size_t i = 0; i < n.size(); ++i) {
            char a = n[i], b = name[i];
            if (a >= 'a' && a <= 'z') a = static_cast<char>(a - 'a' + 'A');
            if (b >= 'a' && b <= 'z') b = static_cast<char>(b - 'a' + 'A');
            if (a != b) { same = false; break; }
        }
        if (same) return true;
    }
    return false;
}

// 系统变量类型（大小写不敏感）；未知返回 OperandType::Unknown
[[nodiscard]] constexpr OperandType systemVariableType(std::string_view name) noexcept {
    if (containsName(kIntegerNames, name)) return OperandType::Int;
    if (containsName(kStringNames, name))  return OperandType::Str;
    return OperandType::Unknown;
}

} // namespace sysvar

// 编译期自检：表本身自洽（字符串表里不应出现数值名，反之亦然）
static_assert(sysvar::systemVariableType("RESULTS") == OperandType::Str);
static_assert(sysvar::systemVariableType("results") == OperandType::Str);
static_assert(sysvar::systemVariableType("GLOBALS") == OperandType::Str);
static_assert(sysvar::systemVariableType("GLOBAL")  == OperandType::Int);
static_assert(sysvar::systemVariableType("FLAG")    == OperandType::Int);
static_assert(sysvar::systemVariableType("CFLAG")   == OperandType::Int);
static_assert(sysvar::systemVariableType("CSTR")    == OperandType::Str);
static_assert(sysvar::systemVariableType("A")       == OperandType::Int);
static_assert(sysvar::systemVariableType("NOSUCHVAR") == OperandType::Unknown);

#endif // AST_SYSTEM_VARIABLES_H
