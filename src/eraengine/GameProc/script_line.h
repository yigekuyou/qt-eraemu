#ifndef SCRIPT_LINE_H
#define SCRIPT_LINE_H

#include <QString>
#include <QList>
#include <QMetaType>
#include <memory>

// Script line type enumeration
enum class ScriptLineType {
    Empty,           // Empty line
    Comment,         // Comment line (starts with ;)
    Label,           // Label definition (@LABEL_NAME)
    Instruction,     // Instruction line (starts with instruction)
    Expression,      // Expression assignment (VAR = value)
    Preprocessor,    // Preprocessor directive (#DIRECTIVE)
    Continuation,    // Line continuation
    Unknown          // Unknown line type
};
Q_DECLARE_METATYPE(ScriptLineType)

// Script position information
struct ScriptPosition {
    QString filename;
    int lineNumber;
    int column;
    
    ScriptPosition(const QString& file = "", int line = 0, int col = 0)
        : filename(file), lineNumber(line), column(col) {}
    
    QString toString() const {
        return QString("%1:%2:%3").arg(filename).arg(lineNumber).arg(column);
    }
};
Q_DECLARE_METATYPE(ScriptPosition)

// Instruction argument
struct InstructionArgument {
    QString value;
    bool isString;  // true if value is a string literal
    bool isVariable;  // true if value is a variable reference
    
    InstructionArgument(const QString& val = "", bool str = false, bool var = false)
        : value(val), isString(str), isVariable(var) {}
};
Q_DECLARE_METATYPE(InstructionArgument)

// Instruction data
struct InstructionData {
    QString name;  // Instruction name (e.g., "PRINT", "GOTO")
    QList<InstructionArgument> arguments;
    ScriptPosition position;
    
    InstructionData() : position("", 0, 0) {}
};
Q_DECLARE_METATYPE(InstructionData)

// Internal data structure
struct ScriptLineData {
    ScriptLineType m_type;
    QString m_content;
    ScriptPosition m_position;
    InstructionData m_instructionData;
    
    ScriptLineData() : m_type(ScriptLineType::Empty), m_content(""), m_position("", 0, 0) {}
};

// Script line representation
class ScriptLine {
public:
    ScriptLine();
    ScriptLine(const ScriptLine& other);
    
    // Line properties
    ScriptLineType type() const;
    QString content() const;
    ScriptPosition position() const;
    InstructionData instructionData() const;
    
    // Setters (for internal use during parsing)
    void setType(ScriptLineType type);
    void setContent(const QString& content);
    void setPosition(const ScriptPosition& position);
    void setInstructionData(const InstructionData& data);
    
    // Factory methods
    static ScriptLine createEmpty(const ScriptPosition& pos);
    static ScriptLine createComment(const QString& comment, const ScriptPosition& pos);
    static ScriptLine createLabel(const QString& labelName, const ScriptPosition& pos);
    static ScriptLine createInstruction(const InstructionData& data);
    
private:
    std::shared_ptr<ScriptLineData> m_data;
};

// Logical line - a complete logical line (may span multiple script lines)
class LogicalLine {
public:
    LogicalLine();
    LogicalLine(const ScriptPosition& pos);
    
    // Get the first position
    ScriptPosition position() const;
    
    // Add a script line
    void addScriptLine(const ScriptLine& line);
    
    // Get script lines
    QList<ScriptLine> scriptLines() const;
    
    // Get the complete content (concatenated)
    QString content() const;
    
    // Check if it's a complete logical line
    bool isComplete() const;
    
    // Set completion status
    void setComplete(bool complete);
    
private:
    ScriptPosition m_position;
    QList<ScriptLine> m_scriptLines;
    bool m_complete;
};

#endif // SCRIPT_LINE_H
