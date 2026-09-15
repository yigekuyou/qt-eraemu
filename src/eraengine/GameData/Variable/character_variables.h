#ifndef CHARACTER_VARIABLES_H
#define CHARACTER_VARIABLES_H

#include <QString>
#include <QList>
#include "variable_types.h"

struct CharacterVariableEntry {
		QString name;
		VariableTypeInfo info;
};

// Character data variables (16 variables from VariableSize.csv)
static const QList<CharacterVariableEntry> CHARACTER_VARIABLES = {
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

#endif // CHARACTER_VARIABLES_H