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

// 控制台视图（方案 H）：
//   * 模板在 QML（ConsoleLine），内容由 C++（ConsoleBackend）决定；
//   * 只显示“可见窗口”，不显示全部历史；超出即回收复用；
//   * 刷新由 C++ 的 1Hz 定时器 / flush 点触发 windowChanged，这里仅重建可见窗口。
Item {
    id: consoleView
    property var backend: null              // ConsoleBackend
    property int lineHeight: 22
    property var pool: []                   // 空闲 item 复用池
    property var live: []                   // 当前可见 item

    Component { id: lineComp; ConsoleLine {} }

    function computeVisibleCount() {
        return Math.max(1, Math.floor(listArea.height / lineHeight));
    }

    function recycleAll() {
        for (let i = 0; i < live.length; ++i) {
            live[i].visible = false;
        }
        pool = pool.concat(live);
        live = [];
    }

    // 重建可见窗口（1Hz 下重建几十行成本可忽略）
    function rebuild() {
        if (!backend) return;
        recycleAll();
        const n = backend.visibleLineCount();
        for (let k = 0; k < n; ++k) {
            const data = backend.visibleLine(k);
            if (!data || data.spans === undefined) continue;
            let it = pool.pop();
            if (!it) it = lineComp.createObject(content, {});
            it.backend = backend;
            it.lineHeight = consoleView.lineHeight;
            it.lineData = data;
            it.lineIndex = k;
            it.isBacklog = !backend.followTail;
            it.y = k * consoleView.lineHeight;
            it.visible = true;
            live.push(it);
        }
    }

    function submit() {
        if (!backend) return;
        backend.submitInput(parseInt(inputField.text) || 0);
        inputField.text = "";
    }

    Item {
        id: listArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: inputBar.top
        clip: true
        focus: true

        Item {
            id: content
            width: listArea.width
            height: listArea.height
        }

        WheelHandler {
            onWheel: (e) => {
                if (!backend) return;
                backend.scrollBy(e.angleDelta.y > 0 ? 3 : -3);
            }
        }

        Keys.onPressed: (e) => {
            if (!backend) return;
            if (e.key === Qt.Key_PageUp)   backend.scrollBy(10);
            if (e.key === Qt.Key_PageDown) backend.scrollBy(-10);
            if (e.key === Qt.Key_End)      backend.scrollToBottom();
        }
    }

    // 输入条：仅当执行链等待用户输入时出现
    Rectangle {
        id: inputBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: backend && backend.waitingInput ? 38 : 0
        visible: height > 0
        color: "#202020"

        Row {
            anchors.fill: parent
            anchors.margins: 4
            spacing: 6

            TextField {
                id: inputField
                width: parent.width - 90
                height: parent.height - 8
                placeholderText: backend ? ("输入（" + backend.inputKind + "）") : ""
                onAccepted: consoleView.submit()
            }
            Button {
                text: qsTr("确定")
                onClicked: consoleView.submit()
            }
        }
    }

    Connections {
        target: backend
        function onWindowChanged() { consoleView.rebuild(); }
    }

    onHeightChanged: {
        if (backend) backend.visibleCount = computeVisibleCount();
    }
    Component.onCompleted: {
        if (backend) backend.visibleCount = computeVisibleCount();
        rebuild();
    }
}
