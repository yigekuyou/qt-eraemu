#include "game_paths.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#ifdef Q_OS_ANDROID
#include "android_saf_paths.h"
#endif

namespace GamePaths {
bool isContent(const QString& path) { return path.startsWith(QLatin1String("content://")); }
QString normalize(const QString& path)
{
    if (isContent(path)) return path;
    const QUrl url(path);
    return url.isLocalFile() ? url.toLocalFile() : path;
}
QString join(const QString& base, const QString& relative)
{
    if (isContent(base)) {
        const QString normalized = QString(relative).replace(u'\\', u'/');
        if (normalized.split(u'/', Qt::SkipEmptyParts).contains(QStringLiteral("..")))
            return {};
#ifdef Q_OS_ANDROID
        return AndroidSafPaths::join(base, relative);
#else
        return base + QLatin1Char('/') + normalized;
#endif
    }
#ifdef Q_OS_ANDROID
    if (isContent(relative)) return AndroidSafPaths::join(base, relative);
#endif
    if (isContent(relative)) return relative;
    return QDir(base).filePath(relative);
}
QString storageRoot(const QString& game)
{
#ifdef Q_OS_ANDROID
    if (isContent(game)) return AndroidSafPaths::storageRoot(game);
#endif
    if (!isContent(game)) return game;
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(
        game.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString root = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation)).filePath(QStringLiteral("save-data/") + hash);
    QDir().mkpath(root + QStringLiteral("/sav"));
    return root;
}
}
