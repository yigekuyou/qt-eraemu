/*
 * emuera —— Emuera（ERB 脚本引擎）的 Qt6 + QML/C++ 移植
 * Copyright (C) 2026  yigekuyou
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "script_processor.h"
#include "era_parse_table.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <iostream>

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
    
    // Find all .erb files in the main directory and ERB subdirectory
    QStringList erbFiles = dir.entryList(QStringList() << "*.ERB" << "*.erb", QDir::Files);
    std::cerr << "[DEBUG] Found " << erbFiles.size() << " ERB files in " << scriptDir.toStdString() << std::endl;
    
    // Also check ERB subdirectory
    QDir erbDir(dir.absoluteFilePath("ERB"));
    if (erbDir.exists()) {
        QStringList erbSubFiles = erbDir.entryList(QStringList() << "*.ERB" << "*.erb", QDir::Files);
        std::cerr << "[DEBUG] Found " << erbSubFiles.size() << " ERB files in ERB subdirectory" << std::endl;
        
        for (const QString& file : erbSubFiles) {
            QString filePath = erbDir.absoluteFilePath(file);
            std::cerr << "[DEBUG] Processing script file from ERB subdirectory: " << filePath.toStdString() << std::endl;
            parseScriptFile(filePath);
        }
    }
    
    for (const QString& file : erbFiles) {
        QString filePath = dir.absoluteFilePath(file);
        std::cerr << "[DEBUG] Processing script file: " << filePath.toStdString() << std::endl;
        parseScriptFile(filePath);
    }
}

void ScriptProcessor::clear()
{
    m_entryPoints.clear();
    m_systemEntryPoint.clear();
    m_systemTitleEntry.clear();
    m_systemLabel.clear();
    m_systemTitleLabel.clear();
    m_eventEntries.clear();
}

void ScriptProcessor::collectFromParseTable(const EraParseTable* table)
{
    if (!table) return;
    clear();
    // 遍历已装载的 AST：函数标签（LineKind::FunctionLabel）即入口点候选
    for (const QString& scriptName : table->scriptNames()) {
        const ScriptData* data = table->script(scriptName);
        if (!data) continue;
        const QString scriptPath = table->scriptPath(scriptName);
        for (int i = 0; i < data->lines.size(); ++i) {
            const LogicalLine& line = data->lines.at(i);
            if (line.kind != LineKind::FunctionLabel) continue;
            const QString name = line.labelName;
            if (name.isEmpty()) continue;

            ScriptEntryPoint entry;
            entry.name = name;
            entry.scriptPath = scriptPath;
            entry.lineNum = line.position.lineNumber;
            m_entryPoints.append(entry);

            if (name.compare(QLatin1String("SYSTEM"), Qt::CaseInsensitive) == 0
                || name.compare(QLatin1String("SYSTEM_INIT"), Qt::CaseInsensitive) == 0) {
                m_systemEntryPoint[QStringLiteral("SYSTEM")] = scriptPath;
                if (m_systemLabel.isEmpty()) m_systemLabel = name;
            } else if (name.compare(QLatin1String("SYSTEM_TITLE"), Qt::CaseInsensitive) == 0) {
                m_systemTitleEntry[QStringLiteral("SYSTEM_TITLE")] = scriptPath;
                if (m_systemTitleLabel.isEmpty()) m_systemTitleLabel = name;
            } else if (name.startsWith(QLatin1String("EVENT"), Qt::CaseInsensitive)) {
                if (!m_eventEntries.contains(scriptPath)) m_eventEntries.append(scriptPath);
            }
        }
    }
}

void ScriptProcessor::parseScriptFile(const QString& scriptPath)
{
    qWarning() << "[DEBUG] Parsing script file:" << scriptPath;
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
            qWarning() << "[DEBUG] Found entry point:" << entryPoint << "at line" << lineNum;
            if (!entryPoint.isEmpty()) {
                ScriptEntryPoint entry;
                entry.name = entryPoint;
                entry.scriptPath = scriptPath;
                entry.lineNum = lineNum;
                m_entryPoints.append(entry);
                
                // Categorize entry points
                if (entryPoint == "SYSTEM" || entryPoint == "SYSTEM_INIT") {
                    m_systemEntryPoint["SYSTEM"] = scriptPath;
                    qWarning() << "[DEBUG] Added SYSTEM entry point:" << scriptPath;
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

void ScriptProcessor::registerEventsWithManager(EventManager* manager)
{
    if (!manager) {
        return;
    }
    
    // Register all event entries
    for (const QString& scriptPath : m_eventEntries) {
        // For each event script, register all @EVENT* functions
        QFile file(scriptPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        
        QTextStream in(&file);
        QString line;
        int lineNum = 0;
        
        while (!in.atEnd()) {
            line = in.readLine();
            lineNum++;
            
            if (line.startsWith('@')) {
                QString entryPoint = extractEntryPointName(line);
                if (!entryPoint.isEmpty() && entryPoint.startsWith("EVENT")) {
                    // Register the event with the manager
                    manager->registerEvent(entryPoint, scriptPath, lineNum);
                }
            }
        }
        
        file.close();
    }
}

// ===========================================================================
// EventManager (formerly GameProc/event_manager.cpp)
// ===========================================================================

EventManager::EventManager(QObject *parent)
    : QObject(parent)
{
}

void EventManager::executeEvent(const QString& eventName)
{
    executeEventInternal(eventName);
}

void EventManager::executeEventInternal(const QString& eventName)
{
    // Look up the event in registered events
    if (m_eventMap.contains(eventName)) {
        EventInfo* event = m_eventMap[eventName];
        event->executed = true;
        m_executedEvents[eventName] = true;

        qDebug() << "[EVENT] Executing event:" << eventName;

        // In a full implementation, this would call the script function
        // For now, just log the event
    } else {
        qDebug() << "[EVENT] Event not found:" << eventName;
    }
}

void EventManager::queueEvent(const QString& eventName)
{
    m_eventQueue.append(eventName);
    qDebug() << "[EVENT] Queued event:" << eventName;
}

void EventManager::processEvents()
{
    // Process all queued events
    for (const QString& eventName : m_eventQueue) {
        executeEventInternal(eventName);
    }

    // Clear the queue after processing
    m_eventQueue.clear();
}

void EventManager::clearEventQueue()
{
    m_eventQueue.clear();
}

void EventManager::registerEvent(const QString& eventName, const QString& scriptPath, int lineNum)
{
    EventInfo event;
    event.name = eventName;
    event.scriptPath = scriptPath;
    event.lineNum = lineNum;
    event.executed = false;

    m_registeredEvents.append(event);
    m_eventMap[eventName] = &m_registeredEvents.last();
    m_executedEvents[eventName] = false;

    qDebug() << "[EVENT] Registered event:" << eventName << "at" << scriptPath << "line" << lineNum;
}

QStringList EventManager::getRegisteredEvents() const
{
    return m_eventMap.keys();
}

bool EventManager::isEventExecuted(const QString& eventName) const
{
    return m_executedEvents.value(eventName, false);
}

void EventManager::setEventExecuted(const QString& eventName, bool executed)
{
    m_executedEvents[eventName] = executed;
}
