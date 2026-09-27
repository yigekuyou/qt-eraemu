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
#ifndef EFFICIENT_CSV_LOADER_H
#define EFFICIENT_CSV_LOADER_H

#include <QObject>
#include <QString>
#include <QStringView>
#include <QHash>
#include <QList>
#include <QVariant>
#include <QMap>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QCryptographicHash>
#include <vector>
#include <string_view>

// Efficient CSV parser using QStringView to minimize allocations
class EfficientCsvLoader {
public:
    // CSV data structure: table name -> rows -> columns
    using RowData = QList<QString>;
    using TableData = QList<RowData>;
    using DataMap = QHash<QString, TableData>;
    
    EfficientCsvLoader();
    ~EfficientCsvLoader() = default;
    
    // Load a CSV file efficiently
    bool loadFile(const QString& filePath);
    
    // Load from cache if available and valid, otherwise parse from source
    bool loadFromCacheOrSource(const QString& filePath);
    
    // Load from source file (bypass cache)
    bool loadFromSource(const QString& filePath);
    
    // Save parsed data to cache
    bool saveToCache(const QString& filePath);
    
    // Get data by table name
    const TableData* getTable(const QString& tableName) const;
    
    // Get all table names
    QStringList getTableNames() const;
    
    // Get row/column count for a table
    int getRowCount(const QString& tableName) const;
    int getColumnCount(const QString& tableName) const;
    
    // Get values with type conversion
    QString getString(const QString& tableName, int row, int col) const;
    int getInt(const QString& tableName, int row, int col, bool* ok = nullptr) const;
    qint64 getLongLong(const QString& tableName, int row, int col, bool* ok = nullptr) const;
    double getDouble(const QString& tableName, int row, int col, bool* ok = nullptr) const;
    
    // Get all data for debugging
    const DataMap& getDataMap() const { return m_data; }
    
    // Cache file path
    QString getCacheFilePath(const QString& filePath) const;
    
    // JSON serialization for caching
    QJsonDocument serializeToJson() const;
    bool deserializeFromJson(const QJsonDocument& doc);

private:
    // Parse a line efficiently using QStringView
    RowData parseLine(const QString& line);
    
    // Trim whitespace from a string view (in-place, no allocation)
    QStringView trimStringView(QStringView view) const;
    
    // Parse a number from string view
    int parseInt(QStringView view, bool* ok = nullptr) const;
    qint64 parseLongLong(QStringView view, bool* ok = nullptr) const;
    double parseDouble(QStringView view, bool* ok = nullptr) const;
    
    // Load CSV content
    void loadCsvData(const QString& filePath);
    
    // Read file into lines (minimal allocation)
    QStringList readFileLines(const QString& filePath);
    
    // Extract table name from file path
    QString extractTableName(const QString& filePath);
    
    // Check if cache is valid (source file hasn't changed)
    bool isCacheValid(const QString& cacheFilePath, const QString& sourceFilePath) const;
    
    // Get file hash for cache validation
    QString getFileHash(const QString& filePath) const;
    
    DataMap m_data;
};

#endif // EFFICIENT_CSV_LOADER_H
