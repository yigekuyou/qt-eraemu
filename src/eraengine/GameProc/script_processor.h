#ifndef SCRIPT_PROCESSOR_H
#define SCRIPT_PROCESSOR_H

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>

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
    
    // Event registration (for event manager)
    void registerEventsWithManager(EventManager* manager);
    
    // Public accessors for signal-slot integration
    const QStringList& getEventEntries() const { return m_eventEntries; }
    const QList<ScriptEntryPoint>& entryPoints() const { return m_entryPoints; }

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
