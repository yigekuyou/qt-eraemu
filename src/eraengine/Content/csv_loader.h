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
    void loadCsvData(const QString& filePath);
    
    // Read file lines helper
    QStringList readFileLines(const QString& filePath);
    
    // CSV data storage: table name -> rows -> columns
    QHash<QString, QList<QList<QString>>> m_data;
    QHash<QString, int> m_rowCounts;
};

#endif // CSV_LOADER_H
