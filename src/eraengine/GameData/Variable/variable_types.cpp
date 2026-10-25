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
