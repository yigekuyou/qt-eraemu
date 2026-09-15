#include "eraengine.h"


// Include the input system header for EraTetrisInputState
#include "input_system.h"

EraEngine::EraEngine(QObject *parent)
		: QObject(parent),
			m_variableStorage(),
			m_expressionEvaluator(),
			m_functionSystem(),
			m_instructionSystem(),
			m_gameFlowControl(),
			m_renderingSystem(),
			m_fileSystem(),
			m_binaryIo(),
			m_inputSystem(),
			m_executionEngine(&m_variableStorage),
			m_processState(),
			m_systemProcessor(&m_processState),
			m_configLoader(),
			m_scriptProcessor(),
			m_inputHandler(),
			m_eventManager()
{
		// All components are initialized by their constructors
}

void EraEngine::registerTypes()
{
		qmlRegisterType<GameBaseData>("io.yigekuoyou.eraengine", 1, 0, "GameBaseData");
		qmlRegisterType<EraEngine>("io.yigekuoyou.eraengine", 1, 0, "EraEngine");

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
				m_gameDirectory = directory;
				m_fileSystem.setRootDir(directory);
				reload();
				emit gameDirectoryChanged();
		}
}

void EraEngine::reload()
{
		// Reload scripts from current directory
		if (!m_gameDirectory.isEmpty()) {
				// First load gamebase data
				loadGameBaseData();

				// Then load scripts
				m_executionEngine.loadScripts(m_gameDirectory);
		}
}

void EraEngine::loadGameBaseData()
{
		// GameBase.csv should be in the game directory
		QString gameBasePath = m_gameDirectory;
		QString localPath = QUrl(gameBasePath).toLocalFile();

		qDebug() << "[DEBUG] Looking for GameBase.csv in:" << localPath;

		// Find the actual CSV directory (case-insensitive)
		QString csvDir = m_fileSystem.findActualDir(localPath, "CSV");
		qDebug() << "[DEBUG] Found CSV directory:" << csvDir;
		gameBasePath=m_fileSystem.getPathWithActualCase(localPath,csvDir);
		QString GameBaseCsv = m_fileSystem.findActualDir(gameBasePath, "GameBase.csv");
		qDebug() << "[DEBUG] Found GameBase name:" << GameBaseCsv;
		gameBasePath = gameBasePath+"/"+GameBaseCsv;
		qDebug() << "[DEBUG] GameBase path:" << gameBasePath;

		// Load GameBase.csv
		CsvLoader csvLoader;
		if (csvLoader.loadFile(gameBasePath)) {
			qDebug() << "[DEBUG] CSV file loaded successfully";
			// Parse the data and populate gameBaseData
			// GameBase.csv format: "列名,值"
			// Example: "タイトル,era俄罗斯方块"

			QStringList tableNames = csvLoader.getTableNames();
			if (!tableNames.isEmpty()) {
				QString tableName = tableNames.first();
				int rowCount = csvLoader.getRowCount(tableName);

				qDebug() << "[DEBUG] Table:" << tableName << "has" << rowCount << "rows";

				// Parse each row
				for (int row = 0; row < rowCount; ++row) {
					// Get the key-value pair
					QString key = csvLoader.getString(tableName, row, 0);
					QString value = csvLoader.getString(tableName, row, 1);

					qDebug() << "[DEBUG] Row" << row << "- Key:" << key << "Value:" << value;

					// Map Japanese keys to GameBaseData properties
					if (key == "ウィンドウタイトル") {
						m_gameBaseData.set("ウィンドウタイトル", value);
						qDebug() << "[DEBUG] Set windowTitle to:" << value;
					} else if (key == "タイトル") {
						m_gameBaseData.set("タイトル", value);
						qDebug() << "[DEBUG] Set title to:" << value;
					} else if (key == "作者") {
						m_gameBaseData.set("作者", value);
						qDebug() << "[DEBUG] Set author to:" << value;
					} else if (key == "バージョン") {
						m_gameBaseData.set("バージョン", value);
						qDebug() << "[DEBUG] Set version to:" << value;
					} else if (key == "製作年") {
						m_gameBaseData.set("製作年", value);
						qDebug() << "[DEBUG] Set releaseYear to:" << value;
					} else if (key == "追加情報") {
						m_gameBaseData.set("追加情報", value);
						qDebug() << "[DEBUG] Set additionalInfo to:" << value;
					}
				}

				// ==========================================
				// 在这里调用 toMap() 获取 QVariantMap 格式的数据
				// ==========================================
				QVariantMap gameDataBaseMap = m_gameBaseData.toMap();
				qDebug() << "[DEBUG] GameBaseData as map:" << gameDataBaseMap;
				qDebug() << "[INFO] GameBase.csv loaded successfully";
			}
		} else {
			qDebug() << "[INFO] GameBase.csv not found or could not be loaded";
		}
}
// Config loading helper methods (callable from QML)
void EraEngine::loadConfig(const QString& filePath, int precedence)
{
		m_configLoader.loadConfigFile(filePath, precedence);
		qDebug() << "[DEBUG] Config loaded:" << filePath << "precedence:" << precedence;
}

void EraEngine::mergeConfig(const QString& filePath, int precedence)
{
		m_configLoader.mergeConfig(filePath, precedence);
		qDebug() << "[DEBUG] Config merged:" << filePath << "precedence:" << precedence;
}

QString EraEngine::getConfig(const QString& key) const
{
		return m_configLoader.getConfig(key);
}

bool EraEngine::hasConfig(const QString& key) const
{
		return m_configLoader.hasConfig(key);
}

// Script processing helper methods (callable from QML)
void EraEngine::processScripts(const QString& scriptDir)
{
		m_scriptProcessor.processScripts(scriptDir);
		qDebug() << "[DEBUG] Scripts processed from:" << scriptDir;
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
		return m_scriptProcessor.findEventEntries();
}

QStringList EraEngine::getAllEntryPoints() const
{
		return m_scriptProcessor.findAllEntryPoints();
}

// Event execution helper methods (callable from QML)
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
