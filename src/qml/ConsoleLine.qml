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

// 一行显示：模板在 QML，内容由 C++（ConsoleBackend）通过 lineData 决定。
// span 逐个渲染；落在按钮范围内的 span 自动可点击（高亮 + 转发 clickAt）。
Item {
    id: line
    property var backend: null          // ConsoleBackend
    property var lineData: ({})         // { spans:[...], buttons:[...], align, kind }
    property int lineIndex: 0
    property int lineHeight: 22
    property bool isBacklog: false

    implicitWidth: parent ? parent.width : 0
    implicitHeight: lineHeight

    function buttonIndexFor(spanIndex) {
        var btns = lineData.buttons || [];
        for (var i = 0; i < btns.length; i++) {
            var b = btns[i];
            if (spanIndex >= b.startSpan && spanIndex < b.startSpan + b.spanCount)
                return i;
        }
        return -1;
    }
    function buttonFor(spanIndex) {
        var i = buttonIndexFor(spanIndex);
        return i >= 0 ? (lineData.buttons[i]) : null;
    }

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        // 对齐：由 C++ 决定（LEFT=0 / CENTER=1 / RIGHT=2）
        x: {
            if (!lineData || lineData.align === undefined) return 0;
            if (lineData.align === 1) return Math.max(0, (parent.width - row.implicitWidth) / 2);
            if (lineData.align === 2) return Math.max(0, parent.width - row.implicitWidth);
            return 0;
        }
        spacing: 0

        Repeater {
            model: lineData.spans || []

            delegate: Item {
                id: spanItem
                required property var modelData
                required property int index

                readonly property var btn: line.buttonFor(index)
                readonly property bool clickable: btn !== null && btn.enabled !== false

                width: txt.implicitWidth
                height: line.lineHeight

                Rectangle {
                    anchors.fill: parent
                    visible: spanItem.clickable && mouse.containsMouse
                    color: spanItem.modelData.buttonColor || "#33ffffff"
                    radius: 2
                }

                Text {
                    id: txt
                    anchors.verticalCenter: parent.verticalCenter
                    text: spanItem.modelData.text !== undefined ? spanItem.modelData.text : ""
                    color: {
                        if (spanItem.clickable && mouse.containsMouse && spanItem.modelData.buttonColor)
                            return spanItem.modelData.buttonColor;
                        if (spanItem.modelData.color)
                            return spanItem.modelData.color;
                        return (line.isBacklog && spanItem.modelData.colorChanged === false)
                               ? "#888888" : "#e0e0e0";
                    }
                    font {
                        bold: spanItem.modelData.bold === true
                        italic: spanItem.modelData.italic === true
                        underline: spanItem.modelData.underline === true
                        strikeout: spanItem.modelData.strike === true
                    }
                }

                MouseArea {
                    id: mouse
                    objectName: "spanButtonMouse"   // 供 QML 测试 findChild 命中
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: spanItem.clickable
                    cursorShape: spanItem.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        if (line.backend && spanItem.clickable)
                            line.backend.clickAt(line.lineIndex, line.buttonIndexFor(spanItem.index));
                    }
                    ToolTip.visible: containsMouse && spanItem.btn !== null
                                     && (spanItem.btn.tooltip || "").length > 0
                    ToolTip.text: spanItem.btn !== null ? (spanItem.btn.tooltip || "") : ""
                }
            }
        }
    }
}
