#ifndef SCRIPT_PROCESSOR_H
#define SCRIPT_PROCESSOR_H

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>

// Script entry point information
struct ScriptEntryPoint {
    QString name;        // Entry point name (e.g., "@SYSTEM", "@SYSTEM_TITLE")
    QString scriptPath;  // Path to the script file
    int lineNum;         // Line number where entry point is defined
};

class ScriptProcessor : public QObject
{
    Q_OBJECT

public:
    explicit ScriptProcessor(QObject *parent = nullptr);
    
    // Process scripts and detect entry points
    void processScripts(const QString& scriptDir);
    
    // Entry point detection
    QString findSystemEntryPoint() const;
    QString findSystemTitleEntry() const;
    QStringList findEventEntries() const;
    QStringList findAllEntryPoints() const;
    
    // Validation
    bool validateScript(const QString& scriptPath);
    
    // Get all detected entry points
    QList<ScriptEntryPoint> getEntryPoints() const;

private:
    // Helper methods
    void parseScriptFile(const QString& scriptPath);
    QString extractEntryPointName(const QString& line) const;
    
    // Entry point storage
    QList<ScriptEntryPoint> m_entryPoints;
    QHash<QString, QString> m_systemEntryPoint;  // "SYSTEM" -> script path
    QHash<QString, QString> m_systemTitleEntry;  // "SYSTEM_TITLE" -> script path
    QStringList m_eventEntries;  // List of @EVENT* script paths
};

#endif // SCRIPT_PROCESSOR_H
