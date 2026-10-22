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
#include <QDir>
#include <QFile>
#include <QStringList>
#include "erb_loader.h"
#include "era_parse_table.h"
#include "process_state.h"

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
		// 标签查询只有一份数据：EraParseTable 的唯一索引（裸名/大小写敏感/
		// last-wins 的裸名表已删）。
		ProcessState state;
		EraParseTable parseTable(&state);
		ErbLoader loader;
		loader.setParseTable(&parseTable);
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
		QHash<QString, QList<LogicalLine>> scripts = loader.getLoadedScripts();
		qDebug() << "   Number of loaded scripts:" << scripts.size();

		for (auto it = scripts.constBegin(); it != scripts.constEnd(); ++it) {
				qDebug().nospace() << "   Script \"" << it.key() << "\" is at path: " << loader.getScriptPath(it.key());
		}

		printStep(6, "Testing label lookup (EraParseTable::labelIndex)");
		// 全部装载结束再定型：索引的全序（脚本名 -> 行号）在收尾时固化。
		parseTable.markLoadComplete();
		const QStringList sampleLabels = {"START", "END", "MAIN"};
		for (const QString& label : sampleLabels) {
				const QList<LabelRef> refs = parseTable.labelIndex().value(label.toUpper());
				if (!refs.isEmpty()) {
						qDebug().nospace() << "   Found label " << label << " at "
										   << refs.first().script << ":" << refs.first().line
										   << " (" << refs.size() << " declaration(s))";
				} else {
						qDebug().nospace() << "   Label " << label << " not found";
				}
		}

		qDebug() << "\nERB loader test complete!";
		return 0;
}