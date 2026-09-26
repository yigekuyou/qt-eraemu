#ifndef FUNCTION_SYSTEM_H
#define FUNCTION_SYSTEM_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QList>
#include <QStack>
#include <QPair>
#include "script_line.h"

class VariableStorage; // Forward declaration

// Function parameter type enumeration
enum class FunctionParamType {
    INTEGER,
    STRING,
    ANY
};

// Function parameter information
struct FunctionParam {
    QString name;
    FunctionParamType type;
    bool isOptional;
    
    FunctionParam(const QString& paramName, FunctionParamType paramType, bool optional = false)
        : name(paramName), type(paramType), isOptional(optional) {}
};

// Function signature
struct FunctionSignature {
    QString name;
    QList<FunctionParam> params;
    bool isBuiltIn;
    QString description;
    
    FunctionSignature() : isBuiltIn(true) {}
    FunctionSignature(const QString& funcName, const QList<FunctionParam>& parameters, bool builtIn = true, const QString& desc = "")
        : name(funcName), params(parameters), isBuiltIn(builtIn), description(desc) {}
};

// Function result
struct FunctionResult {
    bool success;
    QVariant value;
    QString error;
    ScriptPosition position;  // Add position for error context
    
    FunctionResult(bool succ, const QVariant& val, const QString& err = "", 
                   const ScriptPosition& pos = ScriptPosition())
        : success(succ), value(val), error(err), position(pos) {}
};

// Function system interface
class FunctionSystem : public QObject
{
    Q_OBJECT

public:
    explicit FunctionSystem(QObject *parent = nullptr);
    
    // Function registration
    bool registerFunction(const FunctionSignature& signature);
    bool unregisterFunction(const QString& name);
    
    // Function lookup
    bool hasFunction(const QString& name) const;
    FunctionSignature getFunctionSignature(const QString& name) const;
    
    // Enhanced function execution with error context
    FunctionResult executeFunction(const QString& name, const QList<QVariant>& args, 
                                   VariableStorage* storage, 
                                   const ScriptPosition& pos = ScriptPosition());
    
    // Built-in functions for variable operations
    void registerBuiltInFunctions();
    
    // Function call stack management
    void pushCallFrame(const QString& label, const ScriptPosition& pos);
    void popCallFrame();
    QString getCurrentCallFunction() const;

private:
    QHash<QString, FunctionSignature> m_functions;
    QHash<QString, std::function<QVariant(const QList<QVariant>&, VariableStorage*)>> m_builtInFunctions;
    
    // Function call stack
    QStack<QPair<QString, ScriptPosition>> m_callStack;
    
    // Helper methods
    QVariant evaluateExpression(const QString& expression, VariableStorage* storage);
};

#endif // FUNCTION_SYSTEM_H