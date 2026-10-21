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
#include "jump_markings.h"

namespace jumpmark {

// ---------------------------------------------------------------------------
// 标记区：跳转标记 + 控制转移预绑定（行号索引，等价 C# JumpTo / NextLine）
// ---------------------------------------------------------------------------

void buildJumpMarkings(ScriptData& data) {
    data.endifLines.clear();
    data.elseLines.clear();
    data.loopEndLines.clear();
    data.jumpTo.clear();
    data.jumpToEnd.clear();
    data.ifBranches.clear();
    data.catchLines.clear();
    data.endCatchLines.clear();
    data.endFuncLines.clear();
    data.funcEntries.clear();

    struct IfInfo {
        int ifLine = -1;
        int lastBranch = -1;
        QList<int> branches;   // ELSEIF/ELSE 行（C# IfCaseList）
    };
    struct LoopInfo {
        int startLine = -1;
        QString type;
    };
    // SELECTCASE 组（对齐 C# SELECTCASE_Instruction.IfCaseList + CASE.JumpTo）
    struct SelectInfo {
        int selectLine = -1;
        QList<int> caseLines;      // CASE / CASEELSE 行（按出现顺序）
        int lastCase = -1;
    };

    // TRYC / CATCH / ENDCATCH 的配对栈（对齐 C# ErbLoader.ParseFunction 的 nestStack）
    //   进入 TRYC*  -> 压入 (TRY，行号)
    //   遇到 CATCH  -> 若栈顶是 TRY：记录 tryc->catch，弹栈后压入 (CATCH，行号)
    //   遇到 ENDCATCH -> 若栈顶是 CATCH：记录 catch->endcatch，弹栈
    // 注意：eraTW 里绝大多数 TRYC* 是**没有** CATCH 的（972 : 73），
    // 所以这里只认「栈顶就是 TRY」的 CATCH；否则该 CATCH 属于别的结构，忽略。
    struct CatchInfo {
        bool isCatch = false;
        int  line = -1;
    };
    // TRYCALLLIST/TRYJUMPLIST/TRYGOTOLIST 的配对栈（对齐 C# ErbLoader nestStack）
    //   进入 TRY*LIST -> 压栈；体内 FUNC 条目 -> 归入栈顶列表；
    //   ENDFUNC -> 弹栈并记录列表行 -> ENDFUNC（C# pf.JumpTo = func）。
    struct ListInfo {
        int line = -1;
        QList<int> funcLines;
    };

    QList<IfInfo> ifStack;
    QList<LoopInfo> loopStack;
    QList<SelectInfo> selectStack;
    QList<CatchInfo> catchStack;
    QList<ListInfo> listStack;

    for (int i = 0; i < data.lines.size(); ++i) {
        LogicalLine& ll = data.lines[i];
        ll.lineIndex = i;
        if (i + 1 < data.lines.size()) {
            ll.nextLine = i + 1;
        }
        if (ll.kind == LineKind::FunctionLabel) {
            ifStack.clear();
            loopStack.clear();
            selectStack.clear();
            catchStack.clear();
            listStack.clear();
        }
        if (!ll.isInstruction()) {
            continue;
        }

        const QString& name = ll.functionName;

        if (name == QLatin1String("IF")) {
            ifStack.append({i, -1, {}});
        }
        else if (name == QLatin1String("SIF")) {
            // SIF：条件为假则跳过下一行
            int next = i + 1;
            while (next < data.lines.size() && data.lines[next].kind == LineKind::Null) ++next;
            const int after = qMin(next + 1, int(data.lines.size()));
            data.jumpTo[i] = after;
            data.elseLines[i] = after;
            ll.jumpTo = after;
        }
        else if (name == QLatin1String("ELSEIF") || name == QLatin1String("ELSE")) {
            if (!ifStack.isEmpty()) {
                IfInfo& cur = ifStack.last();
                const int prev = (cur.lastBranch >= 0) ? cur.lastBranch : cur.ifLine;
                data.elseLines[prev] = i;
                cur.branches.append(i);
                cur.lastBranch = i;
                // ELSEIF/ELSE 由“顺序落入”执行时，直接跳到 ENDIF 之后（C# state.JumpTo(ENDIF)）
            }
        }
        else if (name == QLatin1String("ENDIF")) {
            if (!ifStack.isEmpty()) {
                const IfInfo cur = ifStack.takeLast();
                const int after = i + 1;
                data.endifLines[cur.ifLine] = after;
                data.jumpTo[cur.ifLine] = after;
                data.lines[cur.ifLine].jumpTo = after;
                data.ifBranches[cur.ifLine] = cur.branches;
                for (int b : cur.branches) {
                    data.endifLines[b] = after;
                    data.jumpTo[b] = after;             // 顺序落入分支 -> 跳过到 ENDIF 之后
                    data.lines[b].jumpTo = after;
                }
            }
        }
        else if (name == QLatin1String("SELECTCASE")) {
            selectStack.append({i, {}, -1});
        }
        else if (name == QLatin1String("CASE") || name == QLatin1String("CASEELSE")) {
            if (!selectStack.isEmpty()) {
                SelectInfo& cur = selectStack.last();
                // 上一个 CASE 体结束 -> 直接跳到 ENDSELECT 之后（C# 是逐级跳到下一个 CASE）
                if (cur.lastCase >= 0) {
                    data.jumpTo[cur.lastCase] = -1;      // 占位，ENDSELECT 时统一填
                    data.lines[cur.lastCase].jumpTo = -2; // 标记「待填」
                }
                cur.caseLines.append(i);
                cur.lastCase = i;
            }
        }
        else if (name == QLatin1String("ENDSELECT")) {
            if (!selectStack.isEmpty()) {
                const SelectInfo cur = selectStack.takeLast();
                const int after = i + 1;
                // 每个 CASE / CASEELSE 顺序落入 -> 跳到 ENDSELECT 之后
                for (int c : cur.caseLines) {
                    data.jumpTo[c] = after;
                    data.lines[c].jumpTo = after;
                }
                // SELECTCASE 的默认目标（无 CASE 命中）
                data.jumpTo[cur.selectLine] = after;
                data.jumpToEnd[cur.selectLine] = i;      // ENDSELECT 行
                data.lines[cur.selectLine].jumpTo = after;
                data.ifBranches[cur.selectLine] = cur.caseLines;
            }
        }
        else if (name == QLatin1String("REPEAT") || name == QLatin1String("WHILE")
                 || name == QLatin1String("FOR") || name == QLatin1String("DO")) {
            loopStack.append({i, name});
        }
        else if (name == QLatin1String("LOOP") || name == QLatin1String("REND") || name == QLatin1String("WEND")
                 || name == QLatin1String("NEXT")) {
            const QString expected = (name == QLatin1String("REND")) ? QStringLiteral("REPEAT")
                                   : (name == QLatin1String("LOOP"))
                                       ? ((!loopStack.isEmpty() && loopStack.last().type == QLatin1String("DO"))
                                           ? QStringLiteral("DO") : QStringLiteral("REPEAT"))
                                   : (name == QLatin1String("WEND")) ? QStringLiteral("WHILE")
                                                                     : QStringLiteral("FOR");
            if (!loopStack.isEmpty() && loopStack.last().type == expected) {
                const LoopInfo info = loopStack.takeLast();
                data.loopEndLines[info.startLine] = i;
                data.jumpToEnd[info.startLine] = i;
                data.jumpTo[i] = info.startLine;
                data.lines[i].jumpTo = info.startLine;
            }
        }
        // ---- TRYC 异常块：TRYCCALL(FORM) / TRYCJUMP(FORM) / TRYCGOTO(FORM) ----
        else if (name == QLatin1String("TRYCCALL") || name == QLatin1String("TRYCCALLFORM")
                 || name == QLatin1String("TRYCJUMP") || name == QLatin1String("TRYCJUMPFORM")
                 || name == QLatin1String("TRYCGOTO") || name == QLatin1String("TRYCGOTOFORM")) {
            catchStack.append({false, i});
        }
        else if (name == QLatin1String("CATCH")) {
            if (!catchStack.isEmpty() && !catchStack.last().isCatch) {
                const CatchInfo tryInfo = catchStack.takeLast();
                data.catchLines[tryInfo.line] = i;          // TRYC -> CATCH
                catchStack.append({true, i});
            }
        }
        else if (name == QLatin1String("ENDCATCH")) {
            if (!catchStack.isEmpty() && catchStack.last().isCatch) {
                const CatchInfo catchInfo = catchStack.takeLast();
                data.endCatchLines[catchInfo.line] = i;     // CATCH -> ENDCATCH
            }
        }
        // ---- TRYCALLLIST/TRYJUMPLIST/TRYGOTOLIST（+GOTOLIST 宽松别名）----
        // 体内只有 FUNC 单行条目，ENDFUNC 收尾（对齐 C# ErbLoader nestStack）。
        else if (name == QLatin1String("TRYCALLLIST") || name == QLatin1String("TRYJUMPLIST")
                 || name == QLatin1String("TRYGOTOLIST") || name == QLatin1String("GOTOLIST")) {
            ListInfo info;
            info.line = i;
            listStack.append(info);
        }
        else if (name == QLatin1String("FUNC")) {
            if (!listStack.isEmpty()) {
                listStack.last().funcLines.append(i);
            }
        }
        else if (name == QLatin1String("ENDFUNC")) {
            if (!listStack.isEmpty()) {
                const ListInfo listInfo = listStack.takeLast();
                data.endFuncLines[listInfo.line] = i;       // TRY*LIST -> ENDFUNC
                data.funcEntries[listInfo.line] = listInfo.funcLines;
            }
        }
    }
}

}  // namespace jumpmark
