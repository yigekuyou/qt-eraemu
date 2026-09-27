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
// ConsoleBuffer —— 有界的显示行缓冲（环形语义）
//
// 历史不可能全显示，也不需要全保留：超过容量就丢弃最旧的行。
// 滚动只是在这个有界窗口内移动视图（见 ConsoleBackend）。
// ---------------------------------------------------------------------------
class ConsoleBuffer {
public:
    explicit ConsoleBuffer(int capacity = 2000) : m_capacity(capacity > 0 ? capacity : 1) {}

    void setCapacity(int capacity) {
        m_capacity = capacity > 0 ? capacity : 1;
        trim();
    }
    int capacity() const { return m_capacity; }
    int count() const { return m_lines.size(); }
    bool isEmpty() const { return m_lines.isEmpty(); }

    const ConsoleDisplayLine& at(int index) const { return m_lines.at(index); }
    const QList<ConsoleDisplayLine>& lines() const { return m_lines; }

    // 追加 span 到当前（最后一行）
    void appendSpanToLast(const ConsoleSpan& span) {
        if (m_lines.isEmpty()) {
            m_lines.append(ConsoleDisplayLine());
        }
        m_lines.last().spans.append(span);
    }

    ConsoleDisplayLine& lastMutable() { return m_lines.last(); }

    void appendLine(const ConsoleDisplayLine& line) {
        m_lines.append(line);
        trim();
    }

    void replaceLast(const ConsoleDisplayLine& line) {
        if (m_lines.isEmpty()) {
            m_lines.append(line);
        } else {
            m_lines.last() = line;
        }
    }

    // CLEARLINE：删除末尾 n 行
    void removeLast(int n) {
        if (n <= 0) return;
        if (n >= m_lines.size()) m_lines.clear();
        else m_lines.erase(m_lines.end() - n, m_lines.end());
    }

    void clear() { m_lines.clear(); }

private:
    void trim() {
        const int overflow = m_lines.size() - m_capacity;
        if (overflow > 0) {
            m_lines.erase(m_lines.begin(), m_lines.begin() + overflow);
        }
    }

    QList<ConsoleDisplayLine> m_lines;
    int m_capacity;
};

#endif // CONSOLE_BUFFER_H
