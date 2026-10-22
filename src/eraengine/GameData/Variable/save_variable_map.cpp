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

#include <algorithm>

namespace erasave {
namespace {

// 条目集合沿用旧 saveShapes 的清单（哪些变量进档不能改）；**写出顺序不再
// 取自这里的追加序**，而由 Map::finalize() 排序决定（见下）。
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

// ---------------------------------------------------------------------------
// 建表收尾：排序 + 重建索引。
//
// 背景：用户 SAVEDATA 声明的枚举顺序来自 VariableTable::declarations() 的
// QHash 遍历（Qt 每进程随机播种）——顺序曾经「靠运气」。这里把条目排成
// **只由数据决定的全序**，之后任何进程、任何 QT_HASH_SEED 下，同一变量状态
// encode 出的存档字节逐字节相同：
//   主键 slot（枚举值升序）
//   → 次键名字的大写形式（QString::compare 码元比较，不依赖 locale）
//   → 内建行（user == false）先于用户行
//   → 私有作用域名（广域 = 空串；eravar::nameOf 的文本，不用 intern id
//      —— 并行装载下 id 没有跨进程稳定性，文本有）。
// 次级键只用于消同名歧义（同名私有变量分属不同函数、用户声明重打内建名）。
// 排序只定**写出顺序**；按名/按键索引的胜负不参与裁决 —— 见下：排序前快照
// append 的结果、经置换重映射，任何同名并列（含用户重打内建名的跨 slot
// 并列）胜者都与排序前逐位相同，不需要用排序键论证。
void Map::finalize()
{
    const int n = int(m_entries.size());
    QVector<QString> upper;   // 次键：大写名（toUpper 是码元的纯函数，无 locale）
    upper.reserve(n);
    QVector<QString> scope;   // 并列裁决：作用域名（scope == 0 -> 空串）
    scope.reserve(n);
    for (const Entry& e : m_entries) {
        upper.append(e.name.toUpper());
        scope.append(eravar::nameOf(e.key.scope));
    }
    QList<int> order;
    order.reserve(n);
    for (int i = 0; i < n; ++i) order.append(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const Entry& ea = m_entries.at(a);
        const Entry& eb = m_entries.at(b);
        if (ea.slot != eb.slot) return int(ea.slot) < int(eb.slot);
        const int byName = QString::compare(upper.at(a), upper.at(b));
        if (byName != 0) return byName < 0;
        if (ea.user != eb.user) return !ea.user;
        return QString::compare(scope.at(a), scope.at(b)) < 0;
    });
    // 索引胜负不重新裁决：快照排序前 append 的结果（非私有行后来者覆盖、
    // 私有行只在名字未被占用时登记），经置换重映射到新位置。
    const QHash<QString, int> byName = m_byName;
    const QHash<quint64, int> byKey = m_byKey;
    QList<int> inverse(n);
    for (int i = 0; i < n; ++i) inverse[order.at(i)] = i;
    QList<Entry> sorted;
    sorted.reserve(n);
    for (int i : order) sorted.append(std::move(m_entries[i]));
    m_entries = std::move(sorted);
    m_byName.clear();
    for (auto it = byName.constBegin(); it != byName.constEnd(); ++it)
        m_byName.insert(it.key(), inverse.at(it.value()));
    m_byKey.clear();
    for (auto it = byKey.constBegin(); it != byKey.constEnd(); ++it)
        m_byKey.insert(it.key(), inverse.at(it.value()));
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
                // 自带作用域 id（段身份），存档期不再拼 FUNC\x1fNAME。
                // 段身份缺失说明声明不属于任何段（装载期按 Global 处理，不会出现；
                // 合成夹具曾用函数名顶替）。这里不退回函数名 —— 那等于把身份改回
                // 旧形态 —— 直接不入表。
                if (d.scopeKey.isEmpty()) continue;
                e.key = eravar::VariableKey{eravar::internName(d.scopeKey), eravar::internName(d.name)};
                // 文件标识 = 段身份文本 + 0x1F + 名字：私有条目从此可唯一还原。
                e.name = eravar::makePrivateStorageKey(d.scopeKey, d.name);
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
    // 收尾：数据决定的全序 —— 存档字节从此与 QHash 遍历序/QT_HASH_SEED 无关。
    map.finalize();
    return map;
}

} // namespace erasave