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

// 一行显示：QML 用 Repeater 为每个 span 创建 `ConsoleSpan` 对象（QML 自建对象）。
// 内容全部来自 C++（ConsoleBackend）通过 lineData 给出的片段/按钮数据。
Item {
    id: lineRoot

    property var backend: null          // ConsoleBackend
    property var lineData: ({})         // { spans:[...], buttons:[...], align, kind }
    property int lineIndex: 0
    property int lineHeight: 22
    property bool isBacklog: false
    property string fontName: ""
    property int fontSize: 16
    property color foreColor: "#e0e0e0"
    property color focusColor: "#ffff00"
    property color logColor: "#9a9a9a"

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
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
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

            delegate: ConsoleSpan {
                required property var modelData
                required property int index

                spanData: modelData
                line: lineRoot
                backend: lineRoot.backend
                button: lineRoot.buttonFor(index)
                buttonIndex: lineRoot.buttonIndexFor(index)
                clickable: button !== null && button.enabled !== false
            }
        }
    }
}
