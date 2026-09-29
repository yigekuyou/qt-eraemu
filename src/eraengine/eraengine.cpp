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
			m_systemStateMachine(&m_processState),
			m_statusManager(),
			m_signalManager(this),
			m_parseTable(&m_processState, &m_executionEngine),
			m_scriptRunner(&m_parseTable, &m_executionEngine, &m_processState, &m_variableStorage),
			m_console(),
			m_guiManager(),
			m_identifierDictionary(),
			m_configLoader(),
			m_scriptProcessor(),
			m_inputHandler(),
			m_eventManager(),
			m_connectionManager(this)
{
		// Set variable storage in parse table for condition evaluation
		m_parseTable.setVariableStorage(&m_variableStorage);
		
		// Set ParseTable reference in ExecutionEngine for CALL/RETURN integration
		m_executionEngine.setParseTable(&m_parseTable);

		// ---- 系统状态机：依赖注入 ----
		m_systemStateMachine.setParseTable(&m_parseTable);
		m_systemStateMachine.setVariableStorage(&m_variableStorage);
		m_systemStateMachine.setScriptRunner(&m_scriptRunner);
        m_systemStateMachine.setPacingEnabled(true);
		m_scriptRunner.setSystemStateMachine(&m_systemStateMachine);
		// 实时/限时输入：AWAIT / INPUTMOUSEKEY 超时 / TONEINPUT 超时 用 QTimer 驱动
		m_systemStateMachine.setTimer([this](int ms, std::function<void()> cb) {
			QTimer::singleShot(ms, this, [cb]() { cb(); });
		});
		buildSystemHost();

		// ---- 执行链：信号与槽，由程序状态控制器驱动 ----
		// 表达式求值器挂上“用户自定义函数”回调（执行链 / 执行引擎 / 解析表共享同一求值器）
		m_parseTable.setExpressionEvaluator(&m_expressionEvaluator);
		m_executionEngine.setExpressionEvaluator(&m_expressionEvaluator);
		m_scriptRunner.setExpressionEvaluator(&m_expressionEvaluator);
		// 变量字符串下标（CSV 常量名）解析依赖常量名表
		m_expressionEvaluator.setConstantTable(&m_constantTable);
		// LINECOUNT = **逻辑行数**（C# logicalLineCount）：折行产生的续行不计入，
		// eraTetris 用它做「CLEARLINE LINECOUNT - FIRSTLINE」清本帧，必须与
		// deleteLine 的计数口径一致（都只数 IsLogicalLine 的行）
		m_expressionEvaluator.setLineCountProvider([this]() -> qint64 {
			return m_console.logicalLineCount();
		});
		// 解析期也需要常量名表（CFLAG:ARG:現在位置 之类的常量名下标）
		m_parseTable.setConstantTable(&m_constantTable);
		m_parseTable.setGameBaseData(&m_gameBaseData);
		m_executionEngine.getErbLoader().setConstantTable(&m_constantTable);

		// ---- 界面管理：控制台 + 窗口标题 ----
		m_guiManager.setConsole(&m_console);
		// 显示层的排版口径：DRAWLINE 用字符（C# 「DRAWLINE文字」，默认 "-"）与
		// 一行最大单位数（DrawableWidth / 列宽）—— 与 GuiManager 的窗口宽保持同步
		m_executionEngine.setDrawLineString(
			m_configLoader.hasConfig(QStringLiteral("DRAWLINE文字"))
				? m_configLoader.getConfig(QStringLiteral("DRAWLINE文字"))
				: QStringLiteral("-"));
		m_executionEngine.setMaxLineUnits(
			qMax(1, m_guiManager.windowWidth() / qMax(1, m_guiManager.fontSize() / 2)));
		// 窗口宽/字号变化后同步（DRAWLINE 的铺满宽度由它决定）
		connect(&m_guiManager, &GuiManager::settingsChanged, this, [this]() {
			m_executionEngine.setMaxLineUnits(
				qMax(1, m_guiManager.windowWidth() / qMax(1, m_guiManager.fontSize() / 2)));
		});
		m_guiManager.setStartDirectory(m_gameDirectory);
		m_guiManager.setWindowTitle(m_gameBaseData.windowTitle());
		connect(&m_gameBaseData, &GameBaseData::dataChanged, this, [this]() {
			m_guiManager.setWindowTitle(m_gameBaseData.windowTitle());
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
        connect(&m_executionEngine, &ExecutionEngine::consolePrintTemplate,
                &m_console, &ConsoleBackend::printTemplate);
        connect(&m_executionEngine, &ExecutionEngine::consolePrintImage,
                &m_console, &ConsoleBackend::printImage);
		connect(&m_executionEngine, &ExecutionEngine::consoleClearLines,
				&m_console, &ConsoleBackend::clearLines);
		connect(&m_executionEngine, &ExecutionEngine::consolePrintButton, this,
				[this](const QString& text, qint64 intValue, const QString& strValue, bool isString) {
					if (isString) m_console.printButtonStr(text, strValue);
					else m_console.printButton(text, intValue);
				});
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

        connect(&m_processState, &ProcessState::execStateChanged, this, [this](ExecState state) {
            if (state == ExecState::Continue || state == ExecState::Halt || state == ExecState::Error)
                m_console.notifyInputDone();
        });

		// ---- 输入：控制台 -> 执行链 ----
        connect(&m_console, &ConsoleBackend::mouseKeySubmitted, this, &EraEngine::provideMouseKey);
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

		// ProcessState emits stateChanged (no args) when state changes
		// Connect to SystemStatusManager to keep state synchronized
		connect(&m_processState, &ProcessState::stateChanged,
				[this]() {
			m_statusManager.onStateChanged(m_processState.getState());
		});

		// Connect system state machine signals to signal manager / status manager
		connect(&m_systemStateMachine, &SystemStateMachine::stateChanged,
				&m_signalManager, [this](StateCode, StateCode newState) {
			m_signalManager.emitStateChanged(newState);
			m_statusManager.onStateChanged(newState);
		});
		connect(&m_systemStateMachine, &SystemStateMachine::inputRequested,
				&m_signalManager, &SignalManager::emitInputRequested);
		connect(&m_systemStateMachine, &SystemStateMachine::inputRequested, this,
				[this](SystemStateCode state) {
			if (state != SystemStateCode::Normal)
                m_console.notifyInputRequested(SystemStateMachine::stateName(state));
		});
		connect(&m_systemStateMachine, &SystemStateMachine::errorOccurred, this,
				[](const QString& message) {
			qWarning().noquote() << "[SystemStateMachine]" << message;
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
				m_guiManager.setGameDirectory(dir);
				m_guiManager.setStartDirectory(dir);
				emit gameDirectoryChanged();
		}
		// loadAsync() is the normal QML loading path. Keep the image provider in
		// sync here as well as in setGameDirectory(), otherwise image://emuera
		// requests still point at the previous game (or at an empty root).
		ResourceImageProvider::setRoot(dir);
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
				m_guiManager.setGameDirectory(dir);
				m_guiManager.setStartDirectory(dir);
				ResourceImageProvider::setRoot(dir);
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
				// 同步装载完成（GUI 可据此启动系统状态机：runSystem()）
				emit scriptsLoaded(true);
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
		// 界面设置（字体/字号/行高/颜色/帧率/窗口…）从配置读入（对齐 C# ConfigData）
		m_guiManager.loadFromConfig(m_configLoader);
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
		// 界面设置回写（字体/字号/行高/颜色/帧率/窗口…），再保存全部已加载配置文件
		m_guiManager.saveToConfig(m_configLoader);
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
		// 随机种子：启动时自动产生；`--seed` / setRandomSeed 固定后可整局复现
		qDebug() << "[EraEngine] 随机种子:" << m_expressionEvaluator.randomSeed()
		         << "（MT19937，可用 --seed 复现）";
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
		if (!csvLoader.loadFile(gameBasePath)) {
				qDebug() << "[DEBUG] Failed to load GameBase.csv";
				return;
		}
		// **真正的载入**：把每一行 (键, 值) 写进 GameBaseData
		// （对齐 C# GameBase.LoadGameBase；此前只打印了行列数，导致 %GAMEBASE_TITLE% 等全为空）
		const QString table = csvLoader.getTableNames().isEmpty()
		                          ? QString() : csvLoader.getTableNames().first();
		const int rows = csvLoader.getRowCount(table);
		int applied = 0;
		for (int r = 0; r < rows; ++r) {
				const QString key = csvLoader.getString(table, r, 0).trimmed();
				if (key.isEmpty()) continue;
				const QString value = csvLoader.getString(table, r, 1);
				m_gameBaseData.set(key, value);
				++applied;
		}
		// GameBaseData::set 只认日文键：英文键（如 "TITLE"）走 get() 的映射，故这里不强求
		qDebug() << "[EraEngine] GameBase.csv:" << applied << "项 标题:" << m_gameBaseData.title()
		         << " 版本:" << m_gameBaseData.version();
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

		if (m_parseTable.scriptNames().isEmpty()) {
				qWarning() << "[EraEngine] 未装载脚本，无法启动系统";
				emit systemFinished();
				return;
		}

		// C# 语义：系统状态机**从 Title_Begin 起步**；beginTitle 会调用 @SYSTEM_TITLE
		// （不存在则绘制标准标题画面）。@SYSTEM_TITLE 里的 BEGIN FIRST/SHOP 由脚本发出。
		// —— 不能把 @SYSTEM_TITLE 当普通入口脚本直接执行（那样状态还是 Title_Begin，
		//    脚本里的 BEGIN FIRST 会被判为「不允许 BEGIN」）。
		qDebug() << "[EraEngine] 系统状态机启动（Title_Begin）"
		         << " SYSTEM_TITLE:" << (m_parseTable.hasLabel(QStringLiteral("SYSTEM_TITLE")) ? "有" : "无")
		         << " 脚本数:" << m_parseTable.scriptNames().size()
		         << " 告警:" << m_parseTable.parseWarningCount()
		         << " 变量:" << m_parseTable.variableTable().count();

		m_processState.setSystemState(SystemStateCode::Title_Begin);

		emit systemStarted();
		const ExecState st = m_systemStateMachine.run();
		if (st == ExecState::WaitInput || st == ExecState::WaitSystemInput) {
				qDebug() << "[EraEngine] execution suspended, waiting for input";
		} else {
				qDebug() << "[EraEngine] execution finished, state=" << static_cast<int>(st);
		}
		emit systemFinished();
}

void EraEngine::provideInput(qint64 value)
{
		// 用户操作交付：写入 RESULT/systemResult 并让状态机继续
		m_console.notifyInputDone();
		m_systemStateMachine.resume(value);
}

void EraEngine::provideInputString(const QString& value)
{
		// 字符串输入：写入 RESULTS（局部字符串槽）后继续
		m_console.notifyInputDone();
		m_systemStateMachine.resumeString(value);
}

void EraEngine::provideInputValues(const QVariantList& values)
{
		// 多值输入（INPUTMOUSEKEY：RESULT:0..4 = 类型 / 坐标 / 按键）
		m_console.notifyInputDone();
		QList<qint64> ints;
		ints.reserve(values.size());
		for (const QVariant& v : values) ints.append(v.toLongLong());
		m_systemStateMachine.deliverInputValues(ints);
}

void EraEngine::provideMouseKey(int type, int r1, int r2, int r3, int r4)
{
		// 对齐 C# InputResult5：RESULT:0..4
		provideInputValues({type, r1, r2, r3, r4});
}

// ---------------------------------------------------------------------------
// 系统状态机 -> 引擎子系统 的适配层（SystemHost）
// ---------------------------------------------------------------------------
void EraEngine::buildSystemHost()
{
		SystemHost host;

		// ---- 输出 -> ConsoleBackend ----
		host.printSingleLine = [this](const QString& text) {
				m_console.print(text);
				m_console.newline();
		};
		host.print = [this](const QString& text) { m_console.print(text); };
		host.printC = [this](const QString& text, bool) { m_console.print(text); };
		host.printTemporaryLine = [this](const QString& text) {
				m_console.print(text);
				m_console.newline();
		};
		host.printError = [this](const QString& text) {
				m_console.print(text);
				m_console.newline();
		};
		host.deleteLine = [this](int count) { m_console.clearLines(count); };
		host.printBar = [this]() {
				m_console.print(QString(44, QChar(0x2015)));   // ――…
		};
		host.newLine = [this]() { m_console.newline(); };
		host.setAlignment = [this](int mode) {
				if (mode == 1) m_console.setAlignment(ConsoleAlign::Center);
				else if (mode == 2) m_console.setAlignment(ConsoleAlign::Right);
				else m_console.setAlignment(ConsoleAlign::Left);
		};
		host.refreshStrings = [this]() { m_console.flush(); };
		host.printFlush = [this]() { m_console.flush(); };

		// ---- 输入等待（UI 通知由 inputRequested 信号统一处理）----
		host.readAnyKey = [this]() { m_console.notifyInputRequested(QStringLiteral("ANYKEY")); };

		// ---- GameBase（标准标题画面）----
		host.scriptTitle = [this]() { return m_gameBaseData.title(); };
		host.scriptVersionText = [this]() { return m_gameBaseData.version(); };
		host.scriptAutherName = [this]() { return m_gameBaseData.author(); };
		host.scriptYear = [this]() { return m_gameBaseData.releaseYear(); };
		host.scriptDetail = [this]() { return m_gameBaseData.additionalInfo(); };
		host.scriptVersion = [this]() -> qint64 {
				bool ok = false;
				const qint64 v = m_gameBaseData.version().toLongLong(&ok);
				return ok ? v : 0;
		};

		// ---- 配置 ----
		const auto boolCfg = [this](const QStringList& keys, bool fallback) {
				for (const QString& key : keys) {
						if (m_configLoader.hasConfig(key)) return m_configLoader.getBool(key, fallback);
				}
				return fallback;
		};
		const auto intCfg = [this](const QStringList& keys, int fallback) {
				for (const QString& key : keys) {
						if (m_configLoader.hasConfig(key)) return m_configLoader.getInt(key, fallback);
				}
				return fallback;
		};
		host.autoSave = [boolCfg]() { return boolCfg({QStringLiteral("オートセーブ"),
		                                               QStringLiteral("AutoSave")}, false); };
		host.maxShopItem = [intCfg]() { return intCfg({QStringLiteral("アイテムの最大数"),
		                                                QStringLiteral("MaxShopItem")}, 100); };
		host.comAbleDefault = [intCfg]() { return intCfg({QStringLiteral("COM_ABLE初期値"),
		                                                   QStringLiteral("ComAbleDefault")}, 1); };
		host.printCPerLine = [intCfg]() { return intCfg({QStringLiteral("PRINTC の表示数"),
		                                                  QStringLiteral("PrintCPerLine")}, 0); };
		host.compatiCallEvent = [boolCfg]() { return boolCfg({QStringLiteral("イベント関数のCALLを許可"),
		                                                       QStringLiteral("CompatiCallEvent")}, false); };
		host.titleMenuString = [](int index) {
				return index == 0 ? QStringLiteral("开始游戏") : QStringLiteral("读取存档");
		};

		m_systemStateMachine.setHost(std::move(host));
}

void EraEngine::setRandomSeed(quint32 seed)
{
		m_expressionEvaluator.setRandomSeed(seed);
		qInfo().noquote() << "[EraEngine] 随机种子已固定:" << seed;
		emit randomSeedChanged();
}

void EraEngine::randomizeRandom()
{
		m_expressionEvaluator.randomize();
		emit randomSeedChanged();
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
