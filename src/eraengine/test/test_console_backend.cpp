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
        // 一个核 → 整行一段，且可点击（C#：buttonCount <= 1 时整行是一段）
        ConsoleBackend c;
        c.print(QString::fromUtf8("[0] 开始游戏"));
        c.newline();
        c.flush();
        const ConsoleDisplayLine& l = c.buffer().at(0);
        check(l.segments.size() == 1, "[0] -> 1 段（整行）");
        if (l.segments.size() == 1) {
            check(l.segments.first().isButton, "这一段可点击");
            check(l.segments.first().isInteger && l.segments.first().intValue == 0,
                  "按钮值 == 0");
            check(l.segments.first().spans.size() == 1
                      && l.segments.first().spans.first().text == QString::fromUtf8("[0] 开始游戏"),
                  "整行一个最小单位区块（文本未切碎）");
        }
        check(l.plainText() == QString::fromUtf8("[0] 开始游戏"), "文本保留原样");

        ConsoleBackend c2;
        c2.print(QString::fromUtf8("[9999] 設定完毕"));
        c2.newline();
        c2.flush();
        check(c2.buffer().at(0).segments.size() == 1
                  && c2.buffer().at(0).segments.first().isButton
                  && c2.buffer().at(0).segments.first().intValue == 9999, "[9999] -> 9999");

        // 非数字 `[abc]`：不是核 → 整行不可点击
        ConsoleBackend c3;
        c3.print(QString::fromUtf8("[abc] 不是数字"));
        c3.newline();
        c3.flush();
        check(!c3.buffer().at(0).segments.isEmpty()
                  && !c3.buffer().at(0).segments.first().isButton, "[abc] 不建按钮");

        // 十六进制 / 指数 / 带符号（C# 正则允许）
        ConsoleBackend c4;
        c4.print(QString::fromUtf8("[0x10] 十六进制"));
        c4.newline();
        c4.flush();
        check(c4.buffer().at(0).segments.first().isButton
                  && c4.buffer().at(0).segments.first().intValue == 16,
              "[0x10] -> 16（C# 正则支持 0x 前缀）");
    }

    // 多个核 → 切成多段（C# 的多段状态机）
    qDebug() << "\n多个按钮切段";
    {
        ConsoleBackend c;
        c.print(QString::fromUtf8("[1] 选择一 [2] 选择二"));
        c.newline();
        c.flush();
        const ConsoleDisplayLine& l = c.buffer().at(0);
        int buttons = 0;
        for (const ConsoleSegment& s : l.segments) if (s.isButton) ++buttons;
        check(buttons == 2, "两个核 -> 两段可点击");
        check(l.plainText() == QString::fromUtf8("[1] 选择一 [2] 选择二"), "切段不改变文本");
    }

    qDebug() << "\nPRINTBUTTON";
    {
        ConsoleBackend c;
        c.printButton(QString::fromUtf8("[←]"), 4);
        c.printButton(QString::fromUtf8("[↓]"), 2);
        c.newline();
        c.flush();
        const ConsoleDisplayLine& l = c.buffer().at(0);
        check(l.segments.size() == 2, "两个 PRINTBUTTON -> 2 段");
        check(l.segments.at(0).intValue == 4 && l.segments.at(1).intValue == 2, "值 4/2");
    }

    qDebug() << "\n最小单位区块 / 图 / 形";
    {
        ConsoleBackend c;
        c.print("A");
        c.printImage("face_01", 100, 100);   // width/height 是 FontSize 的百分比
        c.printShape("rect", {100});
        c.newline();
        c.flush();
        const ConsoleDisplayLine& l = c.buffer().at(0);
        check(l.spanCount() == 3, "3 个最小单位区块（文本/图/形）");
        if (l.spanCount() == 3) {
            const QList<ConsoleSpan> parts = l.flatSpans();
            check(parts.at(0).kind == ConsoleSpanKind::Text, "第 1 个是 text");
            check(parts.at(1).kind == ConsoleSpanKind::Image
                      && parts.at(1).text == "face_01", "第 2 个是 image(face_01)");
            check(parts.at(2).kind == ConsoleSpanKind::Shape
                      && parts.at(2).shapeType == "rect", "第 3 个是 shape(rect)");
            check(parts.at(0).relCol == 0, "区块自带相对列 relCol");
            check(parts.at(0).cols == 1, "文本 'A' 占 1 个单位长");
            check(parts.at(1).cols == 2, "图片 100% FontSize = 18px = 2 个单位长");
        }
    }

    // CLEARLINE 只数逻辑行（C# deleteLine）
    qDebug() << "\nCLEARLINE";
    {
        ConsoleBackend c;
        c.print("A"); c.newline();
        c.print("B"); c.newline();
        c.print("C"); c.newline();
        c.flush();
        check(c.lineCount() == 3, "3 行");
        check(c.logicalLineCount() == 3, "LINECOUNT == 3");
        c.clearLines(2);
        check(c.lineCount() == 1 && linePlain(c, 0) == "A", "CLEARLINE 2 -> 只剩 A");
        check(c.logicalLineCount() == 1, "CLEARLINE 后 LINECOUNT == 1");
    }

    qDebug() << "\n===================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] console backend tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
