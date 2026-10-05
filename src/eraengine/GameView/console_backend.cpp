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
#include "GameData/ast/print_template.h"

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
// 当前等待的输入是否为「任意键」型（WAIT/WAITANYKEY/FORCEWAIT/ANYKEY）。
// 对齐 C# IsWaitingEnterKey：这类等待点击控制台任意位置（或回车）即继续，
// 不要求按钮/文本框 —— 否则 DQPRINT 结尾的 WAIT 永远等不到输入。
bool inputExpectsAnyKey(const QString& kind) {
    const QString k = kind.toUpper();
    return k == QLatin1String("WAIT") || k == QLatin1String("WAITANYKEY")
        || k == QLatin1String("FORCEWAIT") || k == QLatin1String("ANYKEY");
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

    if (m_wrapLines) {
        const QList<ConsoleDisplayLine> lines = m_layout.wrapSegments(std::move(line.segments),
                                                                     line.align);
        int row = m_buffer.count();
        for (ConsoleDisplayLine l : lines) {
            // wrapSegments 只负责切分逻辑行；绝对列/对齐仍由同一入口计算。
            m_layout.placeLine(l, row++);
            trackSpanReach(l);
            m_buffer.appendLine(l);
        }
    } else {
        // pointOffset/part.col 的单位是字符列，不是像素。统一走 placeLine，
        // 由 maxCols() 和 widthUnits() 计算对齐，避免 760px 被误当成 760 列。
        m_layout.placeLine(line, m_buffer.count());
        trackSpanReach(line);
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
    }
    m_buffer.removeLastLogicalLines(n);
    m_lastLineTemporary = false;   // 末尾的「一時行」已被删 -> 清标记
    // 被删掉的行：按行缓存与版本作废（只波动尾部，其余行的缓存仍然有效）。
    // 版本必须一起删 —— 重打印会复用这些绝对行号，旧版本号残留会让模型误判
    // 「内容没变」而保留旧区块（点按钮后画面不刷新、按钮世代过期失效的根因）。
    for (auto it = m_lineCache.begin(); it != m_lineCache.end(); ) {
        if (it.key() >= m_buffer.count()) it = m_lineCache.erase(it);
        else ++it;
    }
    for (auto it = m_lineVersion.begin(); it != m_lineVersion.end(); ) {
        if (it.key() >= m_buffer.count()) it = m_lineVersion.erase(it);
        else ++it;
    }
    clampScroll();
    markDirty();
}

// 记录「区块最多往上探出窗口几行」：rows 本身，加上 ypos 的负偏移折算的行数。
// 只在行定型（measurePart 算完 cols/rows/top）之后调用。
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

void ConsoleBackend::clearAll() {
    qDebug() << "[render] clearAll（清屏，行数" << m_buffer.count() << "）";
    m_buffer.clear();
    m_maxSpanReach = 1;
    m_lastLineTemporary = false;
    m_pendingParts.clear();
    m_sealed.clear();
    m_pendingOpen = false;
    m_scrollOffset = 0;
    invalidateLineCache();        // 行全部消失 -> 按行缓存与版本作废
    ++m_generation;
    emit generationChanged();
    emit cleared();
    emit lineCountChanged();
    notifyWindowChanged();        // 模型随之 reset（beginResetModel）
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

// SETBGCOLOR / RESETBGCOLOR：文字背景色（与前景色同样作用到后续 span）
void ConsoleBackend::setBgColor(const QColor& color) { m_style.bgColor = color; }

void ConsoleBackend::resetBgColor() { m_style.bgColor = QColor(); }

void ConsoleBackend::setFontStyle(bool bold, bool italic, bool underline, bool strike) {
    m_style.bold = bold;
    m_style.italic = italic;
    m_style.underline = underline;
    m_style.strike = strike;
}

void ConsoleBackend::markDirty() {
    m_dirty = true;
    // 内容版本：模型据此判定尾行（未提交/一時行）是否需重建
    ++m_contentVersion;
}

// windowChanged 的统一出口：内容代数(epoch)自增后再广播。所有窗口变化必须走这里。
// 同时把当前窗口同步进增量区块模型 —— 滚动/追加在这里产生 insertRows/removeRows
// 增量，QML 的 Instantiator 只对变化的行做增删（不再整屏重建 delegate）。
void ConsoleBackend::notifyWindowChanged() {
    ++m_windowChangedCount;
    ++m_windowEpoch;
    syncBlockModel();
    emit windowChanged();
}

QString ConsoleBackend::perfReport() const {
    return QStringLiteral(
               "windowChanged=%1\nvisibleBlocksCalls=%2\nlayerBlocksCalls=%3\n"
               "lineFlattenBuilds=%4\nlineFlattenMs=%5\n"
               "modelRows=%6 modelInserts=%7 modelRemoves=%8 modelUpdates=%19 modelResets=%9 modelSyncs=%10 modelSyncMs=%11\n"
               "lineCache=%12 lines=%13 maxSpanReach=%14\n"
               "bufferLines=%15 logicalLines=%16 visibleCount=%17 scrollOffset=%18")
        .arg(m_windowChangedCount)
        .arg(m_visibleBlockCalls)
        .arg(m_layerBlockCalls)
        .arg(m_lineCacheBuilds)
        .arg(m_lineCacheMs)
        .arg(m_blockModel.rowCount())
        .arg(m_blockModel.insertedRows())
        .arg(m_blockModel.removedRows())
        .arg(m_blockModel.resets())
        .arg(m_blockModel.syncs())
        .arg(m_blockModel.lastSyncMs())
        .arg(m_lineCache.size())
        .arg(displayLineCount())
        .arg(m_maxSpanReach)
        .arg(m_buffer.count())
        .arg(m_buffer.logicalLineCount())
        .arg(m_visibleCount)
        .arg(m_scrollOffset)
        .arg(m_blockModel.updatedRows());
}

void ConsoleBackend::resetPerfCounters() {
    m_layerBlockCalls = 0;
    m_visibleBlockCalls = 0;
    m_windowChangedCount = 0;
    m_lineCacheBuilds = 0;
    m_lineCacheMs = 0;
    m_blockModel.resetPerf();
}

void ConsoleBackend::flush() {
    if (!m_dirty) return;
    m_dirty = false;
    emit lineCountChanged();
    notifyWindowChanged();
}

void ConsoleBackend::setFontSize(int px) {
    // 字号决定「区块长」像素（columnWidthPx = 字号/2），区块宽被烘焙进按行缓存
    // —— 变了就整表作废，否则改字号后画面（尤其图片尺寸档位）不更新。
    if (px > 0 && px != m_layout.fontSize()) invalidateLineCache();
    m_layout.setFontSize(px);
    notifyWindowChanged();
}

void ConsoleBackend::setWindowWidth(int px) {
    // 窗口宽决定逻辑列数 -> 折行结果，同样是行内容的一部分。
    if (px > 0 && px != m_layout.windowWidth()) invalidateLineCache();
    m_layout.setWindowWidth(px);
    notifyWindowChanged();
}

// 逻辑网格列/行数：脚本看到的列数（居中、换行都以它为准）。
// 与「窗口像素 / 单元格像素」同源，装载后固定。
void ConsoleBackend::setGridColumns(int columns) {
    if (columns <= 0 || columns == m_layout.gridColumns()) return;
    m_layout.setGridColumns(columns);
    invalidateLineCache();   // 列数变 -> 折行/对齐变 -> 缓存失效
    notifyWindowChanged();   // 行位置在读取时按当前网格惰性重算
}

void ConsoleBackend::setGridRows(int rows) {
    if (rows <= 0 || rows == m_layout.gridRows()) return;
    m_layout.setGridRows(rows);
    invalidateLineCache();
    notifyWindowChanged();
}

void ConsoleBackend::setLineHeight(int px) {
    if (px <= 0 || px == m_lineHeight) return;
    m_lineHeight = px;
    m_layout.setLineHeight(px);     // 区块「高」也按行高算
    invalidateLineCache();          // 行高被烘焙进区块 height/offsetRows
    notifyWindowChanged();
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
//   y = (绝对行 - 窗口顶行) × 行高
//   x = 行对齐平移 + 段内偏移
//
// QML 运行时通过**增量模型**（blockModel）取区块：滚动/追加只产生
// insertRows/removeRows，未变化的行原地复用。下面的兼容入口（visibleBlocks /
// textBlocks / imageBlocks / shapeBlocks）返回模型当前内容，供 test_cli 的
// :geometry/:check 诊断与 QML 测试使用 —— 与 QML 看到的是同一份数据。
// ---------------------------------------------------------------------------

// ===========================================================================
// ConsoleBlockModel
// ===========================================================================

ConsoleBlockModel::ConsoleBlockModel(QObject* parent) : QAbstractListModel(parent) {}

int ConsoleBlockModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_flat.size();
}

QVariant ConsoleBlockModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_flat.size())
        return {};
    if (role == BlockRole) return m_flat.at(index.row());
    return {};
}

QHash<int, QByteArray> ConsoleBlockModel::roleNames() const {
    return { { BlockRole, QByteArrayLiteral("block") } };
}

void ConsoleBlockModel::resetPerf() {
    m_insertedRows = m_removedRows = m_updatedRows = m_resets = m_syncs = m_lastSyncMs = 0;
}

QVariantList ConsoleBlockModel::allBlocks() const {
    QVariantList out;
    out.reserve(m_flat.size());
    for (const QVariantMap& m : m_flat) out.append(m);
    return out;
}

QVariantList ConsoleBlockModel::blocksOfKind(const QString& kind) const {
    QVariantList out;
    const QString key = QStringLiteral("layer");
    for (const QVariantMap& m : m_flat) {
        if (m.value(key).toString() == kind) out.append(m);
    }
    return out;
}

void ConsoleBlockModel::reset() {
    if (m_flat.isEmpty() && m_lines.isEmpty()) return;
    beginResetModel();
    m_lines.clear();
    m_flat.clear();
    ++m_resets;
    endResetModel();
}

// 摘除第 i 条行条目（连带其模型行），后续条目的 startRow 前移。
void ConsoleBlockModel::removeEntryAt(int i) {
    const LineEntry e = m_lines.at(i);
    if (e.count > 0) {
        beginRemoveRows(QModelIndex(), e.startRow, e.startRow + e.count - 1);
        m_flat.remove(e.startRow, e.count);
        m_removedRows += e.count;
        endRemoveRows();
        for (int j = i + 1; j < m_lines.size(); ++j)
            m_lines[j].startRow -= e.count;
    }
    m_lines.removeAt(i);
}

// 行级可见性（与旧 per-block 裁剪同语义：只裁「整块在窗口上方」）：
//   存在区块满足 (abs - first + offsetRows) + rows > 0
bool ConsoleBlockModel::lineVisibleInWindow(const QVariantList& blocks, int abs,
                                            int first, int visibleCount) const {
    Q_UNUSED(visibleCount);
    for (const QVariant& v : blocks) {
        const QVariantMap m = v.toMap();
        const double topRow = double(abs - first) + m.value(QStringLiteral("offsetRows")).toDouble();
        const int rows = qMax(1, m.value(QStringLiteral("rows")).toInt());
        if (topRow + rows > 0.0) return true;
    }
    return false;
}

void ConsoleBlockModel::setWindow(int rangeFirst, int rangeEnd, int windowFirst,
                                  int visibleCount,
                                  const std::function<QVariantList(int, quint64&)>& lineBlocks) {
    QElapsedTimer timer;
    timer.start();
    ++m_syncs;

    if (rangeEnd <= rangeFirst) {
        reset();
        m_lastSyncMs = timer.elapsed();
        return;
    }

    // 1) 收集新窗口的行（含行级裁剪；blocks 从按行缓存取，代价是引用拷贝）。
    //    裁剪锚点 = **窗口顶行**（windowFirst），不是扫描起点（rangeFirst 只多了
    //    窗口上方的探出行，用于接住跨行/负 ypos 的图）。
    //    同时记录每行的**内容版本**，供步骤 2 判定「行号没变但内容变了」。
    QList<int> wantedAbs;
    QHash<int, QVariantList> wantedBlocks;
    QHash<int, quint64> wantedVersion;
    for (int abs = rangeFirst; abs < rangeEnd; ++abs) {
        quint64 version = 0;
        QVariantList blocks = lineBlocks(abs, version);
        if (!lineVisibleInWindow(blocks, abs, windowFirst, visibleCount)) continue;
        wantedAbs.append(abs);
        wantedBlocks.insert(abs, blocks);
        wantedVersion.insert(abs, version);
    }

    // 2) 结构性摘除（自底向上，保持行号稳定）：
    //      * 不在新窗口里的行；
    //      * 区块数变了的行 —— 行程不同，只能摘除后由步骤 3 重插。
    //    区块数没变的「内容已变」行**留在原地**（startRow 不动），交给步骤 4
    //    原地刷新 —— 只摘真正必须摘的，历史行绝不动。
    QList<int> inPlaceAbs;
    for (int i = m_lines.size() - 1; i >= 0; --i) {
        const LineEntry& e = m_lines.at(i);
        const auto wb = wantedBlocks.constFind(e.abs);
        if (wb == wantedBlocks.constEnd()) { removeEntryAt(i); continue; }
        if (wantedVersion.value(e.abs) == e.version) continue;   // 内容未变 -> 复用
        if (e.count == wb.value().size())
            inPlaceAbs.append(e.abs);                            // 原地刷新
        else
            removeEntryAt(i);                                    // 行程变了 -> 重插
    }

    // 3) 插入新增/重建的行（按 abs 升序，插到正确位置）
    for (int abs : wantedAbs) {
        bool exists = false;
        for (const LineEntry& e : m_lines) {
            if (e.abs == abs) { exists = true; break; }
        }
        if (exists) continue;
        int pos = 0;
        int row = 0;
        while (pos < m_lines.size() && m_lines.at(pos).abs < abs) {
            row += m_lines.at(pos).count;
            ++pos;
        }
        const QVariantList blocks = wantedBlocks.value(abs);
        const quint64 version = wantedVersion.value(abs);
        if (blocks.isEmpty()) {
            // 空行：占位条目（无行可发信号）
            m_lines.insert(pos, LineEntry{ abs, row, 0, version });
            continue;
        }
        beginInsertRows(QModelIndex(), row, row + blocks.size() - 1);
        for (int k = 0; k < blocks.size(); ++k)
            m_flat.insert(row + k, blocks.at(k).toMap());
        for (int j = pos; j < m_lines.size(); ++j)
            m_lines[j].startRow += blocks.size();
        m_lines.insert(pos, LineEntry{ abs, row, int(blocks.size()), version });
        m_insertedRows += blocks.size();
        endInsertRows();
    }

    // 4) 原地刷新：Qt 文档（QAbstractItemModel::dataChanged）——「现有条目的数据
    //    变化」发 **dataChanged(topLeft, bottomRight)**：视图只在 [起始行,
    //    终止行] 区间内原地重绑数据，**不销毁/重建委托**；行数不变，也就不会
    //    触发 count 变化引发的重排/滚动扰动。绝不能用 removeRows + insertRows
    //    表达内容变化 —— 那会先拆掉区间内的委托，历史区跟着遭殃。
    //    相邻区间合并成一条 dataChanged（起始行号 + 终止行号）。
    if (!inPlaceAbs.isEmpty()) {
        struct Span { int first, last; };
        QList<Span> spans;
        for (int abs : inPlaceAbs) {
            LineEntry* entry = nullptr;
            for (LineEntry& e : m_lines) {
                if (e.abs == abs) { entry = &e; break; }
            }
            if (!entry) continue;
            const QVariantList blocks = wantedBlocks.value(abs);
            entry->version = wantedVersion.value(abs);
            m_updatedRows += blocks.size();
            for (int k = 0; k < blocks.size(); ++k)
                m_flat[entry->startRow + k] = blocks.at(k).toMap();
            if (!blocks.isEmpty())
                spans.append({ entry->startRow,
                               entry->startRow + int(blocks.size()) - 1 });
        }
        std::sort(spans.begin(), spans.end(),
                  [](const Span& a, const Span& b) { return a.first < b.first; });
        int first = -1, last = -1;
        const auto flush = [&]() {
            if (first >= 0) emit dataChanged(index(first), index(last));
        };
        for (const Span& s : spans) {
            if (first >= 0 && s.first <= last + 1) {
                last = qMax(last, s.last);
            } else {
                flush();
                first = s.first;
                last = s.last;
            }
        }
        flush();
    }

    m_lastSyncMs = timer.elapsed();
}

// ===========================================================================
// ConsoleBackend：按行摊平缓存
// ===========================================================================

// 整表作废（布局度量变化：字号/行高/网格）。区块的 width/height/offsetRows
// 是按当前字号与行高**烘焙**进缓存值的 —— 度量一变，旧缓存全部失效，
// 否则「画像サイズ 拡大/縮小」之类改字号后画面不更新。
void ConsoleBackend::invalidateLineCache() {
    m_lineCache.clear();
    m_lineVersion.clear();
    m_cacheDropped = m_buffer.droppedFromFront();
}

// 取某绝对行的区块列表（**绝对坐标系**：row = abs，z = 打印顺序编码）。
// versionOut 回填该行内容版本：已提交行 = 摊平时的发号（缓存失效后重摊平
// 会得到新号）；未提交行/一時行 = 内容版本号（每次 markDirty 自增）。
// 显示行一旦提交就不可变（可变的只有尾部的「一時行」/未提交行），故只有
// 提交且非一時的行进缓存；行数据与窗口无关，滚动不失效。
QVariantList ConsoleBackend::lineBlocks(int abs, quint64* versionOut) const {
    // 容量裁剪会让所有绝对行号前移 -> 按行号为键的缓存整体作废。
    if (m_buffer.droppedFromFront() != m_cacheDropped) {
        m_lineCache.clear();
        m_lineVersion.clear();
        m_cacheDropped = m_buffer.droppedFromFront();
    }
    const bool cacheable = abs < m_buffer.count()
        && !(m_lastLineTemporary && abs == m_buffer.count() - 1);
    if (cacheable) {
        const auto it = m_lineCache.constFind(abs);
        if (it != m_lineCache.constEnd()) {
            if (versionOut) *versionOut = m_lineVersion.value(abs);
            return it.value();
        }
    }

    QElapsedTimer timer;
    timer.start();
    const ConsoleDisplayLine line = displayLine(abs);
    // pointOffset 与 relCol 都是字符列单位；newline() 已经完成布局。
    const int offset = line.pointOffset;
    QVariantList out;
    int idx = 0;
    for (int si = 0; si < line.segments.size(); ++si) {
        const ConsoleSegment& seg = line.segments.at(si);
        for (const ConsoleSpan& part : seg.spans) {
            QVariantMap m = part.toVariantMap();
            m.insert("layer", part.layer());           // text / image / shape
            // 扁平 z（= 控制台打印顺序）。所有区块挂在同一个父 Item 下，靠它
            // 把 text/image/shape 按真正的绘制顺序叠放（对齐 C#：逐 part 顺序
            // 绘制 —— 图可以压住先打印的字，后打印的字也能盖住先打印的图）。
            // z = 绝对行 × 1024 + 行内序号：确定性、与摊平时机无关（缓存可用）。
            m.insert("z", double(abs) * 1024.0 + double(qMin(idx, 1023)));
            m.insert("col", offset + part.relCol);     // 绝对列（单位 = 区块长）
            m.insert("row", abs);                      // **绝对行**（单位 = 区块高）
            const int spanCols = qMax(1, part.cols);
            // 区块占几行：文本/形状恒 1 行，**图片按资源尺寸跨多行**
            // （ConsoleLayout::measurePart 已按 `<img width/height>` 算好 rows）。
            const int spanRows = qMax(1, part.rows);
            // ypos：图片相对本行的纵向偏移（像素 -> 行数；负 = 往上盖）。QML 侧
            // 按 (row - windowTopRow + offsetRows) 摆放，图层（枠/特效）才会正好
            // 盖在立絵边缘。
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
            m.insert("lineIndex", abs);                // 点击回传 = **绝对行号**
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
    ++m_lineCacheBuilds;
    m_lineCacheMs += timer.elapsed();
    if (cacheable) {
        const quint64 serial = ++m_lineSerial;
        m_lineCache.insert(abs, out);
        m_lineVersion.insert(abs, serial);
        if (versionOut) *versionOut = serial;
    } else if (versionOut) {
        // 未提交行/一時行：内容版本直接当行版本（内容不变 -> 版本不变 ->
        // 模型跳过重建；一有改动 markDirty 自增，版本必变）。高位打标记，
        // 与已提交行的发号（小整数）永不冲突。
        *versionOut = (quint64(1) << 63) | m_contentVersion;
    }
    return out;
}

// 兼容入口：当前窗口的全部区块（= 增量模型的当前内容，含行级裁剪）。
// test_cli 的 :geometry/:check 与 QML 测试用它做诊断 —— 与 QML 展示同源。
QVariantList ConsoleBackend::visibleBlocks() {
    ++m_visibleBlockCalls;
    syncBlockModel();
    return m_blockModel.allBlocks();
}

// 三个层各自的区块列表：从模型的当前内容里按 layer 挑（诊断用）
QVariantList ConsoleBackend::layerBlocks(const QString& layer) {
    ++m_layerBlockCalls;
    syncBlockModel();
    return m_blockModel.blocksOfKind(layer);
}

// 把当前窗口同步进增量模型。窗口范围 = 可见行 ± 最长区块的探出量；
// 行级裁剪（整块在窗口上方的不产出）在模型内完成。
void ConsoleBackend::syncBlockModel() {
    const int n = displayLineCount();
    if (n <= 0 || m_visibleCount <= 0) {
        m_blockModel.reset();
        return;
    }
    const int first = windowFirstLine();
    const int scanFirst = std::max(0, first - m_maxSpanReach);
    const int scanEnd = std::min(n, first + m_visibleCount + m_maxSpanReach);
    // 行的重建由**内容版本**驱动（见 ConsoleBlockModel::setWindow）：已提交行
    // 版本稳定（缓存命中即跳过），未提交/一時行版本 = 内容版本（内容变才重建）。
    m_blockModel.setWindow(scanFirst, scanEnd, first, m_visibleCount,
                           [this](int abs, quint64& version) {
                               return lineBlocks(abs, &version);
                           });
}

// root 层的「内容高度」= 已排版可见行数 × 行高
int ConsoleBackend::contentHeight() const {
    return (displayLineCount() - windowFirstLine()) * m_lineHeight;
}

void ConsoleBackend::clickAt(int absLine, int segmentIndex) {
    const int n = displayLineCount();
    // 区块里的 lineIndex 是**绝对行号**（增量模型的行数据与窗口无关，
    // 滚动时不做任何数据改写）—— 点击按绝对行定位。
    const int abs = absLine;
    if (abs < 0 || abs >= n) {
        return;
    }
    const ConsoleDisplayLine line = displayLine(abs);
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
        qDebug() << "[input] 点击忽略（世代过期）行" << abs << "段" << segmentIndex;
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
    qDebug() << "[input] 点击按钮 行" << abs << "段" << segmentIndex
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
    notifyWindowChanged();
}

void ConsoleBackend::setScrollOffset(int offset) {
    offset = std::max(0, offset);
    const int maxOffset = std::max(0, displayLineCount() - m_visibleCount);
    offset = std::min(offset, maxOffset);
    if (offset == m_scrollOffset) {
        return;
    }
    m_scrollOffset = offset;
    notifyWindowChanged();
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
