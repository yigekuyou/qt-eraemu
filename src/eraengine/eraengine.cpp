#include "GameData/game_paths.h"
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
#include <QClipboard>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>

#include "system_status_manager.h"
#include "erb_loader.h"
#include "i18n.h"
#include "GameView/dbus_debug.h"
#include "GameView/graphics_store.h"
#include <memory>
#include <iostream>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <functional>
#include <QDateTime>
#include <QObject>
#include <QtConcurrent/QtConcurrent>

EraEngine::~EraEngine() {
        stopAsyncLoad();
}

void EraEngine::stopAsyncLoad() {
        m_systemStateMachine.stopForReload();
		// 后台 CSV 准备（路径树/常量表/角色模板）若在跑，必须等它结束再往下：
		// 它仍在写 m_constantTable / m_variableStorage，不等就会与下一轮装载
		// 争同一批容器。SAF 上这一等最长约一分钟，但只在切游戏/重载时发生。
		if (m_csvPrepWatcher.isRunning()) {
				qInfo() << "[EraEngine] 等待后台 CSV 准备结束（SAF 上最多约一分钟）";
				m_csvPrepWatcher.waitForFinished();
		}
        ++m_loadGeneration;
        auto& loader = m_executionEngine.getErbLoader();
        if (m_loadProgressConn) disconnect(m_loadProgressConn);
        if (m_loadHeartbeatConn) disconnect(m_loadHeartbeatConn);
        if (m_loadCompletedConn) disconnect(m_loadCompletedConn);
        loader.cancelAndWait();
        m_completingLoad = false;   // 取消收尾窗口（收尾已内联到 loadScript）
        resetLoadHeartbeat();       // 装载快照复位：别把上一局的阶段名留在遮罩上
}

// ---------------------------------------------------------------------------
// 装载心跳 → QML 属性（UI 的「正在装载」显示一律绑这几个属性）
// ---------------------------------------------------------------------------
void EraEngine::applyLoadHeartbeat(int stage, const QString& name, int done, int total,
                                   qint64 elapsedMs, bool stalled)
{
        Q_UNUSED(stage);
        m_loadStage = name;
        m_loadProcessed = done;
        m_loadTotal = total;
        m_loadElapsedMs = elapsedMs;
        m_loadStalled = stalled;
        emit loadHeartbeatChanged();
}

void EraEngine::resetLoadHeartbeat()
{
        if (m_loadStage.isEmpty() && m_loadProcessed == 0 && m_loadTotal == 0
                && m_loadElapsedMs == 0 && !m_loadStalled) {
                return;   // 已经是初值：不打扰 QML
        }
        m_loadStage.clear();
        m_loadProcessed = 0;
        m_loadTotal = 0;
        m_loadElapsedMs = 0;
        m_loadStalled = false;
        emit loadHeartbeatChanged();
}

void EraEngine::setLoadStageText(const QString& name)
{
        if (m_loadStage == name) return;
        m_loadStage = name;
        emit loadHeartbeatChanged();
}

void EraEngine::clearLoadedGame() {
        m_systemStateMachine.stopForReload();
        m_scriptRunner.clearForReload();
        m_executionEngine.clearForReload();
        m_expressionEvaluator.setConstProvider({});
        m_expressionEvaluator.setConstArrayProvider({});
        m_expressionEvaluator.setConstArrayChecker({});
        m_parseTable.clear();
        m_variableStorage.clearForReload();
        m_constantTable.clear();
        m_gameBaseData.clear();
        m_configLoader.clearFiles();
        m_scriptProcessor.clear();
        m_eventManager.clearEventQueue();
        m_console.notifyInputDone();
        m_console.clearAll();
        m_audio.stopBgm();
        m_audio.stopSounds();
        GraphicsStore::clearAll();
}

EraEngine::EraEngine(QObject *parent)
		: QObject(parent),
			m_variableStorage(),
			m_expressionEvaluator(),
			m_fileSystem(),
			m_binaryIo(),
			m_inputSystem(),
			m_executionEngine(&m_variableStorage, &m_gameBaseData),
			m_processState(),
			m_systemStateMachine(&m_processState),
			m_statusManager(),
			m_parseTable(&m_processState, &m_executionEngine),
			m_scriptRunner(&m_parseTable, &m_executionEngine, &m_processState, &m_variableStorage),
			m_console(),
			m_guiManager(),
			m_identifierDictionary(),
			m_configLoader(),
			m_scriptProcessor(),
			m_inputHandler(),
			m_eventManager()
{
		// 告警/诊断文本的 i18n：装载期在**解析之前**把翻译器装好，之后所有
		// eraengine::diagTr() 才会命中 .qm（未提供 .qm 时原样输出中文源文本）。
		// 幂等，重复构造 EraEngine 不会重复安装。
		eraengine::installTranslators();

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
		// 帧节拍闸门（C# need_settimer）：有 QML 控制台视图时，限时输入的
		// 计时器改为「舞台变更呈现完毕」后启动 —— 视图在 Console.qml 里
		// 把 framePacingAttached 置真，并在处理完这一帧后回报 framePresented。
		m_systemStateMachine.setFramePacingProbe(
			[this]() { return m_console.framePacingAttached(); });
		connect(&m_console, &ConsoleBackend::framePresented,
				&m_systemStateMachine, &SystemStateMachine::onFramePresented);
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
			return m_executionEngine.checkSaveSnapshot(idx);
		});
		// GETCOLOR / GETSTYLE：由执行引擎维护的当前颜色与样式位
		m_expressionEvaluator.setColorProvider([this]() -> qint64 {
			return m_executionEngine.currentColorValue();
		});
        m_executionEngine.setRealRoundingProvider([this]() {
            const QString value = configValue(QStringLiteral("RealRounding"));
            if (value == QLatin1String("floor")) return ExecutionEngine::RealRounding::Floor;
            if (value == QLatin1String("ceil")) return ExecutionEngine::RealRounding::Ceil;
            return ExecutionEngine::RealRounding::Round;
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
                    return m_configLoader.getColor(k, 0xFFFF00);
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
        m_expressionEvaluator.setFormIgnoreProvider([this]() {
            for (const QString& key : {QStringLiteral("SystemIgnoreTripleSymbol"), QStringLiteral("FORM中の三連記号を展開しない")}) {
                if (m_configLoader.hasConfig(key)) {
                    return m_configLoader.getBool(key, false);
                }
            }
            return false;
        });
        m_expressionEvaluator.setSaveCompressionProvider([this]() {
            bool binary=false, zip=false;
            for(const auto& key:QStringList{QStringLiteral("セーブデータをバイナリ形式で保存する"),QStringLiteral("SystemSaveInBinary")})
                if(m_configLoader.hasConfig(key)) { binary=m_configLoader.getBool(key); break; }
            for(const auto& key:QStringList{QStringLiteral("セーブデータを圧縮して保存する"),QStringLiteral("ZipSaveData")})
                if(m_configLoader.hasConfig(key)) { zip=m_configLoader.getBool(key); break; }
            return binary && zip;
        });
        m_executionEngine.setSaveFormatProvider([this]() {
            const auto setting=[this](const QStringList& keys,bool fallback) {
                for(const auto& key:keys) if(m_configLoader.hasConfig(key)) return m_configLoader.getBool(key,fallback);
                return fallback;
            };
            return QPair<bool,bool>{setting({QStringLiteral("セーブデータをバイナリ形式で保存する"),QStringLiteral("SystemSaveInBinary")},false),
                                    setting({QStringLiteral("セーブデータを圧縮して保存する"),QStringLiteral("ZipSaveData")},false)};
        });
        m_expressionEvaluator.setIntegerConfigProvider([this](const QString& key, qint64& value) {
            return m_configLoader.integerConfigValueInErb(key, value);
        });
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
                    return m_configLoader.getColor(k, 0xFFFFFF);
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
			if (kind == 0) return m_executionEngine.skipDisp() ? 1 : 0;
			if (kind == 1) return m_executionEngine.mesSkip() ? 1 : 0;   // SKIPLOG 置位
			return 0;
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
		// 宿主侧服务（控制台文本框 / 流程状态 / GameBase / 配置）：
		// GETTEXTBOX・SETTEXTBOX・FLOWINPUT(S)・UPDATECHECK 的实现经这里访问。
		m_executionEngine.extensions().setHostServices(
			[this]() -> QString { return m_console.textboxText(); },
			[this](const QString& text) { m_console.setTextboxText(text); },
			[this](qint64 def, bool enable, bool canSkip, bool forceSkip) {
				m_processState.setFlowInput(def, enable, canSkip, forceSkip);
			},
			[this](bool isString, const QString& defStr) {
				m_processState.setFlowInputString(isString, defStr);
			},
			[this](const QString& key) -> QString { return m_gameBaseData.get(key); },
			[this]() -> bool {
				// 「UPDATECHECKを許可しない」（C# Config.ForbidUpdateCheck，bool，默认 false）
				bool forbid = false;
				for (const QString& k : {QStringLiteral("UPDATECHECKを許可しない"),
				                          QStringLiteral("DisallowUPDATECHECK")}) {
					if (m_configLoader.hasConfig(k)) {
                        forbid = m_configLoader.getBool(k, false);
						break;
					}
				}
				return forbid;
			});
		// ---- EM/Emuera.NET fork 族（MAP_/ENUM*/XML_/DT_）服务 ----
		// 这些 .NET 扩展要的是「引擎内省（名字表）/ 文件系统 / 游戏目录」，
		// 引擎在这里一次性注入（惰性 provider，装载后才有内容）。
		//（CALLSHARP 保持桩 —— 跨平台宿主不实现本机插件装载，见 fork_extension.cpp）
		m_executionEngine.extensions().setForkServices(
			[this]() -> QStringList {
				// ENUMFUNC*：非事件函数名（保原样大小写）—— 对齐 C# NoneventKeys
				QStringList out;
				const auto& fns = m_parseTable.userFunctions();
				out.reserve(fns.size());
				for (auto it = fns.constBegin(); it != fns.constEnd(); ++it) {
					if (it.value().isEvent) continue;
					out.append(it.value().originalName.isEmpty() ? it.key()
					                                             : it.value().originalName);
				}
				return out;
			},
			[this]() -> QStringList {
				// ENUMVAR*：系统变量（原生 + 扩展）+ 用户广域变量（ERH 的 #DIM/#DIMS）
				QStringList out;
				for (const std::string_view n : sysvar::kIntegerNames)
					out.append(QString::fromStdString(std::string(n)));
				for (const std::string_view n : sysvar::kStringNames)
					out.append(QString::fromStdString(std::string(n)));
				for (const auto& kv : sysvar::extensionVariableStore().defs)
					out.append(QString::fromStdString(kv.first));
				for (const VariableDecl& d : m_parseTable.variableTable().declarations()) {
					if (d.scope != VarScope::Global) continue;      // 函数私有变量不在其中
					out.append(d.name);
				}
				return out;
			},
			[this]() -> QStringList {
				// ENUMMACRO*：#DEFINE 宏名（预处理器的宏表）
				return m_executionEngine.getErbLoader().preprocessor().macroTable().keys();
			},
			[this]() -> QString { return m_gameDirectory; });
		// HTML_STRINGLEN 的像素形态（`HtmlLength`）用字号：半角 = FontSize/2 像素。
		// 取值口径与 CLIENTWIDTH/CLIENTHEIGHT 的排版参数一致（GUI 侧的字号）。
		m_executionEngine.extensions().setFontSizeProvider(
			[this]() -> int { return m_guiManager.fontSize(); });
		// EM 私family「变量名字符串化」（GETVAR/GETVARS/SETVAR/EXISTVAR）：
		// 解析与存在性判定由解析表提供（变量表/常数表/作用域都在那边），
		// 写入由求值器提供（与 ++/-- 的写回同一路径）。
		m_executionEngine.extensions().setVariableNameServices(
			[this](const QString& text) { return m_parseTable.runtimeVariableReference(text); },
			[this](const QString& name) { return m_parseTable.variableFlags(name); },
			[this](const ExpressionNode& target, const QVariant& value) {
				return m_expressionEvaluator.assignNodeValue(target, &m_variableStorage, &m_gameBaseData,
				                                             value, m_parseTable.executionScopeId());
			});
		// 扩展实现抛出的语义错误 -> 置错误状态 + 报错（与 ASSERT 同口径）。
		m_executionEngine.extensions().setRuntimeErrorReporter(
			[this](const QString& message) { m_executionEngine.reportRuntimeError(message); });
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
		// SAVENOS()（式中函数）与语句 `SAVENOS <数值变量>`：C# Config.SaveDataNos
		// —— 配置键是「表示するセーブデータ数」（Config.cs:75/153-161），默认 20、
		// 加载后夹紧到 20..80。此前 C++ 侧查的是「セーブデータの数」/「SaveDataNos」，
		// 与 C# 的键名对不上，恒取默认值。
		m_expressionEvaluator.setSaveDataNosProvider([this]() -> qint64 {
			for (const QString& k : {QString::fromUtf8("表示するセーブデータ数"),
			                         QString::fromUtf8("セーブデータの数"),
			                         QStringLiteral("SaveDataNos")}) {
				if (m_configLoader.hasConfig(k))
					return static_cast<qint64>(qBound(20, m_configLoader.getInt(k, 20), 80));
			}
			return 20;
		});
		// SAVETEXT / LOADTEXT / SAVECHARA / LOADCHARA / GSAVE / GLOAD 的落盘目录
		m_expressionEvaluator.setSaveDirectory(GamePaths::join(m_storageRoot, QStringLiteral("sav")));
        m_expressionEvaluator.setCharacterDirectory(GamePaths::join(m_storageRoot, QStringLiteral("dat")));
		applyScriptPathServices();   // LOADTEXT/SAVETEXT 路径形态与 EXISTFILE 的基准目录/扩展名
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
		connect(&m_systemStateMachine, &SystemStateMachine::restartRequestedByScript,
		        this, &EraEngine::restartRequested, Qt::QueuedConnection);
		connect(&m_scriptRunner, &ScriptRunner::inputRequested, this,
				[this](const QString& kind, const QVariant& def) {
			qDebug() << "[ScriptRunner] waiting for user input:" << kind
					 << "state=" << static_cast<int>(m_processState.getExecState());
			m_console.notifyInputRequested(kind, def);
		});
		// ---- 显示层：执行引擎输出 -> ConsoleBackend ----
		connect(&m_executionEngine, &ExecutionEngine::consolePrint, this,
				[this](const QString& text, bool newline) {
					m_console.print(text);
					if (newline) m_console.newline();
				});
		// PRINTD 系：忽略 SETCOLOR（C# IsPrintDFunction）
		connect(&m_executionEngine, &ExecutionEngine::consolePrintDefaultColor, this,
				[this](const QString& text, bool newline) {
					m_console.printIgnoringSetColor(text);
					if (newline) m_console.newline();
				});
		connect(&m_executionEngine, &ExecutionEngine::consoleReuseLastLine,
				&m_console, &ConsoleBackend::notifyReuseLastLine);
        connect(&m_executionEngine, &ExecutionEngine::consolePrintTemplate,
                &m_console, &ConsoleBackend::printTemplate);
        // HTML_PRINT 缓冲模式：挂进当前行（C# printBuffer.AppendButton）
        connect(&m_executionEngine, &ExecutionEngine::consolePrintTemplateBuffered,
                &m_console, &ConsoleBackend::printTemplateBuffered);
		connect(&m_executionEngine, &ExecutionEngine::consolePrintImage,
				&m_console, &ConsoleBackend::printImage);
		connect(&m_executionEngine, &ExecutionEngine::consolePrintShape,
				&m_console, &ConsoleBackend::printShape);
		// 工具提示样式（C# TOOLTIP_* 指令族 -> WinForms ToolTip 的属性）
		connect(&m_executionEngine, &ExecutionEngine::consoleToolTipStyle,
				&m_console, [this](const QString& family, int pointSize, int format,
				                   bool custom, bool image) {
					m_console.setToolTipFormat(format);
					m_console.setToolTipCustom(custom);
					m_console.setToolTipImage(image);
					m_console.setToolTipFont(family, pointSize);
				});
		connect(&m_executionEngine, &ExecutionEngine::consoleToolTipTiming,
				&m_console, [this](int delayMs, int durationMs) {
					m_console.setToolTipTiming(delayMs, durationMs);
				});
		connect(&m_executionEngine, &ExecutionEngine::consoleToolTipColors,
				&m_console, [this](const QColor& fore, const QColor& back) {
					m_console.setToolTipColors(fore, back);
				});
		// BREAKBUTTON：作废屏上全部按钮（世代号自增，旧按钮点击失效）
		connect(&m_executionEngine, &ExecutionEngine::consoleBreakButtons,
				&m_console, &ConsoleBackend::breakButtons);
		// SETBGIMAGE 族：背景图列表（贴在文字后面；QML 按序叠放）
		connect(&m_executionEngine, &ExecutionEngine::consoleSetBackgroundImage,
				&m_console, &ConsoleBackend::addBackgroundImage);
		connect(&m_executionEngine, &ExecutionEngine::consoleClearBackgroundImages,
				&m_console, &ConsoleBackend::clearBackgroundImages);
		connect(&m_executionEngine, &ExecutionEngine::consoleRemoveBackgroundImage,
				&m_console, &ConsoleBackend::removeBackgroundImage);
		// HTML_PRINT_ISLAND：完全独立的 HTML 层（不随正文滚动）
		connect(&m_executionEngine, &ExecutionEngine::consolePrintHtmlIsland,
				&m_console, &ConsoleBackend::printHtmlIsland);
		connect(&m_executionEngine, &ExecutionEngine::consoleClearHtmlIsland,
				&m_console, &ConsoleBackend::clearHtmlIsland);
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
        connect(&m_executionEngine, &ExecutionEngine::consoleRedraw,
                &m_console, &ConsoleBackend::setRedraw);
        m_expressionEvaluator.setRedrawProvider([this] { return m_console.currentRedraw(); });
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
				[this](StateCode, StateCode newState) {
					m_statusManager.onStateChanged(newState);
				});

		// ---- 直接连接（原经 SignalManager 转发，现改为直连） ----
		connect(&m_executionEngine, &ExecutionEngine::executionStarted,
				&m_statusManager, &SystemStatusManager::onScriptStarted);
		connect(&m_executionEngine, &ExecutionEngine::executionFinished,
				&m_statusManager, &SystemStatusManager::onScriptFinished);
		connect(&m_executionEngine, &ExecutionEngine::errorOccurred,
				&m_statusManager, &SystemStatusManager::onError);
		connect(&m_executionEngine.getErbLoader(), &ErbLoader::parseStarted,
				this, [](const QString& name) { qDebug() << "[parse] started:" << name; });
		connect(&m_executionEngine.getErbLoader(), &ErbLoader::parseFinished,
				this, [](const QString& name) { qDebug() << "[parse] finished:" << name; });
		connect(&m_executionEngine.getErbLoader(), &ErbLoader::parseError,
				this, [](const QString& err) { qWarning().noquote() << "[parse] error:" << err; });		// 交给控制台的必须是**输入类型**（INPUT/TINPUT/ANYKEY…），不是系统状态名 ——
		// 以前这里传 stateName(state)，QML 就显示「输入（Train_CallEventComEnd）」，
		// 且 inputExpectsAnyKey/inputExpectsString 全判错。
		connect(&m_systemStateMachine, &SystemStateMachine::inputKindRequested, this,
				[this](const QString& kind, const QVariant& def) {
					m_console.notifyInputRequested(kind, def);
				});
		connect(&m_systemStateMachine, &SystemStateMachine::errorOccurred, this,
				[](const QString& message) {
			qWarning().noquote() << "[SystemStateMachine]" << message;
		});
}

// QML 类型注册已全部改为声明式（各类型的 QML_ELEMENT / QML_NAMED_ELEMENT /
// QML_SINGLETON / QML_UNCREATABLE 宏 + qt_add_qml_module 扫描 SOURCES 生成的
// 注册代码）。此处原有的 EraEngine::registerTypes() 是 Qt 5 风格的命令式注册
//（qmlRegisterType / qmlRegisterSingletonType / qmlRegisterUncreatableType），
// 与声明式注册重复，而且**从未被任何地方调用** —— 已移除。

bool EraEngine::loadScript(const QString& scriptPath)
{
		return m_executionEngine.loadScripts(scriptPath);
}

void EraEngine::executeScript(const QString& scriptName)
{
        if (isLoadingScripts()) return;
		m_executionEngine.executeScript(scriptName);
}

void EraEngine::loadAsync(const QString& directory)
{
        stopAsyncLoad();
		QString dir = GamePaths::normalize(directory);
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
		m_storageRoot = GamePaths::storageRoot(dir);
		m_executionEngine.setGameDataDir(m_storageRoot);
		m_expressionEvaluator.setSaveDirectory(GamePaths::join(m_storageRoot, QStringLiteral("sav")));
        m_expressionEvaluator.setCharacterDirectory(GamePaths::join(m_storageRoot, QStringLiteral("dat")));
		applyScriptPathServices();
		reloadAsync();
}

QString EraEngine::getGameDirectory() const
{
		return m_gameDirectory;
}

void EraEngine::setGameDirectory(const QString& directory)
{
		if (m_gameDirectory != directory) {
                stopAsyncLoad();
				// Ensure the directory path is properly formatted
				QString dir = GamePaths::normalize(directory);
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
				m_storageRoot = GamePaths::storageRoot(dir);
		m_executionEngine.setGameDataDir(m_storageRoot);
				// SAVETEXT / LOADTEXT / SAVECHARA / LOADCHARA / GSAVE / GLOAD 的落盘目录
				m_expressionEvaluator.setSaveDirectory(GamePaths::join(m_storageRoot, QStringLiteral("sav")));
        m_expressionEvaluator.setCharacterDirectory(GamePaths::join(m_storageRoot, QStringLiteral("dat")));
				applyScriptPathServices();
				reload();
				emit gameDirectoryChanged();
		}
}

// LOADTEXT / SAVETEXT 的**路径形态**与 EXISTFILE 用到的两件事：
//   · 基准目录 = 游戏目录（对齐 C# `Program.ExeDir`，`Utils.GetValidPath` 拼在它下面）；
//   · 扩展名白名单 = `Config.ValidExtension`（配置键
//     「LOADTEXTとSAVETEXTで使える拡張子」，逗号分隔；ConfigData.cs:140/294 的
//     默认值是 {"txt"}）。FL3.8 把它配成 `txt,xml`，脚本正是用 LOADTEXT 读 XML。
// 配置读取用惰性 lambda：`emuera.config` 在游戏目录确定后才装载。
void EraEngine::applyScriptPathServices()
{
    m_expressionEvaluator.setBaseDirectory(m_gameDirectory);
    m_expressionEvaluator.setValidExtensionsProvider([this]() -> QStringList {
        for (const QString& key : {QString::fromUtf8("LOADTEXTとSAVETEXTで使える拡張子"),
                                   QStringLiteral("ValidExtension")}) {
            if (!m_configLoader.hasConfig(key)) continue;
            QStringList out;
            for (const QString& token : m_configLoader.getConfig(key).split(QLatin1Char(','))) {
                const QString t = token.trimmed();
                if (!t.isEmpty()) out.append(t);
            }
            if (!out.isEmpty()) return out;
        }
        return QStringList{QStringLiteral("txt")};
    });
}

void EraEngine::reload()
{
        stopAsyncLoad();
        clearLoadedGame();
		setHasError(false);   // 重新装载 -> 清掉上一次的错误灯
		// 上一局脚本可能 QUIT 过：不清掉会残留 Halt/quitRequested 把新局锁死
		m_processState.clearQuitRequest();
		// QUIT_AND_RESTART 系的重启标志同理：本次 reload 就是「重启」本身的执行，
		// 残留会让下一局的首个 quitRequested 又被当成重启。
		m_processState.clearRestartRequest();
		// closeGame 停在 Halt —— 必须复位成 Continue，否则新局 run()/pump()
		// 第一圈 isRunning()==false 直接 break，标题永远不会出现
		m_processState.setExecState(ExecState::Continue);
		// Reload scripts from current directory
		if (!m_gameDirectory.isEmpty()) {
				m_loadChainStartMs = QDateTime::currentMSecsSinceEpoch();
				// 对齐 C#：先解析 ErbDir / CsvDir（只在这两个目录内检索）
				if (!resolveGameDirs()) {
						m_parseTable.clear();
						emit scriptsLoaded(false);
						resetLoadHeartbeat();
						return;
				}

				// 配置文件：_default.config / _fixed.config 在 CSV 目录，
				// emuera.config 在游戏根目录（C# ConfigData.configPath = ExeDir + "emuera.config"）
				loadConfigFiles();
				resolveTextConfig();   // 编码 / 子目录检索（配置项在编码嗅探下才读得到）

				// GameBase.csv / 常量 CSV：只从 CSV 目录读取。
				// CSV 文本表：GameBase.csv 与常量名表 / 角色 CSV 的字节**同一次**
				// 读取（loadConstantData 收尾释放，不长期驻留）。
				CsvTextTable csvText;
				loadGameBaseData(&csvText);
				loadConstantData(&csvText);

				// 脚本：只从 ERB 目录装载
				// 先清掉上一局的解析数据：loadScript 是增量 append（同名事件
				// 标签/告警会翻倍 —— 上一局 88 条告警第二局变 179 条，
				// @EVENTFIRST 的 6 个声明变 12 个，事件组重复执行）
				m_executionEngine.loadScripts(m_erbDir);
				m_parseTable.markLoadComplete();   // 声明定稿 → 全体脚本就地定型/重绑 → 诊断

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
        stopAsyncLoad();
		qInfo() << "[EraEngine] closeGame: 卸载当前游戏并释放内存";
		setHasError(false);
		m_processState.requestHalt();        // 停掉执行链（若还在跑）
		m_processState.clearQuitRequest();   // 消费掉 QUIT 请求
		m_processState.clearRestartRequest();// 关闭游戏不走重启路径（restart 由宿主 reload 消费）
		m_audio.stopBgm();
		m_audio.stopSounds();
		// 控制台不清屏：QUIT 后最后一屏（测试汇总等）留在窗口上供查看，
		// 真正清屏发生在下一次装载（reload），届时新游戏从空控制台开始。
		// 变量/角色运行时数据回默认（释放内建数组与角色容器）
		m_executionEngine.handleResetData();
		// 脚本 AST/标记区/函数表/标签/告警 全量释放
		m_parseTable.clear();
		m_gameDirectory.clear();
        m_storageRoot.clear();
        m_executionEngine.setGameDataDir(QString());
        m_configLoader.clearFiles();
		m_csvDir.clear();
		m_erbDir.clear();
		ResourceImageProvider::setRoot(QString());
		emit gameDirectoryChanged();
}

bool EraEngine::resolveGameDirs()
{
		if (m_gameDirectory.isEmpty()) {
				m_csvDir.clear();
				m_erbDir.clear();
				return false;
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
		if (m_csvDir.isEmpty() || m_erbDir.isEmpty()) {
				setHasError(true);
				return false;
		}
		// 存档目录注入（SAVEDATA/LOADDATA/DELDATA/CHKDATA 用）
		m_executionEngine.setGameDirectory(m_storageRoot);
		// 目录已变：CSV 预扫描路径树作废（csvTree() 会按新 m_csvDir 重建）
		m_csvTreeValid = false;
		return true;
}

// ---------------------------------------------------------------------------
// CSV 路径树 —— 一次预扫描，供常量表 / Item.csv / 角色 CSV 等全部装载阶段共享。
// SAF（content://）下每次目录列举都是 DocumentsProvider 跨进程往返
// （~0.5-1s/目录，见 docs/android-saf.md）：此前 loadCsvDirectory、Item.csv
// 查找、loadCharaDirectory 三趟独立遍历同一棵树（eraH 实测 ~190 次往返、
// 约两分钟纯遍历开销）。首次调用时扫描一次并计时留痕；resolveGameDirs()
// 换目录后失效重建。
const DirNode& EraEngine::csvTree()
{
		if (!m_csvTreeValid) {
				QElapsedTimer sw;
				sw.start();
				// 扫描进度 → QML 进度条：collectTree 在**后台线程**跑（CSV 准备阶段），
				// 进度回调也在该线程触发，用队列连接投递到主线程写心跳属性
				// （loadStage/loadProcessed/loadTotal → Main.qml 的确定性进度条）。
				const QString stage = QStringLiteral("扫描 CSV 目录");
				m_csvTree = m_fileSystem.collectTree(
				    m_csvDir,
				    [this, stage](int done, int total) {
						const qint64 ms = QDateTime::currentMSecsSinceEpoch() - m_loadChainStartMs;
						QMetaObject::invokeMethod(
						    this,
						    [this, stage, done, total, ms]() {
							    applyLoadHeartbeat(0, stage, done, total, ms, false);
						    },
						    Qt::QueuedConnection);
				    });
				m_csvTreeValid = true;
				int dirCount = 1, fileCount = 0;   // 树根也算一个目录
				const std::function<void(const DirNode&)> count = [&](const DirNode& n) {
						fileCount += n.files.size();
						dirCount += n.subdirs.size();
						for (const DirNode& sub : n.subdirs) count(sub);
				};
				count(m_csvTree);
				qDebug() << "[load] CSV 路径树" << m_csvDir << ":" << dirCount << "目录 /"
				         << fileCount << "文件，耗时" << sw.elapsed() << "ms";
		}
		return m_csvTree;
}

void EraEngine::loadConfigFiles()
{
		m_configLoader.clearFiles();   // 幂等：允许在探测出回退编码后重读一次
		// 1. _default.config（最低优先级，位于 CSV 目录）
		// 2. emuera.config（中等优先级，位于游戏根目录）
		// 3. _fixed.config（最高优先级，位于 CSV 目录）
		if (!m_csvDir.isEmpty()) {
				// C# 同时兼容 _default.config / default.config。
				QString defaultConfig = GamePaths::join(m_csvDir, "_default.config");
				if (!m_fileSystem.fileExists(defaultConfig))
					defaultConfig = GamePaths::join(m_csvDir, "default.config");
				if (m_fileSystem.fileExists(defaultConfig)) m_configLoader.loadConfigFile(defaultConfig, 0);
		}
		const QString mainConfig = GamePaths::join(m_gameDirectory, "emuera.config");
		if (m_fileSystem.fileExists(mainConfig)) m_configLoader.loadConfigFile(mainConfig, 1);
		if (!m_csvDir.isEmpty()) {
				// C# 同时兼容 _fixed.config / fixed.config。
				QString fixedConfig = GamePaths::join(m_csvDir, "_fixed.config");
				if (!m_fileSystem.fileExists(fixedConfig))
					fixedConfig = GamePaths::join(m_csvDir, "fixed.config");
				if (m_fileSystem.fileExists(fixedConfig)) m_configLoader.loadConfigFile(fixedConfig, 2);
				// _Rename.csv 只有在配置明确启用时才加载（C# UseRenameFile，默认 false）。
				const bool useRename = m_configLoader.getBool(QStringLiteral("_Rename.csvを利用する"), false);
				const QString renameCsv = GamePaths::join(m_csvDir, "_Rename.csv");
				if (useRename && m_fileSystem.fileExists(renameCsv)) {
						m_executionEngine.getErbLoader().loadRenameFile(renameCsv);
				}
		}
		// 界面设置（字体/字号/行高/颜色/帧率/窗口…）从配置读入（对齐 C# ConfigData）
		m_guiManager.loadFromConfig(m_configLoader);
}

void EraEngine::loadConstantData(CsvTextTable* texts)
{
		if (m_csvDir.isEmpty()) return;

		// CSV 文本表：调用方（装载链）给的同一张表，或自建一张临时表
		// （独立调用方；每文件仍只读一次，但没有跨消费者共享）。
		// 本函数收尾 clear()：CSV 文本不长期驻留。
		CsvTextTable localTexts;
		CsvTextTable& csvText = texts ? *texts : localTexts;

		// 常量名表（CSV 名 → 下标）：只从 CSV 目录读取，对齐 C# ConstantData。
		// 查预扫描路径树（csvTree() 一次遍历后所有阶段共享，见其注释）。
		// 后台线程 → 主线程的进度上报（节流：最快 100ms 一次 —— 1328 个 CSV
		// 会把主线程事件队列刷爆；最后一个文件总是上报，进度条能走满）。
		const qint64 progressBaseMs = m_loadChainStartMs;
		const auto lastProgressMs = std::make_shared<qint64>(0);
		const auto reportProgress = [this, progressBaseMs, lastProgressMs](
		                                 const QString& stage, int done, int total) {
				const qint64 now = QDateTime::currentMSecsSinceEpoch();
				if (done < total && now - *lastProgressMs < 100) return;
				*lastProgressMs = now;
				QMetaObject::invokeMethod(
				    this,
				    [this, stage, done, total, now, progressBaseMs]() {
						applyLoadHeartbeat(0, stage, done, total, now - progressBaseMs, false);
				    },
				    Qt::QueuedConnection);
		};
		const int tables = m_constantTable.loadCsvDirectory(
		    csvTree(), m_searchSubdirectory,
		    [&reportProgress](int done, int total) {
				reportProgress(QStringLiteral("读取 CSV 常量表"), done, total);
		    },
		    &csvText);

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
				if (!nm.isEmpty()) m_variableStorage.setGlobalStr1D(key + QStringLiteral("NAME"), i, nm,
				                                              eravar::kGlobalScope);
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
				if (!v.isEmpty()) m_variableStorage.setGlobalStr1D(QStringLiteral("STR"), i, v, eravar::kGlobalScope);
			}
			qDebug() << "[load] STR 变量填充:" << n << "槽";
		}


		// 变量尺寸表（对齐 C# VariableData 读取 VariableSize.CSV）
		int sizesLoaded = 0;
		// C# 是 csvDir + "VariableSize.CSV" 顶层直拼 —— 查树顶层即可
		const QStringList sizeCandidates = FileSystem::listFilesFromTree(
		    csvTree(), QStringList{"VariableSize.csv"}, false);
		for (const QString& path : sizeCandidates) {
				bool textOk = false;
				const QString sizeText = csvText.get(path, &textOk);
				if (!textOk) {
						qWarning() << "[var] VariableSize 加载失败:" << path;
						continue;
				}
				if (m_variableStorage.loadVariableSizesFromText(sizeText, path))
						++sizesLoaded;
		}
		// ITEMPRICE：Item.csv **第 3 列**是价格（对齐 C# ConstantData.LoadData 的
		// `loadDataTo(csvDir + "ITEM.CSV", itemIndex, ItemPrice, disp)`：tokens[0] 是
		// 下标、tokens[2] 是价格，不可解析者保留 0）。C# 的顺序是**先**
		// loadVariableSizeData、**再** 逐个 loadDataTo —— 尺寸定下来之前填会按默认
		// 1000 截断，而且必须写进常量容器：ERB 的 `ITEMPRICE:i` 读的是
		// hasSystemVariable -> getSystemVariable（此前无 ITEMPRICE 分支，恒 0），
		// 写进 m_globalInt1D 等于没填 —— 这就是「隙間商店价格全为 $0」的回归。
		{
			// C# 是 csvDir + "ITEM.CSV" 顶层直拼（不递归）；eraH 的 Item.csv 也在
			// 顶层。先查树顶层（零往返），未命中再全树兜底 —— 此前的递归查找
			// 为一个顶层文件把整棵树重新走了一遍（eraH 实测 69 秒）。
			QStringList itemCandidates = FileSystem::listFilesFromTree(
			    csvTree(), QStringList{QStringLiteral("Item.csv")}, false);
			if (itemCandidates.isEmpty())
					itemCandidates = FileSystem::listFilesFromTree(
					    csvTree(), QStringList{QStringLiteral("Item.csv")}, m_searchSubdirectory);
			if (!itemCandidates.isEmpty()) {
				// Item.csv 的字节与常量名表共用（同一次读取）
				bool itemTextOk = false;
				const QString itemText = csvText.get(itemCandidates.first(), &itemTextOk);
				CsvLoader itemLoader;
				if (!itemTextOk) {
					qWarning() << "[load] CSV 读取失败:" << itemCandidates.first();
				} else {
					itemLoader.loadContent(itemCandidates.first(), itemText);
					const QString table = QFileInfo(itemCandidates.first()).baseName();
					// C# `ItemPrice = new Int64[MaxDataList[itemIndex]]`：按当前尺寸重建
					m_variableStorage.resetItemprice();
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
						m_variableStorage.setSystemVariable(QStringLiteral("ITEMPRICE"), index, price);
						++filled;
					}
					qDebug() << "[load] ITEMPRICE 填充:" << filled << "项";
				}
			}
		}

		// 角色 CSV（对齐 C# ConstantData.loadCharacterData）：
		// `Config.GetFiles(csvDir, "CHARA*.CSV")` = 扫描 CSV 目录下所有
		// Chara*.csv（SearchSubdirectory 时含子目录）—— eraTW 放在
		// CSV/Chara/、eraH 按种族放在 CSV/人間/**・CSV/悪魔/**。
		// 旧实现固定拼 `<Csv>/Chara` 且不递归：eraH 一个角色都读不到，
		// 于是 EXISTCSV(0) 为假 -> C_ADD_CHARA(0) 直接 return 2 ->
		// CHARANUM 停在 0 -> 战斗队伍为空 -> 委托 11 的战斗开场即「全灭」。
		const int charaLoaded = CsvLoader::loadCharaDirectory(
		    csvTree(), &m_constantTable, &m_variableStorage, m_searchSubdirectory,
		    [&reportProgress](int done, int total) {
				reportProgress(QStringLiteral("读取角色 CSV"), done, total);
		    },
		    &csvText);   // 常量名表已读过同一批 Chara*.csv：共用这一份字节
		// CSV* 系函数读「模板值」：装载完角色 CSV 后立刻快照一份
		// （此后脚本对 VAR:角色:下标 的修改不再污染模板）
		m_variableStorage.snapshotCharaTemplates();
		qDebug() << "[EraEngine] CSV 目录:" << m_csvDir
		         << " 常量表:" << tables << "(" << m_constantTable.nameCount() << "项)"
		         << " VariableSize:" << sizesLoaded
		         << " 角色:" << charaLoaded;
		// CSV 文本表到此为止：全部消费者（常量名表 / GameBase / VariableSize /
		// Item / 角色 CSV）都已从**同一份字节**取完，立即释放，不长期驻留。
		// 实测规模（一次装载峰值）：eraTW 363 KB 文本 / eraMegaten 8.2 MB
		// —— 相对 GB 级 AST 可忽略，但「能释放就释放」。
		qDebug() << "[load] CSV 文本表:" << csvText.fileCount() << "文件 /"
		         << csvText.charCount() << "字符（读盘" << csvText.readCount()
		         << "次，装载收尾释放）";
		csvText.clear();

		// TRAINNAME 名表不在这里刷新：本函数现在跑在**后台线程**（reloadAsync 的
		// CSV 准备），而 SystemStateMachine 是主线程 QObject。刷新由调用方在
		// 主线程完成（reloadAsync 的完成回调、startErbLoadAsync 之前那一步）。
}

// 从已加载配置里取「编码 / 子目录检索」等引擎级设置
//  · サブディレクトリを検索する  —— Config.SearchSubdirectory（C# 默认 false，era 游戏多配 YES）
//  · TextEncoding / テキストエンコーディング / 文字コード —— 强制读编码（缺省 AUTO = 逐文件嗅探）
void EraEngine::resolveTextConfig()
{
		// C# ConfigData 的默认值是 false；游戏需要递归时由配置文件显式开启。
		m_searchSubdirectory = m_configLoader.getBool(QString::fromUtf8("サブディレクトリを検索する"), false);

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

		// ---- 语言编码（对齐 C# LangManager.setEncode(Config.Encode)）----
		// STRLENS / SUBSTRING / STRFIND / STRLENFORM 的「位置/长度」按该编码的字节数计
		// （见 expression_evaluator.h 的 LangManager 一节）。
		// 来源**只有**配置里显式的 `内部で使用する東アジア言語`（= C# 的 Config.Encode，
		// 932/936/949/950）：它是「游戏语言」这一声明本身。ROM 探测与逐文件嗅探只服务于
		// 「把文件读进来」（内部一律转成 Qt 原生字符 / UTF-8），不参与任何运行期语义 ——
		// 所以这里**不**取探测结论，也不取回退编码。
		// 配置没写时用 C# 默认值 SHIFT-JIS。
		// （此前从未调用 setLanguageEncoding，m_langEncoding 恒为 Shift-Jis：中文/韩文
		//   游戏即便声明了 CHINESE_HANS，LENGTHBYTE 类函数仍按日文语义算。）
		TextEncoding langEnc = TextEncoding::ShiftJis;   // C# Config.Encode 默认（JAPANESE）
		if (fallbackFromConfig) langEnc = fallback;
		m_expressionEvaluator.setLanguageEncoding(langEnc);

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
		         << " 语言编码:" << TextCodecUtil::name(langEnc)
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
		const QString path = GamePaths::join(m_csvDir, QStringLiteral("_fixed.config"));
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

void EraEngine::copyTextToClipboard(const QString& text)
{
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text, QClipboard::Clipboard);
}

bool EraEngine::debugMode() const
{
const QStringList args = QCoreApplication::arguments();
    for (const QString& arg : args)
        if (arg == QStringLiteral("--debug"))
            return true;
    return false;
}

bool EraEngine::executeConsoleCommand(const QString& command)
{
    QString text = command.trimmed();
    if (text.startsWith(QLatin1Char('\\'))) text.remove(0, 1);
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return true;
    const QString op = parts.first().toUpper();
    if (op == QStringLiteral("CONFIG")) {
        emit consoleCommandRequested(QStringLiteral("CONFIG"));
        return true;
    }
    if (op == QStringLiteral("QUIT") || op == QStringLiteral("EXIT")) {
        closeGame();
        emit quitRequested();
        return true;
    }
    if (op == QStringLiteral("REBOOT")) {
        emit restartRequested();
        return true;
    }
    if (op == QStringLiteral("OUTPUT") || op == QStringLiteral("OUTPUTLOG")) {
        const QString path = m_gameDirectory + QStringLiteral("/emuera.log");
        return m_console.outputLog(path);
    }
    if (op == QStringLiteral("DEBUG")) {
        if (!debugMode()) {
            m_console.printPlain(QStringLiteral("デバッグウインドウは--debug引数付きで起動したときのみ使えます"));
            m_console.newline();
            return false;
        }
        emit consoleCommandRequested(QStringLiteral("DEBUG"));
        return true;
    }
    if (!m_configLoader.getBool(QStringLiteral("デバッグコマンドを使用する"), false)
        && !m_configLoader.getBool(QStringLiteral("UseDebugCommand"), false)) {
        m_console.printPlain(QStringLiteral("不明なコンソールコマンド: ") + command);
        m_console.newline();
        return false;
    }
    m_console.printPlain(QStringLiteral("デバッグコマンドは未実装です: ") + command);
    m_console.newline();
    return false;
}

QVariantList EraEngine::configItems() const
{
    static const QList<QStringList> defs = {
        {QStringLiteral("大文字小文字の違いを無視する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("_Rename.csvを利用する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("_Replace.csvを利用する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("マウスを使用する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("メニューを使用する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("デバッグコマンドを使用する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("多重起動を許可する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("オートセーブを行なう"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("キーボードマクロを使用する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("ウィンドウの高さを可変にする"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("描画インターフェース"),QStringLiteral("enum"),QStringLiteral("システム")},
        {QStringLiteral("ウィンドウ幅"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("ウィンドウ高さ"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("ウィンドウ位置X"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("ウィンドウ位置Y"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("起動時のウィンドウ位置を指定する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("起動時にウィンドウを最大化する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("履歴ログの行数"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("PRINTCを並べる数"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("PRINTCの文字数"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("フォント名"),QStringLiteral("string"),QStringLiteral("システム")},
        {QStringLiteral("フォントサイズ"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("一行の高さ"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("文字色"),QStringLiteral("string"),QStringLiteral("システム")},
        {QStringLiteral("背景色"),QStringLiteral("string"),QStringLiteral("システム")},
        {QStringLiteral("選択中文字色"),QStringLiteral("string"),QStringLiteral("システム")},
        {QStringLiteral("履歴文字色"),QStringLiteral("string"),QStringLiteral("システム")},
        {QStringLiteral("フレーム毎秒"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("最大スキップフレーム数"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("スクロール行数"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("無限ループ警告までのミリ秒数"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("表示する最低警告レベル"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("ロード時にレポートを表示する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("ロード時に引数を解析する"),QStringLiteral("enum"),QStringLiteral("システム")},
        {QStringLiteral("呼び出されなかった関数を無視する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("関数が見つからない警告の扱い"),QStringLiteral("enum"),QStringLiteral("システム")},
        {QStringLiteral("関数が呼び出されなかった警告の扱い"),QStringLiteral("enum"),QStringLiteral("システム")},
        {QStringLiteral("デバッグコマンドを使用した時にMASTERの名前を変更する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("ボタンの途中で行を折りかえさない"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("サブディレクトリを検索する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("読み込み順をファイル名順にソートする"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("最終更新コード"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("表示するセーブデータ数"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("eramaker互換性に関する警告を表示する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("システム関数の上書きを許可する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("システム関数が上書きされたとき警告を表示する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("関連づけるテキストエディタ"),QStringLiteral("string"),QStringLiteral("システム")},
        {QStringLiteral("テキストエディタコマンドライン指定"),QStringLiteral("enum"),QStringLiteral("システム")},
        {QStringLiteral("エディタに渡す行指定引数"),QStringLiteral("string"),QStringLiteral("システム")},
        {QStringLiteral("同名の非イベント関数が複数定義されたとき警告する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("解釈不可能な行があっても実行する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("CALLNAMEが空文字列の時にNAMEを代入する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("セーブデータをsavフォルダ内に作成する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("擬似変数RANDの仕様をeramakerに合わせる"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("DRAWLINEを常に新しい行で行う"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("関数・属性については大文字小文字を無視しない"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("全角スペースをホワイトスペースに含める"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("ver1739以前の非ボタン折り返しを再現する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("内部で使用する東アジア言語"),QStringLiteral("enum"),QStringLiteral("システム")},
        {QStringLiteral("ONEINPUT系命令でマウスによる2文字以上の入力を許可する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("イベント関数のCALLを許可する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("SPキャラを使用する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("セーブデータをバイナリ形式で保存する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("ユーザー関数の全ての引数の省略を許可する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("ユーザー関数の引数に自動的にTOSTRを補完する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("FORM中の三連記号を展開しない"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("RealRounding"),QStringLiteral("rounding"),QStringLiteral("システム")},
        {QStringLiteral("TIMESの計算をeramakerにあわせる"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("キャラクタ変数の引数を補完しない"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("文字列変数の代入に文字列式を強制する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("UPDATECHECKを許可しない"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("ERD機能を利用する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("VARSIZEの次元指定をERD機能に合わせる"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("ERDで定義した識別子とローカル変数の重複を確認する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("行連結の改行コードの置換文字列"),QStringLiteral("string"),QStringLiteral("システム")},
        {QStringLiteral("外部プラグインが有効時に警告を表示する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("セーブデータを圧縮して保存する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("CONFIGファイルの内容を英語で保存する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("Emueraの表示言語"),QStringLiteral("string"),QStringLiteral("システム")},
        {QStringLiteral("Emueraのアイコンのパス"),QStringLiteral("string"),QStringLiteral("システム")},
        {QStringLiteral("表示したテキストをクリップボードにコピーする"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("テキスト中の<>タグを無視する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("<>を次の文で置き換える"),QStringLiteral("string"),QStringLiteral("システム")},
        {QStringLiteral("新しい行のみコピーする"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("画面のリフレッシュ時にクリップボードとバッファを消去する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("左クリックをトリガーにする"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("ホイールクリックをトリガーにする"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("ダブルクリックをトリガーにする"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("WAITをトリガーにする"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("INPUTをトリガーにする"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("クリップボードに貼り付ける行数"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("総バッファサイズ"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("スクロールの行数"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("クリップボードの更新間隔(ミリ秒)"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("Rikaichanを使用する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("Rikaichanのファイルパス"),QStringLiteral("string"),QStringLiteral("システム")},
        {QStringLiteral("ポップアップの背景色"),QStringLiteral("string"),QStringLiteral("システム")},
        {QStringLiteral("ポップアップの文字色"),QStringLiteral("string"),QStringLiteral("システム")},
        {QStringLiteral("翻訳中の語句を強調表示する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("Ctrl-Zで元に戻す機能を有効にする"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("起動時にデバッグウインドウを表示する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("デバッグウインドウを最前面に表示する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("デバッグウィンドウ幅"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("デバッグウィンドウ高さ"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("デバッグウィンドウ位置を指定する"),QStringLiteral("bool"),QStringLiteral("システム")},
        {QStringLiteral("デバッグウィンドウ位置X"),QStringLiteral("int"),QStringLiteral("システム")},
        {QStringLiteral("デバッグウィンドウ位置Y"),QStringLiteral("int"),QStringLiteral("システム")}
    };
    QVariantList out;
    for (const auto& d : defs) { QVariantMap m; m[QStringLiteral("key")]=d[0]; m[QStringLiteral("type")]=d[1]; m[QStringLiteral("group")]=d[2]; m[QStringLiteral("value")]=configValue(d[0]); out.append(m); }
    return out;
}

QString EraEngine::configValue(const QString& key) const {
    if (key == QLatin1String("RealRounding")) {
        const QString value = m_configLoader.getConfig(key);
        return value == QLatin1String("floor") || value == QLatin1String("ceil") ? value : QStringLiteral("round");
    }
    return m_configLoader.getConfig(key);
}
bool EraEngine::setConfigValue(const QString& key, const QString& value)
{
    if (key == QLatin1String("RealRounding") && value != QLatin1String("round")
        && value != QLatin1String("floor") && value != QLatin1String("ceil")) return false;
    m_configLoader.setConfig(key, value);
    emit m_configLoader.configChanged(key, value);
    return true;
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

void EraEngine::reloadSavedData()
{
    m_executionEngine.reloadSaveSnapshots();
    m_expressionEvaluator.reloadCharacterSnapshots();
}

void EraEngine::loadFinishedHook()
{
        // setRoot() already scans and installs the resource atlas for this game.
        m_executionEngine.reloadSaveSnapshots();
        m_expressionEvaluator.reloadCharacterSnapshots();
        const auto& loader = m_executionEngine.getErbLoader();
        qDebug() << "[EraEngine] 装载完成：目录" << m_gameDirectory
                 << "ERB" << m_erbDir << "CSV" << m_csvDir
                 << "文件" << loader.loadedFileCount()
                 << "脚本" << m_parseTable.scriptNames().size()
                 << "个，告警" << m_parseTable.parseWarningCount()
                 << "条，变量" << m_parseTable.variableTable().count() << "个";
        if (loader.loadedFileCount() > 0 && !m_parseTable.hasLabel(QStringLiteral("SYSTEM_TITLE"))) {
                qWarning() << "[EraEngine] 已装载脚本但未找到 @SYSTEM_TITLE：请检查游戏目录和 SYSTEM_TITLE 所在 ERB"
                           << "ERB=" << m_erbDir;
        }
		// 随机种子：启动时自动产生；`--seed` / setRandomSeed 固定后可整局复现
		qDebug() << "[EraEngine] 随机种子:" << m_expressionEvaluator.randomSeed()
		         << "（MT19937，可用 --seed 复现）";
		for (int i = 0; i < qMin(12, m_parseTable.parseWarningCount()); ++i) {
				qWarning() << "  [parse warn]" << m_parseTable.parseWarnings().at(i);
		}
}

void EraEngine::reloadAsync()
{
        // 新会话必须先完整终止旧 loader/语义任务，再清空旧 AST 和变量表。
        stopAsyncLoad();
        clearLoadedGame();
		if (m_gameDirectory.isEmpty()) {
				completeAsyncLoad(false);
				return;
		}
		// ---- 与同步 reload() 完全对齐的「重新开始」复位 ----
		// 异步装载是 GUI（openDirectory / 打开目录…）的**唯一**路径，此前只清了
		// QUIT 请求与解析表，漏了下面三项，导致「重写装载」后：
		//   ① 控制台不清屏 —— 上一局画面残留，新画面往下堆（行数翻倍）；
		//   ② execState 停在 Halt —— 上一局 QUIT/closeGame 后 run() 第一圈
		//      isRunning()==false 直接退出，标题永不出现，控制台也不请求输入，
		//      于是输入被拒（ConsoleBackend::submitInput 报 kind "" / 等待中 false）。
		setHasError(false);                  // 清掉上一次的错误灯
		m_processState.clearQuitRequest();   // 清掉上一局的 QUIT 请求
        m_processState.clearRestartRequest();
        // 构建期间保持 Halt；完整发布后再允许脚本执行。

		QElapsedTimer phase;   // 主线程各阶段耗时：定位「装载时界面卡一下」的来源
		phase.start();
		if (!resolveGameDirs()) {
				completeAsyncLoad(false);
				return;
		}
		loadConfigFiles();
		resolveTextConfig();
		// CSV 文本表：GameBase.csv 在这里（主线程）读一次，随后的 CSV 准备
		// （后台线程：常量名表 / VariableSize / Item / 角色 CSV）从**同一张表**
		// 取字节，因此 GameBase.csv 不会被读第二次。shared_ptr 让表活到后台
		// 任务结束（表在 loadConstantData 收尾 clear()）。
		const auto csvText = std::make_shared<CsvTextTable>();
		loadGameBaseData(csvText.get());
		// **CSV 常量/角色准备移到后台线程**（SAF 上是 ~60s 的 provider 往返：
		// 路径树预扫描 + 1328 张常量 CSV + 1318 个角色 CSV）。此前整段在主线程
		// 同步执行 —— 加载遮罩虽有进度文案，主线程却被占住、界面无法重绘。
		// 完成后回到主线程（QFutureWatcher::finished 由事件循环投递）再启动
		// ERB 异步装载，见 startErbLoadAsync()。
		const quint64 prepGeneration = m_loadGeneration;
		const qint64 prepStartMs = QDateTime::currentMSecsSinceEpoch() - phase.elapsed();
		m_loadChainStartMs = prepStartMs;   // 进度「已耗时」的统一基准
		// 装载遮罩（loadingOverlay）的 visible 是 statusStrip.loading，而它只在
		// scriptsLoadStarted 时打开 —— CSV 准备现在跑在后台、紧接着才是 ERB，
		// 这个信号必须**提前到准备开始时**发，否则这几十秒是全黑屏（此前同步版
		// 主线程被占住同样看不到遮罩）。startErbLoadAsync 里不再重复发。
		// ---- loader 信号连接：必须**先于** scriptsLoadStarted 建立 ----
		// loadCompleted 的槽序有语义：EraEngine 的收尾槽要先跑（末尾置
		// m_completingLoad，随后才发 scriptsLoaded），外部监听者（QML/测试）
		// 后跑才能观察到「收尾窗口内 isLoadingScripts() == true」。
		// 连接此前在 CSV 准备完成之后才建立，而 scriptsLoadStarted 已提前到
		// 准备开始时发出，槽序颠倒 —— test_async_lifecycle 会立刻抓到。
		ErbLoader& loader = m_executionEngine.getErbLoader();
		const auto chainElapsed = [prepStartMs] {
				return QDateTime::currentMSecsSinceEpoch() - prepStartMs;
		};
		// loader 是引擎的成员（生命周期 ≥ 本回调），按值捕获指针即可
		ErbLoader* const loaderPtr = &loader;
		if (m_loadProgressConn) disconnect(m_loadProgressConn);
		if (m_loadHeartbeatConn) disconnect(m_loadHeartbeatConn);
		if (m_loadCompletedConn) disconnect(m_loadCompletedConn);
		m_loadProgressConn = connect(&loader, &ErbLoader::loadProgress,
		                             this, &EraEngine::scriptsLoadProgress);
		// UI 的唯一数据源：心跳快照 → loadStage/loadProcessed/loadTotal/loadElapsedMs/
		// loadStalled 五个属性（同步连接：心跳在主线程发）。
		m_loadHeartbeatConn = connect(&loader, &ErbLoader::loadHeartbeat,
		                              this, &EraEngine::applyLoadHeartbeat);
		resetLoadHeartbeat();
		const quint64 generation = prepGeneration;
		m_loadCompletedConn = connect(&loader, &ErbLoader::loadCompleted, this,
		                              [this, generation, chainElapsed, loaderPtr](bool ok) {
                if (generation != m_loadGeneration) {
                        // 更新的一代装载已经接管 UI（loadAsync 里的 stopAsyncLoad 会
                        // 让世代前进）：本次结果必须丢弃，但**不能静默** —— 否则
                        // 真机上只看到「装载没有下文」。
                        qInfo() << "[EraEngine] 异步装载完成回调已过期（世代" << generation
                                << "≠" << m_loadGeneration << "），结果丢弃";
                        return;
                }
				if (!ok) {
						qWarning() << "[EraEngine] 异步装载失败：脚本装载未完成（详见上面的 [ErbLoader]/[load] 日志）";
						loadFinishedHook();
						completeAsyncLoad(false);
						return;
				}
				// 收尾（声明定稿 → 全体脚本就地定型/重绑 → 诊断）由 markLoadComplete
				// 在全部脚本就绪后单趟完成；此处只做入口点收集与完成通知。
				// **收尾在 GUI 线程上**，eraTW 桌面实测 13s+（Android 更久）：前后各
				// 打一行，界面「卡住」的这几秒才有归属。心跳此时已经停了（loader 收尾），
				// 但 UI 不该把上一阶段的文案留在遮罩上 —— 手动置成「收尾」。
				const qint64 finalizeStart = chainElapsed();
				setLoadStageText(QStringLiteral("收尾（声明定稿/定型/校验）"));
				qInfo().noquote() << "[EraEngine] 异步装载·收尾阶段开始(声明定稿+定型+校验):"
				                  << loaderPtr->loadedFileCount() << "个文件";
				m_parseTable.markLoadComplete();
				const qint64 finalizeMs = chainElapsed() - finalizeStart;
				qInfo().noquote() << "[EraEngine] 异步装载·收尾阶段完成:" << finalizeMs << "ms";
				collectEntryPoints();
				loadFinishedHook();
				qInfo().noquote() << "[EraEngine] 异步装载·全链路: 同步准备 + 后台装载 + 收尾"
				                  << "共" << chainElapsed() << "ms";
				// 推迟到下一事件循环再发 scriptsLoaded（由 completeAsyncLoad 统一
				// 负责）：期间若有新的 loadAsync（世代 +1），本次完成即被丢弃 ——
				// 与原「语义阶段」相同的取消语义，只是不再有后台线程/future。
				// m_completingLoad 让 isLoadingScripts() 在收尾窗口内仍报「正在装载」。
				completeAsyncLoad(true);
		});

		emit scriptsLoadStarted();
		if (m_csvPrepConn) disconnect(m_csvPrepConn);
		m_csvPrepWatcher.setFuture(QtConcurrent::run([this, csvText]() { loadConstantData(csvText.get()); }));
		m_csvPrepConn = connect(&m_csvPrepWatcher, &QFutureWatcher<void>::finished, this,
		                        [this, prepGeneration, prepStartMs]() {
				m_scriptProcessor.clear();
				qInfo().noquote()
				    << "[EraEngine] 异步装载·CSV 准备阶段(路径树/常量/角色,后台)"
				    << (QDateTime::currentMSecsSinceEpoch() - prepStartMs) << "ms"
				    << (GamePaths::isContent(m_gameDirectory) ? "(content URI)" : "(本地目录)");
				if (prepGeneration != m_loadGeneration) {
						qInfo() << "[EraEngine] CSV 准备完成回调已过期（世代" << prepGeneration
						        << "≠" << m_loadGeneration << "），结果丢弃";
						return;
				}
				// TRAINNAME 名表依赖 CSV 名表；SystemStateMachine 是主线程 QObject，
				// 写入放回主线程（原来内联在 loadConstantData 尾部）。
				m_systemStateMachine.reloadTrainNames();
				startErbLoadAsync(prepGeneration);
		});
}


// ---------------------------------------------------------------------------
// 异步装载的完成发布（唯一入口）
//
// 为什么不能在发起调用里直接 emit：`loadAsync()` → `reloadAsync()` 里有几条
// 零工作量的提前返回（游戏目录为空 / CSV、ERB 目录解析失败）。它们此前直接
// `emit scriptsLoaded(false)`，而调用方（QProcess 工具、QML 之外的测试）都是
// `connect(); loadAsync(); loop.exec();` 的顺序 —— 信号在 exec 之前到达，
// `QEventLoop::quit()` 对尚未运行的循环是**空操作**，于是调用方永久挂起。
// 改动只影响「何时发」：投递到下一轮事件循环，此时调用方一定已在 loop 里
// （或还没进，但信号仍会在它进 loop 后立刻被处理，不再丢失）。
//
// 恰好一次：m_loadCompletionGen 在**登记时**就记为当前世代，同一代第二次调用
// 只报警不重发；世代由 stopAsyncLoad() 推进，旧世代的定时器在回调里被世代检查
// 丢弃 —— 所以「谁来发完成信号」由状态（世代 + 已登记世代）保证，而不是靠每
// 个 return 前记得发一次。
// ---------------------------------------------------------------------------
void EraEngine::completeAsyncLoad(bool ok)
{
		const quint64 generation = m_loadGeneration;
		if (m_loadCompletionGen == generation) {
				qWarning() << "[EraEngine] 重复的装载完成请求（世代" << generation
				           << "），已忽略：每代装载只发布一次 scriptsLoaded";
				return;
		}
		m_loadCompletionGen = generation;
		m_completingLoad = true;
		QTimer::singleShot(0, this, [this, generation, ok]() {
				m_completingLoad = false;
				if (generation != m_loadGeneration) return;   // 新的一代接管：丢弃
				if (ok) m_processState.setExecState(ExecState::Continue);
				emit scriptsLoaded(ok);
				// QML 的 onScriptsLoaded 已把遮罩收起，这里复位快照不会闪
				resetLoadHeartbeat();
		});
}

// ---------------------------------------------------------------------------
// ERB 异步装载的启动段：CSV 准备（后台线程）完成后由主线程调用。
// 这段代码原来内联在 reloadAsync() 里、紧跟同步的 CSV 准备之后；CSV 准备改为
// 后台执行后，它成为「后台 → 主线程」的接续点。世代与计时基准经参数传入，
// 取消语义不变（世代变化时回调直接丢弃、不会走到这里）。
void EraEngine::startErbLoadAsync(quint64 generation)
{
		ErbLoader& loader = m_executionEngine.getErbLoader();
		qInfo().noquote() << "[EraEngine] 异步装载·目录解析: 游戏" << m_gameDirectory
		                  << "ERB" << m_erbDir << "CSV" << m_csvDir;
        // （scriptsLoadStarted 已在 reloadAsync 的 CSV 准备开始时发出）
        if (generation == m_loadGeneration) {
                // 异步装载（后台 prep → 分块解析 → 主线程合并）的入口。之后的一切
                // 进展都由 ErbLoader 的阶段日志 + 心跳上报，这里只需保证入口可见。
                qInfo().noquote() << "[EraEngine] 进入异步装载（ERB" << m_erbDir
                                  << "），世代" << generation;
                loader.loadDirectoryAsync(m_erbDir);
        } else {
                // 走到这里说明本次装载已被更新的一代接管（正常调用链不会发生）。
                // 旧代码静默跳过 → UI 停在「正在装载脚本」且日志再无输出。
                qWarning() << "[EraEngine] 装载世代已变化（" << generation << "≠"
                           << m_loadGeneration << "），跳过本轮 ERB 目录装载";
        }
}

bool EraEngine::isLoadingScripts() const
{
		return m_completingLoad || m_executionEngine.getErbLoader().isLoading();
}

QStringList EraEngine::parseWarnings() const
{
        if (isLoadingScripts()) return {};
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

void EraEngine::loadGameBaseData(CsvTextTable* texts){
		// 对齐 C#：GameBase.csv 位于 CSV 目录（Program.CsvDir + "GAMEBASE.CSV"）
		if (m_csvDir.isEmpty()) {
				qDebug() << "[EraEngine] CSV 目录未解析，跳过 GameBase.csv";
				return;
		}
		// CSV 文本表：调用方给的是装载链的同一张表（随后常量名表 / 角色 CSV 也
		// 从它取字节）；没给则自建临时表（独立调用方，每文件仍只读一次）。
		CsvTextTable localTexts;
		CsvTextTable& csvText = texts ? *texts : localTexts;
		QString gameBasePath;
		// GameBase.csv 在 CSV 顶层（C# Program.CsvDir + "GAMEBASE.CSV" 直拼）——
		// 查预扫描树，不再多一次目录列举往返。
		const QStringList candidates = FileSystem::listFilesFromTree(
		    csvTree(), QStringList{"GameBase.csv"}, false);
		if (!candidates.isEmpty()) {
				gameBasePath = candidates.first();
		}
		if (gameBasePath.isEmpty()) {
				qDebug() << "[EraEngine] 未找到 GameBase.csv（CSV 目录：" << m_csvDir << "）";
				return;
		}

		// Load GameBase.csv using CsvLoader
		// 字节来自 CSV 文本表（与常量名表共用同一次读取）
		CsvLoader csvLoader;
		bool gameBaseTextOk = false;
		const QString gameBaseText = csvText.get(gameBasePath, &gameBaseTextOk);
		if (!gameBaseTextOk) {
				qDebug() << "[EraEngine] GameBase.csv 装载失败：" << gameBasePath;
				return;
		}
		csvLoader.loadContent(gameBasePath, gameBaseText);
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
        if (isLoadingScripts()) return;
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
                 << "SYSTEM_TITLE:" << (m_parseTable.hasLabel(QStringLiteral("SYSTEM_TITLE")) ? "有" : "无")
                 << "目录:" << m_gameDirectory
                 << "ERB:" << m_erbDir
                 << "脚本数:" << m_parseTable.scriptNames().size()
                 << "文件数:" << m_executionEngine.getErbLoader().loadedFileCount()
                 << "告警:" << m_parseTable.parseWarningCount()
                 << "变量:" << m_parseTable.variableTable().count();

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
        if (isLoadingScripts()) return;
		// 输入交付即离开标题：收起 QML 标准标题界面（无标题时是空操作）
		setDefaultTitleVisible(false);
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
        if (isLoadingScripts()) return;
		// 字符串输入：写入 RESULTS（局部字符串槽）后继续
		qDebug() << "[input] provideInputString" << value;
		m_console.notifyInputDone();
		m_systemStateMachine.resumeString(value);
}

void EraEngine::provideInputValues(const QVariantList& values)
{
        if (isLoadingScripts()) return;
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
// 标准标题画面（QML）：点 [0]/[1] 时交付选择。走与手动输入完全相同的
// provideInput 通路（写 RESULT -> notifyInputDone -> 状态机 resume），
// 因此「从头开始 / 读档」的后续流程、非法值重画都不需要另写。
// ---------------------------------------------------------------------------
void EraEngine::chooseTitle(int index)
{
		if (!m_defaultTitleVisible) return;
		provideInput(index);   // provideInput 内部会收起标题界面
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
		// EE FLOWINPUT canSkip：系统流程输入在 SKIPLOG 跳过中直接交缺省
		// （C# setWaitInput 的 `flowinputCanSkip && Console.MesSkip`）
		host.mesSkip = [this]() { return m_executionEngine.mesSkip(); };

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
		host.autoSave = [boolCfg]() { return boolCfg({QStringLiteral("オートセーブを行なう"),
		                                               QStringLiteral("AutoSave")}, true); };
		host.maxShopItem = [intCfg]() { return intCfg({QStringLiteral("販売アイテム数"),
		                                                QStringLiteral("MaxShopItem")}, 100); };
		host.comAbleDefault = [intCfg]() { return intCfg({QStringLiteral("COM_ABLE初期値"),
		                                                   QStringLiteral("ComAbleDefault")}, 1); };
		host.printCPerLine = [intCfg]() { return intCfg({QStringLiteral("PRINTCを並べる数"),
		                                                  QStringLiteral("PrintCPerLine")}, 3); };
		host.compatiCallEvent = [boolCfg]() { return boolCfg({QStringLiteral("イベント関数のCALLを許可する"),
		                                                       QStringLiteral("CompatiCallEvent")}, false); };
		host.titleMenuString = [](int index) {
				return index == 0 ? QStringLiteral("开始游戏") : QStringLiteral("读取存档");
		};

		// ---- 标准标题画面（QML 渲染）----
		// 状态机只在「没有 @SYSTEM_TITLE」时把开关翻开；QML 的 TitleScreen 组件
		// 绑 eraEngine.defaultTitleVisible 画标题，点菜单调 chooseTitle()。
		host.setDefaultTitleVisible = [this](bool on) { setDefaultTitleVisible(on); };

		// ---- 数据层（RESETDATA 指令 / 标准标题「[0] 从头开始」）----
		// 此前 resetData 从未接线：RESETDATA 只在 ExecutionEngine 里有一半实现，
		// 系统层的新开游戏重置根本没被调用。
		host.resetData = [this]() { m_executionEngine.handleResetData(); };
		// 新游戏开局注册角色（C# Process.SystemProc.cs:198-206 的
		// `ResetData(); AddCharacterFromCsvNo(0); if (DefaultCharacter > 0) …`）。
		// 此前这两个回调从未接线：state machine 里的 `if (m_host.addCharacterFromCsvNo)`
		// 恒为假 -> 一个角色都没登记 -> CHARANUM 停在 0 -> 脚本惯例的
		// `DELCHARA 0`（删掉开局那个 0 号角色）直接报「番号超出角色范围」。
		host.addCharacterFromCsvNo = [this](int csvNo) { m_variableStorage.addChara(csvNo); };
		host.defaultCharacter = [this]() -> int {
				// GameBase.csv 的「最初からいるキャラ」（C# GameBase.cs:124，默认 -1）
				return int(m_gameBaseData.get(QString::fromUtf8("最初からいるキャラ")).toLongLong());
		};

		// ---- 存/读档（SAVEGAME / LOADGAME 系统画面）----
		// 文件名/目录对齐 ExecutionEngine 的 SAVEDATA/LOADDATA（sav/save##.sav），
		// 内容 = C#1808 EraBinaryData document + 描述。
		// 此前这四个回调从未接线：读档画面所有槽位恒判「没有数据」、保存恒失败。
		// 存档槽位数：与 SAVENOS 同源（C# 两处都读 Config.SaveDataNos），
		// 走求值器已接好的 provider，避免两处键名/夹紧规则各写一遍。
		host.saveDataNos = [this]() { return int(m_expressionEvaluator.saveDataNos()); };

		// ---- TRAINNAME（CSV/Train.csv 的调教指令名表）----
		// C# Process.cs:143 `TrainName = constant.GetCsvNameList(TRAINNAME)`：
		// 系统层拿它做三件事 ——
		//   1) 逐个扫描 @COM_ABLE0..N，把「本次可用」的指令填进 comAble，
		//      用户输入命中 comAble 时直接 @EVENTCOM -> @COMxx（eraTW 的正常路径）；
		//   2) @SHOW_USERCOM 期间把编号翻译成指令名；
		//   3) DOTRAIN 的范围检查（train >= TrainName.Length 报错）。
		// 长度取 VariableSize.CSV 的 TRAINNAME（=1000），**不是** Train.csv 的行数
		// —— eraTW 的 Train.csv 只有 ~325 行，@USERCOM:62 的 `DOTRAIN 999`
		// 正是靠这个 1000 长度才合法。此前这个回调从未接线：m_trainNames 恒空，
		// comAble 恒空 -> 点任何指令都走 @USERCOM 兜底（而不是 @COMxxx），
		// 且 DOTRAIN 999 直接报「值超出 TRAINNAME 范围」。
		host.trainNames = [this]() {
				const int n = m_variableStorage.variableConfig()
				                  .getSize1D(QStringLiteral("TRAINNAME"));
				QStringList names;
				names.reserve(n > 0 ? n : 0);
				for (int i = 0; i < n; ++i) {
						names.append(m_variableStorage.getGlobalStr1D(
						    QStringLiteral("TRAINNAME"), i, eravar::kGlobalScope));
				}
				return names;
		};
        host.checkData = [this](int index, QString* message) {
            const int status = m_executionEngine.checkSaveSnapshot(index);
            if (message) *message = status == 0 ? QStringLiteral("ＯＫ") : QStringLiteral("无法加载存档");
            return status == 0;
        };
        host.saveTo = [this](int index, const QString& text) {
            return m_executionEngine.writeSaveSnapshot(index, text);
        };
        host.loadFrom = [this](int index) { return m_executionEngine.loadSaveSnapshot(index); };

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
		// 「返回标题」：丢弃当前执行上下文，重跑系统状态机（Title_Begin）。
		//
		// 旧实现只设置 systemState 就直接返回 —— 但状态机此刻停在 WaitInput
		// 挂起点上（run() 早已返回，等输入才 resume），没有任何东西把它唤醒：
		// 真机上点「返回标题」完全没反应，上一画面（读档菜单等）原样留在屏上。
		//
		// 按 reloadAsync 的同款复位做全：
		//   ① execState 可能停在 Halt（脚本 QUIT/closeGame 之后）→ 复位成
		//      Continue，否则 pump 第一圈 isRunning()==false 直接退出，
		//      标题永远不会出现、输入也会被拒；
		//   ② 清屏：上一画面的文本否则会堆在新标题之上；
		//   ③ 系统状态置回 Title_Begin 并**真正 run() 一次**
		//      （beginTitle → @SYSTEM_TITLE / QML 标准标题），这才离开挂起点。
		setHasError(false);
		m_processState.clearQuitRequest();
		m_processState.clearRestartRequest();
		m_processState.setExecState(ExecState::Continue);
		m_console.notifyInputDone();
		m_console.clearAll();
		m_audio.stopBgm();
		m_audio.stopSounds();
		GraphicsStore::clearAll();
		m_processState.setSystemState(SystemStateCode::Title_Begin);
		m_statusManager.transitionToTitle();
		const ExecState st = m_systemStateMachine.run();
		if (st == ExecState::WaitInput || st == ExecState::WaitSystemInput) {
				qDebug() << "[EraEngine] 返回标题：等待输入";
		} else {
				qDebug() << "[EraEngine] 返回标题：状态机结束，state=" << static_cast<int>(st);
		}
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
