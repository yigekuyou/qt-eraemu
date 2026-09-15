#include "config_loader.h"
#include <QFile>
#include <QTextStream>
#include <QDir>

ConfigLoader::ConfigLoader(QObject *parent)
    : QObject(parent)
{
}

bool ConfigLoader::loadConfigFile(const QString& filePath, int precedence)
{
    ConfigFile config;
    config.filePath = filePath;
    config.precedence = precedence;
    
    parseConfigFile(filePath, config.config);
    m_configFiles.append(config);
    return true;
}

void ConfigLoader::mergeConfig(const QString& filePath, int precedence)
{
    ConfigFile config;
    config.filePath = filePath;
    config.precedence = precedence;
    
    parseConfigFile(filePath, config.config);
    m_configFiles.append(config);
}

void ConfigLoader::parseConfigFile(const QString& filePath, QHash<QString, QString>& config)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to open config file for parsing:" << filePath;
        return;
    }
    
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        
        // Skip empty lines and comments
        if (line.isEmpty() || line.startsWith(';') || line.startsWith("//")) {
            continue;
        }
        
        // Parse key=value pairs
        int eqPos = line.indexOf('=');
        if (eqPos > 0) {
            QString key = line.left(eqPos).trimmed();
            QString value = line.mid(eqPos + 1).trimmed();
            
            // Remove quotes from value if present
            if (value.startsWith('"') && value.endsWith('"')) {
                value = value.mid(1, value.length() - 2);
            }
            
            config[key] = value;
        }
    }
    
    file.close();
}

QString ConfigLoader::getConfig(const QString& key) const
{
    // Search from highest precedence to lowest
    for (int i = m_configFiles.size() - 1; i >= 0; i--) {
        const ConfigFile& config = m_configFiles[i];
        if (config.config.contains(key)) {
            return config.config[key];
        }
    }
    return QString();  // Return empty string if not found
}

void ConfigLoader::setConfig(const QString& key, const QString& value)
{
    // Add to a new config file with highest precedence (precedence = -1)
    ConfigFile config;
    config.filePath = "user_override";
    config.precedence = -1;
    config.config[key] = value;
    m_configFiles.prepend(config);
		emit configChanged(key, value);
}

bool ConfigLoader::hasConfig(const QString& key) const
{
    for (const ConfigFile& config : m_configFiles) {
        if (config.config.contains(key)) {
            return true;
        }
    }
    return false;
}

void ConfigLoader::setConfigPrecedence(const QString& filePath, int precedence)
{
    for (ConfigFile& config : m_configFiles) {
        if (config.filePath == filePath) {
            config.precedence = precedence;
            break;
        }
    }
}

QList<ConfigFile> ConfigLoader::getConfigFiles() const
{
    return m_configFiles;
}
