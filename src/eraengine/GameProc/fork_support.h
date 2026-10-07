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
#pragma once

#include <QString>
#include <QVariant>

class ExpressionNode;
class VariableStorage;

// ---------------------------------------------------------------------------
// fork_support —— EM/Emuera.NET 族扩展的**共享小工具**（ref 实参 / 数组输出）。
//
// 这批 .NET 函数与既有 EE 式中函数最大的不同：它们有 **ref 数组输出**
// （`XML_GET(..., ref str[])`、`DT_SELECT(..., ref int[])`、`ENUM*(pat, ref str[])`、
// `MAP_GETKEYS(name, ref str[], n)`）。注册类的 ExprFn 同时给出实参 AST 节点，
// 于是「取第 n 个实参是不是一个变量、叫什么」这件事在这里统一。
// ---------------------------------------------------------------------------
namespace forksupport {

// 实参节点是变量（VariableTerm）时返回其名字，否则返回空串
// （字面量/表达式不能作为输出目标 —— 对齐 C#「只有 VariableTerm 才回写」）。
[[nodiscard]] QString refTargetName(const ExpressionNode* node);

// 目标数组容量：系统变量走 VariableConfig（RESULT=1500 / RESULTS=100 等），
// 用户变量走 VariableConfig / 已建容器（两者都拿不到时返回 fallback）。
[[nodiscard]] int arrayCapacity(VariableStorage* storage, const QString& name);

// 写入一维数组元素（系统变量 -> 系统槽，用户变量 -> 全局槽）。
void writeInt(VariableStorage* storage, const QString& name, int index, qint64 value);
void writeStr(VariableStorage* storage, const QString& name, int index, const QString& value);

// 实参缺省判定（求值器把「省略实参」表示成无效 QVariant）
[[nodiscard]] inline bool omitted(const QVariant& v) { return !v.isValid(); }

// 「前缀匹配」助手：C# 一律 ToUpper() 后比较（大小写不敏感）。
[[nodiscard]] bool matchesPrefix(const QString& name, const QString& pattern);
[[nodiscard]] bool matchesSuffix(const QString& name, const QString& pattern);
[[nodiscard]] bool matchesContains(const QString& name, const QString& pattern);

} // namespace forksupport
