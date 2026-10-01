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
#include "graphics_store.h"

#include "resource_image_provider.h"

#include <QDebug>
#include <QPainter>

namespace {
QHash<int, QImage>& gImages() {
    static QHash<int, QImage> store;
    return store;
}
QHash<QString, GraphicsStore::Sprite>& sprites() {
    static QHash<QString, GraphicsStore::Sprite> store;   // 键统一大写（ICVariable）
    return store;
}
} // namespace

bool GraphicsStore::gCreate(int id, int width, int height) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return false;
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    gImages().insert(id, image);
    return true;
}

bool GraphicsStore::gCreateFromFile(int id, const QString& resourceName) {
    if (gImages().contains(id)) return false;   // C#：已存在的 G 不重建
    const QString path = ResourceImageProvider::resolvePath(resourceName);
    if (path.isEmpty()) return false;
    QImage image(path);
    if (image.isNull()) return false;
    gImages().insert(id, image.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    return true;
}

bool GraphicsStore::gDispose(int id) {
    return gImages().remove(id) > 0;
}

bool GraphicsStore::gCreated(int id) {
    return gImages().contains(id);
}

bool GraphicsStore::gClear(int id, const QColor& color) {
    auto it = gImages().find(id);
    if (it == gImages().end()) return false;
    it->fill(color.isValid() ? color : QColor(0, 0, 0, 0));
    return true;
}

bool GraphicsStore::gFillRectangle(int id, const QColor& color, const QRect& rect) {
    auto it = gImages().find(id);
    if (it == gImages().end()) return false;
    QPainter painter(&*it);
    painter.fillRect(rect, color);
    return true;
}

bool GraphicsStore::gDrawImage(int id, const QImage& src, const QRect& destRect,
                               const float colorMatrix[5][5]) {
    auto it = gImages().find(id);
    if (it == gImages().end() || src.isNull()) return false;
    QImage drawn = src;
    if (colorMatrix) applyColorMatrix(drawn, colorMatrix);
    QPainter painter(&*it);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(destRect, drawn);
    return true;
}

bool GraphicsStore::gDrawG(int id, int srcId, const QRect& destRect, const QRect& srcRect,
                           const float colorMatrix[5][5]) {
    const QImage src = gImage(srcId);
    if (src.isNull()) return false;
    const QRect rect = srcRect.isNull() ? src.rect() : srcRect;
    return gDrawImage(id, src.copy(rect), destRect, colorMatrix);
}

QImage GraphicsStore::gImage(int id) {
    return gImages().value(id);
}

bool GraphicsStore::spriteCreate(const QString& name, int gId, const QRect& rect) {
    if (name.isEmpty()) return false;
    const QImage src = gImage(gId);
    if (src.isNull()) return false;   // C#：G 未建立 -> 0
    Sprite sprite;
    sprite.created = true;
    sprite.image = rect.isNull() ? src : src.copy(rect);
    if (sprite.image.isNull()) return false;
    sprites().insert(name.toUpper(), sprite);
    return true;
}

bool GraphicsStore::spriteDispose(const QString& name) {
    return sprites().remove(name.toUpper()) > 0;
}

bool GraphicsStore::spriteSetPos(const QString& name, int x, int y) {
    auto it = sprites().find(name.toUpper());
    if (it == sprites().end()) return false;
    it->posX = x;
    it->posY = y;
    return true;
}

bool GraphicsStore::spriteCreated(const QString& name) {
    const Sprite* s = sprite(name);
    return s && s->created;
}

QImage GraphicsStore::spriteImage(const QString& name) {
    const Sprite* s = sprite(name);
    if (s && s->created) return s->image;
    // 静态资源回退：C# 的 imageDictionary 包含 resources 装载的所有精灵，
    // GDRAWSPRITE / SPRITEWIDTH 等对 "55_A1" 这类资源名同样有效。
    return ResourceImageProvider::loadResourceImage(name);
}

const GraphicsStore::Sprite* GraphicsStore::sprite(const QString& name) {
    auto it = sprites().constFind(name.toUpper());
    return it != sprites().constEnd() ? &it.value() : nullptr;
}

bool GraphicsStore::applyColorMatrix(QImage& image, const float colorMatrix[5][5]) {
    if (image.isNull()) return false;
    QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    // GDI+ ColorMatrix：行 = 输出通道（R,G,B,A,平移），列 = 输入分量（R,G,B,A,w）
    for (int y = 0; y < argb.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(argb.scanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            const QRgb c = line[x];
            const float in[4] = { float(qRed(c)), float(qGreen(c)), float(qBlue(c)), float(qAlpha(c)) };
            float out[4];
            for (int row = 0; row < 4; ++row) {
                float v = colorMatrix[row][4] * 255.0f;   // 平移行（0..1 -> 0..255）
                for (int col = 0; col < 4; ++col)
                    v += in[col] * colorMatrix[row][col];
                out[row] = qBound(0.0f, v, 255.0f);
            }
            line[x] = qRgba(int(out[0]), int(out[1]), int(out[2]), int(out[3]));
        }
    }
    image = argb;
    return true;
}

void GraphicsStore::clearAll() {
    gImages().clear();
    sprites().clear();
}
