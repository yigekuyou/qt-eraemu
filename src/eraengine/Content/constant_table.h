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
#ifndef CONSTANT_TABLE_H
#define CONSTANT_TABLE_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <functional>

#include "GameData/file_system_io.h"   // DirNode（树版 loadCsvDirectory）

class CsvTextTable;

// ---------------------------------------------------------------------------
// ConstantTable —— CSV 常量名表（对齐 C# GameData/ConstantData.cs 的子集）
//
// 用途：把「变量字符串下标」解析为整数下标，例如
//   CFLAG:ARG:１moreフラグ   →  CFLAG:ARG:<CFLAG.CSV 中的行号>
//   ABL:MASTER:@"技能3"      →  ABL:MASTER:<ABL.CSV 中的行号>
//
// C# 的做法：VariableToken 带一个 NameTable（如 FLAGNAME/CFLAGNAME），
// 字符串下标经 ConstantData 查表得到索引。
//
// 本表只做「CSV 文件 → 第 0 列名字列表」，不涉及具体变量语义，
// 因此装载入口只有一个：loadCsvDirectory()（只在 CSV 目录内检索，对齐 C# Program.CsvDir）。
// ---------------------------------------------------------------------------
class ConstantTable {
public:
    ConstantTable() = default;

    // 装载 CSV 目录下的全部 *.csv（recursive = Config.SearchSubdirectory）。
    // 返回成功装载的表数量。m_loadedFiles 记录实际读到的文件。
    int loadCsvDirectory(const QString& csvDir, bool recursive);

    // 树版：消费 FileSystem::collectTree 预扫描的路径树（零 provider 往返，
    // SAF 多趟遍历是加载耗时大头 —— 见 file_system_io.h 的 DirNode 注释）。
    // 遍历顺序与路径版一致 —— 本目录文件（名序）在前、子目录递归在后，
    // 同名表只取先出现者。recursive=false 只查树根文件。
    // onProgress(done, total)：已处理 / 候选 CSV 文件数（在调用线程触发；
    // 供装载遮罩显示读取进度）。
    // texts：CSV 文本表（见 csv_text_table.h）。传非空时**不再读盘**，从表里取
    // 同一份字节 —— 角色 CSV 等同一批文件随后还要被 CsvLoader 读，共用一次读取。
    // 传空 = 本表自建一个临时表（每个文件仍只读一次，但没有跨消费者共享）。
    int loadCsvDirectory(const DirNode& tree, bool recursive,
                         const std::function<void(int done, int total)>& onProgress = {},
                         CsvTextTable* texts = nullptr);

    // CSV 文件名（大小写不敏感，可带/不带 .csv）内的名字 → 行号（0 起）
    [[nodiscard]] int indexOf(const QString& csvFileName, const QString& name) const;
    // 行号 → 名字；越界返回空
    [[nodiscard]] QString nameAt(const QString& csvFileName, int index) const;
    [[nodiscard]] int count(const QString& csvFileName) const;
    [[nodiscard]] bool hasTable(const QString& csvFileName) const;
    [[nodiscard]] QStringList tableNames() const;

    // 变量名 → CSV 文件名（对齐 C# VariableCode 的 ～NAME 系变量）。
    // = 原生映射（coreCsvForVariable）优先，其次扩展名表映射
    // （ExtensionRegistry::regNameTable 登记的 fork 专有映射，如 DAY -> DAY.CSV）。
    [[nodiscard]] static QString csvForVariable(const QString& variableName);
    // 仅原生映射（对齐 C# ConstantData.ResolveName 的固定表）；供扩展 fail-fast
    // 判断「核心是否已映射该变量」。
    [[nodiscard]] static QString coreCsvForVariable(const QString& variableName);

    // 变量 + 名字 → 下标（无对应表/未命中返回 -1）
    [[nodiscard]] int indexForVariable(const QString& variableName, const QString& name) const;

    [[nodiscard]] bool isEmpty() const { return m_tables.isEmpty(); }
    [[nodiscard]] int tableCount() const { return m_tables.size(); }
    [[nodiscard]] int nameCount() const { return m_nameCount; }
    [[nodiscard]] const QStringList& loadedFiles() const { return m_loadedFiles; }

    void clear();

private:
    // 树版递归主体：done/total 跨层累计，每处理一个候选文件回调一次进度。
    int loadCsvTreeInto(const DirNode& node, bool recursive,
                        const std::function<void(int done, int total)>& onProgress,
                        int& done, int total, CsvTextTable& texts);

    // 解析单个 CSV 为名字表（取每行第 0 列；跳过空行与 ';' 注释）。
    // 字节从 texts 取（同一文件只读一次），解析语义只在这里。
    bool loadCsvFile(const QString& filePath, const QString& tableKey, CsvTextTable& texts);
    // 解析单行（支持引号包裹与 \" 转义）
    static QStringList parseCsvLine(const QString& line);

    // 表键：CSV 文件名（大写、不含扩展名）
    QHash<QString, QStringList> m_tables;
    QStringList m_loadedFiles;
    int m_nameCount = 0;
};

#endif // CONSTANT_TABLE_H
