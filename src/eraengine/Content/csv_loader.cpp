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
#include "csv_loader.h"
#include "text_encoding.h"
#include <QRegularExpression>
#include <QFileInfo>

CsvLoader::CsvLoader(QObject* parent)
    : QObject(parent)
{
}

bool CsvLoader::loadFile(const QString& filePath) {
    // 编码按文件嗅探（CSV 在日文游戏里常为 Shift-JIS，汉化版为 UTF-8）
    bool ok = false;
    const QString content = TextCodecUtil::readFile(filePath, TextEncoding::Auto, nullptr, &ok);
    if (!ok) {
        return false;
    }

    // Parse the CSV content
    loadCsvData(filePath);

    return true;
}

void CsvLoader::loadCsvData(const QString& filePath) {
    // For now, just load basic CSV format
    // In a full implementation, this would parse all CSV files
    // and store them in m_data
    
    QFileInfo fileInfo(filePath);
    QString tableName = fileInfo.baseName();
    
    // Parse each line of the file
    QStringList lines = readFileLines(filePath);
    QList<QList<QString>> tableData;
    
    for (const QString& line : lines) {
        QStringList values = parseLine(line);
        if (!values.isEmpty()) {
            tableData.append(values);
        }
    }
    
    // Store the data
    m_data.insert(tableName, tableData);
    m_rowCounts.insert(tableName, tableData.size());
}

QStringList CsvLoader::readFileLines(const QString& filePath) {
    QStringList lines;
    bool ok = false;
    const QString text = TextCodecUtil::readFile(filePath, TextEncoding::Auto, nullptr, &ok);
    if (!ok) {
        return lines;
    }
    lines = text.split(QLatin1Char('\n'));
    for (QString& l : lines) {
        if (l.endsWith(QLatin1Char('\r'))) l.chop(1);
    }
    return lines;
}

QStringList CsvLoader::parseLine(const QString& line) {
    QStringList values;
    
    // Simple CSV parsing - split by comma
    // In a full implementation, would handle quoted strings and escaped commas
    values = line.split(',');
    
    return values;
}

QVariant CsvLoader::getValue(const QString& tableName, int row, int col) {
    if (!m_data.contains(tableName)) {
        return QVariant();
    }
    
    QList<QList<QString>>& table = m_data[tableName];
    if (row < 0 || row >= table.size() || col < 0) {
        return QVariant();
    }
    
    if (col >= table[row].size()) {
        return QVariant();
    }
    
    // Try to convert to int if possible
    bool ok;
    int value = table[row][col].toInt(&ok);
    if (ok) {
        return value;
    }
    
    return table[row][col];
}

QString CsvLoader::getString(const QString& tableName, int row, int col) {
    if (!m_data.contains(tableName)) {
        return "";
    }
    
    QList<QList<QString>>& table = m_data[tableName];
    if (row < 0 || row >= table.size() || col < 0) {
        return "";
    }
    
    if (col >= table[row].size()) {
        return "";
    }
    
    return table[row][col];
}

int CsvLoader::getInt(const QString& tableName, int row, int col) {
    return getValue(tableName, row, col).toInt();
}

QStringList CsvLoader::getTableNames() const {
    return m_data.keys();
}

int CsvLoader::getRowCount(const QString& tableName) const {
    if (!m_rowCounts.contains(tableName)) {
        return 0;
    }
    return m_rowCounts[tableName];
}

int CsvLoader::getColumnCount(const QString& tableName) const {
    if (!m_data.contains(tableName)) {
        return 0;
    }
    
    const QList<QList<QString>>& table = m_data[tableName];
    if (table.isEmpty()) {
        return 0;
    }
    
    return table[0].size();
}
