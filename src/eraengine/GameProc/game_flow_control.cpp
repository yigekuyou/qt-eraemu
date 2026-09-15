#include "game_flow_control.h"
#include "variable_storage.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QTimer>

ScriptContext::ScriptContext(QObject *parent)
    : QObject(parent), m_currentLine(0)
{
}

void ScriptContext::setCurrentScript(const QString& scriptName)
{
    m_currentScript = scriptName;
}

QString ScriptContext::currentScript() const
{
    return m_currentScript;
}

void ScriptContext::setCurrentLine(int line)
{
    m_currentLine = line;
}

int ScriptContext::currentLine() const
{
    return m_currentLine;
}

void ScriptContext::addLabel(const QString& name, int line, const QString& file)
{
    m_labels[name] = ScriptLabel(name, line, file);
}

bool ScriptContext::hasLabel(const QString& name) const
{
    return m_labels.contains(name);
}

int ScriptContext::getLabelLine(const QString& name) const
{
    auto it = m_labels.find(name);
    if (it != m_labels.end()) {
        return it.value().lineNumber;
    }
    return -1;
}

void ScriptContext::pushCallStack(const QString& functionName)
{
    m_callStack.append(functionName);
}

QString ScriptContext::popCallStack()
{
    if (!m_callStack.isEmpty()) {
        return m_callStack.takeLast();
    }
    return QString();
}

QList<QString> ScriptContext::callStack() const
{
    return m_callStack;
}

GameFlowControl::GameFlowControl(QObject *parent)
    : QObject(parent), m_context(nullptr), m_paused(false)
{
    m_context = new ScriptContext(this);
    m_executionTimer = new QTimer(this);
    m_executionTimer->setSingleShot(true);
}

bool GameFlowControl::executeScript(const QString& scriptFile)
{
    QFile file(scriptFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    
    QTextStream in(&file);
    QString line;
    int lineNumber = 0;
    
    while (!in.atEnd()) {
        line = in.readLine();
        lineNumber++;
        
        // Skip empty lines and comments
        if (line.trimmed().isEmpty() || line.trimmed().startsWith(";")) {
            continue;
        }
        
        // Process the line
        if (!executeScriptLine(line.trimmed(), nullptr)) {
            // Handle error
            return false;
        }
    }
    
    return true;
}

bool GameFlowControl::executeScriptLine(const QString& line, VariableStorage* storage)
{
    if (line.trimmed().isEmpty()) {
        return true;
    }
    
    // Process different instruction types
    if (line.trimmed().startsWith("GOTO")) {
        QRegularExpression re(R"(^GOTO\s+(.+)$)");
        QRegularExpressionMatch match = re.match(line.trimmed());
        if (match.hasMatch()) {
            QString labelName = match.captured(1).trimmed();
            return handleGoto(labelName);
        }
    }
    
    if (line.trimmed().startsWith("IF")) {
        QRegularExpression re(R"(^IF\s+(.+?)\s+THEN\s+(.+?)(?:\s+ELSE\s+(.+))?$)");
        QRegularExpressionMatch match = re.match(line.trimmed());
        if (match.hasMatch()) {
            QString condition = match.captured(1).trimmed();
            QString thenLabel = match.captured(2).trimmed();
            QString elseLabel = match.captured(3).trimmed();
            return handleIf(condition, thenLabel, elseLabel);
        }
    }
    
    // Process other instruction types...
    
    return true;
}

bool GameFlowControl::handleGoto(const QString& labelName)
{
    // In a real implementation, this would jump to the label
    // For now, just a placeholder
    return true;
}

bool GameFlowControl::handleIf(const QString& condition, const QString& thenLabel, const QString& elseLabel)
{
    // In a real implementation, this would evaluate condition
    // and potentially jump to appropriate label
    // For now, just a placeholder
    return true;
}

bool GameFlowControl::handleCall(const QString& functionName)
{
    // In a real implementation, this would call a function
    // For now, just a placeholder
    return true;
}

bool GameFlowControl::handleReturn()
{
    // In a real implementation, this would return from a function call
    // For now, just a placeholder
    return true;
}

void GameFlowControl::pauseExecution()
{
    m_paused = true;
}

void GameFlowControl::resumeExecution()
{
    m_paused = false;
}

bool GameFlowControl::isPaused() const
{
    return m_paused;
}

bool GameFlowControl::loadScript(const QString& scriptFile)
{
    // In a real implementation, this would load the script into memory
    // For now, just a placeholder
    m_loadedScripts[scriptFile] = scriptFile;
    return true;
}

QStringList GameFlowControl::getLoadedScripts() const
{
    return m_loadedScripts.keys();
}

bool GameFlowControl::isScriptLoaded(const QString& scriptFile) const
{
    return m_loadedScripts.contains(scriptFile);
}

ScriptContext* GameFlowControl::getContext()
{
    return m_context;
}

void GameFlowControl::setContext(ScriptContext* context)
{
    m_context = context;
}

bool GameFlowControl::processScriptLine(const QString& line, VariableStorage* storage)
{
    // This would process a single line of script execution
    // Implementation would be based on eraTW/eraTetris syntax
    return true;
}

QString GameFlowControl::evaluateExpression(const QString& expression, VariableStorage* storage)
{
    // This would evaluate an expression against the variable storage
    return QString();
}