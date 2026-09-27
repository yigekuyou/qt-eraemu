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
#ifndef VARIABLE_CONFIG_H
#define VARIABLE_CONFIG_H

#include <QString>
#include <QHash>
#include <QPair>
#include <QVector>
#include <QMap>

struct VariableSizeInfo {
    int size1D = 0;
    QPair<int, int> size2D;
    QVector<int> size3D;
    bool is2D = false;
    bool is3D = false;
};

class VariableConfig
{
private:
    QMap<QString, VariableSizeInfo> variableSizes;
    
public:
    VariableConfig();
    
    bool loadFromCSV(const QString& filePath);
    void loadDefaults();
    
    int getSize1D(const QString& name) const;
    QPair<int, int> getSize2D(const QString& name) const;
    QVector<int> getSize3D(const QString& name) const;
    
    // Get all variable sizes for debugging/inspection
    QMap<QString, VariableSizeInfo> getAllVariableSizes() const { return variableSizes; }
};

#endif // VARIABLE_CONFIG_H