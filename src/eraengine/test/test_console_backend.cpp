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
// test_console_backend.cpp
//
// 验证显示层（GameView/ConsoleBackend）：
//   1. 有界缓冲（历史不可能全显示）
//   2. 可见窗口 + 滚动（offset / followTail）
//   3. 输出写入（print/newline/printButton）与对齐
//   4. 按钮命中 -> 输入信号；generation 失效旧按钮
//   5. 1Hz 刷新合并（脏标记 + flush）
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>

#include "console_backend.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) {
        qDebug().noquote() << "  [ok ]" << what;
    } else {
        qDebug().noquote() << "  [FAIL]" << what;
        ++g_failures;
    }
}

static QString linePlain(const ConsoleBackend& b, int visibleIndex) {
    return b.visibleLine(visibleIndex).value("plain").toString();
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "ConsoleBackend test";
    qDebug() << "===================";

    ConsoleBackend console;
    console.setVisibleCount(3);

    // ---- 1. 输出写入 ----
    qDebug() << "\n1) 输出写入（print/newline）";
    const QStringList src = {"A", "B", "C", "D", "E"};
    for (const QString& s : src) {
        console.print(s);
        console.newline();
    }
    check(console.lineCount() == 5, "lineCount == 5");

    // ---- 2. 可见窗口（只显示窗口，不显示全部）----
    qDebug() << "\n2) 可见窗口";
    check(console.visibleLineCount() == 3, "visibleLineCount == 3");
    check(linePlain(console, 0) == "C", "window[0] == C（最新 3 行）");
    check(linePlain(console, 2) == "E", "window[2] == E");
    check(console.followTail(), "followTail == true");

    // ---- 3. 滚动（历史回顾）----
    qDebug() << "\n3) 滚动";
    console.scrollBy(2);
    check(!console.followTail(), "scrollBy(2) -> not followTail");
    check(console.scrollOffset() == 2, "scrollOffset == 2");
    check(linePlain(console, 0) == "A", "window[0] == A after scroll");
    console.scrollToBottom();
    check(console.followTail() && console.scrollOffset() == 0, "scrollToBottom -> followTail");

    // ---- 4. 有界缓冲 ----
    qDebug() << "\n4) 有界缓冲（丢最旧）";
    console.buffer().setCapacity(3);
    console.print("F"); console.newline();
    console.print("G"); console.newline();
    check(console.lineCount() == 3, "capacity 3 -> lineCount capped at 3");
    check(linePlain(console, 0) == "E" && linePlain(console, 2) == "G", "kept the newest 3 (E,F,G)");

    // ---- 5. 按钮 -> 输入；generation 失效 ----
    qDebug() << "\n5) 按钮与输入";
    int submitted = -1;
    QObject::connect(&console, &ConsoleBackend::inputSubmitted,
                     [&submitted](qint64 v) { submitted = static_cast<int>(v); });

    console.clearAll();
    console.printButton("[1] 选择一", 1);
    console.printButton("[2] 选择二", 2);
    console.newline();

    // 两个按钮在同一行；点击第 2 个
    const int visIdx = console.visibleLineCount() - 1;
    console.clickAt(visIdx, 1);
    check(submitted == 2, "clickAt(button#1) -> inputSubmitted(2)");

    // 提交后同一批按钮失效（对齐 C# generation）
    submitted = -1;
    console.clickAt(visIdx, 0);
    check(submitted == -1, "stale button after submit ignored");

    // 新一批按钮可点击
    console.printButton("[3] 新选择", 3);
    console.newline();
    console.clickAt(console.visibleLineCount() - 1, 0);
    check(submitted == 3, "new generation button clickable");

    // ---- 6. 刷新合并 ----
    qDebug() << "\n6) 1Hz 刷新合并";
    int windowChanged = 0;
    QObject::connect(&console, &ConsoleBackend::windowChanged,
                     [&windowChanged]() { ++windowChanged; });
    console.print("x"); console.print("y"); console.print("z");   // 多次输出
    console.flush();                                              // 一次刷新
    check(windowChanged == 1, "3 prints + 1 flush -> 1 windowChanged");


    // ---- 菜单项 `[n]` 自动变按钮（对齐 C# ButtonStringCreator）----
    qDebug() << "\n菜单按钮";
    {
        ConsoleBackend c;
        c.print(QString::fromUtf8("[0] 开始游戏"));
        c.newline();
        c.flush();
        const ConsoleDisplayLine& l = c.buffer().at(0);
        check(l.buttons.size() == 1, "[0] -> 1 个按钮");
        check(!l.buttons.isEmpty() && l.buttons.first().isInteger
                  && l.buttons.first().intValue == 0, "按钮值 == 0");
        check(l.plainText() == QString::fromUtf8("[0] 开始游戏"), "文本保留原样");
        // 文本被切成 `[0]` + ` 开始游戏`（按钮覆盖第 1 段）
        check(l.spans.size() == 2, "文本按按钮切成 2 段");
        if (l.spans.size() == 2) {
            check(l.spans.at(0).text == QStringLiteral("[0]"), "第 1 段是 [0]");
            check(l.spans.at(1).text == QString::fromUtf8(" 开始游戏"), "第 2 段是剩余文本");
            check(l.buttons.first().startSpan == 0, "按钮覆盖第 1 段");
        }

        ConsoleBackend c2;
        c2.print(QString::fromUtf8("[9999] 設定完毕"));
        c2.newline();
        c2.flush();
        check(c2.buffer().at(0).buttons.size() == 1
                  && c2.buffer().at(0).buttons.first().intValue == 9999, "[9999] -> 9999");

        ConsoleBackend c3;
        c3.print(QString::fromUtf8("[abc] 不是数字"));
        c3.newline();
        c3.flush();
        check(c3.buffer().at(0).buttons.isEmpty(), "[abc] 不建按钮");
    }

    qDebug() << "\nPRINTBUTTON";
    {
        ConsoleBackend c;
        c.printButton(QString::fromUtf8("[←]"), 4);
        c.printButton(QString::fromUtf8("[↓]"), 2);
        c.newline();
        c.flush();
        const ConsoleDisplayLine& l = c.buffer().at(0);
        check(l.buttons.size() == 2, "两个 PRINTBUTTON");
        check(l.buttons.at(0).intValue == 4 && l.buttons.at(1).intValue == 2, "值 4/2");
    }

    qDebug() << "\nCLEARLINE";
    {
        ConsoleBackend c;
        c.print("A"); c.newline();
        c.print("B"); c.newline();
        c.print("C"); c.newline();
        c.flush();
        check(c.lineCount() == 3, "3 行");
        c.clearLines(2);
        check(c.lineCount() == 1 && linePlain(c, 0) == "A", "CLEARLINE 2 -> 只剩 A");
    }

    qDebug() << "\n===================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] console backend tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
