#ifndef SYSTEM_PROCESSOR_H
#define SYSTEM_PROCESSOR_H

#include <QObject>
#include <QString>
#include <QHash>
#include "process_state.h"
#include "script_line.h"
#include "game_base_data.h"

// Forward declarations
class EraParseTable;
class ExecutionEngine;

// System processor - handles system-level operations
// Uses signal/slot architecture for state-based execution control
class SystemProcessor : public QObject {
    Q_OBJECT

public:
    explicit SystemProcessor(ProcessState* state, QObject* parent = nullptr);
    
    // System operations
    bool processInputRequest();
    bool processConsoleOutput(const QString& text);
    bool processLabelJump(const QString& label);
    
    // State transitions - these methods update state and emit appropriate signals
    bool transitionToTitle();
    bool transitionToGame();
    bool transitionToState(StateCode state);
    
    // Specific state transitions for system phases
    bool transitionToTrain();
    bool transitionToAblup();
    bool transitionToShop();
    bool transitionToSaveGame();
    bool transitionToLoadGame();
    bool transitionToAfterTrain();
    bool transitionToTurnEnd();
    
    // Process script line
    bool processLine(const LogicalLine& line);
    
    // Get state
    ProcessState* getState() const;
    
    // Set associated parse table for state-based execution control
    void setParseTable(EraParseTable* parseTable);
    
    // Set associated execution engine for instruction execution
    void setExecutionEngine(ExecutionEngine* engine);
    
signals:
    void outputReceived(const QString& text);
    void stateChanged(StateCode oldState, StateCode newState);  // Old state for tracking
    void errorOccurred(const QString& message);
    void inputRequested(StateCode state);  // Signal when input is requested based on state
    void parsingPaused(StateCode state);   // Signal when parsing needs to pause for input
    void parsingResumed(StateCode state);  // Signal when parsing resumes after input
    
public slots:
    // Handle BEGIN instruction - processes the keyword and transitions state
    void onBeginRequested(const QString& keyword);
    
private:
    ProcessState* m_state;
    EraParseTable* m_parseTable;
    ExecutionEngine* m_executionEngine;
    
    // Helper methods
    void setStateCode(StateCode code);
    void checkStateForInputWait();  // Check if current state requires input wait
    void emitStateTransition(StateCode oldState, StateCode newState);
};

#endif // SYSTEM_PROCESSOR_H
