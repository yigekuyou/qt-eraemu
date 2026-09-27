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

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "ResourceImageProvider test";
    qDebug() << "==========================";

    QTemporaryDir dir;
    const QString root = dir.path();
    writeImage(QDir(root).filePath(QStringLiteral("resources/face_01.png")));
    writeImage(QDir(root).filePath(QStringLiteral("bg.jpg")));
    writeImage(QDir(root).filePath(QStringLiteral("resources/icon.bmp")));

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
    check(ResourceImageProvider::resolvePath(QStringLiteral("nope"), root).isEmpty(),
          "不存在的资源 -> 空");
    check(ResourceImageProvider::resolvePath(QStringLiteral("face_01"), QString()).isEmpty(),
          "根目录为空 -> 空");

    qDebug() << "\n2) requestImage";
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

    qDebug() << "\n==========================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] resource-image tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
