#ifndef SIGNAL_HUB_H
#define SIGNAL_HUB_H

// ---------------------------------------------------------------------------
// Signal plumbing for the engine.
//
// Formerly GameProc/connection_manager.h + GameProc/signal_manager.h.  Both are
// thin wrappers around Qt's signal/slot machinery -- ConnectionManager tracks
// QMetaObject::Connection handles, SignalManager funnels all pipeline events
// through one signal -- so they live in a single "hub" translation unit.
// ---------------------------------------------------------------------------

#include <QObject>
#include <QList>
#include <QPair>
#include <QHash>
#include <QQueue>
#include <QMap>
#include <QVariant>
#include <functional>

// Forward declarations
class LogicalLine;
class ScriptLine;
class ExecutionEngine;
class SystemStatusManager;
class ProcessState;

// Include for StateCode
#include "process_state.h"
// ===========================================================================
// ConnectionManager
// ===========================================================================

// Connection information structure
struct ConnectionInfo {
    QObject* sender;
    QObject* receiver;
    QString signal;
    QString slot;
    Qt::ConnectionType connectionType;

    ConnectionInfo(QObject* s = nullptr, QObject* r = nullptr,
                   const QString& sig = "", const QString& slt = "",
                   Qt::ConnectionType type = Qt::AutoConnection)
        : sender(s), receiver(r), signal(sig), slot(slt), connectionType(type) {}
};

// Connection manager - manages signal-slot connections between components
// This provides a flexible way to manage connections using Qt's signal-slot mechanism
class ConnectionManager : public QObject {
    Q_OBJECT

public:
    explicit ConnectionManager(QObject* parent = nullptr);
    ~ConnectionManager();

    // Add a connection between sender and receiver
    void addConnection(QObject* sender, const QString& signal,
                       QObject* receiver, const QString& slot,
                       Qt::ConnectionType type = Qt::AutoConnection);

    // Add a connection using function pointers (type-safe)
    // Usage: connectSlots(&SenderClass::signal, &ReceiverClass::slot, type)
    template <typename Func1, typename Func2>
    void connectSlots(QObject* sender, Func1 signal, QObject* receiver, Func2 slot,
                      Qt::ConnectionType type = Qt::AutoConnection) {
        QMetaObject::Connection conn = connect(sender, signal, receiver, slot, type);
        if (conn) {
            m_connections.append(conn);
            // Also store connection info for tracking
            ConnectionInfo info(sender, receiver, "", "", type);
            m_connectionInfos.append(info);
            emit connectionAdded(info);
        }
    }

    // Remove a specific connection
    void removeConnection(QMetaObject::Connection connection);

    // Remove all connections to a specific receiver
    void removeReceiverConnections(QObject* receiver);

    // Remove all connections from a specific sender
    void removeSenderConnections(QObject* sender);

    // Remove all connections
    void clearAllConnections();

    // Get connection count
    int connectionCount() const;

    // Check if two objects are connected
    bool areConnected(QObject* sender, QObject* receiver) const;

    // Disconnect all connections (for cleanup)
    void disconnectAll();

signals:
    // Signal emitted when a new connection is added
    void connectionAdded(ConnectionInfo info);

    // Signal emitted when a connection is removed
    void connectionRemoved(ConnectionInfo info);

    // Signal emitted when all connections are cleared
    void allConnectionsCleared();

private:
    QList<QMetaObject::Connection> m_connections;
    QList<ConnectionInfo> m_connectionInfos;
};

// Utility function to create connection info
inline ConnectionInfo createConnectionInfo(QObject* sender, QObject* receiver,
                                            const QString& signal, const QString& slot,
                                            Qt::ConnectionType type = Qt::AutoConnection) {
    return ConnectionInfo(sender, receiver, signal, slot, type);
}

// ===========================================================================
// SignalManager
// ===========================================================================

// Signal types for different stages of script processing
enum class SignalType {
    // Parsing stage signals
    ParseStarted,           // Parsing started
    ParseLineReady,         // A line has been parsed
    ParseFinished,          // Parsing finished
    ParseError,             // Parse error occurred

    // Execution stage signals
    ExecutionStarted,       // Execution started
    ExecutionLineReady,     // Line ready for execution
    ExecutionLineFinished,  // Line execution finished
    ExecutionFinished,      // Execution finished
    ExecutionError,         // Execution error occurred

    // System management signals
    StateChanged,           // System state changed
    OutputReady,            // Output ready to display
    InputRequested,         // Input requested from user
    FunctionCalled,         // Function called
    FunctionReturned,       // Function returned
    ParsingPaused,          // Parsing paused for input wait
    ParsingResumed,         // Parsing resumed after input
};

// Signal data structures
struct ParseLineData {
    QString scriptName;
    int lineNumber;
    QString lineContent;
};

struct ExecutionLineData {
    QString scriptName;
    int lineNumber;
    QString instruction;
};

struct StateChangeData {
    QString oldState;
    QString newState;
    int stateCode;

    StateChangeData() : stateCode(0) {}
    StateChangeData(const QString& old, const QString& new_state, int code = 0)
        : oldState(old), newState(new_state), stateCode(code) {}
};

Q_DECLARE_METATYPE(ParseLineData)
Q_DECLARE_METATYPE(ExecutionLineData)
Q_DECLARE_METATYPE(StateChangeData)

class SignalManager : public QObject {
    Q_OBJECT

public:
    explicit SignalManager(QObject* parent = nullptr);
    ~SignalManager();

    // Register handlers for different signal types
    void registerHandler(SignalType type, std::function<void(const QVariant&)> handler);

    // Emit signals - these are now slots to be properly connected
    public slots:
        void emitExecutionStarted(const QString& scriptName);
        void emitExecutionFinished();  // No arguments to match ExecutionEngine::executionFinished()
        void emitExecutionError(const QString& errorMessage);  // Single string to match ExecutionEngine::errorOccurred()
        void emitParseStarted(const QString& scriptName);
        void emitParseLineReady(const ParseLineData& data);
        void emitParseFinished(const QString& scriptName);
        void emitParseError(const QString& errorMessage);
        void emitParsingPaused(StateCode state);
        void emitParsingResumed(StateCode state);
        void emitStateChanged(StateCode state);
        void emitInputRequested(StateCode state);

    // Get registered handlers count
    int handlerCount(SignalType type) const;

    // Clear all handlers
    void clearAllHandlers();

signals:
    // Signal routing - all signals are emitted through this single interface
    void signalEmitted(SignalType type, const QVariant& data);

private:
    // Handler storage
    QHash<SignalType, QList<std::function<void(const QVariant&)>>> m_handlers;
};

#endif // SIGNAL_HUB_H
