#ifndef GAME_BASE_DATA_H
#define GAME_BASE_DATA_H

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QQmlEngine>

class GameBaseData : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY dataChanged)
    Q_PROPERTY(QString title READ title NOTIFY dataChanged)
    Q_PROPERTY(QString author READ author NOTIFY dataChanged)
    Q_PROPERTY(QString version READ version NOTIFY dataChanged)
    Q_PROPERTY(QString releaseYear READ releaseYear NOTIFY dataChanged)
    Q_PROPERTY(QString additionalInfo READ additionalInfo NOTIFY dataChanged)

public:
    explicit GameBaseData(QObject *parent = nullptr);
    
    // Property getters
    QString windowTitle() const;
    QString title() const;
    QString author() const;
    QString version() const;
    QString releaseYear() const;
    QString additionalInfo() const;
    
    // Data manipulation
    void set(const QString& key, const QString& value);
    QString get(const QString& key) const;
    QVariantMap toMap() const;

signals:
    void dataChanged();

private:
    QString m_windowTitle;
    QString m_title;
    QString m_author;
    QString m_version;
    QString m_releaseYear;
    QString m_additionalInfo;
};

#endif // GAME_BASE_DATA_H
