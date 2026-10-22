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
#ifndef ERAENGINE_GAMEPROC_STRUCTURE_ISSUE_H
#define ERAENGINE_GAMEPROC_STRUCTURE_ISSUE_H

#include <QList>
#include <QString>

// ---------------------------------------------------------------------------
// StructureIssue —— 配对语法的**未解析记录**（数据，不是日志）
//
// 产生点只有一个：jumpmark::buildJumpMarkings（装载期逐脚本一趟）。消费点
// 只有一个：parsecheck::checkStructuresInScript（收尾第 5 步，只把它翻译成
// 诊断文本）。于是「哪些结构没配上」只有一份判定，跳转表与诊断都是它的投影：
//
//   * 跳转表（运行期）：配上了的行 -> 目标行；没配上 -> 表里没有条目
//     （读侧 jumpmark::jumpTarget 返回 kNoJumpTarget，运行期照旧 gotoLine(-1)）；
//   * 本记录（装载期）：没配上的行 + 形态，供诊断逐条说清楚。
//
// kind 与诊断文本**一一对应**（一个 kind 一个消息模板）——这样读取侧只是
// 「按 kind 选模板 + 填名字」，不需要第二套判定。消息文本仍由
// checkStructuresInScript 的 translate() 持有（诊断文本是契约）。
//
// line / related 都是**逻辑行下标**（0 起，与跳转表同一套行号），不是源文件
// 行号；源文件位置由 lines[line].position 取。
// ---------------------------------------------------------------------------
enum class StructureIssueKind : quint8 {
    // 「X に対応する Y が見つかりません」——开块在函数末尾仍未闭合。
    //   name = 开块名（可能已被改写：TRYC* 配到 CATCH 后，P 语义里名字是 "CATCH"）
    UnresolvedOpener,
    // 「IF～ENDIFの外で%1文が使われました」—— name = ELSEIF / ELSE
    OrphanIfBranch,
    // 「SELECTCASE～ENDSELECTの外で%1文が使われました」—— name = CASE / CASEELSE
    OrphanCaseBranch,
    // 「対応する%1の無い%2文です」—— name = 要求的开块名，closer 名从 lines[line] 取
    OrphanCloser,
    // 「対応するREPEATの無い%1文です」—— LOOP（对应 DO 或 REPEAT，文案固定 REPEAT）
    OrphanLoop,
    // 「対応するPRINTDATA系命令もしくはSTRDATAのない%1です」—— ENDDATA
    OrphanEndData,
    // 「対応するTRYCALLLIST系命令のない%1です」—— FUNC
    OrphanFunc,
    // 「対応するPRINTDATA系命令のない%1です」—— DATA / DATAFORM / DATALIST
    OrphanData,
    // 「対応するTRYC系命令がありません」—— CATCH
    OrphanCatch,
    // 「%1 構文に使用できない命令'%2'が含まれています」—— name = 体内开块名
    IllegalInBody,
    // 「DATALIST 構文に使用できない命令'%1'が含まれています」
    IllegalInDataList,
    // 「SELECTCASE 構文の分岐の外に命令'%1'が含まれています」
    IllegalBeforeCase,
    // 「%1 構文中に $ラベルを定義することはできません」—— name = 体内开块名
    LabelInBody,
    // 「REPEAT, FOR, WHILE, DOの中以外で%1文が使われました」—— name = BREAK / CONTINUE
    BreakOutsideLoop,
    // 「SIF文の次の行がありません」
    SifNoNextLine,
    // 「SIF文の次の行を%1文にすることはできません」—— name = 次行的指令名
    SifNextForbidden,
    // 「RETURNF」行：具体文案（位置/返回类型）由读取侧按函数签名定 —— 见
    // parse_checks.cpp 的同名分支（那里需要 UserFunctionInfo，是视图数据）。
    Returnf,
};

struct StructureIssue {
    StructureIssueKind kind = StructureIssueKind::UnresolvedOpener;
    int     line = -1;      // 诊断落点（0 基）
    int     related = -1;   // 关联行（0 基），-1 = 无（体内非法指令 -> 开块行）
    QString name;           // 消息里的名字（语义见各 kind 注释）
};

#endif  // ERAENGINE_GAMEPROC_STRUCTURE_ISSUE_H
