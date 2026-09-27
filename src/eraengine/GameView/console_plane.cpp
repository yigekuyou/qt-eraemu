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

// 图片/图形在平面里的占位符（按单位数重复）
QString blockGlyphs(const ConsoleSpan& part, const ConsolePlaneOptions& opt) {
    const int cols = qMax(1, part.cols);
    switch (part.kind) {
    case ConsoleSpanKind::Image:
        return QString(opt.imageMark.repeated(cols));
    case ConsoleSpanKind::Shape:
        return QString(opt.shapeMark.repeated(cols));
    default:
        return part.text;
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
    QVector<QString> rows;
    const int maxCols = qMax(1, opt.windowWidth / qMax(1, layout.columnWidthPx()));
    int lastRow = -1;

    const int n = buffer.count();
    for (int i = 0; i < n; ++i) {
        ConsoleDisplayLine line = buffer.at(i);
        layout.placeLine(line, i);              // 行号 = 缓冲行号（单位 = 区块高）
        for (const ConsoleSegment& seg : line.segments) {
            for (const ConsoleSpan& part : seg.spans) {
                const int r = part.row;
                if (r < 0) continue;
                while (rows.size() <= r) rows.append(QString());
                if (r > lastRow) lastRow = r;
                QString& row = rows[r];
                // 补齐到起始列
                const int cur = row.size() >= 0 ? row.size() : 0;
                Q_UNUSED(cur);
                const int curUnits = [&row]() {
                    int u = 0;
                    for (const QChar c : row) u += unitWidth(c);
                    return u;
                }();
                if (part.col > curUnits) {
                    row += QString(part.col - curUnits, QLatin1Char(' '));
                } else if (part.col < curUnits) {
                    row += QChar(0x001B);          // 重叠标记（inspect 会报）
                }
                const QString glyphs = blockGlyphs(part, opt);
                row += opt.terminalSafe ? toTerminalSafe(glyphs) : glyphs;
            }
        }
    }

    QStringList out;
    const int limit = (opt.maxLines > 0) ? qMin(opt.maxLines, static_cast<int>(rows.size()))
                                         : static_cast<int>(rows.size());
    for (int i = 0; i < limit; ++i) {
        QString row = rows.at(i);
        if (opt.withWidths) {
            // 行尾附上「本行实际占的单位数」，与终端字体无关 —— 用来核对列对齐
            const int units = [&row]() {
                int u = 0;
                for (const QChar c : row) {
                    if (c == QChar(0x001B)) continue;      // 重叠标记
                    u += unitWidth(c);
                }
                return u;
            }();
            row += QStringLiteral("  |%1").arg(units);
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
                if (part.rows != 1) {
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
