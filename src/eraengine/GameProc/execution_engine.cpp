#include "execution_engine.h"
#include <QDebug>
#include <QRegularExpression>
#include "expression_evaluator.h"
#include "eraengine.h"

ExecutionEngine::ExecutionEngine(VariableStorage* storage, QObject* parent)
    : QObject(parent), m_storage(storage), m_running(false), m_currentLine(0) {
    connect(&m_erbLoader, &ErbLoader::objectNameChanged, this, &ExecutionEngine::objectNameChanged);
    
    // Initialize assignment regex patterns
    m_simpleAssignmentRegex = QRegularExpression(R"(^(\w[\w:]*?)\s*=\s*(.+)$)");
    m_compoundAssignmentRegex = QRegularExpression(R"(^(\w[\w:]*?)\s*(\+\=|\-\=|\*\=|\/\=)\s*(.+)$)");
}

void ExecutionEngine::executeScript(const QString& scriptName) {
    if (!m_erbLoader.getLoadedScripts().contains(scriptName)) {
        setError("Script not loaded: " + scriptName);
        return;
    }
    
    m_currentScript = scriptName;
    m_running = true;
    
    // Get script lines
    QList<ScriptLine> scriptLines = m_erbLoader.getLoadedScripts().value(scriptName);
    
    // Parse into logical lines
		QList<LogicalLine> logicalLines = m_parser.parseLogicalLines(scriptLines);
    // Add to execution queue
    for (const LogicalLine& line : logicalLines) {
        m_executionQueue.enqueue(line);
    }
    
    // Start execution loop
    while (m_running && !m_executionQueue.isEmpty()) {
        LogicalLine line = m_executionQueue.dequeue();
        executeLogicalLine(line);
        emit lineExecuted(m_currentLine);
        m_currentLine++;
    }
    
    m_running = false;
    emit executionFinished();
}

void ExecutionEngine::executeLogicalLine(const LogicalLine& line) {
    const QList<ScriptLine>& scriptLines = line.scriptLines();
    
    for (const ScriptLine& scriptLine : scriptLines) {
        if (scriptLine.type() == ScriptLineType::Label) {
            // Labels don't execute, they're just jump targets
            continue;
        }
        
        if (scriptLine.type() == ScriptLineType::Instruction) {
            InstructionData data = scriptLine.instructionData();
            if (!executeInstruction(data)) {
                // Error occurred
                return;
            }
        }
        
        // Other line types (comments, empty) are ignored
    }
}

bool ExecutionEngine::handleGoto(const QString& label) {
    ScriptLine* line = nullptr;
    if (resolveLabel(label, line)) {
        // Find this label in the execution queue and continue from there
        // For now, we'll just note it and stop execution
        // In a full implementation, this would restart execution from the label
        qDebug() << "GOTO label:" << label;
        return true;
    } else {
        setError("Label not found: " + label);
        return false;
    }
}

bool ExecutionEngine::handleIf(const QString& condition, const QString& thenLabel) {
    // Evaluate the condition using ExpressionEvaluator
    ExpressionEvaluator evaluator;
    QVariant result = evaluator.evaluate(condition, m_storage, nullptr);
    
    // If condition evaluates to non-zero (true), execute the thenLabel
    // Otherwise, skip (for now, we'll just continue execution)
    if (result.isValid() && result.toInt() != 0) {
        // Condition is true, execute the thenLabel
        return handleGoto(thenLabel);
    }
    return true;
}

QPair<QString, int> ExecutionEngine::parseLHS(const QString& lhs) {
    // Parse variable name and index from LHS
    // Format: VARIABLE or VARIABLE:index
    QRegularExpression re(R"(^(\w+):(\d+)$)");
    QRegularExpressionMatch match = re.match(lhs);
    
    if (match.hasMatch()) {
        QString varName = match.captured(1);
        int index = match.captured(2).toInt();
        return qMakePair(varName, index);
    }
    
    // No index, return variable name with -1 as index
    return qMakePair(lhs, -1);
}



bool ExecutionEngine::handleCompoundAssignment(const QString& lhs, const QString& op, const QString& rhs) {
    // Parse the LHS to get variable name and index
    auto [varName, index] = parseLHS(lhs);
    
    // Get current value
    qint64 currentValue = 0;
    if (index >= 0) {
        currentValue = m_storage->getGlobalInt1D(varName, index);
    } else {
        currentValue = m_storage->getGlobalInt1D(varName, 0);
    }
    
    // Evaluate the RHS expression
    int rhsValue = rhs.toInt();
    
    // Apply the operation
    if (op == "+=") {
        currentValue += rhsValue;
    } else if (op == "-=") {
        currentValue -= rhsValue;
    } else if (op == "*=") {
        currentValue *= rhsValue;
    } else if (op == "/=") {
        if (rhsValue != 0) {
            currentValue /= rhsValue;
        }
    }
    
    // Set the updated value
    if (index >= 0) {
        m_storage->setGlobalInt1D(varName, index, currentValue);
    } else {
        m_storage->setGlobalInt1D(varName, 0, currentValue);
    }
    
    return true;
}

bool ExecutionEngine::isRunning() const {
    return m_running;
}

int ExecutionEngine::getCurrentLine() const {
    return m_currentLine;
}

QString ExecutionEngine::getCurrentScript() const {
    return m_currentScript;
}

bool ExecutionEngine::loadScripts(const QString& scriptDir) {
    return m_erbLoader.loadDirectory(scriptDir);
}

QHash<QString, QList<ScriptLine>> ExecutionEngine::getLoadedScripts() const {
    return m_erbLoader.getLoadedScripts();
}

bool ExecutionEngine::resolveLabel(const QString& label, ScriptLine*& line) {
    // Remove $ prefix if present (era format uses $ for labels)
    QString labelName = label;
    if (labelName.startsWith('$')) {
        labelName = labelName.mid(1);
    }
    
    line = m_erbLoader.findLabel(labelName);
    return line != nullptr;
}

bool ExecutionEngine::executeInstruction(const InstructionData& data) {
    if (data.name == "GOTO") {
        if (data.arguments.size() >= 1) {
            QString label = data.arguments[0].value;
            return handleGoto(label);
        }
    }
    else if (data.name == "IF") {
        // IF instruction - for now, just evaluate condition and continue
        // Full IF/ELSEIF/ELSE/ENDIF block parsing is not yet implemented
        if (data.arguments.size() >= 1) {
            QString condition = data.arguments[0].value;
            // Just evaluate the condition for now
            ExpressionEvaluator evaluator;
            evaluator.evaluate(condition, m_storage, nullptr);
        }
        return true;
    }
    else if (data.name == "ELSEIF") {
        // ELSEIF - skip for now, just continue execution
        return true;
    }
    else if (data.name == "ELSE") {
        // ELSE - skip for now, just continue execution
        return true;
    }
    else if (data.name == "ENDIF") {
        // ENDIF - just continue, no action needed
        return true;
    }
    else if (data.name == "CALL") {
        // CALL instruction - calls a label/subroutine
        if (data.arguments.size() >= 1) {
            QString label = data.arguments[0].value;
            // Remove parentheses if present (e.g., INIT_STAGE() -> INIT_STAGE)
            label.remove(QRegularExpression(R"(\(\s*\)$)"));
            return handleGoto(label);
        }
        return true;
    }
    else if (data.name == "RESETDATA") {
        return handleResetData();
    }
    else if (data.name == "LOADGLOBAL") {
        return handleLoadGlobal();
    }
    else if (data.name == "PRINTFORML") {
        return handlePrint(data.arguments);
    }
    else if (data.name == "PRINT") {
        return handlePrint(data.arguments);
    }
    else if (data.name == "=") {
        qDebug() << "Handling assignment:" << data.arguments[0].value << "=" << data.arguments[1].value;
        // Simple assignment: VARIABLE = value
        if (data.arguments.size() >= 2) {
            QString lhs = data.arguments[0].value;
            QString rhs = data.arguments[1].value;
            return handleAssignment(lhs, rhs);
        }
    }
    else if (data.name == "+=" || data.name == "-=" || data.name == "*=" || data.name == "/=") {
        // Compound assignment
        if (data.arguments.size() >= 2) {
            QString lhs = data.arguments[0].value;
            QString rhs = data.arguments[1].value;
            return handleCompoundAssignment(lhs, data.name, rhs);
        }
    }
    
    // Default: log instruction
    qDebug() << "Executing instruction:" << data.name << "(name=" << data.name << ")";
    return true;
}

bool ExecutionEngine::handleAssignment(const QString& lhs, const QString& rhs) {
    qDebug() << "handleAssignment: lhs=" << lhs << "rhs=" << rhs;
    
    // Parse the LHS to get variable name and index
    auto [varName, index] = parseLHS(lhs);
    
    qDebug() << "  varName=" << varName << "index=" << index;
    
    // Try to evaluate the RHS as a GameBase variable first
    // Format: {GAMEBASE_KEY}
    QRegularExpression gameBaseRegex(R"(^\{GAMEBASE_(.+)\}$)");
    QRegularExpressionMatch gameBaseMatch = gameBaseRegex.match(rhs.trimmed());
    
    if (gameBaseMatch.hasMatch()) {
        QString key = gameBaseMatch.captured(1);
        // We can't access GameBaseData here, so we'll just set it as 0
        // In a full implementation, we would access the GameBaseData through EraEngine
        qDebug() << "  GameBase variable:" << key << "- not implemented yet";
        m_storage->setGlobalInt1D(varName, 0, 0);
        return true;
    }
    
    // Evaluate the RHS expression using ExpressionEvaluator
    ExpressionEvaluator evaluator;
    QVariant rhsValue = evaluator.evaluate(rhs, m_storage, nullptr);
    
    qDebug() << "  rhsValue=" << rhsValue.toString();
    
    // If evaluation failed, try direct conversion
    bool ok = false;
    int intValue = rhs.toInt(&ok);
    if (ok) {
        rhsValue = QVariant(intValue);
    } else {
        rhsValue = QVariant(rhs);
    }
    
    qDebug() << "  Final rhsValue=" << rhsValue.toString();
    
    // Set the variable value
    if (index >= 0) {
        // Array access
        qDebug() << "  Setting array variable:" << varName << "[" << index << "] =" << rhsValue.toInt();
        m_storage->setGlobalInt1D(varName, index, rhsValue.toInt());
    } else {
        // Simple variable access
        qDebug() << "  Setting variable:" << varName << "=" << rhsValue.toInt();
        m_storage->setGlobalInt1D(varName, 0, rhsValue.toInt());
    }
    
    return true;
}

bool ExecutionEngine::handlePrint(const QList<InstructionArgument>& args) {
    QString text;
    for (const InstructionArgument& arg : args) {
        text += arg.value + " ";
    }
    qDebug() << "PRINT:" << text.trimmed();
    return true;
}

bool ExecutionEngine::handleResetData() {
    qDebug() << "RESETDATA";
    return true;
}

bool ExecutionEngine::handleLoadGlobal() {
    qDebug() << "LOADGLOBAL";
    return true;
}

void ExecutionEngine::setError(const QString& message) {
    qDebug() << "Execution error:" << message;
    m_running = false;
    emit errorOccurred(message);
}

void ExecutionEngine::stopExecution() {
    m_running = false;
}
