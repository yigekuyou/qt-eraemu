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
    
    // Control flow
    bool handleGoto(const QString& label);
    bool handleCall(const QString& label);
    bool handleReturn();
    bool handleIf(const QString& condition, const QString& thenLabel);
    
    // Get execution state
    bool isRunning() const;
    int getCurrentLine() const;
    QString getCurrentScript() const;
    int getExecutionQueueSize() const;
    int getTotalInstructionsExecuted() const;
    
    // Stop execution (for timeout control)
    void stopExecution();
    
    // Load scripts
    bool loadScripts(const QString& scriptDir);
    
    // Get loaded scripts
    QHash<QString, QList<LogicalLine>> getLoadedScripts() const;
    
    // Get ErbLoader for signal connections
    ErbLoader& getErbLoader() { return m_erbLoader; }
    const ErbLoader& getErbLoader() const { return m_erbLoader; }
    
signals:
    void executionStarted(const QString& scriptName);
    void lineExecuted(int line);
    void executionFinished();
    void errorOccurred(const QString& message);
    void functionResult(const QString& name, const QVariant& result, const QString& error);
    
    // Instruction executed signal - emitted after each instruction is executed
    // Used to request the next instruction from ParseTable
    void instructionExecuted(const QString& scriptName, int lineNumber, bool success);
    
    // BEGIN instruction signal - emitted when BEGIN is encountered
    void beginRequested(const QString& keyword);

    // ---- 显示输出（由 EraEngine 接到 ConsoleBackend）----
    void consolePrint(const QString& text, bool newline);
    void consoleClearLines(int count);
    void consoleAlign(const QString& align);
    void consoleColor(const QString& colorName);
    void consoleResetColor();
    void consoleRedraw(const QString& mode);
    
public slots:
    // Receive instruction from ParseTable
    void receiveInstruction(const LogicalLine& line);
    
    // Handle jump request from ParseTable
    void handleJumpRequest(const QString& targetScript, const QString& label, int targetPosition);
    
    // Handle memory space change from ParseTable
    void handleMemorySpaceChange(const QString& scriptName);
    
    // Function execution (public for EraEngine to call)
    bool handleFunctionCall(const QString& name, const QList<QString>& args);
    
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
    bool handleCompoundAssignment(const QString& lhs, const QString& op, const QString& rhs);
    
    // Parse LHS (left-hand side) of assignment
    QPair<QString, int> parseLHS(const QString& lhs);
    
    // Loop control (private, called by executeInstruction)
    bool handleFor(const QString& varName, qint64 start, qint64 end);
    bool handleNext(const QString& varName);
    bool handleLoop(bool condition);
    bool handleWhile(const QString& condition);
    bool handleWend();
    
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
    
    // Execution queue
    QQueue<LogicalLine> m_executionQueue;
    
    // Label lookup helper

    // Label position lookup
    int getLabelPosition(const QString& label);
    
    // Restart execution from a specific line
    void restartFromLine(int linePosition);
    
    // Label position map for current script
    QHash<QString, int> m_labelPositions;
    
    // Current execution position (0-indexed line number)
    int m_executionPosition;
    
    // Total instructions executed counter
    int m_totalInstructionsExecuted;
    
    // REPEAT loop state management
    // REPEAT count - stores the original count and remaining iterations
    struct RepeatLoopState {
        int originalCount;
        int remainingCount;
        int loopLine;  // Line number where REPEAT is defined
        int bodyStartLine;  // Line number where loop body starts (REPEAT + 1)
        
        // Required for QList::indexOf
        bool operator==(const RepeatLoopState& other) const {
            return originalCount == other.originalCount &&
                   remainingCount == other.remainingCount &&
                   loopLine == other.loopLine &&
                   bodyStartLine == other.bodyStartLine;
        }
    };
    
    // FOR loop state management
    // FOR LOCAL, start, end - stores the loop variable, start, end, and current value
    struct ForLoopState {
        QString variableName;
        qint64 startValue;
        qint64 endValue;
        qint64 currentValue;
        int loopLine;  // Line number where FOR is defined
        
        // Required for QList::indexOf
        bool operator==(const ForLoopState& other) const {
            return variableName == other.variableName &&
                   startValue == other.startValue &&
                   endValue == other.endValue &&
                   currentValue == other.currentValue &&
                   loopLine == other.loopLine;
        }
    };
    
    // WHILE loop state management
    // WHILE condition - stores the condition and loop line for re-evaluation
    struct WhileLoopState {
        int loopLine;  // Line number where WHILE is defined
        QString condition;  // Condition string (for re-evaluation)
        bool entered;  // Has this loop been entered at least once?
        
        // Required for QList::indexOf
        bool operator==(const WhileLoopState& other) const {
            return loopLine == other.loopLine &&
                   condition == other.condition &&
                   entered == other.entered;
        }
    };
    
    // Stack of active REPEAT loops
    QList<RepeatLoopState> m_repeatLoopStack;
    
    // Stack of active FOR loops
    QList<ForLoopState> m_forLoopStack;
    
    // Stack of active WHILE loops
    QList<WhileLoopState> m_whileLoopStack;
    
public:
    // Flag to indicate a jump occurred (for test compatibility)
    bool m_jumpOccurred;
    
    // Track the last WHILE position for re-entry detection
    int m_lastWhileLine;
    
    // Get/set for testing
    bool getJumpOccurred() const { return m_jumpOccurred; }
    void setJumpOccurred(bool value) { m_jumpOccurred = value; }
    
    // Get execution queue status
    bool isQueueEmpty() const { return m_executionQueue.isEmpty(); }
    
    // Dequeue from execution queue
    LogicalLine dequeueExecutionLine() { return m_executionQueue.dequeue(); }
    
    // Get execution lines (for loop handling)
    QList<LogicalLine> getExecutionLines() { return m_erbLoader.getLogicalLinesCI(m_currentScript); }
    
    // Get repeat loop stack reference
    QList<RepeatLoopState>& getRepeatLoopStack() { return m_repeatLoopStack; }
    
    // Setters
    void setCurrentScript(const QString& script) { m_currentScript = script; }
    void setExecutionPosition(int pos) { m_executionPosition = pos; }
    
};

#endif // EXECUTION_ENGINE_H
