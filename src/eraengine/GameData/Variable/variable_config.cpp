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
#include "variable_config.h"
#include "text_encoding.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

VariableConfig::VariableConfig()
{
    loadDefaults();
}

bool VariableConfig::loadFromCSV(const QString& filePath)
{
    // 编码按文件嗅探（VariableSize.csv 常见 Shift-JIS 或 UTF-8）
    bool readOk = false;
    const QString text = TextCodecUtil::readFile(filePath, TextEncoding::Auto, nullptr, &readOk);
    if (!readOk) {
        return false;
    }

    const QStringList rawLines = text.split(QLatin1Char('\n'));
    for (QString line : rawLines) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        line = line.trimmed();
        
        // Skip comments and empty lines
        if (line.isEmpty() || line.startsWith(";") || line.startsWith("#")) {
            continue;
        }
        
        // Parse line: variable_name,size1[,size2[,size3]]
        QStringList parts = line.split(',');
        if (parts.size() >= 2) {
            QString name = parts[0].trimmed();
            VariableSizeInfo info;
            
            // Check if it's a 2D or 3D variable
	    if (parts.size() >= 4) {
		    info.is3D = true;
		    info.is2D = false;
		    info.size3D = QVector<int>{parts[1].trimmed().toInt(), parts[2].trimmed().toInt(), parts[3].trimmed().toInt()};
	    } else if (parts.size() >= 3) {
		    info.is2D = true;
		    info.is3D = false;
		    info.size2D = QPair<int, int>(parts[1].trimmed().toInt(), parts[2].trimmed().toInt());
	    } else {
				info.size1D = parts[1].trimmed().toInt();
	    }
            
            variableSizes[name] = info;
        }
    }

    return true;
}

void VariableConfig::loadDefaults()
{
    // Load default sizes from VariableSize.csv structure
    // These match what's in eraTW/CSV/VariableSize.csv
    
    // 1D system variables
    variableSizes["DAY"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["MONEY"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["TIME"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["ITEM"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["ITEMSALES"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    // ITEMPRICE 在 C# 侧默认 1000（VariableSize.csv 里常被注释掉），缺注册会让
    // getSize1D 返回 0 -> Item.csv 第 3 列的价格无法装载
    variableSizes["ITEMPRICE"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["NOITEM"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["BOUGHT"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["PBAND"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["FLAG"] = VariableSizeInfo{10000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["TFLAG"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["TARGET"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["MASTER"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["PLAYER"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["ASSI"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["ASSIPLAY"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["UP"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["DOWN"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["LOSEBASE"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["PALAMLV"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["EXPLV"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["EJAC"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["PREVCOM"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["SELECTCOM"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["NEXTCOM"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["RESULT"] = VariableSizeInfo{1500, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["COUNT"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["A"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["B"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["C"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["D"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["E"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["F"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["G"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["H"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["I"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["J"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["K"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["L"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["M"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["N"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["O"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["P"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["Q"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["R"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["S"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["T"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["U"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["V"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["W"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["X"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["Y"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["Z"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    
    // 1D string variables
    variableSizes["SAVESTR"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["RESULTS"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["TSTR"] = VariableSizeInfo{400, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["ITEMNAME"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["ABLNAME"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["TALENTNAME"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["EXPNAME"] = VariableSizeInfo{200, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["MARKNAME"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["PALAMNAME"] = VariableSizeInfo{200, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["TRAINNAME"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["BASENAME"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["SOURCENAME"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["EXNAME"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["EQUIPNAME"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["TEQUIPNAME"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["FLAGNAME"] = VariableSizeInfo{10000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["TFLAGNAME"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["STR"] = VariableSizeInfo{20000, QPair<int, int>(), QVector<int>(), false, false};
    
    // 1D character variables
    variableSizes["BASE"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["MAXBASE"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["ABL"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["TALENT"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["EXP"] = VariableSizeInfo{200, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["MARK"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["PALAM"] = VariableSizeInfo{200, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["SOURCE"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["EX"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["CFLAG"] = VariableSizeInfo{10000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["JUEL"] = VariableSizeInfo{200, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["RELATION"] = VariableSizeInfo{500, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["EQUIP"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["TEQUIP"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["STAIN"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["GOTJUEL"] = VariableSizeInfo{200, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["NOWEX"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["TCVAR"] = VariableSizeInfo{1000, QPair<int, int>(), QVector<int>(), false, false};
    
    // 1D character string variables
    variableSizes["CSTR"] = VariableSizeInfo{100, QPair<int, int>(), QVector<int>(), false, false};
    variableSizes["CFLAGNAME"] = VariableSizeInfo{10000, QPair<int, int>(), QVector<int>(), false, false};
    
    // 2D variables
    variableSizes["DITEMTYPE"] = VariableSizeInfo{0, QPair<int, int>(1000, 1000), QVector<int>(), true, false};
    variableSizes["DA"] = VariableSizeInfo{0, QPair<int, int>(100, 100), QVector<int>(), true, false};
    variableSizes["DB"] = VariableSizeInfo{0, QPair<int, int>(100, 100), QVector<int>(), true, false};
    variableSizes["DC"] = VariableSizeInfo{0, QPair<int, int>(100, 100), QVector<int>(), true, false};
    variableSizes["DD"] = VariableSizeInfo{0, QPair<int, int>(100, 100), QVector<int>(), true, false};
    variableSizes["DE"] = VariableSizeInfo{0, QPair<int, int>(100, 100), QVector<int>(), true, false};
}

int VariableConfig::getSize1D(const QString& name) const
{
    auto it = variableSizes.find(name);
    if (it != variableSizes.end() && !it.value().is2D && !it.value().is3D) {
        return it.value().size1D;
    }
    return 0;
}

QPair<int, int> VariableConfig::getSize2D(const QString& name) const
{
    auto it = variableSizes.find(name);
    if (it != variableSizes.end() && it.value().is2D) {
        return it.value().size2D;
    }
    return QPair<int, int>(0, 0);
}

QVector<int> VariableConfig::getSize3D(const QString& name) const
{
    auto it = variableSizes.find(name);
    if (it != variableSizes.end() && it.value().is3D) {
        return it.value().size3D;
    }
    return QVector<int>();
}
