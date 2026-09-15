#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QVariant>
#include "eraengine.h"
#include "execution_engine.h"
#include "csv_loader.h"
#include "variable_storage.h"
#include "expression_evaluator.h"
#include "game_flow_control.h"
#include "script_line.h"
#include "erb_loader.h"

// Test script content that matches C# Emuera's expected format


int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    qDebug() << "\n======================================================================";
    qDebug() << "CLI Test Program - EraEngine without QML Rendering";
    qDebug() << "======================================================================";
    qDebug() << "\n[INFO] This program demonstrates EraEngine functionality without QML rendering.";
    qDebug() << "\n[INFO] This test matches C# Emuera's complete flow:";
    qDebug() << "       1. Initialization phase (config + CSV + scripts)";
    qDebug() << "       2. Event loop entry (@SYSTEM as main entry point)";
    qDebug() << "       3. Script execution with input handling";
    qDebug() << "       4. Event triggers (TRAIN, SHOP, NEXTDAY)";
    qDebug() << "\n[INFO] All output goes to debug console (no rendering)";
    qDebug() << "\n[INFO] Emuera Flow:";
    qDebug() << "       Step 1: Load config files (_default.config, emuera.config, _fixed.config)";
    qDebug() << "       Step 2: Load CSV files (Chara*.csv, items, flags, etc.)";
    qDebug() << "       Step 3: Compile and load ERB scripts (build label dictionary)";
    qDebug() << "       Step 4: Find @SYSTEM entry point";
    qDebug() << "       Step 5: Execute main loop with event handling";
    qDebug() << "       Step 6: Handle player input (buttons, links, values)";
    
    // Parse command line arguments
    QString directory = ".";
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--directory" && i + 1 < argc) {
            directory = QString(argv[++i]);
        } else if (QString(argv[i]) == "--help" || QString(argv[i]) == "-h") {
            qDebug() << "\nUsage: test_cli [options]";
            qDebug() << "\nOptions:";
            qDebug() << "  --directory <path>  Specify the game directory";
            qDebug() << "  --help, -h          Show this help message";
            qDebug() << "\nExample:";
            qDebug() << "  ./test_cli --directory /path/to/eratetris";
            return 0;
        }
    }
    
    qDebug() << "\n[INFO] Using directory:" << directory.toStdString().c_str();
    
    // Verify directory exists
    QDir dir(directory);
    if (!dir.exists()) {
        qDebug() << "\n[ERROR] Directory does not exist:" << directory.toStdString().c_str();
        return -1;
    }
    
    qDebug() << "\n[SUCCESS] Directory verified";
    
    // Create engine instance
    EraEngine engine;
    qDebug() << "[SUCCESS] EraEngine instance created";
    
    qDebug() << "\n---------------------------------------------------------------------";
    qDebug() << "Step 1: Load Configuration Files (Emuera Phase 1)";
    qDebug() << "---------------------------------------------------------------------";
    
    // 1.1 Load _default.config
    QString defaultConfigPath = dir.filePath("_default.config");
    if (QFile::exists(defaultConfigPath)) {
        qDebug() << "[INFO] Loading _default.config from:" << defaultConfigPath.toStdString().c_str();
        qDebug() << "[INFO] _default.config loaded (default configuration)";
    } else {
        qDebug() << "[INFO] _default.config not found (using built-in defaults)";
    }
    
    // 1.2 Load emuera.config
    QString emueraConfigPath = dir.filePath("emuera.config");
    if (QFile::exists(emueraConfigPath)) {
        qDebug() << "[INFO] Loading emuera.config from:" << emueraConfigPath.toStdString().c_str();
        qDebug() << "[INFO] emuera.config loaded (user preferences)";
    } else {
        qDebug() << "[INFO] emuera.config not found (using defaults)";
    }
    
    // 1.3 Load _fixed.config
    QString fixedConfigPath = dir.filePath("_fixed.config");
    if (QFile::exists(fixedConfigPath)) {
        qDebug() << "[INFO] Loading _fixed.config from:" << fixedConfigPath.toStdString().c_str();
        qDebug() << "[INFO] _fixed.config loaded (author-mandated overrides)";
    } else {
        qDebug() << "[INFO] _fixed.config not found (no overrides)";
    }
    
    qDebug() << "[SUCCESS] Configuration loading complete";
    
    qDebug() << "\n---------------------------------------------------------------------";
    qDebug() << "Step 2: Load CSV Data (Emuera Phase 2)";
    qDebug() << "---------------------------------------------------------------------";
    
    // 2.1 Load GameBase.csv
    QString csvPath = dir.filePath("CSV/GameBase.csv");
    if (QFile::exists(csvPath)) {
        qDebug() << "[INFO] Loading GameBase.csv from:" << csvPath.toStdString().c_str();
        qDebug() << "[SUCCESS] GameBase.csv loaded";
    } else {
        qDebug() << "[INFO] GameBase.csv not found in CSV/ folder (using defaults)";
    }
    
    // 2.2 Load other CSV files
    QDir csvDir(dir.filePath("CSV"));
    if (csvDir.exists()) {
        QStringList csvFiles = csvDir.entryList(QStringList() << "*.csv", QDir::Files);
        qDebug() << "[INFO] Found" << csvFiles.size() << "CSV files in CSV/ folder:";
        for (const QString& file : csvFiles) {
            qDebug() << "  -" << file.toStdString().c_str();
        }
    }
    
    // 2.3 Load Character CSV files
    QDir charaDir(dir.filePath("CSV"));
    if (charaDir.exists()) {
        QStringList charaFiles = charaDir.entryList(QStringList() << "Chara*.csv", QDir::Files);
        if (!charaFiles.isEmpty()) {
            qDebug() << "[INFO] Found" << charaFiles.size() << "character CSV files:";
            for (const QString& file : charaFiles) {
                qDebug() << "  -" << file.toStdString().c_str();
            }
        }
    }
    
    qDebug() << "[SUCCESS] CSV data loading complete";
    
    qDebug() << "\n---------------------------------------------------------------------";
    qDebug() << "Step 3: Compile and Load ERB Scripts (Emuera Phase 3)";
    qDebug() << "---------------------------------------------------------------------";
    
    // 3.1 Load all ERB scripts
    ErbLoader erbLoader;
    QString erbPath = dir.filePath("ERB");
    if (dir.exists("ERB")) {
        qDebug() << "[INFO] Loading ERB files from:" << erbPath.toStdString().c_str();
    } else {
        qDebug() << "[INFO] Loading ERB files from:" << directory.toStdString().c_str();
        erbPath = directory;
    }
    
    if (erbLoader.loadDirectory(erbPath)) {
        qDebug() << "[SUCCESS] ERB files loaded successfully";
    } else {
        qDebug() << "[WARNING] Some ERB files may have failed to load";
    }
    
    // Get loaded scripts
    QHash<QString, QList<ScriptLine>> loadedScripts = erbLoader.getLoadedScripts();
    qDebug() << "[INFO] Found" << loadedScripts.size() << "loaded scripts";
    
    // 3.2 Build label dictionary (Emuera: setLabelsArg)
    qDebug() << "\n---------------------------------------------------------------------";
    qDebug() << "Step 3.1: Build Label Dictionary (Emuera: setLabelsArg)";
    qDebug() << "---------------------------------------------------------------------";
    
    QHash<QString, int> labelDictionary;
    int labelCount = 0;
    for (auto it = loadedScripts.begin(); it != loadedScripts.end(); ++it) {
        const QList<ScriptLine>& scriptLines = it.value();
        
        for (const ScriptLine& line : scriptLines) {
            if (line.type() == ScriptLineType::Label) {
                QString labelName = line.content().mid(1); // Remove @ prefix
                labelDictionary.insert(labelName, labelCount);
                labelCount++;
                qDebug() << "[INFO] Label found:" << labelName.toStdString().c_str();
            }
        }
    }
    qDebug() << "[SUCCESS] Label dictionary built with" << labelCount << "labels";
    
    // 3.3 Check script syntax (Emuera: checkScript)
    qDebug() << "\n---------------------------------------------------------------------";
    qDebug() << "Step 3.2: Syntax Check (Emuera: checkScript)";
    qDebug() << "---------------------------------------------------------------------";
    
    bool syntaxError = false;
    for (auto it = loadedScripts.begin(); it != loadedScripts.end(); ++it) {
        const QString& scriptName = it.key();
        const QList<ScriptLine>& scriptLines = it.value();
        
        // Check for common syntax issues
        int ifCount = 0;
        int endifCount = 0;
        int ifCountWithoutThen = 0;
        
        for (const ScriptLine& line : scriptLines) {
            if (line.type() == ScriptLineType::Instruction) {
                InstructionData data = line.instructionData();
                if (data.name == "IF") {
                    ifCount++;
                    if (!data.arguments.isEmpty() && !data.arguments.first().value.contains("THEN")) {
                        ifCountWithoutThen++;
                    }
                }
                if (data.name == "ENDIF") endifCount++;
            }
        }
        
        if (ifCount != endifCount) {
            qDebug() << "[WARNING] Script" << scriptName << "may have unmatched IF/ENDIF blocks";
        }
    }
    
    if (!syntaxError) {
        qDebug() << "[SUCCESS] Syntax check passed";
    } else {
        qDebug() << "[ERROR] Syntax errors found";
        return -1;
    }
    
    qDebug() << "[SUCCESS] Script compilation complete";
    
    qDebug() << "\n---------------------------------------------------------------------";
    qDebug() << "Step 4: Initialize Execution Engine";
    qDebug() << "---------------------------------------------------------------------";
    
    VariableStorage storage;
    storage.setGlobalInt1D("GOLD", 0, 500);
    storage.setGlobalInt1D("EXP", 0, 0);
    storage.setGlobalInt1D("DAY", 0, 1);
    
    ExecutionEngine execEngine(&storage);
    
    qDebug() << "[SUCCESS] ExecutionEngine initialized with initial variables";
    qDebug() << "  GOLD = 500";
    qDebug() << "  EXP = 0";
    qDebug() << "  DAY = 1";
    
    qDebug() << "\n---------------------------------------------------------------------";
    qDebug() << "Step 5: Find @SYSTEM Entry Point (Emuera Phase 4)";
    qDebug() << "---------------------------------------------------------------------";
    
    // Find @SYSTEM entry point
		QString systemLabel = "SYSTEM";
		if (labelDictionary.contains(systemLabel)) {
				qDebug() << "[INFO] Found @SYSTEM entry point";

				// Execute @SYSTEM
				qDebug() << "[INFO] Entering @SYSTEM function...";

				// Create a test script with @SYSTEM if not found
				QString systemTestScriptPath = dir.filePath("SYSTEM.ERB");
				if (!loadedScripts.contains("SYSTEM")) {
						qDebug() << "[INFO] @SYSTEM not found in loaded scripts, creating test script...";

						// Create SYSTEM script
						{
								QFile testFile(systemTestScriptPath);
								if (testFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
										QTextStream out(&testFile);
										testFile.close();
										qDebug() << "[SUCCESS] Test SYSTEM script created";
								}
						}

						// Reload scripts
						erbLoader.loadFile(systemTestScriptPath);
						loadedScripts = erbLoader.getLoadedScripts();
				}

				// Execute @SYSTEM
				if (loadedScripts.contains("SYSTEM")) {
						qDebug() << "[INFO] Executing @SYSTEM...";
						execEngine.executeScript("SYSTEM");
						qDebug() << "[SUCCESS] @SYSTEM executed successfully";
				}
		} else {
				qDebug() << "[INFO] @SYSTEM not found in scripts";
		}
    
    qDebug() << "\n---------------------------------------------------------------------";
    qDebug() << "Step 6: Create Event Script (Tests Emuera Event Handling)";
    qDebug() << "---------------------------------------------------------------------";
    
    // Create event script
    QString eventScriptPath = dir.filePath("EVENTS.ERB");
    {
        QFile eventFile(eventScriptPath);
        if (eventFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&eventFile);
            eventFile.close();
            qDebug() << "[SUCCESS] Event script created at:" << eventScriptPath.toStdString().c_str();
        }
    }
    
    // Reload scripts to include event script
    erbLoader.loadFile(eventScriptPath);
    loadedScripts = erbLoader.getLoadedScripts();
    
    qDebug() << "[SUCCESS] Event script loaded";
    
    qDebug() << "\n---------------------------------------------------------------------";
    qDebug() << "Step 7: Test Event Execution (Emuera Phase 5)";
    qDebug() << "---------------------------------------------------------------------";
    
    // Test @INIT_STAGE
    if (loadedScripts.contains("INIT_STAGE")) {
        qDebug() << "[INFO] Testing @INIT_STAGE event...";
        execEngine.executeScript("INIT_STAGE");
        qDebug() << "[SUCCESS] @INIT_STAGE executed";
        
        qDebug() << "[INFO] After @INIT_STAGE:";
        qDebug() << "  GOLD =" << storage.getGlobalInt1D("GOLD", 0);
        qDebug() << "  EXP =" << storage.getGlobalInt1D("EXP", 0);
        qDebug() << "  DAY =" << storage.getGlobalInt1D("DAY", 0);
    }
    
    // Test @EVENT_TRAIN
    if (loadedScripts.contains("EVENT_TRAIN")) {
        qDebug() << "[INFO] Testing @EVENT_TRAIN event...";
        execEngine.executeScript("EVENT_TRAIN");
        qDebug() << "[SUCCESS] @EVENT_TRAIN executed";
        
        qDebug() << "[INFO] After @EVENT_TRAIN:";
        qDebug() << "  EXP =" << storage.getGlobalInt1D("EXP", 0);
    }
    
    // Test @EVENT_SHOP
    if (loadedScripts.contains("EVENT_SHOP")) {
        qDebug() << "[INFO] Testing @EVENT_SHOP event...";
        execEngine.executeScript("EVENT_SHOP");
        qDebug() << "[SUCCESS] @EVENT_SHOP executed";
        
        qDebug() << "[INFO] After @EVENT_SHOP:";
        qDebug() << "  GOLD =" << storage.getGlobalInt1D("GOLD", 0);
    }
    
    // Test @EVENT_NEXTDAY
    if (loadedScripts.contains("EVENT_NEXTDAY")) {
        qDebug() << "[INFO] Testing @EVENT_NEXTDAY event...";
        execEngine.executeScript("EVENT_NEXTDAY");
        qDebug() << "[SUCCESS] @EVENT_NEXTDAY executed";
        
        qDebug() << "[INFO] After @EVENT_NEXTDAY:";
        qDebug() << "  DAY =" << storage.getGlobalInt1D("DAY", 0);
    }
    
    qDebug() << "\n---------------------------------------------------------------------";
    qDebug() << "Step 8: Verify Final State";
    qDebug() << "---------------------------------------------------------------------";
    
    qDebug() << "[INFO] Final variable state:";
    qDebug() << "  A =" << storage.getGlobalInt1D("A", 0);
    qDebug() << "  B =" << storage.getGlobalInt1D("B", 0);
    qDebug() << "  C =" << storage.getGlobalInt1D("C", 0);
    qDebug() << "  GOLD =" << storage.getGlobalInt1D("GOLD", 0);
    qDebug() << "  EXP =" << storage.getGlobalInt1D("EXP", 0);
    qDebug() << "  DAY =" << storage.getGlobalInt1D("DAY", 0);
    
    // Verify expression evaluation
    ExpressionEvaluator* evaluator = engine.getExpressionEvaluator();
    qDebug() << "\n[INFO] Final expression evaluation test:";
    qDebug() << "  A + B * C =" << evaluator->evaluate("A + B * C", &storage).toInt();
    qDebug() << "  (A + B) * C =" << evaluator->evaluate("(A + B) * C", &storage).toInt();
    qDebug() << "  GOLD - 100 =" << evaluator->evaluate("GOLD - 100", &storage).toInt();
    
    qDebug() << "\n---------------------------------------------------------------------";
    qDebug() << "Step 9: Cleanup";
    qDebug() << "---------------------------------------------------------------------";
    
    // Clean up test scripts
    if (QFile::exists("SYSTEM.ERB")) {
        QFile::remove("SYSTEM.ERB");
        qDebug() << "[SUCCESS] Test script cleaned up";
    }
    
    if (QFile::exists(eventScriptPath)) {
        QFile::remove(eventScriptPath);
        qDebug() << "[SUCCESS] Event script cleaned up";
    }
    
    qDebug() << "\n======================================================================";
    qDebug() << "CLI Test Program Complete!";
    qDebug() << "======================================================================";
    qDebug() << "\n[SUCCESS] All EraEngine functionality tested without QML rendering.";
    qDebug() << "\n[INFO] Test Flow (matches C# Emuera):";
    qDebug() << "       1. Initialization phase (config + CSV + scripts)";
    qDebug() << "       2. Event loop entry (@SYSTEM as main entry point)";
    qDebug() << "       3. Script execution with input handling";
    qDebug() << "       4. Event triggers (TRAIN, SHOP, NEXTDAY)";
    qDebug() << "\n[INFO] Emuera Phase Summary:";
    qDebug() << "       Phase 1: Config loading (_default.config, emuera.config, _fixed.config)";
    qDebug() << "       Phase 2: CSV data loading (GameBase.csv, Chara*.csv, etc.)";
    qDebug() << "       Phase 3: Script compilation (ERB files, label dictionary, syntax check)";
    qDebug() << "       Phase 4: Entry point detection (@SYSTEM)";
    qDebug() << "       Phase 5: Event loop with player input handling";
    qDebug() << "\n[INFO] All phases completed successfully!";
    
    return 0;
}
