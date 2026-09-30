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
#include "button_string_creator.h"
#include "GameData/ast/print_template.h"

#include <algorithm>
#include <QRegularExpression>

namespace {
// 当前等待的输入是否为**字符串型**（INPUTS/SINPUTS/TONEINPUTS/ARGS 系）。
// 其余（INPUT/TINPUT/ONEINPUT/INPUTMOUSEKEY/系统菜单…）一律按整数型裁决。
bool inputExpectsString(const QString& kind) {
    const QString k = kind.toUpper();
    return k.contains(QLatin1String("INPUTS")) || k.contains(QLatin1String("ARGS"));
}
} // namespace

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

int ConsoleBackend::logicalLineCount() const {
    return m_buffer.logicalLineCount();
}

// ---------------------------------------------------------------------------
// 当前行缓冲（C# PrintStringBuffer）
// ---------------------------------------------------------------------------

void ConsoleBackend::ensureLineOpen() {
    m_pendingOpen = true;
}

// Keep each print operation as a separate span, even when styles match.
void ConsoleBackend::appendPart(const ConsoleSpan& part) {
    m_pendingParts.append(part);
}

// 「未定型」的 part 串 → 已定型的段（C# fromCssToButton）
//
// C# 把 pending 串**整体**交给 ButtonStringCreator 切分，再按字符长度把
// ConsoleStyledString 数组切开。这里同样做：切分点在字符边界上，
// 所以按 part 的字符长度累计即可定位。
void ConsoleBackend::flushPendingToSegments(QList<ConsoleSegment>& out) const {
    if (m_pendingParts.isEmpty()) return;

    QString text;
    for (const ConsoleSpan& p : m_pendingParts) {
        text += (p.kind == ConsoleSpanKind::Text) ? p.text : p.altText;
    }

    const QList<ButtonPrimitive> prims = ButtonStringCreator::split(text);
    if (prims.size() == 1) {
        ConsoleSegment seg;
        seg.spans = m_pendingParts;
        seg.isButton = prims.first().canSelect;
        seg.isInteger = seg.isButton;
        seg.intValue = prims.first().input;
        // C#：单个核时整行是一段；若一个核都没有则不可点击
        seg.generation = m_generation;
        out.append(seg);
        return;
    }

    // 多段：按字符长度回切 part 数组
    QList<ConsoleSpan> queue = m_pendingParts;
    int spanIndex = 0;
    int spanCharStart = 0;
    for (const ButtonPrimitive& prim : prims) {
        if (prim.str.isEmpty()) continue;
        const int endChar = spanCharStart + prim.str.size();   // 本段的字符右界
        ConsoleSegment seg;
        seg.isButton = prim.canSelect;
        seg.isInteger = prim.canSelect;
        seg.intValue = prim.input;
        seg.generation = m_generation;

        while (spanIndex < queue.size()) {
            ConsoleSpan part = queue.at(spanIndex);
            const QString pt = (part.kind == ConsoleSpanKind::Text) ? part.text : part.altText;
            const int partEnd = spanCharStart + pt.size();
            if (partEnd > endChar) {
                // 在 part 内部切开（C# ConsoleStyledString.DivideAt）
                const int used = endChar - spanCharStart;
                if (used > 0 && part.kind == ConsoleSpanKind::Text) {
                    ConsoleSpan head = part;
                    ConsoleSpan tail = part;
                    head.text = pt.left(used);
                    head.width = -1;
                    tail.text = pt.mid(used);
                    tail.width = -1;
                    queue.replace(spanIndex, tail);
                    seg.spans.append(head);
                    spanCharStart = endChar;
                }
                break;
            }
            seg.spans.append(part);
            spanCharStart = partEnd;
            ++spanIndex;
            if (spanCharStart >= endChar) break;
        }
        out.append(seg);
    }
    // 收尾：剩下的 part 归到最后一段
    if (spanIndex < queue.size() && !out.isEmpty()) {
        for (int i = spanIndex; i < queue.size(); ++i) out.last().spans.append(queue.at(i));
    }
}

void ConsoleBackend::sealSpan() {
    flushPendingToSegments(m_sealed);
    m_pendingParts.clear();
}

ConsoleDisplayLine ConsoleBackend::buildLine() const {
    ConsoleDisplayLine line;
    line.segments = m_sealed;
    flushPendingToSegments(line.segments);
    line.align = m_align;
    return line;
}

QString ConsoleBackend::currentLineText() const {
    ConsoleDisplayLine line = buildLine();
    return line.plainText();
}

// ---------------------------------------------------------------------------
// 显示写入
// ---------------------------------------------------------------------------

void ConsoleBackend::print(const QString& text) {
    if (text.isEmpty()) return;
    ensureLineOpen();
    ConsoleSpan part;
    part.kind = ConsoleSpanKind::Text;
    part.text = text;
    part.raw = text;
    part.style = m_style;
    appendPart(part);
    markDirty();
}

void ConsoleBackend::printHtml(const QString& html) {
    printTemplate(PrintTemplateCompiler::evaluate(*PrintTemplateCompiler::compile(html),
        [](const ExpressionNode&) { return QString(); }));
}

void ConsoleBackend::printTemplate(const PrintTemplate& output) {
    if (m_pendingOpen) newline();
    const ConsoleAlign savedAlign = m_align;
    const bool savedWrap = m_wrapLines;
    QList<ConsoleAlign> alignments;
    QList<bool> wrapping;
    ConsoleStyle style = m_style;
    QList<ConsoleStyle> styles;
    ConsoleSegment button;
    bool inButton = false;
    const auto sealButton = [&] {
        if (!inButton) return;
        button.generation = m_generation;
        m_sealed.append(button);
        button = {};
        inButton = false;
    };
    for (const auto& part : output.parts) {
        if (part.kind == PrintTemplatePart::Kind::Style) {
            styles.append(style);
            if (part.style.color.isValid()) { style.color = part.style.color; style.colorChanged = true; }
            if (part.style.buttonColor.isValid()) style.buttonColor = part.style.buttonColor;
            if (!part.style.fontName.isEmpty()) style.fontName = part.style.fontName;
            style.bold |= part.style.bold; style.italic |= part.style.italic;
            style.underline |= part.style.underline; style.strike |= part.style.strike;
            continue;
        }
        if (part.kind == PrintTemplatePart::Kind::EndStyle) {
            for (int i = 0; i < part.x && !styles.isEmpty(); ++i) style = styles.takeLast();
            continue;
        }
        if (part.kind == PrintTemplatePart::Kind::NoWrap) {
            wrapping.append(m_wrapLines); m_wrapLines = false; continue;
        }
        if (part.kind == PrintTemplatePart::Kind::EndNoWrap) {
            if (m_pendingOpen) { sealButton(); newline(); }
            if (!wrapping.isEmpty()) m_wrapLines = wrapping.takeLast();
            continue;
        }
        if (part.kind == PrintTemplatePart::Kind::EndAlignment) {
            if (m_pendingOpen) { sealButton(); newline(); }
            if (!alignments.isEmpty()) m_align = alignments.takeLast();
            continue;
        }
        if (part.kind == PrintTemplatePart::Kind::Alignment) {
            if (m_pendingOpen) { sealButton(); newline(); }
            alignments.append(m_align);
            const QString align = part.text.toLower();
            if (align == "center") m_align = ConsoleAlign::Center;
            else if (align == "right") m_align = ConsoleAlign::Right;
            else if (align == "left") m_align = ConsoleAlign::Left;
            continue;
        }
        if (part.kind == PrintTemplatePart::Kind::Button) {
            sealButton();
            flushPendingToSegments(m_sealed); m_pendingParts.clear();
            button.isButton = true;
            button.intValue = part.buttonValue.toLongLong(&button.isInteger);
            button.strValue = part.buttonValue; button.tooltip = part.tooltip;
            inButton = true;
            continue;
        }
        if (part.kind == PrintTemplatePart::Kind::EndButton) { sealButton(); continue; }
        if (part.kind == PrintTemplatePart::Kind::Break) {
            const auto continued = button; const bool wasButton = inButton;
            sealButton(); newline();
            if (wasButton) { button = continued; button.spans.clear(); inButton = true; }
            continue;
        }
        ConsoleSpan span; span.text = part.text; span.raw = part.text;
        span.style = style;
        if (part.style.color.isValid()) { span.style.color = part.style.color; span.style.colorChanged = true; }
        if (part.style.buttonColor.isValid()) span.style.buttonColor = part.style.buttonColor;
        if (!part.style.fontName.isEmpty()) span.style.fontName = part.style.fontName;
        span.style.bold |= part.style.bold; span.style.italic |= part.style.italic;
        span.style.underline |= part.style.underline; span.style.strike |= part.style.strike;
        if (part.kind == PrintTemplatePart::Kind::Image) {
            span.kind = ConsoleSpanKind::Image; span.imageSize = QSizeF(part.width, part.height);
            span.top = part.y; span.altText = QStringLiteral("<img src='%1'>").arg(part.text);
        } else if (part.kind == PrintTemplatePart::Kind::Shape) {
            span.kind = ConsoleSpanKind::Shape; span.shapeType = part.shapeType;
            span.shapeParams = part.shapeParams; span.altText = QStringLiteral("<shape type='%1'>").arg(part.shapeType);
        }
        ensureLineOpen();
        if (inButton) button.spans.append(span); else appendPart(span);
        markDirty();
    }
    sealButton();
    if (m_pendingOpen) newline();
    m_align = savedAlign;
    m_wrapLines = savedWrap;
}

void ConsoleBackend::printPlain(const QString& text) {
    if (text.isEmpty()) return;
    ensureLineOpen();
    // 先定型当前串，再把这一整段作为「不可点击」的段（C# AppendPlainText）
    flushPendingToSegments(m_sealed);
    m_pendingParts.clear();
    ConsoleSegment seg;
    ConsoleSpan part;
    part.kind = ConsoleSpanKind::Text;
    part.text = text;
    part.raw = text;
    part.style = m_style;
    seg.spans.append(part);
    seg.generation = m_generation;
    m_sealed.append(seg);
    markDirty();
}

void ConsoleBackend::newline() {
    ConsoleDisplayLine line = buildLine();
    // 先定型，避免 buildLine 里 const 的临时结果
    m_sealed.clear();
    m_pendingParts.clear();
    m_pendingOpen = false;

    if (m_wrapLines) {
        const QList<ConsoleDisplayLine> lines = m_layout.wrapSegments(std::move(line.segments),
                                                                     line.align);
        int row = m_buffer.count();
        for (ConsoleDisplayLine l : lines) {
            // wrapSegments 只负责切分逻辑行；绝对列/对齐仍由同一入口计算。
            m_layout.placeLine(l, row++);
            m_buffer.appendLine(l);
        }
    } else {
        // pointOffset/part.col 的单位是字符列，不是像素。统一走 placeLine，
        // 由 maxCols() 和 widthUnits() 计算对齐，避免 760px 被误当成 760 列。
        m_layout.placeLine(line, m_buffer.count());
        m_buffer.appendLine(line);
    }
    markDirty();
}

void ConsoleBackend::printButton(const QString& text, qint64 value, const QString& tooltip) {
    if (text.isEmpty()) return;
    ensureLineOpen();
    flushPendingToSegments(m_sealed);
    m_pendingParts.clear();

    ConsoleSegment seg;
    ConsoleSpan part;
    part.kind = ConsoleSpanKind::Text;
    part.text = text;
    part.raw = text;
    part.style = m_style;
    seg.spans.append(part);
    seg.isButton = true;
    seg.isInteger = true;
    seg.intValue = value;
    seg.tooltip = tooltip;
    seg.generation = m_generation;
    m_sealed.append(seg);
    markDirty();
}

void ConsoleBackend::printButtonStr(const QString& text, const QString& value, const QString& tooltip) {
    if (text.isEmpty()) return;
    ensureLineOpen();
    flushPendingToSegments(m_sealed);
    m_pendingParts.clear();

    ConsoleSegment seg;
    ConsoleSpan part;
    part.kind = ConsoleSpanKind::Text;
    part.text = text;
    part.raw = text;
    part.style = m_style;
    seg.spans.append(part);
    seg.isButton = true;
    seg.isInteger = false;
    seg.strValue = value;
    seg.tooltip = tooltip;
    seg.generation = m_generation;
    m_sealed.append(seg);
    markDirty();
}

void ConsoleBackend::printImage(const QString& resourceName, int width, int height, int ypos) {
    if (resourceName.isEmpty()) return;
    ensureLineOpen();
    ConsoleSpan part;
    part.kind = ConsoleSpanKind::Image;
    part.text = resourceName;          // QML 侧拼 image://emuera/<name>
    part.altText = QStringLiteral("<img src='%1'>").arg(resourceName);   // C# AltText 回退
    part.imageSize = QSizeF(width, height);
    part.top = ypos;
    part.style = m_style;
    appendPart(part);
    markDirty();
}

void ConsoleBackend::printShape(const QString& type, const QList<int>& params) {
    ensureLineOpen();
    ConsoleSpan part;
    part.kind = ConsoleSpanKind::Shape;
    part.shapeType = type;
    part.text = type;
    part.shapeParams = params;
    part.altText = QStringLiteral("<shape type='%1'>").arg(type);
    part.style = m_style;
    appendPart(part);
    markDirty();
}

void ConsoleBackend::clearLines(int n) {
    if (m_pendingOpen) {
        // 当前未 flush 的行先丢掉（C# deleteLine 只作用于 displayLineList）
        m_pendingParts.clear();
        m_sealed.clear();
        m_pendingOpen = false;
    }
    m_buffer.removeLastLogicalLines(n);
    clampScroll();
    markDirty();
}

void ConsoleBackend::clearAll() {
    qDebug() << "[render] clearAll（清屏，行数" << m_buffer.count() << "）";
    m_buffer.clear();
    m_pendingParts.clear();
    m_sealed.clear();
    m_pendingOpen = false;
    m_scrollOffset = 0;
    ++m_generation;
    emit generationChanged();
    emit cleared();
    emit lineCountChanged();
    emit windowChanged();
}

void ConsoleBackend::setAlignment(ConsoleAlign align) {
    m_align = align;
}

void ConsoleBackend::setColor(const QColor& color) {
    m_style.color = color;
    m_style.colorChanged = true;       // C# StringStyle.ColorChanged
}

void ConsoleBackend::resetColor() {
    m_style.color = QColor();
    m_style.colorChanged = false;
}

void ConsoleBackend::setFontStyle(bool bold, bool italic, bool underline, bool strike) {
    m_style.bold = bold;
    m_style.italic = italic;
    m_style.underline = underline;
    m_style.strike = strike;
}

void ConsoleBackend::markDirty() {
    m_dirty = true;
}

void ConsoleBackend::flush() {
    if (!m_dirty) return;
    m_dirty = false;
    emit lineCountChanged();
    emit windowChanged();
}

void ConsoleBackend::setFontSize(int px) {
    m_layout.setFontSize(px);
    emit windowChanged();
}

void ConsoleBackend::setWindowWidth(int px) {
    m_layout.setWindowWidth(px);
    emit windowChanged();
}

void ConsoleBackend::setLineHeight(int px) {
    if (px <= 0 || px == m_lineHeight) return;
    m_lineHeight = px;
    m_layout.setLineHeight(px);     // 区块「高」也按行高算
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
    m_waitingInput = true;
    flush();
    emit inputRequested(kind);
    emit waitingInputChanged();
}

void ConsoleBackend::notifyInputDone() {
    if (!m_waitingInput) return;
    sealSpan();
    ++m_generation;
    emit generationChanged();
    m_inputKind.clear();
    m_waitingInput = false;
    emit waitingInputChanged();
}

// ---------------------------------------------------------------------------
// 窗口 / 滚动
// ---------------------------------------------------------------------------

ConsoleDisplayLine ConsoleBackend::displayLine(int index) const {
    if (index < m_buffer.count()) return m_buffer.at(index);
    ConsoleDisplayLine pending = buildLine();
    m_layout.placeLine(pending, index);
    return pending;
}

int ConsoleBackend::visibleLineCount() const {
    const int n = displayLineCount();
    const int first = std::max(0, n - m_visibleCount - m_scrollOffset);
    return n - first;
}

QVariantMap ConsoleBackend::visibleLine(int index) const {
    const int n = displayLineCount();
    const int first = std::max(0, n - m_visibleCount - m_scrollOffset);
    const int abs = first + index;
    if (index < 0 || abs >= n) {
        return QVariantMap();
    }
    QVariantMap m = displayLine(abs).toVariantMap();
    m.insert("absIndex", abs);
    return m;
}

int ConsoleBackend::windowFirstLine() const {
    const int n = displayLineCount();
    return std::max(0, n - m_visibleCount - m_scrollOffset);
}

QVariantList ConsoleBackend::visibleLines() const {
    QVariantList out;
    const int n = displayLineCount();
    const int first = windowFirstLine();
    for (int abs = first; abs < n; ++abs) {
        QVariantMap m = displayLine(abs).toVariantMap();
        m.insert("absIndex", abs);
        out.append(m);
    }
    return out;
}

// 点击：segmentIndex 是**段**下标（C# ConsoleButtonString）
// 分层模型：把可见窗口的「最小单位区块」摊平，并把坐标算好
//
//   y = 行序号（窗口内）× 行高
//   x = 行对齐平移 + 段内偏移
//
// text / image 两层各自挑出自己关心的 kind，然后按 x/y 摆放即可。
QVariantList ConsoleBackend::visibleBlocks() const {
    QVariantList out;
    const int n = displayLineCount();
    const int first = windowFirstLine();
    for (int abs = first; abs < n; ++abs) {
        const ConsoleDisplayLine line = displayLine(abs);
        const int lineIndex = abs - first;
        // pointOffset 与 relCol 都是字符列单位；newline() 已经完成布局。
        const int offset = line.pointOffset;
        for (int si = 0; si < line.segments.size(); ++si) {
            const ConsoleSegment& seg = line.segments.at(si);
            for (const ConsoleSpan& part : seg.spans) {
                QVariantMap m = part.toVariantMap();
                m.insert("layer", part.layer());           // text / image / shape
                m.insert("col", offset + part.relCol);     // 绝对列（单位 = 区块长）
                m.insert("row", lineIndex);                // 绝对行（单位 = 区块高）
                m.insert("cols", qMax(1, part.cols));
                m.insert("rows", 1);
                m.insert("relCol", part.relCol);
                m.insert("relRow", 0);
                m.insert("width", qMax(1, part.cols) * m_layout.columnWidthPx());
                m.insert("height", m_lineHeight);
                // 段的点击信息（同一个段的所有区块共享）
                m.insert("lineIndex", lineIndex);
                m.insert("segmentIndex", si);
                m.insert("isButton", seg.isButton);
                m.insert("isInteger", seg.isInteger);
                m.insert("clickable", seg.isButton && seg.enabled);
                m.insert("tooltip", seg.tooltip);
                m.insert("generation", QVariant::fromValue<qulonglong>(seg.generation));
                out.append(m);
            }
        }
    }
    return out;
}

// 三个层各自的区块列表：从摊平的可见区块里按 kind 挑
QVariantList ConsoleBackend::layerBlocks(const QString& layer) const {
    QVariantList out;
    const QVariantList all = visibleBlocks();
    for (const QVariant& v : all) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("layer")).toString() == layer) out.append(m);
    }
    return out;
}

// root 层的「内容高度」= 已排版可见行数 × 行高
int ConsoleBackend::contentHeight() const {
    return (displayLineCount() - windowFirstLine()) * m_lineHeight;
}

void ConsoleBackend::clickAt(int visibleIndex, int segmentIndex) {
    const int n = displayLineCount();
    const int first = std::max(0, n - m_visibleCount - m_scrollOffset);
    const int abs = first + visibleIndex;
    if (abs < 0 || abs >= n) {
        return;
    }
    const ConsoleDisplayLine line = displayLine(abs);
    if (segmentIndex < 0 || segmentIndex >= line.segments.size()) {
        return;
    }
    const ConsoleSegment& seg = line.segments.at(segmentIndex);
    if (!seg.isButton || !seg.enabled) {
        return;
    }
    // 只有当前世代的段可点击（对齐 C# selectingButton.Generation != lastButtonGeneration）
    if (seg.generation != m_generation) {
        qDebug() << "[input] 点击忽略（世代过期）行" << visibleIndex << "段" << segmentIndex;
        return;
    }
    // 输入裁决：只有「正在等待输入」且「按钮类型与等待的输入类型一致」才响应。
    // 否则静默忽略——不推进世代（点击无效但按钮保持可重试），
    // 杜绝「点击杀死了按钮却没有产生任何效果」的不确定响应。
    if (!m_waitingInput || inputExpectsString(m_inputKind) == seg.isInteger) {
        qDebug() << "[input] 点击忽略（未等待输入或类型不符）kind" << m_inputKind
                 << "等待中" << m_waitingInput << "段为整数" << seg.isInteger;
        return;
    }
    qDebug() << "[input] 点击按钮 行" << visibleIndex << "段" << segmentIndex
             << (seg.isInteger ? QStringLiteral("整数=") + QString::number(seg.intValue)
                               : QStringLiteral("字符串=") + seg.strValue);
    if (seg.isInteger) submitInput(seg.intValue);
    else submitInputString(seg.strValue);
}

void ConsoleBackend::scrollBy(int lines) {
    setScrollOffset(m_scrollOffset + lines);
}

void ConsoleBackend::scrollToBottom() {
    setScrollOffset(0);
}

void ConsoleBackend::submitMouseKey(int type, int r1, int r2, int r3, int r4) {
    if (!m_waitingInput || m_inputKind != QLatin1String("INPUTMOUSEKEY")) {
        qWarning() << "[input] 鼠标键提交被拒：kind" << m_inputKind << "等待中" << m_waitingInput;
        return;
    }
    qDebug() << "[input] 提交鼠标键 type" << type << r1 << r2 << r3 << r4;
    sealSpan();
    ++m_generation;
    emit generationChanged();
    emit mouseKeySubmitted(type, r1, r2, r3, r4);
}

void ConsoleBackend::submitInput(qint64 value) {
    // 类型分支限制：整数提交只在等待整数型输入（INPUT/TINPUT/ONEINPUT…）时有效
    if (!m_waitingInput || inputExpectsString(m_inputKind)) {
        qWarning() << "[input] 整数提交被拒：kind" << m_inputKind << "等待中" << m_waitingInput;
        return;
    }
    qDebug() << "[input] 提交整数" << value << "kind" << m_inputKind;
    sealSpan();
    ++m_generation;   // 提交后旧按钮失效（C# forceUpdateGeneration）
    emit generationChanged();
    emit inputSubmitted(value);
}

void ConsoleBackend::submitInputString(const QString& value) {
    // 字符串提交只在等待字符串型输入（INPUTS/SINPUTS/TONEINPUTS…）时有效
    if (!m_waitingInput || !inputExpectsString(m_inputKind)) {
        qWarning() << "[input] 字符串提交被拒：kind" << m_inputKind << "等待中" << m_waitingInput;
        return;
    }
    qDebug() << "[input] 提交字符串" << value << "kind" << m_inputKind;
    sealSpan();
    ++m_generation;
    emit generationChanged();
    emit inputSubmittedString(value);
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
    const int maxOffset = std::max(0, displayLineCount() - m_visibleCount);
    offset = std::min(offset, maxOffset);
    if (offset == m_scrollOffset) {
        return;
    }
    m_scrollOffset = offset;
    emit windowChanged();
}

void ConsoleBackend::setFrameMs(int ms) {
    ms = std::max(1, ms);
    if (ms == m_frameMs) {
        return;
    }
    m_frameMs = ms;
    m_timer.setInterval(ms);
    emit frameMsChanged();
}

void ConsoleBackend::clampScroll() {
    const int maxOffset = std::max(0, displayLineCount() - m_visibleCount);
    m_scrollOffset = std::min(m_scrollOffset, maxOffset);
}
