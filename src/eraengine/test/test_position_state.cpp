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

#include "ast/logical_line.h"
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
static LogicalLine instr(const QString& name, const QString& owner) {
    LogicalLine ll;
    ll.kind = LineKind::Instruction;
    ll.functionName = name;
    ll.ownerFunction = owner;
    ll.scopeKey = owner;   // 本测试只关心「位置 -> 身份」的映射，用 owner 当身份文本
    return ll;
}

static LogicalLine funcLabel(const QString& label) {
    LogicalLine ll;
    ll.kind = LineKind::FunctionLabel;
    ll.labelName = label;
    ll.ownerFunction = label;
    ll.scopeKey = label;   // 段身份（本测试的简化：一段一个名字）
    return ll;
}

static LogicalLine gotoLabel(const QString& label, const QString& owner) {
    LogicalLine ll;
    ll.kind = LineKind::GotoLabel;
    ll.labelName = label;
    ll.ownerFunction = owner;
    ll.scopeKey = owner;
    return ll;
}

static ScriptData makeScriptA() {
    ScriptData d;
    d.lines = {funcLabel("MAIN"), instr("PRINT", "MAIN"), instr("CALL", "MAIN"),
               gotoLabel("PRINT_TITLE", "MAIN"), instr("RETURN", "MAIN"),
               funcLabel("SUB"), instr("PRINT", "SUB"), instr("RETURN", "SUB")};
    return d;
}

static ScriptData makeScriptB() {
    ScriptData d;
    d.lines = {funcLabel("REMOTE"), instr("PRINT", "REMOTE"), instr("RETURN", "REMOTE"),
               funcLabel("PRINT_TITLE"), instr("PRINT", "PRINT_TITLE"),
               funcLabel("MAIN"), instr("RETURN", "MAIN"),
               funcLabel("REMOTE_2"), instr("RETURN", "REMOTE_2")};
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
        checkInt(s.callStack.size(), 1, "压了一帧");
        checkInt(s.depth, 1, "depth 同步");
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
        checkInt(s.callStack.size(), 0, "弹空");
        checkInt(s.depth, 0, "depth 同步");
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
        check(idMain == eravar::internName("MAIN"), "A.ERB:1 的段身份 = MAIN（行上的 scopeKey）");
        check(idMain != eravar::kGlobalScope, "MAIN 的 id 不是顶层");

        fx = {};
        position::setPosition(s, view, "A.ERB", 2, false, fx);   // 同一段内换行
        checkInt(int(position::executionScopeId(s, view)), int(idMain),
                 "同段内换行 id 不变");

        fx = {};
        position::setPosition(s, view, "A.ERB", 6, false, fx);   // 换到 @SUB
        const auto idSub = position::executionScopeId(s, view);
        check(idSub == eravar::internName("SUB"), "A.ERB:6 的段身份 = SUB");
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
        auto sharedScript = [](int padCount) {
            QList<LogicalLine> lines;
            lines << funcLabel(QStringLiteral("PAD"));
            for (int i = 0; i < padCount; ++i) lines << instr(QStringLiteral("PRINT"), QStringLiteral("PAD"));
            lines << funcLabel(QStringLiteral("SHARED"));
            lines << instr(QStringLiteral("RETURN"), QStringLiteral("SHARED"));
            return lines;
        };
        const QStringList names = {QStringLiteral("S01.ERB"), QStringLiteral("S02.ERB"),
                                   QStringLiteral("S03.ERB"), QStringLiteral("S04.ERB"),
                                   QStringLiteral("S05.ERB"), QStringLiteral("S06.ERB")};

        // 返回这一轮的可观察结果；两轮（正序 / 逆序装载）必须逐字相同。
        auto run = [&](const QStringList& order) {
            EraParseTable table(nullptr);
            for (const QString& name : order) {
                table.loadScript(name, sharedScript(name == QLatin1String("S01.ERB") ? 4 : 0));
            }
            table.loadScript(QStringLiteral("N.ERB"),
                             {funcLabel(QStringLiteral("NEUTRAL")),
                              instr(QStringLiteral("RETURN"), QStringLiteral("NEUTRAL"))});
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
                         {funcLabel(QStringLiteral("DUP")),
                          instr(QStringLiteral("PRINT"), QStringLiteral("DUP")),
                          funcLabel(QStringLiteral("DUP")),
                          instr(QStringLiteral("PRINT"), QStringLiteral("DUP")),
                          instr(QStringLiteral("RETURN"), QStringLiteral("DUP"))});

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
                         {funcLabel(QStringLiteral("Foo")),
                          instr(QStringLiteral("RETURN"), QStringLiteral("Foo"))});
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
        r.loadScript(reloaded, {funcLabel(QStringLiteral("OLD")),
                                instr(QStringLiteral("RETURN"), QStringLiteral("OLD")),
                                funcLabel(QStringLiteral("DUP"))});
        r.loadScript(reloaded, {instr(QStringLiteral("PRINT"), QString()),
                                funcLabel(QStringLiteral("DUP"))});
        checkInt(r.getLabelPosition(reloaded, QStringLiteral("OLD")), -1,
                 "reload 后旧标签从索引消失");
        checkInt(r.getLabelPosition(reloaded, QStringLiteral("DUP")), 1,
                 "reload 后新标签行号生效（不是旧版的 2）");
    }

    if (g_failures == 0) qDebug() << "\nALL PASSED";
    else qDebug() << "\nFAILURES:" << g_failures;
    return g_failures == 0 ? 0 : 1;
}