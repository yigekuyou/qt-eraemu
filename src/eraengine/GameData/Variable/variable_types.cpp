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
#include "variable_types.h"
#include "variable_storage.h"

// ---------------------------------------------------------------------------
// VariableIdentifier
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// VariableToken
// ---------------------------------------------------------------------------

VariableToken::VariableToken()
    : name(""), typeInfo(), isValid(false)
{
}

VariableToken::VariableToken(const QString &name, const VariableTypeInfo &typeInfo, bool isValid)
    : name(name), typeInfo(typeInfo), isValid(isValid)
{
}

qint64 VariableToken::getIntValue(int) const
{
    if (!isValid || !typeInfo.isInteger || storage == nullptr) return 0;

    // This is a simplified implementation that would need to be expanded to handle
    // different variable types dynamically based on typeInfo
    // For now, returning a placeholder - real implementation would use reflection
    return 0;
}

void VariableToken::setIntValue(int, qint64) const
{
    if (!isValid || !typeInfo.isInteger || storage == nullptr) return;

    // This is a simplified implementation that would need to be expanded to handle
    // different variable types dynamically based on typeInfo
    // For now, doing nothing - real implementation would use reflection
    return;
}

QString VariableToken::getStrValue(int) const
{
    if (!isValid || !typeInfo.isString || storage == nullptr) return "";

    // This is a simplified implementation that would need to be expanded to handle
    // different variable types dynamically based on typeInfo
    // For now, returning a placeholder - real implementation would use reflection
    return "";
}

void VariableToken::setStrValue(int, const QString&) const
{
    if (!isValid || !typeInfo.isString || storage == nullptr) return;

    // This is a simplified implementation that would need to be expanded to handle
    // different variable types dynamically based on typeInfo
    // For now, doing nothing - real implementation would use reflection
    return;
}
