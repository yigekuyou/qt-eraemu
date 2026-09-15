#include "system_processor.h"
#include <QDebug>

SystemProcessor::SystemProcessor(ProcessState* state, QObject* parent)
    : QObject(parent), m_state(state)
{
    if (!m_state) {
        m_state = new ProcessState(this);
    }
}

bool SystemProcessor::processInputRequest() {
    // In a real implementation, this would wait for user input
    // For now, just return success
    qDebug() << "Input request processed";
    return true;
}

bool SystemProcessor::processConsoleOutput(const QString& text) {
    // Output to console
    qDebug() << "Console output:" << text;
    emit outputReceived(text);
    return true;
}

bool SystemProcessor::processLabelJump(const QString& label) {
    // Handle label jump
    qDebug() << "Label jump to:" << label;
    return true;
}

bool SystemProcessor::transitionToTitle() {
    setStateCode(StateCode::Title_Begin);
    m_state->setBegin(BeginType::TITLE);
    return true;
}

bool SystemProcessor::transitionToGame() {
    // Transition to normal game state
    setStateCode(StateCode::Normal);
    m_state->setBegin(BeginType::TRAIN);
    return true;
}

bool SystemProcessor::transitionToState(StateCode state) {
    setStateCode(state);
    return true;
}

bool SystemProcessor::processLine(const LogicalLine& line) {
    // Process a logical line from the script
    const QList<ScriptLine>& scriptLines = line.scriptLines();
    
    for (const ScriptLine& scriptLine : scriptLines) {
        if (scriptLine.type() == ScriptLineType::Label) {
            // Labels are just targets, not executed
            continue;
        }
        
        if (scriptLine.type() == ScriptLineType::Instruction) {
            InstructionData data = scriptLine.instructionData();
            qDebug() << "Processing instruction:" << data.name;
            
            // Process based on instruction type
            if (data.name == "BEGIN") {
                if (!data.arguments.isEmpty()) {
                    QString keyword = data.arguments[0].value;
                    m_state->setBegin(keyword);
                }
            }
            else if (data.name == "GOTO") {
                if (!data.arguments.isEmpty()) {
                    QString label = data.arguments[0].value;
                    processLabelJump(label);
                }
            }
            else if (data.name == "IF") {
                // Handle IF statement
                // For now, just log
                qDebug() << "IF statement processed";
            }
            else {
                // Default instruction processing
                qDebug() << "Executing:" << data.name;
            }
        }
    }
    
    // Advance to next line
    m_state->setLineCount(m_state->getLineCount() + 1);
    return true;
}

void SystemProcessor::setStateCode(StateCode code) {
    m_state->setState(code);
    emit stateChanged(code);
}

ProcessState* SystemProcessor::getState() const {
    return m_state;
}
