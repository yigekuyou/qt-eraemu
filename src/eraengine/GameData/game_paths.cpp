#include "game_paths.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
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
    if (isContent(relative)) return relative;
    if (!isContent(base)) return QDir(base).filePath(relative);
    QString current = base;
    const auto parts = QString(relative).replace(u'\\', u'/').split(u'/', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (part == QLatin1String(".")) continue;
        // A provider document has no portable parent operation. Never escape
        // the authorized resource directory through a script path.
        if (part == QLatin1String("..")) return {};
        QString child;
        const auto entries = QDir(current).entryInfoList(QDir::AllEntries | QDir::Hidden
                                  | QDir::System | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries) {
            if (entry.fileName() == part) { child = entry.absoluteFilePath(); break; }
            if (entry.fileName().compare(part, Qt::CaseInsensitive) == 0)
                child = entry.absoluteFilePath();
        }
        // Qt 6.11's file engine accepts appended paths for creation. Encode
        // literal %, #, spaces and Unicode so document names remain intact.
        current = child.isEmpty() ? current + u'/' + QString::fromLatin1(QUrl::toPercentEncoding(part)) : child;
    }
    return current;
}
static bool writeGranted(const QString& game)
{
#ifdef Q_OS_ANDROID
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const auto gameUri = QJniObject::callStaticObjectMethod(
        "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString(game).object<jstring>());
    // Let Android resolve tree grants and opaque IDs, rather than infer
    // permission from a string prefix. WRITE is Intent's native flag 0x2.
    if (context.callMethod<jint>("checkCallingOrSelfUriPermission",
            "(Landroid/net/Uri;I)I", gameUri.object<jobject>(), jint(2)) != 0)
        return false;
    const auto resolver = context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
    const auto grants = resolver.callObjectMethod("getPersistedUriPermissions", "()Ljava/util/List;");
    const auto treeId = [](const QJniObject& uri) {
        if (!QJniObject::callStaticMethod<jboolean>("android/provider/DocumentsContract",
                "isTreeUri", "(Landroid/net/Uri;)Z", uri.object<jobject>()))
            return QString();
        return QJniObject::callStaticObjectMethod("android/provider/DocumentsContract",
                "getTreeDocumentId", "(Landroid/net/Uri;)Ljava/lang/String;",
                uri.object<jobject>()).toString();
    };
    const QString gameTree = treeId(gameUri);
    const QString authority = gameUri.callObjectMethod("getAuthority", "()Ljava/lang/String;").toString();
    const jint count = grants.callMethod<jint>("size");
    for (jint i = 0; i < count; ++i) {
        const auto grant = grants.callObjectMethod("get", "(I)Ljava/lang/Object;", i);
        if (!grant.callMethod<jboolean>("isWritePermission")) continue;
        const auto uri = grant.callObjectMethod("getUri", "()Landroid/net/Uri;");
        if (uri.toString() == game) return true;
        if (!gameTree.isEmpty() && treeId(uri) == gameTree
                && uri.callObjectMethod("getAuthority", "()Ljava/lang/String;").toString() == authority)
            return true;
    }
#else
    Q_UNUSED(game);
#endif
    return false;
}
QString storageRoot(const QString& game)
{
    if (!isContent(game)) return game;
    if (writeGranted(game)) {
        const QString sav = join(game, QStringLiteral("sav"));
        if (QDir().mkpath(sav)) {
            const QString probe = join(sav, QStringLiteral(".emuera-probe-") + QUuid::createUuid().toString(QUuid::Id128));
            QFile file(probe);
            if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                file.close();
                if (file.remove()) return game;
            }
        }
    }
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(game.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString root = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("save-data/") + hash);
    QDir().mkpath(root + QStringLiteral("/sav"));
    return root;
}
}
