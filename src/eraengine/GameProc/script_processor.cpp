#include "script_processor.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

ScriptProcessor::ScriptProcessor(QObject *parent)
    : QObject(parent)
{
}

void ScriptProcessor::processScripts(const QString& scriptDir)
{
    QDir dir(scriptDir);
    if (!dir.exists()) {
        qDebug() << "Script directory does not exist:" << scriptDir;
        return;
    }
    
    // Find all .erb files
    QStringList erbFiles = dir.entryList(QStringList() << "*.ERB" << "*.erb", QDir::Files);
    
    for (const QString& file : erbFiles) {
        QString filePath = dir.absoluteFilePath(file);
        parseScriptFile(filePath);
    }
}

void ScriptProcessor::parseScriptFile(const QString& scriptPath)
{
    QFile file(scriptPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to open script file:" << scriptPath;
        return;
    }
    
    QTextStream in(&file);
    QString line;
    int lineNum = 0;
    
    while (!in.atEnd()) {
        line = in.readLine();
        lineNum++;
        
        // Look for entry point definitions (lines starting with @)
        if (line.startsWith('@')) {
            QString entryPoint = extractEntryPointName(line);
            if (!entryPoint.isEmpty()) {
                ScriptEntryPoint entry;
                entry.name = entryPoint;
                entry.scriptPath = scriptPath;
                entry.lineNum = lineNum;
                m_entryPoints.append(entry);
                
                // Categorize entry points
                if (entryPoint == "SYSTEM" || entryPoint == "SYSTEM_INIT") {
                    m_systemEntryPoint["SYSTEM"] = scriptPath;
                } else if (entryPoint == "SYSTEM_TITLE") {
                    m_systemTitleEntry["SYSTEM_TITLE"] = scriptPath;
                } else if (entryPoint.startsWith("EVENT")) {
                    m_eventEntries.append(scriptPath);
                }
            }
        }
    }
    
    file.close();
}

QString ScriptProcessor::extractEntryPointName(const QString& line) const
{
    // Extract the entry point name from a line like "@SYSTEM" or "@EVENT_TURNEND"
    QRegularExpression re("^@(\\w+)");
    QRegularExpressionMatch match = re.match(line.trimmed());
    
    if (match.hasMatch()) {
        return match.captured(1);
    }
    
    return "";
}

QString ScriptProcessor::findSystemEntryPoint() const
{
    // Return the first SYSTEM entry point found
    for (const ScriptEntryPoint& entry : m_entryPoints) {
        if (entry.name == "SYSTEM" || entry.name == "SYSTEM_INIT") {
            return entry.scriptPath;
        }
    }
    
    return "";  // Return empty if not found
}

QString ScriptProcessor::findSystemTitleEntry() const
{
    // Return the first SYSTEM_TITLE entry point found
    for (const ScriptEntryPoint& entry : m_entryPoints) {
        if (entry.name == "SYSTEM_TITLE") {
            return entry.scriptPath;
        }
    }
    
    return "";  // Return empty if not found
}

QStringList ScriptProcessor::findEventEntries() const
{
    return m_eventEntries;
}

QStringList ScriptProcessor::findAllEntryPoints() const
{
    QStringList result;
    for (const ScriptEntryPoint& entry : m_entryPoints) {
        result.append(entry.name);
    }
    return result;
}

QList<ScriptEntryPoint> ScriptProcessor::getEntryPoints() const
{
    return m_entryPoints;
}

bool ScriptProcessor::validateScript(const QString& scriptPath)
{
    // Check if script file exists
    QFile file(scriptPath);
    if (!file.exists()) {
        return false;
    }
    
    // Basic validation: file can be opened
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    
    file.close();
    return true;
}
