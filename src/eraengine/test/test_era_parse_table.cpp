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
// test_era_parse_table.cpp
//
// 任务 3 的验证：EraParseTable 的“位置区”。
//   - m_currentScript / m_currentLine(PC) / m_callStack / m_depth
//   - 位置变换槽：advance / jumpToLine / jumpToLabel / callLabel /
//     returnFromCall / setPosition / resetPosition
//   - 位置区信号：positionChanged / callStackChanged
//
// 不依赖 Qt Test，直接断言并在失败时返回非零退出码。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include "ast/logical_line.h"
#include "process_state.h"
#include "era_parse_table.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) {
        qDebug().noquote() << "  [ok ]" << what;
    } else {
        qDebug().noquote() << "  [FAIL]" << what;
        ++g_failures;
    }
}

static LogicalLine makeLabel(const QString& label, int lineNo) {
    LogicalLine ll;
    ll.kind = LineKind::FunctionLabel;
    ll.labelName = label;
    ll.position = ScriptPosition("test.ERB", lineNo, 0);
    ll.raw = "@" + label;
    return ll;
}

static LogicalLine makeInstr(const QString& name, const QStringList& args, int lineNo) {
    LogicalLine ll;
    ll.kind = LineKind::Instruction;
    ll.functionName = name.toUpper();
    ll.position = ScriptPosition("test.ERB", lineNo, 0);
    for (const QString& a : args) {
        ll.arguments.append(Operand(a));
    }
    return ll;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "EraParseTable position-region test";
    qDebug() << "=================================";

    ProcessState state;
    EraParseTable table(&state);

    // ---- 信号计数 ----
    int positionChangedCount = 0;
    int maxDepth = 0;
    int lastDepth = -1;
    QObject::connect(&table, &EraParseTable::positionChanged,
                     [&](const QString&, int) { ++positionChangedCount; });
    QObject::connect(&table, &EraParseTable::callStackChanged,
                     [&](int depth) {
                         lastDepth = depth;
                         if (depth > maxDepth) maxDepth = depth;
                     });

    // ---- 只读区：装载两个脚本 ----
    QList<LogicalLine> main;
    main << makeLabel("MAIN", 0)
         << makeInstr("PRINT", {"\"main\""}, 1)
         << makeInstr("GOTO", {"SUB"}, 2)
         << makeLabel("SUB", 3)
         << makeInstr("RETURN", {}, 4);

    QList<LogicalLine> other;
    other << makeLabel("OTHER_ENTRY", 0)
          << makeInstr("PRINT", {"\"other\""}, 1);

    check(table.loadScript("main", main), "loadScript(\"main\")");
    check(table.loadScript("other", other), "loadScript(\"other\")");

    qDebug() << "\n1) entry point positions the PC";
    table.setEntryPoint("MAIN");
    check(table.currentScript() == "main", "currentScript == main");
    check(table.currentLine() == 0, QString("currentLine == 0 (got %1)").arg(table.currentLine()));
    check(table.depth() == 0, "depth == 0");
    check(table.hasPosition(), "hasPosition()");

    qDebug() << "\n2) advance() moves the PC";
    table.advance();
    check(table.currentLine() == 1, QString("currentLine == 1 (got %1)").arg(table.currentLine()));

    qDebug() << "\n3) jumpToLine() bounds checking";
    check(table.jumpToLine(4), "jumpToLine(4) ok");
    check(table.currentLine() == 4, "currentLine == 4");
    check(!table.jumpToLine(99), "jumpToLine(99) rejected");
    check(!table.jumpToLine(-1), "jumpToLine(-1) rejected");
    check(table.currentLine() == 4, "currentLine unchanged after rejected jumps");

    qDebug() << "\n4) jumpToLabel() within a script does not push";
    check(table.jumpToLabel("SUB"), "jumpToLabel(SUB) ok");
    check(table.currentLine() == 3, "currentLine == 3");
    check(table.depth() == 0, "depth still 0");

    qDebug() << "\n5) callLabel() pushes a frame and records return address";
    check(table.callLabel("SUB"), "callLabel(SUB) ok");
    check(table.depth() == 1, "depth == 1");
    check(table.currentLine() == 3, "currentLine == 3 (callee)");
    check(table.currentFrame().script == "main", "frame.script == main");
    check(table.currentFrame().returnLine == 4, "frame.returnLine == 4 (caller line + 1)");
    check(table.currentFrame().callLabel == "SUB", "frame.callLabel == SUB");

    qDebug() << "\n6) callLabel() across scripts";
    check(table.callLabel("OTHER_ENTRY"), "callLabel(OTHER_ENTRY) ok");
    check(table.currentScript() == "other", "switched to other");
    check(table.currentLine() == 0, "currentLine == 0 in other");
    check(table.depth() == 2, "depth == 2");
    check(table.callStack().size() == 2, "callStack().size() == 2");

    qDebug() << "\n7) returnFromCall() pops and restores position";
    check(table.returnFromCall(), "returnFromCall() #1");
    check(table.currentScript() == "main", "back to main");
    check(table.currentLine() == 4, "currentLine == 4 (return address)");
    check(table.depth() == 1, "depth == 1");

    check(table.returnFromCall(), "returnFromCall() #2");
    check(table.depth() == 0, "depth == 0");
    check(table.currentLine() == 4, "currentLine == 4");

    check(!table.returnFromCall(), "returnFromCall() on empty stack rejected");

    qDebug() << "\n8) resetPosition() clears the call stack and PC";
    table.advance();
    table.callLabel("SUB");
    table.resetPosition();
    check(table.currentLine() == 0, "currentLine == 0");
    check(table.depth() == 0, "depth == 0");
    check(table.callStack().isEmpty(), "callStack empty");

    qDebug() << "\n9) setPosition() switches script and line";
    table.setPosition("other", 1);
    check(table.currentScript() == "other", "currentScript == other");
    check(table.currentLine() == 1, "currentLine == 1");

    qDebug() << "\n10) signals";
    check(positionChangedCount > 0, QString("positionChanged emitted %1 times").arg(positionChangedCount));
    check(maxDepth == 2, QString("callStackChanged reached depth 2 (got %1)").arg(maxDepth));
    check(lastDepth == 0, QString("last callStackChanged depth == 0 (got %1)").arg(lastDepth));

    qDebug() << "\n11) EXISTFUNCTION 语义（C# ExistFunctionMethod）";
    // 两条路的结果集合相同：**大小写无关**，且只认非事件函数（GetNonEventLabel）。
    // 旧实现把「第 2 引数不写」当成大小写敏感，退化成逐个比对 originalName 的
    // 线性扫描 —— FL3.8 的 @FL_INIT_LOADER 扫 6000 次 × 11000 个函数 = 6600 万次
    // 字符串比较，选「重新開始」后几十秒不动，看起来就是状态机死了。
    {
        const auto makePre = [](const QString& raw, int lineNo) {
            LogicalLine ll;
            ll.kind = LineKind::Preprocessor;
            ll.raw = raw;
            ll.position = ScriptPosition("test.ERB", lineNo, 0);
            return ll;
        };
        QList<LogicalLine> funcs;
        funcs << makeLabel("PLAIN_FUNC", 0)
              << makeLabel("EXIST_METHOD", 1) << makePre("#FUNCTION", 2)
              << makeLabel("EXIST_METHODS", 3) << makePre("#FUNCTIONS", 4)
              << makeLabel("EVENTFIRST", 5);
        EraParseTable fnTable(&state);
        check(fnTable.loadScript("funcs", funcs), "loadScript(\"funcs\")");
        check(fnTable.functionExistsKind(QStringLiteral("PLAIN_FUNC"), true) == 1,
              "通常関数 -> 1");
        check(fnTable.functionExistsKind(QStringLiteral("plain_func"), true) == 1,
              "大小写无关（默认路径不再按 originalName 线性扫描）");
        check(fnTable.functionExistsKind(QStringLiteral("EXIST_METHOD"), true) == 2,
              "#FUNCTION -> 2");
        check(fnTable.functionExistsKind(QStringLiteral("exist_methods"), true) == 3,
              "#FUNCTIONS -> 3（大小写无关）");
        check(fnTable.functionExistsKind(QStringLiteral("EVENTFIRST"), true) == 0,
              "事件函数不算（GetNonEventLabel）");
        check(fnTable.functionExistsKind(QStringLiteral("NOT_DEFINED"), true) == 0,
              "未定义 -> 0");
    }

    qDebug() << "\n12) 跨文件 CALL / JUMP 的标签解析（m_labelLists 全局索引）";
    // 旧实现：当前脚本没命中就 for(m_scripts) 扫全部脚本 + 每脚本一次
    // label.toUpper()。eraTW/FL3.8 有 1500+ 个脚本，跨文件 CALL / 式中调用
    // 每次都要付这个 O(n)（eraTW 地图逐格 TRYCALLFORM 一次上万次）。
    // resolveCallableLabel 改查 m_labelLists（装载期就建好的全局索引），
    // 此处锁行为：跨文件命中、当前脚本优先、$ 标签不可调用。
    {
        const auto makeGoto = [](const QString& label, int lineNo) {
            LogicalLine ll;
            ll.kind = LineKind::GotoLabel;
            ll.labelName = label;
            ll.position = ScriptPosition("other2.ERB", lineNo, 0);
            ll.raw = "$" + label;
            return ll;
        };
        QList<LogicalLine> other2;
        other2 << makeLabel("REMOTE_FUNC", 0)
               << makeInstr("RETURN", {}, 1)
               << makeGoto("REMOTE_GOTO", 2);       // 同脚本里同名 $ 标签
        // 第三个脚本：与 other2 同名（同名多份取排序后第一份，确定可复现）
        QList<LogicalLine> other3;
        other3 << makeLabel("REMOTE_FUNC", 0)
               << makeInstr("RETURN", {}, 1);

        EraParseTable ct(&state);
        check(ct.loadScript("main.ERB", main), "loadScript(main.ERB)");
        check(ct.loadScript("other2.ERB", other2), "loadScript(other2.ERB)");
        check(ct.loadScript("other3.ERB", other3), "loadScript(other3.ERB)");
        ct.markLoadComplete();
        ct.setEntryPoint("MAIN");
        check(ct.currentScript() == "main.ERB", "entry in main.ERB");

        // 跨文件 CALL：目标在 other2/other3（$REMOTE_GOTO 不可调用）
        check(ct.callLabel("REMOTE_FUNC"), "跨文件 callLabel 命中 @REMOTE_FUNC");
        check(ct.currentScript() != "main.ERB"
                  && ct.currentLine() == 0
                  && ct.currentFrame().callLabel == "REMOTE_FUNC",
              "callLabel 落在目标函数标签行（非 $ 标签）");
        check(ct.returnFromCall(), "returnFromCall 回到 main.ERB");

        // 式中调用（callLabelWithReturn）：显式返回地址
        check(ct.callLabelWithReturn("REMOTE_FUNC", 1),
              "跨文件 callLabelWithReturn 命中");
        check(ct.currentLine() == 0, "callLabelWithReturn 落在目标首行");
        check(ct.returnFromCall(), "callLabelWithReturn 返回");

        // JUMP（有帧时继承返回地址）
        ct.advance();
        ct.callLabel("SUB", true);
        check(ct.jumpLabel("REMOTE_FUNC"), "跨文件 jumpLabel 命中");
        check(ct.currentFrame().isJump, "JUMP 帧标记 isJump");
        while (ct.returnFromCall()) {}
        ct.setPosition("main.ERB", 0, false);

        // 当前脚本优先：main.ERB 里也有 @SUB —— callLabel("SUB") 必须留在 main.ERB
        check(ct.callLabel("SUB"), "同名时当前脚本优先命中（@SUB in main.ERB）");
        check(ct.currentScript() == "main.ERB", "callLabel(SUB) 未跨脚本");
        ct.returnFromCall();

        // $ 标签不可 CALL：other2 只有 $REMOTE_GOTO（无同名 @）
        check(!ct.callLabel("REMOTE_GOTO"), "$ 标签不可被 CALL 命中");
        // 未定义名字
        check(!ct.callLabel("NO_SUCH_LABEL"), "未定义标签 -> false");

        // labels() 装载收尾后已排序固化：同名多份按 (脚本名, 行号)
        const QList<LabelRef> refs = ct.labels("REMOTE_FUNC");
        check(refs.size() == 2, "同名 @REMOTE_FUNC 两份都在索引里");
        check(refs.size() == 2
                  && refs.at(0).script == "other2.ERB" && refs.at(1).script == "other3.ERB",
              "同名多份按 (脚本名, 行号) 排序固化（不再每次调用重排）");

        // TRY 系探针（hasCallableLabel）：与 callLabel 同源判定，O(1)。
        // 旧 hasLabel 是「@/$ 通吃 + 按名字缓存」，动态 FORM 名（TRYCALLFORM
        // 逐个试）每次都缓存 miss -> for(m_scripts) 全量扫描（eraTW 的
        // @SETLIST_EXIST 一轮 2435 次失败探针实测 830ms）。
        check(ct.hasCallableLabel("REMOTE_FUNC"), "hasCallableLabel：@标签 -> true");
        check(ct.hasCallableLabel("remote_func"), "hasCallableLabel：大小写无关");
        check(!ct.hasCallableLabel("REMOTE_GOTO"), "$ 标签不可 CALL（旧路径走到 callLabel 才失败）");
        check(!ct.hasCallableLabel("NO_SUCH_LABEL"), "未定义 -> false");
    }

    qDebug() << "\n=================================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] position-region tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
