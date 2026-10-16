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
#ifndef ERAENGINE_H
#define ERAENGINE_H

#include <QObject>
#include <QTimer>
#include <memory>
#include <QVariantList>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QQmlEngine>
#include <QQmlContext>
#include "csv_loader.h"
#include "constant_table.h"
#include "encoding_probe.h"
#include "variable_storage.h"
#include "expression_evaluator.h"
#include "file_system_io.h"
#include "execution_engine.h"
#include "process_state.h"
#include "system_state_machine.h"
#include "system_status_manager.h"
#include "ast/logical_line.h"
#include "config_loader.h"
#include "script_processor.h"
#include "input_handler.h"

#include "variable_types.h"
#include "game_base_data.h"
#include "identifier_dictionary.h"
#include "era_parse_table.h"
#include "script_runner.h"
#include "console_backend.h"
#include "gui_manager.h"
#include "resource_image_provider.h"
#include "audio_pipeline_pool.h"

class EraDBusDebug;

class EraEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit EraEngine(QObject *parent = nullptr);
    ~EraEngine() override;   // unique_ptr<EraDBusDebug> 需要完整类型，定义在 .cpp
    
    // QML-exposable properties
		Q_PROPERTY(QString gameDirectory READ getGameDirectory WRITE setGameDirectory NOTIFY gameDirectoryChanged)
		Q_PROPERTY(GameBaseData* gameBaseData READ gameBaseData CONSTANT)
		Q_PROPERTY(EraTetrisInputSystem* inputSystem READ getInputSystem CONSTANT)
		Q_PROPERTY(InputHandler* inputHandler READ getInputHandler CONSTANT)
		Q_PROPERTY(SystemStatusManager* statusManager READ getStatusManager CONSTANT)
		Q_PROPERTY(ExecutionEngine* executionEngine READ getExecutionEngine CONSTANT)
		Q_PROPERTY(EraParseTable* parseTable READ getParseTable CONSTANT)
		Q_PROPERTY(ConsoleBackend* console READ getConsole CONSTANT)
        Q_PROPERTY(VariableStorage* variableStorage READ getVariableStorage CONSTANT)
        Q_PROPERTY(ExpressionEvaluator* expressionEvaluator READ getExpressionEvaluator CONSTANT)
		Q_PROPERTY(GuiManager* gui READ getGuiManager CONSTANT)
		// 音频播放管线池（C++ 控制端）：QML 按 capacity() 维护播放器并监听其信号
		Q_PROPERTY(AudioPipelinePool* audio READ getAudio CONSTANT)
		// 最近一次执行是否出错（底部状态灯用；装载/重新运行会复位）
		Q_PROPERTY(bool hasError READ hasError NOTIFY hasErrorChanged)
		// ---- 装载心跳快照（QML 的装载遮罩/状态灯直接绑这几个属性）----
		// 数据源是 ErbLoader 的 loadHeartbeat（主线程 1s 一次的快照，见该信号注释）：
		// 真机上装载是分钟级（OnePlus/Android 16 实测 eraH：920 文件 / 18MB ≈ 64s，
		// 其中约 53s 花在 SAF 元数据往返上），UI 必须能看出「在哪个阶段、做到第几个、
		// 已经多久、是不是停滞」，否则只能看到一行不动的「正在装载脚本…」。
		// 阶段名用粗粒度（解析/合并交错时不会来回闪）；计数与耗时同源，天然一致。
		Q_PROPERTY(QString loadStage READ loadStage NOTIFY loadHeartbeatChanged)
		Q_PROPERTY(int loadProcessed READ loadProcessed NOTIFY loadHeartbeatChanged)
		Q_PROPERTY(int loadTotal READ loadTotal NOTIFY loadHeartbeatChanged)
		Q_PROPERTY(qint64 loadElapsedMs READ loadElapsedMs NOTIFY loadHeartbeatChanged)
		Q_PROPERTY(bool loadStalled READ loadStalled NOTIFY loadHeartbeatChanged)
		// ---- 标准标题画面（QML 渲染）----
		// 无 @SYSTEM_TITLE 时，状态机翻开 defaultTitleVisible，由 QML 的
		// TitleScreen 组件画标准标题（标题/版本/作者/年份/说明 + [0]/[1] 菜单）；
		// QML 点菜单调用 chooseTitle() 交付输入。标题字符串来自 gameBaseData。
		Q_PROPERTY(bool defaultTitleVisible READ defaultTitleVisible NOTIFY defaultTitleVisibleChanged)
		Q_PROPERTY(QString titleMenu0 READ titleMenu0 CONSTANT)
		Q_PROPERTY(QString titleMenu1 READ titleMenu1 CONSTANT)
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

		// ---- 随机数（启动时随机种子；可显式固定以便复现）----
		Q_PROPERTY(quint32 randomSeed READ randomSeed NOTIFY randomSeedChanged)
		[[nodiscard]] quint32 randomSeed() const { return m_expressionEvaluator.randomSeed(); }
		// 固定随机种子（调试/回归用）：之后整局的随机序列可复现
		Q_INVOKABLE void setRandomSeed(quint32 seed);
		Q_INVOKABLE void randomizeRandom();
		FileSystem* getFileSystem() { return &m_fileSystem; }
		BinaryIo* getBinaryIo() { return &m_binaryIo; }
		EraTetrisInputSystem* getInputSystem() { return &m_inputSystem; }
		ExecutionEngine* getExecutionEngine() { return &m_executionEngine; }
		ProcessState* getProcessState() { return &m_processState; }
		SystemStateMachine* getSystemStateMachine() { return &m_systemStateMachine; }
		InputHandler* getInputHandler() { return &m_inputHandler; }
    SystemStatusManager* getStatusManager() { return &m_statusManager; }
    EraParseTable* getParseTable() { return &m_parseTable; }
    ScriptRunner* getScriptRunner() { return &m_scriptRunner; }
    ConsoleBackend* getConsole() { return &m_console; }
    GuiManager* getGuiManager() { return &m_guiManager; }
    AudioPipelinePool* getAudio() { return &m_audio; }
    [[nodiscard]] bool hasError() const { return m_hasError; }
    void setHasError(bool on) {
        if (m_hasError == on) return;
        m_hasError = on;
        emit hasErrorChanged();
    }

		// ---- 装载心跳快照（只读；由 ErbLoader::loadHeartbeat 刷新，见 Q_PROPERTY）----
		[[nodiscard]] QString loadStage() const { return m_loadStage; }
		[[nodiscard]] int loadProcessed() const { return m_loadProcessed; }
		[[nodiscard]] int loadTotal() const { return m_loadTotal; }
		[[nodiscard]] qint64 loadElapsedMs() const { return m_loadElapsedMs; }
		[[nodiscard]] bool loadStalled() const { return m_loadStalled; }

    // ---- 标准标题画面（QML 渲染）----
    [[nodiscard]] bool defaultTitleVisible() const { return m_defaultTitleVisible; }
    void setDefaultTitleVisible(bool on) {
        if (m_defaultTitleVisible == on) return;
        m_defaultTitleVisible = on;
        emit defaultTitleVisibleChanged();
    }
    // 标准标题的 [0]/[1] 菜单文案（对齐 C# Config.TitleMenuString0/1）
    [[nodiscard]] QString titleMenu0() const { return QStringLiteral("开始游戏"); }
    [[nodiscard]] QString titleMenu1() const { return QStringLiteral("读取存档"); }
    // QML 标准标题点菜单：交付选择（0/1）并收起标题界面
    Q_INVOKABLE void chooseTitle(int index);

    // ---- 目录解析（对齐 C# Program.ErbDir / Program.CsvDir）----
    // 只在这两个目录内检索脚本与数据，不再遍历整个游戏根目录
    // （否则会把 パッチ/、資料/、各種テンプレート/ 之类的非游戏内容一并装载）。
    [[nodiscard]] QString csvDir() const { return m_csvDir; }
    [[nodiscard]] QString erbDir() const { return m_erbDir; }
    [[nodiscard]] bool searchSubdirectory() const { return m_searchSubdirectory; }
    void setSearchSubdirectory(bool on) { m_searchSubdirectory = on; }

    // ---- 文本编码（跨平台）----
    // 读：Auto = 逐文件嗅探（BOM → UTF-8 → Shift-JIS → Latin-1），引擎内部统一 Unicode/UTF-8；
    // 写：默认 UTF-8 + BOM（Linux/macOS/Windows 都能正确识别）。
    // 这两项可被配置文件里的 `TextEncoding` / `テキストエンコーディング` / `文字コード` 覆盖。
    [[nodiscard]] QString textEncodingName() const { return QString::fromLatin1(TextCodecUtil::name(m_textEncoding)); }
    Q_INVOKABLE void setTextEncoding(const QString& name);      // "AUTO"/"UTF-8"/"SHIFT-JIS"/"UTF-8-BOM"
    [[nodiscard]] QString textWriteEncodingName() const { return QString::fromLatin1(TextCodecUtil::name(m_configLoader.writeEncoding())); }
    Q_INVOKABLE void setTextWriteEncoding(const QString& name);
    // 本机 Qt 支持的编解码器名（无 ICU 时只有 UTF-*/Latin-1/System；可用于设置界面置灰）
    [[nodiscard]] Q_INVOKABLE QStringList availableTextCodecs() const {
        return TextCodecUtil::availableCodecs();
    }
    [[nodiscard]] Q_INVOKABLE bool canUseTextEncoding(const QString& name) const {
        const TextEncoding e = TextCodecUtil::fromName(name);
        return e == TextEncoding::Auto ? false
             : (TextCodecUtil::canDecode(e) && TextCodecUtil::canEncode(e));
    }
    // 把已加载的配置按写编码保存（默认 UTF-8+BOM）；返回写成功文件数
    Q_INVOKABLE int saveConfigFiles();
    // 返回某个已加载配置文件的嗅探编码名（未加载返回 "AUTO"）
    Q_INVOKABLE QString configEncodingOf(const QString& filePath) const;

    // ---- ROM（游戏）编码探测 ----
    // 只读:扫描 ERB/CSV/*.config 采样投票，判断「这个游戏整体是什么编码」。
    // 配置里没有显式指定时，探测结果会作为嗅探的回退编码（见 resolveTextConfig）。
    Q_INVOKABLE QString probeGameEncoding();
    [[nodiscard]] const EncodingProbeResult& probedEncoding() const { return m_probeResult; }
    [[nodiscard]] Q_INVOKABLE QString probedEncodingName() const { return m_probeResult.dominantName(); }
    // 把当前读编码写进 CSV/_fixed.config 的 `TextEncoding`（只改这一个键，保留其它行）
    Q_INVOKABLE bool saveEncodingToConfig();

    // ---- 异步装载（ERB -> AST 后台进行，不阻塞 UI）----
    // 配置/CSV/GameBase 同步装载（小文件），ERB 分块后台解析 + 主线程合并；
    // 完成后入口点收集，并发 scriptsLoaded。
    Q_INVOKABLE void reloadAsync();
    // 设定目录并**异步**装载（不阻塞；QML 首屏用）
    Q_INVOKABLE void loadAsync(const QString& directory);
    [[nodiscard]] Q_INVOKABLE bool isLoadingScripts() const;
    [[nodiscard]] Q_INVOKABLE QStringList parseWarnings() const;
    // CSV 常量名表（变量字符串下标 -> 整数下标）
    [[nodiscard]] const ConstantTable& constantTable() const { return m_constantTable; }

    // 用户输入交付（QML 侧调用）：写入 RESULT 并恢复执行
    Q_INVOKABLE void provideInput(qint64 value);
    // 字符串输入交付（INPUTS）：写入 RESULTS 并恢复执行
    Q_INVOKABLE void provideInputString(const QString& value);
    // 多值输入（INPUTMOUSEKEY）：RESULT:0..4 = 类型 / 坐标 / 按键
    Q_INVOKABLE void provideInputValues(const QVariantList& values);
    Q_INVOKABLE void provideMouseKey(int type, int r1, int r2, int r3, int r4);
    
    // QML 类型注册：走 Qt 6 的声明式方式 —— 各类型在自己的头文件里写
    // QML_ELEMENT / QML_NAMED_ELEMENT / QML_SINGLETON / QML_UNCREATABLE，
    // 由 qt_add_qml_module 扫描 SOURCES 生成注册代码。
    // （此处原来的 registerTypes() 是 Qt 5 风格的命令式注册，且从未被调用 ——
    //  与声明式注册重复；已删除，见 prd.md 的历史设计。）
    
    // Script loading helper (callable from QML)
    Q_INVOKABLE bool loadScript(const QString& scriptPath);
    Q_INVOKABLE void executeScript(const QString& scriptName);
    Q_INVOKABLE void reload();
    // 卸载当前游戏（脚本 QUIT / 用户关闭）：停执行、停音频、清控制台、
    // 释放变量与脚本内存，回到「未装载」状态，可重新 openDirectory。
    Q_INVOKABLE void closeGame();
    // C# Emuera 控制台命令：由 QML 控制台直接调用，不经过 ERB 输入通路。
    Q_INVOKABLE bool executeConsoleCommand(const QString& command);
    Q_INVOKABLE void copyTextToClipboard(const QString& text);
    Q_INVOKABLE bool debugMode() const;
    // ConfigData 的可编辑元数据：QML 只负责控件，配置读写仍由 C++ ConfigLoader 负责。
    Q_INVOKABLE QVariantList configItems() const;
    Q_INVOKABLE QString configValue(const QString& key) const;
    Q_INVOKABLE bool setConfigValue(const QString& key, const QString& value);
    
    // Game base data loading
    void loadGameBaseData();
    Q_INVOKABLE void reloadSavedData();

    // 把「配置窗口 ÷ 单元格像素」同步成控制台逻辑网格列/行数。
    // 居中（offset = 网格列/2 - 行宽/2）、DRAWLINE 铺满宽度都以它为准，
    // 必须与 maxLineUnits 同源。
    void syncConsoleGrid();
    
    // Config loading helper (callable from QML)
    Q_INVOKABLE void loadConfig(const QString& filePath, int precedence = 0);
    Q_INVOKABLE void mergeConfig(const QString& filePath, int precedence = 0);
    Q_INVOKABLE QString getConfig(const QString& key) const;
    Q_INVOKABLE bool hasConfig(const QString& key) const;
    
    // Script processing helper (callable from QML)
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
    void randomSeedChanged();
    // 异步装载：开始 / 进度（已处理, 总数）/ 完成
    void scriptsLoadStarted();
    void scriptsLoadProgress(int processed, int total);
    void scriptsLoaded(bool ok);
    void hasErrorChanged();
    // 装载心跳快照变化（loadStage/loadProcessed/loadTotal/loadElapsedMs/loadStalled
    // 五个属性共用一条通知：它们永远来自同一份心跳快照，分开通知只会让 QML 多刷新几次）
    void loadHeartbeatChanged();
    // 标准标题画面（QML）的显示状态变化
    void defaultTitleVisibleChanged();
    // 脚本执行了 QUIT（C# 侧等价关闭游戏窗口）—— GUI 层应退出应用
    void quitRequested();
    // 脚本执行了 QUIT_AND_RESTART / FORCE_QUIT_AND_RESTART（EE v11；C# 侧等价
    // Program.rebootFlag + 关闭窗口后重开）—— 宿主应重新装载本目录
    // （GUI：reloadAsync()；test_cli：reload() + runSystem()）
    void restartRequested();
    // D-Bus /debug saveScreenshot：请求 QML 把当前画面存成图片
    void screenshotRequested(const QString& path);
    // D-Bus /debug startFrameCapture：每帧抓取渲染（QML 侧 Item.grabToImage）
    void frameCaptureRequested(const QString& prefix, int limit);
    void frameCaptureStopRequested();
    void consoleCommandRequested(const QString& command);

private:
    QString m_gameDirectory;
    QString m_storageRoot;
    QString m_csvDir;     // 解析后的 CSV 目录（绝对路径，实际大小写）
    QString m_erbDir;     // 解析后的 ERB 目录（绝对路径，实际大小写）
    bool    m_searchSubdirectory = true;   // Config.SearchSubdirectory（子目录内检索）
    ConstantTable m_constantTable;         // CSV 常量名表
    DirNode m_csvTree;                     // CSV 目录预扫描路径树（SAF 下多趟遍历是加载耗时大头）
    bool m_csvTreeValid = false;           // m_csvTree 是否对应当前 m_csvDir
    TextEncoding  m_textEncoding = TextEncoding::Auto;   // 读编码（Auto = 逐文件嗅探）
    EncodingProbeResult m_probeResult;                  // ROM 编码探测结果
    GameBaseData m_gameBaseData;
    VariableStorage m_variableStorage;
    ExpressionEvaluator m_expressionEvaluator;
    FileSystem m_fileSystem;
    BinaryIo m_binaryIo;
    EraTetrisInputSystem m_inputSystem;
    ExecutionEngine m_executionEngine;
    ProcessState m_processState;
    SystemStateMachine m_systemStateMachine;
    SystemStatusManager m_statusManager;
    EraParseTable m_parseTable;
    ScriptRunner m_scriptRunner;
    ConsoleBackend m_console;
    GuiManager m_guiManager;
    AudioPipelinePool m_audio;   // 音频播放管线池（扩展登记数量，QML 维护播放）
    
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
    // D-Bus 调试/控制入口（/debug 对象；无会话总线时为空壳）
    std::unique_ptr<EraDBusDebug> m_dbusDebug;

    // 异步装载：连接句柄（避免重复连接）
    QMetaObject::Connection m_loadProgressConn;
    QMetaObject::Connection m_loadHeartbeatConn;
    QMetaObject::Connection m_loadCompletedConn;
    // 后台 CSV 准备（路径树预扫描 + 常量表 + 角色模板）：SAF 上是 ~60s 的
    // provider 往返，放后台线程执行，主线程不被占住（见 reloadAsync）。
    QFutureWatcher<void> m_csvPrepWatcher;
    QMetaObject::Connection m_csvPrepConn;
    qint64 m_loadChainStartMs = 0;   // 本轮装载起点（进度「已耗时」的时间基准）
    bool m_hasError = false;   // 最近一次执行出错（状态灯）
    bool m_defaultTitleVisible = false;   // 标准标题画面（QML）是否显示    // ---- 内部：目录解析与分批装载 ----
    bool resolveGameDirs();          // 解析 CSV/ERB 目录（绝对路径 + 实际大小写）
    [[nodiscard]] const DirNode& csvTree();   // 惰性预扫描 m_csvDir 的路径树（resolveGameDirs 时失效）
    void loadConfigFiles();          // _default.config / emuera.config / _fixed.config
    void loadConstantData();         // CSV 目录内的常量数据（VariableSize 等）
    void collectEntryPoints();       // 从已装载 AST 收集入口点（不再二次扫文件）
    void resolveTextConfig();        // 从配置读编码/子目录检索设置（Config.SearchSubdirectory 等）
    void loadFinishedHook();         // 装载完成后的统一收尾（日志/告警）
    void buildSystemHost();          // 系统状态机 -> 引擎子系统 适配层
    void clearLoadedGame();           // 重载边界：释放旧 AST/拍平/变量/常量
    void stopAsyncLoad();             // 取消并等待旧加载会话，随后才能清理解析表
    void startErbLoadAsync(quint64 generation);   // CSV 准备（后台）完成后的 ERB 装载启动段

    // ---- 装载心跳（QML 装载遮罩/状态灯的数据源，见 Q_PROPERTY loadStage 等）----
    void applyLoadHeartbeat(int stage, const QString& name, int done, int total,
                            qint64 elapsedMs, bool stalled);
    void resetLoadHeartbeat();        // 装载开始/结束复位快照（不留上一局文案）
    void setLoadStageText(const QString& name);   // 收尾阶段手动置文案（心跳已停）

    bool m_completingLoad = false;    // 收尾窗口（已弃用后台语义线程，仅保留取消语义）
    quint64 m_loadGeneration = 0;     // 丢弃过期的 loader/完成回调
    QString m_loadStage;              // 心跳快照：粗阶段名
    int m_loadProcessed = 0;          // 心跳快照：已处理（口径随阶段单调）
    int m_loadTotal = 0;              // 心跳快照：总数（0 = 未知）
    qint64 m_loadElapsedMs = 0;       // 心跳快照：本轮装载已耗时
    bool m_loadStalled = false;       // 心跳快照：阶段/进度停滞
};

#endif // ERAENGINE_H
