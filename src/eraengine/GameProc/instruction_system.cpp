#include "instruction_system.h"
#include "variable_storage.h"
#include <QRegularExpression>
#include <functional>

InstructionSystem::InstructionSystem(QObject *parent)
    : QObject(parent)
{
    // Initialize with some basic instructions
}

bool InstructionSystem::registerInstruction(const InstructionDef& instruction)
{
    m_instructions[instruction.name] = instruction;
    return true;
}

bool InstructionSystem::unregisterInstruction(const QString& name)
{
    return m_instructions.remove(name) > 0;
}

bool InstructionSystem::hasInstruction(const QString& name) const
{
    return m_instructions.contains(name);
}

InstructionDef InstructionSystem::getInstruction(const QString& name) const
{
    return m_instructions.value(name);
}

QVariant InstructionSystem::executeInstruction(const QString& name, const QList<QVariant>& args, VariableStorage* storage)
{
    if (!m_instructions.contains(name)) {
        return QVariant();
    }
    
    InstructionDef instruction = m_instructions.value(name);
    if (instruction.handler) {
        return instruction.handler(args, storage);
    }
    
    return QVariant();
}

bool InstructionSystem::parseInstruction(const QString& instruction, QString& instructionName, QStringList& args)
{
    // Simple parsing - would be expanded with full parser
    // Example: "SET VARIABLE 10" would be parsed
    QRegularExpression re(R"(^(\w+)\s+(.*)$)");
    QRegularExpressionMatch match = re.match(instruction.trimmed());
    
    if (match.hasMatch()) {
        instructionName = match.captured(1);
        QString argStr = match.captured(2);
        // Simple argument parsing - would be more robust
        args = argStr.split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
        return true;
    }
    
    return false;
}

QVariant InstructionSystem::handleSetInstruction(const QList<QVariant>& args, VariableStorage* storage)
{
    // Handle SET instruction logic
    return QVariant();
}

QVariant InstructionSystem::handleIfInstruction(const QList<QVariant>& args, VariableStorage* storage)
{
    // Handle IF instruction logic
    return QVariant();
}

QVariant InstructionSystem::handleGotoInstruction(const QList<QVariant>& args, VariableStorage* storage)
{
    // Handle GOTO instruction logic
    return QVariant();
}