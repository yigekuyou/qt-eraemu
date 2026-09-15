#ifndef ERB_LOADER_H
#define ERB_LOADER_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QList>
#include <QFile>
#include <QTextStream>
#include "GameProc/script_line.h"

// ERB script loader
class ErbLoader : public QObject {
    Q_OBJECT

public:
    explicit ErbLoader(QObject* parent = nullptr);
    
    // Load ERB files
    bool loadFile(const QString& filePath);
    bool loadDirectory(const QString& dirPath);
    
    // Access loaded scripts
    QHash<QString, QList<ScriptLine>> getLoadedScripts() const;
    ScriptLine* findLabel(const QString& labelName);
    
    // Get file path for a script
    QString getScriptPath(const QString& scriptName) const;
    
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
    
    // Label lookup (label name -> script line)
    QHash<QString, ScriptLine*> m_labels;
};

#endif // ERB_LOADER_H
