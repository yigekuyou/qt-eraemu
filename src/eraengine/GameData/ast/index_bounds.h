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
#ifndef ERAENGINE_AST_INDEX_BOUNDS_H
#define ERAENGINE_AST_INDEX_BOUNDS_H

#include <QString>

#include <optional>

#include "parse_diagnostic.h"

class ExpressionNode;

// ---------------------------------------------------------------------------
// 下标/尺寸判定与诊断处置的**唯一一份**事实来源。
//
// 收尾 3/6（validateExpression）和 6/6（checkStaticValues）都要判「这个下标
// 是否越界/非法」。此前的分工只写在两边注释里：3/6 判已声明数组的纯字面量
// 与维数，6/6 补负字面量、系统变量尺寸、字面字符串。两边各写一份下界/维度
// 映射，任何一边改动都可能让同一个 `X:…` 得到两个结论。
//
// 现在：
//   * 判定函数（elementDimension / isOutOfRange / constantIndexValue /
//     hasConstantDivisionByZero）只有这里一份，两趟都调用；
//   * 每类事实的 severity / effect / **唯一生产者**（DiagOrigin）是
//     issuePolicy() 的表，不再是对面的第二份 if；
//   * 诊断文本也只有这里一份（QCoreApplication::translate 的同一个上下文）。
//
// 生产者的口径：
//   Pass3 —— 3/6 的 AST 已经带 lengths / 维数，能当场判定并使该行失效；
//            它的结论就是运行期事实（line.isError）。
//   Pass6 —— 3/6 看不见的形态（负字面量、系统变量尺寸、名表字符串、
//            lengths 缺失时的上界），只报告、不禁行（运行期宽容）。
// 6/6 对已被 3/6 判为失效的行整体让位（checkStaticValuesInScript 的第一道
// 守卫）：同一行不允许同时出现 RejectLine 与 ReportOnly。
// ---------------------------------------------------------------------------
namespace indexbounds {

enum class Kind : quint8 {
    TooManyIndices,            // 下标项数 > 3
    DimensionMismatch,         // 与声明维数不符 / 缺少下标
    DeclaredLiteralOutOfRange, // 已声明数组：纯字面量越过 lengths
    NegativeConstantIndex,     // 常量下标 < 0（含 -字面量）
    SystemSizeExceeded,        // 系统/角色系统变量：越过 VariableSize
    UnknownStringIndex,        // 字面字符串下标不在常数名表
    ConstantDivisionByZero,    // 常量表达式除以/模 0（装载期求值必失败）
    DynamicDivisionByZero,     // 除数字面 0、被除数含变量（运行期才知道）
};

struct Policy {
    DiagSeverity severity;
    DiagEffect effect;
    DiagOrigin producer;   // 该事实的唯一生产者
};

// 表征（唯一一份）。
[[nodiscard]] const Policy& issuePolicy(Kind kind) noexcept;
[[nodiscard]] const char* kindName(Kind kind) noexcept;

// ---- 判定（唯一一份）----

// 第 indexPos 个下标对应哪个元素维；返回 -1 = 角色号位（没有元素维）。
// 语义与 3/6 旧实现一致：角色数据只有在下标项数 > 声明维数时才多出号位。
[[nodiscard]] int elementDimension(int indexPos, int dimension, bool characterData,
                                   int indexCount) noexcept;

// 下标下界恒为 0。size <= 0 = 尺寸未知 -> 不判上界。
[[nodiscard]] bool isOutOfRange(qint64 value, int size) noexcept;

// 3/6 能看见该位置的纯字面量上界（lengths > 0）；6/6 据此让位，不重复报。
[[nodiscard]] bool pass3CoversLiteralBounds(int length) noexcept;

// 常量下标：字面量，或 -字面量（*negated = true）。其它形态返回 nullopt。
[[nodiscard]] std::optional<qint64> constantIndexValue(const ExpressionNode& node,
                                                       bool* negated = nullptr);

// 子树里是否有「常量除/模零」节点。调用方负责确认子树本身是常量
// （含变量/函数调用的子树不在此列）。
[[nodiscard]] bool hasConstantDivisionByZero(const ExpressionNode& node);

// 子树是否可在装载期求值（不含变量/函数调用，也不含 ++/--）—— 3/6 与 6/6
// 共用同一份判据；除零的「常量 / 运行期」两分就由它决定。
[[nodiscard]] bool isConstantSubtree(const ExpressionNode& node);

// ---- 诊断文本（唯一副本；逐字保持既有文案）----
[[nodiscard]] QString tooManyIndicesMessage();
[[nodiscard]] QString dimensionMismatchMessage(const QString& name);
[[nodiscard]] QString declaredLiteralOutOfRangeMessage(const QString& name);
[[nodiscard]] QString negativeIndexMessage(const QString& name, qint64 value);
[[nodiscard]] QString beyondSizeMessage(const QString& name, qint64 value, int size);
[[nodiscard]] QString unknownStringIndexMessage(const QString& name, const QString& text);
[[nodiscard]] QString divisionByZeroMessage();

}  // namespace indexbounds

#endif  // ERAENGINE_AST_INDEX_BOUNDS_H
