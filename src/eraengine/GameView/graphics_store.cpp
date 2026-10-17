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
#include <cmath>
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
// 静态资源（CSV 精灵）被 SPRITESETPOS/SPRITEMOVE 改过的位置。
// C# 里静态精灵也是可变对象（AppContents.GetSprite 返回同一个 ASprite），
// 我们不改 ResourceImageProvider 的只读清单，改在这里覆盖。
QHash<QString, QPoint>& spritePosOverrides() {
    static QHash<QString, QPoint> store;   // 键统一大写
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
    // loadImageFile：Qt 解不了的图（eraTW 的 34 字节全透明 ダミー.webp）也要拿到尺寸
    QImage image = ResourceImageProvider::loadImageFile(path);
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
    const QString key = name.toUpper();
    spritePosOverrides().remove(key);
    return sprites().remove(key) > 0;
}

bool GraphicsStore::spriteSetPos(const QString& name, int x, int y) {
    const QString key = name.toUpper();
    auto it = sprites().find(key);
    if (it != sprites().end()) {
        it->posX = x;
        it->posY = y;
        return true;
    }
    // 静态资源：C# 里 SPRITESETPOS 对 CSV 精灵同样有效 -> 记到覆盖表。
    int sx = 0, sy = 0;
    if (!ResourceImageProvider::spriteOffset(name, sx, sy)) return false;
    spritePosOverrides().insert(key, QPoint(x, y));
    return true;
}

bool GraphicsStore::spriteBasePos(const QString& name, int& x, int& y) {
    if (const Sprite* s = sprite(name); s && s->created) {
        x = s->posX;
        y = s->posY;
        return true;
    }
    int sx = 0, sy = 0;
    if (!ResourceImageProvider::spriteOffset(name, sx, sy)) return false;
    const auto ov = spritePosOverrides().constFind(name.toUpper());
    if (ov != spritePosOverrides().constEnd()) {
        x = ov->x();
        y = ov->y();
    } else {
        x = sx;
        y = sy;
    }
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

QColor GraphicsStore::brushColor(int id) {
    const auto it = gExtra().brush.constFind(id);
    // C# 缺省用 Config.BackColor；GraphicsStore 是静态层、无 Config 通道，
    // 未设画刷时回退透明（不产生可见填充）。实际游戏都会先 GSETBRUSH。
    return it == gExtra().brush.constEnd() ? QColor(0, 0, 0, 0) : it.value().color;
}

bool GraphicsStore::gSetPen(int id, const QColor& color, int width) {
    if (!gImages().contains(id)) return false;
    // 对齐 C# `pen = new Pen(color, width)`：整支换新（GDASHSTYLE 此前设置的虚线
    // 样式随之重置 —— GDI+ 语义同样如此）。valid=true 才会被 GDRAWLINE/GGETPEN 采用。
    PenState ps;
    ps.color = color;
    ps.width = qMax(1, width);
    ps.valid = true;
    gExtra().pen[id] = ps;
    return true;
}

bool GraphicsStore::gSetFont(int id, const QString& name, int size, int style) {
    if (!gImages().contains(id)) return false;
    gExtra().font[id] = FontState{ name, qMax(1, size), style, true };
    return true;
}

bool GraphicsStore::fontState(int id, FontState& out) {
    if (!gImages().contains(id)) return false;
    const auto it = gExtra().font.constFind(id);
    if (it == gExtra().font.constEnd()) { out = FontState{}; return true; }
    out = it.value();
    return true;
}

bool GraphicsStore::penState(int id, PenState& out) {
    if (!gImages().contains(id)) return false;
    const auto it = gExtra().pen.constFind(id);
    if (it == gExtra().pen.constEnd()) { out = PenState{}; return true; }
    out = it.value();
    return true;
}

QColor GraphicsStore::brushState(int id) {
    const auto it = gExtra().brush.constFind(id);
    return it == gExtra().brush.constEnd() ? QColor(0, 0, 0, 0) : it.value().color;
}

// QFont 工具：EE 样式位掩码 -> QFont。C# 用 `GraphicsUnit.Pixel`（像素字号）。
QFont GraphicsStore::makeFont(const QString& name, int size, int style) {
    QFont font(name);
    font.setPixelSize(qMax(1, size));
    font.setBold((style & 1) != 0);
    font.setItalic((style & 2) != 0);
    font.setStrikeOut((style & 4) != 0);
    font.setUnderline((style & 8) != 0);
    return font;
}

QSize GraphicsStore::measureText(const QString& text, const QString& fontName,
                                 int size, int style) {
    const QFontMetrics metrics(makeFont(fontName, size, style));
    // C# MeasureString + StringFormat.GenericTypographic = 紧排前进量（无内边距）
    return { metrics.horizontalAdvance(text), metrics.height() };
}

// GDRAWTEXT：C# GraphicsImage.GDrawString(text, x, y)（GraphicsPath.AddString +
// FillPath，原点是文本块**左上角**）。QPainter 用 QRect+AlignTop|AlignLeft 对齐该语义，
// 文字颜色 = 画刷色（GDI+ 用 brush 填充文本；未设置时用调用方的兜底 = Config.ForeColor）。
bool GraphicsStore::gDrawString(int id, const QString& text, int x, int y,
                                const QFont& fallbackFont, const QColor& fallbackBrush,
                                int* measuredW, int* measuredH) {
    auto it = gImages().find(id);
    if (it == gImages().end() || it->isNull()) return false;
    const FontState fs = gExtra().font.value(id);
    const QFont font = fs.valid ? makeFont(fs.name, fs.size, fs.style) : fallbackFont;
    const QFontMetrics metrics(font);
    if (measuredW) *measuredW = metrics.horizontalAdvance(text);
    if (measuredH) *measuredH = metrics.height();
    const QColor brush = fs.valid && gExtra().brush.contains(id) ? brushColor(id) : fallbackBrush;
    QPainter painter(&*it);
    painter.setFont(font);
    painter.setPen(brush);
    painter.drawText(QRect(x, y, metrics.horizontalAdvance(text) + 64, metrics.height() * 2),
                     Qt::TextDontClip | Qt::AlignTop | Qt::AlignLeft, text);
    return true;
}

// GDRAWLINE：C# GraphicsImage.GDrawLine —— 用该 G 的画笔（GSETPEN 的色+宽；
// GDASHSTYLE 的虚线/端点），未设置时用兜底色、宽 1（C# Config.ForeColor）。
bool GraphicsStore::gDrawLine(int id, int x1, int y1, int x2, int y2,
                               const QColor& fallbackColor) {
    auto it = gImages().find(id);
    if (it == gImages().end() || it->isNull()) return false;
    const PenState ps = gExtra().pen.value(id);
    QPen pen(ps.valid ? ps.color : fallbackColor);
    pen.setWidth(ps.valid ? qMax(1, ps.width) : 1);
    // System.Drawing.Drawing2D.DashStyle：0=Solid 1=Dash 2=Dot 3=DashDot 4=DashDotDot
    switch (ps.dashStyle) {
    case 1: pen.setStyle(Qt::DashLine); break;
    case 2: pen.setStyle(Qt::DotLine); break;
    case 3: pen.setStyle(Qt::DashDotLine); break;
    case 4: pen.setStyle(Qt::DashDotDotLine); break;
    default: pen.setStyle(Qt::SolidLine); break;
    }
    // DashCap：0=Flat 2=Round 3=Triangle（GDI+ 枚举，1 未定义）
    if (ps.dashCap == 2) pen.setCapStyle(Qt::RoundCap);
    else if (ps.dashCap == 3) pen.setCapStyle(Qt::SquareCap);
    else pen.setCapStyle(Qt::FlatCap);
    QPainter painter(&*it);
    painter.setPen(pen);
    // Qt 的线段栅格化**不含末端端点像素**（实测 (0,16)-(31,16) 只画到 x=30）；
    // GDI+ DrawLine 两端都画。把线段沿方向延长半像素补上末端端点
    //（宽笔的端帽形状不受影响 —— 延长量远小于线宽）。
    QLineF line(x1, y1, x2, y2);
    const qreal length = std::hypot(line.dx(), line.dy());
    if (length > 0) line.setLength(length + 0.5);
    painter.drawLine(line);
    return true;
}

// GDASHSTYLE：只改画笔的虚线样式/端点；未 GSETPEN 过时不创建具色画笔
//（颜色留给 GDRAWLINE 的兜底 = Config.ForeColor），与 C# 的「pen==null 时先 new
// Pen(Config.ForeColor)」在**最终画出的线**上等价。
bool GraphicsStore::gDashStyle(int id, int dashStyle, int dashCap) {
    if (!gImages().contains(id)) return false;
    PenState& ps = gExtra().pen[id];
    ps.dashStyle = dashStyle;
    ps.dashCap = dashCap;
    return true;
}

// GDRAWGWITHROTATE：C# `Translate(-px,-py, Append); Rotate(angle, Append);
// Translate(px,py, Append); DrawImage(src, 0,0)` —— 组合起来就是**绕 (px,py)
// 旋转**。QPainter 的坐标系操作同序可表达：translate(P) · rotate · translate(-P)。
bool GraphicsStore::gDrawGWithRotate(int dstId, int srcId, double angle, int px, int py) {
    auto it = gImages().find(dstId);
    const QImage src = gImage(srcId);
    if (it == gImages().end() || it->isNull() || src.isNull()) return false;
    QPainter painter(&*it);
    painter.translate(px, py);
    painter.rotate(angle);
    painter.translate(-px, -py);
    painter.drawImage(QPoint(0, 0), src);
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
    // 对齐 C# GraphicsDrawGWithMaskMethod（= Command.html 的失败条件）：
    //   * srcID 与 maskID 的宽高必须完全一致；
    //   * 绘制区域不得超出 destID。
    if (src.width() != mask.width() || src.height() != mask.height()) return false;
    if (dx + src.width() > dst.width() || dy + src.height() > dst.height()) return false;
    const QImage s = src.convertToFormat(QImage::Format_ARGB32);
    const QImage m = mask.convertToFormat(QImage::Format_ARGB32);
    QImage out = dst.convertToFormat(QImage::Format_ARGB32);
    // 文档：掩码图像的**蓝色值**作为不透明度应用到源图（纯白=原样、纯黑=全透明）。
    // C# GraphicsImage.GDrawGWithMask 用的就是 BGRA 的首字节（= 蓝），并做
    // (mask+1)/256 近似混合 —— 这里逐像素对齐。
    const auto mix = [](int srcC, int dstC, int a256) {
        return (srcC * a256 + dstC * (256 - a256)) >> 8;
    };
    for (int y = 0; y < s.height(); ++y) {
        const QRgb* mline = reinterpret_cast<const QRgb*>(m.constScanLine(y));
        const QRgb* sline = reinterpret_cast<const QRgb*>(s.constScanLine(y));
        QRgb* oline = reinterpret_cast<QRgb*>(out.scanLine(dy + y));
        for (int x = 0; x < s.width(); ++x) {
            const int maskv = qBlue(mline[x]);
            if (maskv == 0) continue;                 // 掩码蓝=0 -> 完全透明，保留 dst
            QRgb* d = &oline[dx + x];
            if (maskv == 255) { *d = sline[x]; continue; }
            const int a256 = maskv + 1;
            *d = qRgba(mix(qRed(sline[x]), qRed(*d), a256),
                       mix(qGreen(sline[x]), qGreen(*d), a256),
                       mix(qBlue(sline[x]), qBlue(*d), a256),
                       mix(qAlpha(sline[x]), qAlpha(*d), a256));
        }
    }
    gImages().insert(dstId, out);
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
    // C# CBGSetCIMGMethod 用 AppContents.GetSprite(name) —— imageDictionary 里
    // 既有运行期精灵也有 resources/CSV 静态资源，所以这里同样接受静态资源。
    if (!spriteCreated(sprite) && !ResourceImageProvider::hasResource(sprite)) return false;
    CbgLayer layer;
    layer.isSprite = true; layer.sprite = sprite.toUpper();
    layer.x = x; layer.y = y; layer.z = z;
    gExtra().cbg.append(layer);
    return true;
}

bool GraphicsStore::cbgSetButtonSprite(qint64 value, const QString& sprite,
                                       const QString& selectedSprite, int x, int y, int z,
                                       const QString& tooltip) {
    // C# CBGSetButtonSpriteMethod：只校验按钮值范围 0..0xFFFFFF，**不校验精灵是否存在**
    // （`spriteName` / `spriteNameB` 允许空串 —— 空串/未知名的层在绘制时跳过）。
    if (value < 0 || value > 0xFFFFFF) return false;
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
    // 对齐 C# CBG_ClearRange：zmin > zmax 什么都不做；zdepth == 0 的层（占位）不删。
    // 注意 C# 的 CBGREMOVERANGE 命令**恒返回 1**（与本函数的返回值无关，
    // 由 expression_evaluator 固定给出）。
    if (zMin > zMax) return false;
    auto& cbg = gExtra().cbg;
    const int before = cbg.size();
    for (int i = cbg.size() - 1; i >= 0; --i) {
        const int z = cbg.at(i).z;
        if (z != 0 && z >= zMin && z <= zMax) cbg.removeAt(i);
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
    spritePosOverrides().clear();
    gExtra().pen.clear();
    gExtra().brush.clear();
    gExtra().font.clear();
    gExtra().cbg.clear();
    gExtra().anime.clear();
}
