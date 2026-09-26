#include "erb_loader.h"
#include <QDir>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>
#include <iostream>
#include "file_system_io.h"

ErbLoader::ErbLoader(QObject* parent) : QObject(parent), m_parseTable(nullptr) {}

bool ErbLoader::loadFile(const QString& filePath) {
    QString content = readFileContent(filePath);
    if (content.isEmpty()) {
        return false;
    }
    
    // Extract script name from file path
    QFileInfo fileInfo(filePath);
    QString scriptName = fileInfo.baseName();
    
    // Emit parse started signal
    emit parseStarted(scriptName);
    
    QList<ScriptLine> lines = parseScript(content, filePath);
    
    // Store raw script lines
    m_scripts.insert(scriptName, lines);
    m_scriptPaths.insert(scriptName, filePath);
    
    // Build label lookup and position mapping
    for (int i = 0; i < lines.size(); ++i) {
        ScriptLine& line = lines[i];
        if (line.type() == ScriptLineType::Label) {
            QString labelName = extractLabelName(line.content());
            m_labels.insert(labelName, &line);
            m_labelPositions.insert(scriptName + ":" + labelName, i);
        }
    }
    m_scriptLineCounts.insert(scriptName, lines.size());
    
    // Parse logical lines and cache them
    LogicalLineParser parser;
    QList<LogicalLine> logicalLines = parser.parseLogicalLines(lines);
    m_logicalLines.insert(scriptName, logicalLines);
    
    // Notify parse table about parsed data
    if (m_parseTable) {
        m_parseTable->loadScript(scriptName, logicalLines);
    }
    
    // Emit parse finished signal
    emit parseFinished(scriptName);
    
    return true;
}

bool ErbLoader::loadDirectory(const QString& dirPath, int depth) {
    if (depth > 5) {
        return true;
    }
    
    QDir dir(dirPath);
    if (!dir.exists()) {
        return false;
    }
    
    // Find all .ERB files
    QDir::Filters fileFilter = QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot;
    QStringList nameFilters;
    nameFilters << "*.ERB" << "*.erb" << "*.ERH" << "*.erh";
    
    QFileInfoList allFiles = dir.entryInfoList(nameFilters, fileFilter);
    
    bool success = true;
    for (const QFileInfo& fileInfo : allFiles) {
        QString fileName = fileInfo.fileName();
        if (fileName.endsWith(".erb", Qt::CaseInsensitive) || fileName.endsWith(".erh", Qt::CaseInsensitive)) {
            QString filePath = fileInfo.absoluteFilePath();
            if (!loadFile(filePath)) {
                success = false;
            }
        }
    }
    
    // Also try subdirectories
    QDir::Filters dirFilter = QDir::Dirs | QDir::NoDotAndDotDot;
    QStringList dirNames = dir.entryList(dirFilter);
    for (const QString& subDirName : dirNames) {
        QString subDirPath = dirPath + "/" + subDirName;
        loadDirectory(subDirPath, depth + 1);
    }
    
    return success;
}

QHash<QString, QList<ScriptLine>> ErbLoader::getLoadedScripts() const {
    return m_scripts;
}

ScriptLine* ErbLoader::findLabel(const QString& labelName) {
    return m_labels.value(labelName, nullptr);
}

bool ErbLoader::scriptExists(const QString& scriptName) const {
    return m_scripts.contains(scriptName);
}

QString ErbLoader::resolveScriptName(const QString& scriptName) const {
    // Try exact match first
    if (m_scripts.contains(scriptName)) {
        return scriptName;
    }
    
    // Try case-insensitive match
    QString lowerName = scriptName.toLower();
    for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) {
        if (it.key().toLower() == lowerName) {
            return it.key();
        }
    }
    
    return "";  // Not found
}

QHash<QString, QList<ScriptLine>> ErbLoader::getLoadedScriptsCI() const {
    QHash<QString, QList<ScriptLine>> result;
    for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) {
        result.insert(it.key().toLower(), it.value());
    }
    return result;
}

QList<LogicalLine> ErbLoader::getLogicalLines(const QString& scriptName) {
    return m_logicalLines.value(scriptName, QList<LogicalLine>());
}

QList<LogicalLine> ErbLoader::getLogicalLinesCI(const QString& scriptName) {
    // Try exact match first
    if (m_logicalLines.contains(scriptName)) {
        return m_logicalLines.value(scriptName, QList<LogicalLine>());
    }
    
    // Try case-insensitive match
    QString lowerName = scriptName.toLower();
    for (auto it = m_logicalLines.constBegin(); it != m_logicalLines.constEnd(); ++it) {
        if (it.key().toLower() == lowerName) {
            return it.value();
        }
    }
    
    return QList<LogicalLine>();
}

int ErbLoader::getLabelPosition(const QString& scriptName, const QString& labelName) {
    return m_labelPositions.value(scriptName + ":" + labelName, -1);
}

int ErbLoader::getScriptLineCount(const QString& scriptName) {
    return m_scriptLineCounts.value(scriptName, 0);
}

QString ErbLoader::getScriptPath(const QString& scriptName) const {
    return m_scriptPaths.value(scriptName, "");
}

QString ErbLoader::readFileContent(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return "";
    }
    
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
        
        // Emit parse line ready signal for each parsed line
        emit parseLineReady(QFileInfo(filePath).baseName(), i + 1, line);
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
        ScriptLine scriptLine;
        scriptLine.setType(ScriptLineType::Preprocessor);
        scriptLine.setContent(trimmed);
        scriptLine.setPosition(pos);
        return scriptLine;
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
    QRegularExpression simpleAssignmentRegex(R"(^([^\s]+)\s*=\s*(.+)$)");
    QRegularExpression compoundAssignmentRegex(R"(^([^\s]+)\s*(\+\=|\-\=|\*\=|\/\=)\s*(.+)$)");
    
    QRegularExpressionMatch simpleMatch = simpleAssignmentRegex.match(line.trimmed());
    QRegularExpressionMatch compoundMatch = compoundAssignmentRegex.match(line.trimmed());
    
    if (simpleMatch.hasMatch()) {
        QString variableName = simpleMatch.captured(1);
        QString value = simpleMatch.captured(2);
        
        data.name = "=";
        data.arguments.append(InstructionArgument(variableName));
        data.arguments.append(InstructionArgument(value));
        
        return data;
    }
    
    if (compoundMatch.hasMatch()) {
        QString variableName = compoundMatch.captured(1);
        QString op = compoundMatch.captured(2);
        QString value = compoundMatch.captured(3);
        
        data.name = op;
        data.arguments.append(InstructionArgument(variableName));
        data.arguments.append(InstructionArgument(value));
        
        return data;
    }
    
    // Special handling for CALL instruction with parentheses
    // Match CALL <function_name>[(<args>)]
    // Function name can contain any characters except spaces and parentheses
    QRegularExpression callRegex(R"(^CALL\s+([^\s\(]+)(?:\s*\(([^)]*)\))?\s*$)");
    QRegularExpressionMatch callMatch = callRegex.match(line.trimmed());
    if (callMatch.hasMatch()) {
        data.name = "CALL";
        QString funcName = callMatch.captured(1);
        QString args = callMatch.captured(2);
        
        // Remove @ prefix from function name for label lookup
        if (funcName.startsWith('@')) {
            funcName = funcName.mid(1);
        }
        
        // Store function name as first argument (label to call)
        data.arguments.append(InstructionArgument(funcName));
        
        if (!args.isEmpty()) {
            QStringList argList = args.split(',', Qt::SkipEmptyParts);
            for (const QString& arg : argList) {
                data.arguments.append(InstructionArgument(arg.trimmed()));
            }
        }
        return data;
    }
    
    // Split line into tokens
    QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    
    if (tokens.isEmpty()) {
        return data;
    }
    
    data.name = tokens[0];
    
    for (int i = 1; i < tokens.size(); ++i) {
        QString token = tokens[i];
        InstructionArgument arg(token);
        
        if (token.startsWith('"') && token.endsWith('"')) {
            arg.isString = true;
            arg.value = token.mid(1, token.length() - 2);
        } else if (token.startsWith('$') || token.startsWith('%')) {
            arg.isVariable = true;
            arg.value = token.mid(1);
        }
        
        data.arguments.append(arg);
    }
    
    return data;
}
