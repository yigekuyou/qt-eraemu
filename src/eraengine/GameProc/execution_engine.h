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
#include "variable_storage.h"
#include "ast/logical_line.h"
#include "erb_loader.h"
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
    // CURRENTALIGN / GETFONT 的状态源（ALIGNMENT / SETFONT 语句维护）
    [[nodiscard]] bool skipDisp() const { return m_skipDisp; }
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
    void consolePrintTemplate(const PrintTemplate& output);
    // PRINT_IMG：把资源名作为行内图片输出（C# Console.PrintImg）
    void consolePrintImage(const QString& resourceName, int width, int height, int ypos);
    // PRINTW 的「换行后等任意键」（对齐 C# PRINT_WAITINPUT -> Console.ReadAnyKey）
    void requestAnyKey();
    // PRINTBUTTON：打印一段文本并把它变成按钮（值可为整数或字符串）
    void consolePrintButton(const QString& text, qint64 intValue, const QString& strValue, bool isString);
    void consoleClearLines(int count);
    void consoleAlign(const QString& align);
    void consoleColor(const QString& colorName);
    void consoleFontStyle(bool bold, bool italic, bool underline, bool strike);
    void consoleResetColor();
    void consoleRedraw(const QString& mode);
    // PRINT_RECT / PRINT_SPACE：行内图形（C# Console.PrintShape）
    void consolePrintShape(const QString& type, const QList<int>& params);
    // OUTPUTLOG：把显示行日志写进 emuera.log（C# Console.OutputLog(null)）
    void consoleOutputLog();

public:
    // Execute a single instruction (public for testing)
    bool executeInstruction(const LogicalLine& line);

    // ---- 语句型函数注册表（扩展函数专用，高扩展接口）----
    // 设计规则：只有 **C# 原型没有的函数**（eraTW 依赖的 EmueraEE/EM 扩展系
    // 内建语句）才进注册表；C# 原型已有的函数（SPLIT/REPLACE/VARSET 等）
    // 一律走核心引擎分支。注册表把「名字 -> 执行器」集中管理，
    // 新增扩展函数只需 registerStatementFunction() 一行 + 一个 lambda。
    using StatementFn = std::function<bool(const LogicalLine& line, const QList<Operand>& args)>;
    void registerStatementFunction(const QString& name, StatementFn fn);

private:
    // 一次性建表（构造函数里调用；扩展语句在此登记）
    void buildStatementFunctions();
    QHash<QString, StatementFn> m_statementFunctions;
    // SPLIT：核心函数专用分支（对齐 C# FunctionCode.SPLIT / SpSplitArgument）
    bool handleSplit(const LogicalLine& line);
    // 存档系（C# 原版全量）：SAVEDATA/LOADDATA/DELDATA/CHKDATA 的实现
    void handleSaveData(const LogicalLine& line);
    void handleLoadData(const LogicalLine& line);
    void handleDelData(const LogicalLine& line);
    void handleChkData(const LogicalLine& line);

private:
    
    // PRINT 族统一出口（对齐 C# PRINT_Instruction）：形态由指令名后缀决定
    bool handlePrintInstruction(const LogicalLine& line);
    // PRINTC / PRINTLC 的定宽列补齐（对齐 C# CreateTypeCString）
    [[nodiscard]] QString padPrintC(const QString& text, bool padLeft) const;
    [[nodiscard]] static int printCWidth(const QString& text);
    bool handleResetData();
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
    bool handleStringAssignment(const QString& lhs, const QString& rhs, const QSharedPointer<ExpressionNode>& ast = {});
    // 已求值字符串写入左值（SPLIT 等复用；不做表达式求值）
    bool writeStringValue(const QString& lhs, const QString& value);
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
    void setGameDirectory(const QString& dir) { m_gameDirectory = dir; }
    QString m_gameDataDir;   // SAVEGLOBAL / LOADGLOBAL 的落盘目录
    
    // ParseTable reference for CALL/RETURN integration
    EraParseTable* m_parseTable;
    ExpressionEvaluator* m_expressionEvaluator = nullptr;
    
    bool m_running;
    int m_printCLength = 25;      // PRINTC 一列的文字宽度（C# Config.PrintCLength）
    // SKIPDISP <n>：置位后所有 PRINT 输出被跳过（C# Process.SkipPrint）
    bool m_skipDisp = false;
    // 默认文字色（C# Config.ForeColor，RESETCOLOR 还原到此；由 EraEngine 依配置注入）
    std::function<qint64()> m_defaultColorProvider;
    qint64 m_currentAlign = 0;    // 0=LEFT 1=CENTER 2=RIGHT（CURRENTALIGN）
    QString m_fontName;           // 当前字体（SETFONT；空 = 默认）
    QString m_drawLineString = QStringLiteral("-");   // C# Config.DrawLineString
    int m_maxLineUnits = 84;      // 一行最多单位数（760px / 9px）
    // 当前文字颜色 / 字体样式（GETCOLOR / GETSTYLE 的返回值来源）
    static constexpr qint64 kDefaultColor = 0xFFFFFF;
    qint64 m_colorValue = kDefaultColor;
    qint64 m_styleBits = 0;       // 1=粗体 2=斜体 4=删除线 8=下划线
    static qint64 colorValueOf(const QString& name);   // 颜色名/常量 -> 0xRRGGBB
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
