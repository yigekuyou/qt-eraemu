#include "process_state.h"
#include <QString>
#include <QDebug>

ProcessState::ProcessState(QObject* parent)
    : QObject(parent),
      m_state(StateCode::Title_Begin),
      m_beginType(BeginType::NONE),
      m_currentLine(nullptr),
      m_errorLine(nullptr),
      m_lineCount(0)
{
}

StateCode ProcessState::getState() const {
    return m_state;
}

void ProcessState::setState(StateCode state) {
    m_state = state;
}

BeginType ProcessState::getBeginType() const {
    return m_beginType;
}

void ProcessState::setBegin(BeginType type) {
    m_beginType = type;
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
