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
// 配对词汇（唯一一份）：开块 / 收尾 / 循环体 / 数据体。
// 收敛前这一组判据在 jump_markings.cpp 与 parse_checks.cpp 各写一遍，两边
// 名字集合一旦漂移就会「运行期配上了、静态检查说没配上」（或反之）。
// ---------------------------------------------------------------------------
bool isLoopOpener(const QString& name) {
    return name == QLatin1String("REPEAT") || name == QLatin1String("FOR")
        || name == QLatin1String("WHILE") || name == QLatin1String("DO");
}

bool isPrintDataFamily(const QString& name) {
    return name.startsWith(QLatin1String("PRINTDATA")) || name == QLatin1String("STRDATA");
}

bool isTryListOpener(const QString& name) {
    return name == QLatin1String("TRYCALLLIST") || name == QLatin1String("TRYJUMPLIST")
        || name == QLatin1String("TRYGOTOLIST") || name == QLatin1String("GOTOLIST");
}

bool isTryCatchOpener(const QString& name) {
    return name == QLatin1String("TRYCCALL") || name == QLatin1String("TRYCCALLFORM")
        || name == QLatin1String("TRYCJUMP") || name == QLatin1String("TRYCJUMPFORM")
        || name == QLatin1String("TRYCGOTO") || name == QLatin1String("TRYCGOTOFORM");
}

QString requiredOpener(const QString& closer) {
    if (closer == QLatin1String("REND")) return QStringLiteral("REPEAT");
    if (closer == QLatin1String("NEXT")) return QStringLiteral("FOR");
    if (closer == QLatin1String("WEND")) return QStringLiteral("WHILE");
    if (closer == QLatin1String("LOOP")) return QStringLiteral("REPEAT");   // 判定见同名分支
    if (closer == QLatin1String("ENDIF")) return QStringLiteral("IF");
    if (closer == QLatin1String("ENDSELECT")) return QStringLiteral("SELECTCASE");
    if (closer == QLatin1String("ENDCATCH")) return QStringLiteral("CATCH");
    if (closer == QLatin1String("ENDLIST")) return QStringLiteral("DATALIST");
    if (closer == QLatin1String("ENDNOSKIP")) return QStringLiteral("NOSKIP");
    if (closer == QLatin1String("ENDFUNC")) return QStringLiteral("TRYCALLLIST");
    return QString();   // ENDDATA 的文案单列（见 StructureIssueKind::OrphanEndData）
}

QString requiredCloser(const QString& opener) {
    if (opener == QLatin1String("IF")) return QStringLiteral("ENDIF");
    if (opener == QLatin1String("REPEAT")) return QStringLiteral("REND");
    if (opener == QLatin1String("FOR")) return QStringLiteral("NEXT");
    if (opener == QLatin1String("WHILE")) return QStringLiteral("WEND");
    if (opener == QLatin1String("DO")) return QStringLiteral("LOOP");
    if (opener == QLatin1String("SELECTCASE")) return QStringLiteral("ENDSELECT");
    if (opener == QLatin1String("CATCH")) return QStringLiteral("ENDCATCH");
    if (opener == QLatin1String("DATALIST")) return QStringLiteral("ENDLIST");
    if (opener == QLatin1String("NOSKIP")) return QStringLiteral("ENDNOSKIP");
    if (isTryCatchOpener(opener)) return QStringLiteral("CATCH");
    if (isPrintDataFamily(opener)) return QStringLiteral("ENDDATA");
    if (isTryListOpener(opener)) return QStringLiteral("ENDFUNC");
    return QString();
}

namespace {

// 配对族：运行期只认这五族（各有自己的 LIFO 语义）；静态检查另见 staticVisible。
enum class Family : quint8 {
    If,
    Loop,
    Select,
    TryCatch,
    TryList,
    PrintData,
    DataList,
    NoSkip,
};

// 一个**开块**条目。**同一份**条目同时服务两个投影：
//   * 运行期（跳转表）：family 栈顶 + jumpPaired/tryLinked/endLinked；
//   * 静态检查（诊断）：staticVisible 条目就是它的 nestStack。
// 两个投影只在「谁看见这一行」上不同，而这个差异被压成一个显式布尔
//（staticVisible），不再是两份独立的判定：
//   staticVisible=false —— 该行被「体内允许清单」判掉时运行期仍然配对
//     （收敛前 buildJumpMarkings 不看这些规则），静态检查看不见它；
//   jumpPaired=true     —— 运行期已配对、静态检查仍不弹出（P 只弹栈顶）：
//     条目留给函数末尾的「未闭合」报告。
struct Open {
    Family  family = Family::If;
    QString name;                 // 静态检查眼里的名字（TRYC* 配到 CATCH 后改写为 "CATCH"）
    int     line = -1;            // 开块行（诊断落点）
    bool    staticVisible = true;
    bool    jumpPaired = false;
    bool    branchesSeen = false; // SELECTCASE：已见第一个 CASE/CASEELSE
    bool    isCatch = false;      // TryCatch 族：条目当前角色（false = TRYC*，true = CATCH）
    bool    tryLinked = false;    // TRYC* 已配到 CATCH（运行期已消费）
    bool    endLinked = false;    // CATCH 已配到 ENDCATCH（运行期已消费）
    QList<int> branches;          // IF：ELSEIF/ELSE 行；SELECTCASE：CASE/CASEELSE 行
    QList<int> funcLines;         // TRY*LIST：体内 FUNC 条目行
};

}  // namespace

// ---------------------------------------------------------------------------
// 配对语法的**唯一**实现：一趟扫行，产出
//   ① 各行上的 lineIndex / nextLine / jumpTo（运行期唯一一份跳转事实）；
//   ② ScriptData::structureIssues（装载期诊断）。
//
// 两个投影的判定差异（逐条见结构体 Open 的注释）：
//   * 运行期按**族内栈顶**配对（IF 族只看 IF 栈顶，循环族只看循环栈顶…），
//     静态检查按**单一嵌套栈顶**配对 —— 交叉嵌套时两者的结论不同，收敛后
//     各自由同一批条目上的 jumpPaired/staticVisible 表达；
//   * 静态检查的「体内允许清单」会让运行期看不见的行（它照样配对）；
//   * 静态检查只在栈顶命中时弹出，运行期按族顶消费。
// ---------------------------------------------------------------------------
void buildJumpMarkings(ScriptData& data) {
    data.loopEndLines.clear();
    data.ifBranches.clear();
    data.catchLines.clear();
    data.endCatchLines.clear();
    data.endFuncLines.clear();
    data.funcEntries.clear();
    data.structureIssues.clear();

    QList<Open> stack;
    QList<StructureIssue>& issues = data.structureIssues;
    const int pastEnd = int(data.lines.size());   // 越过末尾 = 脚本结束（位置区的合法位置）

    const auto issue = [&issues](StructureIssueKind kind, int line,
                                 const QString& name = QString(), int related = -1) {
        StructureIssue it;
        it.kind = kind;
        it.line = line;
        it.related = related;
        it.name = name;
        issues.append(it);
    };

    // 静态检查的栈顶：最后一个 staticVisible 条目。
    const auto pTop = [&stack]() -> int {
        for (int k = int(stack.size()) - 1; k >= 0; --k)
            if (stack.at(k).staticVisible) return k;
        return -1;
    };
    // 运行期的族顶：该族最后一个尚未被运行期消费的条目。
    const auto familyTop = [&stack](Family f) -> int {
        for (int k = int(stack.size()) - 1; k >= 0; --k)
            if (stack.at(k).family == f && !stack.at(k).jumpPaired) return k;
        return -1;
    };
    // TRYC/CATCH 的族顶（运行期把它们放在同一个栈里，靠角色区分）。
    const auto tryTop = [&stack]() -> int {
        for (int k = int(stack.size()) - 1; k >= 0; --k) {
            const Open& o = stack.at(k);
            if (o.family != Family::TryCatch) continue;
            if (o.isCatch ? !o.endLinked : !o.tryLinked) return k;
        }
        return -1;
    };
    // 两个投影各自要弹的条目：相同 -> 删除；不同 -> 都删；只有一个 -> 运行期
    // 消费的条目改成 jumpPaired（条目留给静态检查报告未闭合）。
    const auto consume = [&stack](int pIdx, int jIdx) {
        if (pIdx >= 0 && jIdx >= 0) {
            if (pIdx == jIdx) stack.removeAt(jIdx);
            else if (pIdx > jIdx) { stack.removeAt(pIdx); stack.removeAt(jIdx); }
            else { stack.removeAt(jIdx); stack.removeAt(pIdx); }
        } else if (pIdx >= 0) {
            stack.removeAt(pIdx);
        } else if (jIdx >= 0) {
            stack[jIdx].jumpPaired = true;
        }
    };
    // 函数体收尾（@label 行与文件末尾）：静态检查逐条报未闭合（自内向外），
    // 然后清栈（运行期同样在这里清）。
    const auto closeBody = [&]() {
        for (int k = int(stack.size()) - 1; k >= 0; --k) {
            const Open& o = stack.at(k);
            if (!o.staticVisible) continue;
            issue(StructureIssueKind::UnresolvedOpener, o.line, o.name);
        }
        stack.clear();
    };
    const auto push = [&stack](Family f, const QString& name, int line, bool visible) {
        Open o;
        o.family = f;
        o.name = name;
        o.line = line;
        o.staticVisible = visible;
        stack.append(o);
    };

    for (int i = 0; i < data.lines.size(); ++i) {
        LogicalLine& ll = data.lines[i];
        ll.lineIndex = i;
        if (i + 1 < data.lines.size()) {
            ll.nextLine = i + 1;
        }
        if (ll.kind == LineKind::FunctionLabel) {
            closeBody();
            continue;
        }
        if (ll.kind == LineKind::GotoLabel) {
            const int ti = pTop();
            if (ti >= 0) {
                const QString& base = stack.at(ti).name;
                // PRINTDATA 系 / DATALIST / TRY*LIST 体内不允许定义 $标签。
                if (isPrintDataFamily(base) || base == QLatin1String("DATALIST")
                    || isTryListOpener(base)) {
                    issue(StructureIssueKind::LabelInBody, i, base);
                }
            }
            continue;
        }
        if (!ll.isInstruction()) {
            continue;   // 空行 / 注释 / # 行不参与配对
        }

        const QString& name = ll.functionName;
        const int pti = pTop();

        // =====================================================================
        // 静态检查：体内允许清单（唯一会让静态检查「看不见这一行」的规则）
        // =====================================================================
        bool pRejected = false;
        if (pti >= 0) {
            const Open& base = stack.at(pti);
            const QString& bn = base.name;
            if (isPrintDataFamily(bn)) {
                const bool allowed = name == QLatin1String("DATA")
                    || name == QLatin1String("DATAFORM") || name == QLatin1String("DATALIST")
                    || name == QLatin1String("ENDLIST") || name == QLatin1String("ENDDATA");
                if (!allowed) {
                    issue(StructureIssueKind::IllegalInBody, i, bn, base.line);
                    pRejected = true;
                }
            } else if (bn == QLatin1String("DATALIST")) {
                const bool allowed = name == QLatin1String("DATA")
                    || name == QLatin1String("DATAFORM") || name == QLatin1String("ENDLIST");
                if (!allowed) {
                    issue(StructureIssueKind::IllegalInDataList, i, QString(), base.line);
                    pRejected = true;
                }
            } else if (isTryListOpener(bn)) {
                if (name != QLatin1String("FUNC") && name != QLatin1String("ENDFUNC")) {
                    issue(StructureIssueKind::IllegalInBody, i, bn, base.line);
                    pRejected = true;
                }
            } else if (bn == QLatin1String("SELECTCASE")) {
                const bool allowed = name == QLatin1String("CASE")
                    || name == QLatin1String("CASEELSE") || name == QLatin1String("ENDSELECT");
                if (!base.branchesSeen && !allowed) {
                    issue(StructureIssueKind::IllegalBeforeCase, i, QString(), base.line);
                    pRejected = true;
                }
            }
        }

        // =====================================================================
        // 静态检查：逐名规则（与运行期表无关的那部分）
        // =====================================================================
        int pIdx = -1;        // 静态检查要弹出的条目
        int jIdx = -1;        // 运行期要消费的条目
        bool pAccept = !pRejected;   // 静态检查是否把这一行当开块压栈
        const QString ptop = pti >= 0 ? stack.at(pti).name : QString();

        // SIF 的「下一行」只算一次：静态检查（次行合法性）与运行期（跳过目标）
        // 消费同一个结果 —— 收敛前这是两段各自 while 的代码。
        int sifNext = -1;
        int sifAfter = -1;
        if (name == QLatin1String("SIF")) {
            sifNext = i + 1;
            while (sifNext < pastEnd && data.lines.at(sifNext).kind == LineKind::Null) ++sifNext;
            sifAfter = qMin(sifNext + 1, pastEnd);   // 跨过末尾 -> 脚本结束位置
        }

        if (!pRejected) {
            if (name == QLatin1String("ELSEIF") || name == QLatin1String("ELSE")) {
                if (ptop != QLatin1String("IF"))
                    issue(StructureIssueKind::OrphanIfBranch, i, name);
                else
                    stack[pti].branchesSeen = true;
            } else if (name == QLatin1String("CASE") || name == QLatin1String("CASEELSE")) {
                if (ptop != QLatin1String("SELECTCASE"))
                    issue(StructureIssueKind::OrphanCaseBranch, i, name);
                else
                    stack[pti].branchesSeen = true;
            } else if (name == QLatin1String("BREAK") || name == QLatin1String("CONTINUE")) {
                bool inLoop = false;
                for (const Open& o : stack) {
                    if (o.staticVisible && isLoopOpener(o.name)) { inLoop = true; break; }
                }
                if (!inLoop) issue(StructureIssueKind::BreakOutsideLoop, i, name);
            } else if (name == QLatin1String("FUNC")) {
                if (!isTryListOpener(ptop))
                    issue(StructureIssueKind::OrphanFunc, i, name);
            } else if (name == QLatin1String("DATA") || name == QLatin1String("DATAFORM")) {
                if (!(isPrintDataFamily(ptop) || ptop == QLatin1String("DATALIST")))
                    issue(StructureIssueKind::OrphanData, i, name);
            } else if (name == QLatin1String("DATALIST")) {
                if (!isPrintDataFamily(ptop)) {
                    issue(StructureIssueKind::OrphanData, i, name);
                    pAccept = false;
                }
            } else if (name == QLatin1String("LOOP")) {
                if (ptop != QLatin1String("DO") && ptop != QLatin1String("REPEAT"))
                    issue(StructureIssueKind::OrphanLoop, i, name);
                else
                    pIdx = pti;
            } else if (name == QLatin1String("CATCH")) {
                if (pti < 0 || !isTryCatchOpener(stack.at(pti).name)) {
                    issue(StructureIssueKind::OrphanCatch, i);
                } else {
                    stack[pti].name = QStringLiteral("CATCH");   // P 语义：就地把 TRYC* 改名
                }
            } else if (name == QLatin1String("REND") || name == QLatin1String("NEXT")
                       || name == QLatin1String("WEND") || name == QLatin1String("ENDIF")
                       || name == QLatin1String("ENDSELECT") || name == QLatin1String("ENDCATCH")
                       || name == QLatin1String("ENDLIST") || name == QLatin1String("ENDDATA")
                       || name == QLatin1String("ENDNOSKIP") || name == QLatin1String("ENDFUNC")) {
                bool matched = false;
                if (name == QLatin1String("ENDDATA")) {
                    if (isPrintDataFamily(ptop)) matched = true;
                    else issue(StructureIssueKind::OrphanEndData, i, name);
                } else if (name == QLatin1String("ENDFUNC")) {
                    if (isTryListOpener(ptop)) matched = true;
                    else issue(StructureIssueKind::OrphanCloser, i, QStringLiteral("TRYCALLLIST"));
                } else {
                    const QString want = requiredOpener(name);
                    if (ptop == want) matched = true;
                    else issue(StructureIssueKind::OrphanCloser, i, want);
                }
                if (matched) pIdx = pti;
            } else if (name == QLatin1String("SIF")) {
                // 次行必须是可跳过的有效指令（SIF/TRYC 系也吃下一行，再套就错位）。
                if (sifNext >= pastEnd || data.lines.at(sifNext).kind == LineKind::FunctionLabel
                    || data.lines.at(sifNext).kind == LineKind::GotoLabel) {
                    issue(StructureIssueKind::SifNoNextLine, i);
                } else if (data.lines.at(sifNext).kind == LineKind::Instruction) {
                    const QString& nf = data.lines.at(sifNext).functionName;
                    if (nf == QLatin1String("SIF") || isTryCatchOpener(nf)
                        || isTryListOpener(nf)) {
                        issue(StructureIssueKind::SifNextForbidden, i, nf);
                    }
                }
            } else if (name == QLatin1String("RETURNF")) {
                issue(StructureIssueKind::Returnf, i);
            }
        }

        // =====================================================================
        // 运行期投影：跳转表（与静态检查的取行规则完全独立，只共享条目）
        // =====================================================================
        if (name == QLatin1String("IF")) {
            // 开块：见下
        } else if (name == QLatin1String("SIF")) {
            const int after = sifAfter;
            ll.jumpTo = after;
        } else if (name == QLatin1String("ELSEIF") || name == QLatin1String("ELSE")) {
            const int j = familyTop(Family::If);
            if (j >= 0) {
                Open& cur = stack[j];
                cur.branches.append(i);
            }
        } else if (name == QLatin1String("ENDIF")) {
            const int j = familyTop(Family::If);
            if (j >= 0) {
                Open& cur = stack[j];
                const int after = i + 1;
                data.lines[cur.line].jumpTo = after;
                data.ifBranches[cur.line] = cur.branches;
                for (int b : cur.branches) {
                    data.lines[b].jumpTo = after;   // 顺序落入分支 -> 跳过到 ENDIF 之后
                }
                jIdx = j;
            }
        } else if (name == QLatin1String("CASE") || name == QLatin1String("CASEELSE")) {
            const int j = familyTop(Family::Select);
            if (j >= 0) {
                Open& cur = stack[j];
                cur.branches.append(i);   // 未定型：ENDSELECT 时才回填 jumpTo
            }
        } else if (name == QLatin1String("ENDSELECT")) {
            const int j = familyTop(Family::Select);
            if (j >= 0) {
                Open& cur = stack[j];
                const int after = i + 1;
                for (int c : cur.branches) {
                    data.lines[c].jumpTo = after;
                }
                data.lines[cur.line].jumpTo = after;
                data.ifBranches[cur.line] = cur.branches;
                jIdx = j;
            }
        } else if (name == QLatin1String("LOOP") || name == QLatin1String("REND")
                   || name == QLatin1String("WEND") || name == QLatin1String("NEXT")) {
            int j = familyTop(Family::Loop);
            if (j >= 0) {
                const QString& top = stack.at(j).name;
                const QString expected = (name == QLatin1String("REND"))
                    ? QStringLiteral("REPEAT")
                    : (name == QLatin1String("LOOP"))
                        ? (top == QLatin1String("DO") ? QStringLiteral("DO")
                                                      : QStringLiteral("REPEAT"))
                        : (name == QLatin1String("WEND")) ? QStringLiteral("WHILE")
                                                          : QStringLiteral("FOR");
                if (top == expected) {
                    data.loopEndLines[stack.at(j).line] = i;
                    ll.jumpTo = stack.at(j).line;
                    jIdx = j;
                }
            }
        } else if (isTryCatchOpener(name)) {
            // 开块：见下
        } else if (name == QLatin1String("CATCH")) {
            const int j = tryTop();
            if (j >= 0 && !stack.at(j).isCatch) {
                const int tryLine = stack.at(j).line;
                data.catchLines[tryLine] = i;      // TRYC* -> CATCH
                stack[j].tryLinked = true;
                // 运行期把 CATCH 当成新条目（P 语义是就地把 TRYC* 改名）。
                // 这个条目静态检查看不见（staticVisible=false），只为
                // CATCH -> ENDCATCH 的配对存在。
                push(Family::TryCatch, QStringLiteral("CATCH"), i, /*visible=*/false);
                Open& b = stack.last();
                b.isCatch = true;
            }
        } else if (name == QLatin1String("ENDCATCH")) {
            const int j = tryTop();
            if (j >= 0 && stack.at(j).isCatch) {
                data.endCatchLines[stack.at(j).line] = i;   // CATCH -> ENDCATCH
                stack[j].endLinked = true;
                jIdx = j;
            }
        } else if (isTryListOpener(name)) {
            // 开块：见下
        } else if (name == QLatin1String("FUNC")) {
            const int j = familyTop(Family::TryList);
            if (j >= 0) stack[j].funcLines.append(i);
        } else if (name == QLatin1String("ENDFUNC")) {
            const int j = familyTop(Family::TryList);
            if (j >= 0) {
                data.endFuncLines[stack.at(j).line] = i;    // TRY*LIST -> ENDFUNC
                data.funcEntries[stack.at(j).line] = stack.at(j).funcLines;
                jIdx = j;
            }
        }

        // =====================================================================
        // 开块压栈（两个投影共享同一条目）
        // =====================================================================
        const bool isJFamilyOpener = name == QLatin1String("IF")
            || name == QLatin1String("SELECTCASE") || isLoopOpener(name)
            || isTryCatchOpener(name) || isTryListOpener(name);
        if (isJFamilyOpener) {
            const Family f = (name == QLatin1String("IF")) ? Family::If
                : (name == QLatin1String("SELECTCASE")) ? Family::Select
                : isLoopOpener(name) ? Family::Loop
                : isTryCatchOpener(name) ? Family::TryCatch
                                        : Family::TryList;
            push(f, name, i, /*visible=*/!pRejected);
        } else if (pAccept) {
            // 只属于静态检查的开块（运行期没有跳转表）。
            if (isPrintDataFamily(name)) push(Family::PrintData, name, i, true);
            else if (name == QLatin1String("DATALIST")) push(Family::DataList, name, i, true);
            else if (name == QLatin1String("NOSKIP")) push(Family::NoSkip, name, i, true);
        }

        // 两个投影的弹出（顺序无关；consume 按两条独立规则处置）
        consume(pIdx, jIdx);
    }

    closeBody();
}

}  // namespace jumpmark
