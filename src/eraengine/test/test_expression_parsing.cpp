#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QStringList>
#include "expression_token.h"
#include "expression_lexer.h"
#include "expression_parser.h"
#include "expression_ast.h"
#include "expression_evaluator.h"
#include "variable_storage.h"
#include "script_line.h"
#include "logical_line_parser.h"
#include "execution_engine.h"
#include "erb_loader.h"

void printScriptLine(const ScriptLine& line) {
    qDebug() << "  Type:" << static_cast<int>(line.type());
    
    QString content = line.content();
    if (content.isEmpty()) {
        return;
    }
    
    if (line.type() == ScriptLineType::Label) {
        // Label lines start with @
        if (content.startsWith('@')) {
            QString labelName = content.mid(1);
            qDebug() << "  Label:" << labelName;
        } else {
            qDebug() << "  Label:" << content;
        }
    }
    else if (line.type() == ScriptLineType::Comment) {
        qDebug() << "  Comment:" << content;
    }
    else if (line.type() == ScriptLineType::Instruction) {
        InstructionData data = line.instructionData();
        qDebug() << "  Instruction:" << data.name;
        qDebug() << "  Arguments:" << data.arguments.size();
        
        for (int j = 0; j < data.arguments.size(); j++) {
            const InstructionArgument& arg = data.arguments[j];
            QString argStr = QString("    Arg%1: '%2' (str=%3,var=%4)")
                .arg(j)
                .arg(arg.value)
                .arg(arg.isString ? "yes" : "no")
                .arg(arg.isVariable ? "yes" : "no");
            qDebug() << argStr;
        }
    }
    else if (line.type() == ScriptLineType::Expression) {
        qDebug() << "  Expression:" << content;
    }
    else {
        qDebug() << "  Content:" << content;
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    qDebug() << "Expression Parsing System - eraTW Line-by-Line Analysis";
    qDebug() << "======================================================";
    
    // Initialize components
    ErbLoader erbLoader;
    LogicalLineParser logicalParser;
    VariableStorage storage;
    ExecutionEngine engine(&storage);
    
    // Load the script content from eraTW directory
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
		QString scriptDir = directory +"/ERB";
    
    qDebug() << "\nLoading scripts from:" << scriptDir;
    
    bool loaded = engine.loadScripts(scriptDir);
    qDebug() << "Load result:" << loaded;
    
    // Get the loaded scripts directly from ErbLoader
    auto loadedScripts = erbLoader.getLoadedScripts();
    qDebug() << "Loaded scripts count:" << loadedScripts.size();
    qDebug() << "Loaded scripts:" << loadedScripts.keys().join(", ");
    
    // Get the TITLE script
    QString scriptName = "TITLE";
    if (!loadedScripts.contains(scriptName)) {
        qDebug() << "Script not found:" << scriptName;
        return 1;
    }
    
    QList<ScriptLine> scriptLines = loadedScripts.value(scriptName);
    
    qDebug() << "\nFound" << scriptLines.size() << "script lines in" << scriptName;
    
    // Parse into logical lines
    QList<LogicalLine> logicalLines = logicalParser.parseLogicalLines(scriptLines);
    
    qDebug() << "Parsed into" << logicalLines.size() << "logical lines\n";
    
    // Print each logical line
    for (int i = 0; i < logicalLines.size() && i < 20; i++) {  // Limit to first 20
        const LogicalLine& line = logicalLines[i];
        const QList<ScriptLine>& lines = line.scriptLines();
        
        qDebug() << "--- Logical Line" << i + 1 << "---";
        
        for (const ScriptLine& scriptLine : lines) {
            printScriptLine(scriptLine);
        }
        
        qDebug() << "";
    }
    
    // Execute using ExecutionEngine
    qDebug() << "======================================================";
    qDebug() << "Executing script...\n";
    
    engine.executeScript(scriptName);
    
    qDebug() << "\nExecution complete!";
    
    return 0;
}
