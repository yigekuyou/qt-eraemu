#ifndef SYSTEM_STATUS_MANAGER_H
#define SYSTEM_STATUS_MANAGER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QTimer>
#include "process_state.h"
#include "game_base_data.h"

// System status manager - tracks and manages the overall system status
class SystemStatusManager : public QObject {
    Q_OBJECT
    
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY statusChanged)
    Q_PROPERTY(bool isInGame READ isInGame NOTIFY statusChanged)
    Q_PROPERTY(QString currentStatus READ getCurrentStatus NOTIFY statusChanged)
    Q_PROPERTY(QString currentScript READ getCurrentScript NOTIFY statusChanged)
    Q_PROPERTY(qint64 executionTime READ getExecutionTime NOTIFY statusChanged)
    
public:
    explicit SystemStatusManager(QObject* parent = nullptr);
    ~SystemStatusManager();
    
    // Status management
    void setRunning(bool running);
    bool isRunning() const;
    
    // Game state management
    void setInGame(bool inGame);
    bool isInGame() const;
    
    // Current status string
    QString getCurrentStatus() const;
    
    // Current script being executed
    QString getCurrentScript() const;
    void setCurrentScript(const QString& script);
    
    // Execution time tracking
    qint64 getExecutionTime() const;
    
    // System state management (from ProcessState)
    SystemStateCode getSystemState() const;
    void setSystemState(SystemStateCode state);
    void setBegin(BeginType type);
    
    // Status information
    QString getStatusDescription(SystemStateCode state) const;
    QMap<QString, QString> getStatusInfo() const;
    
    // Event tracking
    void trackEvent(const QString& event);
    QStringList getEventHistory(int limit = 10) const;
    
    // Performance tracking
    void startTimer();
    void stopTimer();
    void resetTimer();
    
    // State transitions
    void transitionToTitle();
    void transitionToGame();
    void transitionToState(SystemStateCode state);
    
    // Additional state transitions for system phases
    void transitionToTrain();
    void transitionToAblup();
    void transitionToShop();
    void transitionToSaveGame();
    void transitionToLoadGame();
    void transitionToAfterTrain();
    void transitionToTurnEnd();
    
signals:
    void statusChanged();
    void stateChanged(SystemStateCode newState);
    void scriptChanged(const QString& newScript);
    void errorOccurred(const QString& message);
    
public slots:
    void onScriptStarted(const QString& scriptName);
    void onScriptFinished();
    void onError(const QString& errorMessage);
    void onStateChanged(StateCode newState);
    
private:
    // Core status
    bool m_running;
    bool m_inGame;
    SystemStateCode m_systemState;
    BeginType m_beginType;
    
    // Current script
    QString m_currentScript;
    
    // Timing
    QTimer* m_executionTimer;
    qint64 m_executionStartTime;
    qint64 m_totalExecutionTime;
    
    // Event history
    QStringList m_eventHistory;
    int m_maxEventHistory;
    
    // Status descriptions
    QMap<SystemStateCode, QString> m_statusDescriptions;
};

#endif // SYSTEM_STATUS_MANAGER_H
