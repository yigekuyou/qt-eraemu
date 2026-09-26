#ifndef MEMORY_BLOCK_H
#define MEMORY_BLOCK_H

#include <QObject>
#include <QString>
#include <QList>
#include <memory>

// Forward declarations
class MemoryBlock;
class ScriptMemorySpace;
class VariableStorage;
class ExpressionEvaluator;

// Memory block types
enum class MemoryBlockType {
    Command,       // Single command/instruction
    IfBlock,       // IF condition block
    ElseBlock,     // ELSE block
    ElseIfBlock,   // ELSE IF block
    LoopBlock,     // REPEAT/LOOP block
    FunctionBlock, // Function call block
    GotoBlock,     // GOTO jump block
    CallBlock,     // CALL block
    ReturnBlock,   // RETURN block
    LabelBlock,    // Label definition
    NestedBlock    // General nested block
};

// Base class for all memory blocks (tree structure)
class MemoryBlock : public QObject {
    Q_OBJECT
    
public:
    explicit MemoryBlock(MemoryBlockType type, ScriptMemorySpace* space, QObject* parent = nullptr);
    virtual ~MemoryBlock();
    
    // Block properties
    MemoryBlockType type() const;
    int id() const;
    int depth() const;
    
    // Parent/child relationships (tree structure)
    MemoryBlock* parent() const;
    QList<MemoryBlock*> children() const;
    
    // Add/remove children (returns true if child was added, false if it was already a child)
    bool addChild(MemoryBlock* child);
    void removeChild(MemoryBlock* child);
    
    // Get block content (command/instruction)
    QString content() const;
    void setContent(const QString& content);
    
    // Get/set execution position
    int executionPosition() const;
    void setExecutionPosition(int position);
    
    // Check if block is executable
    bool isExecutable() const;
    
    // Get next block in sequence
    MemoryBlock* nextBlock() const;
    
    // Get previous block in sequence
    MemoryBlock* previousBlock() const;
    
    // Get the root of the tree
    MemoryBlock* root();
    
    // Get script memory space
    ScriptMemorySpace* memorySpace() const;
    
    // Tree navigation
    MemoryBlock* findChildByType(MemoryBlockType type) const;
    MemoryBlock* findChildByContent(const QString& content) const;
    
    // Execute method for subclasses
    virtual bool execute();
    
signals:
    void blockExecuted();
    void blockChanged();
    
protected:
    // Protected members for subclasses
    QString m_content;
    
private:
    MemoryBlockType m_type;
    int m_id;
    int m_depth;
    int m_executionPosition;
    
    MemoryBlock* m_parent;
    QList<MemoryBlock*> m_children;
    
    ScriptMemorySpace* m_memorySpace;
};

// Command block (represents a single instruction)
class CommandBlock : public MemoryBlock {
    Q_OBJECT
    
public:
    explicit CommandBlock(const QString& content, ScriptMemorySpace* space, QObject* parent = nullptr);
    
    // Execute the command
    bool execute() override;
    
    // Get instruction name
    QString instructionName() const;
};

// IF block (condition + true/false branches)
class IfBlock : public MemoryBlock {
    Q_OBJECT
    
public:
    explicit IfBlock(const QString& condition, ScriptMemorySpace* space, QObject* parent = nullptr);
    
    // Condition evaluation
    bool condition() const;
    void setCondition(const QString& condition);
    
    // Branches
    MemoryBlock* trueBranch() const;
    MemoryBlock* falseBranch() const;
    void setTrueBranch(MemoryBlock* branch);
    void setFalseBranch(MemoryBlock* branch);
    
    // Condition evaluation
    // Note: Uses VariableStorage from ScriptMemorySpace
    bool evaluateCondition() const;
    
    // Execute based on condition
    bool execute() override;
    
private:
    QString m_condition;
    MemoryBlock* m_trueBranch;
    MemoryBlock* m_falseBranch;
};

// Else block
class ElseBlock : public MemoryBlock {
    Q_OBJECT
    
public:
    explicit ElseBlock(ScriptMemorySpace* space, QObject* parent = nullptr);
    
    // Execute else block
    bool execute() override;
};

// Else If block
class ElseIfBlock : public MemoryBlock {
    Q_OBJECT
    
public:
    explicit ElseIfBlock(const QString& condition, ScriptMemorySpace* space, QObject* parent = nullptr);
    
    // Condition evaluation
    bool condition() const;
    void setCondition(const QString& condition);
    
    // Condition evaluation using VariableStorage
    bool evaluateCondition() const;
    
    // Execute based on condition
    bool execute() override;
    
private:
    QString m_condition;
};

// Loop block (REPEAT/LOOP)
class LoopBlock : public MemoryBlock {
    Q_OBJECT
    
public:
    explicit LoopBlock(int iterations, ScriptMemorySpace* space, QObject* parent = nullptr);
    
    // Loop control
    int iterations() const;
    void setIterations(int iterations);
    
    int currentIteration() const;
    void setCurrentIteration(int iteration);
    
    bool isFinished() const;
    
    // Execute loop body
    bool execute() override;
    
private:
    int m_iterations;
    int m_currentIteration;
    bool m_finished;
};

// Goto block (jump to label)
class GotoBlock : public MemoryBlock {
    Q_OBJECT
    
public:
    explicit GotoBlock(const QString& targetLabel, ScriptMemorySpace* space, QObject* parent = nullptr);
    
    // Get target label
    QString targetLabel() const;
    void setTargetLabel(const QString& label);
    
    // Execute jump
    bool execute() override;
    
private:
    QString m_targetLabel;
};

// Call block (call function)
class CallBlock : public MemoryBlock {
    Q_OBJECT
    
public:
    explicit CallBlock(const QString& targetLabel, ScriptMemorySpace* space, QObject* parent = nullptr);
    
    // Get target label
    QString targetLabel() const;
    void setTargetLabel(const QString& label);
    
    // Execute call
    bool execute() override;
    
private:
    QString m_targetLabel;
};

// Return block (return from function)
class ReturnBlock : public MemoryBlock {
    Q_OBJECT
    
public:
    explicit ReturnBlock(ScriptMemorySpace* space, QObject* parent = nullptr);
    
    // Execute return
    bool execute() override;
};

// Label block (function entry point)
class LabelBlock : public MemoryBlock {
    Q_OBJECT
    
public:
    explicit LabelBlock(const QString& labelName, ScriptMemorySpace* space, QObject* parent = nullptr);
    
    // Label properties
    QString labelName() const;
    void setLabelName(const QString& labelName);
    
    // Entry point properties
    bool isEntryPoint() const;
    void setEntryPoint(bool entryPoint);
    
private:
    QString m_labelName;
    bool m_isEntryPoint;
};

#endif // MEMORY_BLOCK_H
