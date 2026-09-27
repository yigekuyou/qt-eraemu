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
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QStringList>
#include "ast/expression_lexer.h"
#include "ast/expression_parser.h"
#include "ast/expression_ast.h"
#include "ast/expression_evaluator.h"
#include "ast/logical_line.h"
#include "ast/ast_builder.h"
#include "variable_storage.h"
#include "execution_engine.h"
#include "erb_loader.h"

void printLogicalLine(const LogicalLine& line) {
    switch (line.kind) {
    case LineKind::FunctionLabel:
        qDebug() << "  FunctionLabel:" << line.labelName;
        break;
    case LineKind::GotoLabel:
        qDebug() << "  GotoLabel:" << line.labelName;
        break;
    case LineKind::Instruction: {
        qDebug() << "  Instruction:" << line.functionName;
        qDebug() << "  Arguments:" << line.arguments.size();
        for (int j = 0; j < line.arguments.size(); j++) {
            const Operand& arg = line.arguments.at(j);
            qDebug() << QString("    Arg%1: '%2' (str=%3,var=%4,ast=%5)")
                            .arg(j).arg(arg.raw)
                            .arg(arg.isString ? "yes" : "no")
                            .arg(arg.isVariable ? "yes" : "no")
                            .arg(arg.ast ? "yes" : "no");
        }
        break;
    }
    case LineKind::Preprocessor:
        qDebug() << "  Preprocessor:" << line.raw.trimmed();
        break;
    default:
        break;
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    qDebug() << "Expression Parsing System - eraTW Line-by-Line Analysis";
    qDebug() << "======================================================";
    
    // Initialize components
    ErbLoader erbLoader;
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
    auto loadedScripts = engine.getErbLoader().getLoadedScripts();
    qDebug() << "Loaded scripts count:" << loadedScripts.size();
    qDebug() << "Loaded scripts:" << loadedScripts.keys().join(", ");
    
    // Get the TITLE script
    QString scriptName = "TITLE";
    if (!loadedScripts.contains(scriptName)) {
        qDebug() << "Script not found:" << scriptName;
        return 1;
    }
    
    QList<LogicalLine> logicalLines = loadedScripts.value(scriptName);

    qDebug() << "\nFound" << logicalLines.size() << "logical lines in" << scriptName;

    // Print each logical line
    for (int i = 0; i < logicalLines.size() && i < 20; i++) {  // Limit to first 20
        qDebug() << "--- Logical Line" << i + 1 << "---";
        printLogicalLine(logicalLines.at(i));
        qDebug() << "";
    }
    
    // Execute using ExecutionEngine
    qDebug() << "======================================================";
    qDebug() << "Executing script...\n";
    
    engine.executeScript(scriptName);
    
    qDebug() << "\nExecution complete!";
    
    return 0;
}
