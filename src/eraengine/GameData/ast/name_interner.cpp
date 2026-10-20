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
#include "name_interner.h"

#include <QHash>

#include <deque>
#include <mutex>

namespace {

// ★ 整个仓库 src/ 里唯一知道「私有存储键 = 作用域 + 0x1F + 名字」的地方。
// 任何需要这个字符串的代码都必须经 makePrivateStorageKey / storageKeyOf 取得。
constexpr QChar kScopeSeparator(0x1f);

// intern 表本体。
//   names 用 std::deque：push_back 不搬迁既有元素 —— nameOf() 返回的
//   const QString& 在任何后续登记之后仍然有效（std::vector 的 reallocation
//   会悬垂，QList/vector 同理）。id 单调递增、元素永不删除 => id 本身就是
//   稳定的数组下标 + 1。
//   ids 与 names 都在同一把 mutex 下维护：装载期按脚本并行（QThreadPool），
//   多个线程可能同时对同一名字 internName —— 必须原子地「查/插入」。
struct InternTable {
    std::mutex mutex;
    QHash<QString, quint32> ids;
    std::deque<QString> names;
};

InternTable& internTable()
{
    static InternTable table;
    return table;
}

// id 0 / 越界的统一返回值（函数内静态，线程安全初始化）
const QString& emptyName()
{
    static const QString kEmpty;
    return kEmpty;
}

} // namespace

namespace eravar {

quint32 internName(const QString& name)
{
    if (name.isEmpty()) return 0;
    // ICVariable 语义：标识符大小写不敏感，身份按大写登记。规范化放在这里
    // （而不是各调用点）—— 这样 computeVariableAccess 与 setPrivateScope 即便
    // 传入的大小写形态不同，也必然得到同一个 id。
    const QString upper = name.toUpper();
    if (upper.isEmpty()) return 0;
    InternTable& t = internTable();
    std::lock_guard<std::mutex> lock(t.mutex);
    const auto it = t.ids.constFind(upper);
    if (it != t.ids.constEnd()) return it.value();
    const quint32 id = quint32(t.names.size()) + 1;   // 0 保留给「空名/无作用域」
    t.names.push_back(upper);
    t.ids.insert(upper, id);
    return id;
}

const QString& nameOf(quint32 id)
{
    if (id == 0) return emptyName();
    InternTable& t = internTable();
    std::lock_guard<std::mutex> lock(t.mutex);
    if (id > t.names.size()) return emptyName();
    // deque 元素地址稳定（见 InternTable 注释），锁释放后引用依旧有效。
    return t.names[id - 1];
}

QString makePrivateStorageKey(const QString& scopeUpper, const QString& nameUpper)
{
    // 两个参数**两种来源**（VariableTable 的声明 / VariableStorage 的作用域名表）
    // 都在这里再规范化一次：单一构造点同时是单一规范化点，杜绝「一处 nk()、
    // 一处 toUpper()」在极端字符（titlecase）上分叉的可能。
    QString key = scopeUpper.toUpper();
    key += kScopeSeparator;
    key += nameUpper.toUpper();
    return key;
}

QString storageKeyOf(const VariableKey& key)
{
    if (key.scope == 0) return nameOf(key.name);
    return makePrivateStorageKey(nameOf(key.scope), nameOf(key.name));
}

bool isPrivateStorageKey(const QString& storageKey)
{
    return storageKey.contains(kScopeSeparator);
}

void splitPrivateStorageKey(const QString& storageKey, QString& scopeOut, QString& nameOut)
{
    const qsizetype separator = storageKey.indexOf(kScopeSeparator);
    if (separator < 0) {
        scopeOut.clear();
        nameOut = storageKey;
        return;
    }
    scopeOut = storageKey.left(separator);
    nameOut = storageKey.mid(separator + 1);
}

QString VariableKey::debugKeyText() const
{
    if (scope == 0) return nameOf(name);
    return makePrivateStorageKey(nameOf(scope), nameOf(name));
}

} // namespace eravar