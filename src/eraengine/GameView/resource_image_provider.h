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
#ifndef RESOURCE_IMAGE_PROVIDER_H
#define RESOURCE_IMAGE_PROVIDER_H

#include <QQuickImageProvider>
#include <QString>

// ---------------------------------------------------------------------------
// ResourceImageProvider —— 供 QML 用 `Image { source: "image://emuera/<name>" }`
// 读取游戏资源图（对齐 C# GameView 的 ConstImage「资源名 → 图片」加载）。
//
// 混合形态里 C++ 只提供「资源名 → 文件」的服务；实际显示由 QML 的 Image 对象负责。
// 名称解析顺序（第一个存在者胜出）：
//     <root>/resources/<name>[.ext]
//     <root>/<name>[.ext]
// 默认扩展名：png / jpg / jpeg / bmp / webp / gif（未写扩展名时逐个尝试）。
//
// requestImage 返回 QImage（不依赖 GUI 平台），因此可用 QCoreApplication 单测。
// ---------------------------------------------------------------------------
class ResourceImageProvider : public QQuickImageProvider {
public:
    ResourceImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    // 资源根目录（游戏目录）；由 EraEngine 在装载时设置
    static void setRoot(const QString& dir);
    [[nodiscard]] static QString root();

    // 解析资源名到绝对文件路径（不存在返回空串）。root 为空时用已设置的根。
    [[nodiscard]] static QString resolvePath(const QString& id, const QString& root = QString());

private:
    static QString s_root;
};

#endif // RESOURCE_IMAGE_PROVIDER_H
