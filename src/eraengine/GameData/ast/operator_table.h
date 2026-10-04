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
#ifndef AST_OPERATOR_TABLE_H
#define AST_OPERATOR_TABLE_H

#include <array>
#include <optional>
#include <algorithm>
#include "operand_type.h"
#include "expression_lexer.h"

// ---------------------------------------------------------------------------
// 运算符表（constexpr，C++23）
//
// 对齐 C# `OperatorCode` + `OperatorMethod`：
//   * 每个运算符带优先级；
//   * 每个二元运算符带**按操作数类型的签名**（PlusIntInt / PlusStrStr / MultStrInt …），
//     从而支持强类型推断与严格分派；
//   * 一元运算符同样标注操作数/结果类型。
//
// 该表同时被「语法分析（优先级）」「类型推断」「求值（分派）」使用，
// 取代散落的 switch，避免三处不一致。
// ---------------------------------------------------------------------------

struct BinarySignature {
    OperandType lhs = OperandType::Unknown;
    OperandType rhs = OperandType::Unknown;
    OperandType result = OperandType::Unknown;
};

struct OperatorDef {
    TokenType   op = TokenType::UNKNOWN;
    int         precedence = 0;               // 0 表示不是二元运算符
    std::uint8_t unaryArity = 0;              // 0/1：是否可作一元
    OperandType  unaryOperand = OperandType::Unknown;
    OperandType  unaryResult = OperandType::Unknown;
    std::uint8_t binaryCount = 0;
    std::array<BinarySignature, 4> binaries{};
};

namespace opdetail {

constexpr BinarySignature bin(OperandType l, OperandType r, OperandType res) {
    return BinarySignature{l, r, res};
}

// 供 constexpr 构造：把签名列表压进定长数组
constexpr std::array<BinarySignature, 4> pack(std::initializer_list<BinarySignature> list) {
    std::array<BinarySignature, 4> a{};
    std::size_t i = 0;
    for (const auto& s : list) {
        if (i >= a.size()) break;
        a[i++] = s;
    }
    return a;
}

// 声明式构造一个二元运算符定义
constexpr OperatorDef bop(TokenType op, int prec, std::initializer_list<BinarySignature> sigs) {
    OperatorDef d;
    d.op = op;
    d.precedence = prec;
    d.binaryCount = static_cast<std::uint8_t>(std::min<std::size_t>(sigs.size(), 4));
    d.binaries = pack(sigs);
    return d;
}

// 一元运算符定义（可再叠加二元）；按值返回以便 constexpr 初始化
constexpr OperatorDef asUnary(OperatorDef d, OperandType operand, OperandType result) {
    d.unaryArity = 1;
    d.unaryOperand = operand;
    d.unaryResult = result;
    return d;
}

} // namespace opdetail

inline constexpr auto kOperatorTable = std::to_array<OperatorDef>({
    // 算术
    opdetail::bop(TokenType::MULTIPLY, 10, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int),
                                             opdetail::bin(OperandType::Str, OperandType::Int, OperandType::Str),
                                             opdetail::bin(OperandType::Int, OperandType::Str, OperandType::Str) }),
    opdetail::bop(TokenType::DIVIDE,   10, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int) }),
    opdetail::bop(TokenType::MODULO,   10, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int) }),
    // 注：PLUS / MINUS 同时是一元运算符，定义见下方（避免重复条目）
    // 移位
    opdetail::bop(TokenType::SHIFT_LEFT,  8, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int) }),
    opdetail::bop(TokenType::SHIFT_RIGHT, 8, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int) }),
    // 比较（结果均为 Int）
    opdetail::bop(TokenType::LESS_THAN,     7, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int),
                                                 opdetail::bin(OperandType::Str, OperandType::Str, OperandType::Int) }),
    opdetail::bop(TokenType::LESS_EQUAL,    7, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int),
                                                 opdetail::bin(OperandType::Str, OperandType::Str, OperandType::Int) }),
    opdetail::bop(TokenType::GREATER_THAN,  7, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int),
                                                 opdetail::bin(OperandType::Str, OperandType::Str, OperandType::Int) }),
    opdetail::bop(TokenType::GREATER_EQUAL, 7, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int),
                                                 opdetail::bin(OperandType::Str, OperandType::Str, OperandType::Int) }),
    opdetail::bop(TokenType::EQUALS,     6, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int),
                                              opdetail::bin(OperandType::Str, OperandType::Str, OperandType::Int) }),
    opdetail::bop(TokenType::NOT_EQUALS, 6, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int),
                                              opdetail::bin(OperandType::Str, OperandType::Str, OperandType::Int) }),
    // 位运算
    opdetail::bop(TokenType::BIT_AND,  5, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int) }),
    opdetail::bop(TokenType::BIT_OR,   5, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int) }),
    opdetail::bop(TokenType::BIT_XOR,  5, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int) }),
    // 逻辑运算（结果均为 Int）
    opdetail::bop(TokenType::AND,          4, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int) }),
    opdetail::bop(TokenType::OR,           4, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int) }),
    opdetail::bop(TokenType::LOGICAL_XOR,  4, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int) }),
    opdetail::bop(TokenType::LOGICAL_NAND, 4, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int) }),
    opdetail::bop(TokenType::LOGICAL_NOR,  4, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int) }),
    // 一元（优先级高于所有二元）
    opdetail::asUnary(opdetail::bop(TokenType::NOT,       11, {}), OperandType::Int, OperandType::Int),
    opdetail::asUnary(opdetail::bop(TokenType::BIT_NOT,   11, {}), OperandType::Int, OperandType::Int),
    opdetail::asUnary(opdetail::bop(TokenType::INCREMENT, 11, {}), OperandType::Int, OperandType::Int),
    opdetail::asUnary(opdetail::bop(TokenType::DECREMENT, 11, {}), OperandType::Int, OperandType::Int),
    // 一元 +/- 同时也是二元
    [] {
        auto d = opdetail::bop(TokenType::PLUS, 9, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int),
                                                     opdetail::bin(OperandType::Str, OperandType::Str, OperandType::Str) });
        return opdetail::asUnary(d, OperandType::Int, OperandType::Int);
    }(),
    [] {
        auto d = opdetail::bop(TokenType::MINUS, 9, { opdetail::bin(OperandType::Int, OperandType::Int, OperandType::Int) });
        return opdetail::asUnary(d, OperandType::Int, OperandType::Int);
    }(),
});

[[nodiscard]] constexpr const OperatorDef* findOperator(TokenType op) noexcept {
    for (const auto& d : kOperatorTable) {
        if (d.op == op) return &d;
    }
    return nullptr;
}

[[nodiscard]] constexpr int operatorPrecedence(TokenType op) noexcept {
    const OperatorDef* d = findOperator(op);
    return d ? d->precedence : 0;
}

// 二元结果类型推断：返回 nullopt 表示类型不匹配（解析期类型错误）
[[nodiscard]] constexpr std::optional<OperandType>
inferBinaryType(TokenType op, OperandType lhs, OperandType rhs) noexcept {
    const OperatorDef* d = findOperator(op);
    if (!d) return std::nullopt;
    for (std::uint8_t i = 0; i < d->binaryCount; ++i) {
        const BinarySignature& s = d->binaries[i];
        const bool lhsOk = (s.lhs == OperandType::Unknown) || (lhs == OperandType::Unknown) || (s.lhs == lhs);
        const bool rhsOk = (s.rhs == OperandType::Unknown) || (rhs == OperandType::Unknown) || (s.rhs == rhs);
        if (lhsOk && rhsOk) return s.result;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<OperandType>
inferUnaryType(TokenType op, OperandType operand) noexcept {
    const OperatorDef* d = findOperator(op);
    if (!d || d->unaryArity == 0) return std::nullopt;
    const bool ok = (d->unaryOperand == OperandType::Unknown)
                 || (operand == OperandType::Unknown)
                 || (d->unaryOperand == operand);
    if (!ok) return std::nullopt;
    return d->unaryResult;
}

#endif // AST_OPERATOR_TABLE_H
