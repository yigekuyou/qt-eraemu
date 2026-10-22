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
// test_structure_pairing.cpp —— 配对语法的**异常形态**护栏
//
// 每个夹具固定三件事（收敛前后都必须成立）：
//   ① 标记区（jumpmark::buildJumpMarkings）产出的表/哨兵值；
//   ② 结构检查（parsecheck::checkStructuresInScript）的诊断：**逐字** message
//      + 行号 + 产生顺序 + code/severity/effect；
//   ③ 运行期读取侧对「没有跳转目标」的处置（settle：jumpTarget 值 +
//      position::setPosition 的负数钳位 = 既有语义）。
//
// 这些形态在五语料里几乎不存在（五份 dump_warnings 里 0 条结构诊断），所以
// 「语料不变」不足以证明收敛没改判据 —— 这张表才是证据。
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <initializer_list>

#include "ast/logical_line.h"
#include "ast/parse_diagnostic.h"
#include "era_parse_table.h"   // ScriptData / ScriptView
#include "jump_markings.h"
// 删掉的三张整表副本（endifLines / elseLines / jumpToEnd）的恢复口径：
// 全部从运行期真正在用的 jumpTo / ifBranches 派生。
#include "jump_markings_rebuild.h"
#include "parse_checks.h"
#include "position_state.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

// 指令名序列 -> ScriptData（每行带位置，诊断能定位）：
//   "IF"/"CASE" …  -> Instruction
//   "@NAME"        -> FunctionLabel
//   "$NAME"        -> GotoLabel
//   "<null>"       -> Null（空行/注释）
static ScriptData makeData(const QStringList& names) {
    static const QString kFile = QStringLiteral("T.ERB");
    ScriptData d;
    d.path = kFile;
    d.lines.reserve(names.size());
    for (int i = 0; i < names.size(); ++i) {
        const QString& n = names.at(i);
        LogicalLine ll;
        ll.position = ScriptPosition(kFile, i + 1, 1);
        if (n == QLatin1String("<null>")) {
            ll.kind = LineKind::Null;
        } else if (n.startsWith(QLatin1Char('@'))) {
            ll.kind = LineKind::FunctionLabel;
            ll.labelName = n.mid(1);
        } else if (n.startsWith(QLatin1Char('$'))) {
            ll.kind = LineKind::GotoLabel;
            ll.labelName = n.mid(1);
        } else {
            ll.kind = LineKind::Instruction;
            ll.functionName = n;
        }
        d.lines.append(ll);
    }
    return d;
}

// 第 3 项护栏（每个夹具都跑）：遍历**全部行**，凡 lines[i].jumpTo >= 0，
// 目标行必须存在（<= lines.size()）且属于同一脚本；并核对 jumpTo 表与行字段
// 仍是同一份事实 —— 删副本时若把钳位/越界那一侧一起改了，这里先红。
static void checkWellFormed(const ScriptData& d, const QString& tag) {
    QStringList bad;
    const int nBad = rebuild::countBadJumpTargets(d, &bad);
    check(nBad == 0, QString("%1：jumpTo >= 0 的行，目标行存在且属于同一脚本（遍历 %2 行）")
                         .arg(tag).arg(d.lines.size()));
    for (const QString& s : bad) qDebug().noquote() << "       " << s;
    QStringList drift;
    const int nDrift = rebuild::countJumpTableDrift(d, &drift);
    check(nDrift == 0, tag + "：jumpTo 表与 lines[i].jumpTo 逐项相同");
    for (const QString& s : drift) qDebug().noquote() << "       " << s;
}

static ParseDiagnostics structureDiags(const ScriptData& d) {
    parsecheck::ParseIrView view;   // 这些夹具不含 RETURNF，不需要函数表
    ParseDiagnostics diags;
    parsecheck::checkStructuresInScript(view, d, diags);
    return diags;
}

// 构建 + 结构检查（装载期真实顺序：先建标记，结构检查读它）。
static ScriptData build(const QStringList& names, ParseDiagnostics* out = nullptr) {
    ScriptData d = makeData(names);
    jumpmark::buildJumpMarkings(d);
    checkWellFormed(d, names.join(QLatin1Char(' ')));
    if (!out) return d;
    *out = structureDiags(d);
    return d;
}

struct Expected {
    int line;            // 1 基（LogicalLine::position.lineNumber）
    const char* message; // 逐字
};

// ② 类断言：条数、顺序、行号、message 逐字、code/severity/effect。
static void expectDiags(const ParseDiagnostics& diags,
                        std::initializer_list<Expected> expected, const QString& tag) {
    const QList<ParseDiagnostic>& all = diags.all();
    const int n = int(expected.size());
    check(all.size() == n,
          QString("%1：诊断条数 = %2（得到 %3）").arg(tag).arg(n).arg(all.size()));
    int i = 0;
    for (const Expected& e : expected) {
        const QString what = QString("%1：第 %2 条").arg(tag).arg(i + 1);
        if (i >= all.size()) { check(false, what + QStringLiteral("（缺）")); ++i; continue; }
        const ParseDiagnostic& d = all.at(i);
        check(d.line == e.line && d.message == QString::fromUtf8(e.message),
              QString("%1 = 行 %2「%3」（得到 行 %4「%5」）")
                  .arg(what).arg(e.line).arg(QString::fromUtf8(e.message))
                  .arg(d.line).arg(d.message));
        check(d.code == QLatin1String(DiagCode::kStructure)
                  && d.severity == DiagSeverity::Error
                  && d.effect == DiagEffect::ReportOnly,
              what + QStringLiteral("：code=structure / severity=Error / effect=ReportOnly"));
        ++i;
    }
}

static QString listText(const QList<int>& v) {
    QStringList parts;
    for (int x : v) parts << QString::number(x);
    return QStringLiteral("[") + parts.join(QLatin1Char(',')) + QStringLiteral("]");
}

static void checkInt(int actual, int expected, const QString& what) {
    check(actual == expected,
          QString("%1 = %2（期望 %3）").arg(what).arg(actual).arg(expected));
}

static void checkList(const QList<int>& actual, const QList<int>& expected, const QString& what) {
    check(actual == expected,
          QString("%1 = %2（期望 %3）").arg(what, listText(actual), listText(expected)));
}

// ③ 类断言：读侧「没有目标」= -1（QHash 缺省），运行期由 setPosition 的负数
// 钳位回到行 0 —— 这是收敛**不改**的既有语义，钉在这里。
static void checkNoJumpRuntime() {
    PositionState st;
    st.script = QStringLiteral("T.ERB");
    st.line = 3;
    PositionEffects fx;
    ScriptView view;   // 不需要脚本表：setPosition 不查行数
    position::setPosition(st, view, QStringLiteral("T.ERB"), -1, /*jumped=*/false, fx);
    checkInt(st.line, 0, "运行期：setPosition(-1) 钳到行 0（「无跳转目标」的既有语义）");
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    qDebug() << "Structure pairing: anomaly fixtures (jump tables + diagnostics + runtime)";
    qDebug() << "======================================================================";

    // =====================================================================
    // IF 族
    // =====================================================================
    {
        qDebug() << "\n[IF]";
        // 缺 ENDIF：标记区什么都不产出；P 在函数末尾报在 IF 行。
        ParseDiagnostics p;
        ScriptData d = build({"IF", "NOP"}, &p);
        check(d.ifBranches.isEmpty() && d.jumpTo.isEmpty() && rebuild::endifLines(d).isEmpty(),
              "IF 缺 ENDIF：ifBranches/jumpTo/endifLines 全空（静默丢弃）");
        checkInt(jumpmark::jumpTarget(&d, 0), -1, "IF 缺 ENDIF：jumpTarget(IF) = -1");
        expectDiags(p, {{1, "IF に対応する ENDIF が見つかりません"}}, "IF 缺 ENDIF");
        checkNoJumpRuntime();

        // 多 ENDIF：第二个被静默忽略。
        d = build({"IF", "NOP", "ENDIF", "ENDIF"}, &p);
        checkInt(jumpmark::jumpTarget(&d, 0), 3, "多 ENDIF：jumpTarget(IF) = 第一个 ENDIF+1");
        expectDiags(p, {{4, "対応するIFの無いENDIF文です"}}, "多 ENDIF");

        // ELSEIF 在 IF 之外：标记区不认；P 报「IF～ENDIFの外」。
        d = build({"ELSEIF", "NOP"}, &p);
        check(d.jumpTo.isEmpty() && d.ifBranches.isEmpty(),
              "ELSEIF 在 IF 之外：jumpTo/ifBranches 为空");
        expectDiags(p, {{1, "IF～ENDIFの外でELSEIF文が使われました"}}, "ELSEIF 在 IF 之外");

        // ELSE 重复：两份实现都**不报**（P 只认栈顶是 IF）；运行期第二个 ELSE
        // 成为第一个 ELSE 的下一分支。
        d = build({"IF", "NOP", "ELSE", "NOP", "ELSE", "NOP", "ENDIF"}, &p);
        checkList(d.ifBranches.value(0), {2, 4}, "ELSE 重复：ifBranches[IF] = [ELSE1, ELSE2]");
        checkInt(rebuild::elseLines(d).value(2, -1), 4, "ELSE 重复：elseLines[ELSE1] = ELSE2（ifBranches 链）");
        checkInt(jumpmark::jumpTarget(&d, 2), 7, "ELSE 重复：jumpTarget(ELSE1) = ENDIF+1");
        checkInt(jumpmark::jumpTarget(&d, 4), 7, "ELSE 重复：jumpTarget(ELSE2) = ENDIF+1");
        expectDiags(p, {}, "ELSE 重复（两实现都不报）");

        // SIF 后无行：标记区跳到 lines.size()（越过末尾 = 结束），P 报错。
        d = build({"SIF", "<null>"}, &p);
        checkInt(jumpmark::jumpTarget(&d, 0), 2, "SIF 后无行：jumpTarget(SIF) = lines.size() = 2");
        expectDiags(p, {{1, "SIF文の次の行がありません"}}, "SIF 后无行");
    }

    // =====================================================================
    // SELECTCASE 族
    // =====================================================================
    {
        qDebug() << "\n[SELECTCASE]";
        // 缺 ENDSELECT：CASE 行留下「待填」哨兵（jumpTo 表 -1 / 行字段 -2）。
        ParseDiagnostics p;
        // 收敛后占位哨兵已消除：未定型的分支**不进表**（既没有显式 -1 占位行，
        // 也没有 lines[].jumpTo = -2），「没配上」由 structureIssues 记录表达。
        ScriptData d = build({"SELECTCASE", "CASE", "NOP", "CASEELSE", "NOP"}, &p);
        checkInt(d.jumpTo.value(1, 12345), 12345, "缺 ENDSELECT：jumpTo 表里没有 CASE1 的条目");
        checkInt(d.lines[1].jumpTo, -1, "缺 ENDSELECT：lines[CASE1].jumpTo 保持默认 -1（无 -2 哨兵）");
        check(d.structureIssues.size() == 1
                  && d.structureIssues.first().kind == StructureIssueKind::UnresolvedOpener,
              "缺 ENDSELECT：未解析事实在 structureIssues 里（UnresolvedOpener）");
        check(d.ifBranches.isEmpty(), "缺 ENDSELECT：ifBranches 为空（未定型）");
        checkInt(jumpmark::jumpTarget(&d, 1), -1, "缺 ENDSELECT：jumpTarget(CASE1) = -1");
        checkInt(jumpmark::jumpTarget(&d, 3), -1, "缺 ENDSELECT：jumpTarget(CASEELSE) = -1（无条目）");
        expectDiags(p, {{1, "SELECTCASE に対応する ENDSELECT が見つかりません"}},
                    "SELECTCASE 缺 ENDSELECT");

        // CASE 在 SELECTCASE 之外：标记区不认；P 报「SELECTCASE～ENDSELECTの外」。
        d = build({"CASE", "NOP"}, &p);
        check(d.jumpTo.isEmpty(), "CASE 在 SELECTCASE 之外：jumpTo 为空");
        expectDiags(p, {{1, "SELECTCASE～ENDSELECTの外でCASE文が使われました"}},
                    "CASE 在 SELECTCASE 之外");

        // CASEELSE 重复：两实现都不报；两条都进 caseLines。
        d = build({"SELECTCASE", "CASE", "NOP", "CASEELSE", "NOP", "CASEELSE", "NOP",
                   "ENDSELECT"}, &p);
        checkList(d.ifBranches.value(0), {1, 3, 5}, "CASEELSE 重复：caseLines = [1,3,5]");
        checkInt(jumpmark::jumpTarget(&d, 5), 8, "CASEELSE 重复：jumpTarget(CASEELSE2) = ENDSELECT+1");
        expectDiags(p, {}, "CASEELSE 重复（两实现都不报）");
    }

    // =====================================================================
    // 四种循环
    // =====================================================================
    {
        qDebug() << "\n[loops]";
        struct One {
            const char* tag;
            QStringList lines;
            const char* message;   // P 的诊断（缺闭合 / 多余）
            bool atOpener;          // 诊断落在开块行（1）还是收尾行（2）
        };
        const One cases[] = {
            {"REPEAT 缺 REND", {"REPEAT", "NOP"}, "REPEAT に対応する REND が見つかりません", true},
            {"REND 多余", {"REND"}, "対応するREPEATの無いREND文です", true},
            {"WHILE 缺 WEND", {"WHILE", "NOP"}, "WHILE に対応する WEND が見つかりません", true},
            {"WEND 多余", {"WEND"}, "対応するWHILEの無いWEND文です", true},
            {"FOR 缺 NEXT", {"FOR", "NOP"}, "FOR に対応する NEXT が見つかりません", true},
            {"NEXT 多余", {"NEXT"}, "対応するFORの無いNEXT文です", true},
            {"DO 缺 LOOP", {"DO", "NOP"}, "DO に対応する LOOP が見つかりません", true},
            {"LOOP 多余", {"LOOP"}, "対応するREPEATの無いLOOP文です", true},
        };
        for (const One& c : cases) {
            ParseDiagnostics p;
            ScriptData d = build(c.lines, &p);
            check(d.loopEndLines.isEmpty() && rebuild::jumpToEnd(d).isEmpty()
                      && d.jumpTo.isEmpty(),
                  QString::fromUtf8(c.tag) + QStringLiteral("：标记区全空（静默丢弃）"));
            expectDiags(p, {{c.atOpener ? 1 : 2, c.message}}, QString::fromUtf8(c.tag));
        }

        // LOOP 与 WHILE 混配对：标记区不认（expected 不符），P 在 LOOP 行报错。
        ParseDiagnostics p;
        ScriptData d = build({"WHILE", "LOOP", "WEND"}, &p);
        // 标记区按族内栈顶判定：LOOP 期望 REPEAT 而栈顶是 WHILE -> 不配对；
        // 中间那个未配对的 LOOP 不阻止随后的 WEND 与 WHILE 配对。
        checkInt(d.loopEndLines.value(0, -1), 2, "WHILE/LOOP/WEND：loopEndLines[WHILE] = WEND");
        checkInt(jumpmark::jumpTarget(&d, 2), 0, "WHILE/LOOP/WEND：WEND 回边到 WHILE");
        checkInt(d.jumpTo.value(1, 12345), 12345, "WHILE/LOOP/WEND：LOOP 行无 jumpTo 条目");
        expectDiags(p, {{2, "対応するREPEATの無いLOOP文です"}}, "WHILE/LOOP 混配");
    }

    // =====================================================================
    // TRYC / CATCH / ENDCATCH
    // =====================================================================
    {
        qDebug() << "\n[TRYC]";
        // 缺 ENDCATCH：P 报在 **TRYC 行**、名字已被改写成 CATCH。
        ParseDiagnostics p;
        ScriptData d = build({"TRYCCALL", "NOP", "CATCH", "NOP"}, &p);
        checkInt(jumpmark::catchTarget(&d, 0), 2, "缺 ENDCATCH：catchTarget(TRYC) = CATCH 行");
        checkInt(jumpmark::endCatchTarget(&d, 2), -1, "缺 ENDCATCH：endCatchTarget(CATCH) = -1");
        expectDiags(p, {{1, "CATCH に対応する ENDCATCH が見つかりません"}}, "TRYC 缺 ENDCATCH");

        // CATCH 重复：第二个 CATCH 标记区忽略；P 报「対応するTRYC系命令がありません」。
        d = build({"TRYCCALL", "CATCH", "CATCH", "ENDCATCH"}, &p);
        checkInt(jumpmark::catchTarget(&d, 0), 1, "CATCH 重复：catchTarget(TRYC) = CATCH1");
        checkInt(jumpmark::endCatchTarget(&d, 1), 3, "CATCH 重复：endCatchTarget(CATCH1) = ENDCATCH");
        checkInt(jumpmark::endCatchTarget(&d, 2), -1, "CATCH 重复：CATCH2 无配对");
        expectDiags(p, {{3, "対応するTRYC系命令がありません"}}, "CATCH 重复");

        // ENDCATCH 多余。
        d = build({"ENDCATCH"}, &p);
        check(d.endCatchLines.isEmpty(), "ENDCATCH 多余：endCatchLines 为空");
        expectDiags(p, {{1, "対応するCATCHの無いENDCATCH文です"}}, "ENDCATCH 多余");

        // 嵌套（合法）：两份实现都配好。
        d = build({"TRYCCALL", "TRYCGOTO", "CATCH", "ENDCATCH", "CATCH", "ENDCATCH"}, &p);
        checkInt(jumpmark::catchTarget(&d, 0), 4, "嵌套：外层 TRYC -> CATCH(行 5)");
        checkInt(jumpmark::catchTarget(&d, 1), 2, "嵌套：内层 TRYGOTO -> CATCH(行 3)");
        checkInt(jumpmark::endCatchTarget(&d, 2), 3, "嵌套：内层 CATCH -> ENDCATCH");
        checkInt(jumpmark::endCatchTarget(&d, 4), 5, "嵌套：外层 CATCH -> ENDCATCH");
        expectDiags(p, {}, "TRYC 嵌套（合法）");

        // eraTW 主流形态：TRYC 没有 CATCH —— 标记区空、P 报「TRYCCALL に対応する CATCH」。
        d = build({"TRYCCALL", "NOP"}, &p);
        check(d.catchLines.isEmpty() && d.endCatchLines.isEmpty(), "无 CATCH 的 TRYCCALL：标记区空");
        expectDiags(p, {{1, "TRYCCALL に対応する CATCH が見つかりません"}}, "TRYC 无 CATCH");

        // TRYCALL 不是异常开块（eraTW 用了 288 次）：两实现都不管。
        d = build({"TRYCALL", "NOP"}, &p);
        check(d.catchLines.isEmpty(), "TRYCALL（非异常形态）：标记区空");
        expectDiags(p, {}, "TRYCALL 不是异常开块");
    }

    // =====================================================================
    // TRY*LIST / FUNC / ENDFUNC
    // =====================================================================
    {
        qDebug() << "\n[TRY*LIST]";
        ParseDiagnostics p;
        // 缺 ENDFUNC：标记区不产出（列表行没有 endFuncLines），P 报在列表行。
        ScriptData d = build({"TRYCALLLIST", "FUNC", "FUNC"}, &p);
        check(d.funcEntries.isEmpty() && d.endFuncLines.isEmpty(),
              "缺 ENDFUNC：funcEntries/endFuncLines 为空（FUNC 条目也不进表）");
        expectDiags(p, {{1, "TRYCALLLIST に対応する ENDFUNC が見つかりません"}},
                    "TRY*LIST 缺 ENDFUNC");

        // ENDFUNC 多余。
        d = build({"ENDFUNC"}, &p);
        check(d.endFuncLines.isEmpty(), "ENDFUNC 多余：endFuncLines 为空");
        expectDiags(p, {{1, "対応するTRYCALLLISTの無いENDFUNC文です"}}, "ENDFUNC 多余");

        // FUNC 在列表之外。
        d = build({"FUNC"}, &p);
        check(d.funcEntries.isEmpty(), "FUNC 在列表之外：funcEntries 为空");
        expectDiags(p, {{1, "対応するTRYCALLLIST系命令のないFUNCです"}}, "FUNC 在列表之外");

        // 体内非 FUNC 指令：标记区照旧只收 FUNC，P 报「使用できない命令」。
        d = build({"TRYJUMPLIST", "NOP", "FUNC", "ENDFUNC"}, &p);
        checkList(jumpmark::funcEntryLines(&d, 0), {2}, "体内 NOP：只收 FUNC 条目");
        expectDiags(p, {{2, "TRYJUMPLIST 構文に使用できない命令'NOP'が含まれています"}},
                    "TRY*LIST 体内非法指令");

        // GOTOLIST 宽松别名同样收 FUNC。
        d = build({"GOTOLIST", "FUNC", "ENDFUNC"}, &p);
        checkList(jumpmark::funcEntryLines(&d, 0), {1}, "GOTOLIST 别名：funcEntries = [1]");
        expectDiags(p, {}, "GOTOLIST 别名");
    }

    // =====================================================================
    // 跨函数 / 组内嵌套（两实现的一致点与分歧点）
    // =====================================================================
    {
        qDebug() << "\n[cross-family]";
        ParseDiagnostics p;
        // 跨 @label：标记区清栈、P 在标签处收尾报未闭合。
        ScriptData d = build({"IF", "@F", "ENDIF"}, &p);
        check(rebuild::endifLines(d).isEmpty() && d.jumpTo.isEmpty()
                  && d.ifBranches.isEmpty(),
              "IF 跨 @label：不配对");
        expectDiags(p, {{1, "IF に対応する ENDIF が見つかりません"},
                        {3, "対応するIFの無いENDIF文です"}}, "IF 跨 @label");

        // 交叉嵌套 IF / REPEAT / ENDIF / REND：
        //   标记区按「族内栈顶」各自配对（IF 与 REPEAT 都配上）；
        //   P 是**单一栈**：ENDIF 时栈顶是 REPEAT -> 报错且不弹 IF。
        d = build({"IF", "REPEAT", "ENDIF", "REND"}, &p);
        checkInt(jumpmark::jumpTarget(&d, 0), 3, "交叉嵌套：jumpTarget(IF) = ENDIF+1（标记区仍配对）");
        checkInt(jumpmark::jumpTarget(&d, 3), 1, "交叉嵌套：jumpTarget(REND) = REPEAT（标记区仍配对）");
        checkInt(d.loopEndLines.value(1, -1), 3, "交叉嵌套：loopEndLines[REPEAT] = REND");
        expectDiags(p, {{3, "対応するIFの無いENDIF文です"},
                        {1, "IF に対応する ENDIF が見つかりません"}},
                    "交叉嵌套 IF/REPEAT/ENDIF/REND");

        // SELECTCASE 分岐の外に命令。
        d = build({"SELECTCASE", "NOP", "CASE", "ENDSELECT"}, &p);
        checkList(d.ifBranches.value(0), {2}, "SELECTCASE 分岐前 NOP：caseLines = [2]");
        expectDiags(p, {{2, "SELECTCASE 構文の分岐の外に命令'NOP'が含まれています"}},
                    "SELECTCASE 分岐の外");
    }

    if (g_failures == 0) qDebug() << "\nALL PASSED";
    else qDebug() << "\nFAILURES:" << g_failures;
    return g_failures == 0 ? 0 : 1;
}
