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
#ifndef EXECUTION_ENGINE_H
#define EXECUTION_ENGINE_H

#include <QObject>
#include <QSet>
#include <QString>
#include <QList>
#include <QQueue>
#include <QHash>
#include <functional>
#include <memory>
#include "variable_storage.h"
#include "ast/logical_line.h"
#include "erb_loader.h"
#include "extension_registry.h"
#include "function_system.h"
#include "process_state.h"
#include "game_base_data.h"

// Forward declaration
class EraParseTable;
class ExpressionEvaluator;

// Execution engine - main game loop and instruction execution
class ExecutionEngine : public QObject {
    Q_OBJECT

public:
    explicit ExecutionEngine(VariableStorage* storage, GameBaseData* gameBaseData = nullptr, QObject* parent = nullptr);
    ~ExecutionEngine();
    
    // Set ParseTable reference for CALL/RETURN integration
    void setParseTable(EraParseTable* parseTable);

    // 共享表达式求值器（携带用户自定义函数回调）
    void setExpressionEvaluator(ExpressionEvaluator* evaluator) { m_expressionEvaluator = evaluator; }
    
    // Start execution
    bool executeScript(const QString& scriptName);
    void executeLogicalLine(const LogicalLine& line);

    // Get execution state
    bool isRunning() const;
    int getCurrentLine() const;
    QString getCurrentScript() const;
    int getTotalInstructionsExecuted() const;

    // Load scripts
    bool loadScripts(const QString& scriptDir);
    
    // Get loaded scripts
    QHash<QString, QList<LogicalLine>> getLoadedScripts() const;
    
    // GameBase.csv 数据（GAMEBASE_TITLE 等）；求值器需要它
    [[nodiscard]] GameBaseData* gameBaseData() const { return m_gameBaseData; }

    // PRINTC / PRINTLC 的定宽（C# Config.PrintCLength，默认 25）
    void setPrintCLength(int n) { if (n > 0) m_printCLength = n; }
    [[nodiscard]] int printCLength() const { return m_printCLength; }

    // SAVEGLOBAL / LOADGLOBAL 的落盘目录（游戏根目录；由 EraEngine 接线）
    void setGameDataDir(const QString& dir) { m_gameDataDir = dir; }
    [[nodiscard]] QString gameDataDir() const { return m_gameDataDir; }

    // DRAWLINE 用字符（C# Config.DrawLineString，键「DRAWLINE文字」，默认 "-"）
    void setDrawLineString(const QString& s) { if (!s.isEmpty()) m_drawLineString = s; }
    [[nodiscard]] QString drawLineString() const { return m_drawLineString; }
    // 一行最多几个「半角单位」（C# Config.DrawableWidth / 列宽）
    void setMaxLineUnits(int n) { if (n > 0) m_maxLineUnits = n; }
    [[nodiscard]] int maxLineUnits() const { return m_maxLineUnits; }
    // 当前文字颜色 / 字体样式（GETCOLOR / GETSTYLE）
    [[nodiscard]] qint64 currentColorValue() const { return m_colorValue; }
    [[nodiscard]] qint64 currentStyleBits() const { return m_styleBits; }
    // 打印系 W 后缀（PRINTW/PRINTFORMW/PRINTDATAW…）的等待标记：
    // requestAnyKey 信号是纯通知，挂起由 runner 在指令执行后经本方法消费
    bool consumePrintWaitKey() { const bool v = m_printWaitKey; m_printWaitKey = false; return v; }
    // CURRENTALIGN / GETFONT 的状态源（ALIGNMENT / SETFONT 语句维护）
    [[nodiscard]] bool skipDisp() const { return m_skipDisp; }
    // ---- EE SKIPLOG / MESSKIP（MesSkip）------------------------------------
    // SKIPLOG <n> 直接把「消息跳过中」状态置为 (n != 0)（C# Process.ScriptProc.cs:783
    // 的 console.MesSkip = (iValue != 0)）。MESSKIP() 打印时反映本值；WAIT/
    // WAITANYKEY（可跳过的任意键等待）在跳过中自动放行，而 INPUT 族（需要输入值）
    // 与 FORCEWAIT（不可跳过）会把跳过状态清掉 —— 对齐 C# EmueraConsole 的
    // `while (MesSkip && state == WaitInput) { if (inputReq.NeedValue) break;
    //  if (inputReq.StopMesskip) break; RunEmueraProgram(""); }` 后 MesSkip = false。
    void setMesSkip(bool on) { m_mesSkip = on; }
    [[nodiscard]] bool mesSkip() const { return m_mesSkip; }
    // 默认文字色的惰性读取（配置在 setGameDirectory 之后才可用）
    void setDefaultColorProvider(std::function<qint64()> provider) {
        m_defaultColorProvider = std::move(provider);
    }
    [[nodiscard]] qint64 defaultColorValue() const {
        return m_defaultColorProvider ? m_defaultColorProvider() : 0xFFFFFF;
    }
    [[nodiscard]] qint64 currentAlign() const { return m_currentAlign; }
    [[nodiscard]] QString currentFontName() const { return m_fontName; }
    // 文本占几个半角单位（全角 2 / 半角 1）
    [[nodiscard]] static int unitWidth(const QString& text);

    // Get ErbLoader for signal connections
    ErbLoader& getErbLoader() { return m_erbLoader; }
    const ErbLoader& getErbLoader() const { return m_erbLoader; }
    
signals:
    void executionStarted(const QString& scriptName);
    void lineExecuted(int line);
    void executionFinished();
    void errorOccurred(const QString& message);

    // ---- 显示输出（由 EraEngine 接到 ConsoleBackend）----
    void consolePrint(const QString& text, bool newline);
    // REUSELASTLINE（C# PrintTemporaryLine）：单行输出并标记「一時行」
    void consoleReuseLastLine(const QString& text);
    void consolePrintTemplate(const PrintTemplate& output);    // PRINT_IMG：把资源名作为行内图片输出（C# Console.PrintImg）
    void consolePrintImage(const QString& resourceName, int width, int height, int ypos);
    // PRINTW 的「换行后等任意键」（对齐 C# PRINT_WAITINPUT -> Console.ReadAnyKey）
    void requestAnyKey();
    // PRINTBUTTON：打印一段文本并把它变成按钮（值可为整数或字符串）
    void consolePrintButton(const QString& text, qint64 intValue, const QString& strValue, bool isString);
    void consoleClearLines(int count);
    void consoleAlign(const QString& align);
    void consoleColor(const QString& colorName);
    // CLEARTEXTBOX：清空 QML 侧输入栏（C# Console.ClearTextBox）
    void clearTextBox();
    void consoleFontStyle(bool bold, bool italic, bool underline, bool strike);
    void consoleResetColor();
    // SETBGCOLOR / SETBGCOLORBYNAME / RESETBGCOLOR：文字**背景色**（C# SETBGCOLOR_Instruction）
    void consoleBgColor(const QString& colorName);
    void consoleResetBgColor();
    void consoleRedraw(const QString& mode);
    // PRINT_RECT / PRINT_SPACE：行内图形（C# Console.PrintShape）
    void consolePrintShape(const QString& type, const QList<int>& params);
    // OUTPUTLOG：把显示行日志写进 emuera.log（C# Console.OutputLog(null)）
    void consoleOutputLog();

public:
    // Execute a single instruction (public for testing)
    bool executeInstruction(const LogicalLine& line);

    // RESETDATA（对齐 C# VariableEvaluator.ResetData）：变量回默认 + 角色清空。
    // 供 RESETDATA 指令与系统层 resetData（新开游戏）、QUIT 卸载共用。
    bool handleResetData();

    // ---- 扩展注册类（扩展函数唯一入口；复杂度由注册类承担）----
    // 注册 API 在 ExtensionRegistry（不再挂引擎）：
    //   · reg(name) / reg(name, 实现)（C++ 重载：参数不同 -> 不同重载）/
    //     regForm(name)（实参形态 = StrForm，注册类插入 AST）/
    //     regExpr(name, ret, minArgs, maxArgs, 实现)（式中函数）；
    //   · 扩展只调注册类的函数就能实现扩展函数（EE 扩展 = ee_extension.h
    //     单独一个头文件，只在注册类里被实现 —— 其他位置不得放置 EE 头文件，
    //     注册类构造时一次登记，默认全启用）；
    //   · fail-fast（核心名拒绝注册）/ first-wins（同名重复拒绝）/
    //     统一「留痕跳过」桩 —— 全部由注册类内部承担；
    //   · 分发优先级（固定，由结构决定）：① 核心专用分支（本类内联）->
    //     ② 扩展注册类查表 -> ③ 通用路径（表达式求值 -> kBuiltinFunctions 表）。

    // 扩展注册类实例（EraEngine 装配期用它注入「式中函数」服务 + 挂求值回调；
    // 分发时查表）。
    [[nodiscard]] ExtensionRegistry& extensions() { return m_extensions; }

private:
    // 扩展注册类实例（构造时由注册类装入 EE 扩展 + SPLIT 实现；分发时查表）
    ExtensionRegistry m_extensions;
    // SPLIT：核心函数专用分支（对齐 C# FunctionCode.SPLIT / SpSplitArgument）
    bool handleSplit(const LogicalLine& line);
    // 存档系（C# 原版全量）：SAVEDATA/LOADDATA/DELDATA/CHKDATA 的实现
    void handleSaveData(const LogicalLine& line);
    void handleLoadData(const LogicalLine& line);
    void handleDelData(const LogicalLine& line);
    void handleChkData(const LogicalLine& line);
    // 状态打印 / 角色整理 / 变量存档族（此前未实现、运行期被忽略的指令集合）：
    //   UPCHECK / PRINT_ABL / PRINT_TALENT / PRINT_MARK / PRINT_EXP / PRINT_PALAM /
    //   PRINT_ITEM / PRINT_SHOPITEM / HTML_TAGSPLIT / SORTCHARA / SAVEVAR / LOADVAR
    // 语义对齐 C# Process.ScriptProc.cs、Instraction.Child.cs 与 VariableEvaluator。
    bool handleStatusCommand(const LogicalLine& line);
    // SAVEVAR / LOADVAR（EE 扩展；C# 原版注册了但抛 NotImpl）：
    // 把指定全局变量的整组元素写进/读回 JSON 文本（sav/ 目录）。
    bool handleSaveVarCommand(const LogicalLine& line);

private:
    
    // PRINT 族统一出口（对齐 C# PRINT_Instruction）：形态由指令名后缀决定
    bool handlePrintInstruction(const LogicalLine& line);
    // PRINTC / PRINTLC 的定宽列补齐（对齐 C# CreateTypeCString）
    [[nodiscard]] QString padPrintC(const QString& text, bool padLeft) const;
    [[nodiscard]] static int printCWidth(const QString& text);
    // LOADGLOBAL / SAVEGLOBAL（对齐 C# VEvaluator.LoadGlobal / SaveGlobal）：
    // GLOBAL / GLOBALS 系统数组 + `#DIM SAVEDATA GLOBAL` 用户变量 -> save_global.dat。
    // LOADGLOBAL 成功置 RESULT=1，文件缺失 / 校验失败置 RESULT=0。
    bool handleLoadGlobal();
    bool handleSaveGlobal();
    // 游戏唯一码（对齐 C# gamebase.ScriptUniqueCode，由标题/版本派生）
    [[nodiscard]] qint64 globalUniqueCode() const;

    // 语句形式的内部函数（对齐 C# LogicalLineType.Function / METHOD_Instruction）：
    // 整行是一次内建函数调用，返回值写 RESULT（整型）/ RESULTS:0（字符串）。
    bool executeFunctionCall(const LogicalLine& line);
    // 未实现接口的运行期留痕（输出含「未完成」，同一名字只报一次）
    void reportUnfinished(const QString& what, const QString& name, const LogicalLine& line);
    QSet<QString> m_reportedUnfinished;
    
    // Assignment handling
    bool handleAssignment(const QString& lhs, const QString& rhs, const QSharedPointer<ExpressionNode>& ast = {});
    // 字符串赋值（目的变量是字符串变量时）：右侧按字符串求值后写入字符串容器
    bool handleStringAssignment(const QString& lhs, const QString& rhs,
                                const QSharedPointer<ExpressionNode>& ast = {},
                                const QString& ownerFunction = QString());
    // 已求值字符串写入左值（SPLIT 等复用；不做表达式求值）
    // ownerFunction：左值所属函数（解析**本函数私有**的 #DIMS 声明维数要用；
    //   广域/全局声明可省）。
    bool writeStringValue(const QString& lhs, const QString& value,
                          const QString& ownerFunction = QString());
    bool handleCompoundAssignment(const QString& lhs, const QString& op, const QString& rhs, const QSharedPointer<ExpressionNode>& ast = {});

    // VARSET 族（对齐 C# VARSET_Instruction / CVARSET_Instruction）
    //   eachChara == false: SET / VARSET / SETS / VAR_SET  —— 一个变量的元素区间赋值
    //   eachChara == true : CVARSET                        —— 逐个角色设置同一元素
    bool handleVarSet(const LogicalLine& line, bool eachChara);
    // 目的变量是否字符串型（决定右值按 Str 还是 Int 求值）
    [[nodiscard]] bool isStringVariable(const QString& name, const QString& function) const;
    // #DIM/#DIMS 声明的维数长度（未声明返回空）
    [[nodiscard]] QList<int> declaredLengths(const QString& name, const QString& function) const;
    // VARSET 里 1 次元变量的元素个数（对齐 C# VariableTerm.GetLength）
    [[nodiscard]] int variableLength1D(const QString& name, const QString& function) const;
    
    // Parse LHS (left-hand side) of assignment
    // 兼容旧接口：返回 (名字, 第一个下标)（无下标时下标为 -1）
    QPair<QString, int> parseLHS(const QString& lhs);

public:
    // LHS 引用（支持 2D/3D 下标：`A:i:j` / `A:i:j:k` / `BAG:COUNT` / `BAG:(COUNT+1)`）
    struct LhsRef {
        QString    name;
        QList<int> indices;
        bool       valid = false;
        [[nodiscard]] bool hasIndex() const { return !indices.isEmpty(); }
        [[nodiscard]] int  first() const { return indices.isEmpty() ? 0 : indices.first(); }
    };

private:
    [[nodiscard]] LhsRef parseLhsRef(const QString& lhs);
    // 依据声明维度写入（1D/2D/3D）
    void writeLhs(const LhsRef& ref, qint64 value);
    [[nodiscard]] qint64 readLhs(const LhsRef& ref);
    [[nodiscard]] int lhsDimension(const QString& name) const;
    
    // Helper methods
    void setError(const QString& message);
    
    // Function system for execution
    FunctionSystem* m_functionSystem;
    
    VariableStorage* m_storage;
    GameBaseData* m_gameBaseData;
    ErbLoader m_erbLoader;
    ProcessState m_state;
    // 游戏目录（SAVEDATA/LOADDATA/DELDATA 的存档目录由它决定；EraEngine::setGameDirectory 注入）
    QString m_gameDirectory;
public:
    // ---- PRINTDATA 段（C# PRINT_DATA_Instruction / ErbLoader 的 dataList）----
    // 打印一条 DATAFORM/DATA 行（求值 StrForm，**不换行**）；段内换行 / 段后换行 /
    // 等键由 ScriptRunner 按指令后缀（…L / …W）驱动。
    void printDataFormLine(const LogicalLine& line);
    // 同上的「只求值不显示」版本：STRDATA 需要段文本本身（C# STRDATA 取所选段的字符串）
    [[nodiscard]] QString printDataFormText(const LogicalLine& line);
    void printDataNewline();
    void requestPrintDataWaitKey();
    // PRINTDATA 的可选整型变量实参（如 `PRINTDATAW LOCAL:0`）：写入被选中的段下标。
    void assignPrintDataIndex(const QString& lhsText, qint64 value);

    // STRDATA <字符串变量>：与 PRINTDATA 同段结构，但**不显示**，把被选中段的
    // 文本写进这个变量（C# STRDATA = PRINTDATA 的不显示版）。
    void assignPrintDataString(const QString& lhsText, const QString& value);

    void setGameDirectory(const QString& dir) { m_gameDirectory = dir; }
    QString m_gameDataDir;   // SAVEGLOBAL / LOADGLOBAL 的落盘目录
    
    // ParseTable reference for CALL/RETURN integration
    EraParseTable* m_parseTable;
    ExpressionEvaluator* m_expressionEvaluator = nullptr;

    // perf：求值器统一入口。热路径（每条指令）上不再无条件构造 fallback
    // ExpressionEvaluator（QObject + Mt19937 构造不便宜，eraTW 地图逐字符
    // SELECTCASE 时一帧上万次）—— m_expressionEvaluator 为空才惰性创建。
    ExpressionEvaluator& getEvaluator();
    std::unique_ptr<ExpressionEvaluator> m_fallbackEvaluator;

    // perf：EMUERA_QDBUG_TRACE 只在构造时查一次（此前每次「其它指令」都
    // qEnvironmentVariableIsSet -> getenv）。
    bool m_qdbugTrace = qEnvironmentVariableIsSet("EMUERA_QDBUG_TRACE");
    
    bool m_running;
    int m_printCLength = 25;      // PRINTC 一列的文字宽度（C# Config.PrintCLength）
    // SKIPDISP <n>：置位后所有 PRINT 输出被跳过（C# Process.SkipPrint）
    bool m_skipDisp = false;
    // SKIPLOG <n>：消息跳过状态（C# Console.MesSkip；见 mesSkip()）
    bool m_mesSkip = false;
    // 默认文字色（C# Config.ForeColor，RESETCOLOR 还原到此；由 EraEngine 依配置注入）
    std::function<qint64()> m_defaultColorProvider;
    qint64 m_currentAlign = 0;    // 0=LEFT 1=CENTER 2=RIGHT（CURRENTALIGN）
    QString m_fontName;           // 当前字体（SETFONT；空 = 默认）
    QString m_drawLineString = QStringLiteral("-");   // C# Config.DrawLineString
    int m_maxLineUnits = 84;      // 一行最多单位数（760px / 9px）
    // 当前文字颜色 / 字体样式（GETCOLOR / GETSTYLE 的返回值来源）
    static constexpr qint64 kDefaultColor = 0xFFFFFF;
    qint64 m_colorValue = kDefaultColor;
    // 文字背景色（SETBGCOLOR）。-1 = 未设置/已 RESETBGCOLOR（跟随主题默认）
    qint64 m_bgColorValue = -1;
    qint64 m_styleBits = 0;       // 1=粗体 2=斜体 4=删除线 8=下划线
    static qint64 colorValueOf(const QString& name);   // 颜色名/常量 -> 0xRRGGBB
    // 打印系 W 后缀的等待标记（打印 + 等任意键；runner 消费后挂起）
    bool m_printWaitKey = false;
    int m_currentLine;
    QString m_currentScript;
    
    // Current execution position (0-indexed line number)
    int m_executionPosition;
    
    // Total instructions executed counter
    int m_totalInstructionsExecuted;
    
public:
    // Setters
    void setCurrentScript(const QString& script) { m_currentScript = script; }
    void setExecutionPosition(int pos) { m_executionPosition = pos; }
    
};

#endif // EXECUTION_ENGINE_H
