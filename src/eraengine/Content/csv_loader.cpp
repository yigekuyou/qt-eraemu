#include "csv_loader.h"
#include <QRegularExpression>
#include <QFileInfo>

CsvLoader::CsvLoader(QObject* parent)
    : QObject(parent)
{
}

bool CsvLoader::loadFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    
    // Read entire file content
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();
    
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
    QFile file(filePath);
    QStringList lines;
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return lines;
    }
    
    QTextStream in(&file);
    while (!in.atEnd()) {
        lines.append(in.readLine());
    }
    
    file.close();
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
