#include "variable_token.h"
#include "variable_storage.h"

VariableToken::VariableToken()
    : name(""), typeInfo(), isValid(false)
{
}

VariableToken::VariableToken(const QString &name, const VariableTypeInfo &typeInfo, bool isValid)
    : name(name), typeInfo(typeInfo), isValid(isValid)
{
}

qint64 VariableToken::getIntValue(int index) const
{
    if (!isValid || !typeInfo.isInteger || storage == nullptr) return 0;
    
    // This is a simplified implementation that would need to be expanded to handle
    // different variable types dynamically based on typeInfo
    // For now, returning a placeholder - real implementation would use reflection
    return 0;
}

void VariableToken::setIntValue(int index, qint64 value) const
{
    if (!isValid || !typeInfo.isInteger || storage == nullptr) return;
    
    // This is a simplified implementation that would need to be expanded to handle
    // different variable types dynamically based on typeInfo
    // For now, doing nothing - real implementation would use reflection
    return;
}

QString VariableToken::getStrValue(int index) const
{
    if (!isValid || !typeInfo.isString || storage == nullptr) return "";
    
    // This is a simplified implementation that would need to be expanded to handle
    // different variable types dynamically based on typeInfo
    // For now, returning a placeholder - real implementation would use reflection
    return "";
}

void VariableToken::setStrValue(int index, const QString& value) const
{
    if (!isValid || !typeInfo.isString || storage == nullptr) return;
    
    // This is a simplified implementation that would need to be expanded to handle
    // different variable types dynamically based on typeInfo
    // For now, doing nothing - real implementation would use reflection
    return;
}