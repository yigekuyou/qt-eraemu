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
#include "console_layout.h"

#include <QtGlobal>

// ---------------------------------------------------------------------------
// 单位（网格）坐标
//
//   「区块长」= 一个半角字符宽；「区块高」= 一行高。
//   区块的 cols/rows 是**单位数**（动态测量），col/row 是**单位坐标**。
//   像素大小由 QML/容器决定（px = col × cellW 等），所以这里只在需要
//   折行判定时把窗口宽度换算成列数。
//
//   C# 的度量基准（TextDrawingMode=TEXTRENDERER + 等宽日文字体）：
//     半角 = FontSize/2 px，全角 = FontSize px  →  一列 = FontSize/2 px
// ---------------------------------------------------------------------------

// 文本占几个半角单位（全角 2，半角 1）
int ConsoleLayout::measureUnits(const QString& text) const {
    int units = 0;
    for (const QChar c : text) {
        const ushort u = c.unicode();
        units += ((u < 0x80) || (u >= 0xFF61 && u <= 0xFF9F)) ? 1 : 2;
    }
    return units;
}

int ConsoleLayout::maxCols() const {
    return qMax(1, m_windowWidth / qMax(1, columnWidthPx()));
}

void ConsoleLayout::measurePart(ConsoleSpan& part) const {
    switch (part.kind) {
    case ConsoleSpanKind::Image: {
        // 图片宽度：<img width> 是 FontSize 的百分比；缺省按行高占一格见方
        int wPx = part.imageSize.width() > 0
                      ? static_cast<int>(part.imageSize.width() * m_fontSize / 100.0)
                      : 0;
        if (wPx <= 0) wPx = m_fontSize;                  // 缺省 = 1 个全角宽
        part.cols = qMax(1, qRound(wPx / static_cast<double>(columnWidthPx())));
        part.rows = 1;
        part.width = part.cols * columnWidthPx();
        part.height = m_lineHeight;
        part.top = 0;
        part.bottom = m_lineHeight;
        break;
    }
    case ConsoleSpanKind::Shape: {
        int wPx = part.shapeParams.isEmpty()
                      ? 0
                      : (part.shapeParams.first() * m_fontSize / 100);
        part.cols = wPx > 0 ? qMax(1, qRound(wPx / static_cast<double>(columnWidthPx()))) : 0;
        part.rows = 1;
        part.width = part.cols * columnWidthPx();
        part.height = m_lineHeight;
        part.top = 0;
        part.bottom = m_lineHeight;
        break;
    }
    default:
        part.cols = measureUnits(part.text);
        part.rows = 1;
        part.width = part.cols * columnWidthPx();
        part.height = m_lineHeight;
        part.top = 0;
        part.bottom = m_lineHeight;
        break;
    }
}

void ConsoleLayout::layoutSegments(QList<ConsoleSegment>& segments) const {
    int col = 0;
    for (ConsoleSegment& seg : segments) {
        int c = col;
        for (ConsoleSpan& part : seg.spans) {
            if (part.pointXLocked && part.relCol >= 0) {
                c = part.relCol;                 // C# LockPointX：绝对定位
            }
            part.relCol = c;
            if (part.cols <= 0) {
                measurePart(part);               // 动态测量：文本按字符宽度，图/形按参数
            }
            c += part.cols;
        }
        if (!seg.spans.isEmpty()) {
            seg.relCol = seg.spans.first().relCol;
            const ConsoleSpan& last = seg.spans.last();
            seg.cols = qMax(0, last.relCol + last.cols - seg.relCol);
            seg.width = seg.cols * columnWidthPx();
            col = seg.relCol + seg.cols;
        } else {
            seg.relCol = col;
            seg.cols = 0;
            seg.width = 0;
        }
    }
}

int ConsoleLayout::divideIndex(const ConsoleSpan& part, int col) const {
    if (!part.canDivide() || part.text.isEmpty()) return 0;
    const int limit = maxCols() - col;
    if (limit <= 0) return 0;
    int best = 0;
    int used = 0;
    const int n = part.text.size();
    for (int i = 1; i <= n; ++i) {
        used += ((part.text.at(i - 1).unicode() < 0x80
                  || (part.text.at(i - 1).unicode() >= 0xFF61
                      && part.text.at(i - 1).unicode() <= 0xFF9F)) ? 1 : 2);
        if (used <= limit) best = i;
        else break;
    }
    return best;
}

// 填「网格坐标」：relCol 是行内相对列，col/row 是绝对列/行
void ConsoleLayout::placeLine(ConsoleDisplayLine& line, int rowIndex) const {
    layoutSegments(line.segments);
    // 整行平移（对齐）：C# SetAlignment 按窗口宽算
    int offset = 0;
    const int w = line.widthUnits();
    if (line.align == ConsoleAlign::Center) {
        offset = maxCols() / 2 - w / 2;
    } else if (line.align == ConsoleAlign::Right) {
        offset = maxCols() - w;
    }
    if (offset < 0) offset = 0;
    line.pointOffset = offset;

    for (ConsoleSegment& seg : line.segments) {
        seg.col = offset + seg.relCol;
        seg.row = rowIndex;
        for (ConsoleSpan& part : seg.spans) {
            part.relRow = 0;
            part.col = offset + part.relCol;
            part.row = rowIndex;
            if (part.rows <= 0) part.rows = 1;
        }
    }
}

// 对齐 C# PrintStringBuffer.ButtonsToDisplayLines 的骨架（按**列数**折行）
QList<ConsoleDisplayLine> ConsoleLayout::wrapSegments(QList<ConsoleSegment> segments,
                                                      ConsoleAlign align) const {
    QList<ConsoleDisplayLine> out;
    if (segments.isEmpty()) return out;

    const int limitCols = maxCols();
    QList<ConsoleSegment> current;

    const auto emitLine = [&](QList<ConsoleSegment> segs, bool logical) {
        ConsoleDisplayLine line;
        line.segments = std::move(segs);
        line.align = align;
        line.isLogicalLine = logical;
        out.append(line);
    };
    const auto relayoutFrom = [&](int from) {
        QList<ConsoleSegment> rest;
        for (int i = from; i < segments.size(); ++i) rest.append(segments.at(i));
        layoutSegments(rest);
        for (int i = 0; i < rest.size(); ++i) segments.replace(from + i, rest.at(i));
    };

    layoutSegments(segments);
    for (int i = 0; i < segments.size(); ++i) {
        const ConsoleSegment& seg = segments.at(i);
        if (current.isEmpty() || seg.relCol + seg.cols <= limitCols) {
            current.append(seg);
            continue;
        }
        // 段内切分
        int headCount = 0;
        bool canCut = false;
        if (!seg.isButton) {
            int c = seg.relCol;
            for (int k = 0; k < seg.spans.size(); ++k) {
                const ConsoleSpan& part = seg.spans.at(k);
                if (c + part.cols <= limitCols) { c += part.cols; continue; }
                if (part.canDivide() && divideIndex(part, c) > 0) {
                    headCount = k + 1;
                    canCut = true;
                }
                break;
            }
        }
        if (canCut) {
            ConsoleSegment head = seg;
            ConsoleSegment tail = seg;
            head.spans.clear();
            tail.spans.clear();
            for (int k = 0; k < seg.spans.size(); ++k) {
                ConsoleSpan part = seg.spans.at(k);
                if (k < headCount - 1) {
                    head.spans.append(part);
                } else if (k == headCount - 1) {
                    const int n = divideIndex(part, part.relCol);
                    ConsoleSpan a = part;
                    ConsoleSpan b = part;
                    a.text = part.text.left(n);
                    a.cols = -1;
                    b.text = part.text.mid(n);
                    b.cols = -1;
                    a.width = -1;
                    b.width = -1;
                    head.spans.append(a);
                    tail.spans.append(b);
                } else {
                    tail.spans.append(part);
                }
            }
            if (!head.spans.isEmpty()) {
                current.append(head);
                emitLine(current, out.isEmpty());
                current.clear();
            }
            segments.replace(i, tail);
            relayoutFrom(i);
            --i;
            continue;
        }
        emitLine(current, out.isEmpty());
        current.clear();
        relayoutFrom(i);
        --i;
    }
    if (!current.isEmpty()) emitLine(current, out.isEmpty());
    return out;
}
