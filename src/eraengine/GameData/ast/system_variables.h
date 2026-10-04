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
#include <string>
#include <string_view>
#include <unordered_map>
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
//   · fork 专有变量（EE 的 DAYNAME/TIMENAME/MONEYNAME 等）不在原生 constexpr
//     表内：运行期经注册类（ExtensionRegistry::regVariable）注入下方扩展表，
//     解析期用 systemVariableTypeDyn 查询（原生表保持编译期不变）。
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
    "ITEMPRICE", "LOCAL", "ARG", "GLOBAL", "RANDDATA", "NOTUSE_38",
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
    // DAYNAME/TIMENAME/MONEYNAME 等 fork（EmueraEM+EE）专有 CSV 变量不在此表：
    // 由扩展（ee_extension.cpp 经 ExtensionRegistry::regVariable）在启动时登记，
    // 解析期经 systemVariableTypeDyn 与原生表合并（原生 constexpr 表不变）。
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

// ---------------------------------------------------------------------------
// 运行期「扩展系统变量」表
//
// **原生归原生、扩展归扩展**：原生表 kIntegerNames/kStringNames 保持 constexpr
// 不变；fork 专有 CSV 变量（EmueraEM+EE 的 DAYNAME/TIMENAME/MONEYNAME 等）由
// 扩展在启动时经注册类（ExtensionRegistry::regVariable ->
// sysvar::registerExtensionSystemVariable）注入本表。这样解析期能像原生变量一样
// 拿到强类型，而定义完全住在扩展侧（GameProc/ee_extension.cpp）。
//
// 每个扩展变量同时携带：
//   · nameTableCsv —— 「变量:名字」解析用的名表 CSV（可空）；
//   · size1D       —— 默认一维长度（<=0 表示不指定，交由 VariableSize.csv）。
// 名表映射另存 nameTables（基础变量 -> CSV 文件名，如 DAY -> DAY.CSV）——基础
// 变量本身是原生变量，只是它的名表由扩展补齐。
// ---------------------------------------------------------------------------
struct ExtensionVariableDef {
    OperandType type = OperandType::Unknown;
    std::string nameTableCsv;   // 该变量「名字 -> 下标」用的名表（可空）
    int size1D = 0;             // 默认一维长度（<=0 不指定）
};

struct ExtensionVariableStore {
    std::unordered_map<std::string, ExtensionVariableDef> defs;   // 变量名（大写）-> 定义
    std::unordered_map<std::string, std::string> nameTables;      // 基础变量（大写）-> 名表 CSV
};

inline ExtensionVariableStore& extensionVariableStore() {
    static ExtensionVariableStore store;
    return store;
}

// ASCII 大写（变量名规范：大小写不敏感）
[[nodiscard]] inline std::string toUpperAscii(std::string_view name) {
    std::string out(name);
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

// 注册扩展系统变量（重复注册按后者覆盖 —— 引擎可能创建多个注册类实例）。
inline void registerExtensionSystemVariable(std::string_view name, OperandType type,
                                            std::string_view nameTableCsv = {},
                                            int size1D = 0) {
    extensionVariableStore().defs.insert_or_assign(
        toUpperAscii(name),
        ExtensionVariableDef{type, std::string(nameTableCsv), size1D});
}

// 为（通常是原生）基础变量补名表映射：基础变量 -> 名表 CSV（如 DAY -> DAY.CSV）。
inline void registerExtensionNameTable(std::string_view variableName,
                                       std::string_view csvFileName) {
    extensionVariableStore().nameTables.insert_or_assign(
        toUpperAscii(variableName), std::string(csvFileName));
}

[[nodiscard]] inline const ExtensionVariableDef*
findExtensionSystemVariable(std::string_view name) {
    const ExtensionVariableStore& store = extensionVariableStore();
    const auto it = store.defs.find(toUpperAscii(name));
    return it == store.defs.end() ? nullptr : &it->second;
}

// 合并查表：原生优先（核心不得被扩展覆盖），其次扩展。
[[nodiscard]] inline OperandType systemVariableTypeDyn(std::string_view name) noexcept {
    const OperandType core = systemVariableType(name);
    if (core != OperandType::Unknown) return core;
    if (const ExtensionVariableDef* d = findExtensionSystemVariable(name)) return d->type;
    return OperandType::Unknown;
}

[[nodiscard]] inline std::string_view extensionNameTableCsvOf(std::string_view variableName) {
    const ExtensionVariableStore& store = extensionVariableStore();
    const auto it = store.nameTables.find(toUpperAscii(variableName));
    return it == store.nameTables.end() ? std::string_view{} : std::string_view{it->second};
}

[[nodiscard]] inline int extensionDefault1DSizeOf(std::string_view name) {
    const ExtensionVariableDef* d = findExtensionSystemVariable(name);
    return d ? d->size1D : 0;
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
