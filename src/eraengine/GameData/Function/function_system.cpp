#include "function_system.h"
#include "GameData/Variable/variable_storage.h"
#include <functional>
#include <QRegularExpression>
#include <QRandomGenerator>

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

FunctionResult FunctionSystem::executeFunction(const QString& name, const QList<QVariant>& args, VariableStorage* storage, const ScriptPosition& pos)
{
    // Check if function exists
    if (!m_functions.contains(name) && !m_builtInFunctions.contains(name)) {
        return FunctionResult(false, QVariant(), 
            QString("Function %1 not found").arg(name), pos);
    }
    
    // Execute built-in function
    if (m_builtInFunctions.contains(name)) {
        try {
            auto func = m_builtInFunctions.value(name);
            QVariant result = func(args, storage);
            return FunctionResult(true, result, "", pos);
        } catch (const std::exception& e) {
            return FunctionResult(false, QVariant(), 
                QString("Error executing function %1: %2").arg(name).arg(e.what()), pos);
        }
    }
    
    // User-defined function execution would go here
    return FunctionResult(false, QVariant(), 
        QString("Function %1 is not implemented").arg(name), pos);
}

void FunctionSystem::pushCallFrame(const QString& label, const ScriptPosition& pos)
{
    m_callStack.push(qMakePair(label, pos));
}

void FunctionSystem::popCallFrame()
{
    if (!m_callStack.isEmpty()) {
        m_callStack.pop();
    }
}

QString FunctionSystem::getCurrentCallFunction() const
{
    if (m_callStack.isEmpty()) {
        return "";
    }
    return m_callStack.top().first;
}

void FunctionSystem::registerBuiltInFunctions()
{
    // Mathematical functions
    m_builtInFunctions["ABS"] = [](const QList<QVariant>& args, VariableStorage* storage) -> QVariant {
        if (args.isEmpty()) return QVariant(0);
        if (args.first().canConvert<int>()) {
            int value = args.first().toInt();
            return QVariant(qAbs(value));
        }
        return QVariant(0);
    };
    
    m_builtInFunctions["SGN"] = [](const QList<QVariant>& args, VariableStorage* storage) -> QVariant {
        if (args.isEmpty()) return QVariant(0);
        int val = args.first().toInt();
        if (val > 0) return QVariant(1);
        if (val < 0) return QVariant(-1);
        return QVariant(0);
    };
    
    m_builtInFunctions["INT"] = [](const QList<QVariant>& args, VariableStorage* storage) -> QVariant {
        if (args.isEmpty()) return QVariant(0);
        return QVariant(args.first().toInt());
    };
    
    m_builtInFunctions["RND"] = [](const QList<QVariant>& args, VariableStorage* storage) -> QVariant {
        if (args.isEmpty()) return QVariant(QRandomGenerator::global()->generate() % 1000);
        int range = args.first().toInt();
        if (range <= 0) return QVariant(0);
        return QVariant(QRandomGenerator::global()->generate() % range);
    };
    
    // Input functions
    m_builtInFunctions["ONEINPUT"] = [](const QList<QVariant>& args, VariableStorage* storage) -> QVariant {
        // For CLI testing, return immediately without waiting for input
        // Set RESULT variable to 0 (no input)
        if (storage) {
            storage->setGlobalInt1D("RESULT", 0, 0);
        }
        return QVariant(0);
    };
    
    // String functions
    m_builtInFunctions["LEN"] = [](const QList<QVariant>& args, VariableStorage* storage) -> QVariant {
        if (args.isEmpty()) return QVariant(0);
        return QVariant(args.first().toString().length());
    };
    
    m_builtInFunctions["MID"] = [](const QList<QVariant>& args, VariableStorage* storage) -> QVariant {
        if (args.size() < 2) return QVariant("");
        QString str = args.first().toString();
        int start = args.at(1).toInt();
        if (start <= 0 || start > str.length()) return QVariant("");
        if (args.size() >= 3) {
            int length = args.at(2).toInt();
            return QVariant(str.mid(start - 1, length));
        }
        return QVariant(str.mid(start - 1));
    };
    
    m_builtInFunctions["LEFT"] = [](const QList<QVariant>& args, VariableStorage* storage) -> QVariant {
        if (args.size() < 2) return QVariant("");
        QString str = args.first().toString();
        int length = args.at(1).toInt();
        if (length <= 0) return QVariant("");
        return QVariant(str.left(length));
    };
    
    m_builtInFunctions["RIGHT"] = [](const QList<QVariant>& args, VariableStorage* storage) -> QVariant {
        if (args.size() < 2) return QVariant("");
        QString str = args.first().toString();
        int length = args.at(1).toInt();
        if (length <= 0 || length > str.length()) return QVariant(str);
        return QVariant(str.right(length));
    };
}

QVariant FunctionSystem::evaluateExpression(const QString& expression, VariableStorage* storage)
{
    // This would use the expression evaluator to process expressions
    // For now return a placeholder
    return QVariant();
}