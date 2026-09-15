#ifndef SYSTEM_VARIABLES_H
#define SYSTEM_VARIABLES_H

#include <QString>
#include <QList>
#include "variable_types.h"

struct SystemVariableEntry {
		QString name;
		VariableTypeInfo info;
};

// System variables (28 variables from VariableSize.csv)
static const QList<SystemVariableEntry> SYSTEM_VARIABLES = {
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

#endif // SYSTEM_VARIABLES_H