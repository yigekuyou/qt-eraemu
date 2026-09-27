<!--
emuera —— Emuera（ERB 脚本引擎）的 Qt6 + QML/C++ 移植
Copyright (C) 2026  yigekuyou

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
-->
# Signal Hub (ConnectionManager + SignalManager)

## Overview

The `ConnectionManager` class provides a flexible way to manage Qt signal-slot connections between components in the EraEngine.

## Features

- **Dynamic Connection Management**: Add/remove connections at runtime
- **Connection Tracking**: Keep track of all active connections
- **Bulk Operations**: Remove all connections to/from a specific object
- **Cleanup Support**: Clear all connections for proper resource management
- **Debug Information**: Emit signals when connections change

## Basic Usage

### Creating Connections

```cpp
ConnectionManager manager;

// Add a connection using signal/slot strings
manager.addConnection(sender, SIGNAL(signalName()),
                      receiver, SLOT(slotName()));

// Or using Qt5+ syntax (type-safe)
manager.addConnection(sender, &SenderClass::signal,
                      receiver, &ReceiverClass::slot);
```

### Removing Connections

```cpp
// Remove a specific connection
manager.removeConnection(connection);

// Remove all connections to a receiver
manager.removeReceiverConnections(receiver);

// Remove all connections from a sender
manager.removeSenderConnections(sender);

// Clear all connections
manager.clearAllConnections();
```

### Getting Connection Information

```cpp
// Get connection count
int count = manager.connectionCount();

// Check if two objects are connected
bool connected = manager.areConnected(sender, receiver);
```

## Integration with EraEngine

The `EraEngine` uses `ConnectionManager` to manage connections between its components:

```cpp
EraEngine::EraEngine(QObject *parent)
    : QObject(parent),
      m_connectionManager(this)
{
    // Use connection manager for all component connections
    m_connectionManager.addConnection(
        &m_executionEngine, SIGNAL(executionStarted(const QString&)),
        &m_statusManager, SLOT(onScriptStarted(const QString&)));
    
    // ... more connections
}
```

## Signals

- `connectionAdded(ConnectionInfo info)` - Emitted when a new connection is added
- `connectionRemoved(ConnectionInfo info)` - Emitted when a connection is removed
- `allConnectionsCleared()` - Emitted when all connections are cleared

## ConnectionInfo Structure

```cpp
struct ConnectionInfo {
    QObject* sender;
    QObject* receiver;
    QString signal;
    QString slot;
    Qt::ConnectionType connectionType;
};
```

## Best Practices

1. **Use ConnectionManager for all component connections** instead of direct `connect()` calls
2. **Clear connections before cleanup** to prevent dangling pointers
3. **Use Qt::UniqueConnection** to avoid duplicate connections
4. **Track connections** for debugging and verification

## Example: Managing Script Execution Connections

```cpp
// Connect execution signals to status manager
m_connectionManager.addConnection(
    &m_executionEngine, &ExecutionEngine::executionStarted,
    &m_statusManager, &SystemStatusManager::onScriptStarted);

m_connectionManager.addConnection(
    &m_executionEngine, &ExecutionEngine::executionFinished,
    &m_statusManager, &SystemStatusManager::onScriptFinished);

m_connectionManager.addConnection(
    &m_executionEngine, &ExecutionEngine::errorOccurred,
    &m_statusManager, &SystemStatusManager::onError);
```

## Cleanup

The `ConnectionManager` is a `QObject` and will automatically clean up when its parent is destroyed. All connections are removed in the destructor.
