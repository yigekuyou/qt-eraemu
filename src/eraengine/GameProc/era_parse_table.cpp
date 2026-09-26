#include "era_parse_table.h"
#include "process_state.h"
#include "variable_storage.h"
#include "execution_engine.h"
#include <QDebug>
#include <QFileInfo>
#include <QCoreApplication>
#include <QEventLoop>

EraParseTable::EraParseTable(ProcessState* state, ExecutionEngine* execEngine, QObject* parent)
    : QObject(parent)
    , m_state(state)
    , m_memorySpaceManager(new MemorySpaceManager(this))
    , m_currentMemorySpace(nullptr)
    , m_executionEngine(execEngine)
    // 位置区
    , m_currentScript()
    , m_currentLine(0)
    , m_callStack()
    , m_depth(0)
{
}

EraParseTable::EraParseTable(ProcessState* state, QObject* parent)
    : EraParseTable(state, nullptr, parent)
{
}

EraParseTable::~EraParseTable() {
    // MemorySpaceManager will be deleted automatically
}

void EraParseTable::setVariableStorage(VariableStorage* storage) {
    if (storage) {
        // Set variable storage for all memory spaces
        for (const QString& scriptName : m_executionQueues.keys()) {
            m_memorySpaceManager->setVariableStorage(scriptName, storage);
        }
        // Also set for any memory spaces that might exist
        if (m_currentMemorySpace) {
            m_currentMemorySpace->setVariableStorage(storage);
        }
    }
}

// Helper function to reconstruct operand from instruction arguments
QString EraParseTable::reconstructOperand(const ScriptLine& scriptLine) const
{
    QString operand;
    const QList<InstructionArgument> args = scriptLine.instructionData().arguments;
    
    if (args.isEmpty()) {
        return "";
    }
    
    // Join all arguments with spaces to reconstruct the operand
    for (int i = 0; i < args.size(); ++i) {
        if (i > 0) {
            operand += " ";
        }
        operand += args[i].value;
    }
    
    return operand.trimmed();
}

// Helper function to reconstruct full command text from instruction data
QString EraParseTable::reconstructCommand(const ScriptLine& scriptLine) const
{
    QString command;
    const InstructionData& data = scriptLine.instructionData();
    
    if (data.name.isEmpty()) {
        return scriptLine.content();
    }
    
    // Start with the instruction name
    command = data.name;
    
    // Add all arguments with spaces
    for (const InstructionArgument& arg : data.arguments) {
        command += " " + arg.value;
    }
    
    return command.trimmed();
}

bool EraParseTable::loadScript(const QString& scriptName, const QList<LogicalLine>& lines) {
    if (scriptName.isEmpty() || lines.isEmpty()) {
        return false;
    }
    
    // Store parsed script
    m_parsedScripts[scriptName] = lines;
    
    // Build execution queue
    buildExecutionQueue(scriptName);
    
    // Build memory space tree with proper nested blocks
    buildMemorySpaceTree(scriptName);
    
    // Build label positions
    QHash<QString, int> labelMap;
    for (int i = 0; i < lines.size(); ++i) {
        const LogicalLine& line = lines[i];
        const QList<ScriptLine>& scriptLines = line.scriptLines();
        
        for (const ScriptLine& scriptLine : scriptLines) {
            if (scriptLine.type() == ScriptLineType::Label) {
                // Extract label name from the line content
                QString labelName = scriptLine.content();
                // Remove @ prefix if present
                if (labelName.startsWith('@')) {
                    labelName = labelName.mid(1);
                }
                labelMap[labelName] = i;
            }
        }
    }
    m_labelPositions[scriptName] = labelMap;
    
    emit parseCompleted(scriptName);
    return true;
}

bool EraParseTable::loadDirectory(const QString& dirPath, int depth) {
    // Directory loading should be handled by ErbLoader
    // This method is now obsolete but kept for API compatibility
    return true;
}

void EraParseTable::setEntryPoint(const QString& label) {
    m_entryPoint = label;
    
    // Find which script contains this entry point label
    for (const QString& scriptName : m_labelPositions.keys()) {
        const QHash<QString, int>& labelMap = m_labelPositions[scriptName];
        if (labelMap.contains(label)) {
            m_currentScript = scriptName;
            qDebug() << "Set entry point:" << label << "in script:" << m_currentScript;
            break;
        }
    }
    
    // If entry point not found, try to find a reasonable default
    if (m_currentScript.isEmpty()) {
        qDebug() << "Entry point not found:" << label << ", trying defaults";
        // Try to find a script with common entry point labels
        QStringList commonLabels = {"MAIN_LOOP", "SYSTEM_TITLE", "MAIN", "TITLE"};
        for (const QString& labelToTry : commonLabels) {
            for (const QString& scriptName : m_labelPositions.keys()) {
                const QHash<QString, int>& labelMap = m_labelPositions[scriptName];
                if (labelMap.contains(labelToTry)) {
                    m_currentScript = scriptName;
                    m_entryPoint = labelToTry;
                    qDebug() << "Found default entry point:" << labelToTry << "in" << scriptName;
                    break;
                }
            }
            if (!m_currentScript.isEmpty()) break;
        }
    }
    
    // 位置区：入口点 = 全新执行，清空调用栈并把 PC 定位到入口 label
    if (!m_currentScript.isEmpty()) {
        const int entryLine = getLabelPosition(m_currentScript, m_entryPoint);
        m_currentMemorySpace = getOrCreateMemorySpace(m_currentScript);
        resetPosition();
        setCurrentLineInternal(entryLine >= 0 ? entryLine : 0, true);
    }

    emit entryPointReached(label);

    if (m_state) {
        m_state->requestStateCheck();
    }
}

QString EraParseTable::getEntryPoint() const {
    return m_entryPoint;
}

QQueue<LogicalLine> EraParseTable::getExecutionQueue() const {
    if (m_currentScript.isEmpty()) {
        return QQueue<LogicalLine>();
    }
    return m_executionQueues.value(m_currentScript);
}

int EraParseTable::getLabelPosition(const QString& scriptName, const QString& labelName) const {
    if (!m_labelPositions.contains(scriptName)) {
        return -1;
    }
    
    const QHash<QString, int>& labelMap = m_labelPositions[scriptName];
    return labelMap.value(labelName, -1);
}

bool EraParseTable::resolveJumpTarget(const QString& label, int& position) {
    position = getLabelPosition(m_currentScript, label);
    
    if (position >= 0) {
        return true;
    }
    
    for (const QString& scriptName : m_labelPositions.keys()) {
        position = getLabelPosition(scriptName, label);
        if (position >= 0) {
            m_currentScript = scriptName;
            m_currentMemorySpace = getOrCreateMemorySpace(scriptName);
            return true;
        }
    }
    
    return false;
}

bool EraParseTable::resolveJumpToScript(const QString& scriptName, const QString& label, int& position) {
    position = getLabelPosition(scriptName, label);
    if (position >= 0) {
        switchToMemorySpace(scriptName);
        return true;
    }
    return false;
}

QString EraParseTable::getCurrentScript() const {
    return m_currentScript;
}

// ===========================================================================
// 位置区 (Position region)
// ---------------------------------------------------------------------------
// 位置区只由持有本对象的执行线程读写，所有写入都必须经过下面这些
// setCurrentLineInternal / pushFrame / popFrame 的收敛点，以保证
// m_currentLine、m_callStack、m_depth 始终自洽。
// 对应官方 C#：Process.currentLine + Process.functionList
// (Emuera/GameProc/Process.cs, Process.CalledFunction.cs)。
// ===========================================================================

QString EraParseTable::currentScript() const {
    return m_currentScript;
}

int EraParseTable::currentLine() const {
    return m_currentLine;
}

int EraParseTable::depth() const {
    return m_depth;
}

const QList<Frame>& EraParseTable::callStack() const {
    return m_callStack;
}

Frame EraParseTable::currentFrame() const {
    if (m_callStack.isEmpty()) {
        return Frame();
    }
    return m_callStack.last();
}

bool EraParseTable::hasPosition() const {
    return !m_currentScript.isEmpty();
}

int EraParseTable::lineCountFor(const QString& script) const {
    return m_parsedScripts.value(script).size();
}

void EraParseTable::setCurrentLineInternal(int line, bool forceEmit) {
    if (line < 0) {
        line = 0;
    }
    if (line == m_currentLine && !forceEmit) {
        return;
    }
    m_currentLine = line;
    emit positionChanged(m_currentScript, m_currentLine);
}

void EraParseTable::pushFrame(const Frame& frame) {
    m_callStack.append(frame);
    m_depth = m_callStack.size();
    emit callStackChanged(m_depth);
}

Frame EraParseTable::popFrame() {
    if (m_callStack.isEmpty()) {
        return Frame();
    }
    const Frame frame = m_callStack.takeLast();
    m_depth = m_callStack.size();
    emit callStackChanged(m_depth);
    return frame;
}

void EraParseTable::resetPosition() {
    m_callStack.clear();
    m_depth = 0;
    m_currentLine = 0;
    emit callStackChanged(0);
    emit positionChanged(m_currentScript, 0);
}

void EraParseTable::setPosition(const QString& script, int line) {
    if (!script.isEmpty() && script != m_currentScript) {
        switchToMemorySpace(script);
    }
    setCurrentLineInternal(line, true);
}

void EraParseTable::advance() {
    setCurrentLineInternal(m_currentLine + 1);
}

bool EraParseTable::jumpToLine(int line) {
    const int count = lineCountFor(m_currentScript);
    if (line < 0 || (count > 0 && line >= count)) {
        return false;
    }
    setCurrentLineInternal(line, true);
    return true;
}

bool EraParseTable::jumpToLabel(const QString& label) {
    const QString oldScript = m_currentScript;

    int target = -1;
    if (!resolveJumpTarget(label, target)) {
        return false;
    }

    // resolveJumpTarget 可能已经改变了 m_currentScript，这里补齐内存空间与信号
    if (m_currentScript != oldScript) {
        m_currentMemorySpace = getOrCreateMemorySpace(m_currentScript);
        emit memorySpaceChanged(m_currentScript);
    }

    setCurrentLineInternal(target, true);
    emit jumpRequested(m_currentScript, label, target);
    return true;
}

bool EraParseTable::callLabel(const QString& label) {
    // 目标优先在当前脚本解析，否则跨脚本查找（保持调用者脚本不变）
    QString targetScript = m_currentScript;
    int target = getLabelPosition(m_currentScript, label);
    if (target < 0) {
        for (auto it = m_labelPositions.constBegin(); it != m_labelPositions.constEnd(); ++it) {
            const int pos = it.value().value(label, -1);
            if (pos >= 0) {
                targetScript = it.key();
                target = pos;
                break;
            }
        }
    }
    if (target < 0) {
        return false;
    }

    // 返回地址 = CALL 所在行的下一行，此时仍处于调用者脚本中
    pushFrame(Frame(m_currentScript, m_currentLine + 1, label));

    if (targetScript != m_currentScript) {
        switchToMemorySpace(targetScript);
    }
    setCurrentLineInternal(target, true);
    return true;
}

bool EraParseTable::returnFromCall() {
    if (m_callStack.isEmpty()) {
        return false;
    }

    const Frame frame = popFrame();
    if (!frame.script.isEmpty() && frame.script != m_currentScript) {
        switchToMemorySpace(frame.script);
    }
    setCurrentLineInternal(frame.returnLine, true);
    return true;
}

ScriptMemorySpace* EraParseTable::getCurrentMemorySpace() const {
    return m_currentMemorySpace;
}

MemorySpaceManager* EraParseTable::getMemorySpaceManager() const {
    return m_memorySpaceManager;
}

void EraParseTable::buildExecutionQueue(const QString& scriptName) {
    if (!m_parsedScripts.contains(scriptName)) {
        return;
    }
    
    QQueue<LogicalLine> queue;
    const QList<LogicalLine>& lines = m_parsedScripts[scriptName];
    
    for (const LogicalLine& line : lines) {
        queue.enqueue(line);
    }
    
    m_executionQueues[scriptName] = queue;

    if (m_currentScript == scriptName) {
        // 位置区：脚本重新装载后 PC 归零（加载期，保持安静，不发信号）
        m_currentLine = 0;
    }
}

void EraParseTable::buildMemorySpaceTree(const QString& scriptName) {
    if (!m_parsedScripts.contains(scriptName)) {
        return;
    }
    
    ScriptMemorySpace* space = getOrCreateMemorySpace(scriptName);
    
    // Ensure root block exists
    if (!space->rootBlock()) {
        space->setRootBlock(new MemoryBlock(MemoryBlockType::NestedBlock, space));
    }
    
    const QList<LogicalLine>& lines = m_parsedScripts[scriptName];
    
    MemoryBlock* currentParent = space->rootBlock();
    QList<MemoryBlock*> blockStack;
    
    for (int i = 0; i < lines.size(); ++i) {
        const LogicalLine& line = lines[i];
        const QList<ScriptLine>& scriptLines = line.scriptLines();
        
        for (const ScriptLine& scriptLine : scriptLines) {
            MemoryBlock* block = nullptr;
            
            switch (scriptLine.type()) {
                case ScriptLineType::Label: {
                    QString labelName = scriptLine.content();
                    if (labelName.startsWith('@')) {
                        labelName = labelName.mid(1);
                    }
                    block = new LabelBlock(labelName, space);
                    space->addLabelBlock(labelName, block);
                    space->setLabelPosition(labelName, i);
                    break;
                }
                
                case ScriptLineType::Instruction: {
                    QString instructionName = scriptLine.instructionData().name;
                    QString trimmed = instructionName.trimmed();
                    
                    // Use case-insensitive comparison for instruction names
                    QString upperName = trimmed.toUpper();
                    
                    if (upperName == "IF") {
                        // Reconstruct condition from instruction arguments
                        QString condition = reconstructOperand(scriptLine);
                        IfBlock* ifBlock = new IfBlock(condition, space);
                        block = ifBlock;
                        blockStack.append(currentParent);
                        currentParent = ifBlock;
                    }
                    else if (upperName == "ELSEIF") {
                        QString condition = reconstructOperand(scriptLine);
                        ElseIfBlock* elseIfBlock = new ElseIfBlock(condition, space);
                        block = elseIfBlock;
                        
                        if (!blockStack.isEmpty()) {
                            MemoryBlock* parent = blockStack.last();
                            if (parent && parent->type() == MemoryBlockType::IfBlock) {
                                IfBlock* ifBlock = static_cast<IfBlock*>(parent);
                                ifBlock->setFalseBranch(elseIfBlock);
                                currentParent = elseIfBlock;
                            }
                        }
                    }
                    else if (upperName == "ELSE") {
                        if (!blockStack.isEmpty()) {
                            MemoryBlock* parent = blockStack.last();
                            if (parent && parent->type() == MemoryBlockType::IfBlock) {
                                IfBlock* ifBlock = static_cast<IfBlock*>(parent);
                                if (ifBlock->trueBranch()) {
                                    ifBlock->setFalseBranch(new ElseBlock(space));
                                    block = ifBlock->falseBranch();
                                    currentParent = block;
                                }
                            }
                        }
                    }
                    else if (upperName == "ENDIF") {
                        if (!blockStack.isEmpty()) {
                            currentParent = blockStack.takeLast();
                        }
                        continue;
                    }
                    else if (upperName == "SIF") {
                        // SIF is a conditional IF that skips next line if false
                        QString condition = reconstructOperand(scriptLine);
                        IfBlock* ifBlock = new IfBlock(condition, space);
                        block = ifBlock;
                        blockStack.append(currentParent);
                        currentParent = ifBlock;
                    }
                    else if (upperName == "REPEAT" || upperName == "LOOP") {
                        QString operand = reconstructOperand(scriptLine);
                        LoopBlock* loopBlock = new LoopBlock(operand.toInt(), space);
                        block = loopBlock;
                        
                        blockStack.append(currentParent);
                        currentParent = loopBlock;
                    }
                    else if (upperName == "NEXT") {
                        if (!blockStack.isEmpty()) {
                            currentParent = blockStack.takeLast();
                        }
                        continue;
                    }
                    else if (upperName == "GOTO") {
                        QString target = reconstructOperand(scriptLine);
                        GotoBlock* gotoBlock = new GotoBlock(target, space);
                        block = gotoBlock;
                    }
                    else if (upperName == "CALL") {
                        QString target = reconstructOperand(scriptLine);
                        CallBlock* callBlock = new CallBlock(target, space);
                        block = callBlock;
                    }
                    else if (upperName == "RETURN") {
                        ReturnBlock* returnBlock = new ReturnBlock(space);
                        block = returnBlock;
                    }
                    else {
                        CommandBlock* cmdBlock = new CommandBlock(reconstructCommand(scriptLine), space);
                        block = cmdBlock;
                    }
                    break;
                }
                
                case ScriptLineType::Expression: {
                    CommandBlock* cmdBlock = new CommandBlock(reconstructCommand(scriptLine), space);
                    block = cmdBlock;
                    break;
                }
                
                case ScriptLineType::Comment:
                case ScriptLineType::Empty:
                    continue;
                
                default:
                    continue;
            }
            
            if (block) {
                // Only add to allBlocks if not already owned by a parent
                // Blocks added to parent are owned by parent and will be deleted when parent is deleted
                space->addToExecutionQueue(block);
                currentParent->addChild(block);
            }
        }
    }
    
}

ScriptMemorySpace* EraParseTable::getOrCreateMemorySpace(const QString& scriptName) {
    ScriptMemorySpace* space = m_memorySpaceManager->getMemorySpace(scriptName);
    if (!space) {
        space = m_memorySpaceManager->createMemorySpace(scriptName);
    }
    return space;
}

void EraParseTable::switchToMemorySpace(const QString& scriptName) {
    if (m_currentScript == scriptName) {
        return;
    }
    
    m_currentScript = scriptName;
    m_currentMemorySpace = getOrCreateMemorySpace(scriptName);
    
    qDebug() << "Switched to memory space:" << scriptName;
    
    emit memorySpaceChanged(scriptName);
}

void EraParseTable::onExecutionResult(const QString& scriptName, int lineNumber, bool success) {
    if (!success) {
        qDebug() << "Execution failed:" << scriptName << "line:" << lineNumber;
        return;
    }
    
    qDebug() << "Execution result received for:" << scriptName << "line:" << lineNumber;
    
    emit checkState();
}

void EraParseTable::onJumpRequest(const QString& label) {
    qDebug() << "Jump request received for label:" << label;

    // 位置区统一入口：解析 label（可跨脚本）并更新 PC，再通知执行侧。
    if (!jumpToLabel(label)) {
        qDebug() << "Failed to resolve jump target:" << label;
    }
}

void EraParseTable::onJumpToScript(const QString& scriptName, const QString& label) {
    qDebug() << "Jump to script request:" << scriptName << "label:" << label;
    
    int targetPosition = -1;
    if (resolveJumpToScript(scriptName, label, targetPosition)) {
        qDebug() << "Jump to script resolved:" << scriptName << ":" << label << "->" << targetPosition;
        // 位置区：切换脚本并定位到目标 label
        setPosition(scriptName, targetPosition);
        emit jumpRequested(scriptName, label, targetPosition);
    } else {
        qDebug() << "Failed to resolve jump to script:" << scriptName << ":" << label;
    }
}

void EraParseTable::onStateChange() {
    // State change detected - stop execution
    qDebug() << "State change detected, stopping execution";
}

void EraParseTable::onStateUnchanged() {
    // State unchanged - pump the next instruction
    // This is called when the state check finds no change
    if (m_currentScript.isEmpty() || m_executionQueues[m_currentScript].isEmpty()) {
        return;
    }
    
    pumpInstructions();
}

void EraParseTable::onStateChanged() {
    qDebug() << "State changed, continuing execution";
    // Continue execution after state change
    pumpInstructions();
}

bool EraParseTable::pumpInstructions() {
    // Execute instructions in a loop until state changes or queue is empty
    // This avoids stack overflow from recursive signal chains by:
    // 1. Using Qt::QueuedConnection for stateUnchanged signal (set in eraengine.cpp)
    // 2. Directly processing instructions without emitting signals
    
    if (m_currentScript.isEmpty() || m_executionQueues[m_currentScript].isEmpty()) {
        return false;
    }
    
    // Get reference to the queue
    QQueue<LogicalLine>& queue = m_executionQueues[m_currentScript];
    
    // Process instructions until queue is empty or state changes
    // Execute one instruction at a time to avoid stack overflow
    // This is called repeatedly by onStateUnchanged via Qt::QueuedConnection
    if (queue.isEmpty()) {
        return false;
    }
    
    // Get and execute the next instruction directly
    LogicalLine line = queue.dequeue();
    advance();  // 位置区：PC++，队列出队即前进

    // Use ExecutionEngine to execute the instruction
    if (m_executionEngine) {
        // Set current script and position
        m_executionEngine->setCurrentScript(m_currentScript);
        m_executionEngine->setExecutionPosition(m_currentLine);
        m_executionEngine->executeLogicalLine(line);

        // Execute any additional instructions from ExecutionEngine's queue
        // (e.g., loop body re-execution)
        while (!m_executionEngine->isQueueEmpty()) {
            LogicalLine execLine = m_executionEngine->dequeueExecutionLine();
            advance();
            m_executionEngine->setExecutionPosition(m_currentLine);
            m_executionEngine->executeLogicalLine(execLine);
        }
    } else {
        // Fallback: execute inline
        const QList<ScriptLine>& scriptLines = line.scriptLines();
        for (const ScriptLine& scriptLine : scriptLines) {
            if (scriptLine.type() == ScriptLineType::Label) {
                continue;  // Labels don't execute
            }
            if (scriptLine.type() == ScriptLineType::Instruction) {
                QString instructionName = scriptLine.instructionData().name;
                qDebug() << "Executing instruction:" << instructionName;
            }
        }
    }
    
    // After instruction, give event loop a chance to process
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    
    // Return true if there are more instructions to process
    return !queue.isEmpty();
}

void EraParseTable::startExecutionPump() {
    qDebug() << "Starting execution pump";
    // Start the execution pump by requesting the first instruction
    if (!m_entryPoint.isEmpty() && m_currentScript.isEmpty()) {
        // Set entry point if not already set
        setEntryPoint(m_entryPoint);
    }
    // Pump first instruction
    pumpInstructions();
}

void EraParseTable::onRequestNextInstruction() {
    qDebug() << "Request for next instruction received";
    pumpInstructions();
}

void EraParseTable::onExecutionComplete(const QString& scriptName) {
    qDebug() << "Execution complete for script:" << scriptName;

    // 位置区：一次执行结束，清空调用栈并复位 PC
    resetPosition();

    
    if (m_currentScript == scriptName) {
        // Execution complete for current script
        qDebug() << "Execution complete for current script:" << scriptName;
    }
}
