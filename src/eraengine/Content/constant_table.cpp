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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>
#include "text_encoding.h"

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

bool ConstantTable::loadCsvFile(const QString& filePath, const QString& tableKey) {
    // CSV 名表：同样按文件嗅探编码（CSV 可能是 Shift-JIS 或 UTF-8）
    bool ok = false;
    QString text = TextCodecUtil::readFile(filePath, TextEncoding::Auto, nullptr, &ok);
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
    QDir dir(csvDir);
    if (!dir.exists()) {
        qWarning() << "[load] 常量名表目录不存在:" << csvDir;
        return 0;
    }
    int loaded = 0;
    const QDir::Filters fileFilter = QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot;
    const QFileInfoList files = dir.entryInfoList(fileFilter, QDir::Name);
    for (const QFileInfo& fi : files) {
        if (!fi.fileName().endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) continue;
        // _Rename/_Replace/_default/_fixed 不是常量表
        const QString base = fi.completeBaseName().toUpper();
        if (base.startsWith(QLatin1Char('_'))) continue;
        const QString key = tableKeyOf(fi.fileName());
        if (m_tables.contains(key)) continue;   // 同名（不同目录）只取先出现的
        if (loadCsvFile(fi.absoluteFilePath(), key)) ++loaded;
    }
    qDebug() << "[load] 常量名表" << csvDir << ":" << loaded << "张（累计" << m_tables.size() << "张/" << m_nameCount << "名）";
    if (recursive) {
        const QStringList dirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& d : dirs) {
            loaded += loadCsvDirectory(dir.absoluteFilePath(d), true);
        }
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

// 变量名 → CSV 文件名（对齐 C# VariableCode 的 NameTable 归属）
QString ConstantTable::csvForVariable(const QString& variableName) {
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
        {QStringLiteral("STR"),       QStringLiteral("STR.CSV")},
        {QStringLiteral("CSTR"),      QStringLiteral("CSTR.CSV")},
        {QStringLiteral("SAVESTR"),   QStringLiteral("SAVESTR.CSV")},
        {QStringLiteral("GLOBAL"),    QStringLiteral("GLOBAL.CSV")},
        {QStringLiteral("GLOBALS"),   QStringLiteral("GLOBALS.CSV")},
        {QStringLiteral("TSTR"),      QStringLiteral("TSTR.CSV")},
    };
    return map.value(variableName.toUpper());
}

int ConstantTable::indexForVariable(const QString& variableName, const QString& name) const {
    const QString csv = csvForVariable(variableName);
    if (csv.isEmpty()) return -1;
    return indexOf(csv, name);
}
