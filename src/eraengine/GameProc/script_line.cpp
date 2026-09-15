#include "script_line.h"

// ScriptLine implementation
ScriptLine::ScriptLine() : m_data(std::make_shared<ScriptLineData>()) {}

ScriptLine::ScriptLine(const ScriptLine& other) : m_data(other.m_data) {}

ScriptLineType ScriptLine::type() const {
    return m_data->m_type;
}

QString ScriptLine::content() const {
    return m_data->m_content;
}

ScriptPosition ScriptLine::position() const {
    return m_data->m_position;
}

InstructionData ScriptLine::instructionData() const {
    return m_data->m_instructionData;
}

void ScriptLine::setType(ScriptLineType type) {
    m_data->m_type = type;
}

void ScriptLine::setContent(const QString& content) {
    m_data->m_content = content;
}

void ScriptLine::setPosition(const ScriptPosition& position) {
    m_data->m_position = position;
}

void ScriptLine::setInstructionData(const InstructionData& data) {
    m_data->m_instructionData = data;
}

ScriptLine ScriptLine::createEmpty(const ScriptPosition& pos) {
    ScriptLine line;
    line.m_data->m_type = ScriptLineType::Empty;
    line.m_data->m_position = pos;
    return line;
}

ScriptLine ScriptLine::createComment(const QString& comment, const ScriptPosition& pos) {
    ScriptLine line;
    line.m_data->m_type = ScriptLineType::Comment;
    line.m_data->m_content = comment;
    line.m_data->m_position = pos;
    return line;
}

ScriptLine ScriptLine::createLabel(const QString& labelName, const ScriptPosition& pos) {
    ScriptLine line;
    line.m_data->m_type = ScriptLineType::Label;
    line.m_data->m_content = "@" + labelName;
    line.m_data->m_position = pos;
    
    // Extract label name from content (remove @)
    if (!labelName.isEmpty()) {
        line.m_data->m_instructionData.name = labelName;
    }
    return line;
}

ScriptLine ScriptLine::createInstruction(const InstructionData& data) {
    ScriptLine line;
    line.m_data->m_type = ScriptLineType::Instruction;
    line.m_data->m_content = data.name;
    line.m_data->m_position = data.position;
    line.m_data->m_instructionData = data;
    return line;
}

// LogicalLine implementation
LogicalLine::LogicalLine() : m_position("", 0, 0), m_complete(false) {}

LogicalLine::LogicalLine(const ScriptPosition& pos) : m_position(pos), m_complete(false) {}

ScriptPosition LogicalLine::position() const {
    return m_position;
}

void LogicalLine::addScriptLine(const ScriptLine& line) {
    m_scriptLines.append(line);
}

QList<ScriptLine> LogicalLine::scriptLines() const {
    return m_scriptLines;
}

QString LogicalLine::content() const {
    QString result;
    for (const ScriptLine& line : m_scriptLines) {
        result += line.content();
    }
    return result;
}

bool LogicalLine::isComplete() const {
    return m_complete;
}

void LogicalLine::setComplete(bool complete) {
    m_complete = complete;
}
