/*
 * emuera —— Emuera（ERB 脚本引擎）的 Qt6 + QML/C++ 移植
 * Copyright (C) 2026  yigekuyou
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef VARIABLE_TYPES_H
#define VARIABLE_TYPES_H

// ---------------------------------------------------------------------------
// Variable core types.
//
// This single header replaces the former variable_types.h / character_variables.h
// / system_variables.h / variable_identifier.h / variable_token.h split.  They
// all describe the same thing -- the meta information of an ERA variable -- and
// were previously spread over five headers purely to mirror the C# file layout.
// ---------------------------------------------------------------------------

#include <QObject>
#include <QtGlobal>
#include <QString>
#include <QList>
#include <QHash>

class VariableTypes : public QObject
{
		Q_OBJECT

public:
		enum class Type : quint32 {
				Integer       = 0x00020000,
				String        = 0x00040000,
				Array1D       = 0x00080000,
				Array2D       = 0x08000000,
				Array3D       = 0x20000000,
				CharacterData = 0x00100000,
				Local         = 0x02000000,
				Global        = 0x04000000,
				Unchangeable  = 0x00400000,
				Calc          = 0x00800000,
				Constant      = 0x40000000
		};
		Q_ENUM(Type)

		enum class Scope : quint32 {
				Local         = 0x02000000,
				Global        = 0x04000000,
				CharacterData = 0x00100000
		};
		Q_ENUM(Scope)

		enum class Dimension : quint32 {
				None   = 0x00000000,
				OneD   = 0x00080000,
				TwoD   = 0x08000000,
				ThreeD = 0x20000000
		};
		Q_ENUM(Dimension)

		enum class Flag : quint32 {
				None         = 0x00000000,
				CanForbid    = 0x00010000,
				Unchangeable = 0x00400000,
				Calc         = 0x00800000,
				Constant     = 0x40000000
		};
		Q_ENUM(Flag)
};

// 变量类型元信息结构体
struct VariableTypeInfo {
		VariableTypes::Type type = VariableTypes::Type::Integer;
		VariableTypes::Scope scope = VariableTypes::Scope::Global;
		VariableTypes::Dimension dimension = VariableTypes::Dimension::None;
		VariableTypes::Flag flags = VariableTypes::Flag::None;

		int size1D = 0;
		int size2D = 0;
		int size3D = 0;

		bool isCharacterData = false;
		bool isString = false;
		bool isInteger = true;

		VariableTypeInfo() = default;

		VariableTypeInfo(VariableTypes::Type t,
										 VariableTypes::Scope s,
										 VariableTypes::Dimension d,
										 VariableTypes::Flag f,
										 int s1, int s2, int s3,
										 bool charaData, bool str, bool num)
				: type(t), scope(s), dimension(d), flags(f)
				, size1D(s1), size2D(s2), size3D(s3)
				, isCharacterData(charaData), isString(str), isInteger(num) {}
};

// ---------------------------------------------------------------------------
// Character data variables (16 variables from VariableSize.csv)
// ---------------------------------------------------------------------------
struct CharacterVariableEntry {
		QString name;
		VariableTypeInfo info;
};

inline const QList<CharacterVariableEntry> CHARACTER_VARIABLES = {
		{"BASE",     {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 100, 0, 0, true, false, true}},
		{"ABL",      {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 100, 0, 0, true, false, true}},
		{"TALENT",   {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, true, false, true}},
		{"EXP",      {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 200, 0, 0, true, false, true}},
		{"MARK",     {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 100, 0, 0, true, false, true}},
		{"PALAM",    {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 200, 0, 0, true, false, true}},
		{"SOURCE",   {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, true, false, true}},
		{"EX",       {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 100, 0, 0, true, false, true}},
		{"CFLAG",    {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 10000, 0, 0, true, false, true}},
		{"JUEL",     {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 200, 0, 0, true, false, true}},
		{"RELATION", {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 500, 0, 0, true, false, true}},
		{"EQUIP",    {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 100, 0, 0, true, false, true}},
		{"TEQUIP",   {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, true, false, true}},
		{"STAIN",    {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::None, 100, 0, 0, true, false, true}},
		{"GOTJUEL",  {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 200, 0, 0, true, false, true}},
		{"TCVAR",    {VariableTypes::Type::Integer, VariableTypes::Scope::CharacterData, VariableTypes::Dimension::OneD, VariableTypes::Flag::None, 1000, 0, 0, true, false, true}}
};

// ---------------------------------------------------------------------------
// System variables (28 variables from VariableSize.csv)
// ---------------------------------------------------------------------------
struct SystemVariableEntry {
		QString name;
		VariableTypeInfo info;
};

inline const QList<SystemVariableEntry> SYSTEM_VARIABLES = {
		{"DAY",       {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"MONEY",     {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"ITEM",      {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"ITEMSALES", {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"NOITEM",    {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"BOUGHT",    {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"PBAND",     {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"FLAG",      {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 10000, 0, 0, false, false, true}},
		{"TFLAG",     {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"TARGET",    {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"MASTER",    {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"PLAYER",    {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"ASSI",      {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"ASSIPLAY",  {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"UP",        {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"DOWN",      {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"LOSEBASE",  {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"PALAMLV",   {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"EXPLV",     {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"EJAC",      {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"PREVCOM",   {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"SELECTCOM", {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"NEXTCOM",   {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"RESULT",    {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::None, 1500, 0, 0, false, false, true}},
		{"COUNT",     {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::None, 1000, 0, 0, false, false, true}},
		{"A",         {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"B",         {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}},
		{"C",         {VariableTypes::Type::Integer, VariableTypes::Scope::Global, VariableTypes::Dimension::OneD, VariableTypes::Flag::CanForbid, 1000, 0, 0, false, false, true}}
};

// ---------------------------------------------------------------------------
// A resolved variable name together with its meta information.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// A bound variable reference (name + type + backing storage).
// ---------------------------------------------------------------------------
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

    // These are implemented in variable_types.cpp with access to VariableStorage
    qint64 getIntValue(int index) const;
    void setIntValue(int index, qint64 value) const;
    QString getStrValue(int index) const;
    void setStrValue(int index, const QString& value) const;
};

#endif // VARIABLE_TYPES_H
