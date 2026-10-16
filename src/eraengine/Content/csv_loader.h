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
#ifndef CSV_LOADER_H
#define CSV_LOADER_H

#include <QObject>
#include <QString>
#include <QHash>
#include <QList>
#include <QFile>
#include <QTextStream>
#include <functional>

#include "GameData/file_system_io.h"   // DirNode（树版 loadCharaDirectory）

// CSV data loader - loads CSV files for game data
class ConstantTable;
class VariableStorage;
class CsvLoader : public QObject {
    Q_OBJECT

public:
    explicit CsvLoader(QObject* parent = nullptr);

    // Load CSV file
    bool loadFile(const QString& filePath);

    // 角色 CSV（<Csv> 下 Chara*.csv -> NAME/CALLNAME/BASE/ABL/…）：
    // 返回成功装载的角色数（对齐 C# ConstantData.loadCharacterData 的
    // `Config.GetFiles(csvDir, "CHARA*.CSV")` —— 即**扫描 CSV 目录下所有
    // 名为 Chara*.csv 的文件**，recursive = Config.SearchSubdirectory；
    // 不是只找名为 Chara 的子目录。eraTW 把角色 CSV 放在 CSV/Chara/ 下，
    // eraH 则按种族分放在 CSV/人間/** 与 CSV/悪魔/** 下，两种布局都要能找到。）
    static int loadCharaDirectory(const QString& csvDir,
                                  const ConstantTable* constants,
                                  VariableStorage* storage,
                                  bool recursive = false);

    // 树版：消费 FileSystem::collectTree 预扫描的路径树（零 provider 往返）。
    // 遍历顺序对齐 C# Config.getFiles：**本目录文件（名序）在前、子目录递归
    // 在后**（Config.cs getFiles 先 Directory.GetFiles 再 GetDirectories 递归）。
    // loadCharacterData 按此顺序装载、后载覆盖先载 —— 同名模板时**子目录
    // 覆盖顶层**。路径版历史实现是「先子目录后本目录」（顶层覆盖子目录），
    // 与 C# 相反；eraTW/eraH 的 Chara*.csv 均无跨层同名，实际行为不受影响，
    // 但树版起以 C# 为准（test_dir_tree 锁定该顺序）。
    // onProgress(done, total)：已处理 / 待读取的角色 CSV 文件数（在调用线程
    // 触发；供装载遮罩显示读取进度）。
    static int loadCharaDirectory(const DirNode& tree,
                                  const ConstantTable* constants,
                                  VariableStorage* storage,
                                  bool recursive = false,
                                  const std::function<void(int done, int total)>& onProgress = {});

    // Access data
    QVariant getValue(const QString& tableName, int row, int col);
    QString getString(const QString& tableName, int row, int col);
    int getInt(const QString& tableName, int row, int col);
    
    // Get table info
    QStringList getTableNames() const;
    int getRowCount(const QString& tableName) const;
    int getColumnCount(const QString& tableName) const;
    
private:
    // Parse a single CSV line
		QStringList parseLine(const QString& line);

    // Load CSV content into data structure
		void loadCsvData(const QString& filePath, const QString& content);
		QStringList getRowByFirstColumn(const QString& tableName, const QString& key) const;
    
    // CSV data storage: table name -> rows -> columns
    QHash<QString, QList<QList<QString>>> m_data;
    QHash<QString, int> m_rowCounts;
};

#endif // CSV_LOADER_H
