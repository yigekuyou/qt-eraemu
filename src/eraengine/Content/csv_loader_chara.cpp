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
// ---------------------------------------------------------------------------
// 角色 CSV 装载（对齐 C# GameData/ConstantData.cs 的 CharacterTemplate 读取）
//
//   <Csv>/Chara/*.csv 每行: <类型>[, <名字或下标>[, <值>]]
//     番号,N            -> 角色番号
//     名前/NAME         -> NAME 标量
//     呼び名/CALLNAME   -> CALLNAME 标量
//     あだ名/NICKNAME   -> NICKNAME 标量
//     主人の呼び方      -> MASTERNAME 标量
//     基礎/BASE         -> BASE + MAXBASE[i]   （名字表 Base.csv）
//     能力/ABL          -> ABL[i]              （Abl.csv）
//     素質/TALENT       -> TALENT[i]           （Talent.csv；值缺省 1）
//     経験/EXP          -> EXP[i]              （exp.csv）
//     刻印/MARK         -> MARK[i]             （Mark.csv）
//     相性/RELATION     -> RELATION[对方番号]  （下标只能是数字）
//     フラグ/CFLAG      -> CFLAG[i]            （CFLAG.csv）
//     装着物/EQUIP      -> EQUIP[i]            （Equip.csv）
//     珠/JUEL           -> JUEL[i]             （Palam.csv）
//     CSTR              -> CSTR[i]
//   下标可写数字或 CSV 名字表里的名字；数值行的值缺省/非数字按 C# 语义置 1。
// ---------------------------------------------------------------------------
#include "csv_loader.h"
#include "constant_table.h"
#include "text_encoding.h"
#include "GameData/Variable/variable_storage.h"

#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QSet>
#include <climits>
#include <functional>

namespace {

// 行内注释（`;` 之后）与首尾空白/引号剥离
QString cleanCell(QString cell) {
    const int semi = cell.indexOf(QLatin1Char(';'));
    if (semi >= 0) cell.truncate(semi);
    cell = cell.trimmed();
    if (cell.size() >= 2 && cell.startsWith(QLatin1Char('"')) && cell.endsWith(QLatin1Char('"')))
        cell = cell.mid(1, cell.size() - 2);
    return cell;
}

// tokens[1] 是数字下标还是名字；数字下标直接用
bool asIndex(const QString& cell, int& out) {
    if (cell.isEmpty()) return false;
    bool ok = false;
    const qlonglong v = cell.toLongLong(&ok);
    if (ok && v >= 0 && v <= INT_MAX) { out = static_cast<int>(v); return true; }
    return false;
}

} // namespace

int CsvLoader::loadCharaDirectory(const QString& csvDir,
                                  const ConstantTable* constants,
                                  VariableStorage* storage,
                                  bool recursive) {
    // 薄壳：一次预扫描后查树装载（每目录只一次 provider 往返，见 collectTree）。
    if (!storage) return 0;
    QDir dir(csvDir);
    if (!dir.exists()) return 0;
    return loadCharaDirectory(FileSystem::collectTree(csvDir), constants, storage, recursive);
}

int CsvLoader::loadCharaDirectory(const DirNode& tree,
                                  const ConstantTable* constants,
                                  VariableStorage* storage,
                                  bool recursive) {
    if (!storage) return 0;

    // 对齐 C# `Config.GetFiles(csvDir, "CHARA*.CSV")`：**文件名以 Chara 开头**
    // 的 *.csv，而不是「名为 Chara 的子目录」。遍历顺序对齐 C# Config.getFiles：
    // **先本目录文件（名序）、后子目录递归**（Directory.GetFiles →
    // GetDirectories 排序后逐个递归）；后载覆盖先载 -> 同名模板时子目录覆盖
    // 顶层。（此前路径版是「先子目录后本目录」，与 C# 相反；eraTW/eraH 无
    // 跨层同名，实际行为不变，此处修正为以 C# 为准。）
    const auto isCharaCsv = [](const QString& name) {
        return name.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)
               && name.startsWith(QLatin1String("Chara"), Qt::CaseInsensitive);
    };
    QStringList files;   // 完整路径（entryInfoList 的 absoluteFilePath，直接可读）
    const std::function<void(const DirNode&)> collect = [&](const DirNode& n) {
        for (const DirEntry& f : n.files)
            if (isCharaCsv(f.name)) files.append(f.path);
        if (!recursive) return;
        for (const DirNode& sub : n.subdirs)
            collect(sub);
    };
    collect(tree);

    int loadedChars = 0;
    QSet<QString> reportedUnknownTypes;   // 未知类型只留痕一次（同名去重）
    for (const QString& charaPath : files) {
        bool ok = false;
        QString content = TextCodecUtil::readFile(charaPath,
                                                  TextEncoding::Auto, nullptr, &ok);
        if (!ok || content.isEmpty()) continue;
        if (content.startsWith(QChar(0xFEFF))) content.remove(0, 1);   // UTF-8 BOM

        qint64 charaNo = -1;
        bool defined = false;
        QStringList lines = content.split(QLatin1Char('\n'));
        for (QString& l : lines) l.remove(QLatin1Char('\r'));
        for (const QString& rawLine : lines) {
            const int hash = rawLine.indexOf(QLatin1Char(';'));
            if (hash == 0) continue;                      // 整行注释
            const QString line = cleanCell(hash >= 0 ? rawLine.left(hash) : rawLine);
            if (line.isEmpty()) continue;
            const QStringList tokens = line.split(QLatin1Char(','));
            if (tokens.isEmpty()) continue;
            const QString type = tokens.at(0).trimmed().toUpper();

            // 番号
            if (type == QStringLiteral("番号") || type == QLatin1String("NO")) {
                bool okNo = false;
                const qint64 no = tokens.value(1).toLongLong(&okNo);
                if (okNo && no >= 0) { charaNo = no; defined = true; }
                continue;
            }
            // 字符串标量
            QString strVar;
            if (type == QLatin1String("NAME") || type == QStringLiteral("名前")) strVar = QStringLiteral("NAME");
            else if (type == QLatin1String("CALLNAME") || type == QStringLiteral("呼び名")) strVar = QStringLiteral("CALLNAME");
            else if (type == QLatin1String("NICKNAME") || type == QStringLiteral("あだ名")) strVar = QStringLiteral("NICKNAME");
            else if (type == QLatin1String("MASTERNAME") || type == QStringLiteral("主人の呼び方")) strVar = QStringLiteral("MASTERNAME");
            if (!strVar.isEmpty()) {
                if (charaNo >= 0 && tokens.size() >= 2)
                    storage->setCharaStr(strVar, static_cast<int>(charaNo), 0, cleanCell(tokens.at(1)));
                continue;
            }

            // 数值/字符串数组记录
            QString var;
            bool isStr = false;
            if (type == QLatin1String("BASE") || type == QStringLiteral("基礎")) var = QStringLiteral("BASE");
            else if (type == QLatin1String("ABL") || type == QStringLiteral("能力")) var = QStringLiteral("ABL");
            else if (type == QLatin1String("TALENT") || type == QStringLiteral("素質")) var = QStringLiteral("TALENT");
            else if (type == QLatin1String("EXP") || type == QStringLiteral("経験")) var = QStringLiteral("EXP");
            else if (type == QLatin1String("MARK") || type == QStringLiteral("刻印")) var = QStringLiteral("MARK");
            else if (type == QLatin1String("CFLAG") || type == QStringLiteral("フラグ")) var = QStringLiteral("CFLAG");
            else if (type == QLatin1String("EQUIP") || type == QStringLiteral("装着物")) var = QStringLiteral("EQUIP");
            else if (type == QLatin1String("JUEL") || type == QStringLiteral("珠")) var = QStringLiteral("JUEL");
            else if (type == QLatin1String("CSTR")) { var = QStringLiteral("CSTR"); isStr = true; }
            else if (type == QLatin1String("RELATION") || type == QStringLiteral("相性")) var = QStringLiteral("RELATION");
            else {
                // C# 对未知类型有告警（CharaData 解析失败可见）；此前静默 continue，
                // 整个条目被丢掉却毫无痕迹 —— 至少留痕一次（同名去重）。
                if (!reportedUnknownTypes.contains(type)) {
                    reportedUnknownTypes.insert(type);
                    qWarning() << "[未完成] 角色 CSV 的未知类型（条目被跳过）:" << type
                               << "行:" << line;
                }
                continue;
            }

            if (charaNo < 0 || tokens.size() < 2) continue;
            const QString nameCell = cleanCell(tokens.at(1));

            int index = -1;
            if (var == QLatin1String("RELATION")) {
                // 相性的下标是对方角色番号（无名字表）
                if (!asIndex(nameCell, index)) continue;
            } else if (!asIndex(nameCell, index)) {
                index = constants ? constants->indexForVariable(var, nameCell) : -1;
                if (index < 0) continue;                   // 名字表里没有该名字
            }

            const int charaId = static_cast<int>(charaNo);
            if (isStr) {
                storage->setCharaStr(var, charaId, index, cleanCell(tokens.value(2)));
            } else {
                // C#：值缺省/非数字 -> 1（素質/フラグ 的常见两列写法）
                qint64 value = 1;
                if (tokens.size() >= 3) {
                    bool okV = false;
                    const qint64 v = cleanCell(tokens.at(2)).toLongLong(&okV);
                    if (okV) value = v;
                }
                storage->setCharaInt(var, charaId, index, value);
                // 基礎 同时初始化 MAXBASE（C# 基礎 写 Maxbase，BASE 由此派生）
                if (var == QLatin1String("BASE"))
                    storage->setCharaInt(QStringLiteral("MAXBASE"), charaId, index, value);
            }
        }
        if (defined) {
            ++loadedChars;
            // 登记「该番号的角色模板存在」（EXISTCSV 用）
            if (charaNo >= 0) storage->markCsvExists(static_cast<int>(charaNo));
        }
    }
    qDebug() << "[load] 角色 CSV" << tree.path << (recursive ? "(含子目录)" : "") << ":"
             << loadedChars << "个模板 /" << files.size() << "文件";
    return loadedChars;
}
