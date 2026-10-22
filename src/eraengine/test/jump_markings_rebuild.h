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
// ---------------------------------------------------------------------------
// jump_markings_rebuild.h —— **旧整表副本的恢复口径**（只在测试里用）
//
// ScriptData 曾经除 jumpTo 表之外还存着三份「同一配对事实的第二份副本」：
//   endifLines : IF/SIF 行 -> ENDIF+1
//   elseLines  : IF/SIF 行 -> ELSEIF/ELSE/ENDIF（下一分支）
//   jumpToEnd  : 循环开始行 -> 结束行（外加 SELECTCASE -> ENDSELECT）
// 运行期无人读它们，唯一读者是测试。三份都可以从**运行期真正在读的那份事实**
// 精确重建，所以副本删掉了，测试照旧能断言同一件事：
//
//   endifLines[L]        <- jumpTarget(L)                       （L = IF / 它的分支行）
//   elseLines[SIF]       <- jumpTarget(SIF)                     （= 跨过下一行）
//   elseLines[IF/branch] <- ifBranches 链（IF -> b1 -> b2 -> …）
//   jumpToEnd[loop]      <- 收尾行的回边 lines[closer].jumpTo == opener
//   jumpToEnd[SELECTCASE]<- jumpTo == ENDSELECT+1，故 ENDSELECT = jumpTo-1
//
// 三条口径都只用「运行期读者实际会读的字段」：
//   * jumpTarget / ifBranches 是 jump_markings.h 的读侧自由函数（生产在用）；
//   * lines[i].jumpTo 是行上的扁平字段，也是跳转事实的**唯一**存放在处
//     （ScriptData 的 jumpTo QHash 副本已删除）。
// 换句话说：删掉的表没有携带任何「行字段里没有」的信息。
//
// 本文件只被 test/*.cpp 包含，不进生产代码。
// ---------------------------------------------------------------------------
#ifndef ERAENGINE_TEST_JUMP_MARKINGS_REBUILD_H
#define ERAENGINE_TEST_JUMP_MARKINGS_REBUILD_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "ast/logical_line.h"
#include "era_parse_table.h"   // ScriptData
#include "jump_markings.h"

namespace rebuild {

inline bool isLoopCloserName(const QString& n) {
    return n == QLatin1String("REND") || n == QLatin1String("NEXT")
        || n == QLatin1String("WEND") || n == QLatin1String("LOOP");
}

// 旧 endifLines：IF 行与它的 ELSEIF/ELSE 分支行 -> ENDIF+1。
// 分支集合从哪里来？ifBranches（SELECTCASE 用同一张表，靠行上的指令名区分）。
inline QHash<int, int> endifLines(const ScriptData& d) {
    QHash<int, int> out;
    for (auto it = d.ifBranches.constBegin(); it != d.ifBranches.constEnd(); ++it) {
        const int ifLine = it.key();
        if (ifLine < 0 || ifLine >= d.lines.size()) continue;
        if (d.lines.at(ifLine).functionName != QLatin1String("IF")) continue;
        const int after = jumpmark::jumpTarget(&d, ifLine);
        if (after < 0) continue;
        out.insert(ifLine, after);
        for (int b : it.value()) {
            const int t = jumpmark::jumpTarget(&d, b);
            if (t >= 0) out.insert(b, t);
        }
    }
    return out;
}

// 旧 elseLines：IF/SIF 行 -> 下一分支（没有下一分支就不进表）。
//   * SIF：目标就是「跳过下一行」的那个位置，与 jumpTo 同一个值；
//   * IF ：分支链由 ifBranches 给出，IF -> 第 1 个分支 -> 第 2 个分支 …
inline QHash<int, int> elseLines(const ScriptData& d) {
    QHash<int, int> out;
    for (int i = 0; i < d.lines.size(); ++i) {
        const LogicalLine& ll = d.lines.at(i);
        if (!ll.isInstruction()) continue;
        if (ll.functionName == QLatin1String("SIF")) {
            if (ll.jumpTo >= 0) out.insert(i, ll.jumpTo);
            continue;
        }
        if (ll.functionName != QLatin1String("IF")) continue;
        int prev = i;
        const QList<int> branches = d.ifBranches.value(i);
        for (int b : branches) {
            out.insert(prev, b);
            prev = b;
        }
    }
    return out;
}

// 旧 jumpToEnd：循环开始行 -> 收尾行；SELECTCASE -> ENDSELECT 行。
//   * 循环：唯一的回边在收尾行上（lines[closer].jumpTo = opener），反过来读；
//   * SELECTCASE：开块行的 jumpTo = ENDSELECT+1（顺序落入跳过后面的位置），
//     所以 ENDSELECT = jumpTo-1，并核对那一行确实是 ENDSELECT。
inline QHash<int, int> jumpToEnd(const ScriptData& d) {
    QHash<int, int> out;
    for (int i = 0; i < d.lines.size(); ++i) {
        const LogicalLine& ll = d.lines.at(i);
        if (!ll.isInstruction()) continue;
        if (isLoopCloserName(ll.functionName)) {
            const int opener = ll.jumpTo;
            if (opener >= 0) out.insert(opener, i);
        }
    }
    for (int i = 0; i < d.lines.size(); ++i) {
        const LogicalLine& ll = d.lines.at(i);
        if (ll.functionName != QLatin1String("SELECTCASE")) continue;
        const int after = ll.jumpTo;
        if (after <= 0 || after > d.lines.size()) continue;
        const int endselect = after - 1;
        if (endselect < 0 || endselect >= d.lines.size()) continue;
        const LogicalLine& closer = d.lines.at(endselect);
        if (closer.isInstruction() && closer.functionName == QLatin1String("ENDSELECT"))
            out.insert(i, endselect);
    }
    return out;
}

// 「jumpTo 目标必须落在本脚本里」：遍历**全部行**，凡 jumpTo >= 0 都要求
//   0 <= target <= lines.size()（== size 是「脚本结束」这个合法位置，SIF 在
//   末行时就是它），且 target < size 时那一行属于同一脚本：
//     * 目标行的位置文件名 == 源行的位置文件名（跳转不跨文件）；
//     * 给了 ScriptData::path 时再要求目标行文件名 == path。
// 判定本身与生产侧探针共用 jumpmark::countBadJumpTargets（口径只有一份），
// 这里只做转发，返回不合规的条数。
inline int countBadJumpTargets(const ScriptData& d, QStringList* details = nullptr) {
    return jumpmark::countBadJumpTargets(d, details);
}

// 行字段的只读投影（**测试专用**）：行 -> lines[i].jumpTo（只收 >= 0 的行）。
// jumpTo 的唯一事实是行字段；旧断言里的「两次构建结果相同」「表为空」用它
// 表达同一件事，不引入第二份可变写入。
inline QHash<int, int> jumpFields(const ScriptData& d) {
    QHash<int, int> out;
    for (int i = 0; i < d.lines.size(); ++i) {
        if (d.lines.at(i).jumpTo >= 0) out.insert(i, d.lines.at(i).jumpTo);
    }
    return out;
}

// 表 -> 排序后的「k:v」文本（逐项比对与摘要都用它）。
inline QStringList tableLines(const QHash<int, int>& table) {
    QStringList out;
    out.reserve(table.size());
    for (auto it = table.constBegin(); it != table.constEnd(); ++it)
        out << QStringLiteral("%1:%2").arg(it.key()).arg(it.value());
    out.sort();
    return out;
}

}  // namespace rebuild

#endif  // ERAENGINE_TEST_JUMP_MARKINGS_REBUILD_H