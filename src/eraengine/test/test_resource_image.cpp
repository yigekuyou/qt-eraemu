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
// test_resource_image.cpp
//
// 资源图服务（"image://emuera/<name>"）的名称解析单测（无 GUI）：
//   1. resources/ 优先于根目录
//   2. 未写扩展名时按 png/jpg/... 探测
//   3. 显式扩展名不追加
//   4. 去查询串 / 前导斜杠
//   5. requestImage 对存在/不存在文件的行为
//   6. 图集输出偏移（CSV 第 7/8 列）与「尺寸头回退」（Qt 解不了的 webp）
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include "resource_image_provider.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static void writeImage(const QString& path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(Qt::red);
    img.save(path);
}

// 34 字节的 VP8L「全透明 180x180」webp —— 与 eraTW 的 resources/ダミー.webp
// 逐字节相同。Qt 的 webp 解码器读不了它（libwebp 可以），所以它同时是
// 「尺寸头回退」的夹具。
static const unsigned char kLosslessTransparentWebp[] = {
    0x52, 0x49, 0x46, 0x46, 0x1a, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50,
    0x56, 0x50, 0x38, 0x4c, 0x0d, 0x00, 0x00, 0x00, 0x2f, 0xb3, 0xc0, 0x2c,
    0x10, 0x07, 0x10, 0x11, 0x11, 0x88, 0x88, 0xfe, 0x07, 0x00};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "ResourceImageProvider test";
    qDebug() << "==========================";

    QTemporaryDir dir;
    const QString root = dir.path();
    writeImage(QDir(root).filePath(QStringLiteral("resources/face_01.png")));
    writeImage(QDir(root).filePath(QStringLiteral("bg.jpg")));
    writeImage(QDir(root).filePath(QStringLiteral("resources/icon.bmp")));
    writeImage(QDir(root).filePath(QStringLiteral("resources/服_笑顔.webp")));
    writeImage(QDir(root).filePath(QStringLiteral("resources/title.webp")));
    {
        QFile atlas(QDir(root).filePath(QStringLiteral("resources/list.csv")));
        if (atlas.open(QIODevice::WriteOnly | QIODevice::Text))
            atlas.write(QByteArrayLiteral("TW_title004,title.webp,0,1,2,2\n"));
    }
    // 图集里的输出偏移（第 7/8 列）+ 一个 Qt 解不了的全透明 webp。
    // 注意：图集清单是按 root 缓存的，所有夹具必须在第一次 setRoot() 之前写好。
    const QString webpPath = QDir(root).filePath(QStringLiteral("resources/transparent.webp"));
    {
        QFile webp(webpPath);
        if (webp.open(QIODevice::WriteOnly))
            webp.write(reinterpret_cast<const char*>(kLosslessTransparentWebp),
                       int(sizeof(kLosslessTransparentWebp)));
    }
    {
        QFile atlas(QDir(root).filePath(QStringLiteral("resources/offset.csv")));
        if (atlas.open(QIODevice::WriteOnly | QIODevice::Text)) {
            atlas.write("part,title.webp,0,0,2,2,3,2\n");        // 带偏移
            atlas.write("plain,title.webp,0,0,2,2\n");           // 6 列：无偏移
            atlas.write("plainzero,title.webp,0,0,2,2,0,0\n");   // 显式 (0,0)
            atlas.write("transparent,transparent.webp\n");       // 尺寸头回退
        }
    }

    qDebug() << "\n1) 资源名解析";
    check(ResourceImageProvider::resolvePath(QStringLiteral("face_01"), root)
              == QDir(root).filePath(QStringLiteral("resources/face_01.png")),
          "resources/ 优先 + 探测 .png");
    check(ResourceImageProvider::resolvePath(QStringLiteral("bg"), root)
              == QDir(root).filePath(QStringLiteral("bg.jpg")),
          "根目录回退 + 探测 .jpg");
    check(ResourceImageProvider::resolvePath(QStringLiteral("bg.jpg"), root)
              == QDir(root).filePath(QStringLiteral("bg.jpg")),
          "显式扩展名不追加");
    check(ResourceImageProvider::resolvePath(QStringLiteral("/icon.bmp"), root)
              == QDir(root).filePath(QStringLiteral("resources/icon.bmp")),
          "前导斜杠 + 显式 .bmp");
    check(ResourceImageProvider::resolvePath(QStringLiteral("face_01.png?x=1"), root)
              == QDir(root).filePath(QStringLiteral("resources/face_01.png")),
          "去查询串");
    check(ResourceImageProvider::resolvePath(QStringLiteral("%E6%9C%8D_%E7%AC%91%E9%A1%94"), root)
              == QDir(root).filePath(QStringLiteral("resources/服_笑顔.webp")),
          "URL 编码的日文资源名");
    check(ResourceImageProvider::resolvePath(QStringLiteral("nope"), root).isEmpty(),
          "不存在的资源 -> 空");
    check(ResourceImageProvider::resolvePath(QStringLiteral("face_01"), QString()).isEmpty(),
          "根目录为空 -> 空");

    qDebug() << "\n2) requestImage";
    {
        ResourceImageProvider::setRoot(root);
        ResourceImageProvider prov;
        QSize atlasSize;
        const QImage atlasImage = prov.requestImage(QStringLiteral("TW_title004"), &atlasSize, QSize());
        check(!atlasImage.isNull(), "list.csv 图集资源返回有效图片");
        check(atlasSize == QSize(2, 2), "图集资源按 CSV 裁剪尺寸");
    }
    {
        // 固有尺寸：`<img src='X'>` 不带 width/height 时排版要用它，否则图片会被
        // 当成「一个字号见方」压成小方块（eraTW 标题画面 35 张 1041×16 条图曾如此）。
        ResourceImageProvider::setRoot(root);
        int w = 0, h = 0;
        check(ResourceImageProvider::intrinsicSize(QStringLiteral("TW_title004"), w, h)
                  && w == 2 && h == 2,
              "图集资源固有尺寸取自 list.csv 的矩形");
        w = h = 0;
        check(ResourceImageProvider::intrinsicSize(QStringLiteral("face_01"), w, h)
                  && w == 4 && h == 4,
              "独立图片固有尺寸取自文件实际大小");
        w = h = 0;
        check(!ResourceImageProvider::intrinsicSize(QStringLiteral("ghost"), w, h),
              "不存在的资源 -> false");
    }
    {
        ResourceImageProvider::setRoot(root);
        ResourceImageProvider prov;
        QSize size;
        const QImage img = prov.requestImage(QStringLiteral("face_01"), &size, QSize());
        check(!img.isNull(), "已存在资源返回有效 QImage");
        check(size == QSize(4, 4), "size 出参 == 4x4");

        QSize size2;
        const QImage missing = prov.requestImage(QStringLiteral("ghost"), &size2, QSize());
        check(missing.isNull(), "不存在资源返回空 QImage");

        QSize size3;
        const QImage scaled = prov.requestImage(QStringLiteral("face_01"), &size3, QSize(2, 2));
        check(scaled.width() == 2 && scaled.height() == 2, "按 requestedSize 缩放为 2x2");
    }

    qDebug() << "\n3) 图集输出偏移（CSV 第 7/8 列 = C# SpriteF.DestBasePosition）";
    {
        ResourceImageProvider::setRoot(root);
        int x = -1, y = -1;
        check(ResourceImageProvider::spriteOffset(QStringLiteral("part"), x, y) && x == 3 && y == 2,
              "带第 7/8 列的条目 -> (3,2)");
        x = y = -1;
        check(ResourceImageProvider::spriteOffset(QStringLiteral("plain"), x, y) && x == 0 && y == 0,
              "只有 6 列（旧写法）-> (0,0)");
        x = y = -1;
        check(ResourceImageProvider::spriteOffset(QStringLiteral("plainzero"), x, y) && x == 0 && y == 0,
              "显式 (0,0) -> (0,0)");
        x = y = -1;
        check(!ResourceImageProvider::spriteOffset(QStringLiteral("ghost"), x, y),
              "不是图集条目 -> false");
    }

    qDebug() << "\n4) 尺寸头回退（Qt 解不了的 34 字节全透明 webp）";
    {
        ResourceImageProvider::setRoot(root);
        // 前提：Qt 的 webp 解码器确实读不了它（C# 走 libwebp 直连能读）。
        const QImage direct(webpPath);
        if (!direct.isNull())
            qDebug() << "  [note] 本机 Qt 能解码该 webp，用例退化为普通解码路径";
        else
            check(true, "前提成立：QImage 直接读该 webp 失败");

        const QImage fallback = ResourceImageProvider::loadImageFile(webpPath);
        check(fallback.size() == QSize(180, 180), "按文件头（VP8L）回退出 180x180");
        check(!fallback.isNull() && qAlpha(fallback.pixel(0, 0)) == 0, "回退图是全透明");
        check(ResourceImageProvider::loadImageFile(QStringLiteral("/no/such/file.png")).isNull(),
              "不存在的路径 -> 空图");
        int w = 0, h = 0;
        check(ResourceImageProvider::intrinsicSize(QStringLiteral("transparent"), w, h)
                  && w == 180 && h == 180,
              "intrinsicSize 走回退 -> SPRITEWIDTH/SPRITEHEIGHT 得 180");
        // 正常图不能被回退逻辑影响
        const QImage ok = ResourceImageProvider::loadImageFile(
            QDir(root).filePath(QStringLiteral("resources/face_01.png")));
        check(ok.size() == QSize(4, 4), "能解码的图照常返回原图（4x4）");
    }

    qDebug() << "\n==========================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] resource-image tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
