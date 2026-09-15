#ifndef VARIABLE_IDENTIFIER_H
#define VARIABLE_IDENTIFIER_H

#include <QString>
#include <QHash>
#include "variable_types.h"

class VariableIdentifier
{
public:
		VariableIdentifier() = default;
		VariableIdentifier(const QString &name, const VariableTypeInfo &typeInfo);

		QString name() const { return m_name; }
		VariableTypeInfo typeInfo() const { return m_typeInfo; }

		bool isNull() const { return m_name.isEmpty(); }
		bool isInteger() const { return m_typeInfo.isInteger; }
		bool isString() const { return m_typeInfo.isString; }
		bool isCharacterData() const { return m_typeInfo.isCharacterData; }
		bool isLocal() const { return m_typeInfo.scope == VariableTypes::Scope::Local; }
		bool isGlobal() const { return m_typeInfo.scope == VariableTypes::Scope::Global; }
		bool is1D() const { return m_typeInfo.dimension == VariableTypes::Dimension::OneD; }
		bool is2D() const { return m_typeInfo.dimension == VariableTypes::Dimension::TwoD; }
		bool is3D() const { return m_typeInfo.dimension == VariableTypes::Dimension::ThreeD; }
		bool isUnchangeable() const { return m_typeInfo.flags == VariableTypes::Flag::Unchangeable; }

		static VariableIdentifier fromName(const QString &name);

private:
		QString m_name;
		VariableTypeInfo m_typeInfo;
};

#endif // VARIABLE_IDENTIFIER_H