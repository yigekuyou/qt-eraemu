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
#ifndef SCRIPT_PROCESSOR_H
#define SCRIPT_PROCESSOR_H

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>

class EraParseTable;

// ---------------------------------------------------------------------------
// Event registry.
//
// EventInfo/EventManager formerly lived in GameProc/event_manager.h/.cpp.  They
// are merged here because they are the same concern as ScriptProcessor: keeping
// track of which @EVENT* entry points a script directory declares.
// ---------------------------------------------------------------------------

// Event information
struct EventInfo {
    QString name;           // Event name (e.g., "EVENT_TURN_END")
    QString scriptPath;     // Path to script containing event
    int lineNum;            // Line number where event is defined
    bool executed;          // Whether event has been executed
};

class EventManager : public QObject
{
    Q_OBJECT

public:
    explicit EventManager(QObject *parent = nullptr);

    // Event execution
    Q_INVOKABLE void executeEvent(const QString& eventName);
    Q_INVOKABLE void queueEvent(const QString& eventName);
    Q_INVOKABLE void processEvents();
    Q_INVOKABLE void clearEventQueue();

    // Event registration
    void registerEvent(const QString& eventName, const QString& scriptPath, int lineNum);
    QStringList getRegisteredEvents() const;

    // Event state
    bool isEventExecuted(const QString& eventName) const;
    void setEventExecuted(const QString& eventName, bool executed = true);

    // Event types
    enum EventType { EVENT_TURN_END, EVENT_TRADE, EVENT_SHOP, EVENT_LOAD, EVENT_FIRST };

private:
    // Helper methods
    void executeEventInternal(const QString& eventName);

    // Event storage
    QList<EventInfo> m_registeredEvents;
    QStringList m_eventQueue;
    QHash<QString, EventInfo*> m_eventMap;
    QHash<QString, bool> m_executedEvents;
};

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

    // 从已装载的 AST 直接收集入口点（唯一入口点来源；不再二次扫盘/重解析）
    void collectFromParseTable(const EraParseTable* table);
    void clear();
    
    // Entry point detection
    QString findSystemEntryPoint() const;
    QString findSystemTitleEntry() const;
    // 入口点标签名（如 "SYSTEM" / "SYSTEM_TITLE"）；由 collectFromParseTable 填充
    [[nodiscard]] QString findSystemLabel() const { return m_systemLabel; }
    [[nodiscard]] QString findSystemTitleLabel() const { return m_systemTitleLabel; }
    QStringList findEventEntries() const;
    QStringList findAllEntryPoints() const;
    
    // Get all detected entry points
    QList<ScriptEntryPoint> getEntryPoints() const;
    
    // Event registration (for event manager)
    void registerEventsWithManager(EventManager* manager);
    
    // Public accessors for signal-slot integration
    const QStringList& getEventEntries() const { return m_eventEntries; }
    const QList<ScriptEntryPoint>& entryPoints() const { return m_entryPoints; }

private:
    // Entry point storage
    QList<ScriptEntryPoint> m_entryPoints;
    QHash<QString, QString> m_systemEntryPoint;  // "SYSTEM" -> script path
    QHash<QString, QString> m_systemTitleEntry;  // "SYSTEM_TITLE" -> script path
    QString m_systemLabel;        // 实际找到的入口标签（如 "SYSTEM" / "SYSTEM_INIT"）
    QString m_systemTitleLabel;   // 实际找到的标题标签
    QStringList m_eventEntries;  // List of @EVENT* script paths
};

#endif // SCRIPT_PROCESSOR_H
