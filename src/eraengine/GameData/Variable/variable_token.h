#ifndef VARIABLE_TOKEN_H
#define VARIABLE_TOKEN_H

#include <QString>
#include "variable_types.h"
#include "variable_identifier.h"

class VariableStorage; // Forward declaration

class VariableToken
{
private:
    QString name;
    VariableTypeInfo typeInfo;
    bool isValid;
    mutable VariableStorage* storage = nullptr;

public:
    VariableToken();
    VariableToken(const QString &name, const VariableTypeInfo &typeInfo, bool isValid = true);
    void setStorage(VariableStorage* storage) { this->storage = storage; }

    const QString& getName() const { return name; }
    const VariableTypeInfo& getTypeInfo() const { return typeInfo; }
    bool isValidToken() const { return isValid; }

    bool isInteger() const { return typeInfo.isInteger; }
    bool isString() const { return typeInfo.isString; }
    bool isCharacterData() const { return typeInfo.isCharacterData; }
    bool is1D() const { return typeInfo.dimension == VariableTypes::Dimension::OneD; }
    bool is2D() const { return typeInfo.dimension == VariableTypes::Dimension::TwoD; }
    bool is3D() const { return typeInfo.dimension == VariableTypes::Dimension::ThreeD; }

    // These will be implemented in cpp file with access to VariableStorage
    qint64 getIntValue(int index) const;
    void setIntValue(int index, qint64 value) const;
    QString getStrValue(int index) const;
    void setStrValue(int index, const QString& value) const;
};

#endif // VARIABLE_TOKEN_H