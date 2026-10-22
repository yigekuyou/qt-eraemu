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
#ifndef ERAENGINE_GAMEPROC_PARSE_CHECKS_H
#define ERAENGINE_GAMEPROC_PARSE_CHECKS_H

#include <QHash>
#include <QSet>
#include <QString>

#include "era_parse_table.h"   // ScriptData
#include "ast/logical_line.h"  // LogicalLine（checkCallStatementArgs 的形参）
#include "ast/parse_diagnostic.h"
#include "ast/user_function.h"

class ConstantTable;
class VariableStorage;
class VariableTable;

// ---------------------------------------------------------------------------
// 装载收尾的**只读**步骤（收尾 pass 表里的 4/5/6 号 pass + CALL 实参校验）
//
// 这三个步骤只读取已经定型的 IR 并产出诊断：不改 AST、不改装载状态、不改运行
// 行为。因此它们的实现不依赖 EraParseTable —— 输入是一个**全部 const 的视图**
// （编译期禁止写），输出写进调用方给的 ParseDiagnostics。
//
// 于是它们可以脱离 markLoadComplete 单独调用（LSP 按需重跑、测试直接取证），
// EraParseTable 里只剩同名 forwarder（保住 public API 与 pass 表的成员指针）。
//
// 调用前提（与 EraParseTable 里的调用点一致）：
//   * scripts / variables 必须非空；
//   * functions 为空 = 「没有登记任何用户函数」（此时 RETURNF 与 CALL 实参校验
//     按「找不到函数」处置，与空函数表等价，不会误报结构问题）；
//   * storage / constantTable 可以为空（checkStaticValues 相应少判尺寸/名表）。
// ---------------------------------------------------------------------------
namespace parsecheck {

// 只读收尾步骤的输入：装载完成的 IR 视图。全部 const —— 编译期就禁止写。
struct ParseIrView {
    const QHash<QString, ScriptData>* scripts = nullptr;
    // 唯一标签索引（裸名大写折叠 -> 全部声明）。resolveStaticTargets 建「全库
    // 标签名并集」只读它的键 —— ScriptData 里不再有第二张 name->line 表。
    const QHash<QString, QList<LabelRef>>* labelIndex = nullptr;
    const VariableTable* variables = nullptr;
    const ConstantTable* constantTable = nullptr;
    const VariableStorage* storage = nullptr;   // 只用于尺寸/名字查询
    // 用户函数登记表（@label 的 #FUNCTION/#DIM 形参/返回类型）。
    // 结构检查的 RETURNF 与 CALL 实参校验需要它 —— EraParseTable::userFunction
    // 读的就是这张表，视图里必须一并给出，否则只能靠传不了的回调。
    const QHash<QString, UserFunctionInfo>* functions = nullptr;
};

// ---- 逐脚本版本 ------------------------------------------------------------
//
// 一个脚本一趟：只读 view + **一个** 脚本的数据，诊断写进调用方给的 diags。
// 因此 diags 必须是**本线程独占**的 sink（驱动端每个任务自己一个实例）。
//
// 这三个函数之间的独立性来自数据：脚本之间没有共享可变状态（pass 4 只读全库
// 的标签索引键，pass 5/6 只读本脚本 lines + 全局只读表），所以可以被
// 驱动端并行调用；下面三个「遍历全部脚本」的 public 函数现在就是它们的并行
// 驱动（脚本名排序 -> 并行映射 -> 按名字序归并）。
//
// allLabels = 全库标签名并集（大写折叠键），由**驱动层**在一次 pass 内建一次、
// 以 const 引用传给各任务 —— 任务里退化成 O(1) 成员判定。
//
// scriptName 是**脚本表的键**（不是 position.filename）—— GOTO 的「$ 标签只在
// 本脚本内」要拿它去查唯一标签索引（LabelRef::script 记的正是这个键）。
// 驱动层本来就按名字遍历，顺手传进来，避免让任务自己反推脚本名。
void resolveStaticTargetsInScript(const ParseIrView& view, const QString& scriptName,
                                  const ScriptData& script, ParseDiagnostics& diags,
                                  const QSet<QString>& allLabels);
void checkStructuresInScript(const ParseIrView& view, const ScriptData& script,
                             ParseDiagnostics& diags);
void checkStaticValuesInScript(const ParseIrView& view, const ScriptData& script,
                               ParseDiagnostics& diags);

// 收尾步骤 4：静态解析运行期跳转/调用目标（对齐 C# setJumpTo）。
void resolveStaticTargets(const ParseIrView& view, ParseDiagnostics& diags);

// CALL 语句的实参校验（对齐 C# ConvertArg 在 setJumpTo 里的装载期调用）。
// 只在「常量目标且目标存在」时下结论 —— 由 resolveStaticTargets 调用。
void checkCallStatementArgs(const ParseIrView& view, const LogicalLine& line,
                            const QString& target, ParseDiagnostics& diags);

// 收尾步骤 5：结构/配对检查（对齐 C# ErbLoader.nestCheck 的诊断部分）。
void checkStructures(const ParseIrView& view, ParseDiagnostics& diags);

// 收尾步骤 6：可静态判定的运行期错误（字面量除零 / 常量下标越界）。
void checkStaticValues(const ParseIrView& view, ParseDiagnostics& diags);

}  // namespace parsecheck

#endif  // ERAENGINE_GAMEPROC_PARSE_CHECKS_H