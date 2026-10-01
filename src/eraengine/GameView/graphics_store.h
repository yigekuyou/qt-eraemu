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
#ifndef GRAPHICS_STORE_H
#define GRAPHICS_STORE_H

#include <QHash>
#include <QImage>
#include <QRect>
#include <QString>

// ---------------------------------------------------------------------------
// GraphicsStore —— G 图像缓冲 + 运行期精灵注册表
//
// 对齐 C# 的 GraphicsImage（GCREATE / GCLEAR / GDRAWG / …）与
// AppContents 的 ASprite（SPRITECREATE / SPRITECREATED / …）子系统：
//   * G 图像：脚本用 GCREATE(id, w, h) / GCREATEFROMFILE(id, name) 建立画布，
//     GDRAWG / GDRAWSPRITE 往上面合成，GCREATED / GWIDTH / GHEIGHT 查询；
//   * 精灵：SPRITECREATE(name, gID[, x, y, w, h]) 把 G 图像（或其子矩形）
//     登记成具名精灵；HTML_PRINT 的 <img src='name'> 与 PRINT_IMG 通过
//     ResourceImageProvider 的精灵回退直接显示。
// 立绘合成（eraTW 的 リソース作成.ERB：素体×表情 → SPRITECREATE）全靠这条链。
// ---------------------------------------------------------------------------
class GraphicsStore {
public:
    struct Sprite {
        QImage image;
        bool   created = false;
        int    posX = 0;
        int    posY = 0;
    };

    // ---- G 图像 ----
    static bool gCreate(int id, int width, int height);
    static bool gCreateFromFile(int id, const QString& resourceName);
    static bool gDispose(int id);
    static bool gCreated(int id);
    static bool gClear(int id, const QColor& color);
    static bool gFillRectangle(int id, const QColor& color, const QRect& rect);
    // 把 src 图像画到 id 的 destRect（自动缩放）；colorMatrix 非空时逐像素变换
    static bool gDrawImage(int id, const QImage& src, const QRect& destRect,
                           const float colorMatrix[5][5] = nullptr);
    static bool gDrawG(int id, int srcId, const QRect& destRect, const QRect& srcRect,
                       const float colorMatrix[5][5] = nullptr);
    [[nodiscard]] static QImage gImage(int id);

    // ---- 精灵 ----
    static bool spriteCreate(const QString& name, int gId, const QRect& rect = QRect());
    static bool spriteDispose(const QString& name);
    static bool spriteSetPos(const QString& name, int x, int y);
    [[nodiscard]] static bool spriteCreated(const QString& name);
    [[nodiscard]] static QImage spriteImage(const QString& name);
    [[nodiscard]] static const Sprite* sprite(const QString& name);

    // ---- 颜色矩阵（GDRAWG / GDRAWSPRITE 的第 7/11 实参，5x5 整数 / 256）----
    static bool applyColorMatrix(QImage& image, const float colorMatrix[5][5]);

    // RESETDATA / 重新装载时清空（G 图像与精灵都是运行期状态）
    static void clearAll();
};

#endif // GRAPHICS_STORE_H
