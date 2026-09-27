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

    qDebug() << "\n=================================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] position-region tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
