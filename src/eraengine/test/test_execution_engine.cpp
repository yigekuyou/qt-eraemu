#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include "execution_engine.h"
#include "variable_storage.h"
#include "erb_loader.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    qDebug() << "Execution Engine Test";
    qDebug() << "=====================";
    
    // Get directory from command line arguments
    QString testDir = argc > 1 ? argv[1] : ".";
    qDebug() << "\n1. Testing directory:" << testDir;
    
    // Test 1: Create components
    qDebug() << "\n2. Creating ExecutionEngine components:";
    VariableStorage storage;
    ExecutionEngine engine(&storage);
    qDebug() << "   VariableStorage created";
    qDebug() << "   ExecutionEngine created with storage";
    
    // Test 2: Create a simple ERB script in memory
    qDebug() << "\n3. Creating test ERB script in directory:";
    QString testScript = R"(@START
    PRINT "Hello from ERB!"
    PRINT "This is a test"
    A = 10
    B = 20
    GOTO END

@END
    RESETDATA
    PRINT "Execution complete"
)";
    
    QString testFilePath = QDir(testDir).filePath("sample_execution.ERB");
    {
        QFile file(testFilePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << testScript;
            file.close();
            qDebug() << "   Test script saved to:" << testFilePath;
        } else {
            qDebug() << "   Failed to save test script";
            return -1;
        }
    }
    
    // Test 3: Load scripts from directory
    qDebug() << "\n4. Loading scripts from directory:";
    qDebug() << "   engine.loadScripts(\"" << testDir << "\")";
    if (engine.loadScripts(testDir)) {
        qDebug() << "   Scripts loaded successfully";
    } else {
        qDebug() << "   Failed to load scripts";
        return -1;
    }
    
    // Test 4: Execute script
    qDebug() << "\n5. Executing script:";
    qDebug() << "   engine.executeScript(\"sample_execution\")";
    engine.executeScript("sample_execution");
    
    // Test 5: Check execution state
    qDebug() << "\n6. Checking execution state after execution:";
    qDebug() << "   engine.isRunning():" << (engine.isRunning() ? "true" : "false");
    qDebug() << "   engine.getCurrentLine():" << engine.getCurrentLine();
    qDebug() << "   engine.getCurrentScript():" << engine.getCurrentScript();
    
    // Test 6: Test with variables in script
    qDebug() << "\n7. Creating variable test script:";
    QString variableScript = R"(@START
    A = 10
    B = 20
    PRINT "A + B = " + (A + B)
    C = A + B
    GOTO END

@END
    RESETDATA
    PRINT "Variable test complete"
)";
    
    QString varFilePath = QDir(testDir).filePath("variable_test.ERB");
    {
        QFile file(varFilePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << variableScript;
            file.close();
            qDebug() << "   Variable test script saved";
        }
    }
    
    // Test 7: Execute variable script
    qDebug() << "\n8. Executing variable script:";
    qDebug() << "   engine.executeScript(\"variable_test\")";
    engine.executeScript("variable_test");
    
    // Test 8: Check variable values after execution
    qDebug() << "\n9. Checking variable values after execution:";
    qDebug() << "   storage.getGlobalInt1D(\"A\", 0):" << storage.getGlobalInt1D("A", 0);
    qDebug() << "   storage.getGlobalInt1D(\"B\", 0):" << storage.getGlobalInt1D("B", 0);
    qDebug() << "   storage.getGlobalInt1D(\"C\", 0):" << storage.getGlobalInt1D("C", 0);
    
    // Test 9: Test with multiple scripts
    qDebug() << "\n10. Testing with multiple scripts:";
    QString multiScript = R"(@SCRIPT1
    A = 100
    GOTO END

@SCRIPT2
    B = 200
    GOTO END

@END
    RESETDATA
)";
    
    QString multiFilePath = QDir(testDir).filePath("multi_script.ERB");
    {
        QFile file(multiFilePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << multiScript;
            file.close();
            qDebug() << "   Multi-script file saved";
        }
    }
    
    // Test 10: Execute different scripts
    qDebug() << "\n11. Testing script execution order:";
    qDebug() << "   First: engine.executeScript(\"multi_script\")";
    engine.executeScript("multi_script");
    qDebug() << "   After multi_script execution:";
    qDebug() << "       A:" << storage.getGlobalInt1D("A", 0);
    qDebug() << "       B:" << storage.getGlobalInt1D("B", 0);
    
    // Test 11: Test with file that doesn't exist
    qDebug() << "\n12. Testing error handling (non-existent script):";
    qDebug() << "   engine.executeScript(\"nonexistent\")";
    engine.executeScript("nonexistent");
    qDebug() << "   (Should handle gracefully without crashing)";
    
    // Test 12: Test with directory containing various files
    qDebug() << "\n13. Testing with directory:" << testDir;
    QDir dir(testDir);
    QStringList entries = dir.entryList(QStringList() << "*.ERB", QDir::Files);
    qDebug() << "   Found ERB files:" << entries.join(", ");
    
    for (const QString& file : entries) {
        QString fullPath = dir.filePath(file);
        qDebug() << "   -" << file << "(" << fullPath << ")";
    }
    
    qDebug() << "\nExecution engine test complete!";
    
    return 0;
}
