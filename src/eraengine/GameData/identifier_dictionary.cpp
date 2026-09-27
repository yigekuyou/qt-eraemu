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
#include "identifier_dictionary.h"
#include <QSet>

// Static initialization
const QList<QString> IdentifierDictionary::RESERVED_WORDS = {
    "IS", "TO", "INT", "STR", "REFFUNC", "STATIC", "DYNAMIC",
    "GLOBAL", "PRIVATE", "SAVEDATA", "CHARADATA", "REF",
    "__DEBUG__", "__SKIP__", "_"
};

const QList<QString> IdentifierDictionary::SYSTEM_INSTRUMENTS = {
    // Instructions - these would be loaded from FunctionIdentifier
    // For now, just the basic ones
};

const QList<QString> IdentifierDictionary::SYSTEM_METHODS = {
    // System methods - these would be loaded from FunctionMethodCreator
    // For now, empty list
};

const QList<QString> IdentifierDictionary::SYSTEM_VARIABLES = {
    // System variables - these would be loaded from VariableIdentifier
    // For now, empty list
};

IdentifierDictionary::IdentifierDictionary(QObject *parent)
    : QObject(parent)
{
    initializeReservedWords();
    initializeSystemNames();
}

void IdentifierDictionary::initializeReservedWords()
{
    for (const QString &word : RESERVED_WORDS) {
        m_nameDic[word] = DefinedNameType::Reserved;
    }
}

void IdentifierDictionary::initializeSystemNames()
{
    // System instruments (instructions)
    for (const QString &name : SYSTEM_INSTRUMENTS) {
        m_nameDic[name] = DefinedNameType::SystemInstrument;
    }

    // System methods
    for (const QString &name : SYSTEM_METHODS) {
        m_nameDic[name] = DefinedNameType::SystemMethod;
    }

    // System variables
    for (const QString &name : SYSTEM_VARIABLES) {
        m_nameDic[name] = DefinedNameType::SystemVariable;
    }
}

bool IdentifierDictionary::isReserved(const QString &name) const
{
    return m_nameDic.value(name) == DefinedNameType::Reserved;
}

bool IdentifierDictionary::isSystemVariable(const QString &name) const
{
    return m_nameDic.value(name) == DefinedNameType::SystemVariable;
}

bool IdentifierDictionary::isSystemMethod(const QString &name) const
{
    return m_nameDic.value(name) == DefinedNameType::SystemMethod;
}

bool IdentifierDictionary::isSystemInstrument(const QString &name) const
{
    return m_nameDic.value(name) == DefinedNameType::SystemInstrument;
}

bool IdentifierDictionary::isUserGlobalVariable(const QString &name) const
{
    return m_userGlobalVariables.contains(name);
}

bool IdentifierDictionary::isUserMacro(const QString &name) const
{
    return m_userMacros.contains(name);
}

bool IdentifierDictionary::isUserRefMethod(const QString &name) const
{
    return m_userRefMethods.contains(name);
}

bool IdentifierDictionary::isNameInUse(const QString &name) const
{
    return m_nameDic.contains(name) ||
           m_userGlobalVariables.contains(name) ||
           m_userMacros.contains(name) ||
           m_userRefMethods.contains(name);
}

void IdentifierDictionary::addUserGlobalVariable(const QString &name)
{
    if (!m_userGlobalVariables.contains(name)) {
        m_userGlobalVariables.append(name);
        m_nameDic[name] = DefinedNameType::UserGlobalVariable;
    }
}

void IdentifierDictionary::addUserMacro(const QString &name)
{
    if (!m_userMacros.contains(name)) {
        m_userMacros.append(name);
        m_nameDic[name] = DefinedNameType::UserMacro;
    }
}

void IdentifierDictionary::addUserRefMethod(const QString &name)
{
    if (!m_userRefMethods.contains(name)) {
        m_userRefMethods.append(name);
        m_nameDic[name] = DefinedNameType::UserRefMethod;
    }
}

bool IdentifierDictionary::isReservedWord(const QString &name) const
{
    return RESERVED_WORDS.contains(name);
}

bool IdentifierDictionary::isSystemName(const QString &name) const
{
    return SYSTEM_INSTRUMENTS.contains(name) ||
           SYSTEM_METHODS.contains(name) ||
           SYSTEM_VARIABLES.contains(name);
}
