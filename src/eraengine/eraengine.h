#ifndef ERAENGINE_H
#define ERAENGINE_H

#include <QObject>
#include <QQmlEngine>
#include <QQmlContext>
#include "csv_loader.h"
#include "variable_storage.h"
#include "expression_evaluator.h"
#include "function_system.h"
#include "instruction_system.h"
#include "game_flow_control.h"
#include "rendering_system.h"
#include "file_system_io.h"
#include "input_system.h"
#include "execution_engine.h"
#include "process_state.h"
#include "system_processor.h"
#include "script_line.h"
#include "config_loader.h"
#include "script_processor.h"
#include "input_handler.h"
#include "event_manager.h"
#include "logical_line_parser.h"
#include "variable_types.h"
#include "game_base_data.h"

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
		Q_PROPERTY(ExecutionEngine* executionEngine READ getExecutionEngine CONSTANT)
    // Game base data
    GameBaseData* gameBaseData() { return &m_gameBaseData; }
    
    // Directory management
    QString getGameDirectory() const;
    void setGameDirectory(const QString& directory);
    
    // Main components
		VariableStorage* getVariableStorage() { return &m_variableStorage; }
		ExpressionEvaluator* getExpressionEvaluator() { return &m_expressionEvaluator; }
		FunctionSystem* getFunctionSystem() { return &m_functionSystem; }
		InstructionSystem* getInstructionSystem() { return &m_instructionSystem; }
		GameFlowControl* getGameFlowControl() { return &m_gameFlowControl; }
		RenderingSystem* getRenderingSystem() { return &m_renderingSystem; }
		FileSystem* getFileSystem() { return &m_fileSystem; }
		BinaryIo* getBinaryIo() { return &m_binaryIo; }
		EraTetrisInputSystem* getInputSystem() { return &m_inputSystem; }
		ExecutionEngine* getExecutionEngine() { return &m_executionEngine; }
		ProcessState* getProcessState() { return &m_processState; }
		SystemProcessor* getSystemProcessor() { return &m_systemProcessor; }
		InputHandler* getInputHandler() { return &m_inputHandler; }
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
    
    // Event execution helper (callable from QML)
    Q_INVOKABLE void executeEvent(const QString& eventName);
    Q_INVOKABLE void queueEvent(const QString& eventName);
    Q_INVOKABLE void processEvents();
    Q_INVOKABLE void clearEventQueue();
    Q_INVOKABLE QStringList getRegisteredEvents() const;
    Q_INVOKABLE bool isEventExecuted(const QString& eventName) const;
    
signals:
    void gameDirectoryChanged();
    
private:
    QString m_gameDirectory;
    GameBaseData m_gameBaseData;
    VariableStorage m_variableStorage;
    ExpressionEvaluator m_expressionEvaluator;
    FunctionSystem m_functionSystem;
    InstructionSystem m_instructionSystem;
    GameFlowControl m_gameFlowControl;
    RenderingSystem m_renderingSystem;
    FileSystem m_fileSystem;
    BinaryIo m_binaryIo;
    EraTetrisInputSystem m_inputSystem;
    ExecutionEngine m_executionEngine;
    ProcessState m_processState;
    SystemProcessor m_systemProcessor;
    
    // Config loader
    ConfigLoader m_configLoader;
    
    // Script processor
    ScriptProcessor m_scriptProcessor;
    
    // Input handler
    InputHandler m_inputHandler;
    
    // Event manager
    EventManager m_eventManager;
};

#endif // ERAENGINE_H