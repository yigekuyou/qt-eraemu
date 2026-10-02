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

// ---------------- 画笔 / 刷子 / 字体 ----------------
namespace {
struct GExtra {
    QHash<int, GraphicsStore::PenState> pen;
    QHash<int, GraphicsStore::BrushState> brush;
    QHash<int, GraphicsStore::FontState> font;
    QList<GraphicsStore::CbgLayer> cbg;
    QHash<QString, GraphicsStore::Anime> anime;   // 键统一大写
};
GExtra& gExtra() {
    static GExtra store;
    return store;
}
} // namespace

bool GraphicsStore::gSetBrush(int id, const QColor& color) {
    if (!gImages().contains(id)) return false;
    gExtra().brush[id] = BrushState{ color };
    return true;
}

bool GraphicsStore::gSetPen(int id, const QColor& color, int width) {
    if (!gImages().contains(id)) return false;
    gExtra().pen[id] = PenState{ color, qMax(1, width) };
    return true;
}

bool GraphicsStore::gSetFont(int id, const QString& name, int size) {
    if (!gImages().contains(id)) return false;
    gExtra().font[id] = FontState{ name, size };
    return true;
}

// ---------------- GSAVE / GLOAD ----------------
bool GraphicsStore::gSave(int id, const QString& path) {
    const QImage image = gImage(id);
    if (image.isNull() || path.isEmpty()) return false;
    return image.save(path, "png");
}

bool GraphicsStore::gLoad(int id, const QString& path) {
    if (path.isEmpty()) return false;
    QImage image(path);
    if (image.isNull()) return false;
    gImages().insert(id, image);
    return true;
}

// ---------------- GDRAWGWITHMASK ----------------
bool GraphicsStore::gDrawGWithMask(int dstId, int srcId, int maskId, int dx, int dy) {
    const QImage src = gImage(srcId);
    const QImage mask = gImage(maskId);
    QImage dst = gImage(dstId);
    if (src.isNull() || mask.isNull() || dst.isNull()) return false;
    // 掩码尺寸以 dst 可用区域为准（对齐 C#：以掩码图像的 alpha 通道为选中区）
    const int w = qMin(qMin(src.width(), mask.width()), dst.width() - dx);
    const int h = qMin(qMin(src.height(), mask.height()), dst.height() - dy);
    if (w <= 0 || h <= 0) return false;
    QImage overlay = dst.copy(dx, dy, w, h).convertToFormat(QImage::Format_ARGB32);
    const QImage s = src.convertToFormat(QImage::Format_ARGB32);
    const QImage m = mask.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        const QRgb* mline = reinterpret_cast<const QRgb*>(m.constScanLine(y));
        const QRgb* sline = reinterpret_cast<const QRgb*>(s.constScanLine(y));
        QRgb* oline = reinterpret_cast<QRgb*>(overlay.scanLine(y));
        for (int x = 0; x < w; ++x) {
            if (qAlpha(mline[x]) == 0) continue;   // 掩码透明处保留 dst
            oline[x] = sline[x];
        }
    }
    gDrawImage(dstId, overlay, QRect(dx, dy, w, h));
    return true;
}

// ---------------- CBG 角色背景层 ----------------
bool GraphicsStore::cbgSetG(int gId, int x, int y, int z) {
    if (!gImages().contains(gId)) return false;
    CbgLayer layer;
    layer.gId = gId; layer.x = x; layer.y = y; layer.z = z;
    gExtra().cbg.append(layer);
    return true;
}

bool GraphicsStore::cbgSetSprite(const QString& sprite, int x, int y, int z) {
    if (!spriteCreated(sprite)) return false;
    CbgLayer layer;
    layer.isSprite = true; layer.sprite = sprite.toUpper();
    layer.x = x; layer.y = y; layer.z = z;
    gExtra().cbg.append(layer);
    return true;
}

bool GraphicsStore::cbgSetButtonSprite(qint64 value, const QString& sprite,
                                       const QString& selectedSprite, int x, int y, int z,
                                       const QString& tooltip) {
    if (!spriteCreated(sprite)) return false;
    CbgLayer layer;
    layer.isSprite = true; layer.isButton = true;
    layer.sprite = sprite.toUpper();
    layer.selectedSprite = selectedSprite.toUpper();
    layer.buttonValue = value;
    layer.x = x; layer.y = y; layer.z = z; layer.tooltip = tooltip;
    gExtra().cbg.append(layer);
    return true;
}

bool GraphicsStore::cbgSetBmapG(int gId) {
    if (!gImages().contains(gId)) return false;
    CbgLayer layer;
    layer.gId = gId; layer.isBmap = true;
    gExtra().cbg.append(layer);
    return true;
}

void GraphicsStore::cbgClear() { gExtra().cbg.clear(); }

void GraphicsStore::cbgClearButton() {
    auto& cbg = gExtra().cbg;
    for (int i = cbg.size() - 1; i >= 0; --i)
        if (cbg.at(i).isButton) cbg.removeAt(i);
}

bool GraphicsStore::cbgRemoveRange(int zMin, int zMax) {
    auto& cbg = gExtra().cbg;
    const int before = cbg.size();
    for (int i = cbg.size() - 1; i >= 0; --i) {
        const int z = cbg.at(i).z;
        if (z >= zMin && z <= zMax) cbg.removeAt(i);
    }
    return cbg.size() != before;
}

void GraphicsStore::cbgRemoveBmap() {
    auto& cbg = gExtra().cbg;
    for (int i = cbg.size() - 1; i >= 0; --i)
        if (cbg.at(i).isBmap) cbg.removeAt(i);
}

const QList<GraphicsStore::CbgLayer>& GraphicsStore::cbgLayers() {
    return gExtra().cbg;
}

// ---------------- 精灵动画 ----------------
bool GraphicsStore::spriteAnimeCreate(const QString& name, int width, int height) {
    if (name.isEmpty() || width <= 0 || height <= 0) return false;
    Anime anime;
    anime.size = QSize(width, height);
    gExtra().anime.insert(name.toUpper(), anime);
    return true;
}

bool GraphicsStore::spriteAnimeAddFrame(const QString& name, int gId, int x, int y,
                                        int width, int height, int dx, int dy, int delay) {
    auto it = gExtra().anime.find(name.toUpper());
    if (it == gExtra().anime.end()) return false;
    AnimeFrame frame;
    frame.gId = gId;
    frame.rect = (width > 0 && height > 0) ? QRect(x, y, width, height) : QRect();
    frame.dx = dx; frame.dy = dy; frame.delay = delay;
    it->frames.append(frame);
    return true;
}

void GraphicsStore::clearAll() {
    gImages().clear();
    sprites().clear();
    gExtra().pen.clear();
    gExtra().brush.clear();
    gExtra().font.clear();
    gExtra().cbg.clear();
    gExtra().anime.clear();
}
