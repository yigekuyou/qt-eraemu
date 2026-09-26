#include "memory_block.h"
#include "script_memory_space.h"
#include "expression_evaluator.h"
#include <QRegularExpression>

// MemoryBlock implementation
MemoryBlock::MemoryBlock(MemoryBlockType type, ScriptMemorySpace* space, QObject* parent)
    : QObject(parent)
    , m_type(type)
    , m_id(0)
    , m_depth(0)
    , m_content("")
    , m_executionPosition(0)
    , m_parent(nullptr)
    , m_memorySpace(space)
{
}

MemoryBlock::~MemoryBlock() {
    // Clear children
    qDeleteAll(m_children);
    m_children.clear();
}

MemoryBlockType MemoryBlock::type() const {
    return m_type;
}

int MemoryBlock::id() const {
    return m_id;
}

int MemoryBlock::depth() const {
    return m_depth;
}

MemoryBlock* MemoryBlock::parent() const {
    return m_parent;
}

QList<MemoryBlock*> MemoryBlock::children() const {
    return m_children;
}

bool MemoryBlock::addChild(MemoryBlock* child) {
    if (child && child != this) {
        // Check if already a child (idempotent)
        if (m_children.contains(child)) {
            return false;  // Already a child
        }
        // Set parent
        child->m_parent = this;
        child->m_depth = m_depth + 1;
        m_children.append(child);
        return true;  // Child was added
    }
    return false;
}

void MemoryBlock::removeChild(MemoryBlock* child) {
    if (child && m_children.contains(child)) {
        child->m_parent = nullptr;
        child->m_depth = 0;
        m_children.removeAll(child);
    }
}

QString MemoryBlock::content() const {
    return m_content;
}

void MemoryBlock::setContent(const QString& content) {
    m_content = content;
}

int MemoryBlock::executionPosition() const {
    return m_executionPosition;
}

void MemoryBlock::setExecutionPosition(int position) {
    m_executionPosition = position;
}

bool MemoryBlock::isExecutable() const {
    return true; // Base implementation
}

MemoryBlock* MemoryBlock::nextBlock() const {
    if (m_parent && !m_children.isEmpty()) {
        int index = m_parent->m_children.indexOf(const_cast<MemoryBlock*>(this));
        if (index >= 0 && index + 1 < m_parent->m_children.size()) {
            return m_parent->m_children[index + 1];
        }
    }
    return nullptr;
}

MemoryBlock* MemoryBlock::previousBlock() const {
    if (m_parent && !m_children.isEmpty()) {
        int index = m_parent->m_children.indexOf(const_cast<MemoryBlock*>(this));
        if (index > 0) {
            return m_parent->m_children[index - 1];
        }
    }
    return nullptr;
}

MemoryBlock* MemoryBlock::root() {
    MemoryBlock* current = this;
    while (current->m_parent) {
        current = current->m_parent;
    }
    return current;
}

ScriptMemorySpace* MemoryBlock::memorySpace() const {
    return m_memorySpace;
}

MemoryBlock* MemoryBlock::findChildByType(MemoryBlockType type) const {
    for (MemoryBlock* child : m_children) {
        if (child->m_type == type) {
            return child;
        }
        // Recursively search in children
        MemoryBlock* found = child->findChildByType(type);
        if (found) {
            return found;
        }
    }
    return nullptr;
}

MemoryBlock* MemoryBlock::findChildByContent(const QString& content) const {
    for (MemoryBlock* child : m_children) {
        if (child->m_content == content) {
            return child;
        }
        // Recursively search in children
        MemoryBlock* found = child->findChildByContent(content);
        if (found) {
            return found;
        }
    }
    return nullptr;
}

// CommandBlock implementation
CommandBlock::CommandBlock(const QString& content, ScriptMemorySpace* space, QObject* parent)
    : MemoryBlock(MemoryBlockType::Command, space, parent)
{
    m_content = content;
}

bool CommandBlock::execute() {
    emit blockExecuted();
    return true;
}

QString CommandBlock::instructionName() const {
    // Extract instruction name from content (first word)
    if (m_content.isEmpty()) {
        return "";
    }
    return m_content.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).first();
}

// IfBlock implementation
IfBlock::IfBlock(const QString& condition, ScriptMemorySpace* space, QObject* parent)
    : MemoryBlock(MemoryBlockType::IfBlock, space, parent)
    , m_condition(condition)
    , m_trueBranch(nullptr)
    , m_falseBranch(nullptr)
{
}

bool IfBlock::condition() const {
    return !m_condition.isEmpty();
}

bool IfBlock::evaluateCondition() const {
    // Get VariableStorage from memory space
    VariableStorage* storage = nullptr;
    if (ScriptMemorySpace* space = memorySpace()) {
        storage = space->getVariableStorage();
    }
    
    if (!storage || m_condition.isEmpty()) {
        return false;
    }
    
    // Evaluate the condition using ExpressionEvaluator
    ExpressionEvaluator evaluator;
    qint64 result = 0;
    
    // Try to evaluate as integer expression
    if (evaluator.evaluateInt(m_condition, storage, result)) {
        // Non-zero is true, zero is false
        return (result != 0);
    }
    
    // Fallback: if evaluation fails, check if condition is non-empty
    return !m_condition.isEmpty();
}

void IfBlock::setCondition(const QString& condition) {
    m_condition = condition;
}

MemoryBlock* IfBlock::trueBranch() const {
    return m_trueBranch;
}

MemoryBlock* IfBlock::falseBranch() const {
    return m_falseBranch;
}

void IfBlock::setTrueBranch(MemoryBlock* branch) {
    m_trueBranch = branch;
    if (branch) {
        addChild(branch);
    }
}

void IfBlock::setFalseBranch(MemoryBlock* branch) {
    m_falseBranch = branch;
    if (branch) {
        addChild(branch);
    }
}

bool IfBlock::execute() {
    // Evaluate condition using VariableStorage from memory space
    bool conditionMet = evaluateCondition();
    
    // Execute the appropriate branch based on condition
    if (conditionMet) {
        if (m_trueBranch) {
            m_trueBranch->execute();
        }
    } else {
        if (m_falseBranch) {
            m_falseBranch->execute();
        }
    }
    emit blockExecuted();
    return true;
}

// LoopBlock implementation
LoopBlock::LoopBlock(int iterations, ScriptMemorySpace* space, QObject* parent)
    : MemoryBlock(MemoryBlockType::LoopBlock, space, parent)
    , m_iterations(iterations)
    , m_currentIteration(0)
    , m_finished(false)
{
}

int LoopBlock::iterations() const {
    return m_iterations;
}

void LoopBlock::setIterations(int iterations) {
    m_iterations = iterations;
}

int LoopBlock::currentIteration() const {
    return m_currentIteration;
}

void LoopBlock::setCurrentIteration(int iteration) {
    m_currentIteration = iteration;
}

bool LoopBlock::isFinished() const {
    return m_finished || m_currentIteration >= m_iterations;
}

bool LoopBlock::execute() {
    if (!isFinished()) {
        m_currentIteration++;
        emit blockExecuted();
        return true;
    }
    m_finished = true;
    return false;
}

// Base MemoryBlock implementation
bool MemoryBlock::execute() {
    // Base implementation does nothing
    return true;
}

// LabelBlock implementation
LabelBlock::LabelBlock(const QString& labelName, ScriptMemorySpace* space, QObject* parent)
    : MemoryBlock(MemoryBlockType::LabelBlock, space, parent)
    , m_labelName(labelName)
    , m_isEntryPoint(false)
{
}

QString LabelBlock::labelName() const {
    return m_labelName;
}

void LabelBlock::setLabelName(const QString& labelName) {
    m_labelName = labelName;
}

bool LabelBlock::isEntryPoint() const {
    return m_isEntryPoint;
}

void LabelBlock::setEntryPoint(bool entryPoint) {
    m_isEntryPoint = entryPoint;
}


// ElseBlock implementation

// ElseBlock implementation
ElseBlock::ElseBlock(ScriptMemorySpace* space, QObject* parent)
    : MemoryBlock(MemoryBlockType::ElseBlock, space, parent)
{
}

bool ElseBlock::execute() {
    emit blockExecuted();
    return true;
}

// ElseIfBlock implementation
ElseIfBlock::ElseIfBlock(const QString& condition, ScriptMemorySpace* space, QObject* parent)
    : MemoryBlock(MemoryBlockType::ElseIfBlock, space, parent)
    , m_condition(condition)
{
}

bool ElseIfBlock::condition() const {
    return !m_condition.isEmpty();
}

bool ElseIfBlock::evaluateCondition() const {
    // Get VariableStorage from memory space
    VariableStorage* storage = nullptr;
    if (ScriptMemorySpace* space = memorySpace()) {
        storage = space->getVariableStorage();
    }
    
    if (!storage || m_condition.isEmpty()) {
        return false;
    }
    
    // Evaluate the condition using ExpressionEvaluator
    ExpressionEvaluator evaluator;
    qint64 result = 0;
    
    // Try to evaluate as integer expression
    if (evaluator.evaluateInt(m_condition, storage, result)) {
        // Non-zero is true, zero is false
        return (result != 0);
    }
    
    // Fallback: if evaluation fails, check if condition is non-empty
    return !m_condition.isEmpty();
}

void ElseIfBlock::setCondition(const QString& condition) {
    m_condition = condition;
}

bool ElseIfBlock::execute() {
    emit blockExecuted();
    return true;
}

// GotoBlock implementation
GotoBlock::GotoBlock(const QString& targetLabel, ScriptMemorySpace* space, QObject* parent)
    : MemoryBlock(MemoryBlockType::GotoBlock, space, parent)
    , m_targetLabel(targetLabel)
{
}

QString GotoBlock::targetLabel() const {
    return m_targetLabel;
}

void GotoBlock::setTargetLabel(const QString& label) {
    m_targetLabel = label;
}

bool GotoBlock::execute() {
    emit blockExecuted();
    return true;
}

// CallBlock implementation
CallBlock::CallBlock(const QString& targetLabel, ScriptMemorySpace* space, QObject* parent)
    : MemoryBlock(MemoryBlockType::CallBlock, space, parent)
    , m_targetLabel(targetLabel)
{
}

QString CallBlock::targetLabel() const {
    return m_targetLabel;
}

void CallBlock::setTargetLabel(const QString& label) {
    m_targetLabel = label;
}

bool CallBlock::execute() {
    emit blockExecuted();
    return true;
}

// ReturnBlock implementation
ReturnBlock::ReturnBlock(ScriptMemorySpace* space, QObject* parent)
    : MemoryBlock(MemoryBlockType::ReturnBlock, space, parent)
{
}

bool ReturnBlock::execute() {
    emit blockExecuted();
    return true;
}
