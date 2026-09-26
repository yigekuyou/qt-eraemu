#include "script_memory_space.h"
#include <QFileInfo>
#include "variable_storage.h"

// ScriptMemorySpace implementation
ScriptMemorySpace::ScriptMemorySpace(const QString& scriptName, QObject* parent)
    : QObject(parent)
    , m_scriptName(scriptName)
    , m_rootBlock(nullptr)
    , m_variableStorage(nullptr)
{
}

ScriptMemorySpace::~ScriptMemorySpace() {
    qDeleteAll(m_allBlocks);
    m_allBlocks.clear();
    m_labelBlocks.clear();
    m_positionMap.clear();
}

QString ScriptMemorySpace::scriptName() const {
    return m_scriptName;
}

void ScriptMemorySpace::setScriptName(const QString& scriptName) {
    m_scriptName = scriptName;
}

MemoryBlock* ScriptMemorySpace::rootBlock() const {
    return m_rootBlock;
}

void ScriptMemorySpace::setRootBlock(MemoryBlock* block) {
    m_rootBlock = block;
}

void ScriptMemorySpace::addBlock(MemoryBlock* block) {
    if (block) {
        m_allBlocks.append(block);
        emit blockAdded(block);
    }
}

QList<MemoryBlock*> ScriptMemorySpace::getBlocksByType(MemoryBlockType type) const {
    QList<MemoryBlock*> result;
    for (MemoryBlock* block : m_allBlocks) {
        if (block->type() == type) {
            result.append(block);
        }
    }
    return result;
}

MemoryBlock* ScriptMemorySpace::getBlockByLabel(const QString& labelName) const {
    return m_labelBlocks.value(labelName, nullptr);
}

void ScriptMemorySpace::addLabelBlock(const QString& labelName, MemoryBlock* block) {
    if (block) {
        m_labelBlocks[labelName] = block;
    }
}

QList<MemoryBlock*> ScriptMemorySpace::getBlocksByInstruction(const QString& instruction) const {
    QList<MemoryBlock*> result;
    for (MemoryBlock* block : m_allBlocks) {
        if (block->type() == MemoryBlockType::Command) {
            CommandBlock* cmdBlock = qobject_cast<CommandBlock*>(block);
            if (cmdBlock && cmdBlock->instructionName() == instruction) {
                result.append(block);
            }
        }
    }
    return result;
}

void ScriptMemorySpace::addToExecutionQueue(MemoryBlock* block) {
    if (block && !m_executionQueue.contains(block)) {
        m_executionQueue.append(block);
        m_positionMap[m_executionQueue.size() - 1] = block;
        emit executionQueueChanged();
    }
}

QList<MemoryBlock*> ScriptMemorySpace::getExecutionQueue() const {
    return m_executionQueue;
}

void ScriptMemorySpace::clearExecutionQueue() {
    m_executionQueue.clear();
    m_positionMap.clear();
    emit executionQueueChanged();
}

void ScriptMemorySpace::setEntryPoint(const QString& labelName) {
    m_entryPoint = labelName;
    emit entryPointChanged(labelName);
}

QString ScriptMemorySpace::entryPoint() const {
    return m_entryPoint;
}

VariableStorage* ScriptMemorySpace::getVariableStorage() const {
    return m_variableStorage;
}

void ScriptMemorySpace::setVariableStorage(VariableStorage* storage) {
    m_variableStorage = storage;
}

MemoryBlock* ScriptMemorySpace::entryPointBlock() const {
    return m_labelBlocks.value(m_entryPoint, nullptr);
}

int ScriptMemorySpace::getLabelPosition(const QString& labelName) const {
    return m_labelPositions.value(labelName, -1);
}

void ScriptMemorySpace::setLabelPosition(const QString& labelName, int position) {
    m_labelPositions[labelName] = position;
}

MemoryBlock* ScriptMemorySpace::getNextBlock(MemoryBlock* current) const {
    if (!current || m_executionQueue.isEmpty()) {
        return nullptr;
    }
    
    int index = m_executionQueue.indexOf(current);
    if (index >= 0 && index + 1 < m_executionQueue.size()) {
        return m_executionQueue[index + 1];
    }
    return nullptr;
}

MemoryBlock* ScriptMemorySpace::getPreviousBlock(MemoryBlock* current) const {
    if (!current || m_executionQueue.isEmpty()) {
        return nullptr;
    }
    
    int index = m_executionQueue.indexOf(current);
    if (index > 0) {
        return m_executionQueue[index - 1];
    }
    return nullptr;
}

MemoryBlock* ScriptMemorySpace::getBlockAtPosition(int position) const {
    return m_positionMap.value(position, nullptr);
}

void ScriptMemorySpace::buildTreeFromQueue() {
    if (m_executionQueue.isEmpty()) {
        return;
    }
    
    // Create root block
    if (!m_rootBlock) {
        m_rootBlock = new MemoryBlock(MemoryBlockType::NestedBlock, this);
    }
    
    // Build tree from execution queue
    MemoryBlock* currentParent = m_rootBlock;
    for (MemoryBlock* block : m_executionQueue) {
        currentParent->addChild(block);
        
        // Special handling for nested blocks
        if (block->type() == MemoryBlockType::IfBlock ||
            block->type() == MemoryBlockType::LoopBlock ||
            block->type() == MemoryBlockType::FunctionBlock) {
            // This block may have nested content, make it the new parent
            currentParent = block;
        }
    }
}

QString ScriptMemorySpace::toString() const {
    QString result = QString("ScriptMemorySpace: %1\n").arg(m_scriptName);
    result += QString("Root block: %1\n").arg(m_rootBlock ? "exists" : "null");
    result += QString("Total blocks: %1\n").arg(m_allBlocks.size());
    result += QString("Execution queue size: %1\n").arg(m_executionQueue.size());
    result += QString("Entry point: %1\n").arg(m_entryPoint);
    
    for (const QString& label : m_labelBlocks.keys()) {
        result += QString("  Label: %1 -> %2\n").arg(label).arg(
            m_labelBlocks.value(label) ? "exists" : "null"
        );
    }
    
    return result;
}

// MemorySpaceManager implementation
MemorySpaceManager::MemorySpaceManager(QObject* parent)
    : QObject(parent)
{
}

MemorySpaceManager::~MemorySpaceManager() {
    qDeleteAll(m_memorySpaces);
    m_memorySpaces.clear();
}

ScriptMemorySpace* MemorySpaceManager::createMemorySpace(const QString& scriptName) {
    if (m_memorySpaces.contains(scriptName)) {
        return m_memorySpaces[scriptName];
    }
    
    ScriptMemorySpace* space = new ScriptMemorySpace(scriptName, this);
    m_memorySpaces[scriptName] = space;
    emit memorySpaceCreated(scriptName);
    
    return space;
}

ScriptMemorySpace* MemorySpaceManager::getMemorySpace(const QString& scriptName) const {
    return m_memorySpaces.value(scriptName, nullptr);
}

ScriptMemorySpace* MemorySpaceManager::getMemorySpaceByPath(const QString& filePath) const {
    // Extract script name from file path
    QString scriptName = QFileInfo(filePath).completeBaseName();
    return getMemorySpace(scriptName);
}

void MemorySpaceManager::removeMemorySpace(const QString& scriptName) {
    if (m_memorySpaces.contains(scriptName)) {
        ScriptMemorySpace* space = m_memorySpaces.take(scriptName);
        emit memorySpaceRemoved(scriptName);
        delete space;
    }
}

QList<ScriptMemorySpace*> MemorySpaceManager::getAllMemorySpaces() const {
    return m_memorySpaces.values();
}

int MemorySpaceManager::memorySpaceCount() const {
    return m_memorySpaces.size();
}

void MemorySpaceManager::addBlockToSpace(const QString& scriptName, MemoryBlock* block) {
    ScriptMemorySpace* space = getMemorySpace(scriptName);
    if (space && block) {
        space->addBlock(block);
    }
}

MemoryBlock* MemorySpaceManager::getBlock(const QString& scriptName, const QString& labelName) const {
    ScriptMemorySpace* space = getMemorySpace(scriptName);
    if (space) {
        return space->getBlockByLabel(labelName);
    }
    return nullptr;
}

void MemorySpaceManager::setVariableStorage(const QString& scriptName, VariableStorage* storage) {
    ScriptMemorySpace* space = getMemorySpace(scriptName);
    if (space && storage) {
        space->setVariableStorage(storage);
    }
}

bool MemorySpaceManager::executeBlock(const QString& scriptName, const QString& labelName) {
    ScriptMemorySpace* space = getMemorySpace(scriptName);
    if (space) {
        MemoryBlock* block = space->getBlockByLabel(labelName);
        if (block) {
            bool result = block->execute();
            emit blockExecuted(scriptName, block);
            return result;
        }
    }
    return false;
}

bool MemorySpaceManager::jumpToSpace(const QString& targetSpace, const QString& labelName) {
    ScriptMemorySpace* target = getMemorySpace(targetSpace);
    if (target) {
        MemoryBlock* block = target->getBlockByLabel(labelName);
        if (block) {
            // Execute the block in target space
            bool result = block->execute();
            emit blockExecuted(targetSpace, block);
            return result;
        }
    }
    return false;
}
