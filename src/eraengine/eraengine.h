#ifndef ERAENGINE_H
#define ERAENGINE_H

#include <QObject>
#include <QQmlEngine>
#include <QQmlContext>
#include "csv_loader.h"
#include "variable_storage.h"
#include "expression_evaluator.h"
#include "function_system.h"
#include "rendering_system.h"
#include "file_system_io.h"
#include "execution_engine.h"
#include "process_state.h"
#include "system_processor.h"
#include "system_status_manager.h"
#include "script_line.h"
#include "config_loader.h"
#include "script_processor.h"
#include "input_handler.h"
#include "logical_line_parser.h"
#include "variable_types.h"
#include "game_base_data.h"
#include "identifier_dictionary.h"
#include "signal_hub.h"
#include "era_parse_table.h"
#include "execution/memory_block.h"
#include "execution/script_memory_space.h"

class EraEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit EraEngine(QObject *parent = nullptr);
    
    // QML-exposable properties
		Q_PROPERTY(QString gameDirectory READ getGameDirectory WRITE setGameDirectory NOTIFY gameDirectoryChanged)
		Q_PROPERTY(GameBaseData* gameBaseData READ gameBaseData CONSTANT)
		Q_PROPERTY(RenderingSystem* renderingSystem READ getRenderingSystem CONSTANT)
		Q_PROPERTY(EraTetrisInputSystem* inputSystem READ getInputSystem CONSTANT)
		Q_PROPERTY(InputHandler* inputHandler READ getInputHandler CONSTANT)
		Q_PROPERTY(SystemStatusManager* statusManager READ getStatusManager CONSTANT)
		Q_PROPERTY(ExecutionEngine* executionEngine READ getExecutionEngine CONSTANT)
		Q_PROPERTY(EraParseTable* parseTable READ getParseTable CONSTANT)
    // Game base data
    GameBaseData* gameBaseData() { return &m_gameBaseData; }
    
    // Constant data (CSV data loading)
    
    // Identifier dictionary (name collision detection)
    IdentifierDictionary* getIdentifierDictionary() { return &m_identifierDictionary; }
    
    // Directory management
    QString getGameDirectory() const;
    void setGameDirectory(const QString& directory);
    
    // Main components
		VariableStorage* getVariableStorage() { return &m_variableStorage; }
		ExpressionEvaluator* getExpressionEvaluator() { return &m_expressionEvaluator; }
		FunctionSystem* getFunctionSystem() { return &m_functionSystem; }
		RenderingSystem* getRenderingSystem() { return &m_renderingSystem; }
		FileSystem* getFileSystem() { return &m_fileSystem; }
		BinaryIo* getBinaryIo() { return &m_binaryIo; }
		EraTetrisInputSystem* getInputSystem() { return &m_inputSystem; }
		ExecutionEngine* getExecutionEngine() { return &m_executionEngine; }
		ProcessState* getProcessState() { return &m_processState; }
		SystemProcessor* getSystemProcessor() { return &m_systemProcessor; }
		InputHandler* getInputHandler() { return &m_inputHandler; }
    SystemStatusManager* getStatusManager() { return &m_statusManager; }
    SignalManager* getSignalManager() { return &m_signalManager; }
    EraParseTable* getParseTable() { return &m_parseTable; }
    
    // Execution module accessors
    MemorySpaceManager* getMemorySpaceManager() { return m_parseTable.getMemorySpaceManager(); }
    
    // QML registration
    static void registerTypes();
    
    // Script loading helper (callable from QML)
    Q_INVOKABLE bool loadScript(const QString& scriptPath);
    Q_INVOKABLE void executeScript(const QString& scriptName);
    Q_INVOKABLE void reload();
    
    // Game base data loading
    void loadGameBaseData();
    
    // Config loading helper (callable from QML)
    Q_INVOKABLE void loadConfig(const QString& filePath, int precedence = 0);
    Q_INVOKABLE void mergeConfig(const QString& filePath, int precedence = 0);
    Q_INVOKABLE QString getConfig(const QString& key) const;
    Q_INVOKABLE bool hasConfig(const QString& key) const;
    
    // Script processing helper (callable from QML)
    Q_INVOKABLE void processScripts(const QString& scriptDir);
    Q_INVOKABLE QString getSystemEntryPoint() const;
    Q_INVOKABLE QString getSystemTitleEntry() const;
    Q_INVOKABLE QStringList getEventEntries() const;
    Q_INVOKABLE QStringList getAllEntryPoints() const;
    Q_INVOKABLE void registerEventsWithManager();
    
    // Entry point execution helper (callable from QML)
    Q_INVOKABLE void executeSystemEntryPoint();
    Q_INVOKABLE void executeSystemTitleEntry();
    
    // Full Emuera flow helper (callable from QML)
    Q_INVOKABLE void runSystem();
    
    Q_INVOKABLE void gotoTitle();
    
    // Event execution helper (callable from QML)
    Q_INVOKABLE void executeEvent(const QString& eventName);
    Q_INVOKABLE void queueEvent(const QString& eventName);
    Q_INVOKABLE void processEvents();
    Q_INVOKABLE void clearEventQueue();
    Q_INVOKABLE QStringList getRegisteredEvents() const;
    Q_INVOKABLE bool isEventExecuted(const QString& eventName) const;
    
signals:
    void gameDirectoryChanged();
    void systemStarted();
    void systemFinished();
    
private:
    QString m_gameDirectory;
    GameBaseData m_gameBaseData;
    VariableStorage m_variableStorage;
    ExpressionEvaluator m_expressionEvaluator;
    FunctionSystem m_functionSystem;
    RenderingSystem m_renderingSystem;
    FileSystem m_fileSystem;
    BinaryIo m_binaryIo;
    EraTetrisInputSystem m_inputSystem;
    ExecutionEngine m_executionEngine;
    ProcessState m_processState;
    SystemProcessor m_systemProcessor;
    SystemStatusManager m_statusManager;
    SignalManager m_signalManager;
    EraParseTable m_parseTable;
    
    // Phase 6: Missing features
    IdentifierDictionary m_identifierDictionary;
    
    // Config loader
    ConfigLoader m_configLoader;
    
    // Script processor
    ScriptProcessor m_scriptProcessor;
    
    // Input handler
    InputHandler m_inputHandler;
    
    // Event manager
    EventManager m_eventManager;
    
    // Connection manager - manages signal-slot connections between components
    ConnectionManager m_connectionManager;
};

#endif // ERAENGINE_H
