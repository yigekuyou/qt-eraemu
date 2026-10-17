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
// test_g_drawing.cpp —— G 系绘图的**像素级**回归（GDRAWTEXT / GDRAWLINE /
// GDASHSTYLE / GDRAWGWITHROTATE / GGET* / GGETTEXTSIZE）。
//
// 「画布」不是新后端：GraphicsStore 持有的 QImage 就是画布（QPainter 直接画），
// 显示走既有链路（SPRITECREATE -> ResourceImageProvider / CBG 层）。本测试只对
// **存储里的像素**做断言，与显示层无关 —— 无头环境（QT_QPA_PLATFORM=offscreen）可跑。
//
// C# 权威：emuera.em Creator.Method.cs 的 GraphicsDrawStringMethod（:5568）/
// GraphicsDrawLineMethod（:5807）/ GraphicsSetDashStyleMethod（:5549）/
// GraphicsDrawGWithRotateMethod（:5759）/ GraphicsStateMethod（:5305）/
// GraphicsGetTextSizeMethod（:5642）；UI/Game/Image/GraphicsImage.cs 的 GDrawString/GDrawLine。
// 语义文档：test/data/commands/GDRAWTEXT.md / GDRAWLINE.md / GDASHSTYLE.md。
// ---------------------------------------------------------------------------
#include <QColor>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QFontMetrics>
#include <QImage>
#include <QStringList>

#include "GameView/graphics_store.h"

static int g_failed = 0;
static void check(bool ok, const QString& what) {
    if (ok) qDebug() << "  [PASS]" << what;
    else { ++g_failed; qDebug() << "  [FAIL]" << what; }
}

int main(int argc, char* argv[]) {
    // 字体度量需要 QGuiApplication（QCoreApplication 下 QFontDatabase 不可用）；
    // 离屏平台让 ctest / 无显示环境都能跑（对齐 test_qml_console 的运行方式）。
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    GraphicsStore::clearAll();

    qDebug() << "G 系绘图（像素级）";

    // ---- GDRAWLINE：画笔画线（像素可见 + 端点/中点采样）----
    {
        check(GraphicsStore::gCreate(1, 32, 32), "GCREATE 1");
        GraphicsStore::gClear(1, QColor(0, 0, 0));
        check(GraphicsStore::gSetPen(1, QColor(0, 255, 0), 1), "GSETPEN（绿色、宽 1）");
        check(GraphicsStore::gDrawLine(1, 0, 16, 31, 16, QColor(255, 255, 255)),
              "GDRAWLINE 返回成功");
        const QImage img = GraphicsStore::gImage(1);
        check(img.pixelColor(0, 16) == QColor(0, 255, 0)
              && img.pixelColor(16, 16) == QColor(0, 255, 0)
              && img.pixelColor(31, 16) == QColor(0, 255, 0),
              "GDRAWLINE 的水平线两端与中点是画笔色");
        check(img.pixelColor(16, 0) == QColor(0, 0, 0), "线外像素未被改动");
        // 未 GSETPEN 的 G：兜底色可画（C# Config.ForeColor -> 这里由调用方传入）
        GraphicsStore::gCreate(2, 8, 8);
        GraphicsStore::gClear(2, QColor(0, 0, 0));
        check(GraphicsStore::gDrawLine(2, 0, 4, 7, 4, QColor(255, 0, 0)), "未设画笔也可画（兜底色）");
        check(GraphicsStore::gImage(2).pixelColor(4, 4) == QColor(255, 0, 0),
              "兜底色的线已落像素");
        // GGETPEN / GGETPENWIDTH 回读
        GraphicsStore::PenState ps;
        check(GraphicsStore::penState(1, ps) && ps.valid
              && ps.color == QColor(0, 255, 0) && ps.width == 1,
              "GGETPEN/GGETPENWIDTH 语义（penState 回读）");
    }

    // ---- GDASHSTYLE：虚线样式改变的是「画笔画出的图案」----
    {
        GraphicsStore::gCreate(3, 64, 4);
        GraphicsStore::gClear(3, QColor(0, 0, 0));
        GraphicsStore::gSetPen(3, QColor(255, 255, 255), 1);
        GraphicsStore::gDashStyle(3, 2, 0);       // Dot 虚线
        check(GraphicsStore::gDrawLine(3, 0, 2, 63, 2, QColor(255, 255, 255)),
              "GDASHSTYLE + GDRAWLINE 成功");
        int painted = 0;
        const QImage img = GraphicsStore::gImage(3);
        for (int x = 0; x < img.width(); ++x)
            if (img.pixelColor(x, 2) == QColor(255, 255, 255)) ++painted;
        // 虚线必然「有空洞」：64 像素里实线全亮，Dot 虚线亮段远小于 64
        check(painted > 0 && painted < img.width(),
              QString("GDASHSTYLE(Dot) 画的是虚线（亮 %1/64 像素）").arg(painted));
    }

    // ---- GDRAWTEXT：文本落像素 + 测量写 RESULT 语义 + GGETFONT 族回读 ----
    {
        GraphicsStore::gCreate(10, 256, 64);
        GraphicsStore::gClear(10, QColor(0, 0, 0));
        check(GraphicsStore::gSetFont(10, "sans-serif", 32, 1 /*粗体*/), "GSETFONT（32px 粗体）");
        GraphicsStore::gSetBrush(10, QColor(255, 255, 255));
        int w = -1, h = -1;
        check(GraphicsStore::gDrawString(10, "Test", 0, 0, GraphicsStore::makeFont("sans-serif", 100),
                                          QColor(255, 255, 255), &w, &h),
              "GDRAWTEXT 返回成功");
        int painted = 0;
        const QImage img = GraphicsStore::gImage(10);
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (img.pixelColor(x, y) != QColor(0, 0, 0)) ++painted;
        check(painted > 20, QString("GDRAWTEXT 画出可见像素（%1 个）").arg(painted));
        check(w > 0 && h > 0, QString("GDRAWTEXT 的测量（宽 %1 高 %2）非零").arg(w).arg(h));
        check(w < 256 && h <= 64, "测量值在画布量级内（像素单位）");
        // 未设置画刷/字体时用兜底：另开一个 G 只给兜底，也能画出像素
        GraphicsStore::gCreate(11, 128, 64);
        GraphicsStore::gClear(11, QColor(0, 0, 0));
        int w2 = 0, h2 = 0;
        check(GraphicsStore::gDrawString(11, "X", 0, 0, GraphicsStore::makeFont("sans-serif", 24),
                                          QColor(255, 0, 0), &w2, &h2)
              && GraphicsStore::gImage(11).pixelColor(1, 12) != QColor(0, 0, 0),
              "未 GSETFONT/GSETBRUSH 时按兜底字体/色画（C# Config.FontName@100 / ForeColor）");
        // GGETFONT / GGETFONTSIZE / GGETFONTSTYLE 回读（GSETFONT 的第 4 参）
        GraphicsStore::FontState fs;
        check(GraphicsStore::fontState(10, fs) && fs.valid
              && fs.name == "sans-serif" && fs.size == 32 && fs.style == 1,
              "GGETFONT/GGETFONTSIZE/GGETFONTSTYLE 语义（fontState 回读）");
        GraphicsStore::FontState unset;
        (void)GraphicsStore::fontState(11, unset);
        check(!unset.valid, "未 GSETFONT 的 G：GGETFONT 返回空（fontState 无效）");
        // GGETBRUSH 回读
        check(GraphicsStore::brushState(10) == QColor(255, 255, 255), "GGETBRUSH 回读画刷色");
    }

    // ---- SPRITEDISPOSEALL：清运行期精灵（C# AppContents.SpriteDisposeAll）----
    {
        GraphicsStore::gCreate(30, 4, 4);
        GraphicsStore::gClear(30, QColor(255, 0, 0));
        check(GraphicsStore::spriteCreate("s1", 30) && GraphicsStore::spriteCreate("s2", 30),
              "建两个运行期精灵");
        check(GraphicsStore::spriteDisposeAll(/*delCsvImage=*/false) == 2,
              "SPRITEDISPOSEALL 0 返回运行期精灵数");
        check(!GraphicsStore::spriteCreated("s1") && !GraphicsStore::spriteCreated("s2"),
              "运行期精灵已全部丢弃");
        check(GraphicsStore::spriteDisposeAll(false) == 0, "再次调用返回 0（幂等）");
    }

    // ---- GGETTEXTSIZE：宽随字号单调增；RESULT:1 = 高 ----
    {
        const QSize small = GraphicsStore::measureText("Test", "sans-serif", 10, 0);
        const QSize large = GraphicsStore::measureText("Test", "sans-serif", 40, 0);
        check(small.width() > 0 && small.height() > 0, "GGETTEXTSIZE 非零");
        check(large.width() > small.width() && large.height() > small.height(),
              QString("GGETTEXTSIZE 随字号单调（%1x%2 -> %3x%4）")
                  .arg(small.width()).arg(small.height()).arg(large.width()).arg(large.height()));
        const QSize bold = GraphicsStore::measureText("Test", "sans-serif", 32, 1);
        const QSize plain = GraphicsStore::measureText("Test", "sans-serif", 32, 0);
        check(bold.width() >= plain.width(), "GGETTEXTSIZE 的样式位（粗体不窄于常规）");
    }

    // ---- GDRAWGWITHROTATE：绕轴心旋转（用单像素源图做**精确落点**断言）----
    {
        // src 6x6：只在 (1,1) 放红像素（覆盖 [1,2)x[1,2)，中心 (1.5,1.5)）。
        // 绕轴心 (3,3) 顺时针转 90°（屏幕坐标 y 向下：(x,y)->(-y,x)）：
        // 中心相对轴心 (-1.5,-1.5) -> (1.5,-1.5) -> 绝对中心 (4.5,1.5)
        // -> 落在像素 (4,1)。dst 6x6 置黑。
        GraphicsStore::gCreate(20, 6, 6);
        GraphicsStore::gCreate(21, 6, 6);
        GraphicsStore::gClear(20, QColor(0, 0, 0));
        GraphicsStore::gClear(21, QColor(0, 0, 0));
        // 源图的单像素：gImage 返回**副本**，直接 QPainter 画会丢 —— 用 gFillRectangle
        check(GraphicsStore::gFillRectangle(20, QColor(255, 0, 0), QRect(1, 1, 1, 1)),
              "源图放置单像素（经存储，非副本）");
        check(GraphicsStore::gDrawGWithRotate(21, 20, 90, 3, 3), "GDRAWGWITHROTATE 成功");
        const QImage rotated = GraphicsStore::gImage(21);
        check(rotated.pixelColor(4, 1) == QColor(255, 0, 0),
              QString("顺时针 90°：像素 (1,1) -> (4,1)（实测色 %1）")
                  .arg(rotated.pixelColor(5, 1).name()));
        int painted = 0;
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 6; ++x)
                if (rotated.pixelColor(x, y) != QColor(0, 0, 0)) ++painted;
        check(painted >= 1, "旋转后源像素落入目标（未整图丢失）");
    }

    GraphicsStore::clearAll();
    qDebug() << "======================================";
    if (g_failed == 0) {
        qDebug() << "[SUCCESS] G-drawing tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failed << "check(s) failed";
    return 1;
}
