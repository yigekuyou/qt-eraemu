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
#include "efficient_csv_loader.h"
#include <QRegularExpression>
#include <QChar>
#include <cmath>
#include <QFileInfo>
#include <QStandardPaths>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

EfficientCsvLoader::EfficientCsvLoader() {
    // Pre-allocate common table sizes for better performance
    m_data.reserve(32);
}

QString EfficientCsvLoader::getCacheFilePath(const QString& filePath) const {
    // Generate cache directory path
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (cacheDir.isEmpty()) {
        cacheDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    
    if (cacheDir.isEmpty()) {
        cacheDir = ".";
    }
    
    // Create a unique cache file name based on the source file path
    QString relativePath = filePath;
    relativePath.replace(QChar('/'), QChar('_'));
    relativePath.replace(QChar('\\'), QChar('_'));
    relativePath.replace(QChar(':'), QChar('_'));
    
    return cacheDir + "/csv_cache_" + relativePath + ".bin";
}

QString EfficientCsvLoader::getFileHash(const QString& filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (hash.addData(&file)) {
        return hash.result().toHex();
    }
    
    return QString();
}

bool EfficientCsvLoader::isCacheValid(const QString& cacheFilePath, const QString& sourceFilePath) const {
    // Check if cache file exists
    QFileInfo cacheInfo(cacheFilePath);
    if (!cacheInfo.exists()) {
        return false;
    }
    
    // Check if source file exists
    QFileInfo sourceInfo(sourceFilePath);
    if (!sourceInfo.exists()) {
        return false;
    }
    
    // Check if source file is newer than cache
    if (sourceInfo.lastModified() > cacheInfo.lastModified()) {
        return false;
    }
    
    // Verify file hash matches
    QString sourceHash = getFileHash(sourceFilePath);
    if (sourceHash.isEmpty()) {
        return false;
    }
    
    // Read hash from cache file
    QFile cacheFile(cacheFilePath);
    if (!cacheFile.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    // First 64 bytes should contain the hash (SHA256 = 64 hex chars)
    QByteArray cacheData = cacheFile.read(64);
    cacheFile.close();
    
    return cacheData == sourceHash.toUtf8();
}

bool EfficientCsvLoader::loadFile(const QString& filePath) {
    // First try to load from cache if available
    if (loadFromCacheOrSource(filePath)) {
        return true;
    }
    
    // Fall back to parsing from source
    return loadFromSource(filePath);
}

bool EfficientCsvLoader::loadFromSource(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    
    // Read entire file content
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();
    
    if (content.isEmpty()) {
        return true;
    }
    
    // Parse and store the data
    loadCsvData(filePath);
    
    // Save to cache for next time
    saveToCache(filePath);
    
    return true;
}

bool EfficientCsvLoader::loadFromCacheOrSource(const QString& filePath) {
    // Check if cache is valid
    QString cachePath = getCacheFilePath(filePath);
    if (isCacheValid(cachePath, filePath)) {
        // Load from cache
        QFile cacheFile(cachePath);
        if (cacheFile.open(QIODevice::ReadOnly)) {
            // Read and parse cached data
            QByteArray cacheData = cacheFile.readAll();
            cacheFile.close();
            
            // Skip the hash (first 64 bytes)
            if (cacheData.size() > 64) {
                QByteArray jsonData = cacheData.mid(64);
                
                // Parse JSON data back into m_data
                // For now, fall back to source parsing
                // A full implementation would serialize m_data to JSON
                return loadFromSource(filePath);
            }
        }
    }
    
    // Cache not valid, load from source
    return loadFromSource(filePath);
}

bool EfficientCsvLoader::saveToCache(const QString& filePath) {
    // Get cache file path
    QString cachePath = getCacheFilePath(filePath);
    
    // Generate file hash
    QString fileHash = getFileHash(filePath);
    if (fileHash.isEmpty()) {
        return false;
    }
    
    // Serialize m_data to JSON
    QJsonDocument doc = serializeToJson();
    if (doc.isNull()) {
        return false;
    }
    
    QByteArray cacheData;
    QBuffer buffer(&cacheData);
    if (buffer.open(QIODevice::WriteOnly)) {
        // Write hash first (for validation)
        buffer.write(fileHash.toUtf8());
        // Write JSON data
        buffer.write(doc.toJson(QJsonDocument::Compact));
        buffer.close();
    }
    
    // Write to cache file
    QFile cacheFile(cachePath);
    if (!cacheFile.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    cacheFile.write(cacheData);
    cacheFile.close();
    
    return true;
}

void EfficientCsvLoader::loadCsvData(const QString& filePath) {
    // Extract table name from file path (basename without extension)
    QString tableName = extractTableName(filePath);
    
    // Read all lines
    QStringList lines = readFileLines(filePath);
    
    // Pre-allocate space (estimate ~100 rows per file)
    TableData tableData;
    tableData.reserve(lines.size());
    
    // Parse each line
    for (const QString& line : lines) {
        RowData row = parseLine(line);
        if (!row.isEmpty()) {
            tableData.append(row);
        }
    }
    
    // Store the data
    m_data.insert(tableName, tableData);
}

QString EfficientCsvLoader::extractTableName(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    return fileInfo.baseName().toUpper();
}

QStringList EfficientCsvLoader::readFileLines(const QString& filePath) {
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

QStringView EfficientCsvLoader::trimStringView(QStringView view) const {
    // Trim leading whitespace
    int start = 0;
    while (start < view.size() && view[start].isSpace()) {
        ++start;
    }
    
    // Trim trailing whitespace
    int end = view.size() - 1;
    while (end >= start && view[end].isSpace()) {
        --end;
    }
    
    // Return substring (no allocation if already trimmed)
    if (start == 0 && end == view.size() - 1) {
        return view;
    }
    return view.mid(start, end - start + 1);
}

EfficientCsvLoader::RowData EfficientCsvLoader::parseLine(const QString& line) {
    RowData values;
    
    // Skip empty lines and comments using view-only operations
    QStringView lineView = trimStringView(line);
    if (lineView.isEmpty() || lineView.startsWith(';')) {
        return values;
    }
    
    // Pre-allocate expected number of columns (typical CSV has 2-5 columns)
    values.reserve(8);
    
    // Efficient CSV parsing - split by comma
    // For production use, would handle quoted strings and escaped commas
    int start = 0;
    int len = lineView.size();
    
    while (start < len) {
        // Find the next comma
        int commaPos = lineView.indexOf(',', start);
        
        if (commaPos == -1) {
            // No more commas, take the rest
            QStringView field = trimStringView(lineView.mid(start));
            if (!field.isEmpty()) {
                values.append(field.toString());
            }
            break;
        } else {
            // Extract field up to comma
            QStringView field = trimStringView(lineView.mid(start, commaPos - start));
            if (!field.isEmpty()) {
                values.append(field.toString());
            }
            start = commaPos + 1;
        }
    }
    
    return values;
}

int EfficientCsvLoader::parseInt(QStringView view, bool* ok) const {
    QString trimmed = trimStringView(view).toString();
    return trimmed.toInt(ok);
}

qint64 EfficientCsvLoader::parseLongLong(QStringView view, bool* ok) const {
    QString trimmed = trimStringView(view).toString();
    return trimmed.toLongLong(ok);
}

double EfficientCsvLoader::parseDouble(QStringView view, bool* ok) const {
    QString trimmed = trimStringView(view).toString();
    return trimmed.toDouble(ok);
}

const EfficientCsvLoader::TableData* EfficientCsvLoader::getTable(const QString& tableName) const {
    auto it = m_data.find(tableName);
    if (it != m_data.constEnd()) {
        return &it.value();
    }
    return nullptr;
}

QStringList EfficientCsvLoader::getTableNames() const {
    return m_data.keys();
}

int EfficientCsvLoader::getRowCount(const QString& tableName) const {
    if (!m_data.contains(tableName)) {
        return 0;
    }
    return m_data[tableName].size();
}

int EfficientCsvLoader::getColumnCount(const QString& tableName) const {
    if (!m_data.contains(tableName)) {
        return 0;
    }
    
    const TableData& table = m_data[tableName];
    if (table.isEmpty()) {
        return 0;
    }
    
    // Return max column count
    int maxCols = 0;
    for (const RowData& row : table) {
        maxCols = qMax(maxCols, row.size());
    }
    return maxCols;
}

QString EfficientCsvLoader::getString(const QString& tableName, int row, int col) const {
    const TableData* table = getTable(tableName);
    if (!table || row < 0 || row >= table->size()) {
        return QString();
    }
    
    const RowData& rowData = (*table)[row];
    if (col < 0 || col >= rowData.size()) {
        return QString();
    }
    
    return rowData[col];
}

int EfficientCsvLoader::getInt(const QString& tableName, int row, int col, bool* ok) const {
    const TableData* table = getTable(tableName);
    if (!table || row < 0 || row >= table->size()) {
        if (ok) *ok = false;
        return 0;
    }
    
    const RowData& rowData = (*table)[row];
    if (col < 0 || col >= rowData.size()) {
        if (ok) *ok = false;
        return 0;
    }
    
    bool localOk = false;
    int value = parseInt(rowData[col], &localOk);
    if (ok) *ok = localOk;
    return value;
}

qint64 EfficientCsvLoader::getLongLong(const QString& tableName, int row, int col, bool* ok) const {
    const TableData* table = getTable(tableName);
    if (!table || row < 0 || row >= table->size()) {
        if (ok) *ok = false;
        return 0;
    }
    
    const RowData& rowData = (*table)[row];
    if (col < 0 || col >= rowData.size()) {
        if (ok) *ok = false;
        return 0;
    }
    
    return parseLongLong(rowData[col], ok);
}

double EfficientCsvLoader::getDouble(const QString& tableName, int row, int col, bool* ok) const {
    const TableData* table = getTable(tableName);
    if (!table || row < 0 || row >= table->size()) {
        if (ok) *ok = false;
        return 0.0;
    }
    
    const RowData& rowData = (*table)[row];
    if (col < 0 || col >= rowData.size()) {
        if (ok) *ok = false;
        return 0.0;
    }
    
    return parseDouble(rowData[col], ok);
}

QJsonDocument EfficientCsvLoader::serializeToJson() const {
    QJsonObject root;
    
    for (auto it = m_data.constBegin(); it != m_data.constEnd(); ++it) {
        QJsonArray tableArray;
        
        const TableData& table = it.value();
        for (const RowData& row : table) {
            QJsonArray rowArray;
            for (const QString& cell : row) {
                rowArray.append(cell);
            }
            tableArray.append(rowArray);
        }
        
        root[it.key()] = tableArray;
    }
    
    return QJsonDocument(root);
}

bool EfficientCsvLoader::deserializeFromJson(const QJsonDocument& doc) {
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }
    
    m_data.clear();
    
    QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        QJsonArray tableArray = it.value().toArray();
        TableData tableData;
        
        for (int i = 0; i < tableArray.size(); ++i) {
            QJsonArray rowArray = tableArray[i].toArray();
            RowData rowData;
            
            for (int j = 0; j < rowArray.size(); ++j) {
                rowData.append(rowArray[j].toString());
            }
            
            if (!rowData.isEmpty()) {
                tableData.append(rowData);
            }
        }
        
        m_data[it.key()] = tableData;
    }
    
    return true;
}
