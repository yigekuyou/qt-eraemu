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

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSize>
#include <QStringList>

QString ResourceImageProvider::s_root;

ResourceImageProvider::ResourceImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

void ResourceImageProvider::setRoot(const QString& dir) {
    s_root = dir;
}

QString ResourceImageProvider::root() {
    return s_root;
}

namespace {

// 去掉 image:// 查询串与首尾空白
QString normalizeId(QString id) {
    const int q = id.indexOf(QLatin1Char('?'));
    if (q >= 0) id.truncate(q);
    id = id.trimmed();
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
    const QString path = resolvePath(id);
    if (path.isEmpty()) {
        if (size) *size = QSize();
        return QImage();
    }
    QImage image(path);
    if (image.isNull()) {
        if (size) *size = QSize();
        return QImage();
    }
    if (size) {
        *size = image.size();
    }
    if (requestedSize.isValid() && !requestedSize.isEmpty()) {
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}
