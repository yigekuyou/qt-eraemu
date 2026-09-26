#ifndef SCRIPT_MEMORY_SPACE_H
#define SCRIPT_MEMORY_SPACE_H

#include <QObject>
#include <QString>
#include <QHash>
#include <QList>
#include <memory>
#include "memory_block.h"

// Forward declarations
class MemoryBlock;
class VariableStorage;

// Script memory space - represents a single script file's parsed content
class ScriptMemorySpace : public QObject {
    Q_OBJECT
    
public:
    explicit ScriptMemorySpace(const QString& scriptName, QObject* parent = nullptr);
    ~ScriptMemorySpace();
    
    // Script properties
    QString scriptName() const;
    void setScriptName(const QString& scriptName);
    
    // Block management
    MemoryBlock* rootBlock() const;
    void setRootBlock(MemoryBlock* block);
    
    // Add block to space
    void addBlock(MemoryBlock* block);
    
    // Get blocks by type
    QList<MemoryBlock*> getBlocksByType(MemoryBlockType type) const;
    
    // Get blocks by label
    MemoryBlock* getBlockByLabel(const QString& labelName) const;
    void addLabelBlock(const QString& labelName, MemoryBlock* block);
    
    // Get blocks by instruction
    QList<MemoryBlock*> getBlocksByInstruction(const QString& instruction) const;
    
    // Execution queue management
    void addToExecutionQueue(MemoryBlock* block);
    QList<MemoryBlock*> getExecutionQueue() const;
    void clearExecutionQueue();
    
    // Entry point management
    void setEntryPoint(const QString& labelName);
    QString entryPoint() const;
    MemoryBlock* entryPointBlock() const;
    
    // Label position mapping
    int getLabelPosition(const QString& labelName) const;
    void setLabelPosition(const QString& labelName, int position);
    
    // Navigation
    MemoryBlock* getNextBlock(MemoryBlock* current) const;
    MemoryBlock* getPreviousBlock(MemoryBlock* current) const;
    
    // Find block by position
    MemoryBlock* getBlockAtPosition(int position) const;
    
    // Tree structure
    void buildTreeFromQueue();
    
    // Serialization (for debugging)
    QString toString() const;
    
    // Variable storage access (for condition evaluation)
    VariableStorage* getVariableStorage() const;
    void setVariableStorage(VariableStorage* storage);
    
signals:
    void blockAdded(MemoryBlock* block);
    void executionQueueChanged();
    void entryPointChanged(const QString& labelName);
    
private:
    QString m_scriptName;
    MemoryBlock* m_rootBlock;
    
    // All blocks in this space
    QList<MemoryBlock*> m_allBlocks;
    
    // Execution queue (ordered execution list)
    QList<MemoryBlock*> m_executionQueue;
    
    // Label mapping
    QHash<QString, MemoryBlock*> m_labelBlocks;
    QHash<QString, int> m_labelPositions;
    
    // Entry point
    QString m_entryPoint;
    
    // Position mapping
    QHash<int, MemoryBlock*> m_positionMap;
    
    // Variable storage for condition evaluation
    VariableStorage* m_variableStorage;
};

// Memory space manager - manages multiple script memory spaces
class MemorySpaceManager : public QObject {
    Q_OBJECT
    
public:
    explicit MemorySpaceManager(QObject* parent = nullptr);
    ~MemorySpaceManager();
    
    // Memory space management
    ScriptMemorySpace* createMemorySpace(const QString& scriptName);
    ScriptMemorySpace* getMemorySpace(const QString& scriptName) const;
    ScriptMemorySpace* getMemorySpaceByPath(const QString& filePath) const;
    void removeMemorySpace(const QString& scriptName);
    
    // Get all memory spaces
    QList<ScriptMemorySpace*> getAllMemorySpaces() const;
    
    // Get count
    int memorySpaceCount() const;
    
    // Add block to appropriate space
    void addBlockToSpace(const QString& scriptName, MemoryBlock* block);
    
    // Get block from space
    MemoryBlock* getBlock(const QString& scriptName, const QString& labelName) const;
    
    // Set variable storage for a specific space
    void setVariableStorage(const QString& scriptName, VariableStorage* storage);
    
    // Execute block
    bool executeBlock(const QString& scriptName, const QString& labelName);
    
    // Jump between spaces
    bool jumpToSpace(const QString& targetSpace, const QString& labelName);
    
signals:
    void memorySpaceCreated(const QString& scriptName);
    void memorySpaceRemoved(const QString& scriptName);
    void blockExecuted(const QString& scriptName, MemoryBlock* block);
    
private:
    QHash<QString, ScriptMemorySpace*> m_memorySpaces;
};

#endif // SCRIPT_MEMORY_SPACE_H
