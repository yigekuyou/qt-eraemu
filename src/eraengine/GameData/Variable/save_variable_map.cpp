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
#include "save_variable_map.h"

#include "variable_config.h"
#include "ast/variable_table.h"
#include "ast/system_variables.h"

namespace erasave {
namespace {

// 旧 saveShapes 的列表（顺序即写盘顺序，不能改）：
//   角色：ISASSI NO / NAME CALLNAME NICKNAME MASTERNAME /
//          BASE…TCVAR（1D 整型）/ CSTR（1D 字符串）/ CDFLAG（2D 整型）
//   普通：DAY…BOUGHT + A–Z（1D 整型）/ SAVESTR TSTR（1D 字符串）/
//         DITEMTYPE DA–DE（2D 整型）/ TA TB（3D 整型）
//   广域：GLOBAL（1D 整型）/ GLOBALS（1D 字符串）
QStringList words(const char* text)
{
    return QString::fromLatin1(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

Slot intSlotFor(int rank, bool character)
{
    if (character) return rank >= 2 ? Slot::CharaInt2D : Slot::CharaInt1D;
    return rank >= 3 ? Slot::GlobalInt3D : rank == 2 ? Slot::GlobalInt2D : Slot::GlobalInt1D;
}

Slot strSlotFor(int rank, bool character)
{
    if (character) return rank >= 2 ? Slot::CharaStr2D : Slot::CharaStr1D;
    return rank >= 3 ? Slot::GlobalStr3D : rank == 2 ? Slot::GlobalStr2D : Slot::GlobalStr1D;
}

} // namespace

int shapeCount(const QList<int>& dimensions)
{
    qint64 count = 1;
    for (int n : dimensions) {
        if (n < 0 || n > 16 * 1024 * 1024 || (n && count > 16 * 1024 * 1024 / n)) return 0;
        count *= n;
    }
    return int(count);
}

void Map::append(Entry entry)
{
    const int index = int(m_entries.size());
    // 私有行只在名字尚未被广域/内建行占用时参与「按文件名回查」。
    if (!entry.key.isPrivate() || !m_byName.contains(entry.name)) m_byName.insert(entry.name, index);
    m_byKey.insert(entry.key.packed(), index);
    m_entries.append(std::move(entry));
}

const Entry* Map::findByName(const QString& saveName) const
{
    const auto it = m_byName.constFind(saveName);
    return it == m_byName.constEnd() ? nullptr : &m_entries.at(it.value());
}

const Entry* Map::findByKey(const eravar::VariableKey& key) const
{
    const auto it = m_byKey.constFind(key.packed());
    return it == m_byKey.constEnd() ? nullptr : &m_entries.at(it.value());
}

Map Map::build(const VariableConfig& config, const VariableTable* table, EraBinaryData::FileType type)
{
    Map map;
    const auto globalKey = [](const QString& name) {
        return eravar::VariableKey{eravar::kGlobalScope, eravar::internName(name)};
    };
    const auto push = [&](const QString& name, bool string, QList<int> dims, Slot slot, bool user) {
        Entry e;
        e.name = name;
        e.key = globalKey(name);
        e.string = string;
        e.slot = slot;
        e.dimensions = std::move(dims);
        e.user = user;
        map.append(std::move(e));
    };
    const auto size1 = [&](const QString& name) { return QList<int>{qMax(0, config.getSize1D(name))}; };
    const auto size2 = [&](const QString& name) {
        const auto d = config.getSize2D(name);
        return QList<int>{qMax(0, d.first), qMax(0, d.second)};
    };
    const auto size3 = [&](const QString& name) {
        const auto d = config.getSize3D(name);
        return QList<int>{qMax(0, d.value(0)), qMax(0, d.value(1)), qMax(0, d.value(2))};
    };

    const auto addCharacter = [&] {
        for (const QString& n : words("ISASSI NO")) push(n, false, {}, Slot::CharaInt1D, false);
        for (const QString& n : words("NAME CALLNAME NICKNAME MASTERNAME"))
            push(n, true, {}, Slot::CharaStr1D, false);
        for (const QString& n : words("BASE MAXBASE ABL TALENT EXP MARK PALAM SOURCE EX CFLAG JUEL "
                                      "RELATION EQUIP TEQUIP STAIN GOTJUEL NOWEX DOWNBASE CUP CDOWN TCVAR"))
            push(n, false, size1(n), Slot::CharaInt1D, false);
        push(QStringLiteral("CSTR"), true, size1(QStringLiteral("CSTR")), Slot::CharaStr1D, false);
        push(QStringLiteral("CDFLAG"), false, size2(QStringLiteral("CDFLAG")), Slot::CharaInt2D, false);
    };
    const auto addNormal = [&] {
        for (const QString& n : words("DAY MONEY ITEM FLAG TFLAG UP PALAMLV EXPLV EJAC DOWN RESULT COUNT "
                                      "TARGET ASSI MASTER NOITEM LOSEBASE SELECTCOM ASSIPLAY PREVCOM TIME "
                                      "ITEMSALES PLAYER NEXTCOM PBAND BOUGHT RANDDATA"))
            push(n, false, size1(n),
                 sysvar::systemStorageNames().contains(n) ? Slot::SystemInt1D : Slot::GlobalInt1D, false);
        for (char c = 'A'; c <= 'Z'; ++c) {
            const QString n{QChar(c)};
            push(n, false, size1(n), Slot::SystemInt1D, false);
        }
        push(QStringLiteral("SAVESTR"), true, size1(QStringLiteral("SAVESTR")), Slot::GlobalStr1D, false);
        push(QStringLiteral("TSTR"), true, size1(QStringLiteral("TSTR")), Slot::GlobalStr1D, false);
        for (const QString& n : words("DITEMTYPE DA DB DC DD DE"))
            push(n, false, size2(n), Slot::GlobalInt2D, false);
        push(QStringLiteral("TA"), false, size3(QStringLiteral("TA")), Slot::GlobalInt3D, false);
        push(QStringLiteral("TB"), false, size3(QStringLiteral("TB")), Slot::GlobalInt3D, false);
    };
    const auto addGlobal = [&] {
        push(QStringLiteral("GLOBAL"), false, size1(QStringLiteral("GLOBAL")), Slot::GlobalInt1D, false);
        push(QStringLiteral("GLOBALS"), true, size1(QStringLiteral("GLOBALS")), Slot::GlobalStr1D, false);
    };

    if (type == EraBinaryData::FileType::Normal || type == EraBinaryData::FileType::Var) {
        addNormal();
        if (table) for (const VariableDecl& d : table->declarationsRef()) {
            if (!d.isSavedata || d.isConst || d.isReference || d.isArg || d.isCharaData || d.isGlobal) continue;
            Entry e;
            e.name = d.name;
            e.string = d.type == OperandType::Str;
            e.dimensions = d.lengths.isEmpty() ? QList<int>{1} : d.lengths;
            e.slot = e.string ? strSlotFor(e.dimensions.size(), false)
                              : intSlotFor(e.dimensions.size(), false);
            e.user = true;
            if (d.scope == VarScope::Local) {
                // 函数私有静态（C# userDefinedStaticVarList + IsSavedata）：身份
                // 自带作用域 id，存档期不再拼 FUNC\x1fNAME。
                e.key = eravar::VariableKey{eravar::internName(d.function), eravar::internName(d.name)};
            } else {
                e.key = globalKey(d.name);
            }
            map.append(std::move(e));
        }
    }
    if (type == EraBinaryData::FileType::Global || type == EraBinaryData::FileType::Var) {
        addGlobal();
        if (table) for (const VariableDecl& d : table->declarationsRef()) {
            if (!d.isSavedata || d.isConst || d.isReference || d.isArg
                || d.isCharaData || !d.isGlobal || d.scope != VarScope::Global) continue;
            Entry e;
            e.name = d.name;
            e.key = globalKey(d.name);
            e.string = d.type == OperandType::Str;
            e.dimensions = d.lengths.isEmpty() ? QList<int>{1} : d.lengths;
            e.slot = e.string ? strSlotFor(e.dimensions.size(), false)
                              : intSlotFor(e.dimensions.size(), false);
            e.user = true;
            map.append(std::move(e));
        }
    }
    if (type == EraBinaryData::FileType::Character) {
        addCharacter();
        if (table) for (const VariableDecl& d : table->declarationsRef()) {
            if (!d.isSavedata || d.isConst || d.isReference || d.isArg
                || !d.isCharaData || d.isGlobal || d.scope != VarScope::Global) continue;
            // 参考实现的角色读取没有 3D 情形（旧 saveShapes 同样跳过）。
            if (d.dimension > 2) continue;
            Entry e;
            e.name = d.name;
            e.key = globalKey(d.name);
            e.string = d.type == OperandType::Str;
            e.dimensions = d.lengths.isEmpty() ? QList<int>{1} : d.lengths;
            e.slot = e.string ? strSlotFor(e.dimensions.size(), true)
                              : intSlotFor(e.dimensions.size(), true);
            e.user = true;
            map.append(std::move(e));
        }
    }
    return map;
}

} // namespace erasave