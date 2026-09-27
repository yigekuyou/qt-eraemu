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

// 一个显示片段（span）：文本 / 内联图 / 图形。
// 对齐 C# GameView 的 ConsoleStyledString / ConsoleImagePart / ConsoleShapePart。
//
// 混合形态：C++ 只给「片段数据」（data），QML 自己决定用什么对象呈现：
//   kind === "text"  → Text
//   kind === "image" → Image（source: image://emuera/<资源名>，由 C++ 的
//                       ResourceImageProvider 解析；不存在则什么都不画）
//   kind === "shape" → Rectangle（line 等）
Item {
    id: span

    property var spanData: ({})
    property var line: null          // 所属 ConsoleLine（提供字体/颜色/行高）
    property var button: null        // 命中的按钮（无则 null）
    property int buttonIndex: -1
    property bool clickable: false
    property var backend: null

    readonly property string kind: (spanData && spanData.kind) ? spanData.kind : "text"
    readonly property var style: spanData || ({})

    implicitWidth: kind === "image" ? imageItem.width
                 : kind === "shape" ? shapeItem.width
                 : txt.implicitWidth
    implicitHeight: line ? line.lineHeight : 22

    // 悬停/选中高亮
    Rectangle {
        anchors.fill: parent
        visible: span.clickable && mouse.containsMouse
        color: span.line ? span.line.focusColor : "#ffff00"
        opacity: 0.22
        radius: 2
    }

    Text {
        id: txt
        visible: span.kind === "text"
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        text: span.style.text !== undefined ? span.style.text : ""
        font.family: span.line ? span.line.fontName : ""
        font.pixelSize: span.line ? span.line.fontSize : 16
        color: {
            if (span.clickable && mouse.containsMouse && span.line)
                return span.line.focusColor;
            if (span.style.color)
                return span.style.color;
            if (span.line.isBacklog && span.style.colorChanged === false)
                return span.line.logColor;
            return span.line ? span.line.foreColor : "#e0e0e0";
        }
        font.bold: span.style.bold === true
        font.italic: span.style.italic === true
        font.underline: span.style.underline === true
        font.strikeout: span.style.strike === true
    }

    Image {
        id: imageItem
        visible: span.kind === "image"
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        source: (visible && span.style.text) ? ("image://emuera/" + span.style.text) : ""
        width: (span.style.imageW > 0) ? span.style.imageW : implicitWidth
        height: (span.style.imageH > 0) ? span.style.imageH
                                        : (span.line ? span.line.lineHeight : 22)
        fillMode: Image.PreserveAspectFit
        smooth: true
        asynchronous: true
    }

    // 图形（目前实现「线」；其余留空占位）
    Item {
        id: shapeItem
        visible: span.kind === "shape"
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        width: (span.style.imageW > 0) ? span.style.imageW : 0
        height: span.line ? span.line.lineHeight : 22

        Rectangle {
            visible: span.style.shapeType === "line"
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            width: parent.width
            height: 1
            color: span.style.color ? span.style.color
                                    : (span.line ? span.line.foreColor : "#e0e0e0")
        }
    }

    MouseArea {
        id: mouse
        objectName: "spanButtonMouse"   // 供 QML 测试 findChild 命中
        anchors.fill: parent
        hoverEnabled: true
        enabled: span.clickable
        cursorShape: span.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (span.backend && span.clickable && span.line)
                span.backend.clickAt(span.line.lineIndex, span.buttonIndex);
        }
        ToolTip.visible: containsMouse && span.button !== null
                         && (span.button.tooltip || "").length > 0
        ToolTip.text: span.button !== null ? (span.button.tooltip || "") : ""
    }
}
