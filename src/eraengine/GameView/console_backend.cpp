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
#include "console_backend.h"
#include <algorithm>

ConsoleBackend::ConsoleBackend(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(m_frameMs);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ConsoleBackend::tick);
    m_timer.start();
}

int ConsoleBackend::lineCount() const {
    return m_buffer.count();
}

// ---------------------------------------------------------------------------
// 显示写入
// ---------------------------------------------------------------------------

void ConsoleBackend::print(const QString& text) {
    appendTextWithButtons(text);
    markDirty();
}

// 对齐 C# ButtonStringCreator：把 `[±?digits]` 变成可点击按钮（Emuera 里选择项就是这么做的）。
// 其余文本照旧；按钮只覆盖 `[n]` 这一小段 span。
void ConsoleBackend::appendTextWithButtons(const QString& text) {
    if (!m_pendingOpen) {
        ConsoleDisplayLine line;
        line.align = m_align;
        m_buffer.appendLine(line);
        m_pendingOpen = true;
    }
    const auto appendSpan = [this](const QString& t) {
        if (t.isEmpty()) return;
        ConsoleSpan span;
        span.kind = ConsoleSpanKind::Text;
        span.text = t;
        span.raw = t;
        span.style = m_style;
        m_buffer.appendSpanToLast(span);
    };

    int pos = 0;
    while (pos < text.size()) {
        const int open = text.indexOf(QLatin1Char('['), pos);
        if (open < 0) break;
        const int close = text.indexOf(QLatin1Char(']'), open + 1);
        if (close < 0) break;
        const QString inner = text.mid(open + 1, close - open - 1).trimmed();
        // 只认整数（含 +123 / -123）：与 C# 的整数按钮一致
        bool ok = false;
        QString digits = inner;
        if (digits.startsWith(QLatin1Char('+'))) digits = digits.mid(1);
        const qint64 value = digits.toLongLong(&ok);
        const bool isInt = ok && !digits.isEmpty()
                           && digits.size() <= 9
                           && (digits.at(0).isDigit() || digits.at(0) == QLatin1Char('-'));
        if (!isInt) {
            // 不是按钮：整段按文本推进，继续找下一个 '['
            appendSpan(text.mid(pos, close - pos + 1));
            pos = close + 1;
            continue;
        }
        appendSpan(text.mid(pos, open - pos));              // `[` 之前的文本
        const QString token = text.mid(open, close - open + 1);
        const int startSpan = m_buffer.lastMutable().spans.size();
        appendSpan(token);                                  // `[n]` 本身
        ConsoleButton button;
        button.startSpan = startSpan;
        button.spanCount = 1;
        button.isInteger = true;
        button.intValue = value;
        button.generation = m_generation;
        m_buffer.lastMutable().buttons.append(button);
        pos = close + 1;
    }
    if (pos < text.size()) {
        appendSpan(text.mid(pos));
    }
}

void ConsoleBackend::newline() {
    m_pendingOpen = false;
    markDirty();
}

void ConsoleBackend::printButton(const QString& text, qint64 value, const QString& tooltip) {
    if (!m_pendingOpen) {
        ConsoleDisplayLine line;
        line.align = m_align;
        m_buffer.appendLine(line);
        m_pendingOpen = true;
    }
    ConsoleSpan span;
    span.kind = ConsoleSpanKind::Text;
    span.text = text;
    span.raw = text;
    span.style = m_style;
    const int startSpan = m_buffer.lastMutable().spans.size();
    m_buffer.appendSpanToLast(span);

    ConsoleButton button;
    button.startSpan = startSpan;
    button.spanCount = 1;
    button.isInteger = true;
    button.intValue = value;
    button.tooltip = tooltip;
    button.generation = m_generation;
    m_buffer.lastMutable().buttons.append(button);
    markDirty();
}

void ConsoleBackend::printButtonStr(const QString& text, const QString& value, const QString& tooltip) {
    if (!m_pendingOpen) {
        ConsoleDisplayLine line;
        line.align = m_align;
        m_buffer.appendLine(line);
        m_pendingOpen = true;
    }
    ConsoleSpan span;
    span.kind = ConsoleSpanKind::Text;
    span.text = text;
    span.raw = text;
    span.style = m_style;
    const int startSpan = m_buffer.lastMutable().spans.size();
    m_buffer.appendSpanToLast(span);

    ConsoleButton button;
    button.startSpan = startSpan;
    button.spanCount = 1;
    button.isInteger = false;
    button.strValue = value;
    button.tooltip = tooltip;
    button.generation = m_generation;
    m_buffer.lastMutable().buttons.append(button);
    markDirty();
}

void ConsoleBackend::clearLines(int n) {
    m_buffer.removeLast(n);
    m_pendingOpen = false;
    clampScroll();
    markDirty();
}

void ConsoleBackend::clearAll() {
    m_buffer.clear();
    m_pendingOpen = false;
    m_scrollOffset = 0;
    ++m_generation;
    emit cleared();
    emit lineCountChanged();
    emit windowChanged();
}

void ConsoleBackend::setAlignment(ConsoleAlign align) {
    m_align = align;
}

void ConsoleBackend::setColor(const QColor& color) {
    m_style.color = color;
}

void ConsoleBackend::resetColor() {
    m_style.color = QColor();
}

void ConsoleBackend::setFontStyle(bool bold, bool italic, bool underline, bool strike) {
    m_style.bold = bold;
    m_style.italic = italic;
    m_style.underline = underline;
    m_style.strike = strike;
}

// ---------------------------------------------------------------------------
// 刷新（1Hz 兜底 + flush 点强制）
// ---------------------------------------------------------------------------

void ConsoleBackend::markDirty() {
    m_dirty = true;
}

void ConsoleBackend::flush() {
    m_dirty = false;
    clampScroll();
    emit lineCountChanged();
    emit windowChanged();
}

void ConsoleBackend::setMaxLog(int lines) {
    const int cap = lines > 0 ? lines : 1;
    if (m_buffer.capacity() == cap) return;
    m_buffer.setCapacity(cap);
    clampScroll();
    markDirty();
}

void ConsoleBackend::tick() {
    if (m_dirty) {
        flush();
    }
}

// ---------------------------------------------------------------------------
// 输入桥接
// ---------------------------------------------------------------------------

void ConsoleBackend::notifyInputRequested(const QString& kind) {
    m_inputKind = kind;
    if (!m_waitingInput) {
        m_waitingInput = true;
        emit waitingInputChanged();
    }
    flush();                       // 等待输入前必须刷新，玩家才能看到提示
    emit inputRequested(kind);
}

void ConsoleBackend::notifyInputDone() {
    if (m_waitingInput) {
        m_waitingInput = false;
        emit waitingInputChanged();
    }
    m_inputKind.clear();
}

// ---------------------------------------------------------------------------
// QML 接口
// ---------------------------------------------------------------------------

int ConsoleBackend::visibleLineCount() const {
    const int n = m_buffer.count();
    const int first = std::max(0, n - m_visibleCount - m_scrollOffset);
    return n - first;
}

QVariantMap ConsoleBackend::visibleLine(int index) const {
    const int n = m_buffer.count();
    const int first = std::max(0, n - m_visibleCount - m_scrollOffset);
    const int abs = first + index;
    if (index < 0 || abs >= n) {
        return QVariantMap();
    }
    QVariantMap m = m_buffer.at(abs).toVariantMap();
    m.insert("absIndex", abs);
    return m;
}

int ConsoleBackend::windowFirstLine() const {
    const int n = m_buffer.count();
    return std::max(0, n - m_visibleCount - m_scrollOffset);
}

QVariantList ConsoleBackend::visibleLines() const {
    QVariantList out;
    const int n = m_buffer.count();
    const int first = windowFirstLine();
    for (int abs = first; abs < n; ++abs) {
        QVariantMap m = m_buffer.at(abs).toVariantMap();
        m.insert("absIndex", abs);
        out.append(m);
    }
    return out;
}

void ConsoleBackend::clickAt(int visibleIndex, int buttonIndex) {
    const int n = m_buffer.count();
    const int first = std::max(0, n - m_visibleCount - m_scrollOffset);
    const int abs = first + visibleIndex;
    if (abs < 0 || abs >= n) {
        return;
    }
    const ConsoleDisplayLine& line = m_buffer.at(abs);
    if (buttonIndex < 0 || buttonIndex >= line.buttons.size()) {
        return;
    }
    const ConsoleButton& button = line.buttons.at(buttonIndex);
    if (!button.enabled) {
        return;
    }
    // 只有当前一批按钮可点击（对齐 C#：提交/清屏时 generation 推进，旧按钮失效）
    if (button.generation != m_generation) {
        return;
    }
    if (button.isInteger) {
        emit inputSubmitted(button.intValue);
    } else {
        emit inputSubmittedString(button.strValue);
    }
    // 提交后旧一批按钮全部失效
    ++m_generation;
}

void ConsoleBackend::scrollBy(int lines) {
    setScrollOffset(m_scrollOffset + lines);
}

void ConsoleBackend::scrollToBottom() {
    setScrollOffset(0);
}

void ConsoleBackend::submitInput(qint64 value) {
    emit inputSubmitted(value);
    ++m_generation;   // 提交后旧按钮失效（对齐 C# PressEnterKey -> forceUpdateGeneration）
}

void ConsoleBackend::submitInputString(const QString& value) {
    emit inputSubmittedString(value);
    ++m_generation;
}

// ---------------------------------------------------------------------------
// 属性
// ---------------------------------------------------------------------------

void ConsoleBackend::setVisibleCount(int count) {
    count = std::max(1, count);
    if (count == m_visibleCount) {
        return;
    }
    m_visibleCount = count;
    clampScroll();
    emit windowChanged();
}

void ConsoleBackend::setScrollOffset(int offset) {
    offset = std::max(0, offset);
    const int maxOffset = std::max(0, m_buffer.count() - m_visibleCount);
    offset = std::min(offset, maxOffset);
    if (offset == m_scrollOffset) {
        return;
    }
    m_scrollOffset = offset;
    emit windowChanged();
}

void ConsoleBackend::setFrameMs(int ms) {
    ms = std::max(50, ms);
    if (ms == m_frameMs) {
        return;
    }
    m_frameMs = ms;
    m_timer.setInterval(ms);
    emit frameMsChanged();
}

void ConsoleBackend::clampScroll() {
    const int maxOffset = std::max(0, m_buffer.count() - m_visibleCount);
    m_scrollOffset = std::min(m_scrollOffset, maxOffset);
}
