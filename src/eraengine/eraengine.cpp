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
#include "GameView/dbus_debug.h"
#include <memory>
#include <iostream>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QObject>
#include <QtConcurrent/QtConcurrent>

EraEngine::~EraEngine() {
		// 后台语义阶段（finalizeParse）若仍在跑必须等它结束：它直接改写
		// m_parseTable / m_scripts，EraEngine 析构后再触碰就是释放后使用
		// （退出应用时恰好赶上装载的最后一秒）。
		if (m_semanticFuture.isRunning()) {
				m_semanticFuture.waitForFinished();
		}
}

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

		// ---- D-Bus 调试/控制入口（appemuera 的 /debug 对象；test_cli 无总线时静默）----
		m_dbusDebug = std::make_unique<EraDBusDebug>(this, this);
		// /debug saveScreenshot -> QML（Item.grabToImage 保存渲染结果）
		connect(m_dbusDebug.get(), &EraDBusDebug::screenshotRequested,
		        this, &EraEngine::screenshotRequested);
		// /debug startFrameCapture / stopFrameCapture -> QML（每帧抓取渲染）
		connect(m_dbusDebug.get(), &EraDBusDebug::frameCaptureRequested,
		        this, &EraEngine::frameCaptureRequested);
		connect(m_dbusDebug.get(), &EraDBusDebug::frameCaptureStopRequested,
		        this, &EraEngine::frameCaptureStopRequested);
		m_dbusDebug->registerOnBus();

		// ---- 系统状态机：依赖注入 ----
		m_systemStateMachine.setParseTable(&m_parseTable);
		m_systemStateMachine.setVariableStorage(&m_variableStorage);
		m_systemStateMachine.setScriptRunner(&m_scriptRunner);
        m_systemStateMachine.setPacingEnabled(true);
		m_scriptRunner.setSystemStateMachine(&m_systemStateMachine);
		// BINPUT/BINPUTS：无按钮时直接取缺省值（EE v31fix）
		m_scriptRunner.setButtonAvailableProvider([this]() -> bool {
			return m_console.hasEnabledButton();
		});
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
		// CHKDATA 存在判定：存档目录 <gameDir>/sav 的 save{##}.sav 探测
		//（对齐 C# VariableEvaluator.getSaveDataPath：save{index:00}.sav）
		// 对齐 C# EraDataState：0=OK（可载入） / 1=FILENOTFOUND（不存在）。
		// eraMegaten 用 `CHKDATA n` + `SIF !RESULT` 判断「有存档」，因此
		// 「存在 -> 0」是必须的（此前返回 1 语义相反）。
		m_expressionEvaluator.setSaveExistsProvider([this](const QString& saveName) -> qint64 {
			const qint64 idx = saveName.toLongLong();
			const QString path = m_gameDirectory + QStringLiteral("/sav/save%1.sav")
			                         .arg(idx, 2, 10, QLatin1Char('0'));
			return QFile::exists(path) ? 0 : 1;
		});
		// GETCOLOR / GETSTYLE：由执行引擎维护的当前颜色与样式位
		m_expressionEvaluator.setColorProvider([this]() -> qint64 {
			return m_executionEngine.currentColorValue();
		});
		m_expressionEvaluator.setStyleProvider([this]() -> qint64 {
			return m_executionEngine.currentStyleBits();
		});
		// CURRENTALIGN / GETFONT / CLIENTWIDTH / CLIENTHEIGHT / GETLINESTR /
		// HTML_GETPRINTEDSTR / HTML_POPPRINTINGSTR / DEBUGCLEAR / ISSKIP 等
		m_expressionEvaluator.setAlignProvider([this]() -> qint64 {
			return m_executionEngine.currentAlign();
		});
		m_expressionEvaluator.setFontProvider([this]() -> QString {
			return m_executionEngine.currentFontName();
		});
		m_expressionEvaluator.setFocusColorProvider([this]() -> qint64 {
			// C# Config.FocusColor（選択中文字色，默认 255,255,0）
			for (const QString& k : {QStringLiteral("選択中文字色"),
			                          QStringLiteral("FocusColor")}) {
				if (m_configLoader.hasConfig(k)) {
					const QStringList rgb = m_configLoader.getConfig(k).split(QLatin1Char(','));
					if (rgb.size() >= 3) {
						return qint64((rgb.at(0).toInt() << 16)
						              | (rgb.at(1).toInt() << 8)
						              | rgb.at(2).toInt()) & 0xFFFFFF;
					}
				}
			}
			return 0xFFFF00;
		});
		// ---- GETCONFIG / GETCONFIGS：emuera.config 取值 ----
		// 对齐 C# ConfigData.GetConfigValueInERB：**白名单**（只有列出的配置项允许
		// 被 ERB 读出；白名单外 GETCONFIG -> 0 / GETCONFIGS -> ""），返回形态按项类型：
		//   整数/Int64 -> 数值文本；<bool> -> "1"/"0"；<Color> -> ((R*256)+G)*256+B；
		//   <string>/<char>/<TextDrawingMode> -> 文本（GETCONFIGS 用）。
		// 此前完全没有接线 -> 一律 0/""，eraTW 的画像尺寸计算因此全部除以 0：
		//   `画像横幅 = 默认角色画像横幅 * 拡大比率 / GETCONFIG("フォントサイズ")` -> 0，
		//   于是 `<img ... height='0' width='0'>`，「画像尺寸 拡大/縮小」（选项 5/6）
		//   与尺寸档位（选项 4）在画面上完全看不出变化。
		m_expressionEvaluator.setConfigProvider([this](const QString& key, QString& value) -> bool {
			// 取值的白名单与类型转换在 ConfigLoader::configValueInErb（可单测）；
			// 这里只负责把「配置对象」接到求值器上。
			return m_configLoader.configValueInErb(key, value);
		});
		m_expressionEvaluator.setLineEmptyProvider([this]() -> qint64 {
			return m_console.currentLineEmpty() ? 1 : 0;
		});
		// RESETCOLOR / GETDEFCOLOR 的还原目标 = 配置文字色（C# Config.ForeColor）。
		// 配置在 setGameDirectory 之后才装载，因此这里用惰性 provider。
		const auto defaultForeColor = [this]() -> qint64 {
			for (const QString& k : {QStringLiteral("文字色"), QStringLiteral("ForeColor")}) {
				if (m_configLoader.hasConfig(k)) {
					const QStringList rgb = m_configLoader.getConfig(k).split(QLatin1Char(','));
					if (rgb.size() >= 3) {
						return qint64((rgb.at(0).toInt() << 16)
						              | (rgb.at(1).toInt() << 8)
						              | rgb.at(2).toInt()) & 0xFFFFFF;
					}
				}
			}
			return qint64(0xFFFFFF);
		};
		m_executionEngine.setDefaultColorProvider(defaultForeColor);
		m_expressionEvaluator.setDefaultColorProvider(defaultForeColor);
		m_expressionEvaluator.setClientSizeProvider([this]() -> QPair<int,int> {
			const int columnPx = qMax(1, m_guiManager.fontSize() / 2);
			const int lineHeight = qMax(1, m_guiManager.lineHeight());
			return { qMax(1, m_guiManager.windowWidth() / columnPx),
			         qMax(1, m_guiManager.windowHeight() / lineHeight) };
		});
		m_expressionEvaluator.setSkipProvider([this](int kind) -> int {
			// kind：0=ISSKIP（SKIPDISP） 1=MESSKIP 2=MOUSESKIP（GUI 专属，恒 0）
			return kind == 0 && m_executionEngine.skipDisp() ? 1 : 0;
		});
		m_expressionEvaluator.setLineStrProvider([this](int lineNo) -> QString {
			return m_console.lineText(lineNo);
		});
		// 扩展注册类（EE 等）的式中函数：实现住扩展侧（ee_extension.cpp）。
		// 引擎只提供「服务」（函数存在性 / 当前函数名 / 显示行）与「求值回调」——
		// 原生归原生、扩展归扩展，引擎不内联任何扩展名/实现。
		m_executionEngine.extensions().setExpressionServices(
			[this](const QString& name, bool caseInsensitive) -> int {
				return m_parseTable.functionExistsKind(name, caseInsensitive);
			},
			[this]() -> QString {
				// 当前执行中的函数名：调用帧栈顶的 callLabel（EE：__FUNCTION__ 同义）。
				// 深度 0（入口函数直接执行）时回退到入口标签。
				const Frame frame = m_parseTable.currentFrame();
				if (!frame.callLabel.isEmpty()) return frame.callLabel;
				return m_parseTable.getEntryPoint();
			},
			[this](int lineNo) -> QString {
				return m_console.displayLineText(lineNo);
			});
		// BuiltinOp::Extension 的节点按名字转回注册类的 runExpression。
		m_expressionEvaluator.setExtensionFunctionInvoker(
			[this](const QString& name, const QList<QVariant>& args,
			       const QList<const ExpressionNode*>& argNodes, QVariant& out) -> bool {
				return m_executionEngine.extensions().runExpression(name, args, argNodes, out);
			});
		// ---- 音频播放（扩展能力；C# 原版没有）----
		// 扩展登记「管线数量」（4 字节无符号，不硬编码在核心），引擎把播放池注入
		// 扩展（C++ 控制端），QML 按 pool.capacity() 维护对应数量的播放器。
		m_executionEngine.extensions().setAudioPool(&m_audio);
		m_audio.setSoundPipelines(m_executionEngine.extensions().audioPipelines());
		// 执行错误 -> 状态灯（QML 读 hasError）
		connect(&m_executionEngine, &ExecutionEngine::errorOccurred, this,
		        [this](const QString& message) {
			        qWarning() << "[EraEngine] 执行错误:" << message;
			        setHasError(true);
		        });
		m_expressionEvaluator.setHtmlPrintedProvider(
			[this](int lineNo) -> QString { return m_console.htmlPrintedStr(lineNo); },
			[this]() -> QString { return m_console.htmlPopPrintingStr(); });
		m_expressionEvaluator.setClearProvider([this]() { m_console.clearAll(); });
		m_expressionEvaluator.setPrintCProvider([this]() -> QPair<int,int> {
			// C# Config.PrintCLength（PRINTCの文字数）/ PrintCPerLine（PRINTCを並べる数）
			int len = 25, per = 3;
			for (const QString& k : {QStringLiteral("PRINTCの文字数"),
			                          QStringLiteral("PRINTC 文字数")}) {
				if (m_configLoader.hasConfig(k)) { len = m_configLoader.getInt(k, 25); break; }
			}
			for (const QString& k : {QStringLiteral("PRINTCを並べる数"),
			                          QStringLiteral("PRINTC の表示数"),
			                          QStringLiteral("PrintCPerLine")}) {
				if (m_configLoader.hasConfig(k)) { per = m_configLoader.getInt(k, 3); break; }
			}
			return { len, per };
		});
		// SAVETEXT / LOADTEXT / SAVECHARA / LOADCHARA / GSAVE / GLOAD 的落盘目录
		m_expressionEvaluator.setSaveDirectory(m_gameDirectory + QStringLiteral("/sav"));
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
		// 逻辑网格：脚本看到的列/行数固定为「配置窗口 ÷ 单元格像素」。
		// 它与 maxLineUnits 必须同源 —— 否则居中（offset = 网格列/2 - 行宽/2）
		// 会按错误的列数计算，宽度超过默认 80 列的图片/长行会被挤到左边
		// （eraTW 标题图宽 130 列，正是被 80 列的网格压到 col 0 的）。
		syncConsoleGrid();
		// 窗口宽/字号变化后同步（DRAWLINE 的铺满宽度由它决定）
		connect(&m_guiManager, &GuiManager::settingsChanged, this, [this]() {
			m_executionEngine.setMaxLineUnits(
				qMax(1, m_guiManager.windowWidth() / qMax(1, m_guiManager.fontSize() / 2)));
			syncConsoleGrid();
		});
		m_guiManager.setStartDirectory(m_gameDirectory);
		m_guiManager.setWindowTitle(m_gameBaseData.windowTitle());
		connect(&m_gameBaseData, &GameBaseData::dataChanged, this, [this]() {
			m_guiManager.setWindowTitle(m_gameBaseData.windowTitle());
		});

		connect(&m_scriptRunner, &ScriptRunner::errorOccurred, this, [](const QString& msg) {
			qWarning() << "[ScriptRunner]" << msg;
		});
		// QUIT：脚本请求结束本局。queued 投递，等当前 pump/事件栈退出后再卸载，
		// 避免在 pump() 栈内重入清理解析表。
		connect(&m_systemStateMachine, &SystemStateMachine::quitRequestedByScript,
		        this, &EraEngine::quitRequested, Qt::QueuedConnection);
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
		connect(&m_executionEngine, &ExecutionEngine::consoleReuseLastLine,
				&m_console, &ConsoleBackend::notifyReuseLastLine);
        connect(&m_executionEngine, &ExecutionEngine::consolePrintTemplate,
                &m_console, &ConsoleBackend::printTemplate);
		connect(&m_executionEngine, &ExecutionEngine::consolePrintImage,
				&m_console, &ConsoleBackend::printImage);
		connect(&m_executionEngine, &ExecutionEngine::consolePrintShape,
				&m_console, &ConsoleBackend::printShape);
		connect(&m_executionEngine, &ExecutionEngine::consoleOutputLog, this,
				[this] { m_console.outputLog(m_gameDirectory + QStringLiteral("/emuera.log")); });
		connect(&m_executionEngine, &ExecutionEngine::consoleClearLines,
				&m_console, &ConsoleBackend::clearLines);
		connect(&m_executionEngine, &ExecutionEngine::consolePrintButton, this,
				[this](const QString& text, qint64 intValue, const QString& strValue, bool isString) {
					if (isString) m_console.printButtonStr(text, strValue);
					else m_console.printButton(text, intValue);
				});
		connect(&m_executionEngine, &ExecutionEngine::consoleResetColor,
				&m_console, &ConsoleBackend::resetColor);
		connect(&m_executionEngine, &ExecutionEngine::clearTextBox, this,
				[this] { emit m_console.clearTextBoxRequested(); });
		connect(&m_executionEngine, &ExecutionEngine::consoleRedraw, this,
				[this](const QString&) { m_console.flush(); });
		connect(&m_executionEngine, &ExecutionEngine::consoleAlign, this,
				[this](const QString& align) {
					const QString a = align.toUpper();
					if (a == QLatin1String("CENTER")) m_console.setAlignment(ConsoleAlign::Center);
					else if (a == QLatin1String("RIGHT")) m_console.setAlignment(ConsoleAlign::Right);
					else m_console.setAlignment(ConsoleAlign::Left);
				});
		connect(&m_executionEngine, &ExecutionEngine::consoleFontStyle, this,
				[this](bool bold, bool italic, bool underline, bool strike) {
					m_console.setFontStyle(bold, italic, underline, strike);
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
		connect(&m_executionEngine, &ExecutionEngine::consoleResetBgColor,
				&m_console, &ConsoleBackend::resetBgColor);
		connect(&m_executionEngine, &ExecutionEngine::consoleBgColor, this,
				[this](const QString& colorName) {
					QColor c(colorName);
					if (!c.isValid()) {
						bool ok = false;
						uint v = colorName.toUInt(&ok, 0);   // 0xRRGGBB
						if (ok) c = QColor::fromRgb(v);
					}
					if (c.isValid()) m_console.setBgColor(c);
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
		m_audio.setSoundDirectory(dir);   // 音频资源检索目录（PLAYBGM/PLAYSOUND）
		// 与 setGameDirectory() 保持同一套落盘目录：异步装载是 QML 的主路径，
		// 这里若不设，SAVEGLOBAL / SAVETEXT / SAVECHARA / GSAVE 会落到空目录
		// （或上一个游戏），存档静默丢失。
		m_executionEngine.setGameDataDir(dir);
		m_expressionEvaluator.setSaveDirectory(dir + QStringLiteral("/sav"));
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
				m_audio.setSoundDirectory(dir);   // 音频资源检索目录（PLAYBGM/PLAYSOUND）
				// SAVEGLOBAL / LOADGLOBAL 的落盘目录（对齐 C# getSaveDataPathG）
				m_executionEngine.setGameDataDir(dir);
				// SAVETEXT / LOADTEXT / SAVECHARA / LOADCHARA / GSAVE / GLOAD 的落盘目录
				m_expressionEvaluator.setSaveDirectory(dir + QStringLiteral("/sav"));
				qDebug() << "[DEBUG] About to call reload()";
				reload();
				qDebug() << "[DEBUG] reload() complete";
				qDebug() << "[DEBUG] setGameDirectory complete";
				emit gameDirectoryChanged();
		}
}

void EraEngine::reload()
{
		setHasError(false);   // 重新装载 -> 清掉上一次的错误灯
		// 新游戏从空控制台开始（清屏从 closeGame 挪到这里：QUIT 后保留末屏）
		m_console.clearAll();
		// 上一局脚本可能 QUIT 过：不清掉会残留 Halt/quitRequested 把新局锁死
		m_processState.clearQuitRequest();
		// closeGame 停在 Halt —— 必须复位成 Continue，否则新局 run()/pump()
		// 第一圈 isRunning()==false 直接 break，标题永远不会出现
		m_processState.setExecState(ExecState::Continue);
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
				// 先清掉上一局的解析数据：loadScript 是增量 append（同名事件
				// 标签/告警会翻倍 —— 上一局 88 条告警第二局变 179 条，
				// @EVENTFIRST 的 6 个声明变 12 个，事件组重复执行）
				m_parseTable.clear();
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

// QUIT（脚本）/ 关闭游戏：卸载当前目录的游戏并释放引擎内存，回到「未装载」
// 状态（等价 C# 关闭游戏窗口，但本移植是单实例常驻，卸载后可再 openDirectory）。
void EraEngine::closeGame()
{
		// 守卫：quitRequested 是 queued 投递 —— 若在此期间用户/脚本已经打开了
		// 新目录（reload() 会 clearQuitRequest），这次排队中的卸载必须放弃，
		// 否则会把刚装载好的新局拆掉（表现为新局 run() 立刻 Halt）。
		if (!m_processState.quitRequested()) {
				qDebug() << "[EraEngine] closeGame: QUIT 已被新装载消费，跳过卸载";
				return;
		}
		qInfo() << "[EraEngine] closeGame: 卸载当前游戏并释放内存";
		setHasError(false);
		m_processState.requestHalt();        // 停掉执行链（若还在跑）
		m_processState.clearQuitRequest();   // 消费掉 QUIT 请求
		m_audio.stopBgm();
		m_audio.stopSounds();
		// 控制台不清屏：QUIT 后最后一屏（测试汇总等）留在窗口上供查看，
		// 真正清屏发生在下一次装载（reload），届时新游戏从空控制台开始。
		// 变量/角色运行时数据回默认（释放内建数组与角色容器）
		m_executionEngine.handleResetData();
		// 脚本 AST/标记区/函数表/标签/告警 全量释放
		m_parseTable.clear();
		m_gameDirectory.clear();
		m_csvDir.clear();
		m_erbDir.clear();
		ResourceImageProvider::setRoot(QString());
		emit gameDirectoryChanged();
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
		// 存档目录注入（SAVEDATA/LOADDATA/DELDATA/CHKDATA 用）
		m_executionEngine.setGameDirectory(m_gameDirectory);
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

		// CSV 名表 -> <VAR>NAME 字符串数组（对齐 C#：装载 CSV 时同步填充
		// TALENTNAME/ABLNAME/EXPNAME/…——脚本会直接引用这些数组，
		// 如 GET_TALENTNAME 的 CASEELSE 兜底显示 %TALENTNAME:ARG%）
		int nameArrays = 0;
		for (const QString& key : m_constantTable.tableNames()) {
			const int cap = m_variableStorage.variableConfig().getSize1D(key + QStringLiteral("NAME"));
			if (cap <= 0) continue;   // 未登记的变量（角色 CSV 等）跳过
			const int n = qMin(cap, m_constantTable.count(key + QStringLiteral(".CSV")));
			for (int i = 0; i < n; ++i) {
				const QString nm = m_constantTable.nameAt(key + QStringLiteral(".CSV"), i);
				if (!nm.isEmpty()) m_variableStorage.setGlobalStr1D(key + QStringLiteral("NAME"), i, nm);
			}
			++nameArrays;
		}
		qDebug() << "[load] NAME 数组填充:" << nameArrays << "个";

		// STR 变量本身：文档明确「Str.csv 的数据保存在这里」——string[0..19999]，
		// 第一列是字符串编号、第二列是字符串（值），与 <VAR>NAME 名表是两回事
		// （上面那套循环只填 STRNAME，且 eraTW 没有 StrName.csv）。
		// eraTW @NAME_FROM_PLACE 用 %STR:(6000 + ARG/10)% 取場所名：此前从不填充
		// STR，STR:n 恒为空 -> NAME_FROM_PLACE 返回 "" -> @RANDOM_ODEKAKE 的
		// WHILE 永不收敛（10 万次迭代告警）。
		{
			const int cap = m_variableStorage.variableConfig().getSize1D(QStringLiteral("STR"));
			const int n = qMin(cap, m_constantTable.count(QStringLiteral("STR.CSV")));
			for (int i = 0; i < n; ++i) {
				const QString v = m_constantTable.nameAt(QStringLiteral("STR.CSV"), i);
				if (!v.isEmpty()) m_variableStorage.setGlobalStr1D(QStringLiteral("STR"), i, v);
			}
			qDebug() << "[load] STR 变量填充:" << n << "槽";
		}

		// ITEMPRICE：Item.csv **第 3 列**是价格（对齐 C# ConstantData.loadDataTo
		// 的 targetI 分支：tokens[2] 能解析成整数就写 ItemPrice[index]）。
		// 此前从未填充 -> 隙間商店/通販等所有商品价格显示 $0。
		{
			const QStringList itemCandidates = m_fileSystem.listFiles(
			    m_csvDir, QStringList{QStringLiteral("Item.csv")}, m_searchSubdirectory);
			if (!itemCandidates.isEmpty()) {
				CsvLoader itemLoader;
				if (itemLoader.loadFile(itemCandidates.first())) {
					const QString table = QFileInfo(itemCandidates.first()).baseName();
					const int cap = m_variableStorage.variableConfig().getSize1D(
					    QStringLiteral("ITEMPRICE"));
					const int rows = qMin(itemLoader.getRowCount(table), cap);
					int filled = 0;
					for (int r = 0; r < rows; ++r) {
						bool indexOk = false;
						const int index = itemLoader.getValue(table, r, 0).toInt(&indexOk);
						if (!indexOk || index < 0 || index >= cap) continue;
						bool priceOk = false;
						const qint64 price = itemLoader.getValue(table, r, 2).toLongLong(&priceOk);
						if (!priceOk) continue;   // 无价格列/价格不可解析：保持 0
						m_variableStorage.setGlobalInt1D(QStringLiteral("ITEMPRICE"), index, price);
						++filled;
					}
					qDebug() << "[load] ITEMPRICE 填充:" << filled << "项";
				}
			}
		}

		// 变量尺寸表（对齐 C# VariableData 读取 VariableSize.CSV）
		int sizesLoaded = 0;
		const QStringList sizeCandidates = m_fileSystem.listFiles(m_csvDir,
		                                                          QStringList{"VariableSize.csv"}, false);
		for (const QString& path : sizeCandidates) {
				if (m_variableStorage.loadVariableSizes(path)) ++sizesLoaded;
		}
		// 角色 CSV（对齐 C# ConstantData 读 <Csv>/Chara）：NAME/CALLNAME/BASE/ABL/…
		const int charaLoaded = CsvLoader::loadCharaDirectory(
		    m_csvDir + QStringLiteral("/Chara"), &m_constantTable, &m_variableStorage);
		// CSV* 系函数读「模板值」：装载完角色 CSV 后立刻快照一份
		// （此后脚本对 VAR:角色:下标 的修改不再污染模板）
		m_variableStorage.snapshotCharaTemplates();
		qDebug() << "[EraEngine] CSV 目录:" << m_csvDir
		         << " 常量表:" << tables << "(" << m_constantTable.nameCount() << "项)"
		         << " VariableSize:" << sizesLoaded
		         << " 角色:" << charaLoaded;
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
		// 上一次装载的后台语义阶段还没结束：此时它会持续改写 m_parseTable，
		// 再起一次装载必然互相踩踏。直接忽略本次重载（用户极少在这 10s 内
		// 再次打开目录），等 scriptsLoaded 之后即可正常重载。
		if (m_semanticRunning) {
				qWarning() << "[EraEngine] 上一次装载的语义阶段尚未结束，忽略本次重载";
				return;
		}
		m_processState.clearQuitRequest();   // 同 reload()：清掉上一局的 QUIT 请求
		// 与 reload() 对齐：异步路径此前漏了 clear()，同名脚本/告警/事件会跨次
		// 装载累积（第二次装载脚本数、告警数翻倍）。
		m_parseTable.clear();
		QElapsedTimer phase;   // 主线程各阶段耗时：定位「装载时界面卡一下」的来源
		phase.start();
		resolveGameDirs();
		loadConfigFiles();
		resolveTextConfig();
		loadGameBaseData();
		loadConstantData();
		m_scriptProcessor.clear();
		qInfo().noquote() << "[EraEngine] 异步装载·同步准备阶段(CSV/常量/探测)"
		                  << phase.elapsed() << "ms";

		ErbLoader& loader = m_executionEngine.getErbLoader();
		if (m_loadProgressConn) disconnect(m_loadProgressConn);
		if (m_loadCompletedConn) disconnect(m_loadCompletedConn);
		m_loadProgressConn = connect(&loader, &ErbLoader::loadProgress,
		                             this, &EraEngine::scriptsLoadProgress);
		m_loadCompletedConn = connect(&loader, &ErbLoader::loadCompleted, this, [this](bool ok) {
				if (!ok) {
						loadFinishedHook();
						emit scriptsLoaded(false);
						return;
				}
				// 语义阶段（类型回填 + 参数校验 + 入口点收集）是 eraTW 上**最大的一次
				// 主线程开销**：实测 finalizeParse ≈ 10s（validateArguments 3.5s +
				// applyVariableTypes 5.0s + resolveFunctionNodes 1.4s，2.2M 行 /
				// 57846 个用户函数）。放在主线程 = 界面假死十秒。
				// 装载期间 GUI 不读解析表（runSystem 要等 scriptsLoaded），因此把它
				// 放到后台线程执行，完成后回主线程发 scriptsLoaded。
				m_semanticRunning = true;
				auto* watcher = new QFutureWatcher<void>(this);
				connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
						watcher->deleteLater();
						m_semanticRunning = false;
						loadFinishedHook();
						emit scriptsLoaded(true);
				});
				m_semanticFuture = QtConcurrent::run([this]() {
						QElapsedTimer fin;
						fin.start();
						m_parseTable.finalizeParse();
						collectEntryPoints();
						qInfo().noquote() << "[EraEngine] 后台语义阶段(finalizeParse+入口点)"
						                  << fin.elapsed() << "ms";
				});
				watcher->setFuture(m_semanticFuture);
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

// 逻辑网格 = 脚本看到的列/行数（窗口像素只是呈现层的事，不能反过来影响换行）。
// 列：窗口宽 / 半角字宽；行：窗口高 / 行高。与 executionEngine 的 maxLineUnits 同源。
void EraEngine::syncConsoleGrid()
{
		const int columnPx = qMax(1, m_guiManager.fontSize() / 2);
		const int columns = qMax(1, m_guiManager.windowWidth() / columnPx);
		const int lineHeight = qMax(1, m_guiManager.lineHeight());
		const int rows = qMax(1, m_guiManager.windowHeight() / lineHeight);
		m_console.setGridColumns(columns);
		m_console.setGridRows(rows);
		qDebug() << "[render] 逻辑网格" << columns << "列 x" << rows << "行"
				 << "(单元格" << columnPx << "x" << lineHeight << "px)";
}

void EraEngine::loadGameBaseData(){
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
		// C# 语义：任何输入都写 RESULT 并继续执行。不在这里拦「非分支值」：
		// SELECTCASE 落空后由脚本自己的流程兜底（RESTART / GOTO 重画菜单
		// 再等输入）—— 引擎把 RESTART / GOTO 跑对即可。
		// 用户操作交付：写入 RESULT/systemResult 并让状态机继续
		qDebug() << "[input] provideInput" << value;
		m_console.notifyInputDone();
		m_systemStateMachine.resume(value);
}
void EraEngine::provideInputString(const QString& value)
{
		// 字符串输入：写入 RESULTS（局部字符串槽）后继续
		qDebug() << "[input] provideInputString" << value;
		m_console.notifyInputDone();
		m_systemStateMachine.resumeString(value);
}

void EraEngine::provideInputValues(const QVariantList& values)
{
		// 多值输入（INPUTMOUSEKEY：RESULT:0..4 = 类型 / 坐标 / 按键）
		qDebug() << "[input] provideInputValues" << values;
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

		// ---- 数据层（RESETDATA 指令 / 标准标题「[0] 从头开始」）----
		// 此前 resetData 从未接线：RESETDATA 只在 ExecutionEngine 里有一半实现，
		// 系统层的新开游戏重置根本没被调用。
		host.resetData = [this]() { m_executionEngine.handleResetData(); };

		// ---- 存/读档（SAVEGAME / LOADGAME 系统画面）----
		// 文件名/目录对齐 ExecutionEngine 的 SAVEDATA/LOADDATA（sav/save##.sav），
		// 内容 = VariableStorage::dumpSaveData（eraemu-save-v1）+ SAVETEXT 概要头。
		// 此前这四个回调从未接线：读档画面所有槽位恒判「没有数据」、保存恒失败。
		const auto savePath = [this](int index) {
				return m_gameDirectory + QStringLiteral("/sav/save%1.sav")
						.arg(index, 2, 10, QLatin1Char('0'));
		};
		host.saveDataNos = [intCfg]() {
				return intCfg({QStringLiteral("セーブデータの数"),
				               QStringLiteral("SaveDataNos")}, 20);
		};
		host.checkData = [this, savePath](int index, QString* message) {
				const bool exists = QFile::exists(savePath(index));
				if (message) {
						*message = exists ? QStringLiteral("ＯＫ")
					                      : QStringLiteral("ファイルが存在しません");
				}
				return exists;
		};
		host.saveTo = [this, savePath](int index, const QString& saveText) -> bool {
				QDir().mkpath(m_gameDirectory + QStringLiteral("/sav"));
				QString body = m_variableStorage.dumpSaveData();
				if (!saveText.isEmpty()) {
						body.replace(QStringLiteral("eraemu-save-v1"),
						             QStringLiteral("eraemu-save-v1\nSAVETEXT\t%1").arg(saveText));
				}
				QFile f(savePath(index));
				if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
				f.write(body.toUtf8());
				return true;
		};
		host.loadFrom = [this, savePath](int index) -> bool {
				QFile f(savePath(index));
				if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
				m_variableStorage.restoreSaveData(QString::fromUtf8(f.readAll()));
				return true;
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
		qDebug() << "[var] 随机重置（新种子）" << m_expressionEvaluator.randomSeed();
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
