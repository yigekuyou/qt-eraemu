#ifndef SYSTEM_PROCESSOR_H
#define SYSTEM_PROCESSOR_H

#include <QObject>
#include <QString>
#include "process_state.h"
#include "script_line.h"

// System processor - handles system-level operations
class SystemProcessor : public QObject {
    Q_OBJECT

public:
    explicit SystemProcessor(ProcessState* state, QObject* parent = nullptr);
    
    // System operations
    bool processInputRequest();
    bool processConsoleOutput(const QString& text);
    bool processLabelJump(const QString& label);
    
    // State transitions
    bool transitionToTitle();
    bool transitionToGame();
    bool transitionToState(StateCode state);
    
    // Process script line
    bool processLine(const LogicalLine& line);
    
    // Get state
    ProcessState* getState() const;
    
signals:
    void outputReceived(const QString& text);
    void stateChanged(StateCode newState);
    void errorOccurred(const QString& message);
    
private:
    ProcessState* m_state;
    
    // Helper methods
    void setStateCode(StateCode code);
};

#endif // SYSTEM_PROCESSOR_H
