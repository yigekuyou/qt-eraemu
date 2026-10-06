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
    // 结合性：false = 左结合（EraBasic 全部运算符的默认，对齐 C# TermStack 的
    // 「栈顶优先级 ≥ 新优先级即归约」）；true = 右结合（本表预留，见 operatorBindingPower）。
    bool        rightAssoc = false;
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

// 标记为右结合（EraBasic 原生运算符都是左结合，本表预留）
constexpr OperatorDef asRightAssoc(OperatorDef d) {
    d.rightAssoc = true;
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

// ---------------------------------------------------------------------------
// 绑定力（Pratt / precedence climbing 用）
//
//   left  = 与「当前最小绑定力」比较的阈值（< 则停止，交给外层）；
//   right = 递归解析右操作数时传入的最小绑定力。
//
// 结合性由此自然表达（对齐 matklad「Simple but Powerful Pratt Parsing」）：
//   左结合：{p, p+1}    —— 同优先级的下一个运算符会停止递归 → 先归约左边
//   右结合：{p, p  }    —— 同优先级继续递归 → 归约到右边
// 实测：A - B - C（左，p=9）=> ((A-B)-C)；若把 - 标为右结合 => (A-(B-C))。
//
// 非二元运算符返回 {0, 0}（left==0 即「不是二元运算符」的哨兵）。
// ---------------------------------------------------------------------------
struct BindingPower {
    int left  = 0;
    int right = 0;
};

[[nodiscard]] constexpr BindingPower operatorBindingPower(TokenType op) noexcept {
    const OperatorDef* d = findOperator(op);
    if (!d || d->precedence == 0) return BindingPower{0, 0};
    return BindingPower{d->precedence, d->precedence + (d->rightAssoc ? 0 : 1)};
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
