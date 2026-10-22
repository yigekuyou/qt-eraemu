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
#include <QStringList>

#include "era_parse_table.h"      // ScriptData
#include "structure_issue.h"      // StructureIssue（本文件的另一半产物）

// ---------------------------------------------------------------------------
// 标记区：在**已定型的 AST** 之上补 O(1) 跳转表
//
// 本文件不认识 EraParseTable，只认识 ScriptData —— 输入就是那一份数据本身：
//   * 构建 buildJumpMarkings(ScriptData&)  —— 可写：清表、逐行配对、回填
//     lines[i].lineIndex / nextLine / jumpTo（跳转事实只有行字段这一份）；
//   * 读侧 6 个自由函数(const ScriptData*) —— 只读，nullptr 安全。
//
// 于是「怎么配对」是**数据**问题（ScriptData 里的行号索引表 +
// structureIssues），「怎么查」就是按行号读行字段；两者都不需要 EraParseTable
// 的任何成员状态。所有行号都是**逻辑行下标**（0 起），与 C# 的指针式
// JumpTo/NextLine 等价。
//
// **配对语法的唯一实现**：本文件那一趟 for 循环。它同时产出两个投影——
//   ① 跳转表（各行上的 lineIndex / nextLine / jumpTo，运行期消费）；
//   ② ScriptData::structureIssues（装载期诊断消费）。
// 收尾第 5 步（parse_checks 的结构检查）不再自己扫行配对，只读 ②。
// ---------------------------------------------------------------------------
namespace jumpmark {

// 构建：清空 7 张表与 structureIssues 后按行重建（含 lines[i].lineIndex /
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
// **唯一事实**是行字段 lines[i].jumpTo：buildJumpMarkings 只往行上写一次，
// 这里直接按下标取，不再维护第二份 QHash 副本。越界行号 = 没有这一行 -> -1；
// 存下来的值可以是 lines.size()（「脚本结束」这个合法位置）。
inline int jumpTarget(const ScriptData* data, int line) {
    if (!data || line < 0 || line >= data->lines.size()) return kNoJumpTarget;
    return data->lines.at(line).jumpTo;
}

// 行字段自洽性检查（探针/测试用，运行期不调）：遍历**全部行**，凡
// lines[i].jumpTo >= 0，要求
//   0 <= target <= lines.size()            （== size 是「脚本结束」这个合法位置）
// 且 target < size 时目标行属于同一脚本：
//   * 目标行的位置文件名 == 源行的位置文件名（跳转不跨文件）；
//   * 给了 ScriptData::path 时再要求目标行文件名 == path。
// 返回违例条数；details 里给「源行 -> 目标行」文本。0 违例 = 行字段就是一份
// 合法且已解析的跳转事实（没有未解析哨兵，也没有越界/跨脚本目标）。
inline int countBadJumpTargets(const ScriptData& data, QStringList* details = nullptr) {
    int bad = 0;
    for (int i = 0; i < data.lines.size(); ++i) {
        const int t = data.lines.at(i).jumpTo;
        if (t < 0) continue;
        bool ok = t <= data.lines.size();
        if (ok && t < data.lines.size()) {
            const LogicalLine& target = data.lines.at(t);
            ok = target.position.filename == data.lines.at(i).position.filename;
            if (ok && !data.path.isEmpty()) ok = target.position.filename == data.path;
        }
        if (!ok) {
            ++bad;
            if (details) {
                details->append(QStringLiteral("lines[%1].jumpTo = %2（lines.size() = %3）")
                                    .arg(i).arg(t).arg(data.lines.size()));
            }
        }
    }
    return bad;
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