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
#ifndef SAVE_VARIABLE_MAP_H
#define SAVE_VARIABLE_MAP_H

#include <QHash>
#include <QList>
#include <QString>

#include "era_binary_data.h"
#include "ast/name_interner.h"

class VariableConfig;
class VariableTable;

// ---------------------------------------------------------------------------
// 存档映射层：**存档条目 ↔ 变量身份**的唯一一张表。
//
// 存/读档以前是「遍历字符串键容器 + 自己拼/拆 FUNC\x1fNAME」，于是「键长什么
// 样」这件事同时被运行期和存档期知道。这里把存档期需要的事实收进一张表：
//
//   Entry::name        —— 写进文件/从文件读到的标识（逐字节与旧实现相同）
//   Entry::key         —— 变量身份（eravar::VariableKey，私有变量自带作用域 id）
//   Entry::slot        —— 该条目落在存储层的哪一类容器（数据，不是 if 链）
//   Entry::dimensions  —— 文件里的形状（不含角色维；空 = 标量）
//
// 存：按 entries() 顺序写出（顺序即旧的 saveShapes 顺序，保证字节相同）；
// 读：按文件里的名字 findByName() 找到行，再按 key 落回存储层。
// 新增一类变量 = 表里加一行；只有「新的容器形态」才需要新增 Slot 值
// （容器类型不同，取值/写值的实现必然要分开，那一处 switch 在
//  variable_storage.cpp 的 readSaveEntry/writeSaveCell）。
// ---------------------------------------------------------------------------
namespace erasave {

enum class Slot : quint8 {
    SystemInt1D,   // 有独立存储槽的系统 1D 整型（DAY/FLAG/A…）
    GlobalInt1D, GlobalInt2D, GlobalInt3D,
    GlobalStr1D, GlobalStr2D, GlobalStr3D,
    CharaInt1D, CharaInt2D, CharaStr1D, CharaStr2D,
};

struct Entry {
    QString name;                 // 存档文件里的标识
    eravar::VariableKey key;      // 变量身份（系统/广域：scope == 0）
    Slot slot = Slot::GlobalInt1D;
    bool string = false;
    QList<int> dimensions;
    bool user = false;            // 角色块里写进 userDefined 段
    bool persisted = true;        // false = 该行只是身份登记，不写文件
};

class Map
{
public:
    Map() = default;
    // 按存档种类构建（Normal/Global/Character；Var = Normal + Global）。
    [[nodiscard]] static Map build(const VariableConfig& config,
                                   const VariableTable* table,
                                   EraBinaryData::FileType type);

    [[nodiscard]] const QList<Entry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] bool isEmpty() const noexcept { return m_entries.isEmpty(); }
    // 读档：文件条目名 -> 行。同名时广域/内建行优先，私有行只在名字唯一时命中
    //（Emuera 的私有变量也是按裸名写盘的，同名会撞车 —— 与 C# 行为一致）。
    [[nodiscard]] const Entry* findByName(const QString& saveName) const;
    [[nodiscard]] const Entry* findByKey(const eravar::VariableKey& key) const;

private:
    void append(Entry entry);

    QList<Entry> m_entries;
    QHash<QString, int> m_byName;
    QHash<quint64, int> m_byKey;
};

// 数组元素数（最后一维连续）；越界/超限返回 0。
[[nodiscard]] int shapeCount(const QList<int>& dimensions);

} // namespace erasave

#endif // SAVE_VARIABLE_MAP_H