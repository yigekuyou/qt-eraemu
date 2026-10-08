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
#include "resource_image_provider.h"
#include "key_map.h"
#include "GameData/ast/print_template.h"
#include "eraengine_log.h"   // eraTrace（文本框/输入裁决留痕）

#include <algorithm>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QFile>
#include <QTextStream>

namespace {
// 当前等待的输入是否为**字符串型**（INPUTS/SINPUTS/TONEINPUTS/ARGS 系）。
// 其余（INPUT/TINPUT/ONEINPUT/INPUTMOUSEKEY/系统菜单…）一律按整数型裁决。
bool inputExpectsString(const QString& kind) {
    const QString k = kind.toUpper();
    return k.contains(QLatin1String("INPUTS")) || k.contains(QLatin1String("ARGS"));
}
// EE INPUTANY（C# InputType.AnyValue）：整数与字符串**都**接受 ——
// 整数写 RESULT、字符串写 RESULTS（另一者保持原值）。因此本类型既不算
// 「只等字符串」，也不算「只等整数」：两种提交与两种按钮都放行。
bool inputAcceptsAny(const QString& kind) {
    return kind.toUpper() == QLatin1String("INPUTANY");
}
// 当前等待的输入是否为「任意键」型（WAIT/WAITANYKEY/FORCEWAIT/ANYKEY）。
// 对齐 C# IsWaitingEnterKey：这类等待点击控制台任意位置（或回车）即继续，
// 不要求按钮/文本框 —— 否则 DQPRINT 结尾的 WAIT 永远等不到输入。
bool inputExpectsAnyKey(const QString& kind) {
    const QString k = kind.toUpper();
    return k == QLatin1String("WAIT") || k == QLatin1String("WAITANYKEY")
        || k == QLatin1String("FORCEWAIT") || k == QLatin1String("ANYKEY")
        || k == QLatin1String("TWAIT");   // TWAIT = 限时任意键（C# InputType.EnterKey + Timelimit）
}
} // namespace

ConsoleBackend::ConsoleBackend(QObject* parent)
    : QAbstractListModel(parent)
{
    m_timer.setInterval(m_frameMs);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ConsoleBackend::tick);
    m_timer.start();
}

// ---------------------------------------------------------------------------
// QAbstractListModel
// ---------------------------------------------------------------------------

int ConsoleBackend::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_buffer.count() + (m_tailRow ? 1 : 0);
}

QHash<int, QByteArray> ConsoleBackend::roleNames() const {
    return {
        { BlocksRole,    "blocks" },
        { LineIndexRole, "lineIndex" },
        { SpanRowsRole,  "spanRows" },
    };
}

QVariant ConsoleBackend::data(const QModelIndex& index, int role) const {
    const int row = index.row();
    if (row < 0 || row >= rowCount() || index.column() != 0) return {};
    switch (role) {
    case BlocksRole:    return lineBlocks(row);
    case LineIndexRole: return row;
    case SpanRowsRole:  return lineSpanRows(displayLine(row));
    default:            return {};
    }
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
    m_htmlLines.append(html);
    printTemplate(PrintTemplateCompiler::evaluate(*PrintTemplateCompiler::compile(html),
        [](const ExpressionNode&) { return QString(); }));
}

QString ConsoleBackend::lineText(int lineNo) const {
    if (lineNo < 0 || lineNo >= m_buffer.count()) return QString();
    QString text;
    for (const ConsoleSegment& seg : m_buffer.at(lineNo).segments)
        for (const ConsoleSpan& span : seg.spans)
            text += span.text.isEmpty() ? span.altText : span.text;
    return text;
}

QString ConsoleBackend::displayLineText(int lineNo) const {
    if (lineNo < 0) return QString();
    int seen = -1;
    for (const ConsoleDisplayLine& line : m_buffer.lines()) {
        if (!line.isLogicalLine) continue;   // 折行续行不计入（对齐 LINECOUNT 口径）
        if (++seen != lineNo) continue;
        QString text;
        for (const ConsoleSegment& seg : line.segments)
            for (const ConsoleSpan& span : seg.spans)
                text += span.text.isEmpty() ? span.altText : span.text;
        return text;
    }
    return QString();
}

bool ConsoleBackend::hasEnabledButton() const {
    // BINPUT/BINPUTS 的「当前是否有可点击按钮」判定（EE v31fix）：
    //   只看**尾部连续的按钮块**（当前屏幕刚打印的按钮）——历史屏幕遗留的按钮
    //   不算「実行時点でボタン化されている」。世代（generation）仍用于过滤：
    //   只有与当前世代一致的按钮才可点击（对齐 lastButtonGeneration）。
    // 1) PRINTBUTTON 后尚未换行的当前行
    for (const ConsoleSegment& seg : m_sealed) {
        if (seg.isButton && seg.enabled && seg.generation == m_generation) return true;
    }
    // 2) 末尾连续的「含按钮」显示行
    const QList<ConsoleDisplayLine>& lines = m_buffer.lines();
    for (int i = lines.size() - 1; i >= 0; --i) {
        bool hasButton = false, hasCurrent = false;
        for (const ConsoleSegment& seg : lines.at(i).segments) {
            if (!seg.isButton) continue;
            hasButton = true;
            if (seg.enabled && seg.generation == m_generation) hasCurrent = true;
        }
        if (!hasButton) break;       // 尾部按钮块到此结束
        if (hasCurrent) return true;
    }
    return false;
}

QString ConsoleBackend::htmlPrintedStr(int lineNo) const {
    if (lineNo < 0 || lineNo >= m_htmlLines.size()) return QString();
    return m_htmlLines.at(m_htmlLines.size() - 1 - lineNo);   // 行号从最近一行起算
}

QString ConsoleBackend::htmlPopPrintingStr() {
    return m_htmlLines.isEmpty() ? QString() : m_htmlLines.takeLast();
}

bool ConsoleBackend::currentLineEmpty() const {
    return m_pendingParts.isEmpty() && m_sealed.isEmpty();
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
            span.yposRaw = part.y;
            span.imageButton = part.imageAlt;   // <img srcb=...> 按钮选中态替换图
            span.altText = QStringLiteral("<img src='%1'>").arg(part.text);
            // 资源固有像素尺寸（布局期算纵横比用）；取不到则为空。
            // 只在**宽度缺省**时才查（立絵常态是「只给 height」或「都不给」）：
            // 宽高都给了就不需要纵横比，省掉一次解码/拷贝（立絵每次刷新都打印）。
            int iw = 0, ih = 0;
            if (part.width <= 0 && ResourceImageProvider::intrinsicSize(part.text, iw, ih))
                span.imageIntrinsic = QSizeF(iw, ih);
            // C# ConsoleImagePart：
            //   height = raw_height==0 ? FontSize : FontSize*raw_height/100
            //   Width  = raw_width==0  ? 固有宽*height/固有高 : FontSize*raw_width/100
            // 只有**两个都没写**时才整张按固有像素排版（eraTW 标题 = 35 张 1041×16
            // 的条图，否则被压成 16×16）；只写了 height 的立絵
            // （eraTW `<img src=… height='{iFont_Hei_mag}'>`）必须保留 height、
            // 宽度由布局期按纵横比补 —— 此前「width<=0 || height<=0」把 height
            // 一起覆盖成固有像素，导致「画像サイズ 拡大/縮小」设置完全失效。
            if (part.width <= 0 && part.height <= 0 && !span.imageIntrinsic.isEmpty()) {
                span.imageSize = span.imageIntrinsic;
                span.imageSizeIsPixels = true;
            }
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
    // C# addDisplayLine：最后一行是「一時行」（REUSELASTLINE/PrintTemporaryLine）时，
    // 下一个显示行输出**替换**它（deleteLine(1)）。DQPRINT 逐字动画靠这个
    // 把整句动画在一行内完成；此前按普通输出追加，一句口上会产生几十行。
    if (m_lastLineTemporary) {
        m_lastLineTemporary = false;
        clearLines(1);
    }
    ConsoleDisplayLine line = buildLine();    // 先定型，避免 buildLine 里 const 的临时结果
    m_sealed.clear();
    m_pendingParts.clear();
    m_pendingOpen = false;
    m_tailDirty = false;

    QList<ConsoleDisplayLine> committed;
    if (m_wrapLines) {
        // wrapSegments 只负责切分逻辑行；绝对列/对齐仍由同一入口计算。
        committed = m_layout.wrapSegments(std::move(line.segments), line.align);
    } else {
        committed.append(line);
    }

    const int firstRow = m_buffer.count();
    // 尾行（未定型行）提交：摘除尾行 -> 追加进缓冲 -> 插入已提交行。
    // Qt 文档（QAbstractItemModel）：begin/end 必须括住数据变更。
    if (m_tailRow) {
        beginRemoveRows(QModelIndex(), firstRow, firstRow);
        m_tailRow = false;
        endRemoveRows();
        ++m_removedRows;
    }
    // 头部批量裁剪：先发信号再裁（appendLine 内部的 trim 不再触发，见 trimFront）
    const int total = firstRow + committed.size();
    const int trim = ConsoleBuffer::trimAmount(total, m_buffer.capacity());
    if (trim > 0) {
        beginRemoveRows(QModelIndex(), 0, trim - 1);
        m_buffer.trimFront(trim);
        endRemoveRows();
        m_removedRows += trim;
        pruneHeadCacheIfNeeded();
    }
    if (!committed.isEmpty()) {
        const int insertRow = m_buffer.count();
        beginInsertRows(QModelIndex(), insertRow, insertRow + committed.size() - 1);
        for (int i = 0; i < committed.size(); ++i) {
            m_layout.placeLine(committed[i], insertRow + i);
            trackSpanReach(committed[i]);
            m_buffer.appendLine(committed[i]);
        }
        endInsertRows();
        m_insertedRows += committed.size();
    }
    emit lineCountChanged();
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
    part.yposRaw = ypos;               // 字号百分比的纵向偏移（measurePart 折算成像素）
    // 与 `<img>` 路径同源：固有尺寸供布局期补宽度；两个宽高都缺省时按固有像素排版。
    int iw = 0, ih = 0;
    if (width <= 0 && ResourceImageProvider::intrinsicSize(resourceName, iw, ih))
        part.imageIntrinsic = QSizeF(iw, ih);
    if (width <= 0 && height <= 0 && !part.imageIntrinsic.isEmpty()) {
        part.imageSize = part.imageIntrinsic;
        part.imageSizeIsPixels = true;
    }
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

// OUTPUTLOG（C# Console.OutputLog）：显示行全文写文件
bool ConsoleBackend::outputLog(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "[console] OUTPUTLOG 无法写入：" << path;
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    for (const ConsoleDisplayLine& line : m_buffer.lines()) {
        for (const ConsoleSegment& seg : line.segments) {
            for (const ConsoleSpan& span : seg.spans) {
                out << (span.text.isEmpty() ? span.altText : span.text);
            }
        }
        out << '\n';
    }
    return true;
}

void ConsoleBackend::clearLines(int n) {
    if (m_pendingOpen) {
        // 当前未 flush 的行先丢掉（C# deleteLine 只作用于 displayLineList）
        m_pendingParts.clear();
        m_sealed.clear();
        m_pendingOpen = false;
        m_tailDirty = false;
    }
    const int oldCount = m_buffer.count();
    // CLEARLINE：从尾部删掉 n 个**逻辑行**（连同其折行续行）。
    // 先预告物理行数，才能把 beginRemoveRows 括在真正的删除外面。
    const int physical = m_buffer.physicalTailCount(n);
    if (physical > 0) {
        beginRemoveRows(QModelIndex(), oldCount - physical, oldCount - 1);
        m_buffer.removeLastLogicalLines(n);
        endRemoveRows();
        m_removedRows += physical;
        emit lineCountChanged();
        pruneTailCache();
    }
    if (m_tailRow) {
        beginRemoveRows(QModelIndex(), m_buffer.count(), m_buffer.count());
        m_tailRow = false;
        endRemoveRows();
        ++m_removedRows;
    }
    m_lastLineTemporary = false;   // 末尾的「一時行」已被删 -> 清标记
}

void ConsoleBackend::clearAll() {
    qDebug() << "[render] clearAll（清屏，行数" << m_buffer.count() << "）";
    beginResetModel();
    m_buffer.clear();
    m_pendingParts.clear();
    m_sealed.clear();
    m_pendingOpen = false;
    m_tailRow = false;
    m_tailDirty = false;
    m_dirty = false;
    m_lastLineTemporary = false;
    m_maxSpanReach = 1;
    m_publishedLineCount = 0;
    invalidateLineCache();        // 行全部消失 -> 按行缓存作废
    ++m_generation;
    endResetModel();
    emit generationChanged();
    emit cleared();
    emit lineCountChanged();
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

// SETBGCOLOR / RESETBGCOLOR（文字背景色，与前景色同样作用到后续 span）
void ConsoleBackend::setBgColor(const QColor& color) { m_style.bgColor = color; }

void ConsoleBackend::resetBgColor() { m_style.bgColor = QColor(); }

void ConsoleBackend::setFontStyle(bool bold, bool italic, bool underline, bool strike) {
    m_style.bold = bold;
    m_style.italic = italic;
    m_style.underline = underline;
    m_style.strike = strike;
}

// ---------------------------------------------------------------------------
// 发布（脏状态 -> 模型信号；每帧一次）
// ---------------------------------------------------------------------------

void ConsoleBackend::markDirty() {
    m_dirty = true;
    m_tailDirty = true;
}

void ConsoleBackend::flush() {
    m_dirty = false;
    const int n = m_buffer.count();
    if (m_pendingOpen && !m_tailRow) {
        // 未定型行首次入模：insert 1 行（print 打开行缓冲后，到帧才发布）
        beginInsertRows(QModelIndex(), n, n);
        m_tailRow = true;
        endInsertRows();
        ++m_insertedRows;
        m_tailDirty = false;    // 新行的数据即最新
    } else if (m_tailRow && m_tailDirty) {
        emit dataChanged(index(n), index(n));
        m_tailDirty = false;
    }
    if (m_publishedLineCount != n) {
        m_publishedLineCount = n;
        emit lineCountChanged();
    }
}

void ConsoleBackend::tick() {
    if (m_dirty) flush();
}

QString ConsoleBackend::perfReport() const {
    return QStringLiteral(
               "lineFlattenBuilds=%1\nlineFlattenMs=%2\n"
               "modelRows=%3 modelInserts=%4 modelRemoves=%5\n"
               "lineCache=%6 maxSpanReach=%7\n"
               "bufferLines=%8 logicalLines=%9 gridColumns=%10 gridRows=%11")
        .arg(m_lineCacheBuilds)
        .arg(m_lineCacheMs)
        .arg(rowCount())
        .arg(m_insertedRows)
        .arg(m_removedRows)
        .arg(m_lineCache.size())
        .arg(m_maxSpanReach)
        .arg(m_buffer.count())
        .arg(m_buffer.logicalLineCount())
        .arg(m_layout.gridColumns())
        .arg(m_layout.gridRows());
}

void ConsoleBackend::resetPerfCounters() {
    m_lineCacheBuilds = 0;
    m_lineCacheMs = 0;
    m_insertedRows = 0;
    m_removedRows = 0;
}

void ConsoleBackend::setFontSize(int px) {
    // 字号决定「区块长」像素（columnWidthPx = 字号/2），区块宽被烘焙进按行缓存
    // —— 变了就整表作废，否则改字号后画面（尤其图片尺寸档位）不更新。
    if (px > 0 && px != m_layout.fontSize()) {
        m_layout.setFontSize(px);
        invalidateLineCache();
        if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1));
    }
}

// 逻辑网格列/行数：脚本看到的列数（居中、换行都以它为准）。
// 只来自 emuera.config 推导（EraEngine::syncConsoleGrid），装载后固定；
// 窗口缩放是 QML 舞台的等比 transform，不影响行数。
void ConsoleBackend::setGridColumns(int columns) {
    if (columns <= 0 || columns == m_layout.gridColumns()) return;
    m_layout.setGridColumns(columns);
    invalidateLineCache();   // 列数变 -> 折行/对齐变 -> 缓存失效
    if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1));
}

void ConsoleBackend::setGridRows(int rows) {
    if (rows <= 0 || rows == m_layout.gridRows()) return;
    m_layout.setGridRows(rows);
    // 可见行数由 QML 视口决定（ListView 虚拟化），这里只更新布局
    invalidateLineCache();
    if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1));
}

void ConsoleBackend::setLineHeight(int px) {
    if (px <= 0 || px == m_lineHeight) return;
    m_lineHeight = px;
    m_layout.setLineHeight(px);     // 区块「高」也按行高算
    invalidateLineCache();          // 行高被烘焙进区块 height/offsetRows
    if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1));
}

void ConsoleBackend::setMaxLog(int lines) {
    const int cap = lines > 0 ? lines : 1;
    if (m_buffer.capacity() == cap) return;
    const int removing = ConsoleBuffer::trimAmount(m_buffer.count(), cap);
    if (removing > 0) {
        beginRemoveRows(QModelIndex(), 0, removing - 1);
        m_buffer.setCapacity(cap);
        endRemoveRows();
        m_removedRows += removing;
        emit lineCountChanged();
        pruneHeadCacheIfNeeded();
    } else {
        m_buffer.setCapacity(cap);
    }
}

// ---------------------------------------------------------------------------
// 输入桥接
// ---------------------------------------------------------------------------

void ConsoleBackend::notifyInputRequested(const QString& kind, const QVariant& defaultValue) {
    m_inputKind = kind;
    m_inputDefault = defaultValue;
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
    m_inputDefault = QVariant();
    m_waitingInput = false;
    emit waitingInputChanged();
}

// ---------------------------------------------------------------------------
// EE 文本框（GETTEXTBOX / SETTEXTBOX）
// ---------------------------------------------------------------------------

// SETTEXTBOX <字符串>：整体替换输入栏内容（C# MainWindow.ChangeTextBox：
// `richTextBox1.Text = str`），并通知 QML 输入栏同步显示。
void ConsoleBackend::setTextboxText(const QString& text) {
    if (m_textboxText == text) return;
    m_textboxText = text;
    qCDebug(eraTrace) << "[textbox] SETTEXTBOX ->" << text;
    emit textboxTextChanged(text);
}

// REUSELASTLINE（C# PrintTemporaryLine -> PrintSingleLine(str, temporary=true)）：
// 先定型未定型缓冲（PrintFlush），再把文本作为一条「一時行」加入。
// 空文本不产生任何输出（C# IsNullOrEmpty 早退）。
void ConsoleBackend::notifyReuseLastLine(const QString& text) {
    if (text.isEmpty()) return;
    // C# PrintSingleLine：先 PrintFlush（把未定型缓冲定型成普通行），
    // 再把文本作为一条「一時行」加入；newline() 里处理一時行替换
    if (m_pendingOpen || !m_pendingParts.isEmpty() || !m_sealed.isEmpty()) {
        newline();
    }
    print(text);
    newline();
    m_lastLineTemporary = true;    // 本行是「一時行」
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

// ---------------------------------------------------------------------------
// 按行摊平 + 模型数据
// ---------------------------------------------------------------------------

// 整表作废（布局度量变化：字号/行高/网格）。区块的 width/height/offsetRows
// 是按当前字号与行高**烘焙**进缓存值的 —— 度量一变，旧缓存全部失效，
// 否则「画像サイズ 拡大/縮小」之类改字号后画面不更新。
void ConsoleBackend::invalidateLineCache() {
    m_lineCache.clear();
    m_cacheFirstSerial = 0;
}

// 头部裁剪后，死行（serial < 现存最小 serial）的缓存条目按区间清理。
// 只在 firstSerial 变化时触发 —— 摊平缓存永不因「行号前移」整表作废。
void ConsoleBackend::pruneHeadCacheIfNeeded() const {
    const quint64 first = m_buffer.firstSerial();
    if (m_cacheFirstSerial == first) return;
    m_cacheFirstSerial = first;
    for (auto it = m_lineCache.begin(); it != m_lineCache.end();) {
        if (it.key() < first) it = m_lineCache.erase(it);
        else ++it;
    }
}

// 尾部删除（CLEARLINE）后，被删行的缓存条目成为孤儿 —— 同样按区间清理。
void ConsoleBackend::pruneTailCache() const {
    const quint64 last = m_buffer.lastSerial();
    for (auto it = m_lineCache.begin(); it != m_lineCache.end();) {
        if (it.key() > last) it = m_lineCache.erase(it);
        else ++it;
    }
}

// 该行最高的区块行距（文本恒 1；立絵按资源尺寸跨多行）。委托高度恒为一格，
// 此值供诊断/测试 —— 控制台的行进不因图片变高（网格叠加语义）。
int ConsoleBackend::lineSpanRows(const ConsoleDisplayLine& line) const {
    int rows = 1;
    for (const ConsoleSegment& seg : line.segments)
        for (const ConsoleSpan& part : seg.spans)
            rows = std::max(rows, part.rows);
    return rows;
}

// 摊平一行 = 把行内的段/区块转成 QML 可渲染的数据（网格坐标 + 点击信息）。
// 缓存键 = 行 serial（缓冲下标会被裁剪平移，serial 不会）；行提交后不可变，
// 故只有「已提交且非一時行」进缓存；未定型尾行每次重摊（内容随打印增长）。
QVariantList ConsoleBackend::lineBlocks(int row) const {
    if (row < 0 || row >= rowCount()) return {};
    const bool isTail = m_tailRow && row == m_buffer.count();
    const bool temporaryTail = m_lastLineTemporary && row == m_buffer.count() - 1;
    if (!isTail && !temporaryTail) {
        pruneHeadCacheIfNeeded();
        const auto it = m_lineCache.constFind(m_buffer.at(row).serial);
        if (it != m_lineCache.constEnd()) return it.value();
    }

    QElapsedTimer timer;
    timer.start();
    const QVariantList out = flattenLine(displayLine(row), row);
    ++m_lineCacheBuilds;
    m_lineCacheMs += timer.elapsed();
    if (!isTail && !temporaryTail)
        m_lineCache.insert(m_buffer.at(row).serial, out);
    return out;
}

QVariantList ConsoleBackend::flattenLine(const ConsoleDisplayLine& line, int row) const {
    // pointOffset 与 relCol 都是字符列单位；newline() 已经完成布局。
    const int offset = line.pointOffset;
    QVariantList out;
    int idx = 0;
    for (int si = 0; si < line.segments.size(); ++si) {
        const ConsoleSegment& seg = line.segments.at(si);
        for (const ConsoleSpan& part : seg.spans) {
            QVariantMap m = part.toVariantMap();
            m.insert("layer", part.layer());           // text / image / shape
            // 行内叠放顺序（委托内兄弟 z）：图压住先打印的字，后打印的字盖住图。
            // 跨行叠放（立絵/图层）由「后打印的行 = 后创建的委托」天然保序。
            m.insert("z", idx);
            m.insert("col", offset + part.relCol);     // 绝对列（单位 = 区块长）
            m.insert("row", row);                      // 行号（单位 = 区块高）
            const int spanCols = qMax(1, part.cols);
            // 区块占几行：文本/形状恒 1 行，**图片按资源尺寸跨多行**
            // （ConsoleLayout::measurePart 已按 `<img width/height>` 算好 rows）。
            const int spanRows = qMax(1, part.rows);
            // ypos：图片相对本行的纵向偏移（像素 -> 行数；负 = 往上盖）。
            // 图层（枠/特效）才会正好盖在立絵边缘。
            const double offsetRows = m_lineHeight > 0
                                          ? double(part.top) / double(m_lineHeight) : 0.0;
            m.insert("cols", spanCols);
            m.insert("rows", spanRows);
            m.insert("relCol", part.relCol);
            m.insert("relRow", 0);
            m.insert("offsetRows", offsetRows);
            m.insert("width", spanCols * m_layout.columnWidthPx());
            m.insert("height", spanRows * m_lineHeight);
            // 段的点击信息（同一个段的所有区块共享）
            m.insert("lineIndex", row);                // 点击回传 = 模型行号
            m.insert("segmentIndex", si);
            m.insert("isButton", seg.isButton);
            m.insert("isInteger", seg.isInteger);
            m.insert("clickable", seg.isButton && seg.enabled);
            if (seg.isButton)
                // 按钮值（运行期合法输入）：test_cli 分支选择的兜底候选
                m.insert("btnValue", seg.isInteger ? QVariant(seg.intValue)
                                                   : QVariant(seg.strValue));
            m.insert("tooltip", seg.tooltip);
            m.insert("generation", QVariant::fromValue<qulonglong>(seg.generation));
            out.append(m);
            ++idx;
        }
    }
    return out;
}

// 「当前屏幕」（尾部 gridRows 行）的扁平区块列表 —— test_cli 的 :geometry、
// --auto-branch 与 D-Bus /debug 诊断用；QML 运行时不读（视图走模型）。
QVariantList ConsoleBackend::screenBlocks() const {
    QVariantList out;
    const int n = rowCount();
    const int first = std::max(0, n - std::max(1, m_layout.gridRows()));
    for (int r = first; r < n; ++r) {
        for (const QVariant& b : lineBlocks(r)) out.append(b);
    }
    return out;
}

// 点击：segmentIndex 是**段**下标（C# ConsoleButtonString）
void ConsoleBackend::clickAt(int lineIndex, int segmentIndex) {
    const int n = rowCount();
    // 区块里的 lineIndex 是模型行号 —— 点击按行定位。
    if (lineIndex < 0 || lineIndex >= n) {
        return;
    }
    const ConsoleDisplayLine line = displayLine(lineIndex);
    // WAIT/ANYKEY 系：点击任意位置（含按钮文本）都视为「按任意键」继续。
    // 对齐 C# IsWaitingEnterKey —— EnterKey 等待时点击按钮也不会选中按钮，
    // 只会 PressEnterKey 结束等待（SelectedString 对 EnterKey 请求恒为 null）。
    if (m_waitingInput && inputExpectsAnyKey(m_inputKind)) {
        submitAnyKey();
        return;
    }
    if (segmentIndex < 0 || segmentIndex >= line.segments.size()) {
        return;
    }
    const ConsoleSegment& seg = line.segments.at(segmentIndex);
    if (!seg.isButton || !seg.enabled) {
        return;
    }
    // 只有当前世代的段可点击（对齐 C# selectingButton.Generation != lastButtonGeneration）
    if (seg.generation != m_generation) {
        qDebug() << "[input] 点击忽略（世代过期）行" << lineIndex << "段" << segmentIndex;
        return;
    }
    // 输入裁决：只有「正在等待输入」且「按钮类型与等待的输入类型一致」才响应。
    // 否则静默忽略——不推进世代（点击无效但按钮保持可重试），
    // 杜绝「点击杀死了按钮却没有产生任何效果」的不确定响应。
    // INPUTANY（AnyValue）：两种按钮都接受（整数按钮交 RESULT、字符串按钮交 RESULTS）。
    if (!m_waitingInput
        || (!inputAcceptsAny(m_inputKind)
            && inputExpectsString(m_inputKind) == seg.isInteger)) {
        qDebug() << "[input] 点击忽略（未等待输入或类型不符）kind" << m_inputKind
                 << "等待中" << m_waitingInput << "段为整数" << seg.isInteger;
        return;
    }
    qDebug() << "[input] 点击按钮 行" << lineIndex << "段" << segmentIndex
             << (seg.isInteger ? QStringLiteral("整数=") + QString::number(seg.intValue)
                               : QStringLiteral("字符串=") + seg.strValue);
    if (seg.isInteger) submitInput(seg.intValue);
    else submitInputString(seg.strValue);
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

// QML 键盘入口：把 Qt 键码/修饰符交给键码模型换算成 Emuera 的
// （keycode, keydata），再走与 submitMouseKey(type=3) 完全相同的通路
// （对齐 C# MainWindow.richTextBox1_KeyDown -> PressPrimitiveKey ->
//  InputMouseKey(3, keycode, keydata, 0, 0)）。
// 换算表在 GameView/key_map.h —— QML 侧不再出现任何数字键码。
void ConsoleBackend::submitQtKey(int qtKey, int qtModifiers) {
    const KeyMap::MouseKey k = KeyMap::toMouseKey(qtKey, qtModifiers);
    submitMouseKey(3, k.keyCode, k.keyData, 0, 0);
}

// WAIT/WAITANYKEY/FORCEWAIT/ANYKEY 系等待：点击任意处/回车即继续（C# PressEnterKey）。
// 这类请求没有按钮可点 —— 之前的实现里点击控制台毫无响应，游戏看起来「一直等待」。
void ConsoleBackend::submitAnyKey() {
    if (!m_waitingInput || !inputExpectsAnyKey(m_inputKind)) {
        return;
    }
    qDebug() << "[input] 任意键/点击继续 kind" << m_inputKind;
    sealSpan();
    ++m_generation;   // 与 submitInput 同源：提交后旧按钮失效
    emit generationChanged();
    emit inputSubmitted(0);   // RESULT = 0（WAIT 系不使用输入值）
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

// 空输入 / 非法输入的统一处理（对齐 C# EmueraConsole.doInputToEmueraProgram）：
//   IntValue: 空 && HasDefValue && !计时中 -> 交 DefIntValue；
//             空 && !HasDefValue               -> return false（**忽略**，继续等）；
//             非空但 Int64.TryParse 失败        -> return false（忽略）
//   StrValue: 空 && HasDefValue                -> 交 DefStrValue
// eraTW 外出列表（无参 `INPUT`）依赖「空回车什么都不发生」——否则 0 会命中
// `ELSEIF RESULT == MAIN_MAP` 变成「从外面回家」，也就是「没操作就自动返回」。
void ConsoleBackend::submitIntegerText(const QString& text) {
    if (!m_waitingInput || inputExpectsString(m_inputKind)) {
        qWarning() << "[input] 整数文本提交被拒：kind" << m_inputKind
                   << "等待中" << m_waitingInput;
        return;
    }
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        if (m_inputDefault.isValid()) {
            const qint64 v = m_inputDefault.toLongLong();
            qDebug() << "[input] 空输入 -> 交缺省值" << v << "kind" << m_inputKind;
            submitInput(v);
        } else {
            qDebug() << "[input] 空输入且无缺省值 -> 忽略（继续等待）kind" << m_inputKind;
        }
        return;
    }
    bool ok = false;
    const qint64 v = t.toLongLong(&ok);
    if (!ok) {
        qWarning() << "[input] 不是整数，忽略:" << t << "kind" << m_inputKind;
        return;   // C#：Int64.TryParse 失败 -> return false，不交付
    }
    submitInput(v);
}

void ConsoleBackend::submitStringText(const QString& text) {
    if (!m_waitingInput
        || !(inputExpectsString(m_inputKind) || inputAcceptsAny(m_inputKind))) {
        qWarning() << "[input] 字符串文本提交被拒：kind" << m_inputKind
                   << "等待中" << m_waitingInput;
        return;
    }
    if (text.isEmpty() && !m_inputDefault.isValid()) {
        // 无缺省值的字符串等待：C# 交的是空串本身（StrValue 分支不拦空串）
        submitInputString(text);
        return;
    }
    if (text.isEmpty() && m_inputDefault.isValid()) {
        qDebug() << "[input] 空输入 -> 交缺省字符串 kind" << m_inputKind;
        submitInputString(m_inputDefault.toString());
        return;
    }
    submitInputString(text);
}

void ConsoleBackend::submitInputString(const QString& value) {
    // 字符串提交在等待字符串型输入（INPUTS/SINPUTS/TONEINPUTS…）时有效；
    // EE INPUTANY（AnyValue）双通道，字符串提交同样有效（写 RESULTS）。
    if (!m_waitingInput
        || !(inputExpectsString(m_inputKind) || inputAcceptsAny(m_inputKind))) {
        qWarning() << "[input] 字符串提交被拒：kind" << m_inputKind
                   << "等待中" << m_waitingInput;
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

void ConsoleBackend::setFrameMs(int ms) {
    ms = std::max(1, ms);
    if (ms == m_frameMs) {
        return;
    }
    m_frameMs = ms;
    m_timer.setInterval(ms);
    emit frameMsChanged();
}

// 记录「区块最多往上探出窗口几行」：rows 本身，加上 ypos 的负偏移折算的行数。
// 只在行定型（measurePart 算完 cols/rows/top）之后调用。QML 拿它设
// ListView 的 cacheBuffer，跨行图在锚点行滚出视口后仍保持可见。
void ConsoleBackend::trackSpanReach(const ConsoleDisplayLine& line) {
    for (const ConsoleSegment& seg : line.segments) {
        for (const ConsoleSpan& part : seg.spans) {
            int reach = qMax(1, part.rows);
            if (m_lineHeight > 0 && part.top < 0) {
                const int offRows = int(-part.top) / qMax(1, m_lineHeight);
                reach += offRows;
            }
            m_maxSpanReach = std::max(m_maxSpanReach, reach);
        }
    }
}
