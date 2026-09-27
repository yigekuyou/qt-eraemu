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
#include <QDebug>
#include <iostream>

ScriptProcessor::ScriptProcessor(QObject *parent)
    : QObject(parent)
{
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

void ScriptProcessor::registerEventsWithManager(EventManager* manager)
{
    if (!manager) {
        return;
    }
    // 事件名已在 collectFromParseTable 阶段从 AST 收集，无需再扫盘/重解析
    for (const ScriptEntryPoint& entry : m_entryPoints) {
        if (entry.name.startsWith(QLatin1String("EVENT"), Qt::CaseInsensitive)) {
            manager->registerEvent(entry.name, entry.scriptPath, entry.lineNum);
        }
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
