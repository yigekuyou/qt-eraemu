#include "function_system.h"
#include "GameData/Variable/variable_storage.h"
#include <functional>
#include <QRegularExpression>

FunctionSystem::FunctionSystem(QObject *parent)
    : QObject(parent)
{
    registerBuiltInFunctions();
}

bool FunctionSystem::registerFunction(const FunctionSignature& signature)
{
    m_functions[signature.name] = signature;
    return true;
}

bool FunctionSystem::unregisterFunction(const QString& name)
{
    return m_functions.remove(name) > 0;
}

bool FunctionSystem::hasFunction(const QString& name) const
{
    return m_functions.contains(name);
}

FunctionSignature FunctionSystem::getFunctionSignature(const QString& name) const
{
    return m_functions.value(name);
}

FunctionResult FunctionSystem::executeFunction(const QString& name, const QList<QVariant>& args, VariableStorage* storage)
{
    // Check if function exists
    if (!m_functions.contains(name)) {
        return FunctionResult(false, QVariant(), QString("Function %1 not found").arg(name));
    }
    
    // Check if it's a built-in function
    if (m_builtInFunctions.contains(name)) {
        try {
            auto func = m_builtInFunctions.value(name);
            QVariant result = func(args, storage);
            return FunctionResult(true, result);
        } catch (const std::exception& e) {
            return FunctionResult(false, QVariant(), QString("Error executing function %1: %2").arg(name).arg(e.what()));
        }
    }
    
    // User-defined function execution would go here
    return FunctionResult(false, QVariant(), QString("Function %1 is not implemented").arg(name));
}

void FunctionSystem::registerBuiltInFunctions()
{
    // Register basic built-in functions
    // These would be expanded with actual implementations
    
    // Example: ABS function
    m_builtInFunctions["ABS"] = [](const QList<QVariant>& args, VariableStorage* storage) -> QVariant {
        if (args.isEmpty()) return QVariant(0);
        if (args.first().canConvert<int>()) {
            int value = args.first().toInt();
            return QVariant(qAbs(value));
        }
        return QVariant(0);
    };
    
    // Example: LENGTH function
    m_builtInFunctions["LENGTH"] = [](const QList<QVariant>& args, VariableStorage* storage) -> QVariant {
        if (args.isEmpty()) return QVariant(0);
        QString str = args.first().toString();
        return QVariant(str.length());
    };
    
    // Add more built-in functions as needed
}

QVariant FunctionSystem::evaluateExpression(const QString& expression, VariableStorage* storage)
{
    // This would use the expression evaluator to process expressions
    // For now return a placeholder
    return QVariant();
}