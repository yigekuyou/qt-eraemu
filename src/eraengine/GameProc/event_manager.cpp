#include "event_manager.h"
#include <QDebug>

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
