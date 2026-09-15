#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStringList>
#include "erb_loader.h"

inline void printStep(int num, const QString& desc) {
		qDebug().nospace() << "\n" << num << ". " << desc;
}

int main(int argc, char *argv[]) {
		QCoreApplication app(argc, argv);

		qDebug() << "ERB Loader Test\n===============";

		// 获取命令行传入的外部目录，默认当前目录
		QString testDir = argc > 1 ? argv[1] : ".";
		printStep(1, QString("Testing directory: %1").arg(testDir));

		printStep(2, "Creating ErbLoader");
		ErbLoader loader;
		qDebug() << "   ErbLoader created successfully";

		printStep(3, "Loading ERB files from directory");
		if (loader.loadDirectory(testDir)) {
				qDebug() << "   Successfully loaded ERB files from:" << testDir;
		} else {
				qDebug() << "   Failed to load ERB files from:" << testDir;
		}

		// 支持通过第二个参数传入外部文件路径进行单文件测试
		QString testFilePath = argc > 2 ? argv[2] : QDir(testDir).filePath("sample.ERB");
		printStep(4, QString("Testing single file load: %1").arg(testFilePath));
		if (QFile::exists(testFilePath)) {
				if (loader.loadFile(testFilePath)) {
						qDebug() << "   Successfully loaded file:" << testFilePath;
				} else {
						qDebug() << "   Failed to load file:" << testFilePath;
				}
		} else {
				qDebug() << "   External test file not found:" << testFilePath;
				qDebug() << "   (Please ensure the external .ERB file exists or pass the path via command line)";
		}

		printStep(5, "Checking loaded scripts and paths");
		QHash<QString, QList<ScriptLine>> scripts = loader.getLoadedScripts();
		qDebug() << "   Number of loaded scripts:" << scripts.size();

		for (auto it = scripts.constBegin(); it != scripts.constEnd(); ++it) {
				qDebug().nospace() << "   Script \"" << it.key() << "\" is at path: " << loader.getScriptPath(it.key());
		}

		printStep(6, "Testing label lookup");
		const QStringList sampleLabels = {"START", "END", "MAIN"};
		for (const QString& label : sampleLabels) {
				if (ScriptLine* labelLine = loader.findLabel(label)) {
						qDebug().nospace() << "   Found label @" << label << " at line " << labelLine->position().lineNumber;
				} else {
						qDebug().nospace() << "   Label @" << label << " not found";
				}
		}

		qDebug() << "\nERB loader test complete!";
		return 0;
}