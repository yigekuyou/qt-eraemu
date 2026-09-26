#ifndef LOGICAL_LINE_PARSER_H
#define LOGICAL_LINE_PARSER_H
#include <QDebug>
#include <QObject>
#include <QString>
#include <QList>
#include "script_line.h"
#include "function_label_line.h"

class EmueraConsole;

// Logical line parser - combines script lines into logical lines
class LogicalLineParser : public QObject {
		Q_OBJECT

public:
		explicit LogicalLineParser(QObject* parent = nullptr);

		// Parse logical lines from script lines
		QList<LogicalLine> parseLogicalLines(const QList<ScriptLine>& scriptLines);

		// Parse a single logical line
		LogicalLine parseLogicalLine(const QList<ScriptLine>& scriptLines, int startIndex) ;
		LogicalLine parseLine(const QString& content, const ScriptPosition& position);
		LogicalLine parseLabelLine(const QString& content, const ScriptPosition& position);


		// Check if a script line starts a new logical line
		bool isLogicalLineStart(const ScriptLine& line);
		bool parseSharpLine(FunctionLabelLine* label, QString& streamContent, const ScriptPosition& position, QStringList& onlyLabel);

		// Check if a script line ends a logical line
		bool isLogicalLineEnd(const ScriptLine& line);

		// Combine continuation lines
		QString combineContinuation(const QList<ScriptLine>& lines);

signals:
		void parserWarning(const QString& message, int lineNo, int level);
		void analysisMessagePrinted(const QString& message);
		void logicalLineParsed(const LogicalLine& line, int lineNumber);

private:
		// Helper to skip empty lines and comments
		int skipNonCodeLines(const QList<ScriptLine>& lines, int startIndex);

		// Extract instruction name from content
		QString extractInstructionName(const QString& content);
};

#endif // LOGICAL_LINE_PARSER_H
