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
    console.notifyInputRequested("INPUT");   // 输入裁决：等待整数型输入后按钮才可提交
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
    check(windowChanged == 1, "prints batch into one frame notification");
    check(console.currentLineText() == QStringLiteral("xyz"), "pending line survives refresh without forced newline");
    check(console.lineCount() == 2, "pending line is not counted as a physical newline");


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

    {
        ConsoleBackend c;
        int windows = 0, lines = 0;
        QObject::connect(&c, &ConsoleBackend::windowChanged, [&]() { ++windows; });
        QObject::connect(&c, &ConsoleBackend::lineCountChanged, [&]() { ++lines; });
        for (int i = 0; i < 100; ++i) { c.print("batch"); c.newline(); }
        check(windows == 0 && lines == 0, "output does not refresh once per print or newline");
        c.notifyInputRequested("INPUT");
        check(windows == 1 && lines == 1, "input boundary publishes complete output once");
        c.flush();
        check(windows == 1 && lines == 1, "clean flush does not republish");
        c.clearAll();
        c.printButton("old", 9);
        c.newline();
        c.notifyInputRequested("INPUTMOUSEKEY");
        c.notifyInputDone(); // a timer or direct engine input resumed execution
        c.printButton("new", 10);
        c.newline();
        c.notifyInputRequested("INPUT");
        int value = -1;
        QObject::connect(&c, &ConsoleBackend::inputSubmitted, [&](qint64 v) { value = v; });
        c.clickAt(0, 0);
        check(value == -1, "automatic input completion invalidates old buttons");
        c.clickAt(1, 0);
        check(value == 10, "new prompt remains clickable after automatic completion");
    }

    // ---- 跨行图片（立絵）的可见窗口 / 滚动 / ypos ----
    // 复现两个用户可见缺陷（都不需要跑 eraTW）：
    //   ① 跨行图从窗口上方探进来时整张消失（「不完整显示立绘，立绘就消失」）
    //   ② <img ypos=N> 被丢掉 -> 图层（画像枠/特效）落在立絵下面一行
    //      （「边框没有在立绘边缘」）
    qDebug() << "\n跨行图片：窗口裁剪 / ypos 图层叠加";
    {
        ConsoleBackend c;
        c.setFontSize(16);
        c.setLineHeight(16);
        c.setGridColumns(40);
        c.setGridRows(10);
        c.setVisibleCount(10);                     // 窗口 = 10 行

        auto imageBlock = [&c](const QString& name) -> QVariantMap {
            for (const QVariant& v : c.imageBlocks()) {
                const QVariantMap m = v.toMap();
                if (m.value("text").toString() == name) return m;
            }
            return QVariantMap();
        };

        for (int i = 0; i < 7; ++i) { c.print(QStringLiteral("T%1").arg(i)); c.newline(); }   // 0..6
        c.printImage(QStringLiteral("face"), 400, 400); c.newline();                          // 7
        for (int i = 0; i < 7; ++i) { c.print(QStringLiteral("M%1").arg(i)); c.newline(); }   // 8..14
        c.printImage(QStringLiteral("frame"), 400, 400, -800); c.newline();                   // 15
        for (int i = 0; i < 5; ++i) { c.print(QStringLiteral("B%1").arg(i)); c.newline(); }   // 16..20
        c.flush();
        // 400% * 16px = 64px = 4 行高；ypos=-800 -> -800*16/100 = -128px = -8 行
        check(c.lineCount() == 21, "21 行（图片各占 1 个逻辑行）");

        // followTail：窗口 = 第 11..20 行。立絵锚点在 7、占 7..10 行，
        // 框锚点在 15 但 ypos 把它拉回 7..10 行 —— 两张整块都在窗口上方。
        check(imageBlock(QStringLiteral("face")).isEmpty(),
              "整块在窗口上方 -> 不产出（不会凭空露出一角）");

        // 往上滚 3 行：窗口 = 第 8..17 行 -> 立絵还剩 8/9/10 三行可见。
        // 修复前只遍历窗口内的行，锚点行 7 不在窗口里 -> 整张立絵消失。
        c.scrollBy(3);
        check(c.scrollOffset() == 3, "scrollOffset == 3");
        const QVariantMap face = imageBlock(QStringLiteral("face"));
        check(!face.isEmpty(),
              "立絵从窗口上方探进来（还剩 3 行可见）-> 必须产出区块");
        check(face.value("row").toInt() == 7, "row == 7（**绝对行号**：锚点行 = 7）");
        check(c.windowFirstLine() == 8, "windowTopRow == 8（窗口顶行是第 8 行）");
        check(face.value("rows").toInt() == 4, "跨行：rows == 4（64px / 行高 16）");
        check(face.value("height").toInt() == 64, "跨行：height == rows * 行高 = 64px");
        check(face.value("offsetRows").toDouble() == 0.0, "立絵本身没有 ypos");

        const QVariantMap frame = imageBlock(QStringLiteral("frame"));
        check(!frame.isEmpty(), "ypos 图层：框也在窗口里");
        check(frame.value("rows").toInt() == 4, "框同样 4 行高");
        check(frame.value("row").toInt() == 15, "框 row == 15（绝对行号：锚点行 = 15）");
        check(qFuzzyCompare(frame.value("offsetRows").toDouble(), -8.0),
              "ypos=-800 -> offsetRows == -8（-128px / 行高 16）");
        check(face.value("row").toInt() + face.value("offsetRows").toDouble()
                  == frame.value("row").toInt() + frame.value("offsetRows").toDouble(),
              "框与立絵**同一纵坐标**（图层叠加对齐 -> 边框落在立绘边缘）");

        // 回到最新：两张图整块都在窗口上方 -> 都不产出
        c.scrollToBottom();
        check(imageBlock(QStringLiteral("face")).isEmpty()
                  && imageBlock(QStringLiteral("frame")).isEmpty(),
              "回到最新：整块在窗口上方的图不产出");

        // 滚到顶部：窗口 = 第 0..20 行 -> 两图重新进入窗口
        c.scrollBy(20);
        check(!imageBlock(QStringLiteral("face")).isEmpty()
                  && !imageBlock(QStringLiteral("frame")).isEmpty(),
              "滚到顶部：立絵与框重新进入窗口");
    }

    qDebug() << "\n===================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] console backend tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
