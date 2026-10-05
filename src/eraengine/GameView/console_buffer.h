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

    const ConsoleDisplayLine& at(int index) const { return m_lines.at(index); }
    const QList<ConsoleDisplayLine>& lines() const { return m_lines; }

    // 从**头部**丢弃的行数（容量裁剪）。丢弃会让所有绝对行号整体前移，
    // 按绝对行号为键的缓存（ConsoleBackend::m_lineCache）据此判定失效。
    int droppedFromFront() const { return m_dropped; }

    ConsoleDisplayLine& lastMutable() { return m_lines.last(); }

    void appendLine(const ConsoleDisplayLine& line) {
        m_lines.append(line);
        if (line.isLogicalLine) ++m_logicalCount;
        trim();
    }

    void replaceLast(const ConsoleDisplayLine& line) {
        if (m_lines.isEmpty()) {
            appendLine(line);
            return;
        }
        if (m_lines.last().isLogicalLine) --m_logicalCount;
        m_lines.last() = line;
        if (line.isLogicalLine) ++m_logicalCount;
    }

    // CLEARLINE：从尾部删掉 n 个**逻辑行**（连同其折行续行）
    void removeLastLogicalLines(int n) {
        int deleted = 0;
        while (deleted < n && !m_lines.isEmpty()) {
            const bool logical = m_lines.last().isLogicalLine;
            m_lines.removeLast();
            if (logical) {
                ++deleted;
                if (m_logicalCount > 0) --m_logicalCount;
            }
        }
    }

    void clear() {
        m_lines.clear();
        m_logicalCount = 0;
        // 全部清空 -> 绝对行号重新从 0 开始；丢弃计数一并归零，
        // 使「丢弃计数变化」不会在下一次 append 时误判为一次前移。
        m_dropped = 0;
    }

    // 逻辑行计数 = C# logicalLineCount = LINECOUNT
    int logicalLineCount() const { return m_logicalCount; }

private:
    void trim() {
        const int overflow = m_lines.size() - m_capacity;
        if (overflow > 0) {
            m_lines.erase(m_lines.begin(), m_lines.begin() + overflow);
            m_dropped += overflow;   // 头部丢弃 -> 绝对行号前移
        }
    }

    QList<ConsoleDisplayLine> m_lines;
    int m_capacity;
    int m_logicalCount = 0;
    int m_dropped = 0;
};

#endif // CONSOLE_BUFFER_H
