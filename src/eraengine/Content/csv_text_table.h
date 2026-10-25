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
#ifndef CSV_TEXT_TABLE_H
#define CSV_TEXT_TABLE_H

#include <QHash>
#include <QString>

// ---------------------------------------------------------------------------
// CsvTextTable —— 「CSV 目录里的同一份字节只读一次」的唯一入口。
//
// 问题（刀 55 前实测，TextCodecUtil::readFile 插桩）：
//   CSV 目录里有一批文件被**两个不同消费者**各读一遍 ——
//     · ConstantTable::loadCsvDirectory  读全部 *.csv（建常量名表）
//     · CsvLoader::loadCharaDirectory    再读 Chara*.csv（填角色变量 BASE/ABL/ern）
//     · loadGameBaseData / Item.csv / VariableSize.csv 各自又读一份
//   eraTW 148 个文件 / eraMegaten 2318 个文件被读 2 次（全库 2580 / 13100 次读盘）。
//
// 共用的**只有字节**，不是解析：常量名表取每行第 0/1 列当名字（跳过空行与 ';'），
// 角色 CSV 按 番号/名前/BASE 语义切行 —— 两套语义各自保留在两个消费者里。
// 因此本表只做一件事：路径 → 已解码文本，第一次请求时读盘，之后同一路径的
// 所有消费者拿到**同一份 QString**（Qt 隐式共享，不复制内容）。
//
// 生命周期：**不长期驻留**。装载链把表建在栈上/由 prep 持有，角色 CSV 读完
// 立即 clear()（CSV 总量 eraTW 363 KB / eraMegaten 8.2 MB —— 远小于 GB 级
// AST，但「能释放就释放」是纪律）。
//
// 为什么是「按需」而不是「预扫描全目录一次读光」：
//   · 预扫描要重写一份「哪些 CSV 需要缓存」的判据（ConstantTable 的 `_` 前缀
//     规则 + CsvLoader 的 Chara* 规则 + 同名跳过规则），等于把三个消费者的
//     判据复制到第四个地方 —— 违反「同一事实只允许一份副本」；
//   · 按需缓存下，读盘集合**恰好等于**消费者真正要读的集合，修前修后的
//     读盘计数差就是「被读第二次的那些文件」，没有副作用读。
// ---------------------------------------------------------------------------
class CsvTextTable {
public:
    // 取 path 的文本：首次 → 读盘（编码按文件嗅探，与各消费者原来的读法一致）；
    // 之后 → 返回缓存的同一份。读失败时返回空串并置 *ok=false（**同一路径只读
    // 一次**：失败结果同样缓存，两个消费者看到同一个「不可读」结论）。
    QString get(const QString& path, bool* ok = nullptr);

    // 已缓存的文件数 / 字符数（不含 QString 的 UTF-16 二倍开销）
    [[nodiscard]] int fileCount() const { return m_entries.size(); }
    [[nodiscard]] qint64 charCount() const { return m_chars; }
    // 实际读盘次数（== fileCount，含读失败者）
    [[nodiscard]] int readCount() const { return m_entries.size(); }
    // 缓存命中次数（未读盘就拿到文本的次数）—— 「两个消费者共用一份字节」的
    // 可观测证据：命中 = 有人省掉了一次读盘。
    [[nodiscard]] int hitCount() const { return m_hits; }

    // 释放全部文本（装载收尾调用；之后再 get() 会重新读盘）
    void clear();

private:
    struct Entry {
        QString text;
        bool ok = false;
    };
    QHash<QString, Entry> m_entries;
    qint64 m_chars = 0;
    int m_hits = 0;
};

#endif // CSV_TEXT_TABLE_H