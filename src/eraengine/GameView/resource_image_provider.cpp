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

#include "eraengine_log.h"
#include <QDir>
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
    const QString csvPath = QDir(base).filePath(QStringLiteral("resources/list.csv"));
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

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
        sprite.sourceFile = source;
        if (fields.size() >= 6) {
            bool okX = false, okY = false, okW = false, okH = false;
            sprite.x = fields.at(2).trimmed().toInt(&okX);
            sprite.y = fields.at(3).trimmed().toInt(&okY);
            sprite.w = fields.at(4).trimmed().toInt(&okW);
            sprite.h = fields.at(5).trimmed().toInt(&okH);
            sprite.hasRect = okX && okY && okW && okH && sprite.w > 0 && sprite.h > 0;
        }
        s_atlas.insert(name, sprite);
    }
    qDebug() << "[load] 图集清单" << csvPath << "->" << s_atlas.size() << "项";
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

} // namespace

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

QImage ResourceImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    const QString normalizedId = normalizeId(id);
    ensureAtlasLoaded(s_root);

    QImage image;
    const auto atlasIt = s_atlas.constFind(normalizedId);
    if (atlasIt != s_atlas.constEnd()) {
        const Sprite& sprite = atlasIt.value();
        const QString source = QDir(s_root).filePath(QStringLiteral("resources/%1").arg(sprite.sourceFile));
        image.load(source);
        if (!image.isNull() && sprite.hasRect) {
            const QRect rect(sprite.x, sprite.y, sprite.w, sprite.h);
            if (rect.left() < image.width() && rect.top() < image.height())
                image = image.copy(rect.intersected(image.rect()));
            else
                image = QImage();
        }
    } else {
        const QString path = resolvePath(normalizedId);
        if (!path.isEmpty()) image.load(path);
    }

    if (image.isNull()) {
        qCDebug(eraTrace) << "[render] 图片未命中" << normalizedId
                 << "（图集" << (atlasIt != s_atlas.constEnd() ? "有此项但取图失败" : "无此项") << "）";
        if (size) *size = QSize();
        return QImage();
    }
    qCDebug(eraTrace) << "[render] 图片" << normalizedId << image.size()
             << (atlasIt != s_atlas.constEnd() ? "(图集)" : "(独立文件)");
    if (size) *size = image.size();
    if (requestedSize.isValid() && !requestedSize.isEmpty())
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}
