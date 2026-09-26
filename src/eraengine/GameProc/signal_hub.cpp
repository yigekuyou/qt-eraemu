#include "signal_hub.h"
#include <QDebug>

// ===========================================================================
// ConnectionManager
// ===========================================================================

ConnectionManager::ConnectionManager(QObject* parent)
    : QObject(parent)
{
    qDebug() << "[ConnectionManager] Created";
}

ConnectionManager::~ConnectionManager() {
    qDebug() << "[ConnectionManager] Destroyed, disconnecting all connections";
    disconnectAll();
}

void ConnectionManager::addConnection(QObject* sender, const QString& signal,
                                       QObject* receiver, const QString& slot,
                                       Qt::ConnectionType type) {
    if (!sender || !receiver) {
        qWarning() << "[ConnectionManager] Cannot add connection: sender or receiver is null";
        return;
    }

    // Create connection info
    ConnectionInfo info(sender, receiver, signal, slot, type);

    // Add the connection
    QMetaObject::Connection conn = connect(sender, signal.toUtf8(), receiver, slot.toUtf8(), type);

    if (conn) {
        m_connections.append(conn);
        m_connectionInfos.append(info);

        qDebug() << "[ConnectionManager] Added connection:"
                 << "sender=" << sender->metaObject()->className()
                 << "signal=" << signal
                 << "receiver=" << receiver->metaObject()->className()
                 << "slot=" << slot;

        emit connectionAdded(info);
    } else {
        qWarning() << "[ConnectionManager] Failed to add connection:"
                   << "sender=" << (sender ? sender->metaObject()->className() : "null")
                   << "signal=" << signal
                   << "receiver=" << (receiver ? receiver->metaObject()->className() : "null")
                   << "slot=" << slot;
    }
}

void ConnectionManager::removeConnection(QMetaObject::Connection connection) {
    if (connection) {
        disconnect(connection);

        int index = m_connections.indexOf(connection);
        if (index >= 0) {
            ConnectionInfo info = m_connectionInfos.takeAt(index);
            qDebug() << "[ConnectionManager] Removed connection:"
                     << "sender=" << info.sender->metaObject()->className()
                     << "signal=" << info.signal
                     << "receiver=" << info.receiver->metaObject()->className()
                     << "slot=" << info.slot;
            emit connectionRemoved(info);
        }
    }
}

void ConnectionManager::removeReceiverConnections(QObject* receiver) {
    if (!receiver) {
        return;
    }

    QString receiverName = receiver->metaObject()->className();

    // Iterate backwards to safely remove connections
    for (int i = m_connections.size() - 1; i >= 0; --i) {
        if (m_connectionInfos[i].receiver == receiver) {
            disconnect(m_connections[i]);
            ConnectionInfo info = m_connectionInfos.takeAt(i);
            m_connections.removeAt(i);

            qDebug() << "[ConnectionManager] Removed receiver connections:"
                     << "sender=" << info.sender->metaObject()->className()
                     << "signal=" << info.signal
                     << "receiver=" << receiverName
                     << "slot=" << info.slot;

            emit connectionRemoved(info);
        }
    }
}

void ConnectionManager::removeSenderConnections(QObject* sender) {
    if (!sender) {
        return;
    }

    QString senderName = sender->metaObject()->className();

    // Iterate backwards to safely remove connections
    for (int i = m_connections.size() - 1; i >= 0; --i) {
        if (m_connectionInfos[i].sender == sender) {
            disconnect(m_connections[i]);
            ConnectionInfo info = m_connectionInfos.takeAt(i);
            m_connections.removeAt(i);

            qDebug() << "[ConnectionManager] Removed sender connections:"
                     << "sender=" << senderName
                     << "signal=" << info.signal
                     << "receiver=" << info.receiver->metaObject()->className()
                     << "slot=" << info.slot;

            emit connectionRemoved(info);
        }
    }
}

void ConnectionManager::clearAllConnections() {
    qDebug() << "[ConnectionManager] Clearing all" << m_connections.size() << "connections";

    // Disconnect all connections
    for (QMetaObject::Connection conn : m_connections) {
        disconnect(conn);
    }

    // Clear the lists
    m_connections.clear();
    m_connectionInfos.clear();

    qDebug() << "[ConnectionManager] All connections cleared";
    emit allConnectionsCleared();
}

int ConnectionManager::connectionCount() const {
    return m_connections.size();
}

bool ConnectionManager::areConnected(QObject* sender, QObject* receiver) const {
    if (!sender || !receiver) {
        return false;
    }

    for (const ConnectionInfo& info : m_connectionInfos) {
        if (info.sender == sender && info.receiver == receiver) {
            return true;
        }
    }
    return false;
}

void ConnectionManager::disconnectAll() {
    clearAllConnections();
}

// ===========================================================================
// SignalManager
// ===========================================================================

SignalManager::SignalManager(QObject* parent)
    : QObject(parent)
{
    qDebug() << "[SignalManager] Created";
}

SignalManager::~SignalManager() {
    qDebug() << "[SignalManager] Destroyed, clearing all handlers";
    clearAllHandlers();
}

void SignalManager::registerHandler(SignalType type, std::function<void(const QVariant&)> handler) {
    if (!handler) {
        qWarning() << "[SignalManager] Cannot register null handler";
        return;
    }

    m_handlers[type].append(handler);
    qDebug() << "[SignalManager] Registered handler for signal:" << static_cast<int>(type);
}

void SignalManager::emitExecutionStarted(const QString& scriptName) {
    QVariant data;
    data.setValue(scriptName);
    emit signalEmitted(SignalType::ExecutionStarted, data);
}

void SignalManager::emitExecutionFinished() {
    QVariant data;
    emit signalEmitted(SignalType::ExecutionFinished, data);
}

void SignalManager::emitExecutionError(const QString& errorMessage) {
    QVariant data;
    data.setValue(errorMessage);
    emit signalEmitted(SignalType::ExecutionError, data);
}

void SignalManager::emitParseStarted(const QString& scriptName) {
    QVariant data;
    data.setValue(scriptName);
    emit signalEmitted(SignalType::ParseStarted, data);
}

void SignalManager::emitParseLineReady(const ParseLineData& data) {
    QVariant variantData;
    variantData.setValue(data);
    emit signalEmitted(SignalType::ParseLineReady, variantData);
}

void SignalManager::emitParseFinished(const QString& scriptName) {
    QVariant data;
    data.setValue(scriptName);
    emit signalEmitted(SignalType::ParseFinished, data);
}

void SignalManager::emitParseError(const QString& errorMessage) {
    QVariant data;
    data.setValue(errorMessage);
    emit signalEmitted(SignalType::ParseError, data);
}

void SignalManager::emitParsingPaused(StateCode state) {
    QVariant data;
    data.setValue(static_cast<int>(state));
    emit signalEmitted(SignalType::ParsingPaused, data);
}

void SignalManager::emitParsingResumed(StateCode state) {
    QVariant data;
    data.setValue(static_cast<int>(state));
    emit signalEmitted(SignalType::ParsingResumed, data);
}

void SignalManager::emitStateChanged(StateCode state) {
    QVariant data;
    data.setValue(static_cast<int>(state));
    emit signalEmitted(SignalType::StateChanged, data);
}

void SignalManager::emitInputRequested(StateCode state) {
    QVariant data;
    data.setValue(static_cast<int>(state));
    emit signalEmitted(SignalType::InputRequested, data);
}

int SignalManager::handlerCount(SignalType type) const {
    return m_handlers.value(type).size();
}

void SignalManager::clearAllHandlers() {
    qDebug() << "[SignalManager] Clearing all" << m_handlers.size() << "signal types";
    m_handlers.clear();
}
