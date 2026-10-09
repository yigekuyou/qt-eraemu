#ifndef GAME_LIBRARY_H
#define GAME_LIBRARY_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Paths in games are opaque QFileInfo paths: never rebuild a content URI from
// a display name. selectGame validates asynchronously; prepareGame returns the original path.
class GameLibrary : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString scanRoot READ scanRoot WRITE setScanRoot NOTIFY scanRootChanged FINAL)
    Q_PROPERTY(QVariantList games READ games NOTIFY gamesChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)

public:
    explicit GameLibrary(QObject* parent = nullptr);
    [[nodiscard]] QString scanRoot() const { return m_scanRoot; }
    void setScanRoot(const QString& root);
    [[nodiscard]] QVariantList games() const { return m_games; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] bool busy() const { return m_busy; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString prepareGame(const QString& path);
    Q_INVOKABLE void selectGame(const QString& path);

Q_SIGNALS:
    void scanRootChanged();
    void gamesChanged();
    void errorChanged();
    void busyChanged();
    void gameReady(const QString& path);

private:
    bool accepts(const QString& path) const;
    void setError(const QString& message);
    void setBusy(bool busy);
    QString m_scanRoot;
    QVariantList m_games;
    QString m_error;
    bool m_busy = false;
};

#endif // GAME_LIBRARY_H
