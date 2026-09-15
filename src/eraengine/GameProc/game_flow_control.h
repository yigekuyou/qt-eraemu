#ifndef GAME_FLOW_CONTROL_H
#define GAME_FLOW_CONTROL_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QList>
#include <QVariant>
#include <QTimer>

class VariableStorage; // Forward declaration

// Label/Position in script
struct ScriptLabel {
    QString name;
    int lineNumber;
    QString scriptFile;
    
    ScriptLabel() : lineNumber(0) {}
    ScriptLabel(const QString& labelName, int line, const QString& file)
        : name(labelName), lineNumber(line), scriptFile(file) {}
};

// Script execution context
class ScriptContext : public QObject
{
    Q_OBJECT

public:
    ScriptContext(QObject *parent = nullptr);
    
    // Script state management
    void setCurrentScript(const QString& scriptName);
    QString currentScript() const;
    
    void setCurrentLine(int line);
    int currentLine() const;
    
    // Label management
    void addLabel(const QString& name, int line, const QString& file);
    bool hasLabel(const QString& name) const;
    int getLabelLine(const QString& name) const;
    
    // Control flow tracking
    void pushCallStack(const QString& functionName);
    QString popCallStack();
    QList<QString> callStack() const;

private:
    QString m_currentScript;
    int m_currentLine;
    QHash<QString, ScriptLabel> m_labels;
    QList<QString> m_callStack;
};

// Game flow controller
class GameFlowControl : public QObject
{
    Q_OBJECT

public:
    explicit GameFlowControl(QObject *parent = nullptr);
    
    // Script execution
    bool executeScript(const QString& scriptFile);
    bool executeScriptLine(const QString& line, VariableStorage* storage);
    
    // Control flow instructions
    bool handleGoto(const QString& labelName);
    bool handleIf(const QString& condition, const QString& thenLabel, const QString& elseLabel);
    bool handleCall(const QString& functionName);
    bool handleReturn();
    
    // State management
    void pauseExecution();
    void resumeExecution();
    bool isPaused() const;
    
    // Script loading and management
    bool loadScript(const QString& scriptFile);
    QStringList getLoadedScripts() const;
    bool isScriptLoaded(const QString& scriptFile) const;
    
    // Context management
    ScriptContext* getContext();
    void setContext(ScriptContext* context);

private:
    ScriptContext* m_context;
    bool m_paused;
    QHash<QString, QString> m_loadedScripts;
    QTimer* m_executionTimer;
    
    // Helper methods
    bool processScriptLine(const QString& line, VariableStorage* storage);
    QString evaluateExpression(const QString& expression, VariableStorage* storage);
};

#endif // GAME_FLOW_CONTROL_H