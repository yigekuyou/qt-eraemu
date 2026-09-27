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
#include <QVariantList>
#include <QQmlEngine>
#include <QQmlContext>
#include "csv_loader.h"
#include "constant_table.h"
#include "encoding_probe.h"
#include "variable_storage.h"
#include "expression_evaluator.h"
#include "function_system.h"
#include "rendering_system.h"
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
#include "signal_hub.h"
#include "era_parse_table.h"
#include "script_runner.h"
#include "console_backend.h"
#include "gui_manager.h"
#include "resource_image_provider.h"

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
		Q_PROPERTY(ConsoleBackend* console READ getConsole CONSTANT)
		Q_PROPERTY(GuiManager* gui READ getGuiManager CONSTANT)
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
		SystemStateMachine* getSystemStateMachine() { return &m_systemStateMachine; }
		InputHandler* getInputHandler() { return &m_inputHandler; }
    SystemStatusManager* getStatusManager() { return &m_statusManager; }
    SignalManager* getSignalManager() { return &m_signalManager; }
    EraParseTable* getParseTable() { return &m_parseTable; }
    ScriptRunner* getScriptRunner() { return &m_scriptRunner; }
    ConsoleBackend* getConsole() { return &m_console; }
    GuiManager* getGuiManager() { return &m_guiManager; }

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
    // 完成后 finalizeParse + 入口点收集，并发 scriptsLoaded。
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
    // 异步装载：开始 / 进度（已处理, 总数）/ 完成
    void scriptsLoadStarted();
    void scriptsLoadProgress(int processed, int total);
    void scriptsLoaded(bool ok);
    
private:
    QString m_gameDirectory;
    QString m_csvDir;     // 解析后的 CSV 目录（绝对路径，实际大小写）
    QString m_erbDir;     // 解析后的 ERB 目录（绝对路径，实际大小写）
    bool    m_searchSubdirectory = true;   // Config.SearchSubdirectory（子目录内检索）
    ConstantTable m_constantTable;         // CSV 常量名表
    TextEncoding  m_textEncoding = TextEncoding::Auto;   // 读编码（Auto = 逐文件嗅探）
    EncodingProbeResult m_probeResult;                  // ROM 编码探测结果
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
    SystemStateMachine m_systemStateMachine;
    SystemStatusManager m_statusManager;
    SignalManager m_signalManager;
    EraParseTable m_parseTable;
    ScriptRunner m_scriptRunner;
    ConsoleBackend m_console;
    GuiManager m_guiManager;
    
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

    // 异步装载：连接句柄（避免重复连接）
    QMetaObject::Connection m_loadProgressConn;
    QMetaObject::Connection m_loadCompletedConn;

    // ---- 内部：目录解析与分批装载 ----
    void resolveGameDirs();          // 解析 CSV/ERB 目录（绝对路径 + 实际大小写）
    void loadConfigFiles();          // _default.config / emuera.config / _fixed.config
    void loadConstantData();         // CSV 目录内的常量数据（VariableSize 等）
    void collectEntryPoints();       // 从已装载 AST 收集入口点（不再二次扫文件）
    void resolveTextConfig();        // 从配置读编码/子目录检索设置（Config.SearchSubdirectory 等）
    void loadFinishedHook();         // 装载完成后的统一收尾（日志/告警）
    void buildSystemHost();          // 系统状态机 -> 引擎子系统 适配层
};

#endif // ERAENGINE_H
