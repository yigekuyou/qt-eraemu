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
#ifndef CSV_LOADER_H
#define CSV_LOADER_H

#include <QObject>
#include <QString>
#include <QHash>
#include <QList>
#include <QFile>
#include <QTextStream>

// CSV data loader - loads CSV files for game data
class CsvLoader : public QObject {
    Q_OBJECT

public:
    explicit CsvLoader(QObject* parent = nullptr);
    
    // Load CSV file
    bool loadFile(const QString& filePath);
    
    // Access data
    QVariant getValue(const QString& tableName, int row, int col);
    QString getString(const QString& tableName, int row, int col);
    int getInt(const QString& tableName, int row, int col);
    
    // Get table info
    QStringList getTableNames() const;
    int getRowCount(const QString& tableName) const;
    int getColumnCount(const QString& tableName) const;
    
private:
    // Parse a single CSV line
		QStringList parseLine(const QString& line);
    // Load CSV content into data structure
		void loadCsvData(const QString& filePath, const QString& content);
		QStringList getRowByFirstColumn(const QString& tableName, const QString& key) const;
    
    // CSV data storage: table name -> rows -> columns
    QHash<QString, QList<QList<QString>>> m_data;
    QHash<QString, int> m_rowCounts;
};

#endif // CSV_LOADER_H
