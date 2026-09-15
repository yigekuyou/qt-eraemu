#ifndef FUNCTION_SYSTEM_H
#define FUNCTION_SYSTEM_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QList>

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
    
    FunctionResult(bool succ, const QVariant& val, const QString& err = "")
        : success(succ), value(val), error(err) {}
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
    
    // Function execution
    FunctionResult executeFunction(const QString& name, const QList<QVariant>& args, VariableStorage* storage = nullptr);
    
    // Built-in functions
    void registerBuiltInFunctions();

private:
    QHash<QString, FunctionSignature> m_functions;
    QHash<QString, std::function<QVariant(const QList<QVariant>&, VariableStorage*)>> m_builtInFunctions;
    
    // Helper methods
    QVariant evaluateExpression(const QString& expression, VariableStorage* storage);
};

#endif // FUNCTION_SYSTEM_H