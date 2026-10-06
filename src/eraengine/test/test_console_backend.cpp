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

    // ---- 增量模型：CLEARLINE 后**复用同一批绝对行号**重打印 ----
    // eraTW 的标题/指令循环大量这么干（TITLE.ERB: `CLEARLINE LINECOUNT - LOCAL:2`
    // 后重印整屏）。回归：模型此前只比「绝对行号是否还在窗口里」——行号被复用
    // 时不会产生任何模型信号，点按钮后画面不刷新、按钮因世代过期而失效、选项
    // 不变。修复：按**行内容版本**判定，版本变了就重建模型行。
    qDebug() << "\n增量模型：CLEARLINE 重印复用行号";
    {
        ConsoleBackend c;
        c.setVisibleCount(10);
        const auto texts = [&c]() {
            QStringList out;
            for (const QVariant& v : c.visibleBlocks())
                out << v.toMap().value(QStringLiteral("text")).toString();
            return out;
        };

        c.print("OLD1"); c.newline();                 // abs 0（不动）
        c.print("OLD2"); c.newline();                 // abs 1（将被复用）
        c.notifyInputRequested("INPUT");
        c.printButton(QString::fromUtf8("[1] 旧选项"), 1);   // abs 2（将被复用）
        c.newline();
        c.flush();
        const QStringList before = texts();
        check(before.contains("OLD1") && before.contains("OLD2")
                  && before.contains(QString::fromUtf8("[1] 旧选项")),
              "初始：模型含 OLD1/OLD2/旧选项");

        // CLEARLINE 2（删 abs 1、2）-> 重打印 -> 复用 abs 1、2
        // Qt 文档（QAbstractItemModel::dataChanged）：现有条目的数据变化必须走
        // dataChanged(起始行, 终止行) 原地刷新，不得 removeRows+insertRows ——
        // 否则委托被销毁重建、count 变化还会扰动滚动（「刷一次把历史吃了」）。
        c.resetPerfCounters();
        c.clearLines(2);
        c.print("NEW2"); c.newline();                        // 复用 abs 1
        c.notifyInputRequested("INPUT");
        c.printButton(QString::fromUtf8("[1] 新选项"), 2);   // 复用 abs 2
        c.newline();
        c.flush();

        const QStringList after = texts();
        check(c.lineCount() == 3, "重印后仍 3 行（行号被复用）");
        check(after.contains("NEW2"), "重印后模型含 NEW2（行号复用必须刷新）");
        check(!after.contains("OLD2"), "重印后模型不含 OLD2（旧区块被替换）");
        check(after.contains(QString::fromUtf8("[1] 新选项")), "重印后模型含新按钮文本");
        check(!after.contains(QString::fromUtf8("旧选项")), "重印后模型不含旧按钮文本");

        // 「不销毁历史」的硬约束：本轮只允许 dataChanged，行数与既有模型行不动。
        const auto* model = qobject_cast<const ConsoleBlockModel*>(c.blockModel());
        // NEW2（abs 1）行号复用且行程未变 -> 必须原地 dataChanged，绝不能摘了重插
        check(model && model->updatedRows() >= 1,
              "行号复用的行原地 dataChanged（updatedRows >= 1，不摘除重建）");
        check(model && model->removedRows() <= 1 && model->insertedRows() <= 1,
              "摘除/插入只发生在确实消失过的行（中间态 abs2，历史行不受扰动）");

        // 复用行号上的新按钮仍可点击（世代已随新一批打印刷新）
        int submitted = -1;
        QObject::connect(&c, &ConsoleBackend::inputSubmitted,
                         [&submitted](qint64 v) { submitted = static_cast<int>(v); });
        c.clickAt(2, 0);
        check(submitted == 2, "复用行号上的新按钮可点击 -> inputSubmitted(2)");
    }

    // ---- 增量模型：整表重摊平（字号变化）也绝不能拆历史 ----
    // Qt 文档：现有条目数据变化走 dataChanged(起始行, 终止行)；这里构造
    // 「每行内容都变、行程不变」的最干净场景，锁死 insert/remove 必须为 0。
    qDebug() << "\n增量模型：整表重摊平也不拆历史（纯 dataChanged）";
    {
        ConsoleBackend c;
        c.setVisibleCount(10);
        c.print("L0"); c.newline();
        c.print("L1"); c.newline();
        c.print("L2"); c.newline();
        c.flush();
        const auto texts = [&c]() {
            QStringList out;
            for (const QVariant& v : c.visibleBlocks())
                out << v.toMap().value(QStringLiteral("text")).toString();
            return out;
        };
        const QStringList before = texts();
        check(before.contains("L0") && before.contains("L1") && before.contains("L2"),
              "初始：模型含 L0/L1/L2");

        // 字号变化 -> 按行缓存整体作废 -> 每行版本都变、行程不变
        c.resetPerfCounters();
        c.setFontSize(qMax(1, c.fontSize() - 1));
        c.flush();
        const auto* model = qobject_cast<const ConsoleBlockModel*>(c.blockModel());
        check(model && model->insertedRows() == 0 && model->removedRows() == 0,
              "整表重摊平：insert/remove 为 0（无任何委托被拆）");
        check(model && model->updatedRows() >= 3,
              "整表重摊平：全部行走 dataChanged（updatedRows >= 3）");
        const QStringList after = texts();
        check(after == before, "重摊平后内容不变（L0/L1/L2 原样）");
    }

    // ---- 增量模型：容量裁剪使绝对行号整体前移 -> 按行缓存必须失效 ----
    qDebug() << "\n增量模型：容量裁剪（行号前移）";
    {
        ConsoleBackend c;
        c.setVisibleCount(3);
        c.buffer().setCapacity(3);
        const auto texts = [&c]() {
            QStringList out;
            for (const QVariant& v : c.visibleBlocks())
                out << v.toMap().value(QStringLiteral("text")).toString();
            return out;
        };
        c.print("L0"); c.newline();
        c.print("L1"); c.newline();
        c.print("L2"); c.newline();
        c.flush();
        check(texts() == QStringList{"L0", "L1", "L2"}, "裁剪前：模型 = L0/L1/L2");

        c.print("L3"); c.newline();   // 超容量 -> 丢 L0，绝对行号前移
        c.flush();
        const QStringList t = texts();
        check(t == QStringList{"L1", "L2", "L3"},
              "裁剪后：模型随行号前移刷新为 L1/L2/L3（不返回陈旧区块）");
    }

    // ---- 增量模型：未提交行（尾行）随内容增长重建 ----
    qDebug() << "\n增量模型：未提交行随内容更新";
    {
        ConsoleBackend c;
        c.setVisibleCount(10);
        const auto texts = [&c]() {
            QStringList out;
            for (const QVariant& v : c.visibleBlocks())
                out << v.toMap().value(QStringLiteral("text")).toString();
            return out;
        };
        c.print("AAA"); c.flush();
        check(texts().contains("AAA"), "尾行 AAA 出现");
        c.print("BBB"); c.flush();    // 同一结果行继续追加
        const QStringList t = texts();
        check(t.contains("AAA") && t.contains("BBB"),
              "尾行内容增长 -> 新区块 BBB 进入模型");
    }

    qDebug() << "\n空输入语义（对齐 C# doInputToEmueraProgram）";
    {
        // 以前 QML 交的是 `parseInt(text) || 0`：空回车变成 RESULT=0。
        // eraTW 的外出列表用无参 `INPUT`，0 恰好等于 MAIN_MAP -> 走
        // «从外面回家» 分支，表现就是「还没操作就自动返回了」。
        // 现在：空 && 有缺省 -> 交缺省；空 && 无缺省 -> 忽略；非法 -> 忽略。
        ConsoleBackend c;
        qint64 got = -999;
        QString gotStr = QStringLiteral("<none>");
        int intCount = 0, strCount = 0;
        QObject::connect(&c, &ConsoleBackend::inputSubmitted,
                         [&](qint64 v) { got = v; ++intCount; });
        QObject::connect(&c, &ConsoleBackend::inputSubmittedString,
                         [&](const QString& v) { gotStr = v; ++strCount; });

        // ① 无参 INPUT：空回车必须**什么都不发生**
        c.notifyInputRequested(QStringLiteral("INPUT"));
        c.submitIntegerText(QString());
        check(intCount == 0, "无缺省值的 INPUT：空回车被忽略（不交付）");

        // ② 非法文本同样忽略
        c.submitIntegerText(QStringLiteral("abc"));
        check(intCount == 0, "非整数文本被忽略（C# 的 Int64.TryParse 失败）");

        // ③ 正常整数照常交付
        c.submitIntegerText(QStringLiteral("7"));
        check(intCount == 1 && got == 7, "正常整数 7 照常交付");

        // ④ TINPUT 带缺省：空回车交缺省值（不是 0）
        c.notifyInputRequested(QStringLiteral("TINPUT"), QVariant(qint64(1234)));
        c.submitIntegerText(QString());
        check(intCount == 2 && got == 1234, "TINPUT 缺省 1234：空回车交 1234");

        // ⑤ INPUT 带实参：空回车交该缺省值
        c.notifyInputRequested(QStringLiteral("INPUT"), QVariant(qint64(5)));
        c.submitIntegerText(QString());
        check(intCount == 3 && got == 5, "INPUT 5：空回车交 5");

        // ⑥ 字符串型：空回车同样按缺省值
        c.notifyInputRequested(QStringLiteral("TINPUTS"), QVariant(QStringLiteral("-1")));
        c.submitStringText(QString());
        check(strCount == 1 && gotStr == QLatin1String("-1"),
              "TINPUTS 缺省 \"-1\"：空回车交 \"-1\"");

        // ⑦ kind 必须是输入类型而不是系统状态名（QML 提示文本与校验路由都用它）
        check(c.inputKind() == QLatin1String("TINPUTS") && c.waitingInput(),
              "inputKind 是输入类型（TINPUTS），不是系统状态名");
    }

    qDebug() << "\n===================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] console backend tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
