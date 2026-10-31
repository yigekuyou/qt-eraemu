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
#include "constant_table.h"
#include "csv_text_table.h"           // 同一份 CSV 字节只读一次
#include "GameData/game_paths.h"
#include "GameData/file_system_io.h"   // FileSystem::collectTree

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>
#include <string>
#include <string_view>
#include "system_variables.h"   // sysvar::extensionNameTableCsvOf（扩展名表映射）

namespace {

// 表键：文件名（大写、去扩展名）
QString tableKeyOf(const QString& fileName) {
    QString key = QFileInfo(fileName).completeBaseName().toUpper();
    return key;
}

} // namespace

QStringList ConstantTable::parseCsvLine(const QString& line) {
    QStringList out;
    QString current;
    bool inQuote = false;
    for (int i = 0; i < line.length(); ++i) {
        const QChar c = line.at(i);
        if (inQuote) {
            if (c == QLatin1Char('\\') && i + 1 < line.length()) {
                current += line.at(++i);
                continue;
            }
            if (c == QLatin1Char('"')) { inQuote = false; continue; }
            current += c;
            continue;
        }
        if (c == QLatin1Char('"')) { inQuote = true; continue; }
        if (c == QLatin1Char(',')) { out.append(current.trimmed()); current.clear(); continue; }
        current += c;
    }
    out.append(current.trimmed());
    return out;
}

bool ConstantTable::loadCsvFile(const QString& filePath, const QString& tableKey, CsvTextTable& texts) {
    // CSV 名表：字节来自 CSV 文本表（同一文件只被读一次；角色 CSV 随后还要被
    // CsvLoader 解析成角色变量，共用这一份）。编码嗅探在表里做。
    bool ok = false;
    QString text = texts.get(filePath, &ok);
    if (!ok) {
        return false;
    }
    if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);

    QStringList names;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char(';'))) continue;
        const QStringList cols = parseCsvLine(trimmed);
        if (cols.isEmpty()) continue;
        // 标准 Emuera 名表格式：第 0 列 = 数值下标，第 1 列 = 常量名（其后是注释列）。
        // 名字必须放进「第 0 列声明」的槽位——Base.csv 等存在空缺下标
        // （0,1,2,…,8,10,…），顺序 append 会让整个表错位。
        bool indexOk = false;
        const int declared = cols.first().toInt(&indexOk);
        if (indexOk && declared >= 0 && cols.size() >= 2) {
            if (names.size() <= declared) names.resize(declared + 1);
            names[declared] = cols.at(1);
            continue;
        }
        // 回退：无下标列的名表（每行第 0 列即名字）
        names.append(cols.first());
    }
    if (names.isEmpty()) {
        return false;
    }
    m_nameCount += names.size();
    m_tables.insert(tableKey, names);
    m_loadedFiles.append(filePath);
    return true;
}

int ConstantTable::loadCsvDirectory(const QString& csvDir, bool recursive) {
    // 薄壳：一次预扫描后查树装载（每目录只一次 provider 往返，见 collectTree）。
    // 保留原「目录不存在」告警语义。
    QDir dir(csvDir);
    if (!dir.exists()) {
        qWarning() << "[load] 常量名表目录不存在:" << csvDir;
        return 0;
    }
    return loadCsvDirectory(FileSystem::collectTree(csvDir), recursive);
}

namespace {

// 候选 CSV 文件数（进度条分母；纯内存遍历，零 provider 往返）。
void countCsvCandidates(const DirNode& n, bool recursive, int& total)
{
    for (const DirEntry& f : n.files) {
        if (!f.name.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) continue;
        const QString key = QFileInfo(f.name).completeBaseName().toUpper();
        if (key.startsWith(QLatin1Char('_'))) continue;   // _Rename 等不是常量表
        ++total;
    }
    if (!recursive) return;
    for (const DirNode& sub : n.subdirs) countCsvCandidates(sub, true, total);
}

} // namespace

int ConstantTable::loadCsvDirectory(const DirNode& tree, bool recursive,
                                    const std::function<void(int done, int total)>& onProgress,
                                    CsvTextTable* texts)
{
    // 文本表的唯一副本：调用方给了就共用（装载链：常量名表 → 角色 CSV），
    // 没给就自建一个（测试/独立调用方；每文件仍只读一次）。
    CsvTextTable local;
    CsvTextTable& src = texts ? *texts : local;
    int total = 0;
    countCsvCandidates(tree, recursive, total);
    int done = 0;
    const int loaded = loadCsvTreeInto(tree, recursive, onProgress, done, total, src);
    if (onProgress && total > 0) onProgress(total, total);   // 收尾到 100%
    return loaded;
}

int ConstantTable::loadCsvTreeInto(const DirNode& node, bool recursive,
                                   const std::function<void(int done, int total)>& onProgress,
                                   int& done, int total, CsvTextTable& texts) {
    int loaded = 0;
    for (const DirEntry& f : node.files) {
        if (!f.name.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) continue;
        // _Rename/_Replace/_default/_fixed 不是常量表（tableKeyOf = 去扩展名大写）
        const QString key = tableKeyOf(f.name);
        if (key.startsWith(QLatin1Char('_'))) continue;
        ++done;
        if (onProgress) onProgress(done, total);
        if (m_tables.contains(key)) continue;   // 同名（不同目录）只取先出现的
        // f.path 已是 entryInfoList 的 absoluteFilePath()，直接读取；
        // 不走 GamePaths::join（content:// 下每文件多一次 exists 探测）。
        if (loadCsvFile(f.path, key, texts)) ++loaded;
    }
    qDebug() << "[load] 常量名表" << node.path << ":" << loaded << "张（累计" << m_tables.size() << "张/" << m_nameCount << "名）";
    if (recursive) {
        for (const DirNode& sub : node.subdirs)
            loaded += loadCsvTreeInto(sub, true, onProgress, done, total, texts);
    }
    return loaded;
}

int ConstantTable::indexOf(const QString& csvFileName, const QString& name) const {
    const QString key = tableKeyOf(csvFileName);
    const auto it = m_tables.constFind(key);
    if (it == m_tables.constEnd()) return -1;
    const QStringList& names = it.value();
    for (int i = 0; i < names.size(); ++i) {
        if (names.at(i) == name) return i;
    }
    return -1;
}

QString ConstantTable::nameAt(const QString& csvFileName, int index) const {
    const QString key = tableKeyOf(csvFileName);
    const auto it = m_tables.constFind(key);
    if (it == m_tables.constEnd()) return QString();
    const QStringList& names = it.value();
    if (index < 0 || index >= names.size()) return QString();
    return names.at(index);
}

int ConstantTable::count(const QString& csvFileName) const {
    const auto it = m_tables.constFind(tableKeyOf(csvFileName));
    return it == m_tables.constEnd() ? 0 : it.value().size();
}

bool ConstantTable::hasTable(const QString& csvFileName) const {
    return m_tables.contains(tableKeyOf(csvFileName));
}

QStringList ConstantTable::tableNames() const {
    return m_tables.keys();
}

void ConstantTable::clear() {
    m_tables.clear();
    m_loadedFiles.clear();
    m_nameCount = 0;
}

// 原生变量名 → CSV 文件名（对齐 C# VariableCode 的 NameTable 归属）。
// 只含原生映射；fork 专有映射（DAY -> DAY.CSV）由扩展登记，见 csvForVariable。
QString ConstantTable::coreCsvForVariable(const QString& variableName) {
    static const QHash<QString, QString> map = {
        {QStringLiteral("FLAG"),      QStringLiteral("FLAG.CSV")},
        {QStringLiteral("TFLAG"),     QStringLiteral("TFLAG.CSV")},
        {QStringLiteral("CFLAG"),     QStringLiteral("CFLAG.CSV")},
        {QStringLiteral("TCVAR"),     QStringLiteral("TCVAR.CSV")},
        {QStringLiteral("ABL"),       QStringLiteral("ABL.CSV")},
        {QStringLiteral("EXP"),       QStringLiteral("EXP.CSV")},
        {QStringLiteral("TALENT"),    QStringLiteral("TALENT.CSV")},
        {QStringLiteral("MARK"),      QStringLiteral("MARK.CSV")},
        {QStringLiteral("PALAM"),     QStringLiteral("PALAM.CSV")},
        {QStringLiteral("BASE"),      QStringLiteral("BASE.CSV")},
        {QStringLiteral("MAXBASE"),   QStringLiteral("BASE.CSV")},
        {QStringLiteral("DOWNBASE"),  QStringLiteral("BASE.CSV")},
        {QStringLiteral("CUP"),       QStringLiteral("PALAM.CSV")},
        {QStringLiteral("CDOWN"),     QStringLiteral("PALAM.CSV")},
        {QStringLiteral("SOURCE"),    QStringLiteral("SOURCE.CSV")},
        {QStringLiteral("EX"),        QStringLiteral("EX.CSV")},
        {QStringLiteral("JUEL"),      QStringLiteral("JUEL.CSV")},
        {QStringLiteral("GOTJUEL"),   QStringLiteral("JUEL.CSV")},
        {QStringLiteral("NOWEX"),     QStringLiteral("EX.CSV")},
        {QStringLiteral("EQUIP"),     QStringLiteral("EQUIP.CSV")},
        {QStringLiteral("TEQUIP"),    QStringLiteral("TEQUIP.CSV")},
        {QStringLiteral("ITEM"),      QStringLiteral("ITEM.CSV")},
        {QStringLiteral("ITEMSALES"), QStringLiteral("ITEM.CSV")},
        {QStringLiteral("STAIN"),     QStringLiteral("STAIN.CSV")},
        // STR 的**值**来自 Str.csv，但 `STR:名前` 的**名表**是 StrName.csv
        // （C# ConstantData.ResolveName：case VariableCode.STR -> nameToIntDics[strnameIndex],
        //  errPos = "strname.csv"；而 STR.CSV 的逆引き字典被显式跳过「Strは逆引き無用」）。
        // 文档 Variable.html：`STRNAME ← StrName.csv`「Str.csv 指定的是 STR 的内容，
        // 而不是元素的名称」。此前错挂 STR.CSV，导致 `STR:<值文本>` 被当成下标。
        {QStringLiteral("STR"),       QStringLiteral("STRNAME.CSV")},
        {QStringLiteral("CSTR"),      QStringLiteral("CSTR.CSV")},
        {QStringLiteral("SAVESTR"),   QStringLiteral("SAVESTR.CSV")},
        {QStringLiteral("GLOBAL"),    QStringLiteral("GLOBAL.CSV")},
        {QStringLiteral("GLOBALS"),   QStringLiteral("GLOBALS.CSV")},
        {QStringLiteral("TSTR"),      QStringLiteral("TSTR.CSV")},
        // 注：DAY/TIME/MONEY 的名表是 fork（EmueraEM+EE）专有，不在此原生表；
        // 由扩展登记（ee_extension.cpp: regNameTable("DAY", "DAY.CSV")…）。
    };
    return map.value(variableName.toUpper());
}

// 合并查表：原生优先（核心不得被扩展改写），其次扩展名表映射，
// 最后是**用户变量**的兜底：CSV 文件名 == 变量名（大小写不敏感）。
//
// 为什么需要这条兜底：`#DIM SAVEDATA FLCONFIG, 300` 这类**用户自定义数组**
// 的要素名由 CSV/FLconfig.CSV 定义（101=BGMの音量、102=SEの音量…），
// 而 C# 的 ConstantData 只登记内建 VariableCode，靠 EmueraEM+EE 的
// 「CSV 文件名即变量名」约定接上用户变量。缺这条兜底时：
//   * `FLCONFIG:BGMの音量` 的下标既不是数字也不是已登记关键词，
//     被当成**普通变量名**求值 -> Unknown -> 整型全局 -> 永远 0；
//   * 于是 @INIT_CONFIG_FLCONFIG 里几十条 `FLCONFIG:xx = n` 全写到下标 0，
//     最后一次写入再把它盖成 0；`SETBGMVOLUME FLCONFIG:BGMの音量` 读到的
//     也是 0 -> 游戏在跑、音乐管线在跑，但音量 0 = 用户听不见声音；
//   * `GETNUM(FLCONFIG, "BGMの音量")` 返回 -1（配置页 [変更] 按钮值变成 -1）。
// 这条兜底只在本表里**确实存在**同名 CSV 时生效，不会把普通变量误判成关键词表。
QString ConstantTable::csvForVariable(const QString& variableName) {
    const QString core = coreCsvForVariable(variableName);
    if (!core.isEmpty()) return core;
    const std::string_view ext =
        sysvar::extensionNameTableCsvOf(variableName.toStdString());
    return ext.empty() ? QString() : QString::fromStdString(std::string(ext));
}

int ConstantTable::indexForVariable(const QString& variableName, const QString& name) const {
    QString csv = csvForVariable(variableName);
    if (csv.isEmpty()) {
        // 用户变量兜底：CSV 文件名 == 变量名（大小写不敏感）。
        // `#DIM SAVEDATA FLCONFIG, 300` 的要素名由 CSV/FLconfig.CSV 定义
        // （101=BGMの音量、102=SEの音量…）。C# 的 ConstantData 只登记内建
        // VariableCode，用户变量靠 EmueraEM+EE 的「CSV 名即变量名」约定接上；
        // 缺这条兜底时 `FLCONFIG:BGMの音量` 的下标既不是数字也不是已登记关键词，
        // 会被当成**普通变量名**求值（Unknown -> 整型全局 -> 恒 0）：
        //   * @INIT_CONFIG_FLCONFIG 里几十条 `FLCONFIG:xx = n` 全写进下标 0，
        //     最后一次写入再把它盖掉；`SETBGMVOLUME FLCONFIG:BGMの音量` 读到的
        //     同样是 0 —— 音乐管线在跑、解码在跑，音量 0 = 听不见；
        //   * `GETNUM(FLCONFIG, "BGMの音量")` 返回 -1（配置页 [変更] 按钮值 -1）。
        // 只在**本表确实存在同名 CSV** 时生效，不会把普通变量误判成关键词表。
        const QString userKey = tableKeyOf(variableName);
        if (!userKey.isEmpty() && !userKey.startsWith(QLatin1Char('_'))
            && m_tables.contains(userKey))
            csv = userKey;
    }
    if (csv.isEmpty()) return -1;
    return indexOf(csv, name);
}
