#include "system_status_manager.h"
#include <QDateTime>
#include <QDebug>

SystemStatusManager::SystemStatusManager(QObject* parent)
    : QObject(parent),
      m_running(false),
      m_inGame(false),
      m_systemState(SystemStateCode::Title_Begin),
      m_beginType(BeginType::NONE),
      m_currentScript(""),
      m_executionStartTime(0),
      m_totalExecutionTime(0),
      m_maxEventHistory(50)
{
    // Initialize execution timer
    m_executionTimer = new QTimer(this);
    connect(m_executionTimer, &QTimer::timeout, this, [this]() {
        m_totalExecutionTime += 100;  // 100ms intervals
        emit statusChanged();
    });
    
    // Initialize status descriptions
    m_statusDescriptions[SystemStateCode::Title_Begin] = "Title Screen - Ready";
    m_statusDescriptions[SystemStateCode::Openning] = "Opening Sequence";
    m_statusDescriptions[SystemStateCode::Normal] = "Normal Game Flow";
    m_statusDescriptions[SystemStateCode::Train_Begin] = "TRAIN System - Begin";
    m_statusDescriptions[SystemStateCode::Train_CallEventTrain] = "TRAIN System - Event";
    m_statusDescriptions[SystemStateCode::Train_CallShowStatus] = "TRAIN System - Show Status";
    m_statusDescriptions[SystemStateCode::Train_CallComAbleXX] = "TRAIN System - Command";
    m_statusDescriptions[SystemStateCode::Train_CallShowUserCom] = "TRAIN System - User Command";
    m_statusDescriptions[SystemStateCode::Train_WaitInput] = "TRAIN System - Waiting Input";
    m_statusDescriptions[SystemStateCode::Train_CallEventCom] = "TRAIN System - Event";
    m_statusDescriptions[SystemStateCode::Train_CallComXX] = "TRAIN System - Command";
    m_statusDescriptions[SystemStateCode::Train_CallSourceCheck] = "TRAIN System - Source Check";
    m_statusDescriptions[SystemStateCode::Train_CallEventComEnd] = "TRAIN System - Event End";
    m_statusDescriptions[SystemStateCode::Train_DoTrain] = "TRAIN System - Processing";
    m_statusDescriptions[SystemStateCode::AfterTrain_Begin] = "AFTERTRAIN System - Begin";
    m_statusDescriptions[SystemStateCode::Ablup_Begin] = "ABLUP System - Begin";
    m_statusDescriptions[SystemStateCode::Ablup_CallShowJuel] = "ABLUP System - Show Juel";
    m_statusDescriptions[SystemStateCode::Ablup_CallShowAblupSelect] = "ABLUP System - Select";
    m_statusDescriptions[SystemStateCode::Ablup_WaitInput] = "ABLUP System - Waiting Input";
    m_statusDescriptions[SystemStateCode::Ablup_CallAblupXX] = "ABLUP System - Processing";
    m_statusDescriptions[SystemStateCode::Turnend_Begin] = "TURNEND System - Begin";
    m_statusDescriptions[SystemStateCode::Shop_Begin] = "SHOP System - Begin";
    m_statusDescriptions[SystemStateCode::Shop_CallEventShop] = "SHOP System - Event";
    m_statusDescriptions[SystemStateCode::Shop_CallShowShop] = "SHOP System - Show Shop";
    m_statusDescriptions[SystemStateCode::Shop_WaitInput] = "SHOP System - Waiting Input";
    m_statusDescriptions[SystemStateCode::Shop_CallEventBuy] = "SHOP System - Buy Event";
    m_statusDescriptions[SystemStateCode::SaveGame_Begin] = "Save Game - Begin";
    m_statusDescriptions[SystemStateCode::SaveGame_WaitInput] = "Save Game - Waiting Input";
    m_statusDescriptions[SystemStateCode::SaveGame_WaitInputOverwrite] = "Save Game - Overwrite Prompt";
    m_statusDescriptions[SystemStateCode::SaveGame_CallSaveInfo] = "Save Game - Saving Info";
    m_statusDescriptions[SystemStateCode::LoadGame_Begin] = "Load Game - Begin";
    m_statusDescriptions[SystemStateCode::LoadGame_WaitInput] = "Load Game - Waiting Input";
    m_statusDescriptions[SystemStateCode::LoadGameOpenning_Begin] = "Load Game Opening";
    m_statusDescriptions[SystemStateCode::LoadGameOpenning_WaitInput] = "Load Game Opening - Waiting";
    m_statusDescriptions[SystemStateCode::AutoSave_CallSaveInfo] = "Auto Save - Saving Info";
    m_statusDescriptions[SystemStateCode::AutoSave_CallUniqueAutosave] = "Auto Save - Unique Save";
    m_statusDescriptions[SystemStateCode::AutoSave_Skipped] = "Auto Save - Skipped";
    m_statusDescriptions[SystemStateCode::LoadData_DataLoaded] = "Load Data - Data Loaded";
    m_statusDescriptions[SystemStateCode::LoadData_CallSystemLoad] = "Load Data - System Load";
    m_statusDescriptions[SystemStateCode::LoadData_CallEventLoad] = "Load Data - Event Load";
    m_statusDescriptions[SystemStateCode::Openning_TitleLoadgame] = "Opening - Title Load Game";
    m_statusDescriptions[SystemStateCode::System_Reloaderb] = "System - Reloading Scripts";
    m_statusDescriptions[SystemStateCode::First_Begin] = "First System - Begin";
}

SystemStatusManager::~SystemStatusManager() {
    if (m_executionTimer) {
        m_executionTimer->stop();
        delete m_executionTimer;
        m_executionTimer = nullptr;
    }
}

// Status management
void SystemStatusManager::setRunning(bool running) {
    if (m_running != running) {
        m_running = running;
        if (m_running) {
            startTimer();
        } else {
            stopTimer();
        }
        emit statusChanged();
    }
}

bool SystemStatusManager::isRunning() const {
    return m_running;
}

// Game state management
void SystemStatusManager::setInGame(bool inGame) {
    if (m_inGame != inGame) {
        m_inGame = inGame;
        emit statusChanged();
    }
}

bool SystemStatusManager::isInGame() const {
    return m_inGame;
}

// Current status string
QString SystemStatusManager::getCurrentStatus() const {
    QString status = getStatusDescription(m_systemState);
    
    if (m_inGame) {
        status += " (In Game)";
    } else {
        status += " (Not In Game)";
    }
    
    if (!m_currentScript.isEmpty()) {
        status += " - Script: " + m_currentScript;
    }
    
    return status;
}

// Current script
QString SystemStatusManager::getCurrentScript() const {
    return m_currentScript;
}

void SystemStatusManager::setCurrentScript(const QString& script) {
    if (m_currentScript != script) {
        m_currentScript = script;
        emit scriptChanged(script);
        emit statusChanged();
    }
}

// Execution time
qint64 SystemStatusManager::getExecutionTime() const {
    if (m_running && m_executionStartTime > 0) {
        return m_totalExecutionTime + (QDateTime::currentMSecsSinceEpoch() - m_executionStartTime);
    }
    return m_totalExecutionTime;
}

// System state management
SystemStateCode SystemStatusManager::getSystemState() const {
    return m_systemState;
}

void SystemStatusManager::setSystemState(SystemStateCode state) {
    m_systemState = state;
    emit stateChanged(state);
    emit statusChanged();
}

void SystemStatusManager::setBegin(BeginType type) {
    m_beginType = type;
    emit statusChanged();
}

// Status descriptions
QString SystemStatusManager::getStatusDescription(SystemStateCode state) const {
    if (m_statusDescriptions.contains(state)) {
        return m_statusDescriptions[state];
    }
    return "Unknown State";
}

QMap<QString, QString> SystemStatusManager::getStatusInfo() const {
    QMap<QString, QString> info;
    info["status"] = getCurrentStatus();
    info["running"] = m_running ? "true" : "false";
    info["inGame"] = m_inGame ? "true" : "false";
    info["currentScript"] = m_currentScript;
    info["executionTime"] = QString::number(getExecutionTime()) + " ms";
    info["systemState"] = QString::number(static_cast<int>(m_systemState));
    info["beginType"] = QString::number(static_cast<int>(m_beginType));
    info["eventHistory"] = m_eventHistory.join(", ");
    return info;
}

// Event tracking
void SystemStatusManager::trackEvent(const QString& event) {
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString entry = QString("[%1] %2").arg(timestamp).arg(event);
    
    m_eventHistory.append(entry);
    
    // Keep only the last N events
    while (m_eventHistory.size() > m_maxEventHistory) {
        m_eventHistory.removeFirst();
    }
}

QStringList SystemStatusManager::getEventHistory(int limit) const {
    int actualLimit = qMin(limit, m_eventHistory.size());
    return m_eventHistory.last(actualLimit);
}

// Timer management
void SystemStatusManager::startTimer() {
    m_executionStartTime = QDateTime::currentMSecsSinceEpoch();
    m_executionTimer->start(100);  // Update every 100ms
}

void SystemStatusManager::stopTimer() {
    m_executionTimer->stop();
    if (m_executionStartTime > 0) {
        m_totalExecutionTime += (QDateTime::currentMSecsSinceEpoch() - m_executionStartTime);
        m_executionStartTime = 0;
    }
}

void SystemStatusManager::resetTimer() {
    stopTimer();
    m_totalExecutionTime = 0;
    m_executionStartTime = 0;
}

// State transitions
void SystemStatusManager::transitionToTitle() {
    m_systemState = SystemStateCode::Title_Begin;
    m_beginType = BeginType::NONE;
    m_inGame = false;
    emit stateChanged(SystemStateCode::Title_Begin);
    emit statusChanged();
}

void SystemStatusManager::transitionToGame() {
    m_systemState = SystemStateCode::Normal;
    m_beginType = BeginType::TRAIN;
    m_inGame = true;
    emit stateChanged(SystemStateCode::Normal);
    emit statusChanged();
}

void SystemStatusManager::transitionToState(SystemStateCode state) {
    m_systemState = state;
    emit stateChanged(state);
    emit statusChanged();
}

void SystemStatusManager::transitionToTrain() {
    m_systemState = SystemStateCode::Train_Begin;
    m_beginType = BeginType::TRAIN;
    emit stateChanged(SystemStateCode::Train_Begin);
    emit statusChanged();
}

void SystemStatusManager::transitionToAblup() {
    m_systemState = SystemStateCode::Ablup_Begin;
    m_beginType = BeginType::ABLUP;
    emit stateChanged(SystemStateCode::Ablup_Begin);
    emit statusChanged();
}

void SystemStatusManager::transitionToShop() {
    m_systemState = SystemStateCode::Shop_Begin;
    m_beginType = BeginType::SHOP;
    emit stateChanged(SystemStateCode::Shop_Begin);
    emit statusChanged();
}

void SystemStatusManager::transitionToSaveGame() {
    m_systemState = SystemStateCode::SaveGame_Begin;
    emit stateChanged(SystemStateCode::SaveGame_Begin);
    emit statusChanged();
}

void SystemStatusManager::transitionToLoadGame() {
    m_systemState = SystemStateCode::LoadGame_Begin;
    emit stateChanged(SystemStateCode::LoadGame_Begin);
    emit statusChanged();
}

void SystemStatusManager::transitionToAfterTrain() {
    m_systemState = SystemStateCode::AfterTrain_Begin;
    emit stateChanged(SystemStateCode::AfterTrain_Begin);
    emit statusChanged();
}

void SystemStatusManager::transitionToTurnEnd() {
    m_systemState = SystemStateCode::Turnend_Begin;
    emit stateChanged(SystemStateCode::Turnend_Begin);
    emit statusChanged();
}

// Slots
void SystemStatusManager::onScriptStarted(const QString& scriptName) {
    setCurrentScript(scriptName);
    trackEvent("Script started: " + scriptName);
    emit statusChanged();
}

void SystemStatusManager::onScriptFinished() {
    trackEvent("Script finished: " + m_currentScript);
    // Don't clear current script immediately - keep it for reference
    emit statusChanged();
}

void SystemStatusManager::onError(const QString& errorMessage) {
    trackEvent("Error: " + errorMessage);
    emit errorOccurred(errorMessage);
    emit statusChanged();
}

void SystemStatusManager::onStateChanged(StateCode newState) {
    // Update the system state
    m_systemState = newState;
    emit statusChanged();
    qDebug() << "[SystemStatusManager] State changed to:" << static_cast<int>(newState);
}
