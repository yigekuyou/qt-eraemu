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
// 验证显示层（GameView/ConsoleBackend，本身即 QAbstractListModel）：
//   1. 输出写入（print/newline/printButton）与对齐
//   2. 行模型信号：未定型尾行 flush 后 insertRows，newline 摘尾行插已提交行
//   3. CLEARLINE / 容量裁剪 -> removeRows；rowCount 与缓冲恒一致
//   4. 按钮命中 -> 输入信号；generation 失效旧按钮
//   5. 跨行图片 / ypos 图层的区块数据（立絵行距、offsetRows 对齐）
//   6. 空输入语义（对齐 C# doInputToEmueraProgram）
//
// 滚动/窗口的簿记已移交 QML ListView（虚拟化 + reuseItems），不再在此测试。
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

static QString textOf(const QVariantList& blocks, const QString& name) {
    for (const QVariant& v : blocks) {
        const QVariantMap m = v.toMap();
        if (m.value("text").toString() == name) return m.value("text").toString();
    }
    return QString();
}

static QVariantMap blockOf(const QVariantList& blocks, const QString& name) {
    for (const QVariant& v : blocks) {
        const QVariantMap m = v.toMap();
        if (m.value("text").toString() == name) return m;
    }
    return QVariantMap();
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "ConsoleBackend test";
    qDebug() << "===================";

    // ---- 1. 输出写入 ----
    qDebug() << "\n1) 输出写入（print/newline）";
    {
        ConsoleBackend console;
        for (const QString& s : QStringList{"A", "B", "C", "D", "E"}) {
            console.print(s);
            console.newline();
        }
        check(console.lineCount() == 5, "lineCount == 5");
        check(console.rowCount() == 5, "rowCount == 5（已提交行全部入模）");
        check(console.lineText(0) == "A" && console.lineText(4) == "E",
              "缓冲行序 A..E");
    }

    // ---- 2. 行模型信号：未定型尾行 ----
    qDebug() << "\n2) 行模型信号（尾行 insert/dataChanged）";
    {
        ConsoleBackend c;
        int inserted = 0, removed = 0, dataChanges = 0;
        QObject::connect(&c, &QAbstractItemModel::rowsInserted,
                         [&](const QModelIndex&, int, int) { ++inserted; });
        QObject::connect(&c, &QAbstractItemModel::rowsRemoved,
                         [&](const QModelIndex&, int, int) { ++removed; });
        QObject::connect(&c, &QAbstractItemModel::dataChanged,
                         [&](const QModelIndex&, const QModelIndex&, const QList<int>&) { ++dataChanges; });

        c.print("A");
        check(inserted == 0, "print 不发信号（未定型行等帧发布）");
        c.flush();
        check(inserted == 1 && c.rowCount() == 1, "flush：尾行 insertRows 1 次");
        check(textOf(c.lineBlocks(0), "A") == "A", "尾行数据可读（A）");

        c.print("B");
        c.flush();
        check(dataChanges == 1 && inserted == 1 && removed == 0,
              "尾行内容增长只走 dataChanged（行数不变，不重建）");
        check(c.lineBlocks(0).size() >= 2, "尾行摊平含 A、B 两个区块");

        c.newline();
        check(removed == 1 && inserted == 2 && c.rowCount() == 1,
              "newline：尾行摘除 + 已提交行插入（rowCount 不变）");

        for (int i = 0; i < 100; ++i) { c.print("batch"); c.newline(); }
        check(c.rowCount() == c.lineCount(), "rowCount 与缓冲行数恒一致");
        check(textOf(c.lineBlocks(c.rowCount() - 1), "batch") == "batch",
              "最后一行数据可读");
    }

    // ---- 3. CLEARLINE / 容量裁剪 -> removeRows ----
    qDebug() << "\n3) CLEARLINE 与容量裁剪";
    {
        ConsoleBackend c;
        c.print("A"); c.newline();
        c.print("B"); c.newline();
        c.print("C"); c.newline();
        c.flush();
        check(c.lineCount() == 3 && c.logicalLineCount() == 3, "3 行，LINECOUNT == 3");

        int removed = 0, first = -1, last = -1;
        QObject::connect(&c, &QAbstractItemModel::rowsRemoved,
                         [&](const QModelIndex&, int f, int l) {
                             ++removed; first = f; last = l;
                         });
        c.clearLines(2);
        check(removed == 1 && first == 1 && last == 2, "CLEARLINE 2 -> 尾部区间 removeRows(1..2)");
        check(c.lineCount() == 1 && c.lineText(0) == "A", "只剩 A");
        check(c.logicalLineCount() == 1, "CLEARLINE 后 LINECOUNT == 1");
    }
    {
        ConsoleBackend c;
        c.setMaxLog(24);   // 小容量：min(kTrimBatch, cap) = 24，溢出即裁
        int removedBatches = 0;
        QObject::connect(&c, &QAbstractItemModel::rowsRemoved,
                         [&](const QModelIndex&, int, int) { ++removedBatches; });
        for (int i = 0; i < 100; ++i) { c.print(QStringLiteral("L%1").arg(i)); c.newline(); }
        check(c.lineCount() == 24, "容量 24 -> lineCount 恒为 24");
        check(c.rowCount() == c.lineCount(), "rowCount 与缓冲一致（头部裁剪有信号）");
        check(removedBatches > 0, "头部裁剪发出 removeRows");
        check(c.lineText(0) == QStringLiteral("L76"), "最旧保留 L76（丢最旧 76 行）");
    }

    // ---- 4. 按钮与输入；generation 失效 ----
    qDebug() << "\n4) 按钮与输入";
    {
        ConsoleBackend console;
        int submitted = -1;
        QObject::connect(&console, &ConsoleBackend::inputSubmitted,
                         [&submitted](qint64 v) { submitted = static_cast<int>(v); });

        console.clearAll();
        console.notifyInputRequested("INPUT");   // 输入裁决：等待整数型输入后按钮才可提交
        console.printButton("[1] 选择一", 1);
        console.printButton("[2] 选择二", 2);
        console.newline();

        // 两个按钮在同一行；点击第 2 个
        console.clickAt(console.lineCount() - 1, 1);
        check(submitted == 2, "clickAt(button#1) -> inputSubmitted(2)");

        // 提交后同一批按钮失效（对齐 C# generation）
        submitted = -1;
        console.clickAt(console.lineCount() - 1, 0);
        check(submitted == -1, "stale button after submit ignored");

        // 新一批按钮可点击
        console.printButton("[3] 新选择", 3);
        console.newline();
        console.clickAt(console.lineCount() - 1, 0);
        check(submitted == 3, "new generation button clickable");
    }
    {
        ConsoleBackend c;
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

    // ---- 菜单项 `[n]` 自动变按钮（对齐 C# ButtonStringCreator）----
    qDebug() << "\n菜单按钮";
    {
        ConsoleBackend c;
        c.print(QString::fromUtf8("[0] 开始游戏"));
        c.newline();
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
        check(c2.buffer().at(0).segments.size() == 1
                  && c2.buffer().at(0).segments.first().isButton
                  && c2.buffer().at(0).segments.first().intValue == 9999, "[9999] -> 9999");

        // 非数字 `[abc]`：不是核 → 整行不可点击
        ConsoleBackend c3;
        c3.print(QString::fromUtf8("[abc] 不是数字"));
        c3.newline();
        check(!c3.buffer().at(0).segments.isEmpty()
                  && !c3.buffer().at(0).segments.first().isButton, "[abc] 不建按钮");

        // 十六进制 / 指数 / 带符号（C# 正则允许）
        ConsoleBackend c4;
        c4.print(QString::fromUtf8("[0x10] 十六进制"));
        c4.newline();
        check(c4.buffer().at(0).segments.first().isButton
                  && c4.buffer().at(0).segments.first().intValue == 16,
              "[0x10] -> 16（C# 正则支持 0x 前缀）");
    }

    qDebug() << "\n多个按钮切段";
    {
        ConsoleBackend c;
        c.print(QString::fromUtf8("[1] 选择一 [2] 选择二"));
        c.newline();
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

    // ---- 5. 跨行图片 / ypos 图层的区块数据 ----
    // 立絵跨多行（rows）、<img ypos=N> 折算成 offsetRows（负 = 往上盖）：
    // eraTW 的画像枠/特效靠它与立絵同坐标叠加。可见性（谁在视口里）由
    // QML ListView 的虚拟化 + cacheBuffer 决定，C++ 数据永远完整产出。
    qDebug() << "\n跨行图片：行距与 ypos 图层叠加";
    {
        ConsoleBackend c;
        c.setFontSize(16);
        c.setLineHeight(16);
        c.setGridColumns(40);
        c.setGridRows(10);

        for (int i = 0; i < 7; ++i) { c.print(QStringLiteral("T%1").arg(i)); c.newline(); }   // 0..6
        c.printImage(QStringLiteral("face"), 400, 400); c.newline();                          // 7
        for (int i = 0; i < 7; ++i) { c.print(QStringLiteral("M%1").arg(i)); c.newline(); }   // 8..14
        c.printImage(QStringLiteral("frame"), 400, 400, -800); c.newline();                   // 15
        for (int i = 0; i < 5; ++i) { c.print(QStringLiteral("B%1").arg(i)); c.newline(); }   // 16..20
        // 400% * 16px = 64px = 4 行高；ypos=-800 -> -800*16/100 = -128px = -8 行
        check(c.lineCount() == 21, "21 行（图片各占 1 个逻辑行）");
        check(c.maxSpanReach() >= 12, "maxSpanReach >= 12（立絵 4 行 + 框上探 8 行，QML cacheBuffer 依据）");

        const QVariantMap face = blockOf(c.lineBlocks(7), "face");
        check(!face.isEmpty(), "第 7 行含立絵区块");
        check(face.value("rows").toInt() == 4, "跨行：rows == 4（64px / 行高 16）");
        check(face.value("height").toInt() == 64, "跨行：height == rows * 行高 = 64px");
        check(face.value("offsetRows").toDouble() == 0.0, "立絵本身没有 ypos");
        check(face.value("lineIndex").toInt() == 7, "lineIndex == 7（点击回传行号）");

        const QVariantMap frame = blockOf(c.lineBlocks(15), "frame");
        check(!frame.isEmpty(), "第 15 行含框图层区块");
        check(frame.value("rows").toInt() == 4, "框同样 4 行高");
        check(qFuzzyCompare(frame.value("offsetRows").toDouble(), -8.0),
              "ypos=-800 -> offsetRows == -8（-128px / 行高 16）");
        check(face.value("row").toInt() + face.value("offsetRows").toDouble()
                  == frame.value("row").toInt() + frame.value("offsetRows").toDouble(),
              "框与立絵**同一纵坐标**（图层叠加对齐 -> 边框落在立绘边缘）");
    }

    // ---- 6. 空输入语义（对齐 C# doInputToEmueraProgram）----
    qDebug() << "\n空输入语义";
    {
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
