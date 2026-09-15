#ifndef INSTRUCTION_SYSTEM_H
#define INSTRUCTION_SYSTEM_H

#include <QObject>
#include <QString>
#include <QHash>
#include <QList>
#include <QVariant>
#include <functional>

class VariableStorage; // Forward declaration

// Instruction argument type
enum class InstructionArgType {
    INTEGER,
    STRING,
    VARIABLE,
    FUNCTION,
    EXPRESSION,
    UNKNOWN
};

// Instruction argument information
struct InstructionArg {
    QString name;
    InstructionArgType type;
    bool isRequired;
    QString description;
    
    InstructionArg(const QString& argName, InstructionArgType argType, bool required = true, const QString& desc = "")
        : name(argName), type(argType), isRequired(required), description(desc) {}
};

// Instruction definition
struct InstructionDef {
    QString name;
    QString description;
    QList<InstructionArg> arguments;
    std::function<QVariant(const QList<QVariant>&, VariableStorage*)> handler;
    
    InstructionDef() {}
    InstructionDef(const QString& instName, const QString& desc, const QList<InstructionArg>& args,
                   const std::function<QVariant(const QList<QVariant>&, VariableStorage*)>& handlerFunc)
        : name(instName), description(desc), arguments(args), handler(handlerFunc) {}
};

// Instruction system interface
class InstructionSystem : public QObject
{
    Q_OBJECT

public:
    explicit InstructionSystem(QObject *parent = nullptr);
    
    // Instruction registration
    bool registerInstruction(const InstructionDef& instruction);
    bool unregisterInstruction(const QString& name);
    
    // Instruction lookup
    bool hasInstruction(const QString& name) const;
    InstructionDef getInstruction(const QString& name) const;
    
    // Instruction execution
    QVariant executeInstruction(const QString& name, const QList<QVariant>& args, VariableStorage* storage = nullptr);
    
    // Instruction parsing
    bool parseInstruction(const QString& instruction, QString& instructionName, QStringList& args);
    
private:
    QHash<QString, InstructionDef> m_instructions;
    
    // Helper methods for common instruction types
    QVariant handleSetInstruction(const QList<QVariant>& args, VariableStorage* storage);
    QVariant handleIfInstruction(const QList<QVariant>& args, VariableStorage* storage);
    QVariant handleGotoInstruction(const QList<QVariant>& args, VariableStorage* storage);
};

#endif // INSTRUCTION_SYSTEM_H