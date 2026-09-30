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

    // DRAWLINE 用字符（C# Config.DrawLineString，键「DRAWLINE文字」，默认 "-"）
    void setDrawLineString(const QString& s) { if (!s.isEmpty()) m_drawLineString = s; }
    [[nodiscard]] QString drawLineString() const { return m_drawLineString; }
    // 一行最多几个「半角单位」（C# Config.DrawableWidth / 列宽）
    void setMaxLineUnits(int n) { if (n > 0) m_maxLineUnits = n; }
    [[nodiscard]] int maxLineUnits() const { return m_maxLineUnits; }
    // 当前文字颜色 / 字体样式（GETCOLOR / GETSTYLE）
    [[nodiscard]] qint64 currentColorValue() const { return m_colorValue; }
    [[nodiscard]] qint64 currentStyleBits() const { return m_styleBits; }
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

public:
    // Execute a single instruction (public for testing)
    bool executeInstruction(const LogicalLine& line);
    
private:
    
    // PRINT 族统一出口（对齐 C# PRINT_Instruction）：形态由指令名后缀决定
    bool handlePrintInstruction(const LogicalLine& line);
    // PRINTC / PRINTLC 的定宽列补齐（对齐 C# CreateTypeCString）
    [[nodiscard]] QString padPrintC(const QString& text, bool padLeft) const;
    [[nodiscard]] static int printCWidth(const QString& text);
    bool handleResetData();
    bool handleLoadGlobal();

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
    
    // ParseTable reference for CALL/RETURN integration
    EraParseTable* m_parseTable;
    ExpressionEvaluator* m_expressionEvaluator = nullptr;
    
    bool m_running;
    int m_printCLength = 25;      // PRINTC 一列的文字宽度（C# Config.PrintCLength）
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
