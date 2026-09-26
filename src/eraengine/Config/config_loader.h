#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QList>

// Config file with precedence level
struct ConfigFile {
    QString filePath;
    int precedence;  // Lower number = lower priority
    bool isFixed;    // True if this is a fixed config (e.g., _fixed.config)
    QHash<QString, QString> config;
};

class ConfigLoader : public QObject
{
    Q_OBJECT

public:
    explicit ConfigLoader(QObject *parent = nullptr);

    // Load configuration files
    bool loadConfigFile(const QString& filePath, int precedence = 0);
    void mergeConfig(const QString& filePath, int precedence = 0);

    // Config access
    QString getConfig(const QString& key) const;
    void setConfig(const QString& key, const QString& value);
    bool hasConfig(const QString& key) const;

    // Precedence handling
    void setConfigPrecedence(const QString& filePath, int precedence);
    QList<ConfigFile> getConfigFiles() const;
signals:
		void configChanged(const QString &key, const QString &value);
private:
    QList<ConfigFile> m_configFiles;

    // Helper methods
    void parseConfigFile(const QString& filePath, QHash<QString, QString>& config);
};

#endif // CONFIG_LOADER_H
