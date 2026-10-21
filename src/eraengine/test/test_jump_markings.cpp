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
// test_jump_markings.cpp
//
// 验收物：标记区（ScriptData 上的跳转表）可以**脱离 EraParseTable / 解析器**
// 单独构建与查询。
//
// 做法：不用编译器，直接手搓 ScriptData —— LogicalLine 只需要 kind
//（Instruction / FunctionLabel / Null）与 functionName（大写指令名），
// 因为 buildJumpMarkings 只看这两项（isInstruction() 只判 kind）。
// 于是每条配对规则都能用「指令名序列」直接钉死：
//   IF/ELSEIF/ELSE/ENDIF、SIF、SELECTCASE、四种循环、TRYC/CATCH/ENDCATCH、
//   TRY*LIST/FUNC/ENDFUNC、跨函数清栈、幂等、nullptr。
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "ast/logical_line.h"
#include "era_parse_table.h"   // ScriptData
#include "jump_markings.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

// 指令名序列 -> ScriptData。
//   "IF" / "CASE" …  -> LineKind::Instruction（functionName = 大写指令名）
//   "@NAME"          -> LineKind::FunctionLabel（函数标签行）
//   "<null>"         -> LineKind::Null（空行/注释）
static ScriptData makeData(const QStringList& names) {
    ScriptData d;
    d.lines.reserve(names.size());
    for (const QString& n : names) {
        LogicalLine ll;
        if (n == QLatin1String("<null>")) {
            ll.kind = LineKind::Null;
        } else if (n.startsWith(QLatin1Char('@'))) {
            ll.kind = LineKind::FunctionLabel;
            ll.labelName = n.mid(1);
        } else {
            ll.kind = LineKind::Instruction;
            ll.functionName = n;
        }
        d.lines.append(ll);
    }
    return d;
}

static QString listText(const QList<int>& v) {
    QStringList parts;
    for (int x : v) parts << QString::number(x);
    return QStringLiteral("[") + parts.join(QLatin1Char(',')) + QStringLiteral("]");
}

// 期望值比对，失败时把实际值打进消息（行号错了要能一眼看出差多少）。
static void checkInt(int actual, int expected, const QString& what) {
    check(actual == expected,
          QString("%1 = %2（期望 %3）").arg(what).arg(actual).arg(expected));
}

static void checkList(const QList<int>& actual, const QList<int>& expected, const QString& what) {
    check(actual == expected,
          QString("%1 = %2（期望 %3）").arg(what, listText(actual), listText(expected)));
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    qDebug() << "Jump markings (free functions over ScriptData)";
    qDebug() << "=============================================";

    // ---- 1) IF / ELSEIF / ELSE / ENDIF ----
    {
        qDebug() << "\n[1] IF / ELSEIF / ELSE / ENDIF";
        ScriptData d = makeData({"IF", "NOP", "ELSEIF", "NOP", "ELSE", "NOP", "ENDIF", "NOP"});
        jumpmark::buildJumpMarkings(d);
        checkInt(d.endifLines.value(0, -1), 7, "lines.endifLines[IF]");
        checkInt(d.endifLines.value(2, -1), 7, "lines.endifLines[ELSEIF]");
        checkInt(d.endifLines.value(4, -1), 7, "lines.endifLines[ELSE]");
        checkInt(d.elseLines.value(0, -1), 2, "lines.elseLines[IF]（下一分支）");
        checkInt(d.elseLines.value(2, -1), 4, "lines.elseLines[ELSEIF]（下一分支）");
        checkList(d.ifBranches.value(0), {2, 4}, "lines.ifBranches[IF]");
        checkInt(jumpmark::jumpTarget(&d, 0), 7, "jumpTarget(IF)");
        checkInt(jumpmark::jumpTarget(&d, 2), 7, "jumpTarget(ELSEIF)");
        checkInt(jumpmark::jumpTarget(&d, 4), 7, "jumpTarget(ELSE)");
        check(d.lines[0].jumpTo == 7 && d.lines[2].jumpTo == 7 && d.lines[4].jumpTo == 7,
              "lines[i].jumpTo 与 jumpTo 表同步（IF/ELSEIF/ELSE = 7）");
    }

    // ---- 2) SIF：跳过下一行（Null 行要继续往后找）----
    {
        qDebug() << "\n[2] SIF";
        ScriptData d = makeData({"SIF", "<null>", "<null>", "NOP", "NOP"});
        jumpmark::buildJumpMarkings(d);
        checkInt(jumpmark::jumpTarget(&d, 0), 4, "jumpTarget(SIF)（跨过 2 个 Null 行）");
        checkInt(d.elseLines.value(0, -1), 4, "lines.elseLines[SIF] == jumpTo");
        checkInt(d.lines[0].jumpTo, 4, "lines[SIF].jumpTo");
        checkInt(d.lines[1].lineIndex, 1, "lines[1].lineIndex 显式赋值");
        checkInt(d.lines[1].nextLine, 2, "lines[1].nextLine 显式赋值");
        checkInt(d.lines[4].nextLine, -1, "最后一行 nextLine 保持 -1");

        ScriptData tail = makeData({"SIF", "<null>", "NOP"});
        jumpmark::buildJumpMarkings(tail);
        checkInt(jumpmark::jumpTarget(&tail, 0), 3, "jumpTarget(SIF 在末尾)= lines.size()");
    }

    // ---- 3) SELECTCASE / CASE / CASEELSE / ENDSELECT ----
    {
        qDebug() << "\n[3] SELECTCASE";
        ScriptData d = makeData({"SELECTCASE", "CASE", "NOP", "CASEELSE", "NOP", "ENDSELECT", "NOP"});
        jumpmark::buildJumpMarkings(d);
        checkList(d.ifBranches.value(0), {1, 3}, "lines.ifBranches[SELECTCASE]（= CASE 行）");
        checkInt(jumpmark::jumpTarget(&d, 1), 6, "jumpTarget(CASE) = ENDSELECT+1");
        checkInt(jumpmark::jumpTarget(&d, 3), 6, "jumpTarget(CASEELSE) = ENDSELECT+1");
        checkInt(jumpmark::jumpTarget(&d, 0), 6, "jumpTarget(SELECTCASE) = ENDSELECT+1");
        checkInt(d.jumpToEnd.value(0, -1), 5, "lines.jumpToEnd[SELECTCASE] = ENDSELECT 行");
        check(d.lines[1].jumpTo == 6 && d.lines[3].jumpTo == 6 && d.lines[0].jumpTo == 6,
              "lines[i].jumpTo 与 jumpTo 表同步（CASE/CASEELSE/SELECTCASE = 6）");
    }

    // ---- 4) 四种循环：loopEndLines / jumpToEnd / 回边 ----
    {
        qDebug() << "\n[4] REPEAT/REND、WHILE/WEND、FOR/NEXT、DO/LOOP";
        struct LoopCase { const char* start; const char* end; };
        const LoopCase cases[] = {
            {"REPEAT", "REND"}, {"WHILE", "WEND"}, {"FOR", "NEXT"}, {"DO", "LOOP"},
        };
        for (const LoopCase& c : cases) {
            ScriptData d = makeData({QString::fromLatin1(c.start), "NOP",
                                     QString::fromLatin1(c.end), "NOP"});
            jumpmark::buildJumpMarkings(d);
            const QString tag = QStringLiteral("%1/%2")
                                    .arg(QString::fromLatin1(c.start),
                                         QString::fromLatin1(c.end));
            checkInt(d.loopEndLines.value(0, -1), 2, tag + " loopEndLines[起点]");
            checkInt(d.jumpToEnd.value(0, -1), 2, tag + " jumpToEnd[起点]");
            checkInt(jumpmark::jumpTarget(&d, 2), 0, tag + " 回边 jumpTarget(终点)");
            checkInt(d.lines[2].jumpTo, 0, tag + " lines[终点].jumpTo");
        }
    }

    // ---- 5) TRYC / CATCH / ENDCATCH（含「没有 CATCH 的 TRYC」）----
    {
        qDebug() << "\n[5] TRYC / CATCH / ENDCATCH";
        ScriptData d = makeData({"TRYCCALL", "NOP", "CATCH", "NOP", "ENDCATCH"});
        jumpmark::buildJumpMarkings(d);
        checkInt(jumpmark::catchTarget(&d, 0), 2, "catchTarget(TRYCCALL)");
        checkInt(jumpmark::endCatchTarget(&d, 2), 4, "endCatchTarget(CATCH)");

        // eraTW 的主流形态：TRYC* 没有 CATCH —— 不产生任何条目。
        ScriptData noCatch = makeData({"TRYCCALL", "NOP", "TRYCGOTO", "NOP"});
        jumpmark::buildJumpMarkings(noCatch);
        check(noCatch.catchLines.isEmpty() && noCatch.endCatchLines.isEmpty(),
              "没有 CATCH 的 TRYC* 不产生 catchLines/endCatchLines 条目");
    }

    // ---- 6) TRYCALLLIST / FUNC×n / ENDFUNC ----
    {
        qDebug() << "\n[6] TRYCALLLIST / FUNC / ENDFUNC";
        ScriptData d = makeData({"TRYCALLLIST", "FUNC", "FUNC", "FUNC", "ENDFUNC"});
        jumpmark::buildJumpMarkings(d);
        checkList(jumpmark::funcEntryLines(&d, 0), {1, 2, 3}, "funcEntryLines(列表行)（顺序）");
        checkInt(jumpmark::endFuncTarget(&d, 0), 4, "endFuncTarget(列表行)");
        checkList(d.funcEntries.value(0), {1, 2, 3}, "lines.funcEntries[列表行]");
        checkInt(d.endFuncLines.value(0, -1), 4, "lines.endFuncLines[列表行]");
    }

    // ---- 7) FunctionLabel 清空 5 个配对栈（跨函数不串）----
    {
        qDebug() << "\n[7] FunctionLabel 清空配对栈";
        ScriptData ifCase = makeData({"IF", "@F", "ENDIF"});
        jumpmark::buildJumpMarkings(ifCase);
        check(ifCase.endifLines.isEmpty() && ifCase.jumpTo.isEmpty()
                  && ifCase.ifBranches.isEmpty(),
              "IF 跨过 @label 不再与 ENDIF 配对");

        ScriptData loopCase = makeData({"REPEAT", "@F", "REND"});
        jumpmark::buildJumpMarkings(loopCase);
        check(loopCase.loopEndLines.isEmpty() && loopCase.jumpToEnd.isEmpty(),
              "REPEAT 跨过 @label 不再与 REND 配对");

        ScriptData selCase = makeData({"SELECTCASE", "@F", "ENDSELECT"});
        jumpmark::buildJumpMarkings(selCase);
        checkList(selCase.ifBranches.value(0), {}, "SELECTCASE 跨过 @label 不再与 ENDSELECT 配对");

        ScriptData catchCase = makeData({"TRYCCALL", "@F", "CATCH", "@G", "ENDCATCH"});
        jumpmark::buildJumpMarkings(catchCase);
        check(catchCase.catchLines.isEmpty() && catchCase.endCatchLines.isEmpty(),
              "TRYCCALL 跨过 @label 不再与 CATCH 配对");

        ScriptData listCase = makeData({"TRYCALLLIST", "@F", "FUNC", "ENDFUNC"});
        jumpmark::buildJumpMarkings(listCase);
        check(listCase.funcEntries.isEmpty() && listCase.endFuncLines.isEmpty(),
              "TRYCALLLIST 跨过 @label 不再与 ENDFUNC 配对");
    }

    // ---- 8) 幂等：连续 build 两次，10 张表逐表相等 ----
    {
        qDebug() << "\n[8] 幂等（重复构建不叠加）";
        ScriptData d = makeData({"IF", "<null>", "ELSE", "SIF", "NOP", "ENDIF",
                                 "SELECTCASE", "CASE", "CASEELSE", "ENDSELECT",
                                 "REPEAT", "REND", "WHILE", "WEND", "FOR", "NEXT",
                                 "DO", "LOOP",
                                 "TRYCCALL", "CATCH", "ENDCATCH",
                                 "TRYCALLLIST", "FUNC", "ENDFUNC",
                                 "@NEXT_FUNC", "IF", "ENDIF"});
        jumpmark::buildJumpMarkings(d);
        const ScriptData once = d;
        jumpmark::buildJumpMarkings(d);
        check(d.endifLines == once.endifLines, "endifLines 两次构建相同");
        check(d.elseLines == once.elseLines, "elseLines 两次构建相同");
        check(d.loopEndLines == once.loopEndLines, "loopEndLines 两次构建相同");
        check(d.jumpTo == once.jumpTo, "jumpTo 两次构建相同");
        check(d.jumpToEnd == once.jumpToEnd, "jumpToEnd 两次构建相同");
        check(d.ifBranches == once.ifBranches, "ifBranches 两次构建相同");
        check(d.catchLines == once.catchLines, "catchLines 两次构建相同");
        check(d.endCatchLines == once.endCatchLines, "endCatchLines 两次构建相同");
        check(d.endFuncLines == once.endFuncLines, "endFuncLines 两次构建相同");
        check(d.funcEntries == once.funcEntries, "funcEntries 两次构建相同");
    }

    // ---- 9) nullptr：读侧全部 nullptr 安全 ----
    {
        qDebug() << "\n[9] nullptr 读侧";
        const ScriptData* none = nullptr;
        checkInt(jumpmark::jumpTarget(none, 0), -1, "jumpTarget(nullptr)");
        check(jumpmark::ifBranches(none, 0).isEmpty(), "ifBranches(nullptr) 为空");
        checkInt(jumpmark::catchTarget(none, 0), -1, "catchTarget(nullptr)");
        checkInt(jumpmark::endCatchTarget(none, 0), -1, "endCatchTarget(nullptr)");
        check(jumpmark::funcEntryLines(none, 0).isEmpty(), "funcEntryLines(nullptr) 为空");
        checkInt(jumpmark::endFuncTarget(none, 0), -1, "endFuncTarget(nullptr)");
    }

    if (g_failures == 0) qDebug() << "\nALL PASSED";
    else qDebug() << "\nFAILURES:" << g_failures;
    return g_failures == 0 ? 0 : 1;
}