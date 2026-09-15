#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include "csv_loader.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    qDebug() << "CSV Loader Test";
    qDebug() << "===============";
    
    // Get directory from command line arguments
    QString testDir = argc > 1 ? argv[1] : ".";
    qDebug() << "\n1. Testing directory:" << testDir;
    
    // Test 1: Create a CSV loader instance
    qDebug() << "\n2. Creating CsvLoader:";
    CsvLoader loader;
    qDebug() << "   CsvLoader created successfully";
    
    // Test 2: Load a CSV file from eraTW
    qDebug() << "\n3. Loading CSV file from eraTW:";
    QString csvPath = QDir(testDir).filePath("CSV/FLAG.csv");
    if (loader.loadFile(csvPath)) {
        qDebug() << "   Successfully loaded CSV file:" << csvPath;
    } else {
        qDebug() << "   Failed to load CSV file:" << csvPath;
        return 1;
    }
    
    // Test 3: Get table names
    qDebug() << "\n4. Getting table names:";
    QStringList tables = loader.getTableNames();
    qDebug() << "   Tables found:" << tables.join(", ");
    
    // Test 4: Get table info
    qDebug() << "\n5. Getting table information:";
    for (const QString& tableName : tables) {
        qDebug() << "   Table:" << tableName;
        qDebug() << "   Rows:" << loader.getRowCount(tableName);
        qDebug() << "   Columns:" << loader.getColumnCount(tableName);
    }
    
    // Test 5: Get values using QVariant
    qDebug() << "\n6. Getting values from CSV (QVariant):";
    int rowCount = loader.getRowCount("FLAG");
    int colCount = loader.getColumnCount("FLAG");
    for (int row = 0; row < qMin(5, rowCount); row++) {
        for (int col = 0; col < qMin(5, colCount); col++) {
            QVariant value = loader.getValue("FLAG", row, col);
            qDebug() << "   loader.getValue(\"FLAG\", " << row << ", " << col << ") =" << value.toString();
        }
    }
    
    // Test 6: Test string extraction
    qDebug() << "\n7. Testing String extraction:";
    for (int row = 0; row < qMin(5, rowCount); row++) {
        QString stringValue = loader.getString("FLAG", row, 1);  // Second column
        qDebug() << "   loader.getString(\"FLAG\", " << row << ", 1) =" << stringValue;
    }
    
    // Test 7: Test int extraction
    qDebug() << "\n8. Testing Int extraction:";
    for (int row = 0; row < qMin(5, rowCount); row++) {
        int intValue = loader.getInt("FLAG", row, 2);  // Third column
        qDebug() << "   loader.getInt(\"FLAG\", " << row << ", 2) =" << intValue;
    }
    
    // Test 8: Test with a different CSV file
    qDebug() << "\n9. Testing with another CSV file (if exists):";
    QStringList csvFiles = {"CSV/STR.csv", "CSV/Item.csv"};
    for (const QString& csvFile : csvFiles) {
        QString filePath = QDir(testDir).filePath(csvFile);
        if (QFile::exists(filePath)) {
            qDebug() << "   Testing:" << filePath;
            if (loader.loadFile(filePath)) {
                qDebug() << "   Successfully loaded:" << filePath;
                QStringList newTables = loader.getTableNames();
                qDebug() << "   Tables found:" << newTables.join(", ");
            } else {
                qDebug() << "   Failed to load:" << filePath;
            }
        }
    }
    
    qDebug() << "\nCSV loader test complete!";
    
    return 0;
}
