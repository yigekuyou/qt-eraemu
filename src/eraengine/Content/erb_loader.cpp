#include "erb_loader.h"
#include <QDir>
#include <QRegularExpression>
#include <QTextStream>
#include "file_system_io.h"

ErbLoader::ErbLoader(QObject* parent) : QObject(parent) {}

bool ErbLoader::loadFile(const QString& filePath) {
    QString content = readFileContent(filePath);
    if (content.isEmpty()) {
        return false;
    }
    
    // Extract script name from file path
    QFileInfo fileInfo(filePath);
    QString scriptName = fileInfo.baseName();
    
    QList<ScriptLine> lines = parseScript(content, filePath);
    m_scripts.insert(scriptName, lines);
    m_scriptPaths.insert(scriptName, filePath);
    
    // Build label lookup
    for (ScriptLine& line : lines) {
        if (line.type() == ScriptLineType::Label) {
            QString labelName = extractLabelName(line.content());
            m_labels.insert(labelName, &line);
        }
    }
    
    return true;
}

bool ErbLoader::loadDirectory(const QString& dirPath) {
    QDir dir(dirPath);
    if (!dir.exists()) {
        return false;
    }
    
    // Find all .ERB files (case-insensitive) recursively
    QDir::Filters fileFilter = QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot;
    QStringList nameFilters;
    nameFilters << "*.ERB" << "*.erb" << "*.ERH" << "*.erh";
    
    QFileInfoList allFiles = dir.entryInfoList(nameFilters, fileFilter);
    
    bool success = true;
    for (const QFileInfo& fileInfo : allFiles) {
        QString fileName = fileInfo.fileName();
        // Case-insensitive check for .erb and .erh extensions
        if (fileName.endsWith(".erb", Qt::CaseInsensitive) || fileName.endsWith(".erh", Qt::CaseInsensitive)) {
            QString filePath = fileInfo.absoluteFilePath();
            if (!loadFile(filePath)) {
                success = false;
            }
        }
    }
    
    // Also try subdirectories (like ERB/, CSV/)
    QDir::Filters dirFilter = QDir::Dirs | QDir::NoDotAndDotDot;
    QStringList dirNames = dir.entryList(dirFilter);
    for (const QString& subDirName : dirNames) {
        QString subDirPath = dirPath + "/" + subDirName;
        loadDirectory(subDirPath);
    }
    
    return success;
}

QHash<QString, QList<ScriptLine>> ErbLoader::getLoadedScripts() const {
    return m_scripts;
}

ScriptLine* ErbLoader::findLabel(const QString& labelName) {
    return m_labels.value(labelName, nullptr);
}

QString ErbLoader::getScriptPath(const QString& scriptName) const {
    return m_scriptPaths.value(scriptName, "");
}

QString ErbLoader::readFileContent(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return "";
    }
    
    // Qt 6 uses UTF-8 by default for QTextStream
    QTextStream in(&file);
    QString content = in.readAll();
    
    file.close();
    return content;
}

QList<ScriptLine> ErbLoader::parseScript(const QString& content, const QString& filePath) {
    QList<ScriptLine> lines;
    QStringList linesList = content.split('\n');
    
    for (int i = 0; i < linesList.size(); ++i) {
        QString line = linesList[i];
        ScriptPosition pos(filePath, i + 1, 1);
        
        ScriptLine scriptLine = parseLine(line, i + 1, filePath);
        lines.append(scriptLine);
    }
    
    return lines;
}

ScriptLine ErbLoader::parseLine(const QString& line, int lineNumber, const QString& filePath) {
    ScriptPosition pos(filePath, lineNumber, 1);
    QString trimmed = line.trimmed();
    
    // Empty line
    if (trimmed.isEmpty()) {
        return ScriptLine::createEmpty(pos);
    }
    
    // Comment line (starts with ;)
    if (trimmed.startsWith(';')) {
        return ScriptLine::createComment(trimmed, pos);
    }
    
    // Preprocessor directive (starts with #)
    if (trimmed.startsWith('#')) {
        ScriptLine line;
        line.setType(ScriptLineType::Preprocessor);
        line.setContent(trimmed);
        line.setPosition(pos);
        return line;
    }
    
    // Label definition (starts with @ or $)
    if (trimmed.startsWith('@') || trimmed.startsWith('$')) {
        QString labelName = extractLabelName(trimmed);
        return ScriptLine::createLabel(labelName, pos);
    }
    
    // Instruction line
    InstructionData data = extractInstruction(trimmed);
    data.position = pos;
    return ScriptLine::createInstruction(data);
}

QString ErbLoader::extractLabelName(const QString& line) {
    QString label = line;
    
    // Remove @ or $ prefix
    if (label.startsWith('@') || label.startsWith('$')) {
        label = label.mid(1);
    }
    
    // Handle function labels with parentheses: @LABEL(args) -> LABEL
    int parenPos = label.indexOf('(');
    if (parenPos > 0) {
        label = label.left(parenPos).trimmed();
    }
    
    // Handle labels with colons or other syntax
    int colonPos = label.indexOf(':');
    if (colonPos > 0) {
        label = label.left(colonPos).trimmed();
    }
    
    return label.trimmed();
}

InstructionData ErbLoader::extractInstruction(const QString& line) {
    InstructionData data;
    
    // Check for assignment operations first
    // Simple assignment: VARIABLE = value
    // Compound assignment: VARIABLE += value, VARIABLE -= value, etc.
    // Use Unicode-aware pattern for variable names (letters, digits, underscores)
    QRegularExpression simpleAssignmentRegex(R"(^([^\s]+)\s*=\s*(.+)$)");
    QRegularExpression compoundAssignmentRegex(R"(^([^\s]+)\s*(\+\=|\-\=|\*\=|\/\=)\s*(.+)$)");
    
    QRegularExpressionMatch simpleMatch = simpleAssignmentRegex.match(line.trimmed());
    QRegularExpressionMatch compoundMatch = compoundAssignmentRegex.match(line.trimmed());
    
    if (simpleMatch.hasMatch()) {
        QString variableName = simpleMatch.captured(1);
        QString value = simpleMatch.captured(2);
        
        // For assignment, the "instruction" is the operator
        data.name = "=";
        
        // First argument is the variable name
        data.arguments.append(InstructionArgument(variableName));
        // Second argument is the value
        data.arguments.append(InstructionArgument(value));
        
        return data;
    }
    
    if (compoundMatch.hasMatch()) {
        QString variableName = compoundMatch.captured(1);
        QString op = compoundMatch.captured(2);
        QString value = compoundMatch.captured(3);
        
        // For compound assignment, the "instruction" is the operator
        data.name = op;  // +=, -=, *=, /=
        
        // First argument is the variable name
        data.arguments.append(InstructionArgument(variableName));
        // Second argument is the value
        data.arguments.append(InstructionArgument(value));
        
        return data;
    }
    
    // Special handling for CALL instruction with parentheses
    // CALL INIT_STAGE() or CALL CHECK_PLAYER_COLLISION(arg1, arg2)
    QRegularExpression callRegex(R"(^CALL\s+(\w+)(?:\s*\(([^)]*)\))?\s*$)");
    QRegularExpressionMatch callMatch = callRegex.match(line.trimmed());
    if (callMatch.hasMatch()) {
        data.name = "CALL";
        QString funcName = callMatch.captured(1);  // Function name without parentheses
        QString args = callMatch.captured(2);  // Arguments (may be empty)
        
        if (!args.isEmpty()) {
            // Parse arguments by splitting on comma
            QStringList argList = args.split(',', Qt::SkipEmptyParts);
            for (const QString& arg : argList) {
                data.arguments.append(InstructionArgument(arg.trimmed()));
            }
        }
        data.arguments.append(InstructionArgument(funcName));
        return data;
    }
    
    // Split line into tokens
    QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    
    if (tokens.isEmpty()) {
        return data;
    }
    
    // First token is the instruction name
    data.name = tokens[0];
    
    // Remaining tokens are arguments
    for (int i = 1; i < tokens.size(); ++i) {
        QString token = tokens[i];
        InstructionArgument arg(token);
        
        // Check if it's a string literal (starts with ")
        if (token.startsWith('"') && token.endsWith('"')) {
            arg.isString = true;
            arg.value = token.mid(1, token.length() - 2);
        }
        // Check if it's a variable reference (starts with $ or %)
        else if (token.startsWith('$') || token.startsWith('%')) {
            arg.isVariable = true;
            arg.value = token.mid(1);  // Remove prefix
        }
        
        data.arguments.append(arg);
    }
    
    return data;
}
