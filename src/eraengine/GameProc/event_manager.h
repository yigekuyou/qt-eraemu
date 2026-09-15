#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>

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

#endif // EVENT_MANAGER_H
