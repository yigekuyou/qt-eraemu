#ifndef ERB_LOADER_H
#define ERB_LOADER_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QList>
#include <QFile>
#include <QTextStream>
#include "GameProc/script_line.h"
#include "GameProc/era_parse_table.h"
#include "GameProc/logical_line_parser.h"

// ERB script loader
class ErbLoader : public QObject {
    Q_OBJECT

public:
    explicit ErbLoader(QObject* parent = nullptr);
    
    // Load ERB files
    bool loadFile(const QString& filePath);
    bool loadDirectory(const QString& dirPath, int depth = 0);
    
    // Set parse table for notifications
    void setParseTable(EraParseTable* parseTable) { m_parseTable = parseTable; }
    
    // Access loaded scripts
    QHash<QString, QList<ScriptLine>> getLoadedScripts() const;
    ScriptLine* findLabel(const QString& labelName);
    
    // Check if script exists
    bool scriptExists(const QString& scriptName) const;
    
    // Resolve script name (case-insensitive) to stored key, returns "" if not found
    QString resolveScriptName(const QString& scriptName) const;
    
    // Get loaded scripts (case-insensitive lookup)
    QHash<QString, QList<ScriptLine>> getLoadedScriptsCI() const;
    
    // Get logical lines for a script (cached after first parse)
    QList<LogicalLine> getLogicalLines(const QString& scriptName);
    
    // Case-insensitive lookup for script name
    QList<LogicalLine> getLogicalLinesCI(const QString& scriptName);
    
    // Get label position in script (returns -1 if not found)
    int getLabelPosition(const QString& scriptName, const QString& labelName);
    
    // Get script line count
    int getScriptLineCount(const QString& scriptName);
    
    // Get file path for a script
    QString getScriptPath(const QString& scriptName) const;
    
signals:
    // Parsing stage signals
    void parseStarted(const QString& scriptName);
    void parseLineReady(const QString& scriptName, int lineNumber, const QString& lineContent);
    void parseFinished(const QString& scriptName);
    void parseError(const QString& errorMessage);
    
private:
    // Parse script content into script lines
    QList<ScriptLine> parseScript(const QString& content, const QString& filePath);
    
    // Parse individual lines
    ScriptLine parseLine(const QString& line, int lineNumber, const QString& filePath);
    
    // Extract label name from line
    QString extractLabelName(const QString& line);
    
    // Extract instruction from line
    InstructionData extractInstruction(const QString& line);
    
    // Helper to read file content
    QString readFileContent(const QString& filePath);
    
    // Map script names to file paths
    QHash<QString, QString> m_scriptPaths;
    
    // Loaded scripts
    QHash<QString, QList<ScriptLine>> m_scripts;
    
    // Parsed logical lines (cached after first parse)
    QHash<QString, QList<LogicalLine>> m_logicalLines;
    
    // Label lookup (label name -> script line)
    QHash<QString, ScriptLine*> m_labels;
    
    // Label position mapping
    QHash<QString, int> m_labelPositions;
    
    // Script name -> line count mapping
    QHash<QString, int> m_scriptLineCounts;
    
    // Parse table pointer
    EraParseTable* m_parseTable;
};

#endif // ERB_LOADER_H
