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
#ifndef ERAENGINE_GAMEPROC_JUMP_MARKINGS_H
#define ERAENGINE_GAMEPROC_JUMP_MARKINGS_H

#include <QHash>
#include <QList>
#include <QString>

#include "era_parse_table.h"      // ScriptData
#include "structure_issue.h"      // StructureIssue（本文件的另一半产物）

// ---------------------------------------------------------------------------
// 标记区：在**已定型的 AST** 之上补 O(1) 跳转表
//
// 本文件不认识 EraParseTable，只认识 ScriptData —— 输入就是那一份数据本身：
//   * 构建 buildJumpMarkings(ScriptData&)  —— 可写：清表、逐行配对、回填
//     lines[i].lineIndex / nextLine / jumpTo；
//   * 读侧 6 个自由函数(const ScriptData*) —— 只读，nullptr 安全。
//
// 于是「怎么配对」是**数据**问题（ScriptData 里的 10 张行号索引表 +
// structureIssues），「怎么查」就是 QHash::value；两者都不需要 EraParseTable
// 的任何成员状态。所有行号都是**逻辑行下标**（0 起），与 C# 的指针式
// JumpTo/NextLine 等价。
//
// **配对语法的唯一实现**：本文件那一趟 for 循环。它同时产出两个投影——
//   ① 跳转表 + lines[i].lineIndex / nextLine / jumpTo（运行期消费）；
//   ② ScriptData::structureIssues（装载期诊断消费）。
// 收尾第 5 步（parse_checks 的结构检查）不再自己扫行配对，只读 ②。
// ---------------------------------------------------------------------------
namespace jumpmark {

// 构建：清空 10 张表与 structureIssues 后按行重建（含 lines[i].lineIndex /
// nextLine 的显式赋值）。幂等：重复调用结果相同（先全部 clear）。
void buildJumpMarkings(ScriptData& data);

// ---------------------------------------------------------------------------
// 配对词汇（唯一一份）：开块 <-> 收尾。
//
// 判据（哪些名字算开块/收尾/循环体）只在这里写一次；装载期配对、诊断文案、
// 体内允许清单全部调这里 —— 这正是收敛前「两份判定各写一遍」的地方。
// ---------------------------------------------------------------------------
bool isLoopOpener(const QString& name);       // REPEAT / FOR / WHILE / DO
bool isPrintDataFamily(const QString& name);  // PRINTDATA* / STRDATA
bool isTryListOpener(const QString& name);    // TRYCALLLIST / TRYJUMPLIST / TRYGOTOLIST / GOTOLIST
bool isTryCatchOpener(const QString& name);   // TRYCCALL(FORM) / TRYCJUMP(FORM) / TRYCGOTO(FORM)

// 收尾指令 -> 它要求的开块名（"" = 不是收尾指令）。LOOP 的特例（DO 或 REPEAT）
// 见 buildJumpMarkings 的判定；这里返回固定的 "REPEAT"（与诊断文案一致）。
QString requiredOpener(const QString& closer);
// 开块指令 -> 它要求的收尾名（"" = 不是开块）。诊断「X に対応する Y」用它。
QString requiredCloser(const QString& opener);

// 「没有跳转目标」：该行不是可跳转指令，或其配对在装载期未解析（此时
// ScriptData::structureIssues 里必有一条 UnresolvedOpener / OrphanCloser）。
// 运行期照旧把它交给 gotoLine(-1)，由位置区把负数钳到行 0 —— 既有语义。
inline constexpr int kNoJumpTarget = -1;

// ---- 读侧：全部是对 ScriptData 内一张表的查表；nullptr = 没有脚本 ----

// 行 -> 控制转移目标行（C# JumpTo）；-1 = 无。
inline int jumpTarget(const ScriptData* data, int line) {
    if (!data) return -1;
    return data->jumpTo.value(line, -1);
}

// IF 行 -> 其 ELSEIF/ELSE 分支行列表（C# IfCaseList）；空 = 无分支。
// SELECTCASE 行走同一张表：行 -> 其 CASE/CASEELSE 行列表。
inline QList<int> ifBranches(const ScriptData* data, int ifLine) {
    if (!data) return QList<int>();
    return data->ifBranches.value(ifLine, QList<int>());
}

// TRYC 系异常块：TRYC 行 -> 配对 CATCH 行（失败时跳到它**之后**）；-1 = 无配对。
inline int catchTarget(const ScriptData* data, int trycLine) {
    if (!data) return -1;
    return data->catchLines.value(trycLine, -1);
}

// CATCH 行 -> 配对 ENDCATCH 行（顺序落入时跳到它之后）；-1 = 无配对。
inline int endCatchTarget(const ScriptData* data, int catchLine) {
    if (!data) return -1;
    return data->endCatchLines.value(catchLine, -1);
}

// TRYCALLLIST/TRYJUMPLIST/TRYGOTOLIST：行 -> 体内 FUNC 条目行（按出现顺序）。
inline QList<int> funcEntryLines(const ScriptData* data, int listLine) {
    if (!data) return {};
    return data->funcEntries.value(listLine);
}

// TRY*LIST 行 -> 配对 ENDFUNC 行（全部候选失败时跳到它**之后**）；-1 = 无配对。
inline int endFuncTarget(const ScriptData* data, int listLine) {
    if (!data) return -1;
    return data->endFuncLines.value(listLine, -1);
}

}  // namespace jumpmark

#endif  // ERAENGINE_GAMEPROC_JUMP_MARKINGS_H