#include "GameData/game_paths.h"
#include "game_library.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPair>
#include <QSettings>
#include <QUrl>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

namespace {
constexpr auto rootKey = "gameLibrary/scanRoot";
using Preparation = QPair<QString, QString>; // original path, error

bool contentPath(const QString& path)
{
    return path.startsWith(QStringLiteral("content://"), Qt::CaseInsensitive);
}

bool safeName(const QString& name)
{
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")
        || name.size() > 255 || name.contains(u'/') || name.contains(u'\\')
        || name.contains(u':'))
        return false;
    for (const QChar c : name) {
        if (c.unicode() < 32 || c.unicode() == 127)
            return false;
    }
    return true;
}

bool readableDirectory(const QString& path)
{
    const QFileInfo info(path);
    return !path.isEmpty() && info.exists() && info.isDir() && info.isReadable();
}

bool gameDirectory(const QString& path)
{
    if (!readableDirectory(path))
        return false;
    bool csv = false;
    bool erb = false;
    const auto children = QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                   QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& child : children) {
        if (!child.isReadable() || child.isSymLink())
            continue;
        csv |= child.fileName().compare(QStringLiteral("CSV"), Qt::CaseInsensitive) == 0;
        erb |= child.fileName().compare(QStringLiteral("ERB"), Qt::CaseInsensitive) == 0;
    }
    return csv && erb;
}

Preparation prepare(const QString& path)
{
    if (!gameDirectory(path))
        return {{}, QStringLiteral("The selected game is no longer readable or lacks CSV/ERB directories.")};
    return {path, {}};
}
} // namespace

GameLibrary::GameLibrary(QObject* parent) : QObject(parent)
{
    m_scanRoot = QSettings(QStringLiteral("yigekuyou"), QStringLiteral("eraemu")).value(QLatin1String(rootKey)).toString();
    refresh();
}

void GameLibrary::setError(const QString& message)
{
    if (m_error == message)
        return;
    m_error = message;
    Q_EMIT errorChanged();
}

void GameLibrary::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

void GameLibrary::setScanRoot(const QString& root)
{
    if (m_busy) {
        setError(QStringLiteral("A game is being prepared; wait before changing the library root."));
        return;
    }
    QString path = root;
    const QUrl url(root);
    if (url.isLocalFile())
        path = url.toLocalFile();
    else if (contentPath(root)) {
        if (!url.isValid() || url.host().isEmpty() || url.hasQuery() || url.hasFragment()) {
            setError(QStringLiteral("Invalid content directory URI."));
            return;
        }
        path = url.toString(QUrl::FullyEncoded);
    } else if (!url.scheme().isEmpty() && !QDir::isAbsolutePath(root)) {
        setError(QStringLiteral("Choose a local directory or an Android content directory."));
        return;
    }
    if (!readableDirectory(path)) {
        setError(QStringLiteral("The library root must be an existing readable directory."));
        return;
    }
    if (!contentPath(path))
        path = QFileInfo(path).absoluteFilePath();
    // Qt 6 Android QAndroidPlatformFileDialogHelper::handleActivityResult calls
    // takePersistableUriPermission before publishing the selected tree URI;
    // its helper persists READ, adding WRITE for AcceptSave dialogs only.
    // Resources need READ; save storage is selected separately by the engine.
    // Verified in qtbase/src/plugins/platforms/android/qandroidplatformfiledialoghelper.cpp.
    // No duplicate JNI grant is needed here. Non-picker URIs must already have
    // permission; revoked/unreadable grants are reported by directory validation.
    QSettings settings(QStringLiteral("yigekuyou"), QStringLiteral("eraemu"));
    settings.setValue(QLatin1String(rootKey), path);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setError(QStringLiteral("Cannot persist the library root."));
        return;
    }
    if (m_scanRoot != path) {
        m_scanRoot = path;
        Q_EMIT scanRootChanged();
    }
    refresh();
}

void GameLibrary::refresh()
{
    QVariantList games;
    QString error;
    if (!m_scanRoot.isEmpty()) {
        if (!readableDirectory(m_scanRoot)) {
            error = QStringLiteral("The library root is no longer readable. Choose it again.");
        } else {
            const auto directories = QDir(m_scanRoot).entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
            for (const QFileInfo& directory : directories) {
                if (directory.isSymLink() || !safeName(directory.fileName()))
                    continue;
                const QString path = directory.absoluteFilePath();
                if (gameDirectory(path))
                    games.append(QVariantMap{{QStringLiteral("name"), directory.fileName()},
                                             {QStringLiteral("path"), path}});
            }
        }
    }
    if (m_games != games) {
        m_games = games;
        Q_EMIT gamesChanged();
    }
    setError(error);
}

bool GameLibrary::accepts(const QString& path) const
{
    for (const QVariant& game : m_games) {
        if (game.toMap().value(QStringLiteral("path")).toString() == path)
            return true;
    }
    return false;
}

QString GameLibrary::prepareGame(const QString& path)
{
    if (m_busy || !accepts(path)) {
        setError(m_busy ? QStringLiteral("A game is already being prepared.")
                        : QStringLiteral("Select a game from the current library."));
        return {};
    }
    setError({});
    setBusy(true);
    const Preparation result = prepare(path);
    setError(result.second);
    setBusy(false);
    return result.first;
}

void GameLibrary::selectGame(const QString& path)
{
    if (m_busy || !accepts(path)) {
        setError(m_busy ? QStringLiteral("A game is already being prepared.")
                        : QStringLiteral("Select a game from the current library."));
        return;
    }
    setError({});
    setBusy(true);
    auto* watcher = new QFutureWatcher<Preparation>(this);
    connect(watcher, &QFutureWatcher<Preparation>::finished, this, [this, watcher] {
        const Preparation result = watcher->result();
        watcher->deleteLater();
        setError(result.second);
        setBusy(false);
        if (!result.first.isEmpty())
            Q_EMIT gameReady(result.first);
    });
    // Worker captures only values, so destroying the QML object during validation
    // cannot access freed QObject state. Notifications stay on its owning thread.
    watcher->setFuture(QtConcurrent::run([path] { return prepare(path); }));
}
