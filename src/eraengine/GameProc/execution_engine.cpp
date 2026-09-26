#include "execution_engine.h"
#include <QDebug>
#include <QRegularExpression>
#include "expression_evaluator.h"
#include "eraengine.h"
#include "function_system.h"

ExecutionEngine::ExecutionEngine(VariableStorage* storage, GameBaseData* gameBaseData, QObject* parent)
    : QObject(parent), m_storage(storage), m_gameBaseData(gameBaseData), m_functionSystem(nullptr), m_running(false), m_currentLine(0), m_executionPosition(0), m_totalInstructionsExecuted(0) {
    connect(&m_erbLoader, &ErbLoader::objectNameChanged, this, &ExecutionEngine::objectNameChanged);
    
    // Initialize assignment regex patterns
    m_simpleAssignmentRegex = QRegularExpression(R"(^(\w[\w:]*?)\s*=\s*(.+)$)");
    m_compoundAssignmentRegex = QRegularExpression(R"(^(\w[\w:]*?)\s*(\+\=|\-\=|\*\=|\/\=)\s*(.+)$)");
    
    // Initialize function system
    m_functionSystem = new FunctionSystem(this);
    
    // Initialize repeat loop stack
    // m_repeatLoopStack is automatically initialized by QList
}

ExecutionEngine::~ExecutionEngine() {
    if (m_functionSystem) {
        delete m_functionSystem;
        m_functionSystem = nullptr;
    }
}

void ExecutionEngine::setParseTable(EraParseTable* parseTable) {
    m_parseTable = parseTable;
}

bool ExecutionEngine::executeScript(const QString& scriptName) {
    // This function is kept for backward compatibility
    // The new signal/slot architecture handles execution
    // This function now just validates and returns true
    
    // Resolve script name (case-insensitive) to stored key
    const QString resolved = m_erbLoader.resolveScriptName(scriptName);
    if (resolved.isEmpty()) {
        setError("Script not loaded: " + scriptName);
        return false;
    }
    
    m_currentScript = resolved;
    m_running = true;
    m_executionPosition = 0;
    
    // Emit signal when execution starts (connects parsing system to state management)
    emit executionStarted(scriptName);
    
    qDebug() << "Script loaded (signal/slot architecture handles execution):" << scriptName;
    
    // Get cached logical lines from ErbLoader
    QList<LogicalLine> logicalLines = m_erbLoader.getLogicalLinesCI(scriptName);
    
    if (logicalLines.isEmpty()) {
        setError("No logical lines found for script: " + scriptName);
        m_running = false;
        emit executionFinished();
        return false;
    }
    
    qDebug() << "Execution will be driven by signal/slot chain for script:" << scriptName;
    
    // Note: Actual execution is now driven by ParseTable's executeInstruction signals
    // ExecutionEngine just receives and executes via receiveInstruction()
    
    return true;
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
    qDebug() << "handleGoto called for label:" << label;
    // Get label position in current script
    int targetLine = getLabelPosition(label);
    
    qDebug() << "Target line for" << label << ":" << targetLine;
    
    if (targetLine < 0) {
        setError("Label not found: " + label);
        return false;
    }
    
    qDebug() << "GOTO label:" << label << "-> line" << targetLine 
             << "function count:" << m_state.getFunctionCount();
    
    // GOTO should NOT push to function stack - it's just a jump
    // Only CALL should push to the function stack
    
    // Update execution position to target line
    m_executionPosition = targetLine;
    
    // Restart execution from the target line
    restartFromLine(targetLine);
    
    return true;
}

bool ExecutionEngine::handleCall(const QString& label) {
    qDebug() << "handleCall called for label:" << label;
    
    // Get label position
    int targetLine = getLabelPosition(label);
    
    if (targetLine < 0) {
        setError("Label not found for CALL: " + label);
        return false;
    }
    
    qDebug() << "CALL target line for" << label << ":" << targetLine;
    
    // Record return address (current position + 1)
    // The return address is the line AFTER the CALL instruction
    int rawReturnPos = m_executionPosition + 1;
    ScriptPosition returnPos(m_currentScript, rawReturnPos, 0);
    
    qDebug() << "Return address (raw calc:" << m_executionPosition << " + 1 =" << rawReturnPos << ")";
    
    // Push function to stack with return address
    m_state.intoFunction(label, returnPos);
    
    qDebug() << "After intoFunction, function count:" << m_state.getFunctionCount();
    
    // Update execution position
    m_executionPosition = targetLine;
    
    // Restart execution from target line
    restartFromLine(targetLine);
    
    qDebug() << "CALL label:" << label << "-> line" << targetLine 
             << "return position:" << (m_executionPosition - 1)
             << "function count:" << m_state.getFunctionCount();
    
    return true;
}

bool ExecutionEngine::handleReturn() {
    qDebug() << "handleReturn called, function count:" << m_state.getFunctionCount();
    
    if (m_state.getFunctionCount() <= 0) {
        qDebug() << "RETURN without function call - ignoring";
        return true;  // No function to return from
    }
    
    // Get the return address BEFORE popping (from the function we're returning from)
    ScriptPosition returnPos = m_state.getCurrentFunctionPosition();
    int returnAddress = returnPos.lineNumber;
    QString returnScript = returnPos.filename;
    
    qDebug() << "Return address from current function:" << returnAddress << "script:" << returnScript;
    
    // Pop from function stack
    m_state.returnF();
    
    qDebug() << "After RETURN, function count:" << m_state.getFunctionCount();
    
    if (m_state.getFunctionCount() > 0) {
        // Return to caller - use the saved return address
        qDebug() << "Returning to line:" << returnAddress << "script:" << returnScript;
        
        // Switch to the return script if it's different
        if (returnScript != m_currentScript && !returnScript.isEmpty()) {
            qDebug() << "Switching back to script:" << returnScript;
            // We need to find the script name in our loaded scripts
            // For now, just use the return script directly
            // In a more complete implementation, we'd have a script manager
        }
        
        m_executionPosition = returnAddress;
        restartFromLine(returnAddress);
    } else {
        // All functions returned, but we should continue executing
        // from the return address (this handles RETURN at the end of a function)
        qDebug() << "All functions returned, continuing from return address:" << returnAddress << "script:" << returnScript;
        
        m_executionPosition = returnAddress;
        restartFromLine(returnAddress);
    }
    
    return true;
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

// FOR loop control - handles FOR LOCAL, start, end
bool ExecutionEngine::handleFor(const QString& varName, qint64 start, qint64 end) {
    qDebug() << "handleFor: var=" << varName << "start=" << start << "end=" << end;
    
    ForLoopState state;
    state.variableName = varName;
    state.startValue = start;
    state.endValue = end;
    state.currentValue = start;
    state.loopLine = m_executionPosition;
    
    // For now, store in a global-like variable using T series
    // In a full implementation, this would use a proper local variable scope
    QString storageVarName = varName;
    if (storageVarName.startsWith('%')) storageVarName = storageVarName.mid(1);
    if (storageVarName.endsWith('%')) storageVarName.chop(1);
    
    // Store in T series (integer variables)
    m_storage->setGlobalInt1D(storageVarName, 0, state.currentValue);
    
    m_forLoopStack.append(state);
    qDebug() << "FOR loop pushed, stack size:" << m_forLoopStack.size();
    
    return true;
}

// NEXT loop control - handles NEXT variable
bool ExecutionEngine::handleNext(const QString& varName) {
    qDebug() << "handleNext: var=" << varName << "stack size:" << m_forLoopStack.size();
    
    if (m_forLoopStack.isEmpty()) {
        qDebug() << "NEXT without FOR - ignoring";
        return true;
    }
    
    // Find the matching FOR loop (for nested loops)
    ForLoopState* state = nullptr;
    for (int i = m_forLoopStack.size() - 1; i >= 0; --i) {
        if (m_forLoopStack[i].variableName == varName) {
            state = &m_forLoopStack[i];
            break;
        }
    }
    
    if (!state) {
        qDebug() << "NEXT for unknown variable:" << varName;
        return true;
    }
    
    // Increment the loop variable
    state->currentValue++;
    
    qDebug() << "  Incremented currentValue to:" << state->currentValue;
    
    // Check if we've reached the end
    if (state->currentValue > state->endValue) {
        // Loop ended, pop from stack
        m_forLoopStack.removeAt(m_forLoopStack.indexOf(*state));
        qDebug() << "  Loop ended, stack size:" << m_forLoopStack.size();
    } else {
        // Loop continues, jump back to FOR line
        qDebug() << "  Loop continues, jumping back to line:" << state->loopLine;
        
        // Update the loop variable in storage
        QString storageVarName = state->variableName;
        if (storageVarName.startsWith('%')) storageVarName = storageVarName.mid(1);
        if (storageVarName.endsWith('%')) storageVarName.chop(1);
        m_storage->setGlobalInt1D(storageVarName, 0, state->currentValue);
        
        // Restart from the FOR line
        restartFromLine(state->loopLine);
    }
    
    return true;
}

// LOOP control - handles LOOP [condition]
bool ExecutionEngine::handleLoop(bool condition) {
    qDebug() << "handleLoop: condition=" << condition << "stack size:" << m_forLoopStack.size();
    
    if (m_forLoopStack.isEmpty()) {
        qDebug() << "LOOP without DO - ignoring";
        return true;
    }
    
    // For DO...LOOP without condition (infinite loop)
    if (!condition) {
        // Get the DO line position (stored in the loop stack)
        ForLoopState& state = m_forLoopStack.last();
        qDebug() << "  Infinite loop, jumping back to DO line";
        restartFromLine(state.loopLine);
        return true;
    }
    
    // For DO...LOOP WHILE condition
    // The condition is checked at the end, so if true, we loop
    if (condition) {
        ForLoopState& state = m_forLoopStack.last();
        qDebug() << "  Condition true, jumping back to DO line";
        restartFromLine(state.loopLine);
    }
    // If condition is false, exit the loop (do nothing)
    
    // Pop the loop from stack (we're done with this DO...LOOP)
    m_forLoopStack.removeLast();
    
    return true;
}

int ExecutionEngine::getLabelPosition(const QString& label) {
    // Remove $ prefix if present
    QString labelName = label;
    if (labelName.startsWith('$')) {
        labelName = labelName.mid(1);
    }
    
    // First, try to find label in current script
    int pos = m_erbLoader.getLabelPosition(m_currentScript, labelName);
    if (pos >= 0) {
        qDebug() << "Found label" << labelName << "in current script" << m_currentScript << "at position" << pos;
        return pos;
    }
    
    // If not found in current script, search all loaded scripts
    qDebug() << "Label" << labelName << "not found in" << m_currentScript << ", searching all scripts...";
    auto scripts = m_erbLoader.getLoadedScripts();
    for (auto it = scripts.begin(); it != scripts.end(); ++it) {
        pos = m_erbLoader.getLabelPosition(it.key(), labelName);
        if (pos >= 0) {
            qDebug() << "Found label" << labelName << "in script" << it.key() << "at position" << pos;
            // Update current script when label found in different script
            m_currentScript = it.key();
            return pos;
        }
    }
    
    // Label not found
    qDebug() << "Label" << labelName << "not found in any script!";
    return -1;
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

int ExecutionEngine::getExecutionQueueSize() const {
    return m_executionQueue.size();
}

int ExecutionEngine::getTotalInstructionsExecuted() const {
    return m_totalInstructionsExecuted;
}

bool ExecutionEngine::loadScripts(const QString& scriptDir) {
    return m_erbLoader.loadDirectory(scriptDir, 0);
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

bool ExecutionEngine::resolveLabelWithPosition(const QString& label, ScriptLine*& line, int& position) {
    // Remove $ prefix if present (era format uses $ for labels)
    QString labelName = label;
    if (labelName.startsWith('$')) {
        labelName = labelName.mid(1);
    }
    
    line = m_erbLoader.findLabel(labelName);
    if (line) {
        position = getLabelPosition(labelName);
        return true;
    }
    return false;
}

bool ExecutionEngine::executeInstruction(const InstructionData& data) {
    qDebug() << "Executing instruction:" << data.name << "m_executionPosition:" << m_executionPosition;
    m_totalInstructionsExecuted++;
    m_executionPosition++;  // Increment position after each instruction
    
    bool result = true;
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
    else if (data.name == "FOR") {
        // FOR instruction - for FOR...NEXT loops
        // Format: FOR LOCAL, start, end
        if (data.arguments.size() >= 3) {
            QString varName = data.arguments[0].value;
            // Extract variable name (remove % if present)
            if (varName.startsWith('%')) varName = varName.mid(1);
            if (varName.endsWith('%')) varName.chop(1);
            
            // Get start and end values
            bool startOk = false, endOk = false;
            qint64 start = data.arguments[1].value.toLongLong(&startOk);
            qint64 end = data.arguments[2].value.toLongLong(&endOk);
            
            if (startOk && endOk) {
                return handleFor(varName, start, end);
            }
        }
        return true;
    }
    else if (data.name == "CALL") {
        // CALL instruction - calls a label/subroutine
        if (data.arguments.size() >= 1) {
            QString label = data.arguments[0].value;
            // Remove parentheses if present (e.g., INIT_STAGE() -> INIT_STAGE)
            label.remove(QRegularExpression(R"(\(\s*\)$)"));
            return handleCall(label);
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
    else if (data.name == "REPEAT") {
        // REPEAT - for REPEAT...ENDREPEAT loops
        // REPEAT count - execute the following instructions count times
        if (data.arguments.size() >= 1) {
            bool countOk = false;
            int count = data.arguments[0].value.toInt(&countOk);
            if (countOk && count > 0) {
                // Push loop state onto stack
                // loopLine points to the REPEAT instruction
                // The loop body starts at the NEXT line (loopLine + 1)
                RepeatLoopState state;
                state.originalCount = count;
                state.remainingCount = count;
                state.loopLine = m_executionPosition;  // REPEAT line
                state.bodyStartLine = m_executionPosition - 1;  // First line of loop body (current position - 1 to get the line AFTER REPEAT)
                m_repeatLoopStack.append(state);
                qDebug() << "REPEAT loop pushed, count:" << count << "stack size:" << m_repeatLoopStack.size()
                         << "loopLine:" << state.loopLine << "bodyStartLine:" << state.bodyStartLine
                         << "m_executionPosition:" << m_executionPosition
                         << "currentScript:" << m_currentScript;
            }
        }
        return true;
    }
    else if (data.name == "ENDREPEAT") {
        // ENDREPEAT - end of REPEAT loop
        if (m_repeatLoopStack.isEmpty()) {
            qDebug() << "ENDREPEAT without REPEAT - ignoring";
            return true;
        }
        
        // Decrement the counter
        RepeatLoopState& state = m_repeatLoopStack.last();
        state.remainingCount--;
        
        qDebug() << "ENDREPEAT: remainingCount:" << state.remainingCount << "loopLine:" << state.loopLine << "bodyStartLine:" << state.bodyStartLine;
        
        if (state.remainingCount > 0) {
            // Loop continues - re-execute the body
            qDebug() << "ENDREPEAT: loop continues, remaining:" << state.remainingCount;
            
            // Get the logical lines for the current script
            QList<LogicalLine> logicalLines = m_erbLoader.getLogicalLinesCI(m_currentScript);
            
            // Rebuild the queue with the body
            m_executionQueue.clear();
            
            // Add body lines from bodyStartLine until we reach the ENDREPEAT
            for (int i = state.bodyStartLine; i < logicalLines.size(); i++) {
                const LogicalLine& line = logicalLines[i];
                const QList<ScriptLine>& scriptLines = line.scriptLines();
                
                // Check if this is ENDREPEAT
                bool isEndRepeat = false;
                for (const ScriptLine& scriptLine : scriptLines) {
                    if (scriptLine.type() == ScriptLineType::Instruction) {
                        if (scriptLine.instructionData().name == "ENDREPEAT") {
                            isEndRepeat = true;
                            break;
                        }
                    }
                }
                
                m_executionQueue.enqueue(line);
                
                // Stop after adding ENDREPEAT
                if (isEndRepeat) {
                    break;
                }
            }
            
            qDebug() << "Loop queue rebuilt, size:" << m_executionQueue.size() << "currentScript:" << m_currentScript;
            
            // Debug: print logicalLines size
            qDebug() << "logicalLines.size():" << logicalLines.size();
            for (int i = state.bodyStartLine; i < qMin(state.bodyStartLine + 5, logicalLines.size()); i++) {
                const LogicalLine& line = logicalLines[i];
                const QList<ScriptLine>& scriptLines = line.scriptLines();
                qDebug() << "  logicalLines[" << i << "]:" << scriptLines[0].instructionData().name;
            }
            
            // Debug: print queue contents
            qDebug() << "Queue contents:";
            QQueue<LogicalLine> tempQueue = m_executionQueue;
            while (!tempQueue.isEmpty()) {
                const LogicalLine& line = tempQueue.dequeue();
                const QList<ScriptLine>& scriptLines = line.scriptLines();
                for (const ScriptLine& scriptLine : scriptLines) {
                    if (scriptLine.type() == ScriptLineType::Instruction) {
                        qDebug() << "  -" << scriptLine.instructionData().name;
                    }
                }
            }
            
            // Execute the body
            while (!m_executionQueue.isEmpty()) {
                LogicalLine line = m_executionQueue.dequeue();
                executeLogicalLine(line);
            }
        } else {
            // Loop ended, pop from stack
            m_repeatLoopStack.removeLast();
            qDebug() << "ENDREPEAT: loop ended, stack size:" << m_repeatLoopStack.size();
        }
        return true;
    }
    else if (data.name == "LOOP") {
        // LOOP - for DO...LOOP construct
        // Evaluate the condition if present
        bool condition = true;
        if (data.arguments.size() >= 1) {
            // LOOP has a condition - evaluate it
            ExpressionEvaluator evaluator;
            QVariant result = evaluator.evaluate(data.arguments[0].value, m_storage, m_gameBaseData);
            condition = result.isValid() && result.toInt() != 0;
        }
        return handleLoop(condition);
    }
    else if (data.name == "NEXT") {
        // NEXT - for FOR...NEXT loops
        if (data.arguments.size() >= 1) {
            // NEXT variable - extract variable name
            QString varName = data.arguments[0].value;
            // Remove % prefix/suffix if present
            if (varName.startsWith('%')) varName = varName.mid(1);
            if (varName.endsWith('%')) varName.chop(1);
            return handleNext(varName);
        }
        return handleNext("");  // Empty name will be handled by handleNext
    }
    else if (data.name == "RETURN") {
        // RETURN - return from a function call
        return handleReturn();
    }
    else if (data.name == "SELECTCASE") {
        // SELECTCASE - for now, just continue (CASE handling not implemented)
        return true;
    }
    else if (data.name == "CASE") {
        // CASE - for now, just continue
        return true;
    }
    else if (data.name == "ENDSELECT") {
        // ENDSELECT - just continue
        return true;
    }
    else if (data.name == "INPUT") {
        // INPUT - wait for user input (for CLI testing, stop execution and set RESULT)
        // In Emuera, INPUT typically shows a menu and waits for user selection
        // For CLI testing, we'll stop execution and set RESULT to 0
        if (data.arguments.size() >= 1) {
            qDebug() << "INPUT with arguments (skipping debug output for InstructionArgument list)";
        }
        m_storage->setGlobalInt1D("RESULT", 0, 0);
        m_running = false;  // Stop execution to simulate waiting for input
        return true;
    }
    else if (data.name == "TONEINPUT") {
        // TONEINPUT - wait for input with timeout (for CLI testing, stop execution)
        if (data.arguments.size() >= 1) {
            m_storage->setGlobalInt1D("RESULT", 0, 0);
        }
        m_running = false;  // Stop execution to simulate waiting for input
        return true;
    }
    else if (data.name == "ONEINPUT") {
        // ONEINPUT - wait for single input (for CLI testing, stop execution)
        m_storage->setGlobalInt1D("RESULT", 0, 0);
        m_running = false;  // Stop execution to simulate waiting for input
        return true;
    }
    else if (data.name == "PRINTBUTTON") {
        if (data.arguments.size() >= 1) {
            QString text = data.arguments[0].value;
            if (data.arguments.size() >= 2) {
                QString key = data.arguments[1].value;
                qDebug() << "PRINTBUTTON:" << text << "(key:" << key << ")";
            } else {
                qDebug() << "PRINTBUTTON:" << text;
            }
        }
        return true;
    }
    else if (data.name == "CLEARLINE") {
        if (data.arguments.size() >= 1) {
            qDebug() << "CLEARLINE:" << data.arguments[0].value;
        } else {
            qDebug() << "CLEARLINE: 1 line";
        }
        return true;
    }
    else if (data.name == "REDRAW") {
        QString redrawValue = data.arguments.size() >= 1 ? data.arguments[0].value : "1";
        qDebug() << "REDRAW:" << redrawValue;
        return true;
    }
    else if (data.name == "RESETCOLOR") {
        qDebug() << "RESETCOLOR";
        return true;
    }
    else if (data.name == "BEGIN") {
        if (data.arguments.size() >= 1) {
            QString keyword = data.arguments[0].value;
            m_state.setBegin(keyword);
            qDebug() << "BEGIN:" << keyword;
            // Emit signal for system processor to handle state transition
            emit beginRequested(keyword);
        }
        return true;
    }
    else if (data.name == "SIF") {
        if (data.arguments.size() >= 1) {
            QString condition = data.arguments[0].value;
            ExpressionEvaluator evaluator;
            QVariant result = evaluator.evaluate(condition, m_storage, m_gameBaseData);
            bool isTrue = result.isValid() && result.toInt() != 0;
            if (isTrue) {
                return true;
            }
        }
        return true;
    }
    else if (data.name == "ALIGNMENT") {
        QString alignValue = data.arguments.size() >= 1 ? data.arguments[0].value : "LEFT";
        qDebug() << "ALIGNMENT:" << alignValue;
        return true;
    }
    else if (data.name == "DRAWLINE") {
        qDebug() << "DRAWLINE";
        return true;
    }
    else if (data.name == "SETCOLOR") {
        QString colorValue = data.arguments.size() >= 1 ? data.arguments[0].value : "default";
        qDebug() << "SETCOLOR:" << colorValue;
        return true;
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
    
    // Evaluate the RHS expression using ExpressionEvaluator
    // Pass m_gameBaseData if available so GameBase variables can be resolved
    ExpressionEvaluator evaluator;
    QVariant rhsValue = evaluator.evaluate(rhs, m_storage, m_gameBaseData);
    
    qDebug() << "  rhsValue=" << rhsValue.toString();
    
    // If the evaluated result is not valid, try direct conversion
    if (!rhsValue.isValid() || rhsValue.toString().isEmpty()) {
        bool ok = false;
        int intValue = rhs.toInt(&ok);
        if (ok) {
            rhsValue = QVariant(intValue);
        }
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
        QString value = arg.value;
        
        // If this is a variable reference, substitute its value
        if (arg.isVariable) {
            QString varName = value;
            
            // Remove trailing % if present (Emuera format uses %VAR%)
            if (varName.endsWith('%')) {
                varName.chop(1);
            }
            
            // Check if it's a GameBase variable (GAMEBASE_*)
            if (varName.startsWith("GAMEBASE_") && m_gameBaseData) {
                QString key = varName.mid(9);  // Remove "GAMEBASE_" prefix
                value = m_gameBaseData->get(key);
                if (value.isEmpty()) {
                    value = "{%" + varName + "%}";  // Keep original if not found
                }
            }
            // Check if it's a system variable ($)
            else if (varName.startsWith('$')) {
                // System variables not fully implemented yet
                value = "0";
            }
            // Check if it's a regular variable (T series)
            else {
                // Try to get as integer variable
                int intVal = m_storage->getGlobalInt1D(varName, 0);
                value = QString::number(intVal);
            }
        }
        
        text += value + " ";
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

void ExecutionEngine::restartFromLine(int linePosition) {
    if (linePosition < 0) {
        setError("Invalid line position: " + QString::number(linePosition));
        return;
    }
    
    // Get cached logical lines from ErbLoader
    QList<LogicalLine> logicalLines = m_erbLoader.getLogicalLinesCI(m_currentScript);
    
    if (logicalLines.isEmpty()) {
        setError("No logical lines found for script: " + m_currentScript);
        return;
    }
    
    qDebug() << "restartFromLine:" << linePosition << "logicalLines.size():" << logicalLines.size() << "current queue size:" << m_executionQueue.size();
    
    // Clear the execution queue
    m_executionQueue.clear();
    
    // Add lines from the specified position onwards
    for (int i = linePosition; i < logicalLines.size(); ++i) {
        m_executionQueue.enqueue(logicalLines[i]);
    }
    
    m_executionPosition = linePosition;
    qDebug() << "Restarted execution from line" << linePosition << "in script" << m_currentScript << "new queue size:" << m_executionQueue.size();
}

void ExecutionEngine::setError(const QString& message) {
    qDebug() << "Execution error:" << message;
    m_running = false;
    emit errorOccurred(message);
}

void ExecutionEngine::stopExecution() {
    m_running = false;
}

bool ExecutionEngine::handleFunctionCall(const QString& name, const QList<QString>& args) {
    QList<QVariant> variantArgs;
    for (const QString& arg : args) {
        variantArgs.append(arg);
    }
    
    ScriptPosition pos(m_currentScript, m_currentLine, 0);
    FunctionResult result = m_functionSystem->executeFunction(name, variantArgs, m_storage, pos);
    
    if (result.success) {
        emit functionResult(name, result.value, "");
    } else {
        setError("Function " + name + " error: " + result.error);
    }
    
    return result.success;
}

// Receive instruction from ParseTable
void ExecutionEngine::receiveInstruction(const LogicalLine& line) {
    executeLogicalLine(line);
    
    // Emit signal that instruction was executed
    emit instructionExecuted(m_currentScript, m_currentLine, true);
}

// Handle jump request from ParseTable
void ExecutionEngine::handleJumpRequest(const QString& targetScript, const QString& label, int targetPosition) {
    qDebug() << "handleJumpRequest: targetScript=" << targetScript << "label=" << label << "position=" << targetPosition;
    
    // Jump to the target script and position
    m_currentScript = targetScript;
    m_currentLine = targetPosition;
    
    // Get the logical lines for the target script
    QList<LogicalLine> logicalLines = m_erbLoader.getLogicalLinesCI(targetScript);
    
    // Rebuild the execution queue from the target position
    m_executionQueue.clear();
    for (int i = targetPosition; i < logicalLines.size(); ++i) {
        m_executionQueue.enqueue(logicalLines[i]);
    }
    
    qDebug() << "Jumped to script" << targetScript << "position" << targetPosition << "queue size:" << m_executionQueue.size();
}

// Handle memory space change from ParseTable
void ExecutionEngine::handleMemorySpaceChange(const QString& scriptName) {
    qDebug() << "handleMemorySpaceChange: switching to script" << scriptName;
    m_currentScript = scriptName;
}
