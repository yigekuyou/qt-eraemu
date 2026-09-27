/*
 * emuera —— Emuera（ERB 脚本引擎）的 Qt6 + QML/C++ 移植
 * Copyright (C) 2026  yigekuyou
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "eraengine.h"

// Include ConsoleDisplay for QML singleton registration
#include "rendering_system.h"
#include "system_status_manager.h"
#include "signal_hub.h"
#include "erb_loader.h"
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
		m_scriptRunner(&m_parseTable, &m_executionEngine, &m_processState, &m_variableStorage),
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

		// ---- 执行链：信号与槽，由程序状态控制器驱动 ----
		// 表达式求值器挂上“用户自定义函数”回调（执行链 / 执行引擎 / 解析表共享同一求值器）
		m_parseTable.setExpressionEvaluator(&m_expressionEvaluator);
		m_executionEngine.setExpressionEvaluator(&m_expressionEvaluator);
		m_scriptRunner.setExpressionEvaluator(&m_expressionEvaluator);
		// 变量字符串下标（CSV 常量名）解析依赖常量名表
		m_expressionEvaluator.setConstantTable(&m_constantTable);
		// 解析期也需要常量名表（CFLAG:ARG:現在位置 之类的常量名下标）
		m_parseTable.setConstantTable(&m_constantTable);
		m_executionEngine.getErbLoader().setConstantTable(&m_constantTable);

		// 控制器发出 continueExecution() -> 执行链槽 onContinueExecution()
		connect(&m_processState, &ProcessState::continueExecution,
				&m_scriptRunner, &ScriptRunner::onContinueExecution);

		connect(&m_scriptRunner, &ScriptRunner::finished, this, [this]() {
			emit systemFinished();
		});
		connect(&m_scriptRunner, &ScriptRunner::errorOccurred, this, [](const QString& msg) {
			qWarning() << "[ScriptRunner]" << msg;
		});
		connect(&m_scriptRunner, &ScriptRunner::inputRequested, this, [this](const QString& kind) {
			qDebug() << "[ScriptRunner] waiting for user input:" << kind
					 << "state=" << static_cast<int>(m_processState.getExecState());
			m_console.notifyInputRequested(kind);
		});

		// ---- 显示层：执行引擎输出 -> ConsoleBackend ----
		connect(&m_executionEngine, &ExecutionEngine::consolePrint, this,
				[this](const QString& text, bool newline) {
					m_console.print(text);
					if (newline) m_console.newline();
				});
		connect(&m_executionEngine, &ExecutionEngine::consoleClearLines,
				&m_console, &ConsoleBackend::clearLines);
		connect(&m_executionEngine, &ExecutionEngine::consoleResetColor,
				&m_console, &ConsoleBackend::resetColor);
		connect(&m_executionEngine, &ExecutionEngine::consoleRedraw, this,
				[this](const QString&) { m_console.flush(); });
		connect(&m_executionEngine, &ExecutionEngine::consoleAlign, this,
				[this](const QString& align) {
					const QString a = align.toUpper();
					if (a == QLatin1String("CENTER")) m_console.setAlignment(ConsoleAlign::Center);
					else if (a == QLatin1String("RIGHT")) m_console.setAlignment(ConsoleAlign::Right);
					else m_console.setAlignment(ConsoleAlign::Left);
				});
		connect(&m_executionEngine, &ExecutionEngine::consoleColor, this,
				[this](const QString& colorName) {
					QColor c(colorName);
					if (!c.isValid()) {
						bool ok = false;
						uint v = colorName.toUInt(&ok, 0);   // 0xRRGGBB
						if (ok) c = QColor::fromRgb(v);
					}
					if (c.isValid()) m_console.setColor(c);
				});

		// ---- 输入：控制台 -> 执行链 ----
		connect(&m_console, &ConsoleBackend::inputSubmitted, this,
				[this](qint64 value) { provideInput(value); });
		connect(&m_console, &ConsoleBackend::inputSubmittedString, this,
				[this](const QString& value) { provideInputString(value); });
		
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

		// Register enum types
		qRegisterMetaType<StateCode>("StateCode");
		qRegisterMetaType<BeginType>("BeginType");
		qRegisterMetaType<LineKind>("LineKind");
		qRegisterMetaType<VariableTypes::Type>("VariableTypes::Type");
		qRegisterMetaType<VariableTypes::Scope>("VariableTypes::Scope");
		qRegisterMetaType<VariableTypes::Dimension>("VariableTypes::Dimension");
		qRegisterMetaType<VariableTypes::Flag>("VariableTypes::Flag");
		qRegisterMetaType<ScriptPosition>("ScriptPosition");
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

void EraEngine::loadAsync(const QString& directory)
{
		QString dir = directory;
		if (dir.startsWith(QLatin1String("file://"))) {
				dir = QUrl(dir).toLocalFile();
		}
		if (m_gameDirectory != dir) {
				m_gameDirectory = dir;
				m_fileSystem.setRootDir(dir);
				emit gameDirectoryChanged();
		}
		reloadAsync();
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
				// 对齐 C#：先解析 ErbDir / CsvDir（只在这两个目录内检索）
				resolveGameDirs();

				// 配置文件：_default.config / _fixed.config 在 CSV 目录，
				// emuera.config 在游戏根目录（C# ConfigData.configPath = ExeDir + "emuera.config"）
				loadConfigFiles();
				resolveTextConfig();   // 编码 / 子目录检索（配置项在编码嗅探下才读得到）

				// GameBase.csv / 常量 CSV：只从 CSV 目录读取
				loadGameBaseData();
				loadConstantData();

				// 脚本：只从 ERB 目录装载
				qDebug() << "[DEBUG] Loading scripts from:" << m_erbDir;
				m_executionEngine.loadScripts(m_erbDir);
				m_parseTable.finalizeParse();   // 全量回填变量类型（一次性）
				qDebug() << "[DEBUG] Scripts loaded";

				collectEntryPoints();
				loadFinishedHook();
		}
}

void EraEngine::resolveGameDirs()
{
		if (m_gameDirectory.isEmpty()) {
				m_csvDir.clear();
				m_erbDir.clear();
				return;
		}
		// 与 C# 的 Program.CsvDir / Program.ErbDir 对齐：大小写不敏感地定位子目录
		m_csvDir = m_fileSystem.resolveSubDir(m_gameDirectory, QStringList{"CSV", "csv"});
		m_erbDir = m_fileSystem.resolveSubDir(m_gameDirectory, QStringList{"ERB", "erb"});
		if (m_csvDir.isEmpty()) {
				qWarning() << "[EraEngine] CSV 目录未找到：" << m_gameDirectory;
		}
		if (m_erbDir.isEmpty()) {
				qWarning() << "[EraEngine] ERB 目录未找到：" << m_gameDirectory;
		}
}

void EraEngine::loadConfigFiles()
{
		m_configLoader.clearFiles();   // 幂等：允许在探测出回退编码后重读一次
		// 1. _default.config（最低优先级，位于 CSV 目录）
		// 2. emuera.config（中等优先级，位于游戏根目录）
		// 3. _fixed.config（最高优先级，位于 CSV 目录）
		if (!m_csvDir.isEmpty()) {
				const QString defaultConfig = QDir(m_csvDir).absoluteFilePath("_default.config");
				if (m_fileSystem.fileExists(defaultConfig)) m_configLoader.loadConfigFile(defaultConfig, 0);
		}
		const QString mainConfig = QDir(m_gameDirectory).absoluteFilePath("emuera.config");
		if (m_fileSystem.fileExists(mainConfig)) m_configLoader.loadConfigFile(mainConfig, 1);
		if (!m_csvDir.isEmpty()) {
				const QString fixedConfig = QDir(m_csvDir).absoluteFilePath("_fixed.config");
				if (m_fileSystem.fileExists(fixedConfig)) m_configLoader.loadConfigFile(fixedConfig, 2);
				// _Rename.csv：行内 [[..]] 替换（对齐 C# ParserMediator.LoadEraExRenameFile）
				const QString renameCsv = QDir(m_csvDir).absoluteFilePath("_Rename.csv");
				if (m_fileSystem.fileExists(renameCsv)) {
						m_executionEngine.getErbLoader().loadRenameFile(renameCsv);
				}
		}
}

void EraEngine::loadConstantData()
{
		if (m_csvDir.isEmpty()) return;

		// 常量名表（CSV 名 → 下标）：只从 CSV 目录读取，对齐 C# ConstantData
		const int tables = m_constantTable.loadCsvDirectory(m_csvDir, m_searchSubdirectory);

		// 变量尺寸表（对齐 C# VariableData 读取 VariableSize.CSV）
		int sizesLoaded = 0;
		const QStringList sizeCandidates = m_fileSystem.listFiles(m_csvDir,
		                                                          QStringList{"VariableSize.csv"}, false);
		for (const QString& path : sizeCandidates) {
				if (m_variableStorage.loadVariableSizes(path)) ++sizesLoaded;
		}
		qDebug() << "[EraEngine] CSV 目录:" << m_csvDir
		         << " 常量表:" << tables << "(" << m_constantTable.nameCount() << "项)"
		         << " VariableSize:" << sizesLoaded;
}

// 从已加载配置里取「编码 / 子目录检索」等引擎级设置
//  · サブディレクトリを検索する  —— Config.SearchSubdirectory（C# 默认 false，era 游戏多配 YES）
//  · TextEncoding / テキストエンコーディング / 文字コード —— 强制读编码（缺省 AUTO = 逐文件嗅探）
void EraEngine::resolveTextConfig()
{
		m_searchSubdirectory = m_configLoader.getBool(QString::fromUtf8("サブディレクトリを検索する"), true);

		TextEncoding enc = TextEncoding::Auto;
		const QStringList keys = {
				QStringLiteral("TextEncoding"),
				QStringLiteral("TextCodec"),
				QString::fromUtf8("テキストエンコーディング"),
				QString::fromUtf8("文字コード"),
		};
		for (const QString& key : keys) {
				if (!m_configLoader.hasConfig(key)) continue;
				const TextEncoding parsed = TextCodecUtil::fromName(m_configLoader.getConfig(key));
				if (parsed != TextEncoding::Auto) { enc = parsed; break; }
		}
		m_textEncoding = enc;
		m_configLoader.setReadEncoding(enc);
		m_executionEngine.getErbLoader().setReadEncoding(enc);

		// 回退编码的来源（按优先级）：
		//   1. 就地配置的 `内部で使用する東アジア言語`（对齐 C# useLanguage → Config.Encode 932/949/936/950）
		//   2. 没有配置时 -> **ROM 探测**（扫描 ERB/CSV/*.config 投票）；只取非 UTF-8 的结论
		//   3. 都没有 -> Latin-1
		TextEncoding fallback = TextEncoding::Latin1;
		bool fallbackFromConfig = false;
		const QString langKey = QString::fromUtf8("内部で使用する東アジア言語");
		if (m_configLoader.hasConfig(langKey)) {
				const TextEncoding langEnc =
						TextCodecUtil::fromLanguageName(m_configLoader.getConfig(langKey));
				if (langEnc != TextEncoding::Auto) {
						fallback = langEnc;
						fallbackFromConfig = true;
				}
		}
		if (!fallbackFromConfig && m_textEncoding == TextEncoding::Auto) {
				const QString probeReport = probeGameEncoding();
				// 没有置信度门槛：探测就是「严格解码命中最多者」
				if (m_probeResult.dominant == TextEncoding::ShiftJis
				    || m_probeResult.dominant == TextEncoding::Gbk
				    || m_probeResult.dominant == TextEncoding::Big5
				    || m_probeResult.dominant == TextEncoding::EucKr) {
						fallback = m_probeResult.dominant;
						qDebug().noquote() << "[EraEngine] ROM 探测结论用于回退编码:" << probeReport;
				}
		}
		TextCodecUtil::setFallbackEncoding(fallback);

		// 配置文件的编码也受回退编码影响（中文/韩文游戏的 emuera.config 本身就是 GBK/Big5）：
		// 若回退编码变了且不是 Latin-1，用新回退重读一次配置（3 个小文件，代价可忽略）。
		// 注意顺序：loadConfigFiles() -> resolveTextConfig() -> 其余装载都在这之后。
		if (!fallbackFromConfig
		    && TextCodecUtil::canDecode(fallback)
		    && fallback != TextEncoding::Latin1) {
				loadConfigFiles();   // 内部会先清空，避免重复累积
		}

		qDebug() << "[EraEngine] 文本编码:" << TextCodecUtil::name(enc)
		         << " 回退编码:" << TextCodecUtil::name(fallback)
		         << "(" << TextCodecUtil::backendFor(fallback) << ")"
		         << " 子目录检索:" << m_searchSubdirectory;
}

void EraEngine::setTextEncoding(const QString& name)
{
		m_textEncoding = TextCodecUtil::fromName(name);
		m_configLoader.setReadEncoding(m_textEncoding);
		m_executionEngine.getErbLoader().setReadEncoding(m_textEncoding);
}

void EraEngine::setTextWriteEncoding(const QString& name)
{
		const TextEncoding enc = TextCodecUtil::fromName(name);
		if (enc != TextEncoding::Auto) {
				m_configLoader.setWriteEncoding(enc);
		}
}

// ROM（游戏）编码探测：只读扫描，给出「这个游戏整体是什么编码」
QString EraEngine::probeGameEncoding()
{
		QStringList dirs;
		if (!m_erbDir.isEmpty()) dirs << m_erbDir;
		if (!m_csvDir.isEmpty()) dirs << m_csvDir;
		if (dirs.isEmpty() && !m_gameDirectory.isEmpty()) dirs << m_gameDirectory;

		m_probeResult = EncodingProbe::probe(dirs, EncodingProbe::defaultSuffixes(),
		                                     m_searchSubdirectory);
		const QString report = m_probeResult.summary();
		qDebug().noquote() << "[EraEngine] ROM 编码探测:\n" + report;
		return report;
}

bool EraEngine::saveEncodingToConfig()
{
		if (m_csvDir.isEmpty()) return false;
		const QString path = QDir(m_csvDir).absoluteFilePath(QStringLiteral("_fixed.config"));
		// 显式设置优先；否则用探测结论（前端可以「探测 -> 落盘」一步完成）
		TextEncoding toSave = m_textEncoding;
		if (toSave == TextEncoding::Auto) {
				toSave = m_probeResult.dominant;
		}
		const QString value = QString::fromLatin1(TextCodecUtil::name(toSave));
		const bool ok = m_configLoader.setConfigValueInFile(path, QStringLiteral("TextEncoding"),
		                                                   value, TextEncoding::Utf8Bom);
		if (ok) {
				// 立刻在内存里生效，避免等下次 reload
				m_configLoader.setConfig(QStringLiteral("TextEncoding"), value);
		}
		return ok;
}

int EraEngine::saveConfigFiles()
{
		return m_configLoader.saveAll();
}

QString EraEngine::configEncodingOf(const QString& filePath) const
{
		return QString::fromLatin1(TextCodecUtil::name(m_configLoader.encodingOf(filePath)));
}

void EraEngine::collectEntryPoints()
{
		// 入口点直接来自已装载的 AST（避免像以前那样把 ERB 目录再全量扫一遍）
		m_scriptProcessor.collectFromParseTable(&m_parseTable);
}

void EraEngine::loadFinishedHook()
{
		qDebug() << "[EraEngine] 装载完成：脚本" << m_parseTable.scriptNames().size()
		         << "个，告警" << m_parseTable.parseWarningCount()
		         << "条，变量" << m_parseTable.variableTable().count() << "个";
		for (int i = 0; i < qMin(12, m_parseTable.parseWarningCount()); ++i) {
				qWarning() << "  [parse warn]" << m_parseTable.parseWarnings().at(i);
		}
}

void EraEngine::reloadAsync()
{
		if (m_gameDirectory.isEmpty()) {
				emit scriptsLoaded(false);
				return;
		}
		resolveGameDirs();
		loadConfigFiles();
		resolveTextConfig();
		loadGameBaseData();
		loadConstantData();
		m_scriptProcessor.clear();

		ErbLoader& loader = m_executionEngine.getErbLoader();
		if (m_loadProgressConn) disconnect(m_loadProgressConn);
		if (m_loadCompletedConn) disconnect(m_loadCompletedConn);
		m_loadProgressConn = connect(&loader, &ErbLoader::loadProgress,
		                             this, &EraEngine::scriptsLoadProgress);
		m_loadCompletedConn = connect(&loader, &ErbLoader::loadCompleted, this, [this](bool ok) {
				// 语义阶段（类型回填 + 参数校验 + 入口点收集）必须在全量装载之后
				m_parseTable.finalizeParse();
				collectEntryPoints();
				loadFinishedHook();
				emit scriptsLoaded(ok);
		});

		emit scriptsLoadStarted();
		loader.loadDirectoryAsync(m_erbDir);
}

bool EraEngine::isLoadingScripts() const
{
		return m_executionEngine.getErbLoader().isLoading();
}

QStringList EraEngine::parseWarnings() const
{
		return m_parseTable.parseWarnings();
}

void EraEngine::loadGameBaseData()
{
		// 对齐 C#：GameBase.csv 位于 CSV 目录（Program.CsvDir + "GAMEBASE.CSV"）
		if (m_csvDir.isEmpty()) {
				qDebug() << "[EraEngine] CSV 目录未解析，跳过 GameBase.csv";
				return;
		}
		QString gameBasePath;
		const QStringList candidates = m_fileSystem.listFiles(m_csvDir,
		                                                      QStringList{"GameBase.csv"}, false);
		if (!candidates.isEmpty()) {
				gameBasePath = candidates.first();
		}
		if (gameBasePath.isEmpty()) {
				qDebug() << "[DEBUG] GameBase.csv not found in" << m_csvDir;
				return;
		}

		// Load GameBase.csv using CsvLoader
		CsvLoader csvLoader;
		if (csvLoader.loadFile(gameBasePath)) {
				qDebug() << "[DEBUG] GameBase.csv loaded successfully";
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

		// 入口点：直接按标签启动（标签来自已装载 AST，不再二次扫盘/重载脚本）
		QString label = m_scriptProcessor.findSystemLabel();
		if (label.isEmpty()) {
				label = m_scriptProcessor.findSystemTitleLabel();
		}
		if (label.isEmpty()) {
				const QStringList commonLabels = {"SYSTEM", "SYSTEM_TITLE", "MAIN", "MAIN_LOOP"};
				for (const QString& candidate : commonLabels) {
						if (m_parseTable.hasLabel(candidate)) { label = candidate; break; }
				}
		}
		if (label.isEmpty()) {
				qWarning() << "[EraEngine] 未找到入口点（@SYSTEM / @SYSTEM_TITLE）";
				emit systemFinished();
				return;
		}

		qDebug() << "[EraEngine] 入口点:" << label
		         << " 脚本数:" << m_parseTable.scriptNames().size()
		         << " 告警:" << m_parseTable.parseWarningCount()
		         << " 变量:" << m_parseTable.variableTable().count();
		m_parseTable.setEntryPoint(label);

		// 启动执行链（信号与槽：控制器置 Continue 并驱动 onContinueExecution）
		emit systemStarted();
		const ExecState st = m_scriptRunner.runToCompletion();
		if (st == ExecState::WaitInput || st == ExecState::WaitSystemInput) {
				qDebug() << "[EraEngine] execution suspended, waiting for input";
		} else {
				qDebug() << "[EraEngine] execution finished, state=" << static_cast<int>(st);
		}
		emit systemFinished();
}

void EraEngine::provideInput(qint64 value)
{
		// 用户操作交付：写入 RESULT 并请求控制器恢复执行
		m_console.notifyInputDone();
		m_scriptRunner.onInputProvided(value);
}

void EraEngine::provideInputString(const QString& value)
{
		// 字符串输入：写入 RESULTS（局部字符串槽）后恢复执行
		m_variableStorage.setLocalStr(0, value);
		m_console.notifyInputDone();
		m_scriptRunner.onInputProvided(0);
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
