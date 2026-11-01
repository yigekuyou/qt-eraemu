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
#include "graphics_store.h"
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
    m_historyModel.setSourceModel(this);
    m_timer.setInterval(m_frameMs);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ConsoleBackend::tick);
    // 只在有待发布内容时运行；空闲时不让事件循环每帧唤醒。
    // markDirty() 会按需启动，flush() 会在发布完成后停止。
    // 帧呈现应答的兜底（见 notifyInputRequested）：50ms 内没等到 QML 的
    // 呈现回报就自行放行，宁可少等待一点也不能让限时输入卡死。
    m_presentationFallback.setSingleShot(true);
    m_presentationFallback.setInterval(50);
    connect(&m_presentationFallback, &QTimer::timeout, this, [this]() {
        notifyFramePresented(m_presentationGeneration);
    });
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

bool ConsoleBackend::lastLineIsEmpty() const {
    if (m_buffer.isEmpty())
        return buildLine().plainText().isEmpty();
    return m_buffer.at(m_buffer.count() - 1).plainText().isEmpty();
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

// PRINTD 系：临时把「SETCOLOR 生效的颜色」摘掉再打印（span 不带 color ->
// QML 落到 root.foreColor，即 Config.ForeColor），字体样式/字体名/背景色照旧。
// SETCOLOR 的状态本身保留：C# 只在 Style 取值时分叉，userStyle.Color 不变。
void ConsoleBackend::printIgnoringSetColor(const QString& text) {
    if (text.isEmpty()) return;
    const QColor savedColor = m_style.color;
    const bool savedColorChanged = m_style.colorChanged;
    m_style.color = QColor();
    m_style.colorChanged = false;
    print(text);
    m_style.color = savedColor;
    m_style.colorChanged = savedColorChanged;
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
    renderTemplate(output, /*toBuffer=*/false);
}

void ConsoleBackend::printTemplateBuffered(const PrintTemplate& output) {
    renderTemplate(output, /*toBuffer=*/true);
}

void ConsoleBackend::renderTemplate(const PrintTemplate& output, bool toBuffer) {
    // C# PrintHtml(str, toPrintBuffer)（EmueraConsole.Print.cs:498）：
    //   * 非缓冲：`if (!printBuffer.IsEmpty) { Flush; addRangeDisplayLine; }`
    //     然后 addRangeDisplayLine(Html2DisplayLine(...)) —— 先冲刷挂起的当前行，
    //     HTML 内容**自成一行**（HTML_PRINT 自动换行，类似 PRINTSL）；
    //   * 缓冲：`printBuffer.AppendButton(...)` **不冲刷** —— 挂进当前行，
    //     与后续 PRINT 合成同一行，等下一次 newline()/非缓冲输出统一发布。
    // 本移植的 m_pendingParts/m_sealed/m_pendingOpen 就是 C# PrintStringBuffer，
    // 此前缺的只是这条「不冲刷、不收尾」的路径。
    if (!toBuffer && m_pendingOpen) newline();
    const ConsoleAlign savedAlign = m_align;
    const bool savedWrap = m_wrapLines;
    QList<ConsoleAlign> alignments;
    QList<bool> wrapping;
    ConsoleStyle style = m_style;
    QList<ConsoleStyle> styles;
    ConsoleSegment button;
    bool inButton = false;
    // Relative div children have their own cursor. Sibling divs at the same
    // rect share an origin (FL3.8 overlays face, pupils and features this way).
    //
    // px = div 内**已经排到第几像素**。同一个 <div> 里可以有多段（换色/换字体
    // 就会切成多段），它们必须顺次前进；只有「没有 px 游标」时每段才会退回
    // div 原点互相压住（FL3.8 情報画面「性別:」被「男性」盖住就是这个）。
    struct DivisionCursor { int x; int y; int col = 0; int px = 0; };
    QList<DivisionCursor> divisions;
    const auto lengthPx = [this](int value, bool pixels) {
        return pixels ? value : value * m_layout.fontSize() / 100;
    };
    // ---- <div> 行缓冲 ------------------------------------------------------
    // C# ConsoleDivPart 持有自己的 Buffer（内容先排进 div 的行），DrawTo 时整块
    // 画到 rect 位置；div 内的 <br> 只影响 div 自己的行，不会打断正文。
    // 移植侧以前把 div 内容直接排进正文流：`<br>` 会换正文行，于是 FL3.8 拠点
    // 地图的 25 个 `<div rect>` 格子被摊成一列（每格两行）。
    // 现在：捕获期内容进 captures.last()，EndDivision 后按 rect 的 y/行高 落到
    // 对应行，x 由 positionedXpx（div 原点 + 段内偏移）交给布局绝对定位。
    struct DivCapture {
        int xPx = 0, yPx = 0, wPx = 0, hPx = 0;
        QList<QList<ConsoleSegment>> lines{1};   // 每行一组段
    };
    QList<DivCapture> captures;
    QList<DivCapture> overlays;
    int blockRow = 0;                             // 本区块起点行号
    const auto captureLine = [&captures](QList<ConsoleSegment>& fallback) -> QList<ConsoleSegment>& {
        return captures.isEmpty() ? fallback : captures.last().lines.last();
    };

    const auto sealButton = [&] {
        if (!inButton) return;
        button.generation = m_generation;
        captureLine(m_sealed).append(button);
        button = {};
        inButton = false;
    };
    // 换行 = 回到 div 的左边距（C# HtmlManager 在 <br> 时把 PointX 复位到 div.Left）。
    // 不复位的话 div 内的 <br> 会让每一行接着上一行末尾继续向右漂 —— FL3.8 情報
    // 画面的「陥落／淫乱素質／嗜好」就画成斜阶梯（实测每行右移 42px）。
    const auto resetDivisionCursors = [&divisions] {
        for (auto& d : divisions) { d.px = 0; d.col = 0; }
    };
    for (const auto& part : output.parts) {
        if (part.kind == PrintTemplatePart::Kind::Division) {
            sealButton();
            flushPendingToSegments(captureLine(m_sealed)); m_pendingParts.clear();
            const int parentX = divisions.isEmpty() ? 0 : divisions.last().x;
            const int parentY = divisions.isEmpty() ? 0 : divisions.last().y;
            const int divX = parentX + lengthPx(part.x, part.xposIsPixels);
            const int divY = parentY + lengthPx(part.y, part.yposIsPixels);
            divisions.append({divX, divY, 0, 0});
            // 只有「整块提交」的 HTML_PRINT 才把 div 当独立叠放层处理：
            // 缓冲模式（PRINTFORM 续行 / HTML_PRINT toBuffer）里 div 的内容
            // 必须继续留在未定型行上，否则后续 print 接不上。
            if (toBuffer) { continue; }
            if (captures.isEmpty()) {
                // 顶层 div = 独立叠放层：先把正文行定格，再记下本区块的起始行，
                // div 的 rect.y 就是相对这个起始行的像素偏移。
                if (m_pendingOpen) newline();
                blockRow = m_buffer.count();
            }
            DivCapture cap;
            cap.xPx = divX;
            cap.yPx = divY;
            cap.wPx = lengthPx(part.width, part.widthIsPixels);
            cap.hPx = lengthPx(part.height, part.heightIsPixels);
            captures.append(cap);
            continue;
        }
        if (part.kind == PrintTemplatePart::Kind::EndDivision) {
            sealButton();
            flushPendingToSegments(captureLine(m_sealed)); m_pendingParts.clear();
            if (!captures.isEmpty()) {
                DivCapture cap = captures.takeLast();
                if (captures.isEmpty()) overlays.append(cap);
                else captures.last().lines.last().append(cap.lines.value(0));
            }
            if (!divisions.isEmpty()) divisions.removeLast();
            continue;
        }
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
            if (m_pendingOpen) { sealButton(); newline(); resetDivisionCursors(); }
            if (!wrapping.isEmpty()) m_wrapLines = wrapping.takeLast();
            continue;
        }
        if (part.kind == PrintTemplatePart::Kind::EndAlignment) {
            if (m_pendingOpen) { sealButton(); newline(); resetDivisionCursors(); }
            if (!alignments.isEmpty()) m_align = alignments.takeLast();
            continue;
        }
        if (part.kind == PrintTemplatePart::Kind::Alignment) {
            // C# HtmlManager 的 <p align> 只设置整行对齐，不提交已有片段。
            // eraTW 左对齐会把 <shape type='space'> 拼在 <p> 前面；若在此
            // 换行，每个图层就多占一行，而框的 ypos 只补偿一行，最终下错一格。
            alignments.append(m_align);
            const QString align = part.text.toLower();
            if (align == "center") m_align = ConsoleAlign::Center;
            else if (align == "right") m_align = ConsoleAlign::Right;
            else if (align == "left") m_align = ConsoleAlign::Left;
            continue;
        }
        if (part.kind == PrintTemplatePart::Kind::Button || part.kind == PrintTemplatePart::Kind::NonButton) {
            sealButton();
            flushPendingToSegments(captureLine(m_sealed)); m_pendingParts.clear();
            button.isButton = part.kind == PrintTemplatePart::Kind::Button;
            button.pointXLocked = part.lockedPosition;
            button.positionPercent = part.x;
            button.intValue = part.buttonValue.toLongLong(&button.isInteger);
            button.strValue = part.buttonValue; button.tooltip = part.tooltip;
            inButton = true;
            continue;
        }
        if (part.kind == PrintTemplatePart::Kind::EndButton) { sealButton(); continue; }
        if (part.kind == PrintTemplatePart::Kind::Break) {
            const auto continued = button; const bool wasButton = inButton;
            sealButton();
            if (!captures.isEmpty()) {
                // div 内的换行只影响 div 自己的行缓冲，不打断正文；
                // 行内游标回到 div 左边界（C# 在 <br> 时把 PointX 复位到 div.Left）。
                // 先把本行还没定型的 part 冲进 div 的**当前行**，再另起一行；
                // 否则文字会漏到下一行、上一行变空（div 内多行内容只剩一行）。
                flushPendingToSegments(captureLine(m_sealed)); m_pendingParts.clear();
                captures.last().lines.append(QList<ConsoleSegment>());
                if (!divisions.isEmpty()) { divisions.last().px = 0; divisions.last().col = 0; }
            } else {
                newline();
                resetDivisionCursors();
            }
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
            span.widthIsPixels = part.widthIsPixels;
            span.heightIsPixels = part.heightIsPixels;
            span.yposIsPixels = part.yposIsPixels;
            span.yposRaw = part.y;
            span.imageButton = part.imageAlt;   // <img srcb=...> 按钮选中态替换图
            // C# ConsoleImagePart 在解析期就取到图像并持有；移植侧 QML 异步按名取图，
            // 所以这里先把像素钉住：QUIT（closeGame -> setRoot("")+GraphicsStore 清空）
            // 之后仍能画出最后一屏的 <img>（否则 "Failed to get image from provider"）。
            reportMissingImage(part.text);
            if (!span.imageButton.isEmpty()) reportMissingImage(span.imageButton);
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
        if (!divisions.isEmpty()) {
            auto& division = divisions.last();
            span.pointXLocked = true;
            // rect 原点 + **div 内已排到第几像素**。同一个 <div> 里换色/换字体
            // 会切成多段（FL3.8：「性別:<font color=…>男性</font>」），每段都必须
            // 接在前一段右边；只给 div 原点会让后一段压在前一段上。
            span.positionedXpx = division.x + division.px;
            span.relCol = qRound(double(division.x) / m_layout.columnWidthPx()) + division.col;
            span.positionedTopPx = division.y;
            m_layout.measurePart(span);
            // 前进量取「这一段实际占的像素」：图片用**原始像素宽**（80px 的缩略图
            // 按 11 列 × 7px = 77px 前进，十张就少 30px），其余用测量宽度。
            division.px += (span.kind == ConsoleSpanKind::Image && span.imageWidthPx > 0)
                               ? span.imageWidthPx
                               : qMax(0, span.width);
            division.col += span.cols;
        }
        ensureLineOpen();
        if (inButton) button.spans.append(span); else appendPart(span);
        markDirty();
    }
    sealButton();
    // ---- div 叠放层落位 --------------------------------------------------
    // rect.y / 行高 = 目标行（相对本区块起点），rect.x 已由 positionedXpx
    // 交给布局绝对定位；同一行的多个 div 合并成一行的多个段（FL3.8 拠点地图
    // 就是 5 个 780×480 的格子并排）。超出 rect 的部分由 QML 侧裁剪。
    if (!overlays.isEmpty()) {
        const int lineH = qMax(1, m_layout.fontSize());
        const auto dropEmpty = [](const DivCapture& c) {
            QList<QList<ConsoleSegment>> ls = c.lines;
            // `<div><br>…` 的引导换行不该占一行（C# 的 div 缓冲同样不吃它）
            if (ls.size() > 1 && ls.first().isEmpty()) ls.removeFirst();
            while (!ls.isEmpty() && ls.last().isEmpty()) ls.removeLast();
            return ls;
        };
        QMap<int, QList<ConsoleSegment>> rows;
        for (const DivCapture& c : overlays) {
            const int row = c.yPx / lineH;
            const QList<QList<ConsoleSegment>> ls = dropEmpty(c);
            for (int k = 0; k < ls.size(); ++k) rows[row + k] += ls.at(k);
        }
        int guard = 0;
        for (auto it = rows.cbegin(); it != rows.cend(); ++it) {
            while (m_buffer.count() < blockRow + it.key() && guard++ < 4096) {
                m_sealed.clear(); m_pendingParts.clear();
                newline();
            }
            m_sealed = it.value();
            m_pendingParts.clear();
            m_pendingOpen = true;
            newline();
        }
        overlays.clear();
    }
    // 收尾：非缓冲把内容定格成完整行（HTML_PRINT 自动换行）；缓冲把尾部
    // **留在未定型行**，后续 print 继续接在同一行上。
    if (!toBuffer && m_pendingOpen) newline();
    m_align = savedAlign;
    m_wrapLines = savedWrap;
}

// ---- 工具提示样式（C# TOOLTIP_* 族 -> WinForms ToolTip 的属性）----
void ConsoleBackend::setToolTipColors(const QColor& fore, const QColor& back) {
    if (m_toolTipForeColor == fore && m_toolTipBackColor == back) return;
    m_toolTipForeColor = fore;
    m_toolTipBackColor = back;
    emit toolTipStyleChanged();
}

// ---- SETBGIMAGE 族（C# EmueraConsole.AddBackgroundImage / 背景列表）----
void ConsoleBackend::addBackgroundImage(const QString& name, int depth, int alpha) {
    if (name.isEmpty()) return;
    BgImage img;
    img.name = name;
    img.depth = depth;
    img.alpha = qBound(0, alpha, 255);
    // C# AddBackgroundImage：按深度**降序**插入（`(v1.Key >= v2.Key) ? -1 : 1`）；
    // BakeBackground 从头往后画 -> 列表后面的（深度小）盖在前面（深度大）之上。
    int at = 0;
    while (at < m_bgImages.size() && m_bgImages.at(at).depth >= img.depth) ++at;
    m_bgImages.insert(at, img);
    emit backgroundImagesChanged();
}

void ConsoleBackend::clearBackgroundImages() {
    if (m_bgImages.isEmpty()) return;
    m_bgImages.clear();
    emit backgroundImagesChanged();
}

void ConsoleBackend::removeBackgroundImage(const QString& upperName) {
    bool removed = false;
    for (int i = m_bgImages.size() - 1; i >= 0; --i) {
        if (m_bgImages.at(i).name.compare(upperName, Qt::CaseInsensitive) == 0) {
            m_bgImages.removeAt(i);
            removed = true;
        }
    }
    if (removed) emit backgroundImagesChanged();
}

QVariantList ConsoleBackend::backgroundImages() const {
    QVariantList out;
    for (const BgImage& img : m_bgImages) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), img.name);
        m.insert(QStringLiteral("depth"), img.depth);
        m.insert(QStringLiteral("alpha"), img.alpha);
        out.append(m);
    }
    return out;
}

// ---- HTML_PRINT_ISLAND（C# EmueraConsole.PrintHTMLIsland / _htmlElementList）----
void ConsoleBackend::printHtmlIsland(const PrintTemplate& markup) {
    // 岛层借用**同一走片循环**（风格/图片/按钮/<br> 全部同一套），但正文缓冲的
    // 挂起状态必须原样保留：先存后清，走完还原（renderTemplate 会清空
    // m_pendingParts/m_sealed 并在收尾调用 newline() —— 岛模式下提交进岛层）。
    const QList<ConsoleSpan> savedParts = m_pendingParts;
    const QList<ConsoleSegment> savedSealed = m_sealed;
    const bool savedOpen = m_pendingOpen;
    m_pendingParts.clear();
    m_sealed.clear();
    m_pendingOpen = false;
    m_islandMode = true;
    renderTemplate(markup, /*toBuffer=*/false);
    m_islandMode = false;
    m_pendingParts = savedParts;
    m_sealed = savedSealed;
    m_pendingOpen = savedOpen;
    emit htmlIslandChanged();
}

void ConsoleBackend::clearHtmlIsland() {
    if (m_islandLines.isEmpty()) return;
    m_islandLines.clear();
    emit htmlIslandChanged();
}

QVariantList ConsoleBackend::htmlIsland() const {
    // 岛行 -> 已摊平区块（与正文行同一 ConsoleBlock 形状；QML 用同一委托渲染）。
    // lineIndex 用行号占位即可 —— 岛层按钮已禁用（enabled=false），不会有点击回传。
    QVariantList rows;
    for (int i = 0; i < m_islandLines.size(); ++i) {
        QVariantMap row;
        row.insert(QStringLiteral("blocks"),
                   flattenLine(m_islandLines.at(i), i));
        rows.append(row);
    }
    return rows;
}

// BREAKBUTTON（C# Console.forceUpdateGeneration）：世代号自增 -> 屏上旧按钮的
// generation 与当前值不符，点击/输入判定一律忽略（见 buttonAt/hasEnabledButton）。
void ConsoleBackend::breakButtons() {
    ++m_generation;
    emit generationChanged();
    markDirty();
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
    newlineInternal(false);
}

void ConsoleBackend::newlineInternal(bool temporary) {
    // HTML_PRINT_ISLAND 期间（printHtmlIsland 借用同一走片循环）：行提交进
    // 岛层（C# _htmlElementList）—— 不进正文缓冲、不发模型信号；岛层按钮
    // 不可点（C# 的点击命中走主窗口的正文行列表，岛层只参与绘制）。
    if (m_islandMode) {
        ConsoleDisplayLine line = buildLine();
        m_sealed.clear();
        m_pendingParts.clear();
        m_pendingOpen = false;
        if (m_wrapLines) {
            QList<ConsoleDisplayLine> wrapped =
                m_layout.wrapSegments(std::move(line.segments), line.align);
            for (ConsoleDisplayLine& w : wrapped) {
                for (ConsoleSegment& seg : w.segments) seg.enabled = false;
                m_islandLines.append(std::move(w));
            }
        } else {
            for (ConsoleSegment& seg : line.segments) seg.enabled = false;
            m_islandLines.append(std::move(line));
        }
        return;
    }
    // C# PrintSingleLine / PrintFlush：**先把本行成型**（BufferToSingleLine），
    // 再走 addDisplayLine。顺序不能反 —— 反过来（先删舊行、后成型）会把本次
    // 待显示的未定型缓冲一起清掉，DQPRINT 的最后一句就成了空行。
    ConsoleDisplayLine line = buildLine();
    line.isTemporary = temporary;
    // C# addDisplayLine：`if (LastLineIsTemporary) deleteLine(1);` —— 顶掉上一条
    // 「一時行」；只作用于**已提交的**显示行，不碰 printBuffer。
    if (lastLineIsTemporary())
        deleteTailLogicalLines(1);
    m_sealed.clear();
    m_pendingParts.clear();
    m_pendingOpen = false;
    m_tailDirty = false;

    QList<ConsoleDisplayLine> committed;
    if (m_wrapLines) {
        // wrapSegments 只负责切分逻辑行；绝对列/对齐仍由同一入口计算。
        committed = m_layout.wrapSegments(std::move(line.segments), line.align);
        if (temporary) {
            // 折行的续行也属于这条一時行：deleteLine(1) 会把整组一起删掉，
            // 尾部判定（LastLineIsTemporary）看的是物理末行。
            for (ConsoleDisplayLine& w : committed) w.isTemporary = true;
        }
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
        // 图锚索引：新提交的行若含 Image span，记下它的 serial
        //（newline() 是行提交的唯一入口；updateStageModels 的舞台快照
        //  不再每次 flush 扫全部历史行找锚）。
        {
            const int lastRow = m_buffer.count() - 1;
            for (int i = 0; i < committed.size() && lastRow - i >= 0; ++i) {
                const ConsoleDisplayLine& appended = m_buffer.at(lastRow - i);
                for (const ConsoleSegment& seg : appended.segments) {
                    bool hasImage = false;
                    for (const ConsoleSpan& part : seg.spans) {
                        if (part.kind == ConsoleSpanKind::Image) { hasImage = true; break; }
                    }
                    if (hasImage) { m_imageSerials.insert(appended.serial); break; }
                }
            }
        }
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

// 区块生成时钉住像素；解析不到就把**资源名**交给上层（EraEngine 用当前执行
// 位置落一条可定位诊断）。空名字不算「缺资源」（`<img>` 没写 src 是排版问题，
// 不是资源问题）。
void ConsoleBackend::reportMissingImage(const QString& resourceName) {
    if (resourceName.isEmpty()) return;
    if (!ResourceImageProvider::pinImage(resourceName))
        emit imageResolutionFailed(resourceName);
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
    // 同 <img> 路径：区块生成时钉住像素（C# ConsoleImagePart 的 cImage 语义）
    reportMissingImage(resourceName);
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

// C# deleteLine：从 displayLineList 尾部删掉 n 个**逻辑行**（连同其折行续行）。
// 只动**已提交的显示行**；C# 的 printBuffer（= 本移植的 m_pendingParts/
// m_sealed）原样保留 —— CLEARLINE 之后接着 PRINT，内容仍接在当前行上。
// 「一時行替换」（C# addDisplayLine 的 deleteLine(1)）走同一入口。
void ConsoleBackend::deleteTailLogicalLines(int n) {
    if (n <= 0 || m_buffer.isEmpty()) return;
    const int oldCount = m_buffer.count();
    // 先预告物理行数，才能把 beginRemoveRows 括在真正的删除外面。
    const int physical = m_buffer.physicalTailCount(n);
    if (physical <= 0) return;
    beginRemoveRows(QModelIndex(), oldCount - physical, oldCount - 1);
    // 逻辑行计数（LINECOUNT）由缓冲按**实际删掉的**逻辑行递减：
    // removeLastLogicalLines 每弹掉一行 IsLogicalLine 就 --m_logicalCount。
    m_buffer.removeLastLogicalLines(n);
    endRemoveRows();
    // 预告的物理行数必须与实删一致（both 走同一个「只数逻辑行」的循环）
    Q_ASSERT(m_buffer.count() == oldCount - physical);
    m_removedRows += physical;
    pruneTailCache();
    // C# deleteLine 在现存行不够时提前 break（`if (displayLineList.Count == 0)
    // break;`），所以这里**没有**「补齐请求量」的补偿 —— 请求超过现存行数时
    // LINECOUNT 停在 0 附近而不是被减成负数（eraTW 的 -14 就是这么来的）。
    emit lineCountChanged();
}

bool ConsoleBackend::lastLineIsTemporary() const {
    return !m_buffer.isEmpty() && m_buffer.at(m_buffer.count() - 1).isTemporary;
}

void ConsoleBackend::clearLines(int n) {
    deleteTailLogicalLines(n);
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
    m_timer.stop();
    m_maxSpanReach = 1;
    m_publishedLineCount = 0;
    // HTML 岛层随清屏一起清空（C# ClearDisplay 清 _htmlElementList：初始化 /
    // 报错 / 返回标题都会走）。背景图是**窗口**的底图（ConsoleBackground），
    // C# 不随正文清屏，保留。
    const bool hadIsland = !m_islandLines.isEmpty();
    m_islandLines.clear();
    m_imageSerials.clear();       // 含图行索引随清屏整表作废
    // 屏上已无任何区块引用图片像素：把钉住的副本一起释放（下一次装载重新钉）。
    ResourceImageProvider::clearPinnedImages();
    invalidateLineCache();        // 行全部消失 -> 按行缓存作废
    ++m_generation;
    endResetModel();
    emit generationChanged();
    emit cleared();
    emit lineCountChanged();
    if (hadIsland) emit htmlIslandChanged();
    // 清屏也必须发布空的舞台图片快照；否则独立 QML 图片层会保留上一帧。
    updateStageModels(true);
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
    if (!m_timer.isActive())
        m_timer.start();
}

void ConsoleBackend::flush() {
    m_dirty = false;
    m_timer.stop();
    const int n = m_buffer.count();
    if (m_pendingOpen && !m_tailRow) {
        // 未定型行首次入模：insert 1 行（print 打开行缓冲后，到帧才发布）
        beginInsertRows(QModelIndex(), n, n);
        m_tailRow = true;
        endInsertRows();
        ++m_insertedRows;
        m_tailDirty = false;    // 新行的数据即最新
    } else if (m_tailRow && m_tailDirty) {
        emit dataChanged(index(n), index(n), {BlocksRole, SpanRowsRole});
        m_tailDirty = false;
    }
    if (m_publishedLineCount != n) {
        m_publishedLineCount = n;
        emit lineCountChanged();
    }
    // 帧边界统一发布舞台快照，避免 CLEARLINE 到 PRINT 中间状态被呈现。
    updateStageModels();
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
        updateInputEchoLine();       // 区块尺寸被烘焙进缓存，回显行同样要重摊
    }
}

// 逻辑网格列/行数：脚本看到的列数（居中、换行都以它为准）。
// 只来自 emuera.config 推导（EraEngine::syncConsoleGrid），装载后固定；
// 窗口缩放是 QML 舞台的等比 transform，不影响行数。
void ConsoleBackend::setGridColumns(int columns) {
    if (columns <= 0 || columns == m_layout.gridColumns()) return;
    m_layout.setGridColumns(columns);
    emit gridChanged();
    invalidateLineCache();   // 列数变 -> 折行/对齐变 -> 缓存失效
    if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1));
}

void ConsoleBackend::setStageInputRowReserved(bool reserved) {
    if (m_stageInputRowReserved == reserved) return;
    m_stageInputRowReserved = reserved;
    emit gridChanged();
    updateStageModels(true);
}

void ConsoleBackend::setGridRows(int rows) {
    if (rows <= 0 || rows == m_layout.gridRows()) return;
    m_layout.setGridRows(rows);
    emit gridChanged();
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
    updateInputEchoLine();
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

void ConsoleBackend::setRedraw(qint64 mode) {
    m_autoRedraw = (mode & 1) != 0;
    if ((mode & 2) != 0)
        updateStageModels(true);
}

void ConsoleBackend::updateStageModels(bool force) {
    // REDRAW 0 只保留当前舞台前帧；源模型仍遵守正常的 Qt 通知契约。
    if (!force && !m_autoRedraw) return;
    const int total = rowCount();
    const int visibleRows = m_stageInputRowReserved ? stageRows() : m_layout.gridRows();
    // 预留了输入行时，**溢出到输入行**的那一行不属于舞台：窗口最底一行是输入框
    // （C# 的 richTextBox1；本移植是 QML 的 ConsoleInput，不在舞台上），era 脚本
    // 会把输入提示打在这一行（它总是最新的那一行）。舞台装不下全部行时
    // （total > 舞台行数），最新一行改由 inputEchoLine 递交进输入回显栏，
    // 舞台快照止于它前面一行 —— 于是「舞台 45 行 + 输入回显 1 行 = 窗口 46 行」。
    // 没有溢出时（例如 eraTW 标题这 45 行定页画面）不摘：全部行都在舞台上。
    // 没有输入栏的宿主（test_cli、纯历史视图）不预留，舞台照旧占满 gridRows。
    const int echoRow = inputEchoRow();
    const bool echoExtracted = echoRow >= 0;
    // 输入栏在舞台外。尾行含图片时不能把它交给输入栏，但仍必须把舞台
    // 正文限制在 stageRows；否则最后一行会额外占用一个舞台模型行，表现为
    // 每次 PRINTL/PX_NEWLINE 后舞台行数逐步偏移。
    const int bodyRows = echoExtracted ? total - 1 : total;
    int start = qMax(0, bodyRows - visibleRows);
    if (!echoExtracted && total > visibleRows)
        start = qMax(start, total - visibleRows);
    // A line is only the anchor of an image, not its full drawing extent.
    // Keep earlier anchors whose image still overlaps the current stage. The
    // extra rows scroll above the viewport; ListView clips the image itself.
    const int viewportStart = start;
    // 图锚扫描只看「含图行」索引（newline() 提交时维护）。此前是每次 flush 都扫
    // 0..viewportStart 的**全部**历史行 —— backlog 上限 5000 行（eraMegaten
    // 配置到 50000），存/读档画面每行 flushOut 一次，一次画档 = 40+ 次全量扫描。
    const quint64 firstSerial = m_buffer.firstSerial();
    const quint64 lastSerial = m_buffer.lastSerial();
    for (auto it = m_imageSerials.begin(); it != m_imageSerials.end();) {
        const quint64 serial = *it;
        // 惰性剪枝：头部裁剪/CLEARLINE 产生的死 serial（区间外，
        // 或 serial 对应的行已不在该下标上）就地清除。
        if (serial < firstSerial || serial > lastSerial) {
            it = m_imageSerials.erase(it);
            continue;
        }
        const int row = int(serial - firstSerial);
        // 回显行（最底一行）不参与舞台，不能把它的图锚拉低视口起点。
        if (row >= viewportStart || row >= bodyRows) { ++it; continue; }
        if (m_buffer.at(row).serial != serial) { ++it; continue; }   // 下标与 serial 不对齐留下的洞
        const auto line = displayLine(row);
        for (const auto& segment : line.segments) {
            for (const auto& part : segment.spans) {
                if (part.kind != ConsoleSpanKind::Image) continue;
                const double top = row + (m_lineHeight > 0
                    ? double(part.top) / m_lineHeight : 0.0);
                if (top < total && top + qMax(1, part.rows) > viewportStart)
                    start = qMin(start, row);
            }
        }
        ++it;
    }
    const bool visible = m_stageMode && start < bodyRows;
    QVector<QHash<int, QVariant>> rows;
    QVariantList imageBlocks;
    if (visible) {
        rows.reserve(bodyRows - start);
        for (int row = start; row < bodyRows; ++row) {
            QHash<int, QVariant> item;
            const QVariantList blocks = lineBlocks(row);
            item.insert(BlocksRole, blocks);
            for (const QVariant& value : blocks) {
                QVariantMap image = value.toMap();
                if (image.value(QStringLiteral("kind")).toString() != QStringLiteral("image"))
                    continue;
                image.insert(QStringLiteral("sourceRow"), row);
                imageBlocks.append(image);
            }
            item.insert(LineIndexRole, row);
            item.insert(SpanRowsRole, lineSpanRows(displayLine(row)));
            rows.append(item);
        }
    }
    const bool boundaryChanged = m_stageStart != start || m_stageVisible != visible;
    const bool imagesChanged = m_stageImageBlocks != imageBlocks;
    m_stageImageBlocks = imageBlocks;
    m_stageSnapshot.setRows(rows);
    m_stageStart = start;
    m_stageVisible = visible;
    // 仅在过滤边界变化时更新历史代理和定位；普通行 dataChanged 不重置舞台位置。
    m_historyModel.setStage(start, visible);
    if (boundaryChanged)
        emit stageChanged();
    if (imagesChanged)
        emit stageImageBlocksChanged();
    // 舞台之外的那一行：递交进 QML 的输入回显栏。
    updateInputEchoLine();
    // 模型刷新完成 -> 把这一轮累计的精灵像素变更一次性推给 QML（缓冲区）。
    const QStringList changedSprites = GraphicsStore::takeDirtySpriteNames();
    if (!changedSprites.isEmpty())
        emit spriteImagesChanged(changedSprites);
}

// 窗口最底那一行（输入行）该由谁画：舞台装不下时才摘给输入栏。
// C# 的 echo 来自 doInputToEmueraProgram 末尾的 `Print(str)`：提交值成为正文的
// 一行；era 脚本也会往这一行打提示/按钮。本移植的输入栏不在舞台上，这一行的
// 区块由 updateInputEchoLine() 单独递交，QML 在输入框左侧并排显示。
// 判据见头文件；这里只有一份，舞台快照与回显行共用，避免两边各判一次而错位。
int ConsoleBackend::inputEchoRow() const {
    if (!m_stageInputRowReserved) return -1;
    const int total = rowCount();            // 含未定型的尾行
    if (total <= stageRows()) return -1;     // 没溢出：全部行都在舞台上
    const int row = total - 1;
    // 含图的行走不了这一行文字栏（跨行图在输入框里没有意义），整行留给舞台。
    const ConsoleDisplayLine tail = displayLine(row);
    for (const ConsoleSegment& seg : tail.segments)
        for (const ConsoleSpan& part : seg.spans)
            if (part.kind == ConsoleSpanKind::Image) return -1;
    return row;
}

void ConsoleBackend::updateInputEchoLine() {
    QVariantList line;
    const int echoRow = inputEchoRow();
    if (echoRow >= 0)
        line = lineBlocks(echoRow);
    if (line == m_inputEchoLine)
        return;
    m_inputEchoLine = line;
    emit inputEchoLineChanged();
}

bool ConsoleBackend::anyKeyInput() const {
    return m_waitingInput && inputExpectsAnyKey(m_inputKind);
}

bool ConsoleBackend::stringInput() const {
    return m_waitingInput && inputExpectsString(m_inputKind);
}

// C# InputRequest.OneInput：ONEINPUT/ONEINPUTS/TONEINPUT/TONEINPUTS 四者
// 都是「单字符即时提交」（FunctionIdentifier：TONEINPUT → TINPUT_Instruction(true)）。
bool ConsoleBackend::oneInput() const {
    return m_waitingInput && m_inputKind.toUpper().contains(QLatin1String("ONEINPUT"));
}

void ConsoleBackend::setFramePacingAttached(bool attached) {
    if (m_framePacingAttached == attached) return;
    m_framePacingAttached = attached;
    emit framePacingAttachedChanged();
}

void ConsoleBackend::notifyFramePresented(quint64 generation) {
    if (!m_presentationPending
        || (generation != 0 && generation != m_presentationGeneration))
        return;
    m_presentationPending = false;
    m_presentationFallback.stop();
    emit framePresented();
}

// 输入类型 ↔ 按钮类型的裁决（**唯一事实**，clickAt / QML clickable 都读它）。
// 对齐 C# EmueraConsole：
//   SelectedString（:319-337）接受**任意**按钮 —— 整数按钮返回其 Input.ToString()；
//   SelectedInteger（:1700-1706）才拒绝字符串按钮。
// 所以：字符串输入**不**拒绝整数按钮（此前写反了，BINPUTS 那一屏的 [取消] 点不动）。
bool ConsoleBackend::buttonValueAccepted(bool isIntegerButton) const {
    if (!m_waitingInput) return false;
    if (inputAcceptsAny(m_inputKind)) return true;   // INPUTANY：两种按钮都收
    if (isIntegerButton) return true;                // 整数按钮：任何输入类型都收
    return inputExpectsString(m_inputKind);          // 字符串按钮：只有字符串输入收
}

void ConsoleBackend::notifyInputRequested(const QString& kind, const QVariant& defaultValue) {
    m_inputKind = kind;
    m_inputDefault = defaultValue;
    m_waitingInput = true;
    flush();
    // flush() already publishes the current stage when automatic redraw is enabled.
    // Keep the forced path for REDRAW 0, where the input prompt must still be visible.
    if (!m_autoRedraw)
        updateStageModels(true);
    emit inputRequested(kind);
    emit waitingInputChanged();
    // C# EmueraConsole：限时输入不在语句处启动计时器，等这一帧画完再启动
    //（need_settimer，消费点在 PrintFlush 的绘制路径）。本工程由 QML 视图
    // 把「舞台变更处理完毕」回报回来：这里先请它处理这一帧。
    // 没有视图（test_cli / CTest）时不做闸门，计时器在语句处立即启动。
    if (m_framePacingAttached) {
        ++m_presentationGeneration;
        m_presentationPending = true;
        emit presentationRequested(m_presentationGeneration);
        m_presentationFallback.start();
    }
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

// 提交收尾 = C# doInputToEmueraProgram 的最后两步，顺序相同：
// 写输入（调用方 resume 里做）→ 正文定型（PrintFlush）→ 继续执行。
//
// 占位行必须在 ++m_generation **之前**：C# 的 newGeneration() 在
// callEmueraProgram 末尾（DoScript 之后）才执行，所以提交那一刻定型出来的行
// 属于「提交前那一世代」—— 与屏幕上残留的旧按钮同命（不可点）。顺序反了，
// 这一行就会带**活**按钮，等于凭空多出一个可点的输入候选。
void ConsoleBackend::notifyInputCommitted() {
    if (!m_waitingInput) return;
    if (inputAccountingLineNeeded()) sealInputAccountingLine();
    notifyInputDone();
}

// C# EmueraConsole.PrintFlush(false) 的行效果：把未定型缓冲立刻定型成一条显示行。
// 本移植**不**在这里 Print(str)：提交值由 QML 输入栏显示（TextInput.text 就是
// 回显），正文再打印一份 = 同一事实两份副本。但**行**要留下 —— eraTW 的
// `CLEARLINE 16`（15 行菜单 + 1 行回显）按行数删，少了这行每次刷新多删一行。
void ConsoleBackend::sealInputAccountingLine() {
    newline();
}

// C# doInputToEmueraProgram 只在 state == WaitInput 且 InputType 为
// IntValue/StrValue 时拿到非 null 的 str（WAIT/ANYKEY 系走
// callEmueraProgram("")、INPUTMOUSEKEY 走 callEmueraProgram(null)）；
// 那两条路 C# 结尾的 PrintFlush(false) 在缓冲为空时是空操作，不产出行。
bool ConsoleBackend::inputAccountingLineNeeded() const {
    if (!m_waitingInput) return false;
    if (inputExpectsAnyKey(m_inputKind)) return false;
    return m_inputKind != QLatin1String("INPUTMOUSEKEY");
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
    // C# PrintSingleLine(str, temporary: true)：
    //   1) PrintFlush(false) —— 挂着的未定型缓冲先定型成普通行；
    //   2) printBuffer.Append(str) 并**立即成型**；
    //   3) addDisplayLine —— 若尾行是「一時行」就 deleteLine(1) 顶掉它。
    // 成型与「顶掉舊行」的顺序在 newlineInternal 里：先行成型、再删舊行。
    if (m_pendingOpen || !m_pendingParts.isEmpty() || !m_sealed.isEmpty())
        newline();
    print(text);
    newlineInternal(/*temporary=*/true);
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
    // 一時行下一行就会被顶掉：不为它留缓存条目（键是 serial，留着也不会错，
    // 但会让缓存里堆一批只显示过一帧的行）。
    const bool temporaryTail = row == m_buffer.count() - 1 && lastLineIsTemporary();
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
            // HTML `<div rect='Xpx,...'>` 的**累计像素**左边界（含整行对齐平移）。
            // col 是把像素逐个 qRound 成列的结果：80px / 7px = 11.428…，十张
            // 缩略图会得到 0,11,23,34,46,57 列（而不是 0,11,22,34,45,57 的正确
            // 递进），到第 10 张就差出 3 格。像素坐标只在这里算一次，QML 再按
            // 自己的格宽折算，不在任何一步重新取整。
            // -1 = 没有 div 定位（按 col × 格宽走网格）。
            m.insert("pxLeft", part.positionedXpx >= 0
                                   ? part.positionedXpx + offset * m_layout.columnWidthPx()
                                   : -1);
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
            // ypos 的**像素**值（C#：rect.Y = pointY + raw_ypos*FontSize/100）。
            // 图片层在格子上做位移时用这一份：行决定行首，ypos 是加在行首上的
            // 像素位移，不再折算成「负多少格」再乘行高。
            m.insert("pxTop", part.top);
            // 图片的**原始尺寸**：QML 用它当宽高（位置仍按格子）。
            m.insert("pxWidth", part.imageWidthPx > 0 ? part.imageWidthPx
                                                      : spanCols * m_layout.columnWidthPx());
            m.insert("pxHeight", part.imageHeightPx > 0 ? part.imageHeightPx
                                                        : spanRows * m_lineHeight);
            m.insert("width", spanCols * m_layout.columnWidthPx());
            m.insert("height", spanRows * m_lineHeight);
            // 段的点击信息（同一个段的所有区块共享）
            m.insert("lineIndex", row);                // 点击回传 = 模型行号
            m.insert("segmentIndex", si);
            m.insert("isButton", seg.isButton);
            m.insert("isInteger", seg.isInteger);
            // 绘制缓存只保存按钮自身状态；输入权限由 NOTIFY 属性绑定控制。
            m.insert("clickable", seg.isButton && seg.enabled);
            if (seg.isButton)
                // 按钮值（运行期合法输入）：test_cli 分支选择的兜底候选
                m.insert("btnValue", seg.isInteger ? QVariant(seg.intValue)
                                                   : QVariant(seg.strValue));
            m.insert("tooltip", seg.tooltip);
            if (seg.isButton)
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
    // 输入裁决（buttonValueAccepted 是唯一事实）：不接受的按钮静默忽略 ——
    // 不推进世代（点击无效但按钮保持可重试），杜绝「点击杀死了按钮却没有
    // 产生任何效果」的不确定响应。
    if (!buttonValueAccepted(seg.isInteger)) {
        qDebug() << "[input] 点击忽略（未等待输入或类型不符）kind" << m_inputKind
                 << "等待中" << m_waitingInput << "段为整数" << seg.isInteger;
        return;
    }
    qDebug() << "[input] 点击按钮 行" << lineIndex << "段" << segmentIndex
             << (seg.isInteger ? QStringLiteral("整数=") + QString::number(seg.intValue)
                               : QStringLiteral("字符串=") + seg.strValue);
    submitButton(seg);
}

// 按钮命中后的提交：形态由**等待的输入类型**决定（C# EmueraConsole.SelectedString）：
//   字符串型等待 + 整数按钮 -> 交十进制文本（Input.ToString()）；
//   整数型等待（含 INPUTANY）+ 整数按钮 -> 交整数；
//   字符串按钮 -> 交字符串（整数型等待下已在 buttonValueAccepted 被拒）。
void ConsoleBackend::submitButton(const ConsoleSegment& button) {
    if (!button.isInteger) { submitInputString(button.strValue); return; }
    if (inputExpectsString(m_inputKind)) submitInputString(QString::number(button.intValue));
    else submitInput(button.intValue);
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
    // C# doInputToEmueraProgram：OneInput 时键盘输入只取**第 1 个字符**
    //（`if (inputReq.OneInput && … && inputs.Length > 1) inputs = inputs.Remove(1);`）。
    // 鼠标选择按钮走 submitInput（changedByMouse），不做截断。
    const QString t = oneInput() ? text.trimmed().left(1) : text.trimmed();
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
    // 同上：OneInput 的键盘输入只取第 1 个字符（C# inputs.Remove(1)）
    submitInputString(oneInput() ? text.left(1) : text);
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
    if (m_dirty && !m_timer.isActive())
        m_timer.start();
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
