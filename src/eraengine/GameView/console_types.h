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
#include <QColor>
#include <QList>
#include <QVariantMap>
#include <QVariantList>
#include <QSizeF>

// ---------------------------------------------------------------------------
// 控制台显示数据模型（对齐 C# GameView）
//
//   ConsoleStyledString -> ConsoleSpan   （文本 + 样式）
//   （内联图/图形）      -> ConsoleSpan.kind = "image" | "shape"
//   ConsoleButtonString  -> ConsoleButton （可点击区域）
//   ConsoleDisplayLine(行) -> ConsoleDisplayLine （一行 = 若干 span + 若干 button）
//
// 这些是「值类型」：C++ 侧持有数据，QML 只读渲染。
// ---------------------------------------------------------------------------

enum class ConsoleAlign { Left = 0, Center = 1, Right = 2 };

struct ConsoleStyle {
    QColor  color;              // 前景色（无效表示用默认前景）
    QColor  buttonColor;        // 选中/悬停高亮色（无效表示默认）
    bool    bold = false;
    bool    italic = false;
    bool    underline = false;
    bool    strike = false;
    QString fontName;

    bool colorChanged() const { return color.isValid(); }
};

enum class ConsoleSpanKind { Text, Image, Shape };

struct ConsoleSpan {
    ConsoleSpanKind kind = ConsoleSpanKind::Text;
    QString  text;              // Text：内容；Image：资源名；Shape：类型
    ConsoleStyle style;
    QString  raw;               // 原始文本（调试/回退）

    // Image / Shape 附加参数
    int      imageId = -1;      // 资源 id（由资源缓存分配；-1 表示未解析）
    QSizeF   imageSize;         // 目标尺寸（<=0 表示按行高自适应）
    QString  shapeType;         // "space" | "rect" | "line" ...
    QList<int> shapeParams;     // 百分比参数
};

struct ConsoleButton {
    int     startSpan = 0;
    int     spanCount = 0;
    bool    isInteger = false;
    qint64  intValue = 0;
    QString strValue;
    QString tooltip;
    quint64 generation = 0;     // 新一批输出后旧按钮失效（对齐 C# Generation）
    bool    enabled = true;
};

struct ConsoleDisplayLine {
    QList<ConsoleSpan>    spans;
    QList<ConsoleButton>  buttons;
    ConsoleAlign          align = ConsoleAlign::Left;
    bool  isLogicalLine = true;   // 折行产生的续行为 false（决定对齐作用范围）
    bool  temporary = false;      // 临时行（等待输入时被替换）

    QString plainText() const {
        QString s;
        for (const ConsoleSpan& sp : spans) {
            if (sp.kind == ConsoleSpanKind::Text) s += sp.text;
        }
        return s;
    }

    // 供 QML 读取的扁平结构
    QVariantMap toVariantMap() const {
        QVariantList spanList;
        spanList.reserve(spans.size());
        for (const ConsoleSpan& sp : spans) {
            QVariantMap m;
            switch (sp.kind) {
            case ConsoleSpanKind::Image: m.insert("kind", "image"); break;
            case ConsoleSpanKind::Shape: m.insert("kind", "shape"); break;
            default:                     m.insert("kind", "text");  break;
            }
            m.insert("text", sp.text);
            m.insert("imageId", sp.imageId);
            m.insert("imageW", sp.imageSize.width());
            m.insert("imageH", sp.imageSize.height());
            m.insert("shapeType", sp.shapeType);
            if (sp.style.color.isValid())       m.insert("color", sp.style.color.name(QColor::HexArgb));
            if (sp.style.buttonColor.isValid()) m.insert("buttonColor", sp.style.buttonColor.name(QColor::HexArgb));
            m.insert("bold", sp.style.bold);
            m.insert("italic", sp.style.italic);
            m.insert("underline", sp.style.underline);
            m.insert("strike", sp.style.strike);
            if (!sp.style.fontName.isEmpty()) m.insert("fontName", sp.style.fontName);
            m.insert("colorChanged", sp.style.colorChanged());
            spanList.append(m);
        }

        QVariantList buttonList;
        buttonList.reserve(buttons.size());
        for (const ConsoleButton& b : buttons) {
            QVariantMap m;
            m.insert("startSpan", b.startSpan);
            m.insert("spanCount", b.spanCount);
            m.insert("isInteger", b.isInteger);
            m.insert("intValue", b.intValue);
            m.insert("strValue", b.strValue);
            m.insert("tooltip", b.tooltip);
            m.insert("generation", QVariant::fromValue<qulonglong>(b.generation));
            m.insert("enabled", b.enabled);
            buttonList.append(m);
        }

        QVariantMap out;
        out.insert("spans", spanList);
        out.insert("buttons", buttonList);
        out.insert("align", static_cast<int>(align));
        out.insert("isLogicalLine", isLogicalLine);
        out.insert("kind", buttons.isEmpty() ? "text" : "button");
        out.insert("plain", plainText());
        return out;
    }
};

#endif // CONSOLE_TYPES_H
