#ifndef EXECUTION_ENGINE_H
#define EXECUTION_ENGINE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QQueue>
#include <QHash>
#include <QRegularExpression>
#include "variable_storage.h"
#include "script_line.h"
#include "erb_loader.h"
#include "logical_line_parser.h"

// Execution engine - main game loop and instruction execution
class ExecutionEngine : public QObject {
    Q_OBJECT

public:
    explicit ExecutionEngine(VariableStorage* storage, QObject* parent = nullptr);
    
    // Start execution
    void executeScript(const QString& scriptName);
    void executeLogicalLine(const LogicalLine& line);
    
    // Control flow
    bool handleGoto(const QString& label);
    bool handleIf(const QString& condition, const QString& thenLabel);
    
    // Get execution state
    bool isRunning() const;
    int getCurrentLine() const;
    QString getCurrentScript() const;
    
    // Load scripts
    bool loadScripts(const QString& scriptDir);
    
    // Get loaded scripts
    QHash<QString, QList<ScriptLine>> getLoadedScripts() const;
    
signals:
    void lineExecuted(int line);
    void executionFinished();
    void errorOccurred(const QString& message);
    
private:
    // Execute a single instruction
    bool executeInstruction(const InstructionData& data);
    
    // Handle basic instructions
    bool handlePrint(const QList<InstructionArgument>& args);
    bool handleResetData();
    bool handleLoadGlobal();
    
    // Assignment handling
    bool handleAssignment(const QString& lhs, const QString& rhs);
    bool handleCompoundAssignment(const QString& lhs, const QString& op, const QString& rhs);
    
    // Parse LHS (left-hand side) of assignment
    QPair<QString, int> parseLHS(const QString& lhs);
    
    // Helper methods
    void setError(const QString& message);
    void stopExecution();
    
    VariableStorage* m_storage;
    ErbLoader m_erbLoader;
    LogicalLineParser m_parser;
    bool m_running;
    int m_currentLine;
    QString m_currentScript;
    
    // Execution queue
    QQueue<LogicalLine> m_executionQueue;
    
    // Label lookup helper
    bool resolveLabel(const QString& label, ScriptLine*& line);
    
    // Assignment regex patterns
    QRegularExpression m_simpleAssignmentRegex;
    QRegularExpression m_compoundAssignmentRegex;
};

#endif // EXECUTION_ENGINE_H
