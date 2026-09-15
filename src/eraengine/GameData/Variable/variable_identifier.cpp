#include "variable_identifier.h"
#include "system_variables.h"
#include <QHash>

VariableIdentifier::VariableIdentifier(const QString &name, const VariableTypeInfo &typeInfo)
    : m_name(name), m_typeInfo(typeInfo)
{
}

VariableIdentifier VariableIdentifier::fromName(const QString &name)
{
    static const QHash<QString, VariableTypeInfo> variableTypeMap = []() {
        QHash<QString, VariableTypeInfo> map;
        for (const auto &entry : SYSTEM_VARIABLES) {
            map[entry.name] = entry.info;
        }
        return map;
    }();

    auto it = variableTypeMap.constFind(name);
    if (it != variableTypeMap.constEnd()) {
        return VariableIdentifier(name, it.value());
    }
    
    // Return null identifier for unknown variables
    return VariableIdentifier();
}
