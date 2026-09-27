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
#ifndef AST_OPERAND_TYPE_H
#define AST_OPERAND_TYPE_H

#include <QtGlobal>
#include <string_view>

// ---------------------------------------------------------------------------
// OperandType —— 强类型的操作数类型（对齐 C# IOperandTerm.GetOperandType()）
//
//   C# 用 System.Type（typeof(Int64)/typeof(string)/typeof(void)）标注每个
//   IOperandTerm 的类型，运算符按类型分派（PlusIntInt / PlusStrStr / MultStrInt …）。
//   此处在 AST 上显式建模，供解析期类型检查与求值期严格分派使用。
// ---------------------------------------------------------------------------
enum class OperandType : quint8 {
    Unknown = 0,   // 尚未推断
    Int,           // 整数（C# Int64）
    Str,           // 字符串（C# string）
    Void           // 无返回值（C# void）
};

[[nodiscard]] constexpr std::string_view operandTypeName(OperandType t) noexcept {
    switch (t) {
    case OperandType::Int:  return "Int";
    case OperandType::Str:  return "Str";
    case OperandType::Void: return "Void";
    case OperandType::Unknown: break;
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool isInteger(OperandType t) noexcept { return t == OperandType::Int; }
[[nodiscard]] constexpr bool isString(OperandType t) noexcept { return t == OperandType::Str; }
[[nodiscard]] constexpr bool isKnown(OperandType t) noexcept { return t != OperandType::Unknown; }

// 数值上下文：Unknown 视为 Int（宽松），Void 非法
[[nodiscard]] constexpr OperandType asNumeric(OperandType t) noexcept {
    return t == OperandType::Unknown ? OperandType::Int : t;
}

#endif // AST_OPERAND_TYPE_H
