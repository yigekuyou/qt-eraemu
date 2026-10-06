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
// test_console_plane —— 「最小单位区块 + 相对网格坐标 + 二维平面」的回归
//
//   1) ButtonStringCreator：一行文本 → 若干段（`[n]` 的识别规则）
//   2) ConsoleLayout      ：单位换算与动态测量（cols/rows）
//   3) ConsolePlane       ：分层区块 → 二维字符平面（列/行都是「单位」）
//   4) ConsolePlane::inspect：几何自检（越界/重叠/尺寸缺失）
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDebug>
#include <QString>

#include "button_string_creator.h"
#include "console_backend.h"
#include "console_buffer.h"
#include "console_plane.h"

static int g_failures = 0;

static void check(bool ok, const QString& what) {
    if (ok) {
        qDebug().noquote() << "  [ok ]" << what;
    } else {
        ++g_failures;
        qDebug().noquote() << "  [FAIL]" << what;
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // -----------------------------------------------------------------------
    qDebug() << "\n1) ButtonStringCreator（一行 → 若干段）";
    {
        // 只有一个核：整行是一段且可点击（C# buttonCount <= 1 的行为）
        const QList<ButtonPrimitive> one = ButtonStringCreator::split(QString::fromUtf8("[0] 开始游戏"));
        check(one.size() == 1, "单个核 -> 1 段");
        check(one.first().canSelect && one.first().input == 0, "整段可点击，值 = 0");

        // 多个核：切成多段，文本总量不变
        const QList<ButtonPrimitive> many =
            ButtonStringCreator::split(QString::fromUtf8("[1] 甲 [2] 乙"));
        int buttons = 0;
        QString joined;
        for (const ButtonPrimitive& b : many) {
            joined += b.str;
            if (b.canSelect) ++buttons;
        }
        check(buttons == 2, "两个核 -> 两段可点击");
        check(joined == QString::fromUtf8("[1] 甲 [2] 乙"), "切段不改变字符总数");

        // C# 正则允许 0x / 0b / 正负号 / 指数 / 括号内空白
        check(ButtonStringCreator::isButtonCore(QStringLiteral("[0x10]")), "[0x10] 是按钮核");
        check(ButtonStringCreator::isButtonCore(QStringLiteral("[ 123 ]")), "[ 123 ] 是按钮核");
        // 正则接受指数，但整数解析不接受 —— 所以 [1e2] 过正则却仍不是按钮核
        // （与 C# 一致：numReg 通过后还要 LexicalAnalyzer.ReadInt64）
        check(ButtonStringCreator::isNumericBracket(QStringLiteral("[1e2]")), "[1e2] 过数字正则");
        check(!ButtonStringCreator::isButtonCore(QStringLiteral("[1e2]")),
              "[1e2] 整数解析失败 -> 不是按钮核");
        qint64 v = 0;
        check(ButtonStringCreator::isButtonCore(QStringLiteral("[0x10]"), &v), "hex button token recognized");
        check(v == 16, "[0x10] -> 16");
        check(!ButtonStringCreator::isButtonCore(QStringLiteral("[abc]")), "[abc] 不是按钮核");
        check(!ButtonStringCreator::isButtonCore(QStringLiteral("[]")), "[] 不是按钮核");

        // 不配对的括号：整行退化成一段不可点击
        const QList<ButtonPrimitive> bad = ButtonStringCreator::split(QStringLiteral("[1 没有闭括号"));
        check(bad.size() == 1 && !bad.first().canSelect, "括号不配对 -> 整行不可点击");
    }

    // -----------------------------------------------------------------------
    qDebug() << "\n2) 单位换算与动态测量";
    {
        ConsoleLayout layout;
        layout.setFontSize(18);
        layout.setLineHeight(19);
        layout.setGridColumns(84);            // 760px ÷ 9px ≈ 84 列（列数只由配置网格决定）
        check(layout.columnWidthPx() == 9, "列宽 = FontSize/2 = 9px");
        check(layout.maxCols() == 84, "网格 84 列");
        check(layout.measureUnits(QString::fromUtf8("あ")) == 2, "全角 1 字 = 2 单位");
        check(layout.measureUnits(QStringLiteral("ab")) == 2, "半角 2 字 = 2 单位");

        ConsoleSpan text;
        text.kind = ConsoleSpanKind::Text;
        text.text = QString::fromUtf8("あい");
        layout.measurePart(text);
        check(text.cols == 4 && text.rows == 1, "「あい」-> cols=4 rows=1（动态测量）");

        ConsoleSpan img;
        img.kind = ConsoleSpanKind::Image;
        img.text = QStringLiteral("face");
        img.imageSize = QSizeF(100, 100);     // 百分比 of FontSize
        layout.measurePart(img);
        check(img.cols == 2, "图片 100% FontSize -> 2 单位长");
    }

    // -----------------------------------------------------------------------
    qDebug() << "\n3) 平面重建（分层区块 → 二维字符平面）";
    {
        ConsoleBackend c;
        c.setFontSize(18);
        c.setLineHeight(19);
        c.setGridColumns(84);                 // 760px ÷ 9px ≈ 84 列

        // 第 1 行：文本 + 5 个全角空格 + 文本（用空格把第 2 个区块推到第 11 列）
        c.print(QStringLiteral("A"));
        c.print(QString::fromUtf8("　　　"));
        c.print(QStringLiteral("B"));
        c.newline();
        // 第 2 行：图片
        c.printImage("face_01", 100, 100);
        c.newline();

        const ConsoleDisplayLine& l0 = c.buffer().at(0);
        const QList<ConsoleSpan> parts = l0.flatSpans();
        check(parts.size() == 3, "each print operation remains a distinct span");

        ConsolePlaneOptions opt;
        opt.windowWidth = c.gridColumns() * c.columnWidth();
        opt.terminalSafe = false;           // 这里要看原始占位符
        const QStringList plane = ConsolePlane::render(c.buffer(), c.layout(), opt);
        check(plane.size() == 2, "2 行 -> 平面 2 行");
        if (plane.size() == 2) {
            // "A" + 5 全角空格 + "B" = 1 + 10 + 1 = 12 列
            check(plane.at(0) == QStringLiteral("A") + QString::fromUtf8("　　　")
                                      + QStringLiteral("B"),
                  QStringLiteral("平面第 1 行 = A+全角空格+B"));
            check(plane.at(1).startsWith(QString(QChar(0x25A8))), "平面第 2 行是图片占位符 ▨");
            // 终端安全模式：歧义宽度符号 → 2 个 ASCII 字符（保证终端里 2 格）
            ConsolePlaneOptions safe = opt;
            safe.terminalSafe = true;
            const QStringList safePlane = ConsolePlane::render(c.buffer(), c.layout(), safe);
            check(safePlane.size() == 2 && safePlane.at(1).startsWith(QStringLiteral("@@")),
                  "终端安全模式：▨ -> @@（宽度恒为 2 格）");
            check(ConsolePlane::toTerminalSafe(QString(QChar(0x25A1))) == QStringLiteral(".."),
                  "□ -> ..（空心格）");
            check(ConsolePlane::toTerminalSafe(QString(QChar(0x25A0))) == QStringLiteral("##"),
                  "■ -> ##（实心块）");
            check(ConsolePlane::isWideChar(QChar(0x25A1)) && ConsolePlane::isAmbiguousWidth(QChar(0x25A1)),
                  "□ 是「宽且有歧义」的字符");
        }

        const QList<ConsolePlaneIssue> issues =
            ConsolePlane::inspect(c.buffer(), c.layout(), opt);
        check(issues.isEmpty(), QStringLiteral("几何自检无问题（%1 项）").arg(issues.size()));
    }

    // -----------------------------------------------------------------------
    qDebug() << "\n4) 绝对列与占位符宽度";
    {
        ConsoleBuffer buffer;
        ConsoleLayout layout;
        layout.setFontSize(18);
        layout.setGridColumns(84);

        ConsoleDisplayLine line;
        ConsoleSegment left;
        ConsoleSpan wide;
        wide.kind = ConsoleSpanKind::Text;
        wide.text = QString(QChar(0x25A0));
        wide.cols = 2;
        left.spans.append(wide);

        ConsoleSegment right;
        right.relCol = 6;
        ConsoleSpan label;
        label.kind = ConsoleSpanKind::Text;
        label.text = QStringLiteral("X");
        label.cols = 1;
        label.relCol = 6;
        label.pointXLocked = true;
        right.spans.append(label);
        line.segments = {left, right};
        buffer.appendLine(line);

        ConsolePlaneOptions opt;
        opt.windowWidth = 760;
        opt.terminalSafe = true;
        opt.withWidths = true;
        const QStringList plane = ConsolePlane::render(buffer, layout, opt);
        check(plane.size() == 1 && plane.first() == QStringLiteral("##    X  |7"),
              "绝对列 6 的区块保持在第 7 列，替换字符不推动位置");

        ConsolePlaneOptions debug = opt;
        debug.terminalSafe = false;
        debug.debugCompare = true;
        const QStringList debugPlane = ConsolePlane::render(buffer, layout, debug);
        check(debugPlane.size() == 1 && debugPlane.first() == QStringLiteral("##    X  |7"),
              "调试占位符保持 2 列宽");

        buffer.lastMutable().segments[0].spans[0].style.color = QColor("#ff1abd");
        ConsolePlaneOptions ansi = opt;
        ansi.ansiColors = true;
        check(ConsolePlane::render(buffer, layout, ansi).first()
                  == QStringLiteral("\033[38;2;255;26;189m##\033[39m    X  |7"),
              "真实颜色输出不改变后续区块列号或行宽");
        check(ConsolePlane::render(buffer, layout, opt).first() == plane.first(),
              "关闭 ANSI 时仍输出纯文本");

        ConsoleBuffer imageBuffer;
        ConsoleDisplayLine imageLine;
        ConsoleSegment imageSegment;
        ConsoleSpan image;
        image.kind = ConsoleSpanKind::Image;
        image.cols = 4;
        imageSegment.spans.append(image);
        imageLine.segments.append(imageSegment);
        imageBuffer.appendLine(imageLine);
        const QStringList imagePlane = ConsolePlane::render(imageBuffer, layout, opt);
        check(imagePlane.size() == 1 && imagePlane.first() == QStringLiteral("@@@@  |4"),
              "4 列图片只占 4 列，不重复放大占位符");
    }

    // -----------------------------------------------------------------------
    qDebug() << "\n5) 平面自检能抓到越界 / 重叠";
    {
        ConsoleBackend c;
        c.setFontSize(18);
        c.setLineHeight(19);
        c.setGridColumns(10);                 // 只有 10 列

        c.print(QStringLiteral("0123456789ABCDEF"));   // 16 列 > 10 列
        c.newline();

        ConsolePlaneOptions opt;
        opt.windowWidth = c.gridColumns() * c.columnWidth();
        const QList<ConsolePlaneIssue> issues =
            ConsolePlane::inspect(c.buffer(), c.layout(), opt);
        check(!issues.isEmpty(), "越界被检出");
        bool overflow = false;
        for (const ConsolePlaneIssue& x : issues) {
            if (x.message.contains(QString::fromUtf8("越出"))) overflow = true;
        }
        check(overflow, "报出了「越出 root 宽度」");
    }

    qDebug() << "\n===================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] console plane tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
