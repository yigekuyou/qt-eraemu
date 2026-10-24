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
// test_position_state.cpp
//
// 验收物：位置区（执行位置 + 调用栈）可以**脱离 ExecutionEngine / 解析器**验证。
//
// 1) 前半部分直接驱动 position:: 的自由函数：手搓两个 ScriptData（含
//    LogicalLine.ownerFunction）+ 一张标签索引，拼 ScriptView。
//    不经过 QObject、不经过装载流水线 —— 调用栈/返回地址语义因此可以逐条钉死。
// 2) 后半部分用真的 EraParseTable 验**信号顺序**（Effects 只回答「发哪个」，
//    顺序在 emitEffects 里写死）：callLabel / returnFromCall / jumpToLabel 按
//    规范顺序，clear() 按旧实现的反序（positionChanged 先、callStackChanged 后）。
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <type_traits>

#include "ast/logical_line.h"
#include "ast/name_interner.h"
#include "era_parse_table.h"
#include "position_state.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static void checkInt(int actual, int expected, const QString& what) {
    check(actual == expected, QStringLiteral("%1 (got %2, want %3)")
                                   .arg(what).arg(actual).arg(expected));
}

static void checkStr(const QString& actual, const QString& expected, const QString& what) {
    check(actual == expected, QStringLiteral("%1 (got \"%2\", want \"%3\")")
                                  .arg(what, actual.isEmpty() ? QStringLiteral("<empty>") : actual,
                                       expected.isEmpty() ? QStringLiteral("<empty>") : expected));
}

// Effects 契约的第 10 条：**除期望位之外的位全为 false**。
// 每次操作都用它收尾 —— 多亮一位就是多发一个信号。
static void only(const PositionEffects& fx, bool callStack, bool memorySpace, bool position,
                 bool jump, const QString& what) {
    const bool ok = fx.callStackChanged == callStack && fx.memorySpaceChanged == memorySpace
                    && fx.positionChanged == position && fx.jumpRequested == jump;
    check(ok, QStringLiteral("%1 -> Effects{call:%2 space:%3 pos:%4 jump:%5}")
                  .arg(what)
                  .arg(fx.callStackChanged).arg(fx.memorySpaceChanged)
                  .arg(fx.positionChanged).arg(fx.jumpRequested));
}

// ---------------------------------------------------------------------------
// 夹具：两个脚本 + 一张全局标签索引
//
// A.ERB
//   0  @MAIN            owner=MAIN
//   1  PRINT            owner=MAIN        <- 作用域测试：与 2 同函数
//   2  CALL             owner=MAIN        <- 调用点（返回地址 = 3）
//   3  $PRINT_TITLE     owner=MAIN        <- **$ 标签**：不可被 CALL
//   4  RETURN           owner=MAIN
//   5  @SUB             owner=SUB
//   6  PRINT            owner=SUB
//   7  RETURN           owner=SUB
//
// B.ERB
//   0  @REMOTE          owner=REMOTE
//   1  PRINT            owner=REMOTE
//   2  RETURN           owner=REMOTE
//   3  @PRINT_TITLE     owner=PRINT_TITLE  <- 与 A 的 $PRINT_TITLE 同名（eraMegaten 形态）
//   4  PRINT            owner=PRINT_TITLE
//   5  @MAIN            owner=MAIN          <- 同名标签的第二份声明（"口上"式重复）
//   6  RETURN           owner=MAIN
//   7  @REMOTE_2        owner=REMOTE_2
//   8  RETURN           owner=REMOTE_2
// ---------------------------------------------------------------------------
// 段身份必须由**同一个**构造点给出（eravar::segmentKeyText = (脚本名, 段起点行)）：
// 装载期 loadScript 会按 (脚本名, @ 标签行号) 重算并自检，夹具若自己编一个
// 名字（旧版用 owner 函数名），每一行都会被记成「段身份两侧不一致」刷进 stderr。
static LogicalLine instr(const QString& script, int segmentStart, const QString& name,
                         const QString& owner) {
    LogicalLine ll;
    ll.kind = LineKind::Instruction;
    ll.functionName = name;
    ll.ownerFunction = owner;
    ll.scopeKey = eravar::segmentKeyText(script, segmentStart);
    return ll;
}

static LogicalLine funcLabel(const QString& label, const QString& script, int segmentStart) {
    LogicalLine ll;
    ll.kind = LineKind::FunctionLabel;
    ll.labelName = label;
    ll.ownerFunction = label;
    ll.scopeKey = eravar::segmentKeyText(script, segmentStart);
    return ll;
}

static LogicalLine gotoLabel(const QString& label, const QString& script, int segmentStart,
                             const QString& owner) {
    LogicalLine ll;
    ll.kind = LineKind::GotoLabel;
    ll.labelName = label;
    ll.ownerFunction = owner;
    ll.scopeKey = eravar::segmentKeyText(script, segmentStart);
    return ll;
}

static ScriptData makeScriptA() {
    const QString script = QStringLiteral("A.ERB");
    ScriptData d;
    d.lines = {funcLabel("MAIN", script, 0), instr(script, 0, "PRINT", "MAIN"),
               instr(script, 0, "CALL", "MAIN"),
               gotoLabel("PRINT_TITLE", script, 0, "MAIN"),
               instr(script, 0, "RETURN", "MAIN"),
               funcLabel("SUB", script, 5), instr(script, 5, "PRINT", "SUB"),
               instr(script, 5, "RETURN", "SUB")};
    return d;
}

static ScriptData makeScriptB() {
    const QString script = QStringLiteral("B.ERB");
    ScriptData d;
    d.lines = {funcLabel("REMOTE", script, 0), instr(script, 0, "PRINT", "REMOTE"),
               instr(script, 0, "RETURN", "REMOTE"),
               funcLabel("PRINT_TITLE", script, 3), instr(script, 3, "PRINT", "PRINT_TITLE"),
               funcLabel("MAIN", script, 5), instr(script, 5, "RETURN", "MAIN"),
               funcLabel("REMOTE_2", script, 7), instr(script, 7, "RETURN", "REMOTE_2")};
    return d;
}

struct Fixture {
    QHash<QString, ScriptData> scripts;
    QHash<QString, QList<LabelRef>> labels;

    Fixture() {
        scripts.insert(QStringLiteral("A.ERB"), makeScriptA());
        scripts.insert(QStringLiteral("B.ERB"), makeScriptB());
        // 手工夹具的索引只放 @条目（A 的 $PRINT_TITLE 故意不入索引，
        // 用来验证 CALL 不会落到 $ 标签）；按 (脚本名, 行号) 排序固化 ——
        // 与 markLoadComplete 之后的形状一致。
        labels[QStringLiteral("MAIN")] =
            {LabelRef{QStringLiteral("A.ERB"), 0}, LabelRef{QStringLiteral("B.ERB"), 5}};
        labels[QStringLiteral("SUB")] = {LabelRef{QStringLiteral("A.ERB"), 5}};
        labels[QStringLiteral("REMOTE")] = {LabelRef{QStringLiteral("B.ERB"), 0}};
        labels[QStringLiteral("PRINT_TITLE")] = {LabelRef{QStringLiteral("B.ERB"), 3}};
        labels[QStringLiteral("REMOTE_2")] = {LabelRef{QStringLiteral("B.ERB"), 7}};
    }

    ScriptView view() const {
        ScriptView v;
        v.scripts = &scripts;
        v.labelIndex = &labels;
        return v;
    }
};

// 把状态设到 A.ERB:line（不带 jumped 语义），返回清空后的 fx 由调用方自取。
static void seed(PositionState& s, const ScriptView& view, const QString& script, int line) {
    PositionEffects fx;
    position::setPosition(s, view, script, line, /*jumped=*/true, fx);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Fixture f;
    const ScriptView view = f.view();

    // -----------------------------------------------------------------------
    qDebug() << "\n1) setPosition";
    {
        PositionState s;
        PositionEffects fx;
        position::setPosition(s, view, "A.ERB", 2, /*jumped=*/false, fx);
        checkStr(s.script, "A.ERB", "script set");
        checkInt(s.line, 2, "line set");
        check(!s.jumped, "jumped passthrough = false");
        only(fx, false, true, true, false, "empty -> A.ERB 切空间");

        fx = {};
        position::setPosition(s, view, "A.ERB", 5, /*jumped=*/true, fx);
        only(fx, false, false, true, false, "同脚本不亮 memorySpaceChanged");
        check(s.jumped, "jumped passthrough = true");
        checkInt(s.line, 5, "line updated");

        fx = {};
        position::setPosition(s, view, "A.ERB", 5, /*jumped=*/true, fx);
        only(fx, false, false, true, false, "同行 + forceEmit 仍亮 positionChanged");

        fx = {};
        position::setPosition(s, view, "B.ERB", 1, /*jumped=*/false, fx);
        only(fx, false, true, true, false, "换脚本亮 memorySpaceChanged");
        checkStr(s.script, "B.ERB", "script switched");

        fx = {};
        position::setPosition(s, view, QString(), 4, /*jumped=*/false, fx);
        checkStr(s.script, "B.ERB", "空脚本名不切空间");
        only(fx, false, false, true, false, "空脚本名 -> 只定位");
    }

    // -----------------------------------------------------------------------
    qDebug() << "\n2) advance / setLineInternal 的 early-return";
    {
        PositionState s;
        seed(s, view, "A.ERB", 2);
        PositionEffects fx;
        position::advance(s, fx);
        checkInt(s.line, 3, "PC + 1");
        check(!s.jumped, "advance 清 jumped");
        only(fx, false, false, true, false, "advance 只亮 positionChanged");

        fx = {};
        position::setLineInternal(s, 3, /*forceEmit=*/false, fx);
        checkInt(s.line, 3, "line 未变");
        only(fx, false, false, false, false, "line 未变 + 不强制 -> 什么都不亮");

        fx = {};
        position::setLineInternal(s, -7, /*forceEmit=*/false, fx);
        checkInt(s.line, 0, "负数钳到 0");
        only(fx, false, false, true, false, "钳位后位置变化");
    }

    // -----------------------------------------------------------------------
    qDebug() << "\n3) jumpToLine";
    {
        PositionState s;
        seed(s, view, "A.ERB", 2);
        PositionEffects fx;
        check(!position::jumpToLine(s, view, 8, fx), "越界（== 行数）返回 false");
        checkInt(s.line, 2, "越界：PC 不变");
        check(s.jumped, "越界：jumped 不变（seed 的 true）");
        only(fx, false, false, false, false, "越界：不亮位");

        check(!position::jumpToLine(s, view, -1, fx), "负行号返回 false");
        only(fx, false, false, false, false, "负行号：不亮位");

        check(position::jumpToLine(s, view, 6, fx), "合法行号返回 true");
        checkInt(s.line, 6, "PC = 6");
        check(s.jumped, "jumped = true");
        only(fx, false, false, true, false, "只亮 positionChanged");

        PositionState unknown;   // 脚本不存在 -> 行数 0 -> 任意行号都「合法」
        PositionEffects fx2;
        check(position::jumpToLine(unknown, view, 999, fx2), "无脚本时行数为 0（不判越界）");
        only(fx2, false, false, true, false, "无脚本跳转只亮 positionChanged");
    }

    // -----------------------------------------------------------------------
    qDebug() << "\n4) callLabel（返回地址 / 跨脚本）";
    {
        PositionState s;
        seed(s, view, "A.ERB", 2);
        PositionEffects fx;
        check(position::callLabel(s, view, "SUB", /*advanceWasCalled=*/false, fx),
              "callLabel(SUB) 成功");
        checkInt(s.callStack.size(), 1, "压了一帧（栈深 = callStack.size()）");
        const Frame fr = s.callStack.last();
        checkStr(fr.script, "A.ERB", "帧里的返回脚本");
        checkInt(fr.returnLine, 3, "返回地址 = 当前行 + 1");
        checkStr(fr.callLabel, "SUB", "帧里的 callLabel");
        checkInt(fr.entryLine, 5, "帧里的 entryLine");
        check(!fr.isJump, "CALL 帧不是 jump");
        checkInt(s.line, 5, "PC = 函数入口");
        only(fx, true, false, true, false, "同脚本 CALL：callStack + position");

        PositionState s2;
        seed(s2, view, "A.ERB", 2);
        PositionEffects fx2;
        check(position::callLabel(s2, view, "SUB", /*advanceWasCalled=*/true, fx2),
              "advanceWasCalled=true 成功");
        checkInt(s2.callStack.last().returnLine, 2, "advanceWasCalled -> 返回地址 = 当前行");

        PositionState s3;
        seed(s3, view, "A.ERB", 2);
        PositionEffects fx3;
        check(position::callLabel(s3, view, "REMOTE", false, fx3), "跨脚本 CALL 成功");
        checkStr(s3.script, "B.ERB", "内存空间切到 B");
        checkStr(s3.callStack.last().script, "A.ERB", "返回脚本仍是 A");
        only(fx3, true, true, true, false, "跨脚本 CALL：callStack + space + position");

        PositionState s4;
        PositionEffects fx4;
        check(!position::callLabel(s4, view, "NO_SUCH_LABEL", false, fx4), "未知标签返回 false");
        checkInt(s4.callStack.size(), 0, "未知标签不压帧");
        only(fx4, false, false, false, false, "未知标签不亮位");
    }

    // -----------------------------------------------------------------------
    qDebug() << "\n5) 当前脚本优先 + $ 标签不可调用";
    {
        // A.ERB:3 是 $PRINT_TITLE（kind != FunctionLabel，所以 CALL 不能落到它）。
        check(f.scripts["A.ERB"].lines.at(3).kind == LineKind::GotoLabel,
              "夹具：A.ERB:3 是 GotoLabel");

        PositionState s;
        seed(s, view, "A.ERB", 2);
        QString target;
        int line = -1;
        check(position::resolveCallableLabel(s, view, "print_title", target, line),
              "$标签不回退成「找不到」，而是落到全局索引");
        checkStr(target, "B.ERB", "$PRINT_TITLE -> 全局索引里的 @PRINT_TITLE 所在脚本");
        checkInt(line, 3, "$PRINT_TITLE -> @PRINT_TITLE 的行号");

        // 当前脚本优先：B 里同时有 @MAIN(5)，全局索引的第一份是 A.ERB:0。
        PositionState sb;
        seed(sb, view, "B.ERB", 1);
        check(position::resolveCallableLabel(sb, view, "MAIN", target, line), "B 里解析 MAIN");
        checkStr(target, "B.ERB", "当前脚本优先于全局索引的第一份");
        checkInt(line, 5, "当前脚本里的 @MAIN");

        PositionState sa;
        seed(sa, view, "A.ERB", 2);
        check(position::resolveCallableLabel(sa, view, "MAIN", target, line), "A 里解析 MAIN");
        checkStr(target, "A.ERB", "A 里解析到 A 的 @MAIN");
        checkInt(line, 0, "A 的 @MAIN 在 0");
    }

    // -----------------------------------------------------------------------
    qDebug() << "\n6) jumpLabel（空栈退化 / 非空栈压 isJump 帧）";
    {
        PositionState s;
        seed(s, view, "A.ERB", 2);
        PositionEffects fx;
        check(position::jumpLabel(s, view, "REMOTE", fx), "空栈 jumpLabel 成功");
        checkInt(s.callStack.size(), 0, "空栈：不压帧（退化为 jumpToLabel）");
        checkStr(s.script, "B.ERB", "空栈：切到目标脚本");
        checkInt(s.line, 0, "空栈：定位到目标行");
        only(fx, false, true, true, true, "空栈：space + position + jumpRequested");

        PositionState s2;
        seed(s2, view, "A.ERB", 2);
        PositionEffects fx2;
        check(position::callLabel(s2, view, "REMOTE", false, fx2), "先 CALL 到 B（帧 = A.ERB:3）");
        checkInt(s2.line, 0, "现在在 B.ERB:0");
        fx2 = {};
        check(position::jumpLabel(s2, view, "REMOTE_2", fx2), "非空栈 jumpLabel 成功");
        checkInt(s2.callStack.size(), 2, "非空栈：压一帧");
        const Frame jf = s2.callStack.last();
        check(jf.isJump, "新帧 isJump = true");
        checkStr(jf.script, "A.ERB", "新帧 returnScript 继承当前帧（不是当前内存空间 B）");
        checkInt(jf.returnLine, 3, "新帧 returnLine 继承当前帧");
        checkStr(jf.callLabel, "REMOTE_2", "新帧 callLabel = 目标标签");
        checkInt(jf.entryLine, 7, "新帧 entryLine = 目标行");
        checkInt(s2.line, 7, "PC = 目标行");
        only(fx2, true, false, true, false, "非空栈：callStack + position（不亮 jumpRequested）");

        PositionState s3;
        seed(s3, view, "A.ERB", 2);
        PositionEffects fx3;
        check(!position::jumpLabel(s3, view, "NO_SUCH_LABEL", fx3), "空栈 + 未知标签 -> false");
        only(fx3, false, false, false, false, "失败不亮位");
    }

    // -----------------------------------------------------------------------
    qDebug() << "\n7) returnFromCall";
    {
        PositionState s;
        seed(s, view, "A.ERB", 2);
        PositionEffects fx;
        position::callLabel(s, view, "REMOTE", false, fx);
        fx = {};
        check(position::returnFromCall(s, view, fx), "返回成功");
        checkStr(s.script, "A.ERB", "回到帧里的脚本");
        checkInt(s.line, 3, "回到帧里的返回地址");
        check(!s.jumped, "RETURN 后 jumped = false");
        checkInt(s.callStack.size(), 0, "弹空（栈深 = callStack.size()）");
        only(fx, true, true, true, false, "弹帧 + 切空间 + 定位");

        PositionState s2;
        seed(s2, view, "A.ERB", 2);
        PositionEffects fx2;
        check(!position::returnFromCall(s2, view, fx2), "空栈返回 false");
        checkInt(s2.line, 2, "空栈：PC 不变");
        checkStr(s2.script, "A.ERB", "空栈：脚本不变");
        only(fx2, false, false, false, false, "空栈不亮位");
    }

    // -----------------------------------------------------------------------
    qDebug() << "\n8) callLabelAt";
    {
        PositionState s;
        seed(s, view, "A.ERB", 2);
        PositionEffects fx;
        check(position::callLabelAt(s, view, "B.ERB", 3, "C.ERB", 77, fx), "显式返回地址调用成功");
        const Frame fr = s.callStack.last();
        checkStr(fr.script, "C.ERB", "返回脚本 = 显式参数");
        checkInt(fr.returnLine, 77, "返回地址 = 显式参数");
        checkStr(fr.callLabel, "PRINT_TITLE", "callLabel 取自目标行的 labelName");
        checkInt(fr.entryLine, 3, "entryLine = 目标行");
        checkStr(s.script, "B.ERB", "切到目标脚本");
        checkInt(s.line, 3, "PC = 目标行");
        only(fx, true, true, true, false, "callStack + space + position");

        PositionState s2;
        seed(s2, view, "A.ERB", 2);
        PositionEffects fx2;
        check(!position::callLabelAt(s2, view, "B.ERB", 99, "C.ERB", 77, fx2), "越界行返回 false");
        checkInt(s2.callStack.size(), 0, "越界：不压帧");
        checkInt(s2.line, 2, "越界：PC 不变");
        checkStr(s2.script, "A.ERB", "越界：脚本不变");
        only(fx2, false, false, false, false, "越界不亮位");

        PositionEffects fx3;
        check(!position::callLabelAt(s2, view, "NO.ERB", 0, "C.ERB", 77, fx3), "未知脚本返回 false");
        only(fx3, false, false, false, false, "未知脚本不亮位");
    }

    // -----------------------------------------------------------------------
    qDebug() << "\n9) executionScopeId（段身份，无函数名回查）";
    {
        PositionState s;
        PositionEffects fx;
        checkInt(int(position::executionScopeId(s, view)), int(eravar::kGlobalScope),
                 "无位置 -> kGlobalScope");

        position::setPosition(s, view, "A.ERB", 1, true, fx);
        const auto idMain = position::executionScopeId(s, view);
        check(idMain == eravar::internName(eravar::segmentKeyText(QStringLiteral("A.ERB"), 0)),
              "A.ERB:1 的段身份 = (A.ERB,0)（行上的 scopeKey）");
        check(idMain != eravar::kGlobalScope, "MAIN 的 id 不是顶层");

        fx = {};
        position::setPosition(s, view, "A.ERB", 2, false, fx);   // 同一段内换行
        checkInt(int(position::executionScopeId(s, view)), int(idMain),
                 "同段内换行 id 不变");

        fx = {};
        position::setPosition(s, view, "A.ERB", 6, false, fx);   // 换到 @SUB
        const auto idSub = position::executionScopeId(s, view);
        check(idSub == eravar::internName(eravar::segmentKeyText(QStringLiteral("A.ERB"), 5)),
              "A.ERB:6 的段身份 = (A.ERB,5)（@SUB 行）");
        check(idSub != idMain, "换段后 id 变");

        fx = {};
        position::setPosition(s, view, "A.ERB", 3, false, fx);   // 回到 MAIN 段
        checkInt(int(position::executionScopeId(s, view)), int(idMain), "回到 MAIN id 复原");
    }

    // -----------------------------------------------------------------------
    // 信号顺序：Effects 只回答「发哪个」，顺序由 EraParseTable::emitEffects 写死。
    // 这里用真的 EraParseTable（loadScript 只需 LogicalLine 序列）把顺序录下来。
    // -----------------------------------------------------------------------
    qDebug() << "\n10) 信号顺序（壳子）";
    {
        EraParseTable table(nullptr);
        QList<LogicalLine> aLines = makeScriptA().lines;
        QList<LogicalLine> bLines = makeScriptB().lines;
        table.loadScript(QStringLiteral("A.ERB"), aLines);
        table.loadScript(QStringLiteral("B.ERB"), bLines);

        QStringList log;
        QObject::connect(&table, &EraParseTable::callStackChanged,
                         [&log](int d) { log << QStringLiteral("callStack(%1)").arg(d); });
        QObject::connect(&table, &EraParseTable::memorySpaceChanged,
                         [&log](const QString& s) { log << QStringLiteral("space(%1)").arg(s); });
        QObject::connect(&table, &EraParseTable::positionChanged,
                         [&log](const QString& s, int l) {
                             log << QStringLiteral("position(%1,%2)").arg(s).arg(l);
                         });
        QObject::connect(&table, &EraParseTable::jumpRequested,
                         [&log](const QString& s, const QString& l, int p) {
                             log << QStringLiteral("jump(%1,%2,%3)").arg(s, l).arg(p);
                         });

        table.setPosition(QStringLiteral("A.ERB"), 2, true);
        log.clear();

        check(table.callLabel(QStringLiteral("REMOTE")), "壳子：跨脚本 CALL");
        checkStr(log.join(QLatin1Char(' ')),
                 "callStack(1) space(B.ERB) position(B.ERB,0)",
                 "CALL 的 emit 顺序 = callStack -> space -> position");

        log.clear();
        check(table.returnFromCall(), "壳子：RETURN");
        checkStr(log.join(QLatin1Char(' ')),
                 "callStack(0) space(A.ERB) position(A.ERB,3)",
                 "RETURN 的 emit 顺序 = callStack -> space -> position");

        log.clear();
        check(table.jumpToLabel(QStringLiteral("REMOTE")), "壳子：GOTO 跨脚本");
        checkStr(log.join(QLatin1Char(' ')),
                 "space(B.ERB) position(B.ERB,0) jump(B.ERB,REMOTE,0)",
                 "GOTO 的 emit 顺序 = space -> position -> jumpRequested");

        log.clear();
        table.clear();
        checkStr(log.join(QLatin1Char(' ')), "position(,0) callStack(0)",
                 "clear() 例外：positionChanged 先、callStackChanged 后（旧顺序）");
    }

    // -----------------------------------------------------------------------
    // 11) 裸标签只属于一个脚本：唯一索引 + 唯一全序 + 唯一择一规则
    //
    // 6 个脚本都声明 @SHARED（重名），只有 S01 的那份不在 1 行（在 5 行）。
    // 全序 = 脚本名 QString::compare 升序 -> 行号升序，所以答案只有一个：
    // S01.ERB:5。装载顺序反过来（QHash 的插入序、旧实现的遍历序都跟着变）
    // 也必须给出同一个答案。
    // -----------------------------------------------------------------------
    qDebug() << "\n11) 重名裸标签：装载序无关的唯一定序";
    {
        // SHARED 落在第 padCount + 1 行（0 行是 @PAD，中间是 PRINT）。
        auto sharedScript = [](const QString& script, int padCount) {
            QList<LogicalLine> lines;
            lines << funcLabel(QStringLiteral("PAD"), script, 0);
            for (int i = 0; i < padCount; ++i)
                lines << instr(script, 0, QStringLiteral("PRINT"), QStringLiteral("PAD"));
            lines << funcLabel(QStringLiteral("SHARED"), script, padCount + 1);
            lines << instr(script, padCount + 1, QStringLiteral("RETURN"), QStringLiteral("SHARED"));
            return lines;
        };
        const QStringList names = {QStringLiteral("S01.ERB"), QStringLiteral("S02.ERB"),
                                   QStringLiteral("S03.ERB"), QStringLiteral("S04.ERB"),
                                   QStringLiteral("S05.ERB"), QStringLiteral("S06.ERB")};

        // 返回这一轮的可观察结果；两轮（正序 / 逆序装载）必须逐字相同。
        auto run = [&](const QStringList& order) {
            EraParseTable table(nullptr);
            for (const QString& name : order) {
                table.loadScript(name, sharedScript(name, name == QLatin1String("S01.ERB") ? 4 : 0));
            }
            table.loadScript(QStringLiteral("N.ERB"),
                             {funcLabel(QStringLiteral("NEUTRAL"), QStringLiteral("N.ERB"), 0),
                              instr(QStringLiteral("N.ERB"), 0, QStringLiteral("RETURN"),
                                    QStringLiteral("NEUTRAL"))});
            table.markLoadComplete();

            // 中性脚本里没有 SHARED -> 只能跨脚本解析。
            table.setPosition(QStringLiteral("N.ERB"), 0, /*jumped=*/false);
            QString callScript;
            int callLine = -1;
            const bool callOk = table.resolveCallableLabel(QStringLiteral("SHARED"), callScript, callLine);
            int gotoLine = -1;
            const bool gotoOk = table.resolveJumpTarget(QStringLiteral("SHARED"), gotoLine);
            const QString gotoScript = table.getCurrentScript();

            check(callOk && gotoOk, "两轮：SHARED 同时可 CALL 与可 GOTO");
            checkStr(callScript, "S01.ERB", "CALL：全序第一 = 脚本名 compare 最小的脚本");
            checkInt(callLine, 5, "CALL：该脚本内的行号 5");
            checkStr(gotoScript, "S01.ERB", "GOTO：落到同一个脚本");
            checkInt(gotoLine, 5, "GOTO：落到同一行");
            check(callScript == gotoScript && callLine == gotoLine,
                  "CALL 与 GOTO 解析到**同一个** (script, line)（旧实现两条路各答各的）");

            // 索引本身的全序（数据，不是消费者）：S01 -> S06，S01 的行号 5 在 S06 的 1 之前。
            const QList<LabelRef> all = table.labels(QStringLiteral("SHARED"));
            checkInt(int(all.size()), 6, "索引收了全部 6 份声明");
            if (all.size() == 6) {
                checkStr(all.first().script, "S01.ERB", "索引第一条 = S01.ERB");
                checkInt(all.first().line, 5, "索引第一条 = S01.ERB:5（脚本名先于行号）");
                checkStr(all.last().script, "S06.ERB", "索引最后一条 = S06.ERB");
                bool sorted = true;
                for (int i = 1; i < all.size(); ++i) sorted = sorted && labelRefLess(all.at(i - 1), all.at(i));
                check(sorted, "索引按 labelRefLess 全序排列");
            }

            // 当前脚本优先：S04 里也有 SHARED -> 两条路都落到 S04，不跑到全序第一。
            table.setPosition(QStringLiteral("S04.ERB"), 0, /*jumped=*/false);
            QString curScript;
            int curLine = -1;
            check(table.resolveCallableLabel(QStringLiteral("SHARED"), curScript, curLine)
                      && curScript == QLatin1String("S04.ERB") && curLine == 1,
                  "本脚本优先：CALL 走 S04.ERB:1");
            int curGoto = -1;
            check(table.resolveJumpTarget(QStringLiteral("SHARED"), curGoto)
                      && table.getCurrentScript() == QLatin1String("S04.ERB") && curGoto == 1,
                  "本脚本优先：GOTO 也走 S04.ERB:1");

            // 入口点：同一个 firstLabel，答案与装载序无关。
            table.setEntryPoint(QStringLiteral("SHARED"));
            checkStr(table.getCurrentScript(), "S01.ERB", "入口脚本 = 全序第一的脚本");
            checkInt(table.currentLine(), 5, "入口行 = 同一条目的行号");

            return QStringLiteral("call=%1:%2 goto=%3:%4 entry=%5:%6 labels=%7")
                .arg(callScript).arg(callLine).arg(gotoScript).arg(gotoLine)
                .arg(table.getCurrentScript()).arg(table.currentLine()).arg(all.size());
        };

        const QString forward = run(names);
        QStringList reversed = names;
        std::reverse(reversed.begin(), reversed.end());
        const QString backward = run(reversed);
        checkStr(backward, forward, "相反装载序 -> CALL/GOTO/入口点/索引全序逐字相同");
    }

    // -----------------------------------------------------------------------
    // 12) 一个名字只有一个行号：getLabelPosition 与 CALL / 索引同解
    //
    // 同一脚本里 @DUP 声明两次（行 0 / 行 3）。旧 ScriptData::labelPositions
    // 是 last-wins（答 3），索引是 first-wins（答 0），CALL 上一刀已改成
    // first-wins —— 本刀删掉前两张表后，三者只能是同一个答案 0。
    // -----------------------------------------------------------------------
    qDebug() << "\n12) 同脚本重复声明：CALL / getLabelPosition / 索引第一条逐值相等";
    {
        const QString script = QStringLiteral("DUP.ERB");
        EraParseTable table(nullptr);
        table.loadScript(script,
                         {funcLabel(QStringLiteral("DUP"), script, 0),
                          instr(script, 0, QStringLiteral("PRINT"), QStringLiteral("DUP")),
                          funcLabel(QStringLiteral("DUP"), script, 2),
                          instr(script, 2, QStringLiteral("PRINT"), QStringLiteral("DUP")),
                          instr(script, 2, QStringLiteral("RETURN"), QStringLiteral("DUP"))});

        // (b) 装载完成**之前**：索引尚未跨脚本排序固化，但同脚本内按行号
        //     追加，因此 onlyScript 查询的答案已是确定的 0。
        const int before = table.getLabelPosition(script, QStringLiteral("DUP"));
        checkInt(before, 0, "markLoadComplete 之前：第一条 = 行 0");

        table.markLoadComplete();
        const int after = table.getLabelPosition(script, QStringLiteral("DUP"));
        checkInt(after, 0, "markLoadComplete 之后：同一个答案（行 0）");
        checkInt(before, after, "装载完成前后答案相同");

        const QList<LabelRef> decls = table.labelIndex().value(QStringLiteral("DUP"));
        checkInt(int(decls.size()), 2, "索引保留两条声明（事件导航需要全部）");
        if (!decls.isEmpty()) checkInt(decls.first().line, 0, "索引第一条 = 行 0（first-wins）");

        table.setPosition(script, 0, /*jumped=*/false);
        QString targetScript;
        int targetLine = -1;
        check(table.resolveCallableLabel(QStringLiteral("DUP"), targetScript, targetLine),
              "resolveCallableLabel(DUP) 成功");
        checkStr(targetScript, script, "CALL 落在同脚本");
        checkInt(targetLine, after, "CALL 目标行 == getLabelPosition == 索引第一条（0）");

        // (c) 大小写不敏感：索引键是大写折叠的。旧 ErbLoader::m_labelPositions
        //     用 "script:label" 原样拼接、大小写敏感；那张表已删，这条钉住回归。
        const QString mixed = QStringLiteral("CASE.ERB");
        check(table.beginIncrementalLoad(), "增量装载 CASE.ERB 前 beginIncrementalLoad");
        table.loadScript(mixed,
                         {funcLabel(QStringLiteral("Foo"), mixed, 0),
                          instr(mixed, 0, QStringLiteral("RETURN"), QStringLiteral("Foo"))});
        const int foo = table.getLabelPosition(mixed, QStringLiteral("foo"));
        const int FOO = table.getLabelPosition(mixed, QStringLiteral("FOO"));
        const int Foo = table.getLabelPosition(mixed, QStringLiteral("Foo"));
        checkInt(foo, 0, "查询 \"foo\" -> 行 0");
        checkInt(FOO, 0, "查询 \"FOO\" -> 行 0");
        checkInt(Foo, foo, "三种大小写同解");
        checkInt(FOO, Foo, "三种大小写同解（全等）");
        checkInt(table.getLabelPosition(QStringLiteral("NO_SUCH.ERB"), QStringLiteral("Foo")), -1,
                 "不存在的脚本 -> -1（空 onlyScript 不会退化成跨脚本查询）");

        // 重复装载同名脚本（LSP 编辑/测试 reload）：索引随 ScriptData 一起替换。
        // 若旧声明留在索引里，firstLabel 会答出一个新版本里不存在的行。
        const QString reloaded = QStringLiteral("RELOAD.ERB");
        EraParseTable r(nullptr);
        r.loadScript(reloaded, {funcLabel(QStringLiteral("OLD"), reloaded, 0),
                                instr(reloaded, 0, QStringLiteral("RETURN"), QStringLiteral("OLD")),
                                funcLabel(QStringLiteral("DUP"), reloaded, 2)});
        // 第二版第一行在**任何 @ 之前**：段起点 = -1（文件头段），与
        // loadScript 的 currentFunctionLine 初值同源。
        r.loadScript(reloaded, {instr(reloaded, -1, QStringLiteral("PRINT"), QString()),
                                funcLabel(QStringLiteral("DUP"), reloaded, 1)});
        checkInt(r.getLabelPosition(reloaded, QStringLiteral("OLD")), -1,
                 "reload 后旧标签从索引消失");
        checkInt(r.getLabelPosition(reloaded, QStringLiteral("DUP")), 1,
                 "reload 后新标签行号生效（不是旧版的 2）");
    }

    // -----------------------------------------------------------------------
    // 13) 栈深只有一个事实来源
    //
    // depth() 是 callStack 的**公开名字**，不是第二份副本：任何时候都必须
    // 等于 callStack().size()。callStackChanged(int) 的实参同样必须是
    // 「发信号那一刻」的栈深 —— 这里逐次录下来，既比较每次的实参，也比较
    // 整条序列，作为「信号值 = 栈深」的唯一证据。
    // -----------------------------------------------------------------------
    qDebug() << "\n13) depth() == callStack().size()（栈深唯一事实来源）";
    {
        EraParseTable table(nullptr);
        table.loadScript(QStringLiteral("A.ERB"), makeScriptA().lines);
        table.loadScript(QStringLiteral("B.ERB"), makeScriptB().lines);

        // 不变量：每个操作之后都查一次。
        auto invariant = [&table](const QString& what) {
            checkInt(table.depth(), int(table.callStack().size()),
                     QStringLiteral("不变量 depth() == callStack().size()：%1").arg(what));
        };

        int signalCount = 0;
        bool payloadMatches = true;
        QList<int> seenDepths;
        // emitEffects 是同步的：lambda 里读到的 table 状态就是发信号那一刻的状态。
        QObject::connect(&table, &EraParseTable::callStackChanged, [&](int d) {
            ++signalCount;
            seenDepths << d;
            if (d != table.depth() || d != table.callStack().size()) payloadMatches = false;
        });

        table.setPosition(QStringLiteral("A.ERB"), 2, true);

        // (a) reset：depth() == 0 且栈空
        table.resetPosition();
        checkInt(table.depth(), 0, "reset 后 depth() == 0");
        check(table.callStack().isEmpty(), "reset 后 callStack 为空");
        checkInt(signalCount, 1, "reset 发一次 callStackChanged");
        invariant(QStringLiteral("reset 后"));

        // (b) 连续三次压帧 -> depth() == 3
        check(table.callLabel(QStringLiteral("REMOTE")), "第 1 次压帧（A.ERB:2 -> B.ERB:REMOTE）");
        checkInt(table.depth(), 1, "压一帧后 depth() == 1");
        check(table.callLabel(QStringLiteral("SUB")), "第 2 次压帧（B.ERB -> A.ERB:SUB）");
        checkInt(table.depth(), 2, "压两帧后 depth() == 2");
        check(table.callLabel(QStringLiteral("REMOTE_2")), "第 3 次压帧（A.ERB -> B.ERB:REMOTE_2）");
        checkInt(table.depth(), 3, "连续三次压帧后 depth() == 3");
        invariant(QStringLiteral("三次压帧后"));

        // (c) 弹两次 -> depth() == 1
        const int beforePops = signalCount;
        check(table.returnFromCall(), "第 1 次弹帧");
        check(table.returnFromCall(), "第 2 次弹帧");
        checkInt(table.depth(), 1, "弹两次后 depth() == 1");
        checkInt(signalCount - beforePops, 2, "两次弹帧各发一次 callStackChanged");
        invariant(QStringLiteral("弹两次后"));

        // (d) 空栈：弹帧失败、depth() == 0、不亮 callStackChanged
        check(table.returnFromCall(), "第 3 次弹帧（弹到空栈）");
        checkInt(table.depth(), 0, "弹到空栈后 depth() == 0");
        const int beforeEmptyPop = signalCount;
        check(!table.returnFromCall(), "空栈 returnFromCall 返回 false");
        checkInt(table.depth(), 0, "空栈弹帧后 depth() 仍为 0");
        checkInt(signalCount - beforeEmptyPop, 0, "空栈弹帧不亮 callStackChanged");
        invariant(QStringLiteral("空栈弹帧后"));

        // (e) 基元层同样：空栈 popFrame 返回默认帧且一位都不亮
        {
            PositionState s;
            PositionEffects fx;
            const Frame fr = position::popFrame(s, fx);
            check(fr.script.isEmpty() && fr.returnLine == 0, "空栈 popFrame 返回默认 Frame");
            check(s.callStack.isEmpty(), "空栈 popFrame 后 callStack 仍为空");
            only(fx, false, false, false, false, "空栈 popFrame 一位都不亮");
        }

        // (f) 信号值 = 发信号那一刻的栈深
        check(signalCount > 0, "本轮至少发过一次 callStackChanged");
        check(payloadMatches, "每次 callStackChanged(int) 的实参都等于当时的 depth()");
        QStringList seen;
        for (int d : seenDepths) seen << QString::number(d);
        checkStr(seen.join(QLatin1Char(' ')), QStringLiteral("0 1 2 3 2 1 0"),
                 "callStackChanged 的实参序列 = reset(0) / 压 1,2,3 / 弹 2,1,0");
    }

    // -----------------------------------------------------------------------
    // 14) executionScopeId 只读位置：每次调用都按当前行现算（没有缓存）
    //
    // 段身份由装载期写好的 LogicalLine::scopeKey 决定；函数只读 PositionState。
    // 这节把「每次现算、跟着位置走」钉住：不缓存返回值，也不回写 state。
    // -----------------------------------------------------------------------
    qDebug() << "\n14) executionScopeId 每次现算（位置决定段身份）";
    {
        // (a) 同一行连续取两次：结果相同，且都等于对当前行 scopeKey 现 intern 的结果。
        PositionState s;
        PositionEffects fx;
        position::setPosition(s, view, QStringLiteral("A.ERB"), 1, true, fx);
        const auto idMain = position::executionScopeId(s, view);
        const auto idMainAgain = position::executionScopeId(s, view);
        check(idMain == idMainAgain, "同一行取两次：结果相同");
        check(idMain == eravar::internName(makeScriptA().lines.at(1).scopeKey),
              "取值 == internName(当前行 scopeKey)（每次现算）");

        // (b) 换到另一个 @ 标签（SUB）后 id 改变；回到原行后复原。
        fx = {};
        position::setPosition(s, view, QStringLiteral("A.ERB"), 6, false, fx);
        const auto idSub = position::executionScopeId(s, view);
        check(idSub != idMain, "换到另一段（@SUB）后 id 改变");
        check(idSub == eravar::internName(makeScriptA().lines.at(6).scopeKey),
              "@SUB 行的 id == internName(该行 scopeKey)");
        fx = {};
        position::setPosition(s, view, QStringLiteral("A.ERB"), 2, false, fx);
        checkInt(int(position::executionScopeId(s, view)), int(idMain),
                 "回到 @MAIN 段的行后 id 复原");

        // (c) 同名 @MAIN 的第二份声明：段身份是 (脚本, 段起点行)，所以 id 不同。
        //     两个脚本各一行 @MAIN，scopeKey 用真实的 segmentKeyText 构造。
        auto sameNameScript = [](const QString& script) {
            LogicalLine ll;
            ll.kind = LineKind::FunctionLabel;
            ll.labelName = QStringLiteral("MAIN");
            ll.ownerFunction = QStringLiteral("MAIN");
            ll.scopeKey = eravar::segmentKeyText(script, 0);   // 段身份 = (脚本, 0)
            ScriptData d;
            d.lines = {ll};
            return d;
        };
        QHash<QString, ScriptData> dupScripts;
        dupScripts.insert(QStringLiteral("D1.ERB"), sameNameScript(QStringLiteral("D1.ERB")));
        dupScripts.insert(QStringLiteral("D2.ERB"), sameNameScript(QStringLiteral("D2.ERB")));
        ScriptView dupView;
        dupView.scripts = &dupScripts;
        PositionState ds;
        position::setPosition(ds, dupView, QStringLiteral("D1.ERB"), 0, true, fx);
        const auto idDup1 = position::executionScopeId(ds, dupView);
        fx = {};
        position::setPosition(ds, dupView, QStringLiteral("D2.ERB"), 0, false, fx);
        const auto idDup2 = position::executionScopeId(ds, dupView);
        check(idDup1 != idDup2, "同名 @MAIN 的另一份声明（另一段身份）id 不同");
        check(idDup2 == eravar::internName(sameNameScript(QStringLiteral("D2.ERB")).lines.at(0).scopeKey),
              "D2.ERB:0 的 id == internName(该行 scopeKey)");
        fx = {};
        position::setPosition(ds, dupView, QStringLiteral("D1.ERB"), 0, false, fx);
        checkInt(int(position::executionScopeId(ds, dupView)), int(idDup1),
                 "回到 D1.ERB 的同名段后 id 复原");

        // (d) 文件头段：段起点行 = -1，身份仍存在（不是 kGlobalScope）。
        LogicalLine header;
        header.kind = LineKind::Instruction;
        header.functionName = QStringLiteral("PRINT");
        header.scopeKey = eravar::segmentKeyText(QStringLiteral("HDR.ERH"), -1);
        QHash<QString, ScriptData> hdrScripts;
        ScriptData hdrData;
        hdrData.lines = {header};
        hdrScripts.insert(QStringLiteral("HDR.ERH"), hdrData);
        ScriptView hdrView;
        hdrView.scripts = &hdrScripts;
        PositionState hs;
        position::setPosition(hs, hdrView, QStringLiteral("HDR.ERH"), 0, true, fx);
        const auto idHeader = position::executionScopeId(hs, hdrView);
        check(idHeader == eravar::internName(header.scopeKey),
              "文件头段 id == internName((HDR.ERH,-1) 的段身份)");
        check(idHeader != eravar::kGlobalScope, "文件头段的 id 不是 kGlobalScope");

        // (e) 没有行就没有段身份：空位置 / 越界 / 负行 / 未知脚本 -> kGlobalScope。
        PositionState none;
        checkInt(int(position::executionScopeId(none, view)), int(eravar::kGlobalScope),
                 "空位置（空 script）-> kGlobalScope");
        PositionState oob;
        oob.script = QStringLiteral("A.ERB");
        oob.line = 999;
        checkInt(int(position::executionScopeId(oob, view)), int(eravar::kGlobalScope),
                 "越界行 -> kGlobalScope");
        oob.line = -7;
        checkInt(int(position::executionScopeId(oob, view)), int(eravar::kGlobalScope),
                 "负行 -> kGlobalScope");
        PositionState unknown;
        unknown.script = QStringLiteral("NO.ERB");
        checkInt(int(position::executionScopeId(unknown, view)), int(eravar::kGlobalScope),
                 "未知脚本 -> kGlobalScope");

        // (f) 编译期证据：签名确实是 const PositionState&。
        static_assert(std::is_same_v<decltype(&position::executionScopeId),
                                     eravar::ScopeId (*)(const PositionState&, const ScriptView&)>,
                      "executionScopeId 必须接受 const PositionState&");
        eravar::ScopeId (*const scopeFn)(const PositionState&, const ScriptView&) =
            &position::executionScopeId;
        const PositionState& constRef = s;
        checkInt(int(scopeFn(constRef, view)), int(idMain),
                 "经 const PositionState& 调用结果不变");
    }

    if (g_failures == 0) qDebug() << "\nALL PASSED";
    else qDebug() << "\nFAILURES:" << g_failures;
    return g_failures == 0 ? 0 : 1;
}