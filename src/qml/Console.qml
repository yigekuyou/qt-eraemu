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
import QtQml

// 控制台视图（混合形态：C++ 提供服务，QML 自建对象）
//
//   * C++ 的 ConsoleBackend 提供「可见行模型」`visibleLines`（QVariantList）；
//   * QML 用 **Instantiator** 按模型创建 `ConsoleLine` 对象，模型变化时自动增删
//     （对齐 quickshell 的 `Instantiator { model: …; delegate: … }` 用法）；
//   * 每行内部再由 `ConsoleLine` 用 `Repeater` 创建 span 对象（含内联图/图形）；
//   * 只承载「可见窗口」，不建整段历史对象。
Item {
    id: consoleView
    property var backend: null              // ConsoleBackend
    property int lineHeight: 22
    property string fontName: ""            // 来自 GuiManager
    property int fontSize: 16
    property color foreColor: "#e0e0e0"
    property color focusColor: "#ffff00"
    property color logColor: "#9a9a9a"

    // 可见行数 / 取第 i 个可见行对象（供测试与外部使用）
    readonly property int visibleCount: linesInst.count
    function lineAt(i) { return linesInst.objectAt(i) }

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

        // ---- 可见行窗口：模型 → 对象（QML 自建）----
        Instantiator {
            id: linesInst

            model: consoleView.backend ? consoleView.backend.visibleLines : []
            delegate: ConsoleLine {
                width: consoleView.width
                height: consoleView.lineHeight
                lineIndex: index
                lineData: modelData
                backend: consoleView.backend
                isBacklog: consoleView.backend ? !consoleView.backend.followTail : false
                fontName: consoleView.fontName
                fontSize: consoleView.fontSize
                foreColor: consoleView.foreColor
                focusColor: consoleView.focusColor
                logColor: consoleView.logColor
            }

            // 位置由 Instantiator 管理的对象自行计算（行高 × 序号）
            // 注意：Instantiator 不会把对象挂进可视树，必须显式设置 parent；
            //       对象由 Instantiator 负责销毁，勿手动 destroy()
            onObjectAdded: (index, object) => {
                object.parent = listArea;
                object.y = index * consoleView.lineHeight;
            }
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

    onHeightChanged: {
        if (backend) backend.visibleCount = Math.max(1, Math.floor(listArea.height / lineHeight));
    }
    Component.onCompleted: {
        if (backend) backend.visibleCount = Math.max(1, Math.floor(listArea.height / lineHeight));
    }
}
