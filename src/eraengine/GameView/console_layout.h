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
#ifndef CONSOLE_LAYOUT_H
#define CONSOLE_LAYOUT_H

#include <QString>
#include <QList>
#include "console_types.h"

// ---------------------------------------------------------------------------
// ConsoleLayout —— 行内排版（单位 = 区块长 / 区块高）
//
//   * 坐标系是**相对网格坐标**：列（单位 = 一个半角字符宽）/ 行（单位 = 一行高）；
//   * 区块大小（cols/rows）由 C++ **动态测量**：文本按字符宽度（全角 2、半角 1），
//     图片/图形按 <img>、<shape> 的百分比参数；
//   * **像素大小由 QML/容器决定**：QML 用自己的 cell 宽高换算
//     （px = col × cellW…）。C++ 只把窗口宽度换算成列数用于折行判定。
// ---------------------------------------------------------------------------
class ConsoleLayout {
public:
    // 可选：注入「像素 → 单位」的换算后端。默认用等宽估算
    // （半角 = FontSize/2 px）。注入后 cols 仍以单位计，只影响窗口列数与像素参考值。
    using MeasureFn = int (*)(const QString&, const ConsoleStyle&, int fontSize);

    void setFontSize(int px) { if (px > 0) m_fontSize = px; }
    [[nodiscard]] int fontSize() const { return m_fontSize; }

    void setLineHeight(int px) { if (px > 0) m_lineHeight = px; }
    [[nodiscard]] int lineHeight() const { return m_lineHeight; }

    void setWindowWidth(int px) { if (px > 0) m_windowWidth = px; }
    [[nodiscard]] int windowWidth() const { return m_windowWidth; }

    // 一个「区块长」（列）等于多少像素 —— QML 侧也应取同一个值
    [[nodiscard]] int columnWidthPx() const { return qMax(1, m_fontSize / 2); }
    // 逻辑网格固定后，窗口变化只改变 QML 的像素格子大小。
    void setGridColumns(int columns) { if (columns > 0) m_gridColumns = columns; }
    [[nodiscard]] int gridColumns() const { return m_gridColumns; }
    void setGridRows(int rows) { if (rows > 0) m_gridRows = rows; }
    [[nodiscard]] int gridRows() const { return m_gridRows; }
    // root 全平面的固定列数
    [[nodiscard]] int maxCols() const;

    // ---- 单位（网格）换算 ----
    [[nodiscard]] int measureUnits(const QString& text) const;

    void setButtonWrap(bool on) { m_buttonWrap = on; }
    void setCompatiLinefeedAs1739(bool on) { m_compatiLinefeed = on; }

    // ---- 排版 ----
    // 段内区块填 relCol/cols（相对行），段填 relCol/cols
    void layoutSegments(QList<ConsoleSegment>& segments) const;
    // 动态测量一个区块的格子数（cols/rows）与像素参考值
    void measurePart(ConsoleSpan& part) const;
    // 填绝对网格坐标：col = 对齐平移 + relCol，row = rowIndex
    void placeLine(ConsoleDisplayLine& line, int rowIndex) const;
    // 按最大列数折行（C# ButtonsToDisplayLines）
    [[nodiscard]] QList<ConsoleDisplayLine> wrapSegments(QList<ConsoleSegment> segments,
                                                         ConsoleAlign align) const;
    // C# getDivideIndex：一个文本区块在列限内能放下的字符数
    [[nodiscard]] int divideIndex(const ConsoleSpan& part, int col) const;

private:
    int  m_fontSize = 18;
    int  m_lineHeight = 19;
    int  m_windowWidth = 760;
    int  m_gridColumns = 80;
    int  m_gridRows = 25;
    bool m_buttonWrap = true;
    bool m_compatiLinefeed = false;
};

#endif // CONSOLE_LAYOUT_H
