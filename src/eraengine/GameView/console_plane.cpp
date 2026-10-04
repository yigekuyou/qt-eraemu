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
#include "console_plane.h"

#include <QVector>
#include <QtGlobal>

namespace {

struct PlaneCell {
    QString text;
    int units = 0;      // > 0: glyph origin; 0: empty; -1: wide-glyph continuation
};

// 半角 1 格 / 全角 2 格（与 ConsoleLayout 的度量同一套单位）
inline bool isHalfWidthChar(QChar c) {
    const ushort u = c.unicode();
    return (u < 0x80) || (u >= 0xFF61 && u <= 0xFF9F);
}

inline int unitWidth(QChar c) {
    return isHalfWidthChar(c) ? 1 : 2;
}

// 歧义宽度符号 → 2 个 ASCII 字符（保证终端里恒为 2 格）
QString ambiguousSubstitute(QChar c) {
    switch (c.unicode()) {
    case 0x25A0: return QStringLiteral("##");   // ■ 实心（方块/墙）
    case 0x25A1: return QStringLiteral("..");   // □ 空心（空白格）
    case 0x25AA: case 0x25AB:            // ▪ ▫
        return QStringLiteral("[]");
    case 0x25A8: case 0x25A9:            // ▨ ▩（图片占位）
    case 0x25A4: case 0x25A5:            // ▤ ▥
        return QStringLiteral("@@");
    case 0x25CF: case 0x25CB:            // ● ○
        return QStringLiteral("()");
    case 0x25C6: case 0x25C7:            // ◆ ◇
        return QStringLiteral("<>");
    case 0x2500: case 0x2501:            // ─ ━（图形占位）
    case 0x2015: case 0x2014:            // ― —（DRAWLINE）
    case 0x2010: case 0x2013:            // ‐ –
        return QStringLiteral("--");
    case 0x2190: return QStringLiteral("<-");   // ←
    case 0x2191: return QStringLiteral("^^");   // ↑
    case 0x2192: return QStringLiteral("->");   // →
    case 0x2193: return QStringLiteral("vv");   // ↓
    case 0x00B7: case 0x30FB:            // · ・
        return QStringLiteral("..");
    default:
        break;
    }
    return QStringLiteral("??");
}

// 图片/图形在平面里的单个占位符；栅格化时按 part.cols 填满。
QString blockGlyphs(const ConsoleSpan& part, const ConsolePlaneOptions& opt) {
    switch (part.kind) {
    case ConsoleSpanKind::Image:
        return opt.imageMark;
    case ConsoleSpanKind::Shape:
        return opt.shapeMark;
    default:
        return part.text;
    }
}

QString debugSubstitute(QChar c, int col, bool withColor) {
    QString text;
    switch (c.unicode()) {
    case 0x25A0: text = QStringLiteral("##"); break;
    case 0x25A1: text = QStringLiteral(".."); break;
    default:     text = ambiguousSubstitute(c); break;
    }
    if (!withColor) return text;

    static constexpr int colors[] = {31, 32, 34, 33};
    return QStringLiteral("\033[%1m%2\033[0m").arg(colors[qAbs(col) % 4]).arg(text);
}

void clearCell(QVector<PlaneCell>& row, int col) {
    if (col < 0 || col >= row.size() || row.at(col).units == 0) return;

    int origin = col;
    while (origin > 0 && row.at(origin).units < 0) --origin;
    const int units = qMax(1, row.at(origin).units);
    for (int i = origin; i < origin + units && i < row.size(); ++i) row[i] = {};
}

void putGlyph(QVector<PlaneCell>& row, int col, int units, const QString& text) {
    if (col < 0 || units <= 0) return;
    if (row.size() < col + units) row.resize(col + units);
    for (int i = col; i < col + units; ++i) clearCell(row, i);
    row[col].text = text;
    row[col].units = units;
    for (int i = 1; i < units; ++i) row[col + i].units = -1;
}

// 按绝对网格列写入一个 span。part.cols 是唯一宽度来源，终端替换文本
// 和 ANSI 转义序列不能改变后续 span 的起始列。
void putPart(QVector<PlaneCell>& row, const ConsoleSpan& part,
             const ConsolePlaneOptions& opt) {
    const int start = qMax(0, part.col);
    const int width = qMax(0, part.cols);
    const int end = start + width;
    int col = start;

    const QString glyphs = blockGlyphs(part, opt);
    const auto displayText = [&](QChar c, int glyphCol) {
        QString text = QString(c);
        if (opt.debugCompare && ConsolePlane::isAmbiguousWidth(c)) {
            text = debugSubstitute(c, glyphCol, opt.debugColor);
        }
        if (opt.terminalSafe && ConsolePlane::isAmbiguousWidth(c)) {
            text = ambiguousSubstitute(c);
        }
        if (opt.ansiColors && part.style.color.isValid()) {
            const QColor color = part.style.color;
            text = QStringLiteral("\033[38;2;%1;%2;%3m%4\033[39m")
                       .arg(color.red()).arg(color.green()).arg(color.blue()).arg(text);
        }
        return text;
    };

    for (const QChar c : glyphs) {
        const int units = unitWidth(c);
        if (col + units > end) break;
        putGlyph(row, col, units, displayText(c, col));
        col += units;
    }

    // 图片和图形表示矩形区块，需要用单个标记铺满模型声明的宽度。
    if (part.kind != ConsoleSpanKind::Text && !glyphs.isEmpty()) {
        const QChar marker = glyphs.at(0);
        while (col < end) {
            const int units = qMin(unitWidth(marker), end - col);
            const QString text = units == unitWidth(marker)
                                     ? displayText(marker, col)
                                     : QStringLiteral("?");
            putGlyph(row, col, units, text);
            col += units;
        }
    }
}

} // namespace

bool ConsolePlane::isWideChar(QChar c) {
    return !isHalfWidthChar(c);
}

// East-Asian Ambiguous：常见区间是「一般标点 / 箭头 / 几何图形 / 制表符」
bool ConsolePlane::isAmbiguousWidth(QChar c) {
    const ushort u = c.unicode();
    return u >= 0x2000 && u <= 0x2BFF;
}

QString ConsolePlane::toTerminalSafe(const QString& text) {
    QString out;
    out.reserve(text.size() * 2);
    for (const QChar c : text) {
        if (isAmbiguousWidth(c)) {
            out += ambiguousSubstitute(c);      // 2 个 ASCII 字符 = 2 格
        } else {
            out += c;                            // 半角 / CJK 全角（终端可靠地按 1/2 格）
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 平面 = 网格本身
//
// 因为坐标就是「区块长 / 区块高」单位，平面重建不需要任何像素换算：
// 直接把每个区块按 (col,row) 落到矩阵上即可。
// ---------------------------------------------------------------------------
QStringList ConsolePlane::render(const ConsoleBuffer& buffer, const ConsoleLayout& layout,
                                 const Options& opt) {
    QVector<QVector<PlaneCell>> rows;

    const int n = buffer.count();
    for (int i = 0; i < n; ++i) {
        ConsoleDisplayLine line = buffer.at(i);
        layout.placeLine(line, i);              // 行号 = 缓冲行号（单位 = 区块高）
        for (const ConsoleSegment& seg : line.segments) {
            for (const ConsoleSpan& part : seg.spans) {
                const int r = part.row;
                if (r < 0) continue;
                while (rows.size() <= r) rows.append(QVector<PlaneCell>());
                putPart(rows[r], part, opt);
            }
        }
    }

    QStringList out;
    const int limit = (opt.maxLines > 0) ? qMin(opt.maxLines, static_cast<int>(rows.size()))
                                         : static_cast<int>(rows.size());
    for (int i = 0; i < limit; ++i) {
        const QVector<PlaneCell>& cells = rows.at(i);
        int used = cells.size();
        while (used > 0 && cells.at(used - 1).units == 0) --used;

        QString row;
        for (int col = 0; col < used; ++col) {
            const PlaneCell& cell = cells.at(col);
            if (cell.units > 0) row += cell.text;
            else if (cell.units == 0) row += QLatin1Char(' ');
        }
        if (opt.withWidths) {
            // 宽度直接来自网格右界，不受 ANSI 转义或替换文本长度影响。
            row += QStringLiteral("  |%1").arg(used);
        }
        out.append(row);
    }
    return out;
}

QStringList ConsolePlane::renderWithRuler(const ConsoleBuffer& buffer,
                                          const ConsoleLayout& layout, const Options& opt) {
    QStringList out;
    QString ruler;
    const int cols = qMax(1, opt.windowWidth / qMax(1, layout.columnWidthPx()));
    for (int c = 0; c < cols; ++c) {
        ruler += QLatin1Char((c % 10 == 0) ? '|' : '-');
    }
    out.append(ruler);
    out += render(buffer, layout, opt);
    return out;
}

QList<ConsolePlaneIssue> ConsolePlane::inspect(const ConsoleBuffer& buffer,
                                               const ConsoleLayout& layout,
                                               const Options& opt) {
    QList<Issue> issues;
    const int maxCols = qMax(1, opt.windowWidth / qMax(1, layout.columnWidthPx()));
    const int lineHeight = qMax(1, layout.lineHeight());

    const int n = buffer.count();
    for (int i = 0; i < n; ++i) {
        ConsoleDisplayLine line = buffer.at(i);
        layout.placeLine(line, i);

        for (const ConsoleSegment& seg : line.segments) {
            int colCursor = -1;
            for (const ConsoleSpan& part : seg.spans) {
                // 1) 区块必须落在 root 平面内
                if (part.col + qMax(1, part.cols) > maxCols) {
                    issues.append({i, QStringLiteral("区块越出 root 宽度：列 %1..%2 > %3 列（\"%4\"）")
                                          .arg(part.col)
                                          .arg(part.col + qMax(1, part.cols) - 1)
                                          .arg(maxCols)
                                          .arg(part.text.left(12))});
                }
                // 2) 网格坐标/尺寸必须合法
                if (part.cols <= 0 && part.kind != ConsoleSpanKind::Shape) {
                    issues.append({i, QStringLiteral("区块宽度为 0 单位（%1）")
                                          .arg(part.text.left(20))});
                }
                // 图片区块天然跨多行（eraTW 立絵是 176px 高的 sprite），
                // 「1 行 · 像素高=行高」这两条启发式只对文本/形状成立。
                const bool isImage = part.kind == ConsoleSpanKind::Image;
                if (!isImage && part.rows != 1) {
                    issues.append({i, QStringLiteral("区块高度应为 1 行（rows=%1）")
                                          .arg(part.rows)});
                }
                if (part.relRow != 0) {
                    issues.append({i, QStringLiteral("区块的相对行 relRow 应为 0，实为 %1")
                                          .arg(part.relRow)});
                }
                // 3) 段内区块必须首尾相接（同段是一个整体，不能有洞/重叠）
                if (colCursor >= 0 && part.relCol < colCursor) {
                    issues.append({i, QStringLiteral("段内区块重叠：列 %1 < 前一区块右界 %2")
                                          .arg(part.relCol).arg(colCursor)});
                }
                colCursor = part.relCol + qMax(1, part.cols);
            }
        }

        // 4) 行的 Y 单位必须与行高对齐（row 是单位行号，因此恒成立；这里校验像素参考值）
        for (const ConsoleSegment& seg : line.segments) {
            for (const ConsoleSpan& part : seg.spans) {
                if (part.kind == ConsoleSpanKind::Image) continue;   // 图片像素高 ≠ 行高（见上）
                if (part.height > 0 && part.height != lineHeight) {
                    issues.append({i, QStringLiteral("区块像素高 %1 ≠ 行高 %2")
                                          .arg(part.height).arg(lineHeight)});
                }
            }
        }
    }
    return issues;
}

QStringList ConsolePlane::issueTexts(const QList<ConsolePlaneIssue>& issues) {
    QStringList out;
    out.reserve(issues.size());
    for (const ConsolePlaneIssue& x : issues) {
        out << QStringLiteral("第 %1 行: %2").arg(x.line).arg(x.message);
    }
    return out;
}
