#include "logical_line_parser.h"
#include <QRegularExpression>

LogicalLineParser::LogicalLineParser(QObject* parent) : QObject(parent) {}

QList<LogicalLine> LogicalLineParser::parseLogicalLines(const QList<ScriptLine>& scriptLines) {
		QList<LogicalLine> logicalLines;
		int i = 0;
		int lineNumber = 0;

		while (i < scriptLines.size()) {
				i = skipNonCodeLines(scriptLines, i);
				if (i >= scriptLines.size()) {
						break;
				}

				LogicalLine line = parseLogicalLine(scriptLines, i);
				logicalLines.append(line);
				
				// Emit signal when a logical line is parsed
				emit logicalLineParsed(line, lineNumber);
				lineNumber++;

				// Move past this logical line
				i += line.scriptLines().size();
		}

		return logicalLines;
}

int LogicalLineParser::skipNonCodeLines(const QList<ScriptLine>& lines, int startIndex) {
		int i = startIndex;
		while (i < lines.size()) {
				ScriptLineType type = lines[i].type();
				if (type != ScriptLineType::Empty && type != ScriptLineType::Comment) {
						break;
				}
				i++;
		}
		return i;
}

bool LogicalLineParser::isLogicalLineStart(const ScriptLine& line) {
		// Labels, instructions, and expressions start new logical lines
		ScriptLineType type = line.type();
		return type == ScriptLineType::Label ||
					 type == ScriptLineType::Instruction ||
					 type == ScriptLineType::Expression;
}

bool LogicalLineParser::isLogicalLineEnd(const ScriptLine& line) {
		// Lines ending with certain instructions are complete
		// In era scripts, most lines are complete on their own
		ScriptLineType type = line.type();
		return type == ScriptLineType::Instruction ||
					 type == ScriptLineType::Expression;
}

QString LogicalLineParser::combineContinuation(const QList<ScriptLine>& lines) {
		QString result;
		for (const ScriptLine& line : lines) {
				result += line.content();
		}
		return result;
}

QString LogicalLineParser::extractInstructionName(const QString& content) {
		// Extract first word (instruction name)
		QRegularExpression re("^\\s*(\\w+)");
		QRegularExpressionMatch match = re.match(content);
		if (match.hasMatch()) {
				return match.captured(1);
		}
		return "";
}
bool LogicalLineParser::parseSharpLine(FunctionLabelLine* label, QString& streamContent, const ScriptPosition& position, QStringList& onlyLabel) {
		if (!label) return false;

		// 移除开头的 '#'
		if (streamContent.startsWith('#')) {
				streamContent.remove(0, 1);
		}

		// Qt 6 现代正则匹配首个标识符
		QRegularExpression re("^([A-Za-z_]\\w*)");
		QRegularExpressionMatch match = re.match(streamContent.trimmed());
		if (!match.hasMatch()) {
				emit parserWarning("解釈できない#行です", position.lineNumber, 1);
				return false;
		}

		QString token = match.captured(1).toUpper();
		streamContent.remove(0, match.capturedLength(1));

		static const QStringList validTokens = {
				"SINGLE", "LATER", "PRI", "ONLY", "FUNCTION", "FUNCTIONS",
				"LOCALSIZE", "LOCALSSIZE", "DIM", "DIMS"
		};

		// Qt 6 switch-case 逻辑分支对齐 C#
		if (token == "SINGLE") {
				if (label->isMethod()) {
						emit parserWarning("式中関数では#SINGLEは機能しません", position.lineNumber, 1);
				} else if (!label->isEvent()) {
						emit parserWarning("イベント関数以外では#SINGLEは機能しません", position.lineNumber, 1);
				} else if (label->isSingle()) {
						emit parserWarning("#SINGLEが重複して使われています", position.lineNumber, 1);
				} else {
						label->setIsSingle(true);
				}
		}
		else if (token == "FUNCTION" || token == "FUNCTIONS") {
				if (label->isMethod()) {
						emit parserWarning(QString("関数%1にはすでに#%2が宣言されています").arg(label->labelName(), token), position.lineNumber, 1);
						return false;
				}
				label->setIsMethod(true);
				label->setDepth(0);
				label->methodType((token == "FUNCTIONS") ? "string" : "int");
		}
		else if (token == "LOCALSIZE" || token == "LOCALSSIZE") {
				if (label->isEvent()) {
						emit parserWarning(QString("イベント関数では#%1によるサイズ指定は無視されます").arg(token), position.lineNumber, 1);
				} else {
						bool ok = false;
						int size = streamContent.trimmed().toInt(&ok);
						if (!ok || size <= 0) {
								emit parserWarning(QString("#%1の後に有効な数値が指定されていません").arg(token), position.lineNumber, 2);
						} else {
								if (token == "LOCALSIZE") {
										label->setLocalLength(size);
								} else {
										label->setLocalsLength(size);
								}
						}
				}
		}
		else if (token == "LATER") {
				if (label->isMethod()) {
						emit parserWarning("式中関数では#LATERは機能しません", position.lineNumber, 1);
				} else if (!label->isEvent()) {
						emit parserWarning("イベント関数以外では#LATERは機能しません", position.lineNumber, 1);
				} else if (label->isLater()) {
						emit parserWarning("#LATERが重複して使われています", position.lineNumber, 1);
				} else if (label->isOnly()) {
						emit parserWarning("#ONLYが指定されたイベント関数では#LATERは機能しません", position.lineNumber, 1);
				} else {
						if (label->isPri())
								emit parserWarning("#PRIと#LATERが重複して使われています(この関数は2度呼ばれます)", position.lineNumber, 1);
						label->setIsLater(true);
				}
		}
		else if (token == "PRI") {
				if (label->isMethod()) {
						emit parserWarning("式中関数では#PRIは機能しません", position.lineNumber, 1);
				} else if (!label->isEvent()) {
						emit parserWarning("イベント関数以外では#PRIは機能しません", position.lineNumber, 1);
				} else if (label->isPri()) {
						emit parserWarning("#PRIが重複して使われています", position.lineNumber, 1);
				} else if (label->isOnly()) {
						emit parserWarning("#ONLYが指定されたイベント関数では#PRIは機能しません", position.lineNumber, 1);
				} else {
						if (label->isLater())
								emit parserWarning("#PRIと#LATERが重複して使われています(この関数は2度呼ばれます)", position.lineNumber, 1);
						label->setIsPri(true);
				}
		}
		else if (token == "ONLY") {
				if (label->isMethod()) {
						emit parserWarning("式中関数では#ONLYは機能しません", position.lineNumber, 1);
				} else if (!label->isEvent()) {
						emit parserWarning("イベント関数以外では#ONLYは機能しません", position.lineNumber, 1);
				} else if (label->isOnly()) {
						emit parserWarning("#ONLYが重複して使われています", position.lineNumber, 1);
				} else {
						if (onlyLabel.contains(label->labelName())) {
								emit parserWarning(QString("このイベント関数\"@%1\"にはすでに#ONLYが宣言されています（この関数は実行されません）").arg(label->labelName()), position.lineNumber, 1);
						}
						onlyLabel.append(label->labelName());
						label->setIsOnly(true);
						if (label->isPri()) {
								emit parserWarning("このイベント関数には#PRIが宣言されていますが無視されます", position.lineNumber, 1);
								label->setIsPri(false);
						}
						if (label->isLater()) {
								emit parserWarning("このイベント関数には#LATERが宣言されていますが無視されます", position.lineNumber, 1);
								label->setIsLater(false);
						}
						if (label->isSingle()) {
								emit parserWarning("このイベント関数には#SINGLEが宣言されていますが無視されます", position.lineNumber, 1);
								label->setIsSingle(false);
						}
				}
		}
		else if (token == "DIM" || token == "DIMS") {
						bool isString = (token == "DIMS");
						try {
								// 解析变量配置（修饰符、变量名、维数、初始值）
								UserDefinedVariableData varData = UserDefinedVariableData::create(
										streamContent,
										isString,
										true, // #DIM 为私有局部变量
										position
								);

								// 注册私有变量至函数节点
								if (!label->addPrivateVariable(varData)) {
										emit parserWarning(QString("変数名%1は既に使用されています").arg(varData.name), position.lineNumber, 2);
										return false;
								}
						}
						catch (const std::exception& e) {
								emit parserWarning(e.what(), position.lineNumber, 2);
								return false;
						}
				}

		return true;
}
LogicalLine LogicalLineParser::parseLogicalLine(const QList<ScriptLine>& scriptLines, int startIndex) {
		const ScriptLine& firstLine = scriptLines[startIndex];
		QString trimmedContent = firstLine.content().trimmed();

		if (trimmedContent.startsWith('@') || trimmedContent.startsWith('$')) {
				return parseLabelLine(trimmedContent, firstLine.position());
		}

		LogicalLine line(firstLine.position());
		int i = startIndex;
		while (i < scriptLines.size()) {
				const ScriptLine& current = scriptLines[i];
				if (current.type() == ScriptLineType::Empty) {
						break;
				}
				line.addScriptLine(current);
				if (isLogicalLineEnd(current)) {
						line.setComplete(true);
						break;
				}
				i++;
		}

		return line;
}

LogicalLine LogicalLineParser::parseLabelLine(const QString& content, const ScriptPosition& position) {
		bool isFunction = content.startsWith('@');
		QString labelName = content.mid(1).trimmed();

		if (labelName.isEmpty()) {
				emit parserWarning("関数名が不正であるか存在しません", position.lineNumber, 2);
				LogicalLine errLine(position);
				errLine.setComplete(true);
				return errLine;
		}

		if (!isFunction) {
				ScriptLine subLine = ScriptLine::createLabel(labelName, position);
				LogicalLine gotoLine(position);
				gotoLine.addScriptLine(subLine);
				gotoLine.setComplete(true);
				return gotoLine;
		}

		// For function labels (@LABEL), we need to create a logical line
		// with a label script line so the parser can properly track position
		ScriptLine labelLine = ScriptLine::createLabel(labelName, position);
		LogicalLine line(position);
		line.addScriptLine(labelLine);
		line.setComplete(true);
		return line;
}

LogicalLine LogicalLineParser::parseLine(const QString& content, const ScriptPosition& position) {
		QString trimmed = content.trimmed();
		LogicalLine line(position);

		if (trimmed.isEmpty() || trimmed.startsWith(';')) {
				return line;
		}

		QString instName = extractInstructionName(trimmed);
		InstructionData data;
		data.name = instName;
		data.position = position;

		ScriptLine subLine = ScriptLine::createInstruction(data);
		line.addScriptLine(subLine);
		line.setComplete(true);
		return line;
}
