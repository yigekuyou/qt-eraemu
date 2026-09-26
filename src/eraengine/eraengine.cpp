#include "eraengine.h"

// Include ConsoleDisplay for QML singleton registration
#include "rendering_system.h"
#include "system_status_manager.h"
#include "signal_hub.h"
#include "erb_loader.h"
#include "logical_line_parser.h"
#include <iostream>
#include <QObject>

EraEngine::EraEngine(QObject *parent)
		: QObject(parent),
			m_variableStorage(),
			m_expressionEvaluator(),
			m_functionSystem(),
			m_renderingSystem(),
			m_fileSystem(),
			m_binaryIo(),
			m_inputSystem(),
			m_executionEngine(&m_variableStorage, &m_gameBaseData),
			m_processState(),
			m_systemProcessor(&m_processState),
			m_parseTable(&m_processState, &m_executionEngine),
			m_configLoader(),
			m_scriptProcessor(),
			m_inputHandler(),
			m_eventManager(),
			m_identifierDictionary(),
			m_statusManager(),
			m_signalManager(this),
			m_connectionManager(this)
{
		// Set variable storage in parse table for condition evaluation
		m_parseTable.setVariableStorage(&m_variableStorage);
		
		// Set ParseTable reference in ExecutionEngine for CALL/RETURN integration
		m_executionEngine.setParseTable(&m_parseTable);
		
		// Initialize signal manager with default handlers
		// Connect execution engine signals to signal manager
		connect(&m_executionEngine, &ExecutionEngine::executionStarted,
				&m_signalManager, &SignalManager::emitExecutionStarted);
		
		connect(&m_executionEngine, &ExecutionEngine::executionFinished,
				&m_signalManager, &SignalManager::emitExecutionFinished);
		
		connect(&m_executionEngine, &ExecutionEngine::errorOccurred,
				&m_signalManager, &SignalManager::emitExecutionError);
		
		// Connect ErbLoader signals to signal manager
		connect(&m_executionEngine.getErbLoader(), &ErbLoader::parseStarted,
				&m_signalManager, &SignalManager::emitParseStarted);
		connect(&m_executionEngine.getErbLoader(), &ErbLoader::parseFinished,
				&m_signalManager, &SignalManager::emitParseFinished);
		connect(&m_executionEngine.getErbLoader(), &ErbLoader::parseError,
				&m_signalManager, &SignalManager::emitParseError);
		
		// Connect LogicalLineParser signals to signal manager
		connect(&m_executionEngine.getLogicalLineParser(), &LogicalLineParser::logicalLineParsed,
				&m_signalManager, [this](const LogicalLine& line, int lineNumber) {
			// Convert LogicalLine to ExecutionLineData for signal manager
			const QList<ScriptLine>& scriptLines = line.scriptLines();
			for (const ScriptLine& scriptLine : scriptLines) {
				if (scriptLine.type() == ScriptLineType::Instruction) {
					ExecutionLineData data;
					data.scriptName = "current";
					data.lineNumber = lineNumber;
					data.instruction = scriptLine.content();
					m_signalManager.emitParseLineReady({data.scriptName, data.lineNumber, data.instruction});
				}
			}
		});
		
		// Connect signal manager to system status manager slots for all signal types
		connect(&m_signalManager, &SignalManager::signalEmitted,
				&m_statusManager, [this](SignalType type, const QVariant& data) {
			switch (type) {
				case SignalType::ExecutionStarted: {
					QString scriptName = data.value<QString>();
					m_statusManager.onScriptStarted(scriptName);
					break;
				}
				case SignalType::ExecutionFinished: {
					m_statusManager.onScriptFinished();
					break;
				}
				case SignalType::ExecutionError: {
					QString errorMessage = data.value<QString>();
					m_statusManager.onError(errorMessage);
					break;
				}
				case SignalType::ParseStarted: {
					QString scriptName = data.value<QString>();
					qDebug() << "[SystemStatus] Parse started:" << scriptName;
					break;
				}
				case SignalType::ParseLineReady: {
					ParseLineData lineData = data.value<ParseLineData>();
					qDebug() << "[SystemStatus] Parse line ready:" << lineData.scriptName
							 << "line" << lineData.lineNumber << ":" << lineData.lineContent;
					break;
				}
				case SignalType::ParseFinished: {
					QString scriptName = data.value<QString>();
					qDebug() << "[SystemStatus] Parse finished:" << scriptName;
					break;
				}
				case SignalType::ParseError: {
					QString errorMessage = data.value<QString>();
					qDebug() << "[SystemStatus] Parse error:" << errorMessage;
					break;
				}
				default:
					break;
			}
		});
		
		qDebug() << "[EraEngine] Connection manager initialized with" 
				 << m_connectionManager.connectionCount() << "connections";
		qDebug() << "[EraEngine] Signal manager ready";
		
		// Connect ErbLoader to ParseTable for parsing notifications
		m_executionEngine.getErbLoader().setParseTable(&m_parseTable);
		
		// Connect EraParseTable to ProcessState for state-based execution control
		// ParseTable emits checkState when it needs to verify if execution should continue
		// ProcessState emits stateUnchanged if state didn't change, allowing execution to continue
		connect(&m_parseTable, &EraParseTable::checkState,
				&m_processState, &ProcessState::requestStateCheck);
		
		// ProcessState emits stateUnchanged when state didn't change
		// Use Qt::QueuedConnection to break the signal chain and avoid stack overflow
		connect(&m_processState, &ProcessState::stateUnchanged,
				&m_parseTable, &EraParseTable::onStateUnchanged, Qt::QueuedConnection);
		
		// ProcessState emits stateChangedSignal when state changed
		// Use Qt::QueuedConnection to break the signal chain and avoid stack overflow
		connect(&m_processState, &ProcessState::stateChangedSignal,
				&m_parseTable, &EraParseTable::onStateChanged, Qt::QueuedConnection);
		
		// ProcessState emits stateChanged (no args) when state changes
		// Connect to SystemStatusManager to keep state synchronized
		connect(&m_processState, &ProcessState::stateChanged,
				[this]() {
			m_statusManager.onStateChanged(m_processState.getState());
		});
		
		// Removed: requestNextInstruction signal is no longer used
		// Execution is now driven by pumpInstructions() directly
		
		// Instruction execution is now handled directly by EraParseTable::pumpInstructions()
		// to avoid stack overflow from recursive signal chains
		
		connect(&m_parseTable, &EraParseTable::jumpRequested,
				&m_executionEngine, &ExecutionEngine::handleJumpRequest);
		
		connect(&m_parseTable, &EraParseTable::memorySpaceChanged,
				&m_executionEngine, &ExecutionEngine::handleMemorySpaceChange);
		
		// Connect system processor signals to signal manager
		connect(&m_systemProcessor, &SystemProcessor::stateChanged,
				&m_signalManager, [this](StateCode oldState, StateCode newState) {
			m_signalManager.emitStateChanged(newState);
		});
		
		connect(&m_systemProcessor, &SystemProcessor::inputRequested,
				&m_signalManager, &SignalManager::emitInputRequested);
		
		// Connect execution engine BEGIN signal to system processor
		connect(&m_executionEngine, &ExecutionEngine::beginRequested,
				&m_systemProcessor, &SystemProcessor::onBeginRequested);
		
		// Connect system processor signals to signal manager parsing signals
		connect(&m_systemProcessor, &SystemProcessor::parsingPaused,
				&m_signalManager, [this](StateCode state) {
			m_signalManager.emitParsingPaused(state);
		});
		
		connect(&m_systemProcessor, &SystemProcessor::parsingResumed,
				&m_signalManager, [this](StateCode state) {
			m_signalManager.emitParsingResumed(state);
		});
		
		// Connect signal manager to system status manager for parsing signals
		connect(&m_signalManager, &SignalManager::signalEmitted,
				&m_statusManager, [this](SignalType type, const QVariant& data) {
			switch (type) {
				case SignalType::ParsingPaused: {
					int state = data.value<int>();
					qDebug() << "[SystemStatus] Parsing paused for state:" << state;
					break;
				}
				case SignalType::ParsingResumed: {
					int state = data.value<int>();
					qDebug() << "[SystemStatus] Parsing resumed from state:" << state;
					break;
				}
				case SignalType::StateChanged: {
					int state = data.value<int>();
					qDebug() << "[SystemStatus] State changed to:" << state;
					break;
				}
				case SignalType::InputRequested: {
					int state = data.value<int>();
					qDebug() << "[SystemStatus] Input requested for state:" << state;
					break;
				}
				default:
					break;
			}
		});
}

void EraEngine::registerTypes()
{
		qmlRegisterType<GameBaseData>("io.yigekuoyou.eraengine", 1, 0, "GameBaseData");
		qmlRegisterType<EraEngine>("io.yigekuoyou.eraengine", 1, 0, "EraEngine");
		qmlRegisterType<SystemStatusManager>("io.yigekuoyou.eraengine", 1, 0, "SystemStatusManager");
		qmlRegisterType<SignalManager>("io.yigekuoyou.eraengine", 1, 0, "SignalManager");

		// Register rendering system types
		qmlRegisterSingletonType<ConsoleDisplay>("io.yigekuoyou.eraengine", 1, 0, "ConsoleDisplay",
				[](QQmlEngine *engine, QJSEngine *scriptEngine) -> QObject* {
					Q_UNUSED(engine)
					Q_UNUSED(scriptEngine)
					return new ConsoleDisplay();
				});

		// Register input system types
		qmlRegisterUncreatableType<EraTetrisInputSystem>("io.yigekuoyou.eraengine", 1, 0, "EraTetrisInputSystem",
				"EraTetrisInputSystem is created by EraEngine");

		// Register script types
		qmlRegisterUncreatableType<ScriptLine>("io.yigekuoyou.eraengine", 1, 0, "ScriptLine",
				"ScriptLine is used internally");
		qmlRegisterUncreatableType<LogicalLine>("io.yigekuoyou.eraengine", 1, 0, "LogicalLine",
				"LogicalLine is used internally");

		// Register enum types
		qRegisterMetaType<StateCode>("StateCode");
		qRegisterMetaType<BeginType>("BeginType");
		qRegisterMetaType<ScriptLineType>("ScriptLineType");
		qRegisterMetaType<VariableTypes::Type>("VariableTypes::Type");
		qRegisterMetaType<VariableTypes::Scope>("VariableTypes::Scope");
		qRegisterMetaType<VariableTypes::Dimension>("VariableTypes::Dimension");
		qRegisterMetaType<VariableTypes::Flag>("VariableTypes::Flag");
		qRegisterMetaType<ScriptPosition>("ScriptPosition");
		qRegisterMetaType<InstructionArgument>("InstructionArgument");
		qRegisterMetaType<InstructionData>("InstructionData");
		qRegisterMetaType<CalledFunction>("CalledFunction");
		qRegisterMetaType<SignalType>("SignalType");
		qRegisterMetaType<ExecutionLineData>("ExecutionLineData");
		qRegisterMetaType<ParseLineData>("ParseLineData");
}

bool EraEngine::loadScript(const QString& scriptPath)
{
		return m_executionEngine.loadScripts(scriptPath);
}

void EraEngine::executeScript(const QString& scriptName)
{
		m_executionEngine.executeScript(scriptName);
}

QString EraEngine::getGameDirectory() const
{
		return m_gameDirectory;
}

void EraEngine::setGameDirectory(const QString& directory)
{
		if (m_gameDirectory != directory) {
				// Ensure the directory path is properly formatted
				QString dir = directory;
				// If it's a URL (starts with file://), convert to local path
				if (dir.startsWith("file://")) {
						dir = QUrl(dir).toLocalFile();
				}
				m_gameDirectory = dir;
				m_fileSystem.setRootDir(dir);
				qDebug() << "[DEBUG] About to call reload()";
				reload();
				qDebug() << "[DEBUG] reload() complete";
				qDebug() << "[DEBUG] setGameDirectory complete";
				emit gameDirectoryChanged();
		}
}

void EraEngine::reload()
{
		// Reload scripts from current directory
		if (!m_gameDirectory.isEmpty()) {
				// First load config files in correct precedence order
				// 1. _default.config (lowest precedence)
				// 2. emuera.config (medium precedence)
				// 3. _fixed.config (highest precedence)
				QString csvDir = m_fileSystem.findActualDir(m_gameDirectory, "CSV");
				if (csvDir.isEmpty()) {
						csvDir = m_fileSystem.findActualDir(m_gameDirectory, "csv");
				}
				
				if (!csvDir.isEmpty()) {
						// Load default config first (lowest precedence)
						QString defaultConfig = m_fileSystem.getConfigPath(m_gameDirectory, "_default.config");
						if (m_fileSystem.fileExists(defaultConfig)) {
								m_configLoader.loadConfigFile(defaultConfig, 0);
						}
						// Load main config file (medium precedence)
						QString mainConfig = m_fileSystem.getConfigPath(m_gameDirectory, "emuera.config");
						if (m_fileSystem.fileExists(mainConfig)) {
								m_configLoader.loadConfigFile(mainConfig, 1);
						}
						// Load fixed config last (highest precedence)
						QString fixedConfig = m_fileSystem.getConfigPath(m_gameDirectory, "_fixed.config");
						if (m_fileSystem.fileExists(fixedConfig)) {
								m_configLoader.loadConfigFile(fixedConfig, 2);
						}
				}

				// Then load gamebase data
				loadGameBaseData();

				// Then load constant data from CSV files
				if (!csvDir.isEmpty()) {
						qDebug() << "[DEBUG] Loading constant data from CSV...";
						qDebug() << "[DEBUG] Constant data loaded";
				}

				// Then load scripts
				qDebug() << "[DEBUG] Loading scripts...";
				m_executionEngine.loadScripts(m_gameDirectory);
				qDebug() << "[DEBUG] Scripts loaded";
				
				// Extract entry points from loaded scripts
				qDebug() << "[DEBUG] Processing entry points...";
				m_scriptProcessor.processScripts(m_gameDirectory);
				qDebug() << "[DEBUG] Entry points processed";
		}
}

void EraEngine::loadGameBaseData()
{
		// Debug: Print the raw game directory
		qDebug() << "[DEBUG] Raw m_gameDirectory:" << m_gameDirectory;
		
		// GameBase.csv should be in the CSV directory
		QString localPath = QUrl(m_gameDirectory).toLocalFile();
		qDebug() << "[DEBUG] localPath from QUrl:" << localPath;

		if (localPath.isEmpty()) {
			localPath = m_gameDirectory;
			qDebug() << "[DEBUG] Using direct conversion:" << localPath;
		}

		qDebug() << "[DEBUG] Looking for GameBase.csv in:" << localPath;

		QString gameBasePath = localPath + "/GameBase.csv";
		qDebug() << "[DEBUG] GameBase.csv path:" << gameBasePath;

		if (!m_fileSystem.fileExists(gameBasePath)) {
			qDebug() << "[DEBUG] GameBase.csv not found";
			return;
		}

		// Load GameBase.csv using CsvLoader
		CsvLoader csvLoader;
		if (csvLoader.loadFile(gameBasePath)) {
			qDebug() << "[DEBUG] GameBase.csv loaded successfully";
			// Get table names
			QStringList tableNames = csvLoader.getTableNames();
			for (const QString& tableName : tableNames) {
				int rowCount = csvLoader.getRowCount(tableName);
				int colCount = csvLoader.getColumnCount(tableName);
				qDebug() << "[DEBUG] Table:" << tableName << "rows:" << rowCount << "cols:" << colCount;
			}
		} else {
			qDebug() << "[DEBUG] Failed to load GameBase.csv";
		}
}

void EraEngine::loadConfig(const QString& filePath, int precedence)
{
		m_configLoader.loadConfigFile(filePath, precedence);
		qDebug() << "[Config] Loaded config:" << filePath << "precedence:" << precedence;
}

void EraEngine::mergeConfig(const QString& filePath, int precedence)
{
		m_configLoader.mergeConfig(filePath, precedence);
		qDebug() << "[Config] Merged config:" << filePath << "precedence:" << precedence;
}

QString EraEngine::getConfig(const QString& key) const
{
		return m_configLoader.getConfig(key);
}

bool EraEngine::hasConfig(const QString& key) const
{
		return m_configLoader.hasConfig(key);
}

void EraEngine::processScripts(const QString& scriptDir)
{
		m_scriptProcessor.processScripts(scriptDir);
}

QString EraEngine::getSystemEntryPoint() const
{
		return m_scriptProcessor.findSystemEntryPoint();
}

QString EraEngine::getSystemTitleEntry() const
{
		return m_scriptProcessor.findSystemTitleEntry();
}

QStringList EraEngine::getEventEntries() const
{
		return m_scriptProcessor.getEventEntries();
}

QStringList EraEngine::getAllEntryPoints() const
{
		QStringList entries;
		for (const auto& entry : m_scriptProcessor.entryPoints()) {
				entries.append(entry.name);
		}
		return entries;
}

void EraEngine::registerEventsWithManager()
{
		for (const auto& event : m_scriptProcessor.getEventEntries()) {
				m_eventManager.registerEvent(event, "", 0);
		}
}

void EraEngine::executeSystemEntryPoint()
{
		QString entryPoint = getSystemEntryPoint();
		if (!entryPoint.isEmpty()) {
				executeScript(entryPoint);
		}
}

void EraEngine::executeSystemTitleEntry()
{
		QString titleEntry = getSystemTitleEntry();
		if (!titleEntry.isEmpty()) {
				executeScript(titleEntry);
		}
}

void EraEngine::runSystem()
{
		qDebug() << "[EraEngine] Running system...";
		
		// Resolve entry point and execute
		const QString entryPoint = getSystemEntryPoint();
		QString scriptName;
		
		if (!entryPoint.isEmpty()) {
			// Use @SYSTEM or @SYSTEM_INIT entry point
			QFileInfo fileInfo(entryPoint);
			scriptName = fileInfo.baseName();
		} else {
			// Fallback to @SYSTEM_TITLE entry point if available
			const QString titleEntry = getSystemTitleEntry();
			if (!titleEntry.isEmpty()) {
				QFileInfo fileInfo(titleEntry);
				scriptName = fileInfo.baseName();
			} else {
				scriptName = "system";  // ultimate fallback
			}
		}
		
		qDebug() << "[EraEngine] Executing script:" << scriptName;
		
		// Build execution queue for the entry point script
		if (!m_parseTable.loadScript(scriptName, m_executionEngine.getErbLoader().getLogicalLinesCI(scriptName))) {
			qWarning() << "[EraEngine] Failed to load script into parse table:" << scriptName;
		}
		
		// Set entry point to start execution
		// The entry point is the label name (e.g., "SYSTEM", "SYSTEM_TITLE")
		// We need to find the label in the loaded scripts
		QString scriptPath = getSystemEntryPoint();
		if (scriptPath.isEmpty()) {
			scriptPath = getSystemTitleEntry();
		}
		if (!scriptPath.isEmpty()) {
			// Extract the base name (script name without extension)
			QFileInfo fileInfo(scriptPath);
			QString scriptBaseName = fileInfo.baseName();
			
			// Look for the @SYSTEM or @SYSTEM_TITLE label in the loaded scripts
			// The label name should match the script base name (case-insensitive)
			QString labelName = scriptBaseName.toUpper();
			if (labelName == "SYSTEM" || labelName == "SYSTEM_TITLE") {
				// Use the base name as the label name
				m_parseTable.setEntryPoint(scriptBaseName);
			} else {
				// Try common entry point labels
				QStringList commonLabels = {"SYSTEM", "SYSTEM_TITLE", "MAIN", "MAIN_LOOP"};
				for (const QString& label : commonLabels) {
					if (m_parseTable.getLabelPosition(scriptBaseName, label) >= 0) {
						m_parseTable.setEntryPoint(label);
						break;
					}
				}
			}
		}
		
		// Start the execution pump to begin instruction execution
		// The pump uses iterative execution to avoid stack overflow
		m_parseTable.startExecutionPump();
		
		qDebug() << "[EraEngine] System execution pump started";
		emit systemStarted();
		// Note: systemFinished() is NOT emitted here because execution is synchronous
		// The execution happens in pumpInstructions() which blocks until complete
}

void EraEngine::gotoTitle()
{
		m_processState.setSystemState(SystemStateCode::Title_Begin);
		m_statusManager.transitionToTitle();
}

void EraEngine::executeEvent(const QString& eventName)
{
		m_eventManager.executeEvent(eventName);
}

void EraEngine::queueEvent(const QString& eventName)
{
		m_eventManager.queueEvent(eventName);
}

void EraEngine::processEvents()
{
		m_eventManager.processEvents();
}

void EraEngine::clearEventQueue()
{
		m_eventManager.clearEventQueue();
}

QStringList EraEngine::getRegisteredEvents() const
{
		return m_eventManager.getRegisteredEvents();
}

bool EraEngine::isEventExecuted(const QString& eventName) const
{
		return m_eventManager.isEventExecuted(eventName);
}
