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
#include "resource_image_provider.h"

#include "graphics_store.h"
#include "eraengine_log.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QSize>
#include <QStringList>
#include <QUrl>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

QString ResourceImageProvider::s_root;
QHash<QString, ResourceImageProvider::Sprite> ResourceImageProvider::s_atlas;
QString ResourceImageProvider::s_atlasRoot;

ResourceImageProvider::ResourceImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

void ResourceImageProvider::setRoot(const QString& dir) {
    const QString normalized = QDir(dir).absolutePath();
    if (s_root == normalized) return;
    s_root = normalized;
    s_atlas.clear();
    s_atlasRoot.clear();
}

void ResourceImageProvider::ensureAtlasLoaded(const QString& root) {
    const QString base = root.isEmpty() ? s_root : QDir(root).absolutePath();
    if (base.isEmpty() || s_atlasRoot == base) return;

    s_atlas.clear();
    s_atlasRoot = base;
    // 对齐 C# AppContents.LoadContents：递归扫描 resources/ 下**所有** csv
    // （eraTW 的 差し替え.csv / 39_コマンド.csv 等也注册精灵；
    //   只读 list.csv 会让 SPRITECREATED("55_A1") 恒假 -> 立绘合成被跳过）。
    // 源图片路径相对于**该 csv 所在目录**（C# 用 csv 的目录拼接）。
    const QString resDir = QDir(base).filePath(QStringLiteral("resources"));
    QDirIterator csvIt(resDir, QStringList{QStringLiteral("*.csv")},
                       QDir::Files, QDirIterator::Subdirectories);
    while (csvIt.hasNext()) {
        QFile file(csvIt.next());
        // 源文件相对路径：resources/ 之下的 csv 子目录前缀
        QString relDir = QDir(resDir).relativeFilePath(QFileInfo(csvIt.fileInfo()).absolutePath());
        if (relDir == QLatin1String(".")) relDir.clear();
        else relDir += QLatin1Char('/');
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        while (!file.atEnd()) {
            QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.startsWith(QChar(0xFEFF))) line.remove(0, 1);
            if (line.isEmpty() || line.startsWith(QLatin1Char(';'))
                || line.startsWith(QLatin1Char('#'))) continue;
            const QStringList fields = line.split(QLatin1Char(','));
            if (fields.size() < 2) continue;
            const QString name = fields.at(0).trimmed();
            const QString source = fields.at(1).trimmed();
            if (name.isEmpty() || source.isEmpty()) continue;

            Sprite sprite;
            sprite.sourceFile = relDir + source;
            if (fields.size() >= 6) {
                bool okX = false, okY = false, okW = false, okH = false;
                sprite.x = fields.at(2).trimmed().toInt(&okX);
                sprite.y = fields.at(3).trimmed().toInt(&okY);
                sprite.w = fields.at(4).trimmed().toInt(&okW);
                sprite.h = fields.at(5).trimmed().toInt(&okH);
                sprite.hasRect = okX && okY && okW && okH && sprite.w > 0 && sprite.h > 0;
            }
            // 第 7/8 列 = 输出偏移（C# 在同一层里读 tokens[6],tokens[7]）。
            // 只有两列都能解析才算数（eraTW 里大量行以逗号结尾 -> 列数是 7，
            // 偏移缺席，按 (0,0) 处理）。
            if (fields.size() >= 8) {
                bool okOX = false, okOY = false;
                const int ox = fields.at(6).trimmed().toInt(&okOX);
                const int oy = fields.at(7).trimmed().toInt(&okOY);
                if (okOX && okOY) {
                    sprite.hasOffset = true;
                    sprite.offsetX = ox;
                    sprite.offsetY = oy;
                }
            }
            s_atlas.insert(name, sprite);
        }
    }
    qDebug() << "[load] 图集清单" << resDir << "->" << s_atlas.size() << "项";
}

QString ResourceImageProvider::root() {
    return s_root;
}

namespace {

// 去掉 image:// 查询串与首尾空白
QString normalizeId(QString id) {
    const int q = id.indexOf(QLatin1Char('?'));
    if (q >= 0) id.truncate(q);
    // QML percent-encodes non-ASCII resource names in image provider IDs.
    id = QUrl::fromPercentEncoding(id.toUtf8()).trimmed();
    id.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (id.startsWith(QLatin1Char('/'))) id.remove(0, 1);
    return id;
}

bool hasExtension(const QString& name) {
    const int slash = name.lastIndexOf(QLatin1Char('/'));
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    return dot > slash;
}

quint32 le32(const QByteArray& d, int off) {
    if (off < 0 || off + 4 > d.size()) return 0;
    return quint32(quint8(d.at(off))) | (quint32(quint8(d.at(off + 1))) << 8)
           | (quint32(quint8(d.at(off + 2))) << 16) | (quint32(quint8(d.at(off + 3))) << 24);
}
quint32 be32(const QByteArray& d, int off) {
    if (off < 0 || off + 4 > d.size()) return 0;
    return (quint32(quint8(d.at(off))) << 24) | (quint32(quint8(d.at(off + 1))) << 16)
           | (quint32(quint8(d.at(off + 2))) << 8) | quint32(quint8(d.at(off + 3)));
}

// 从文件头解析图片尺寸（Qt 解不了的图也要能排版）。成功返回 true。
// 支持 WebP(VP8/VP8L/VP8X) / PNG / JPEG / BMP / GIF。
bool parseImageSizeFromHeader(const QByteArray& d, int& width, int& height) {
    const auto at = [&d](int i) { return i < d.size() ? quint8(d.at(i)) : 0; };
    if (d.size() >= 16 && d.startsWith(QByteArrayLiteral("RIFF"))
        && d.mid(8, 4) == QByteArrayLiteral("WEBP")) {
        const QByteArray fourcc = d.mid(12, 4);
        if (fourcc == QByteArrayLiteral("VP8 ")) {
            // 有损：帧头在 payload[6..9]（宽 14 位 + 高 14 位，小端）
            width = int((at(26) | (at(27) << 8)) & 0x3FFF);
            height = int((at(28) | (at(29) << 8)) & 0x3FFF);
            return width > 0 && height > 0;
        }
        if (fourcc == QByteArrayLiteral("VP8L")) {
            // 无损：payload[0] 是签名 0x2f，随后 14 位宽-1、14 位高-1（LSB first）
            const quint32 v = at(21) | (quint32(at(22)) << 8) | (quint32(at(23)) << 16)
                              | (quint32(at(24)) << 24);
            width = int(v & 0x3FFF) + 1;
            height = int((v >> 14) & 0x3FFF) + 1;
            return true;
        }
        if (fourcc == QByteArrayLiteral("VP8X")) {
            // 扩展：payload[4..6] = 宽-1，payload[7..9] = 高-1（24 位小端）
            width = int(quint32(at(24)) | (quint32(at(25)) << 8) | (quint32(at(26)) << 16)) + 1;
            height = int(quint32(at(27)) | (quint32(at(28)) << 8) | (quint32(at(29)) << 16)) + 1;
            return true;
        }
        return false;
    }
    if (d.size() >= 24 && at(0) == 0x89 && at(1) == 'P' && at(2) == 'N' && at(3) == 'G') {
        width = int(be32(d, 16));
        height = int(be32(d, 20));
        return width > 0 && height > 0;
    }
    if (d.size() >= 10 && (d.startsWith(QByteArrayLiteral("GIF87a"))
                           || d.startsWith(QByteArrayLiteral("GIF89a")))) {
        width = at(6) | (at(7) << 8);
        height = at(8) | (at(9) << 8);
        return width > 0 && height > 0;
    }
    if (d.size() >= 26 && d.startsWith(QByteArrayLiteral("BM"))) {
        width = int(qint32(le32(d, 18)));
        height = qAbs(int(qint32(le32(d, 22))));
        return width > 0 && height > 0;
    }
    if (d.size() >= 4 && at(0) == 0xFF && at(1) == 0xD8) {
        // JPEG：扫 SOFn（C0..CF，去掉 C4/C8/CC）里的高/宽（大端 16 位）
        int i = 2;
        while (i + 9 < d.size()) {
            if (at(i) != 0xFF) { ++i; continue; }
            const int marker = at(i + 1);
            if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8
                && marker != 0xCC) {
                height = int((at(i + 5) << 8) | at(i + 6));
                width = int((at(i + 7) << 8) | at(i + 8));
                return width > 0 && height > 0;
            }
            const int len = (at(i + 2) << 8) | at(i + 3);
            if (len < 2) return false;
            i += 2 + len;
        }
        return false;
    }
    return false;
}

} // namespace

QImage ResourceImageProvider::loadImageFile(const QString& path) {
    QImage image;
    if (path.isEmpty()) return image;
    if (image.load(path) && !image.isNull()) return image;

    // 解码失败 -> 按文件头补一个同尺寸的全透明图（见头文件里的 ダミー.webp 说明）。
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QImage();
    const QByteArray head = file.read(64);   // 头解析最多用到 ~30 字节
    int w = 0, h = 0;
    if (!parseImageSizeFromHeader(head, w, h) || w <= 0 || h <= 0 || w > 8192 || h > 8192)
        return QImage();
    qCDebug(eraTrace) << "[load] 图片解码失败，按文件头尺寸回退" << path << QSize(w, h);
    image = QImage(w, h, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    return image;
}

QString ResourceImageProvider::resolvePath(const QString& id, const QString& root) {
    const QString name = normalizeId(id);
    if (name.isEmpty()) {
        return QString();
    }
    const QString base = root.isEmpty() ? s_root : root;
    if (base.isEmpty()) {
        return QString();
    }

    const QStringList exts = hasExtension(name)
                                 ? QStringList{QString()}
                                 : QStringList{QStringLiteral(".png"), QStringLiteral(".jpg"),
                                               QStringLiteral(".jpeg"), QStringLiteral(".bmp"),
                                               QStringLiteral(".webp"), QStringLiteral(".gif")};
    const QStringList dirs = {QDir(base).filePath(QStringLiteral("resources")), base};

    for (const QString& dir : dirs) {
        for (const QString& ext : exts) {
            const QString candidate = QDir(dir).filePath(name + ext);
            if (QFileInfo::exists(candidate)) {
                return QFileInfo(candidate).absoluteFilePath();
            }
        }
    }
    return QString();
}

bool ResourceImageProvider::hasResource(const QString& id) {
    const QString normalized = normalizeId(id);
    if (normalized.isEmpty()) return false;
    ensureAtlasLoaded(s_root);
    if (s_atlas.contains(normalized)) return true;
    return !resolvePath(normalized).isEmpty();
}

// 资源图片固有尺寸：优先用清单里的矩形（图集裁剪后的真实尺寸，无需解码图片），
// 否则退回加载文件取原始尺寸。只用于排版测量；失败不影响实际渲染。
bool ResourceImageProvider::intrinsicSize(const QString& id, int& width, int& height) {
    const QString normalized = normalizeId(id);
    ensureAtlasLoaded(s_root);

    // 运行期精灵（SPRITECREATE 的产物）优先 —— 对齐 C# AppContents.GetSprite：
    // 精灵名覆盖同名静态资源。
    {
        const QImage runtimeSprite = GraphicsStore::spriteImage(normalized);
        if (!runtimeSprite.isNull()) {
            width = runtimeSprite.width();
            height = runtimeSprite.height();
            return true;
        }
    }

    const auto atlasIt = s_atlas.constFind(normalized);
    if (atlasIt != s_atlas.constEnd()) {
        const Sprite& sprite = atlasIt.value();
        if (sprite.hasRect && sprite.w > 0 && sprite.h > 0) {
            width = sprite.w;
            height = sprite.h;
            return true;
        }
        const QString source = QDir(s_root).filePath(
            QStringLiteral("resources/%1").arg(sprite.sourceFile));
        const QImage image = loadImageFile(source);
        if (!image.isNull()) {
            width = image.width();
            height = image.height();
            return true;
        }
        return false;
    }

    const QString path = resolvePath(normalized);
    if (path.isEmpty()) return false;
    const QImage image = loadImageFile(path);
    if (image.isNull()) return false;
    width = image.width();
    height = image.height();
    return true;
}

bool ResourceImageProvider::spriteOffset(const QString& id, int& x, int& y) {
    const QString normalized = normalizeId(id);
    ensureAtlasLoaded(s_root);
    const auto it = s_atlas.constFind(normalized);
    if (it == s_atlas.constEnd()) return false;
    x = it.value().offsetX;
    y = it.value().offsetY;
    return true;
}


// 仅按静态资源（图集 / 文件）取图，不含运行期精灵回退。
// GraphicsStore::spriteImage 依赖它做「静态资源即精灵」的兜底，
// 两者不得互相递归。
QImage ResourceImageProvider::loadResourceImage(const QString& id) {
    const QString normalizedId = normalizeId(id);
    ensureAtlasLoaded(s_root);

    QImage image;
    const auto atlasIt = s_atlas.constFind(normalizedId);
    if (atlasIt != s_atlas.constEnd()) {
        const Sprite& sprite = atlasIt.value();
        const QString source = QDir(s_root).filePath(QStringLiteral("resources/%1").arg(sprite.sourceFile));
        image = loadImageFile(source);
        if (!image.isNull() && sprite.hasRect) {
            const QRect rect(sprite.x, sprite.y, sprite.w, sprite.h);
            if (rect.left() < image.width() && rect.top() < image.height())
                image = image.copy(rect.intersected(image.rect()));
            else
                image = QImage();
        }
    } else {
        const QString path = resolvePath(normalizedId);
        // 与图集分支一致：走 loadImageFile，Qt 解不了的图片（eraTW 的
        // ダミー.webp：VP8L 全透明，libqwebp 拒读）按文件头尺寸回退，
        // 否则 QML 的 <img src='ダミー.webp'> 会退化成 AltText。
        if (!path.isEmpty()) image = loadImageFile(path);
    }
    return image;
}

QImage ResourceImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    const QString normalizedId = normalizeId(id);
    ensureAtlasLoaded(s_root);

    QImage image;
    // 运行期精灵优先（SPRITECREATE 产物，同名覆盖静态资源）
    image = GraphicsStore::spriteImage(normalizedId);
    if (!image.isNull()) {
        if (size) *size = image.size();
        if (requestedSize.isValid() && !requestedSize.isEmpty())
            image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return image;
    }

    image = loadResourceImage(normalizedId);

    if (image.isNull()) {
        qCDebug(eraTrace) << "[render] 图片未命中" << normalizedId;
        if (size) *size = QSize();
        return QImage();
    }
    qCDebug(eraTrace) << "[render] 图片" << normalizedId << image.size();
    if (size) *size = image.size();
    if (requestedSize.isValid() && !requestedSize.isEmpty())
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}
