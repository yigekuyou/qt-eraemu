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
// test_image_layout.cpp
//
// `<img src=…>` 的排版尺寸（对齐 C# GameView/ConsoleImagePart.cs）：
//     height = raw_height==0 ? FontSize : FontSize*raw_height/100
//     Width  = raw_width==0  ? 固有宽*height/固有高 : FontSize*raw_width/100
//     top    = raw_ypos * FontSize / 100
//
// 缺陷现场（eraTW 立絵）：
//   IMAGE.ERB:311 `<img src='%sRes_Name%' height='{iFont_Hei_mag}'>` —— 只给 height。
//   引擎此前把「width<=0 **或** height<=0」都当成「按资源固有像素排版」，
//   于是 height 被丢掉、立絵永远画成资源原始像素 —— GETCONFIG 里的
//   「画像サイズ 拡大/縮小」（iSize）完全不起作用；立絵高度也不再跨 10 行，
//   后面的 <br> 与画像枠/時間停止 的 ypos 叠层全部错位。
//
// 用 60×20 的资源（半径外均为透明）覆盖三种形态：
//   * 两个都不写 → 固有像素；
//   * 只写 height → 宽度按纵横比补（本次修复点）；
//   * 两个都写   → 各自按字号百分比。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include "console_backend.h"
#include "console_types.h"
#include "resource_image_provider.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static constexpr int kFontSize = 16;    // 一列（半角）= FontSize/2 = 8px
static constexpr int kLineHeight = 20;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Image layout (<img>) test";
    qDebug() << "=========================";

    // ---- 夹具：resources/list.csv + 60×20 的 PNG（宽高比 3:1）----
    QTemporaryDir tmp;
    if (!tmp.isValid()) { qCritical() << "QTemporaryDir 创建失败"; return 1; }
    QDir().mkpath(tmp.path() + QStringLiteral("/resources"));
    {
        QImage img(60, 20, QImage::Format_ARGB32);
        img.fill(Qt::red);
        img.save(tmp.path() + QStringLiteral("/resources/wide.png"));
        QFile csv(tmp.path() + QStringLiteral("/resources/list.csv"));
        if (!csv.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCritical() << "夹具 csv 创建失败";
            return 1;
        }
        csv.write("wide,wide.png,0,0,60,20\n");
        csv.close();
    }
    ResourceImageProvider::setRoot(tmp.path());

    ConsoleBackend console;
    console.setFontSize(kFontSize);
    console.setLineHeight(kLineHeight);
    console.setGridColumns(80);        // 640px ÷ 8px = 80 列
    console.setGridRows(50);

    // 打印一段 `<img …><br>` 并取回首行唯一的 span（flush 之后才做过排版测量）
    const auto spanOf = [&](const QString& html) -> ConsoleSpan {
        console.clearAll();
        console.printHtml(html + QStringLiteral("<br>"));
        console.flush();
        if (console.buffer().count() == 0) return ConsoleSpan{};
        const QList<ConsoleSpan> spans = console.buffer().at(0).flatSpans();
        return spans.isEmpty() ? ConsoleSpan{} : spans.first();
    };

    qDebug() << "\n1) `<img src='wide'>`（两个都不写 -> 固有像素 60×20）";
    {
        const ConsoleSpan s = spanOf(QStringLiteral("<img src='wide'>"));
        check(s.kind == ConsoleSpanKind::Image, "kind == Image");
        check(s.imageIntrinsic == QSizeF(60, 20), "imageIntrinsic 记录固有像素");
        check(s.imageSizeIsPixels, "imageSizeIsPixels == true（两者都缺省）");
        check(s.width == 64 && s.height == 20,
              QString("块像素 64×20（cols=8,rows=1）得到 %1×%2").arg(s.width).arg(s.height));
    }

    qDebug() << "\n2) `<img src='wide' height='100'>`（只给 height -> 宽度按纵横比）";
    {
        const ConsoleSpan s = spanOf(QStringLiteral("<img src='wide' height='100'>"));
        check(!s.imageSizeIsPixels, "保留 height（不再被固有像素覆盖）");
        // height = 16px；width = 60*16/20 = 48px -> cols = 48/8 = 6
        check(s.height == 20, QString("height == 20（16px 取整到 1 行）得到 %1").arg(s.height));
        check(s.width == 48, QString("width == 48（60×16/20，纵横比）得到 %1").arg(s.width));
    }

    qDebug() << "\n3) `<img src='wide' height='1000'>`（eraTW 立絵 10 行）×";
    {
        const ConsoleSpan s = spanOf(QStringLiteral("<img src='wide' height='1000'>"));
        // height = 160px -> rows = 8；width = 60*160/20 = 480px -> cols = 60
        check(s.height == 160, QString("height == 160（10 倍字号）得到 %1").arg(s.height));
        check(s.width == 480, QString("width == 480（纵横比 3:1）得到 %1").arg(s.width));
        check(s.top == 0, "ypos 缺省 top == 0");
        // 回归：旧实现这里会得到 60×20（固有像素），height='1000' 被吞
        check(!(s.width == 64 && s.height == 20), "不再回退成固有像素 64×20");
    }

    qDebug() << "\n4) `<img src='wide' width='1000'>`（只给 width）";
    {
        const ConsoleSpan s = spanOf(QStringLiteral("<img src='wide' width='1000'>"));
        check(s.width == 160, QString("width == 160（10 倍字号）得到 %1").arg(s.width));
        check(s.height == kLineHeight, QString("height 缺省 = 1 行得到 %1").arg(s.height));
    }

    qDebug() << "\n5) `<img src='wide' width='1000' height='1000'>`（都写）";
    {
        const ConsoleSpan s = spanOf(QStringLiteral("<img src='wide' width='1000' height='1000'>"));
        check(s.width == 160 && s.height == 160, "两者都按字号百分比");
    }

    qDebug() << "\n6) ypos 与叠层（C# raw_ypos * FontSize / 100）";
    {
        const ConsoleSpan s = spanOf(
            QStringLiteral("<img src='wide' height='1000' ypos='-100'>"));
        check(s.top == -16, QString("ypos=-100 -> top = -16px 得到 %1").arg(s.top));
    }

    qDebug() << "\n=========================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] image layout tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
