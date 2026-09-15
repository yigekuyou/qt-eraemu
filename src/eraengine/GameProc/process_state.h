#ifndef PROCESS_STATE_H
#define PROCESS_STATE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QMetaType>
#include "script_line.h"
#include "logical_line_parser.h"

// State codes matching C# ProcessState
enum class StateCode {
    Title_Begin = 0,
    Openning = 1,
    Normal = 0xFFFF,
    System_Reloaderb = 0x230,
    First_Begin = 0x240,
};

// Begin types
enum class BeginType {
    NONE = 0,
    SHOP = 2,
    TRAIN = 3,
    AFTERTRAIN = 4,
    ABLUP = 5,
    TURNEND = 6,
    FIRST = 7,
    TITLE = 8,
};

// Called function information
struct CalledFunction {
    QString labelName;
    ScriptPosition position;
    
    CalledFunction(const QString& label = "", const ScriptPosition& pos = ScriptPosition())
        : labelName(label), position(pos) {}
};

Q_DECLARE_METATYPE(CalledFunction)

// Process state management
class ProcessState : public QObject {
    Q_OBJECT

public:
    explicit ProcessState(QObject* parent = nullptr);
    
    // State management
    StateCode getState() const;
    void setState(StateCode state);
    
    // Begin type management
    BeginType getBeginType() const;
    void setBegin(BeginType type);
    void setBegin(const QString& keyword);
    
    // Line management
    LogicalLine* getCurrentLine() const;
    void setCurrentLine(LogicalLine* line);
    
    LogicalLine* getErrorLine() const;
    void setErrorLine(LogicalLine* line);
    
    // Function call stack
    void pushFunction(const QString& label, const ScriptPosition& pos);
    void popFunction();
    int getFunctionCount() const;
    
    // Line counting
    int getLineCount() const;
    void setLineCount(int count);
    
    // State queries
    bool isScriptEnd() const;
    bool isBegun() const;
    
private:
    StateCode m_state;
    BeginType m_beginType;
    LogicalLine* m_currentLine;
    LogicalLine* m_errorLine;
    int m_lineCount;
    QList<CalledFunction> m_functionList;
};

#endif // PROCESS_STATE_H
