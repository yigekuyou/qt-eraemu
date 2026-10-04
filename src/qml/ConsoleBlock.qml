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
//   kind === "text"  → Text（**可点击 span 也是 Text**，只是多一层悬停高亮）
//   kind === "image" → Image（source: image://emuera/<资源名>；取不到退化成 altText）
//   kind === "shape" → Rectangle（space / rect / line）
//
// 点击由区块自己上报：同一个「段」的多个区块共享同一份点击值，
// 所以点哪个区块都等价（对齐 C# 的「先命中 part，再映射到 ConsoleButtonString」）。
//
// 按钮 span **不再套原生 Button**（00097c1 引入，已移除）：Qt Quick Controls 的
// Button 有自己的内边距/最小尺寸/居中文本，画出来比文字宽，且与按网格排布的
// 相邻区块对不齐。Emuera 的按钮就是「文字 + 换色 + tooltip」，所以这里统一按
// 网格逐字排布，只保留悬停高亮；命中仍只有一个入口 blockButtonMouse
// （「命中 → clickAt → submitInput」，与 C++ 世代校验同源）。
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

    // SETBGCOLOR 的文字背景色（span style.bgColor；无效 = 透明 = 主题背景）
    readonly property color effectiveBgColor: (blockData && blockData.bgColor)
                                              ? blockData.bgColor : "transparent"

    readonly property string kind: blockData ? (blockData.kind || "text") : "text"
    readonly property int gridCol: blockData && blockData.col !== undefined ? blockData.col : 0
    readonly property int gridRow: blockData && blockData.row !== undefined ? blockData.row : 0
    readonly property int gridCols: blockData && blockData.cols > 0 ? blockData.cols : 1
    readonly property int gridRows: blockData && blockData.rows > 0 ? blockData.rows : 1
    readonly property bool clickable: blockData && backend
        ? blockData.clickable === true && blockData.generation === backend.generation : false
    readonly property bool hovered: mouse.containsMouse && clickable

    // 图片的 `<img ypos=N>`：C++ 折算成「行数」的纵向偏移（负 = 往上盖）。
    // eraTW 的画像枠/時間停止/特效各自在被打印的那一行，靠它拉回来盖在立絵边缘。
    readonly property real offsetRows: blockData && blockData.offsetRows ? blockData.offsetRows : 0

    // 位置与尺寸：网格坐标 × 单元格大小（QML 说了算）
    // row 允许为负 / 超过行数：跨行图与带 ypos 的图层块会探出窗口，交给视口 clip。
    x: gridCol * cellWidth
    y: (gridRow + offsetRows) * cellHeight
    // 外层至少覆盖 C++ 的网格测量；文本内容本身由 glyph 容器自动撑开。
    width: Math.max(gridCols * cellWidth, contentRow.implicitWidth)
    height: Math.max(gridRows * cellHeight, contentRow.implicitHeight)

    // ---- 悬停高亮（可点击区块，含按钮 span）----
    Rectangle {
        anchors.fill: parent
        visible: block.hovered
        color: block.effectiveFocusColor
        opacity: 0.22
        radius: 2
    }

    // A Text.width constrains layout, not glyph advance. Render each grid
    // character in its assigned cells so font fallback/proportional fonts cannot
    // move later characters or paint over the following span.
    // SETBGCOLOR 背景（画在文字之后，z = -1）
    Rectangle {
        objectName: "spanBg"
        visible: block.kind === "text" && block.effectiveBgColor.a > 0
        color: block.effectiveBgColor
        anchors.fill: parent
        z: -1
    }

    Row {
        id: contentRow
        objectName: "textCells"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        visible: block.kind === "text"
        Repeater {
            model: block.visible && block.kind === "text"
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
        // 只在可点击时开启悬停：全屏数千个区块逐帧 hover 命中测试是
        // 滚动/鼠标移动卡顿的主要来源之一（不可点击区块永远不需要高亮）。
        hoverEnabled: block.clickable
        enabled: block.clickable
        cursorShape: block.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (block.backend && block.clickable && block.blockData)
                block.backend.clickAt(block.blockData.lineIndex, block.blockData.segmentIndex);
        }
        // 按钮的 Title（C# ButtonString.Title）——以前挂在原生 Button 上，
        // 去掉按钮样式后改挂在命中区上，行为不变。
        ToolTip.visible: block.hovered && block.blockData
                         && (block.blockData.tooltip || "") !== ""
        ToolTip.delay: 350
        ToolTip.text: block.blockData ? (block.blockData.tooltip || "") : ""
    }
}
