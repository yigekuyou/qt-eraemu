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
#ifndef CONSOLE_TYPES_H
#define CONSOLE_TYPES_H

#include <QString>
#include <QStringList>
#include <QColor>
#include <QList>
#include <QVariantMap>
#include <QVariantList>
#include <QSizeF>

// ---------------------------------------------------------------------------
// 控制台显示数据模型 —— 与 C# GameView 一一对应
//
//   C# AConsoleDisplayPart              -> ConsoleSpan      （最小单位区块）
//   C# ConsoleStyledString/Image/Shape  -> ConsoleSpan.kind
//   C# ConsoleButtonString              -> ConsoleSegment   （可点击段）
//   C# ConsoleDisplayLine               -> ConsoleDisplayLine（显示行）
//
// 层级（对齐 C# GameView/PrintStringBuffer.cs 顶部注释）：
//     ConsoleSpan  = 文本/图/形 + 样式 + 行内位置与宽度
//     ConsoleSegment = ConsoleSpan[] + 点击值 + 世代
//     ConsoleDisplayLine = ConsoleSegment[]
//
// 「最小单位区块」是 ConsoleSpan：它自带 pointX/width，QML 只需要为一
// 个 span 创建一个可视对象并按 pointX 摆放（也可以忽略 pointX 用 Row
// 自然排版）。段（ConsoleSegment）负责「是否可点击 / 点击值 / 世代」。
// ---------------------------------------------------------------------------

enum class ConsoleAlign { Left = 0, Center = 1, Right = 2 };

// C# StringStyle（GameView/StringStyle.cs）
struct ConsoleStyle {
    QColor  color;              // 前景色（无效表示用默认前景）
    QColor  buttonColor;        // 选中/悬停高亮色（无效表示默认）
    bool    bold = false;
    bool    italic = false;
    bool    underline = false;
    bool    strike = false;
    QString fontName;
    // C# ConsoleStyledString.colorChanged：这一段是否被显式改过颜色
    // （历史/日志模式下「没改过色」的段用 Config.LogColor 画）
    bool    colorChanged = false;

    bool operator==(const ConsoleStyle& o) const {
        return color == o.color && buttonColor == o.buttonColor && bold == o.bold
               && italic == o.italic && underline == o.underline && strike == o.strike
               && fontName == o.fontName;
    }
    bool operator!=(const ConsoleStyle& o) const { return !(*this == o); }
};

enum class ConsoleSpanKind { Text, Image, Shape };

// ---------------------------------------------------------------------------
// ConsoleSpan —— 最小单位区块（C# AConsoleDisplayPart）
// ---------------------------------------------------------------------------
struct ConsoleSpan {
    ConsoleSpanKind kind = ConsoleSpanKind::Text;
    QString  text;              // Text：内容；Image：资源名；Shape：形状类型
    ConsoleStyle style;
    QString  raw;               // 原始文本（调试/回退）
    QString  altText;           // Image/Shape 取不到资源时的替代文本（C# AltText）

    // ---- 相对网格坐标（单位：区块长 / 区块高）----
    //   「区块长」= 一个半角字符宽，「区块高」= 一行高。
    //   C++ 只给**网格坐标与格子数**；**最终像素大小由 QML 决定**
    //   （QML 用自己的 cell 宽高换算：px = col * cellW, py = row * cellH）。
    //
    //   绝对坐标：col / row        —— 相对 root 全平面
    //   相对坐标：relCol / relRow  —— 相对所属「行」（QML 想按行容器摆放时用）
    //   格子数  ：cols / rows      —— 区块占几个单位（动态测量：文本按字符宽度，
    //                                 图片按目标尺寸、图形按百分比参数）
    int   col     = -1;         // 绝对列（单位 = 半角字符宽；<0 = 未布局）
    int   row     = 0;          // 绝对行（单位 = 行高）
    int   relCol  = -1;         // 行内相对列
    int   relRow  = 0;          // 行内相对行（恒 0）
    int   cols    = 0;          // 宽（单位数）
    int   rows    = 1;          // 高（单位数）

    // ---- 像素参考值（按当前字体测出来的「建议像素」，QML 可忽略）----
    int   width    = -1;
    int   height   = -1;
    float xSubPixel = 0.0f;     // 亚像素累积（C# XsubPixel）
    bool  pointXLocked = false; // <img pos=…> / <shape> 的绝对定位
    int   top = 0;              // 命中测试用（C# Top/Bottom）
    int   bottom = 0;

    // 兼容旧名（= relCol）
    [[nodiscard]] int pointX() const { return relCol; }

    // ---- Image / Shape 附加参数 ----
    int      imageId = -1;      // 资源 id（-1 = 未解析）
    QSizeF   imageSize;         // 目标尺寸（<=0 表示按行高自适应）
    // imageSize 是**像素**（取自资源固有尺寸）还是**字号百分比**（<img width=N>）。
    // Emuera 的 <img width=N> 里 N 是相对字号的百分比；而 `<img src='X'>` 不带尺寸时
    // 必须用资源自身像素尺寸，否则整张图会被压成一个字号见方。
    bool     imageSizeIsPixels = false;
    QString  shapeType;         // "space" | "rect" | "line" | "polygon"
    QList<int> shapeParams;     // 百分比参数（× FontSize / 100）
    bool     error = false;     // 字体/资源异常（C# part.Error）

    [[nodiscard]] bool canDivide() const { return kind == ConsoleSpanKind::Text && !error; }

    // 图层名：QML 侧按 root / text / image（含图形）分三层渲染
    [[nodiscard]] static QString layerName(ConsoleSpanKind k) {
        switch (k) {
        case ConsoleSpanKind::Image: return QStringLiteral("image");
        case ConsoleSpanKind::Shape: return QStringLiteral("shape");
        default:                     return QStringLiteral("text");
        }
    }
    [[nodiscard]] QString layer() const { return layerName(kind); }
    [[nodiscard]] int displayWidth() const { return width > 0 ? width : 0; }

    QVariantMap toVariantMap() const {
        QVariantMap m;
        switch (kind) {
        case ConsoleSpanKind::Image: m.insert("kind", "image"); break;
        case ConsoleSpanKind::Shape: m.insert("kind", "shape"); break;
        default:                     m.insert("kind", "text");  break;
        }
        m.insert("text", text);
        m.insert("altText", altText);
        // 网格坐标（单位 = 区块长 / 区块高）—— QML 用它 × 自己的单元格大小
        m.insert("col", col);
        m.insert("row", row);
        m.insert("cols", cols);
        m.insert("rows", rows);
        m.insert("relCol", relCol);
        m.insert("relRow", relRow);
        // 像素参考值（可忽略）
        m.insert("w", width);
        m.insert("h", height);
        m.insert("xSubPixel", xSubPixel);
        m.insert("pointXLocked", pointXLocked);
        m.insert("top", top);
        m.insert("bottom", bottom);
        m.insert("imageId", imageId);
        m.insert("imageW", imageSize.width());
        m.insert("imageH", imageSize.height());
        m.insert("shapeType", shapeType);
        QVariantList params;
        for (int p : shapeParams) params.append(p);
        m.insert("shapeParams", params);
        if (style.color.isValid())       m.insert("color", style.color.name(QColor::HexArgb));
        if (style.buttonColor.isValid()) m.insert("buttonColor", style.buttonColor.name(QColor::HexArgb));
        m.insert("bold", style.bold);
        m.insert("italic", style.italic);
        m.insert("underline", style.underline);
        m.insert("strike", style.strike);
        if (!style.fontName.isEmpty()) m.insert("fontName", style.fontName);
        m.insert("colorChanged", style.colorChanged);
        m.insert("error", error);
        return m;
    }
};

// ---------------------------------------------------------------------------
// ConsoleSegment —— 可点击段（C# ConsoleButtonString）
//
// 一个显示行由若干段组成：文本段（IsButton=false）与按钮段（IsButton=true）。
// `generation` 与 EmueraConsole 的世代机制对应：新一批输出/新的 INPUT
// 之后，旧世代的段不再可点击。
// ---------------------------------------------------------------------------
struct ConsoleSegment {
    QList<ConsoleSpan> spans;
    bool    isButton  = false;
    bool    isInteger = false;      // 点击值是整数（INPUT 才可用）
    qint64  intValue  = 0;
    QString strValue;
    QString tooltip;                // C# Title（ToolTip）
    quint64 generation = 0;
    bool    enabled = true;
    bool    errPos  = false;        // C# ErrPos != null（错误行按钮）

    // ---- 网格坐标（单位 = 区块长 / 区块高）----
    int relCol = 0;                 // 行内相对列
    int col    = 0;                 // 绝对列
    int row    = 0;                 // 绝对行
    int cols   = 0;                 // 宽（单位数）
    int width  = 0;                 // 像素参考宽

    [[nodiscard]] QString plainText() const {
        QString s;
        for (const ConsoleSpan& sp : spans) {
            if (sp.kind == ConsoleSpanKind::Text) s += sp.text;
        }
        return s;
    }

    // 这一段是否含某个图层的区块（QML 分层渲染时用来分发）
    [[nodiscard]] bool hasLayer(const QString& layer) const {
        for (const ConsoleSpan& sp : spans) {
            if (ConsoleSpan::layerName(sp.kind) == layer) return true;
        }
        return false;
    }

    QVariantMap toVariantMap() const {
        QVariantList parts;
        parts.reserve(spans.size());
        for (const ConsoleSpan& sp : spans) parts.append(sp.toVariantMap());
        QVariantMap m;
        m.insert("parts", parts);
        m.insert("isButton", isButton);
        m.insert("isInteger", isInteger);
        m.insert("intValue", intValue);
        m.insert("strValue", strValue);
        m.insert("tooltip", tooltip);
        m.insert("generation", QVariant::fromValue<qulonglong>(generation));
        m.insert("enabled", enabled && isButton);
        m.insert("relCol", relCol);
        m.insert("col", col);
        m.insert("row", row);
        m.insert("cols", cols);
        m.insert("width", width);
        m.insert("plain", plainText());
        return m;
    }
};

// ---------------------------------------------------------------------------
// ConsoleDisplayLine —— 显示行（C# ConsoleDisplayLine）
// ---------------------------------------------------------------------------
struct ConsoleDisplayLine {
    QList<ConsoleSegment> segments;
    ConsoleAlign align = ConsoleAlign::Left;
    int  lineNo = -1;                  // 全局行号（C# LineNo）
    bool isLogicalLine = true;         // 折行产生的续行为 false
    bool isTemporary = false;          // 临时行（下一行加入时被顶掉）
    int  pointOffset = 0;              // 对齐产生的整行平移（C# SetAlignment）

    [[nodiscard]] bool isEmpty() const { return segments.isEmpty(); }

    // 扁平化视图（调试/测试用）：把每段的 span 顺序摊开
    [[nodiscard]] QList<ConsoleSpan> flatSpans() const {
        QList<ConsoleSpan> out;
        for (const ConsoleSegment& seg : segments) out += seg.spans;
        return out;
    }
    [[nodiscard]] int spanCount() const {
        int n = 0;
        for (const ConsoleSegment& seg : segments) n += seg.spans.size();
        return n;
    }
    // 行内可点击段的索引列表
    [[nodiscard]] QList<int> buttonIndices() const {
        QList<int> out;
        for (int i = 0; i < segments.size(); ++i) {
            if (segments.at(i).isButton) out.append(i);
        }
        return out;
    }

    [[nodiscard]] QString plainText() const {
        QString s;
        for (const ConsoleSegment& seg : segments) s += seg.plainText();
        return s;
    }

    [[nodiscard]] int width() const {
        int w = 0;
        for (const ConsoleSegment& seg : segments) w += seg.width;
        return w;
    }
    // 行宽（单位 = 区块长）
    [[nodiscard]] int widthUnits() const {
        int w = 0;
        for (const ConsoleSegment& seg : segments) w += seg.cols;
        return w;
    }

    QVariantMap toVariantMap() const {
        QVariantList segs;
        segs.reserve(segments.size());
        for (const ConsoleSegment& seg : segments) segs.append(seg.toVariantMap());
        QVariantMap out;
        out.insert("segments", segs);
        out.insert("align", static_cast<int>(align));
        out.insert("lineNo", lineNo);
        out.insert("isLogicalLine", isLogicalLine);
        out.insert("temporary", isTemporary);
        out.insert("pointOffset", pointOffset);
        out.insert("width", width());
        out.insert("cols", widthUnits());
        out.insert("kind", segments.size() == 1 && segments.first().isButton
                               ? "button" : "text");
        out.insert("plain", plainText());
        return out;
    }
};

#endif // CONSOLE_TYPES_H
