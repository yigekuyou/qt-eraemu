/*
 * emuera —— Emuera（ERB 脚本引擎）的 Qt6 + QML/C++ 移植
 * Copyright (C) 2026  yigekuyou
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "execution_engine.h"
#include <QDebug>
#include "expression_evaluator.h"
#include "ast/expression_ast.h"
#include "era_parse_table.h"
#include "eraengine.h"
#include "function_system.h"

namespace {

// 优先使用 EraParseTable 缓存的 AST 求值（AST 单一解析流水线）；
// 未命中再回退到字符串入口。这样执行侧不再“二次解析”表达式。
QVariant evalExpressionCached(EraParseTable* table,
                              ExpressionEvaluator& evaluator,
                              const QString& expr,
                              VariableStorage* storage,
                              GameBaseData* gameBaseData) {
    if (table) {
        const QSharedPointer<ExpressionNode> ast = table->expressionAst(expr);
        if (ast) {
            return evaluator.evaluate(*ast, storage, gameBaseData);
        }
    }
    return evaluator.evaluate(expr, storage, gameBaseData);
}

} // namespace

ExecutionEngine::ExecutionEngine(VariableStorage* storage, GameBaseData* gameBaseData, QObject* parent)
    : QObject(parent), m_storage(storage), m_gameBaseData(gameBaseData), m_functionSystem(nullptr), m_running(false), m_currentLine(0), m_executionPosition(0), m_totalInstructionsExecuted(0) {
    connect(&m_erbLoader, &ErbLoader::objectNameChanged, this, &ExecutionEngine::objectNameChanged);

    // ParseTable reference (installed later via setParseTable)
    m_parseTable = nullptr;

    // Initialize function system
    m_functionSystem = new FunctionSystem(this);
    
    // Initialize repeat loop stack
    // m_repeatLoopStack is automatically initialized by QList
    
    // Initialize jump flag
    m_jumpOccurred = false;
    
    // Initialize last WHILE line
    m_lastWhileLine = -1;
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
    // 完整 AST：一行即一个 LogicalLine。
    if (line.kind == LineKind::FunctionLabel || line.kind == LineKind::GotoLabel) {
        // 标签只是跳转目标，不执行
        return;
    }
    if (line.kind != LineKind::Instruction) {
        return;   // 空行 / 注释 / 预处理指令
    }

    qDebug() << "[executeLogicalLine]   Executing Instruction:" << line.functionName;
    if (!executeInstruction(line)) {
        qDebug() << "[executeLogicalLine]   executeInstruction returned false";
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

bool ExecutionEngine::handleWhile(const QString& condition) {
    qDebug() << "handleWhile ENTER: condition=" << condition << "stack size:" << m_whileLoopStack.size()
             << "lastWhileLine=" << m_lastWhileLine << "m_executionPosition=" << m_executionPosition;
    
    // Check if we're re-entering a WHILE loop (same line, already in stack)
    // Note: m_executionPosition was already incremented in executeInstruction,
    // so we check against m_executionPosition - 1 (the actual line position)
    int currentWhilePos = m_executionPosition - 1;
    bool reEntry = false;
    
    qDebug() << "  Checking re-entry: currentWhilePos=" << currentWhilePos << "stack size=" << m_whileLoopStack.size();
    int i = 0;
    for (const WhileLoopState& state : m_whileLoopStack) {
        qDebug() << "    State[" << i << "]: loopLine=" << state.loopLine << ", condition=" << state.condition << ", entered=" << state.entered;
        if (state.loopLine == currentWhilePos) {
            reEntry = true;
            qDebug() << "    -> MATCH! Re-entry detected.";
            break;
        }
        i++;
    }
    
    if (!reEntry) {
        // First time entering this WHILE - push to stack
        WhileLoopState state;
        // Note: m_executionPosition was already incremented in executeInstruction,
        // so we need to use m_executionPosition - 1 as the loop line
        state.loopLine = currentWhilePos;  // WHILE line (0-indexed)
        state.condition = condition;  // Save condition for re-evaluation
        state.entered = true;  // Mark as entered
        
        m_whileLoopStack.append(state);
        qDebug() << "WHILE loop pushed, stack size:" << m_whileLoopStack.size()
                 << "loopLine:" << state.loopLine
                 << "condition:" << condition;
    } else {
        // Re-entry from WEND - don't push again
        qDebug() << "WHILE re-entry (same line as existing WHILE), not pushing to stack";
    }
    
    // Update last WHILE line for next check (use the position BEFORE increment)
    m_lastWhileLine = m_executionPosition - 1;
    
    qDebug() << "handleWhile EXIT: loopLine=" << m_lastWhileLine;
    
    // The WHILE instruction itself is at m_executionPosition
    // The loop body starts at the NEXT line (m_executionPosition + 1)
    return true;
}

bool ExecutionEngine::handleWend() {
    qDebug() << "handleWend: stack size:" << m_whileLoopStack.size();
    
    if (m_whileLoopStack.isEmpty()) {
        qDebug() << "WEND without WHILE - ignoring";
        return true;
    }
    
    // Re-evaluate the condition using ExpressionEvaluator
    // Note: We DON'T pop from stack in this function - that happens after we know the condition is false
    
    // Find the matching state in the stack (it should be the last one)
    WhileLoopState state;
    if (!m_whileLoopStack.isEmpty()) {
        state = m_whileLoopStack.last();
    } else {
        qDebug() << "WEND without WHILE - ignoring";
        return true;
    }
    
    // Re-evaluate the condition using ExpressionEvaluator
    ExpressionEvaluator evaluator;
    QVariant result = evaluator.evaluate(state.condition, m_storage, m_gameBaseData);
    bool conditionMet = result.isValid() && result.toInt() != 0;
    
    qDebug() << "WEND: condition=" << state.condition << "result=" << result.toString() 
             << "conditionMet=" << conditionMet << "loopLine:" << state.loopLine
             << "stackSize:" << m_whileLoopStack.size()
             << "A=" << m_storage->getGlobalInt1D("A", -1) << "B=" << m_storage->getGlobalInt1D("B", -1);
    
    if (conditionMet) {
        // Loop continues - jump back to WHILE line
        // Note: The WHILE instruction is at state.loopLine, so we need to re-execute it
        // We use the ParseTable's jumpToLine to properly update position
        qDebug() << "WEND: loop continues, jumping back to line:" << state.loopLine;
        
        if (m_parseTable) {
            // Use ParseTable's jumpToLine to properly update position
            m_parseTable->jumpToLine(state.loopLine);
            // Update our execution position to match
            m_executionPosition = state.loopLine;
            // Mark that a jump occurred (for test compatibility)
            m_jumpOccurred = true;
            // Clear lastWhileLine so the WHILE at this line is treated as new entry
            m_lastWhileLine = -1;
        } else {
            // Fallback: just set the position
            m_executionPosition = state.loopLine;
            m_lastWhileLine = -1;
        }
    } else {
        // Loop ended - pop from stack and clear last WHILE line
        if (!m_whileLoopStack.isEmpty()) {
            m_whileLoopStack.takeLast();
        }
        m_lastWhileLine = -1;
        qDebug() << "WEND: loop ended, popped from stack";
    }
    
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
    // 手工解析 LHS：VARIABLE 或 VARIABLE:index（不再使用正则）。
    const QString trimmed = lhs.trimmed();
    const int colon = trimmed.lastIndexOf(':');
    if (colon > 0) {
        const QString name = trimmed.left(colon).trimmed();
        bool ok = false;
        const int index = trimmed.mid(colon + 1).trimmed().toInt(&ok);
        if (ok && !name.isEmpty()) {
            return qMakePair(name, index);
        }
    }
    return qMakePair(trimmed, -1);
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
    
    // Evaluate the RHS expression (prefer cached AST)
    ExpressionEvaluator localEvaluator;
    ExpressionEvaluator& evaluator = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
    const QVariant rhsVar = evalExpressionCached(m_parseTable, evaluator, rhs, m_storage, m_gameBaseData);
    int rhsValue = rhsVar.isValid() ? rhsVar.toInt() : rhs.toInt();
    
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

QHash<QString, QList<LogicalLine>> ExecutionEngine::getLoadedScripts() const {
    return m_erbLoader.getLoadedScripts();
}



bool ExecutionEngine::executeInstruction(const LogicalLine& line) {
    const QString& name = line.functionName;
    const QList<Operand>& args = line.arguments;
    qDebug() << "Executing instruction:" << name << "m_executionPosition:" << m_executionPosition;
    m_totalInstructionsExecuted++;
    m_executionPosition++;  // Increment position after each instruction
    
    bool result = true;
    if (name == "GOTO") {
        if (args.size() >= 1) {
            QString label = args[0].raw;
            return handleGoto(label);
        }
    }
    else if (name == "IF") {
        // IF instruction - for now, just evaluate condition and continue
        // Full IF/ELSEIF/ELSE/ENDIF block parsing is not yet implemented
        if (args.size() >= 1) {
            QString condition = args[0].raw;
            // Just evaluate the condition for now
            ExpressionEvaluator evaluator;
            evaluator.evaluate(condition, m_storage, nullptr);
        }
        return true;
    }
    else if (name == "ELSEIF") {
        // ELSEIF - skip for now, just continue execution
        return true;
    }
    else if (name == "ELSE") {
        // ELSE - skip for now, just continue execution
        return true;
    }
    else if (name == "ENDIF") {
        // ENDIF - just continue, no action needed
        return true;
    }
    else if (name == "FOR") {
        // FOR instruction - for FOR...NEXT loops
        // Format: FOR LOCAL, start, end
        if (args.size() >= 3) {
            QString varName = args[0].raw;
            // Extract variable name (remove % if present)
            if (varName.startsWith('%')) varName = varName.mid(1);
            if (varName.endsWith('%')) varName.chop(1);
            
            // Get start and end values
            bool startOk = false, endOk = false;
            qint64 start = args[1].raw.toLongLong(&startOk);
            qint64 end = args[2].raw.toLongLong(&endOk);
            
            if (startOk && endOk) {
                return handleFor(varName, start, end);
            }
        }
        return true;
    }
    else if (name == "CALL") {
        // CALL instruction - calls a label/subroutine
        if (args.size() >= 1) {
            QString label = args[0].raw;
            // Remove trailing parentheses if present (e.g., INIT_STAGE() -> INIT_STAGE)
            const int paren = label.indexOf('(');
            if (paren >= 0) {
                label = label.left(paren);
            }
            label = label.trimmed();
            return handleCall(label);
        }
        return true;
    }
    else if (name == "RESETDATA") {
        return handleResetData();
    }
    else if (name == "LOADGLOBAL") {
        return handleLoadGlobal();
    }
    else if (name == "PRINTFORM" || name == "PRINTFORMS") {
        return handlePrintForm(args, false);
    }
    else if (name == "PRINTFORML" || name == "PRINTFORMW" || name == "PRINTFORMSL"
             || name == "PRINTFORMSW" || name == "PRINTFORMC" || name == "PRINTFORMLC") {
        return handlePrintForm(args, true);
    }
    else if (name == "PRINTFORMC" || name == "PRINTC") {
        return handlePrint(args, false);
    }
    else if (name == "PRINT") {
        return handlePrint(args, false);
    }
    else if (name == "PRINTL") {
        return handlePrint(args, true);
    }
    else if (name == "=") {
        qDebug() << "Handling assignment:" << args[0].raw << "=" << args[1].raw;
        // Simple assignment: VARIABLE = value
        if (args.size() >= 2) {
            QString lhs = args[0].raw;
            QString rhs = args[1].raw;
            return handleAssignment(lhs, rhs);
        }
    }
    else if (name == "+=" || name == "-=" || name == "*=" || name == "/=") {
        // Compound assignment
        if (args.size() >= 2) {
            QString lhs = args[0].raw;
            QString rhs = args[1].raw;
            return handleCompoundAssignment(lhs, name, rhs);
        }
    }
    else if (name == "REPEAT") {
        // REPEAT - for REPEAT...ENDREPEAT loops
        // REPEAT count - execute the following instructions count times
        if (args.size() >= 1) {
            bool countOk = false;
            int count = args[0].raw.toInt(&countOk);
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
    else if (name == "ENDREPEAT") {
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
                const LogicalLine& body = logicalLines[i];
                const bool isEndRepeat = body.is("ENDREPEAT");

                m_executionQueue.enqueue(body);
                
                // Stop after adding ENDREPEAT
                if (isEndRepeat) {
                    break;
                }
            }
            
            qDebug() << "Loop queue rebuilt, size:" << m_executionQueue.size() << "currentScript:" << m_currentScript;
            
            // Debug: print logicalLines size
            qDebug() << "logicalLines.size():" << logicalLines.size();
            for (int i = state.bodyStartLine; i < qMin(state.bodyStartLine + 5, logicalLines.size()); i++) {
                qDebug() << "  logicalLines[" << i << "]:" << logicalLines[i].functionName;
            }
            
            // Debug: print queue contents
            qDebug() << "Queue contents:";
            QQueue<LogicalLine> tempQueue = m_executionQueue;
            while (!tempQueue.isEmpty()) {
                qDebug() << "  -" << tempQueue.dequeue().functionName;
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
    else if (name == "LOOP") {
        // LOOP - for DO...LOOP construct
        // Evaluate the condition if present
        bool condition = true;
        if (args.size() >= 1) {
            // LOOP has a condition - evaluate it
            ExpressionEvaluator evaluator;
            QVariant result = evaluator.evaluate(args[0].raw, m_storage, m_gameBaseData);
            condition = result.isValid() && result.toInt() != 0;
        }
        return handleLoop(condition);
    }
    else if (name == "NEXT") {
        // NEXT - for FOR...NEXT loops
        if (args.size() >= 1) {
            // NEXT variable - extract variable name
            QString varName = args[0].raw;
            // Remove % prefix/suffix if present
            if (varName.startsWith('%')) varName = varName.mid(1);
            if (varName.endsWith('%')) varName.chop(1);
            return handleNext(varName);
        }
        return handleNext("");  // Empty name will be handled by handleNext
    }
    else if (name == "RETURN") {
        // RETURN - return from a function call
        return handleReturn();
    }
    else if (name == "SELECTCASE") {
        // SELECTCASE - for now, just continue (CASE handling not implemented)
        return true;
    }
    else if (name == "CASE") {
        // CASE - for now, just continue
        return true;
    }
    else if (name == "ENDSELECT") {
        // ENDSELECT - just continue
        return true;
    }
    else if (name == "INPUT") {
        // INPUT - wait for user input (for CLI testing, stop execution and set RESULT)
        // In Emuera, INPUT typically shows a menu and waits for user selection
        // For CLI testing, we'll stop execution and set RESULT to 0
        if (args.size() >= 1) {
            qDebug() << "INPUT with arguments (skipping debug output for Operand list)";
        }
        m_storage->setGlobalInt1D("RESULT", 0, 0);
        m_running = false;  // Stop execution to simulate waiting for input
        return true;
    }
    else if (name == "TONEINPUT") {
        // TONEINPUT - wait for input with timeout (for CLI testing, stop execution)
        if (args.size() >= 1) {
            m_storage->setGlobalInt1D("RESULT", 0, 0);
        }
        m_running = false;  // Stop execution to simulate waiting for input
        return true;
    }
    else if (name == "ONEINPUT") {
        // ONEINPUT - wait for single input (for CLI testing, stop execution)
        m_storage->setGlobalInt1D("RESULT", 0, 0);
        m_running = false;  // Stop execution to simulate waiting for input
        return true;
    }
    else if (name == "PRINTBUTTON") {
        if (args.size() >= 1) {
            QString text = args[0].raw;
            if (args.size() >= 2) {
                QString key = args[1].raw;
                qDebug() << "PRINTBUTTON:" << text << "(key:" << key << ")";
            } else {
                qDebug() << "PRINTBUTTON:" << text;
            }
        }
        return true;
    }
    else if (name == "CLEARLINE") {
        int n = 1;
        if (args.size() >= 1) {
            n = args[0].raw.toInt();
            if (n <= 0) n = 1;
        }
        emit consoleClearLines(n);
        return true;
    }
    else if (name == "REDRAW") {
        const QString redrawValue = args.size() >= 1 ? args[0].raw : "1";
        emit consoleRedraw(redrawValue);
        return true;
    }
    else if (name == "RESETCOLOR") {
        emit consoleResetColor();
        return true;
    }
    else if (name == "SETCOLOR") {
        if (args.size() >= 1) {
            emit consoleColor(args[0].raw);
        }
        return true;
    }
    else if (name == "BEGIN") {
        if (args.size() >= 1) {
            QString keyword = args[0].raw;
            m_state.setBegin(keyword);
            qDebug() << "BEGIN:" << keyword;
            // Emit signal for system processor to handle state transition
            emit beginRequested(keyword);
        }
        return true;
    }
    else if (name == "SIF") {
        if (args.size() >= 1) {
            QString condition = args[0].raw;
            ExpressionEvaluator evaluator;
            QVariant result = evaluator.evaluate(condition, m_storage, m_gameBaseData);
            bool isTrue = result.isValid() && result.toInt() != 0;
            if (isTrue) {
                return true;
            }
        }
        return true;
    }
    else if (name == "ALIGNMENT") {
        const QString alignValue = args.size() >= 1 ? args[0].raw : QStringLiteral("LEFT");
        emit consoleAlign(alignValue);
        return true;
    }
    else if (name == "DRAWLINE") {
        qDebug() << "DRAWLINE";
        return true;
    }
    else if (name == "SETCOLOR") {
        QString colorValue = args.size() >= 1 ? args[0].raw : "default";
        qDebug() << "SETCOLOR:" << colorValue;
        return true;
    }
    else if (name == "WHILE") {
        // WHILE condition - start a while loop
        if (args.size() >= 1) {
            QString condition = args[0].raw;
            return handleWhile(condition);
        }
        return true;  // No condition, skip the loop
    }
    else if (name == "WEND") {
        // WEND - end of while loop
        return handleWend();
    }
    else if (name == "+" || name == "-" || name == "*" || name == "/" || name == "%") {
        // Compound assignment operators: A + 1 means A = A + 1
        if (args.size() >= 2) {
            QString lhs = args[0].raw;
            QString rhs = args[1].raw;
            // Build expression: lhs + rhs (e.g., "A + 1")
            QString expr = lhs + " " + name + " " + rhs;
            return handleAssignment(lhs, expr);
        }
        return true;
    }
    
    // Default: log instruction
    qDebug() << "Executing instruction:" << name << "(name=" << name << ")";
    return true;
}

bool ExecutionEngine::handleAssignment(const QString& lhs, const QString& rhs) {
    qDebug() << "handleAssignment: lhs=" << lhs << "rhs=" << rhs;
    
    // Parse the LHS to get variable name and index
    auto [varName, index] = parseLHS(lhs);
    
    qDebug() << "  varName=" << varName << "index=" << index;
    
    // Evaluate the RHS expression using the parse table's cached AST.
    // Pass m_gameBaseData if available so GameBase variables can be resolved.
    ExpressionEvaluator localEvaluator;
    ExpressionEvaluator& evaluator = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
    QVariant rhsValue = evalExpressionCached(m_parseTable, evaluator, rhs, m_storage, m_gameBaseData);
    
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

bool ExecutionEngine::handlePrint(const QList<Operand>& args, bool newline) {
    QString text;
    for (const Operand& arg : args) {
        QString value = arg.raw;
        
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
    // 输出到显示层（ConsoleBackend）；换行由 *L 系决定
    emit consolePrint(text, newline);
    return true;
}

bool ExecutionEngine::handlePrintForm(const QList<Operand>& args, bool newline) {
    ExpressionEvaluator localEvaluator;
    ExpressionEvaluator& evaluator = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;

    QString text;
    for (const Operand& arg : args) {
        if (arg.ast) {
            // StrForm AST：文本 + 内嵌表达式
            text += evaluator.evaluate(*arg.ast, m_storage, m_gameBaseData).toString();
        } else {
            text += arg.raw;
        }
    }
    emit consolePrint(text, newline);
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
