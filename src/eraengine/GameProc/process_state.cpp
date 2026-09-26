#include "process_state.h"
#include <QString>
#include <QDebug>

ProcessState::ProcessState(QObject* parent)
    : QObject(parent),
      m_systemState(SystemStateCode::Title_Begin),
      m_beginType(BeginType::NONE),
      m_currentLine(nullptr),
      m_errorLine(nullptr),
      m_lineCount(0),
      m_lastState(SystemStateCode::Title_Begin)
{
}

SystemStateCode ProcessState::getSystemState() const {
    return m_systemState;
}

void ProcessState::setSystemState(SystemStateCode state) {
    if (m_systemState != state) {
        m_lastState = m_systemState;
        m_systemState = state;
        emit stateChanged();
    }
}

SystemStateCode ProcessState::getState() const {
    return m_systemState;
}

void ProcessState::setState(SystemStateCode state) {
    if (m_systemState != state) {
        m_lastState = m_systemState;
        m_systemState = state;
        emit stateChanged();
    }
}

BeginType ProcessState::getBeginType() const {
    return m_beginType;
}

void ProcessState::setBegin(BeginType type) {
    m_beginType = type;
}

bool ProcessState::canSave() const {
    return (static_cast<int>(m_systemState) & static_cast<int>(SystemStateCode::__CAN_SAVE__)) != 0;
}

bool ProcessState::canBegin() const {
    return (static_cast<int>(m_systemState) & static_cast<int>(SystemStateCode::__CAN_BEGIN__)) != 0;
}

void ProcessState::processBegin(BeginType type) {
    // Clear function stack on BEGIN
    m_functionList.clear();
    
    // Set the begin type
    m_beginType = type;
    
    // Store old state for signal
    SystemStateCode oldState = m_systemState;
    
    // Set state based on begin type
    switch (type) {
        case BeginType::SHOP:
            m_systemState = SystemStateCode::Shop_Begin;
            break;
        case BeginType::TRAIN:
            m_systemState = SystemStateCode::Train_Begin;
            break;
        case BeginType::AFTERTRAIN:
            m_systemState = SystemStateCode::AfterTrain_Begin;
            break;
        case BeginType::ABLUP:
            m_systemState = SystemStateCode::Ablup_Begin;
            break;
        case BeginType::TURNEND:
            m_systemState = SystemStateCode::Turnend_Begin;
            break;
        case BeginType::FIRST:
            m_systemState = SystemStateCode::First_Begin;
            break;
        case BeginType::TITLE:
            m_systemState = SystemStateCode::Title_Begin;
            break;
        case BeginType::NONE:
        default:
            break;
    }
    
    // Emit state changed signal if state changed
    if (m_systemState != oldState) {
        emit stateChanged();
    }
}

void ProcessState::setBegin(const QString& keyword) {
    QString upperKeyword = keyword.toUpper();
    
    if (upperKeyword == "SHOP") {
        m_beginType = BeginType::SHOP;
    } else if (upperKeyword == "TRAIN") {
        m_beginType = BeginType::TRAIN;
    } else if (upperKeyword == "AFTERTRAIN") {
        m_beginType = BeginType::AFTERTRAIN;
    } else if (upperKeyword == "ABLUP") {
        m_beginType = BeginType::ABLUP;
    } else if (upperKeyword == "TURNEND") {
        m_beginType = BeginType::TURNEND;
    } else if (upperKeyword == "FIRST") {
        m_beginType = BeginType::FIRST;
    } else if (upperKeyword == "TITLE") {
        m_beginType = BeginType::TITLE;
    } else {
        // In a real implementation, this would throw an exception
        qDebug() << "BEGIN keyword not recognized:" << keyword;
    }
}

LogicalLine* ProcessState::getCurrentLine() const {
    return m_currentLine;
}

void ProcessState::setCurrentLine(LogicalLine* line) {
    m_currentLine = line;
}

LogicalLine* ProcessState::getErrorLine() const {
    return m_errorLine ? m_errorLine : m_currentLine;
}

void ProcessState::setErrorLine(LogicalLine* line) {
    m_errorLine = line;
}

void ProcessState::pushFunction(const QString& label, const ScriptPosition& pos) {
    m_functionList.append(CalledFunction(label, pos));
}

void ProcessState::popFunction() {
    if (!m_functionList.isEmpty()) {
        m_functionList.removeLast();
    }
}

// IntoFunction: Push function to stack and set current line for execution
void ProcessState::intoFunction(const QString& label, const ScriptPosition& pos) {
    m_functionList.append(CalledFunction(label, pos));
    // Note: We don't set m_currentLine here because the ExecutionEngine
    // manages its own execution queue. This is for state tracking only.
}

// ReturnF: Pop function from stack and return to caller
void ProcessState::returnF() {
    if (!m_functionList.isEmpty()) {
        m_functionList.removeLast();
    }
}

// Get the position of the current function (top of stack)
ScriptPosition ProcessState::getCurrentFunctionPosition() const {
    if (m_functionList.isEmpty()) {
        return ScriptPosition();
    }
    return m_functionList.last().getPosition();
}

// Get label name of current function
QString ProcessState::getCurrentFunctionLabel() const {
    if (m_functionList.isEmpty()) {
        return "";
    }
    return m_functionList.last().getLabelName();
}

int ProcessState::getFunctionCount() const {
    return m_functionList.count();
}

int ProcessState::getLineCount() const {
    return m_lineCount;
}

void ProcessState::setLineCount(int count) {
    m_lineCount = count;
}

bool ProcessState::isScriptEnd() const {
    return m_functionList.isEmpty();
}

bool ProcessState::isBegun() const {
    return m_beginType != BeginType::NONE;
}

void ProcessState::requestStateCheck() {
    // Emit state unchanged if state didn't change since last check
    if (m_systemState == m_lastState) {
        emit stateUnchanged();
    } else {
        emit stateChangedSignal();
        // Update last state to current state
        m_lastState = m_systemState;
    }
}

void ProcessState::emitRequestNextInstruction() {
    emit requestNextInstruction();
}

void ProcessState::setEntryPointScript(const QString& scriptName) {
    m_entryPointScript = scriptName;
    qDebug() << "Set entry point script:" << scriptName;
}
