#include "android_saf_paths.h"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJniObject>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace AndroidSafPaths {

QString join(const QString& base, const QString& relative)
{
    if (relative.startsWith(QStringLiteral("content://"))) return relative;
    if (!base.startsWith(QStringLiteral("content://"))) return QDir(base).filePath(relative);
    QString current = base;
    const auto parts = QString(relative).replace(u'\\', u'/').split(u'/', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (part == QStringLiteral(".")) continue;
        if (part == QStringLiteral("..")) return {};
        const QString appended = current + u'/' + QString::fromLatin1(QUrl::toPercentEncoding(part));
        if (QFileInfo::exists(appended)) {
            current = appended;
            continue;
        }
        QString child;
        const auto entries = QDir(current).entryInfoList(QDir::AllEntries | QDir::Hidden
                                                         | QDir::System | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries) {
            if (entry.fileName().compare(part, Qt::CaseInsensitive) == 0) {
                child = entry.absoluteFilePath();
                break;
            }
        }
        current = child.isEmpty() ? appended : child;
    }
    return current;
}

static bool writeGranted(const QString& game)
{
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const auto gameUri = QJniObject::callStaticObjectMethod(
        "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
        QJniObject::fromString(game).object<jstring>());
    if (context.callMethod<jint>("checkCallingOrSelfUriPermission",
            "(Landroid/net/Uri;I)I", gameUri.object<jobject>(), jint(2)) != 0)
        return false;
    const auto resolver = context.callObjectMethod("getContentResolver",
                                                   "()Landroid/content/ContentResolver;");
    const auto grants = resolver.callObjectMethod("getPersistedUriPermissions",
                                                  "()Ljava/util/List;");
    const auto treeId = [](const QJniObject& uri) {
        if (!QJniObject::callStaticMethod<jboolean>("android/provider/DocumentsContract",
                "isTreeUri", "(Landroid/net/Uri;)Z", uri.object<jobject>()))
            return QString();
        return QJniObject::callStaticObjectMethod("android/provider/DocumentsContract",
                "getTreeDocumentId", "(Landroid/net/Uri;)Ljava/lang/String;",
                uri.object<jobject>()).toString();
    };
    const QString gameTree = treeId(gameUri);
    const QString authority = gameUri.callObjectMethod("getAuthority",
                                                        "()Ljava/lang/String;").toString();
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
    return false;
}

QString storageRoot(const QString& game)
{
    if (!game.startsWith(QStringLiteral("content://"))) return game;
    if (writeGranted(game)) {
        const QString sav = join(game, QStringLiteral("sav"));
        if (QDir().mkpath(sav)) {
            const QString probe = join(sav, QStringLiteral(".emuera-probe-")
                                             + QUuid::createUuid().toString(QUuid::Id128));
            QFile file(probe);
            if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                file.close();
                if (file.remove()) return game;
            }
        }
    }
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(
        game.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString root = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation)).filePath(QStringLiteral("save-data/") + hash);
    QDir().mkpath(root + QStringLiteral("/sav"));
    return root;
}
}
#else
namespace AndroidSafPaths {
QString join(const QString& base, const QString& relative) { return QDir(base).filePath(relative); }
QString storageRoot(const QString& game) { return game; }
}
#endif
