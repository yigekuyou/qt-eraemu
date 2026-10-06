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
#ifndef CONSOLE_BUFFER_H
#define CONSOLE_BUFFER_H

#include <QList>
#include "console_types.h"

// ---------------------------------------------------------------------------
// ConsoleBuffer —— 有界的显示行缓冲
//
// 对齐 C# EmueraConsole.displayLineList：
//   * appendLine：追加一行（超过容量丢最旧，C# 是 displayLineList.RemoveAt(0)）；
//   * removeLastLogicalLines(n)：CLEARLINE 的实体（C# deleteLine）——
//     从尾部弹，**只数 IsLogicalLine 的行**，折行产生的续行被删掉但不计数；
//   * lineCount()：逻辑行总数（C# logicalLineCount，也就是 LINECOUNT）。
//
// 注意：C# 的 MaxLog 裁剪**不会**回退 logicalLineCount，这里同样保持
// 「逻辑行计数只增不减（除 deleteLine）」的语义。
//
// 身份与裁剪（配合「全量 append-only 行模型」）：
//   * 每行在 appendLine 时领一个单调递增的 serial（ConsoleDisplayLine.serial），
//     它是按行摊平缓存的键 —— 缓冲下标会被头部裁剪/CLEARLINE 平移，serial 不会；
//   * 裁剪是**批量化**的：攒够 kTrimBatch 行溢出才一次性 erase 头部，
//     摊销后追加为 O(1)（此前每追加一行都把整段缓冲前移一格）；
//     代价是 lineCount 最多超容量 kTrimBatch-1 行（历史多留几行，无语义影响）；
//   * firstSerial()/lastSerial() 供缓存按 serial 区间清理孤儿条目。
// ---------------------------------------------------------------------------
class ConsoleBuffer {
public:
    explicit ConsoleBuffer(int capacity = 5000) : m_capacity(capacity > 0 ? capacity : 1) {}

    void setCapacity(int capacity) {
        m_capacity = capacity > 0 ? capacity : 1;
        trim();
    }
    int  capacity() const { return m_capacity; }
    int  count() const { return m_lines.size(); }
    bool isEmpty() const { return m_lines.isEmpty(); }

    // 批量裁剪的批次（public：ConsoleBackend 预测头部裁剪行数，先发信号再裁）
    static constexpr int kTrimBatch = 32;

    // 预测：projectedCount 行会在现容量下触发多大的头部裁剪（0 = 不裁）。
    // 小容量（<= kTrimBatch）溢出即裁（对齐 C# 逐行 RemoveAt(0) 语义），
    // 大容量攒 kTrimBatch 行摊销 O(1)。
    static int trimAmount(int projectedCount, int capacity) {
        const int cap = capacity > 0 ? capacity : 1;
        const int overflow = projectedCount - cap;
        if (overflow <= 0) return 0;
        return (cap <= kTrimBatch || overflow >= kTrimBatch) ? overflow : 0;
    }

    // 立即从头部裁掉 n 行（模型先发 beginRemoveRows(0, n-1) 再调用本函数）
    void trimFront(int n) {
        if (n <= 0) return;
        m_lines.erase(m_lines.begin(), m_lines.begin() + qMin(n, m_lines.size()));
    }

    const ConsoleDisplayLine& at(int index) const { return m_lines.at(index); }
    const QList<ConsoleDisplayLine>& lines() const { return m_lines; }

    ConsoleDisplayLine& lastMutable() { return m_lines.last(); }

    void appendLine(const ConsoleDisplayLine& line) {
        ConsoleDisplayLine committed = line;
        committed.serial = ++m_nextSerial;
        if (committed.isLogicalLine) ++m_logicalCount;
        m_lines.append(committed);
        trim();
    }

    void replaceLast(const ConsoleDisplayLine& line) {
        if (m_lines.isEmpty()) {
            appendLine(line);
            return;
        }
        if (m_lines.last().isLogicalLine) --m_logicalCount;
        m_lines.last() = line;
        m_lines.last().serial = ++m_nextSerial;
        if (line.isLogicalLine) ++m_logicalCount;
    }

    // 返回本次实际删除的**物理**行数（含折行续行）
    int removeLastLogicalLines(int n) {
        int deleted = 0;
        while (deleted < n && !m_lines.isEmpty()) {
            const bool logical = m_lines.last().isLogicalLine;
            m_lines.removeLast();
            ++deleted;
            if (logical && m_logicalCount > 0) --m_logicalCount;
        }
        return deleted;
    }

    // 预告：从尾部删 n 个逻辑行会动掉多少**物理**行（不删）。
    // 模型发 beginRemoveRows 前需要先知道区间。
    int physicalTailCount(int n) const {
        int logical = 0, physical = 0;
        for (int i = m_lines.size() - 1; i >= 0 && logical < n; --i, ++physical) {
            if (m_lines.at(i).isLogicalLine) ++logical;
        }
        return physical;
    }

    void clear() {
        m_lines.clear();
        m_logicalCount = 0;
        m_nextSerial = 0;   // 绝对行号重新从 0 开始；缓存由 clearAll 整表作废
    }

    // 逻辑行计数 = C# logicalLineCount = LINECOUNT
    int logicalLineCount() const { return m_logicalCount; }

    // 现存行的 serial 区间（缓存按区间清理孤儿条目）。空缓冲时返回
    // [m_nextSerial+1, m_nextSerial] —— 空区间，任何缓存键都应被清掉。
    quint64 firstSerial() const { return m_lines.isEmpty() ? m_nextSerial + 1 : m_lines.first().serial; }
    quint64 lastSerial() const { return m_lines.isEmpty() ? m_nextSerial : m_lines.last().serial; }

private:
    // 批量裁剪：攒够批次才动一次头部（摊销 O(1)；小容量按容量）
    void trim() {
        const int n = trimAmount(m_lines.size(), m_capacity);
        if (n > 0) trimFront(n);
    }

    QList<ConsoleDisplayLine> m_lines;
    int m_capacity;
    int m_logicalCount = 0;
    quint64 m_nextSerial = 0;
};

#endif // CONSOLE_BUFFER_H
