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
import QtQuick
import QtQuick.Controls


// 最小单位区块（QML 侧的「一个区块对象」）
//
// C++ 只给**相对网格坐标与格子数**（单位 = 区块长 / 区块高）：
//   col / row        —— 绝对网格坐标（相对 root 全平面）
//   relCol / relRow  —— 相对网格坐标（相对所属行）
//   cols / rows      —— 占几个单位（动态测量出来的格子数）
// **像素大小由本组件决定**：px = col × cellWidth，py = row × cellHeight，
// 也就是说换字体/缩放只改 cellWidth/cellHeight，不用动 C++。
//
//   kind === "text"  → Text（按钮 span 则是原生 Button）
//   kind === "image" → Image（source: image://emuera/<资源名>；取不到退化成 altText）
//   kind === "shape" → Rectangle（space / rect / line）
//
// 点击由区块自己上报：同一个「段」的多个区块共享同一份点击值，
// 所以点哪个区块都等价（对齐 C# 的「先命中 part，再映射到 ConsoleButtonString」）。
// 按钮 span 用原生 Button 提供外观与禁用态；点击仍统一走 blockButtonMouse，
// 让「命中 → clickAt → submitInput」只有一个入口（与 C++ 世代校验同源）。
Item {
    id: block

    property var blockData: ({})
    property var backend: null
    // ---- 单元格大小（由容器决定）----
    property real cellWidth: 9
    property real cellHeight: 19
    // 空字体/颜色表示使用系统主题默认值；C++ 只在模板显式指定时提供覆盖值。
    property string fontName: ""
    property int fontSize: 16
    property string foreColor: ""
    property string focusColor: ""
    property string logColor: ""
    property bool isBacklog: false
    SystemPalette { id: systemPalette }

    readonly property string effectiveFontName: blockData && blockData.fontName
                                                ? blockData.fontName : fontName
    readonly property color effectiveFocusColor: blockData && blockData.buttonColor
                                                ? blockData.buttonColor
                                                : (focusColor !== "" ? focusColor : systemPalette.highlight)
    readonly property color effectiveTextColor: {
        const d = blockData || ({});
        if (hovered) return effectiveFocusColor;
        if (isBacklog && d.colorChanged === false && logColor !== "") return logColor;
        if (d.color) return d.color;
        return foreColor !== "" ? foreColor : systemPalette.text;
    }

    readonly property string kind: blockData ? (blockData.kind || "text") : "text"
    readonly property bool isButtonSpan: blockData ? blockData.isButton === true : false
    readonly property int gridCol: blockData && blockData.col !== undefined ? blockData.col : 0
    readonly property int gridRow: blockData && blockData.row !== undefined ? blockData.row : 0
    readonly property int gridCols: blockData && blockData.cols > 0 ? blockData.cols : 1
    readonly property int gridRows: blockData && blockData.rows > 0 ? blockData.rows : 1
    readonly property bool clickable: blockData && backend
        ? blockData.clickable === true && blockData.generation === backend.generation : false
    readonly property bool hovered: mouse.containsMouse && clickable

    // 位置与尺寸：网格坐标 × 单元格大小（QML 说了算）
    x: gridCol * cellWidth
    y: gridRow * cellHeight
    // 外层至少覆盖 C++ 的网格测量；文本内容本身由 glyph 容器自动撑开。
    width: Math.max(gridCols * cellWidth, isButtonSpan ? 0 : contentRow.implicitWidth)
    height: Math.max(gridRows * cellHeight, contentRow.implicitHeight)

    // ---- 按钮 span：原生 Button 提供外观/悬停/禁用态 ----
    Button {
        id: buttonChrome
        visible: block.isButtonSpan
        anchors.fill: parent
        enabled: false                       // 交互统一由 blockButtonMouse 上报
        hoverEnabled: false
        leftPadding: 6
        rightPadding: 6
        topPadding: 0
        bottomPadding: 0
        text: block.blockData ? (block.blockData.text || "") : ""
        font.family: block.effectiveFontName
        font.pixelSize: block.fontSize > 0 ? block.fontSize : undefined
        font.bold: block.blockData ? block.blockData.bold === true : false
        opacity: block.clickable ? 1.0 : 0.65
        palette.button: block.effectiveTextColor
        palette.buttonText: block.hovered ? block.effectiveFocusColor : block.effectiveTextColor
        palette.highlight: block.effectiveFocusColor
        down: block.hovered                  // 由 blockButtonMouse 驱动原生按压/高亮
        highlighted: block.hovered
        // 按钮文本按 C++ 量好的格子宽度截断，不撑破平面布局
        contentItem: Label {
            text: buttonChrome.text
            font: buttonChrome.font
            color: buttonChrome.palette.buttonText
            verticalAlignment: Text.AlignVCenter
            elide: Label.ElideRight
            maximumLineCount: 1
        }
        ToolTip.visible: block.hovered && block.blockData && (block.blockData.tooltip || "") !== ""
        ToolTip.delay: 350
        ToolTip.text: block.blockData ? (block.blockData.tooltip || "") : ""
    }

    // ---- 悬停高亮（可点击区块）----
    Rectangle {
        anchors.fill: parent
        visible: block.hovered && !block.isButtonSpan
        color: block.effectiveFocusColor
        opacity: 0.22
        radius: 2
    }

    // A Text.width constrains layout, not glyph advance. Render each grid
    // character in its assigned cells so font fallback/proportional fonts cannot
    // move later characters or paint over the following span.
    Row {
        id: contentRow
        objectName: "textCells"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        visible: block.kind === "text" && !block.isButtonSpan
        Repeater {
            model: block.visible && block.kind === "text" && !block.isButtonSpan
                   ? (block.blockData ? (block.blockData.text || "").split("") : []) : []
            delegate: Item {
                required property string modelData
                readonly property int units: {
                    const u = modelData.charCodeAt(0);
                    return u < 128 || (u >= 0xff61 && u <= 0xff9f) ? 1 : 2;
                }
                width: units * block.cellWidth
                height: block.gridRows * block.cellHeight
                clip: true
                Text {
                    id: glyph
                    objectName: "gridGlyph"
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: parent.modelData
                    // 空 family 交给 Qt 使用系统默认字体。
                    font.family: block.effectiveFontName
                    font.pixelSize: block.fontSize > 0 ? block.fontSize : undefined
                    transform: Scale {
                        xScale: glyph.implicitWidth > 0 ? glyph.parent.width / glyph.implicitWidth : 1
                    }
                    color: block.effectiveTextColor
                    font.bold: block.blockData ? block.blockData.bold === true : false
                    font.italic: block.blockData ? block.blockData.italic === true : false
                    font.underline: block.blockData ? block.blockData.underline === true : false
                    font.strikeout: block.blockData ? block.blockData.strike === true : false
                }
            }
        }
    }

    Image {
        id: imageItem
        visible: block.kind === "image"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        source: (visible && block.blockData && block.blockData.text)
                    ? ("image://emuera/" + encodeURIComponent(block.blockData.text)) : ""
        // ConsoleLayout supplies grid dimensions.  Keep the Image item at the
        // same size as its span so a loaded resource cannot paint into the
        // following line or leave a zero-sized QML item.
        width: Math.max(1, block.width)
        height: Math.max(1, block.height)
        fillMode: Image.PreserveAspectFit
        smooth: true
        asynchronous: true

        // 资源取不到时按文本回退（对齐 C#：把 <img src='…'> 当文字画）
        Text {
            visible: imageItem.status === Image.Error
            textFormat: Text.PlainText
            text: block.blockData ? (block.blockData.altText || "") : ""
            color: block.effectiveTextColor
            font.family: block.effectiveFontName
            font.pixelSize: block.fontSize > 0 ? block.fontSize : undefined
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Rectangle {
        id: shapeRect
        visible: block.kind === "shape"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: block.width
        height: (block.blockData && block.blockData.shapeType === "line") ? 1 : block.height
        opacity: (block.blockData && block.blockData.shapeType === "space") ? 0 : 1
        color: block.effectiveTextColor
    }

    MouseArea {
        id: mouse
        objectName: "blockButtonMouse"      // 供 QML 测试 findChild 命中
        z: 1
        anchors.fill: parent
        hoverEnabled: true
        enabled: block.clickable
        cursorShape: block.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (block.backend && block.clickable && block.blockData)
                block.backend.clickAt(block.blockData.lineIndex, block.blockData.segmentIndex);
        }
    }
}
