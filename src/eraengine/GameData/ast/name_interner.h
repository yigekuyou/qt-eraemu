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
#ifndef NAME_INTERNER_H
#define NAME_INTERNER_H

#include <QString>
#include <QtGlobal>

// 变量身份的唯一来源。
//
// 旧形态是 stringly-typed 的「函数名 + 0x1F + 变量名」复合键，且复合键在
// computeVariableAccess / setPrivateScope / era_parse_table / restoreDeclaredDefaults
// 四处各手搓一次 —— 任一处差一个字节（大小写、分隔符、空的函数名）就会让同一个
// 变量悄悄变成另一个变量，且不报错。这里把它换成**名字 intern 表 + 整数 id**：
//
//   · 复合键的拼接只允许出现在 makePrivateStorageKey 一个函数里（唯一的 0x1F 知识）；
//   · 运行期身份比较是 quint32 相等，不再逐字符比较 QString；
//   · id 只能经 nameOf() 还原成文本（调试/测试渲染、存储层字符串键的解析）。
namespace eravar {

// 作用域身份 = **段身份** (脚本, 段起点行) 的 intern id（0 = 顶层：无私有作用域）。
// 段起点行 = 该段那个 @函数标签的行号（文件头段 = -1），与 LabelRef::functionLine
// 是同一个事实。同名 @F 的两份声明因此是**两个不同的段 id**（两份私有域）。
// 运行期「当前作用域」是可以显式传参的普通值，不再是存储层里需要求值器
// 去对账的环境状态。**函数名不再是作用域键** —— 名字只在需要「函数名」这一
// 事实的地方使用（形参、事件分组、RETURNF 类型检查）。
using ScopeId = quint32;
inline constexpr ScopeId kGlobalScope = 0;
inline constexpr ScopeId kInvalidScope = 0xFFFFFFFFu;

// 变量身份键：两个 intern id。
//   scope == 0  -> 无私有作用域（广域声明 / 未声明裸名 / LOCAL·ARG 等槽位名）
//   scope != 0  -> 私有作用域（声明所在函数名）的 id
// name == 0 表示「空名字」（Dynamic 节点不带静态键）。
struct VariableKey {
    quint32 scope = 0;
    quint32 name = 0;
    [[nodiscard]] bool operator==(const VariableKey&) const = default;
    [[nodiscard]] bool isPrivate() const noexcept { return scope != 0; }
    [[nodiscard]] bool isNull() const noexcept { return scope == 0 && name == 0; }
    // 记忆化用的打包键：(scope << 32) | name
    [[nodiscard]] quint64 packed() const noexcept { return (quint64(scope) << 32) | name; }
    // 调试/测试渲染："FUNC\x1fNAME"（私有）或 "BARE"（裸键）。
    // 只用于诊断与断言 —— 运行期判定一律走 id 比较。
    [[nodiscard]] QString debugKeyText() const;
};

// ---- 名字 intern 表 ----
// internName：把名字规范化成大写（ICVariable 语义）后登记，返回恒定 id（0 = 空名）。
//   进程生命周期内单调递增、**不复用**；并行装载期线程安全；运行期只读（命中路径）。
// nameOf：id -> 名字（id 0 / 越界 -> 空串）。返回的引用永不悬垂（见 .cpp 的 deque 说明）。
[[nodiscard]] quint32 internName(const QString& name);
[[nodiscard]] const QString& nameOf(quint32 id);

// ---- 段身份（私有作用域的唯一键）----
// 私有作用域 = 段身份 (脚本名, 段起点行)。段起点行 = 该段 @函数标签的行号
// （文件头段 = -1），与 LabelRef::functionLine 是同一份事实，装载期唯一写入。
// 返回的文本**总是含 ':'**（ERB 标识符不能含 ':'），因此它不可能与变量名
// 撞车：同一张 intern 表里，变量名 id 与作用域 id 不会互相冒充。
// 这是段身份文本的**唯一构造点**；其它代码只读装载期算好的字段。
[[nodiscard]] QString segmentKeyText(const QString& script, int functionLine);
// segmentKeyText 的逆：把段身份文本还原成 (脚本, 段起点行)。
// 存档条目的私有写盘名 = 段身份文本 + 0x1F + 名字，因此这是「私有变量属于
// 哪个段」从字节还原的唯一入口。格式不认识（缺长度/分隔/行号）返回 false。
// 注意：存储键构造点会做大写规范化，脚本名大小写不敏感（与 internName 同口径）。
[[nodiscard]] bool parseSegmentKey(const QString& text, QString& script, int& functionLine);

// ---- 复合键的唯一构造点 ----
// scopeUpper == 空串时也会得到 "\x1fNAME"（常数表键的历史形态，调用方语义自担）；
// 存储键的调用方只在「确实有私有作用域」时使用本函数。
[[nodiscard]] QString makePrivateStorageKey(const QString& scopeUpper, const QString& nameUpper);

// VariableKey -> 存储层字符串键（scope == 0 -> 裸名，否则复合键）。
// 纯函数；VariableStorage::storageKeyFor 在其上做 (scope<<32)|name 记忆化。
[[nodiscard]] QString storageKeyOf(const VariableKey& key);

// 存储键是否私有（含作用域分隔符）
[[nodiscard]] bool isPrivateStorageKey(const QString& storageKey);
// 拆分存储键（VARSIZE 的声明回查）：非复合键 -> scope 空、name = 整串
void splitPrivateStorageKey(const QString& storageKey, QString& scopeOut, QString& nameOut);

} // namespace eravar

#endif // NAME_INTERNER_H