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
    // PRINTBUTTON：打印一段文本并把它变成按钮（值可为整数或字符串）
    void consolePrintButton(const QString& text, qint64 intValue, const QString& strValue, bool isString);
    void consoleClearLines(int count);
    void consoleAlign(const QString& align);
    void consoleColor(const QString& colorName);
    void consoleResetColor();
    void consoleRedraw(const QString& mode);

public:
    // Execute a single instruction (public for testing)
    bool executeInstruction(const LogicalLine& line);
    
private:
    
    // Handle basic instructions
    bool handlePrint(const QList<Operand>& args, bool newline);
    // 格式化串输出（PRINTFORM* → StrForm 求值）
    bool handlePrintForm(const QList<Operand>& args, bool newline);
    bool handleResetData();
    bool handleLoadGlobal();
    
    // Assignment handling
    bool handleAssignment(const QString& lhs, const QString& rhs);
    // 字符串赋值（目的变量是字符串变量时）：右侧按字符串求值后写入字符串容器
    bool handleStringAssignment(const QString& lhs, const QString& rhs);
    bool handleCompoundAssignment(const QString& lhs, const QString& op, const QString& rhs);
    
    // Parse LHS (left-hand side) of assignment
    QPair<QString, int> parseLHS(const QString& lhs);
    
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
