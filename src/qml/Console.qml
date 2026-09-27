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

// 控制台视图 —— root 层 + 分层渲染
//
//   root 层（本组件）     ：可见窗口容器（滚动/裁剪/输入条）
//   text 层（textLayer）  ：所有文本区块
//   image 层（imageLayer）：所有图片区块
//   shape 层（shapeLayer）：所有图形区块
//
// 三个层都是 root 的子 Item，坐标同源，所以：
//   * 区块的**绝对位置**就是它在层内的 x/y（同 root 坐标系）；
//   * 区块的**相对位置**是 relX/relY（相对它所属的显示行）；
//   * 尺寸（w/h）由 C++ 按当前字体/字号/资源**动态测量**后给出。
//
// **位置与尺寸都是 C++ 说了算**（对齐 C# 的 SetAlignment / CalcPointX / SetWidth）：
// QML 只负责「按数据把区块对象创建到对应的层里」。层内对象用 Instantiator 创建，
// 模型变化（滚动/输出/换字号）时自动增删。
Item {
    id: root

    property var backend: null              // ConsoleBackend
    property int lineHeight: 19
    property string fontName: ""            // 来自 GuiManager
    property int fontSize: 18
    property color foreColor: "#e0e0e0"
    property color focusColor: "#ffff00"
    property color logColor: "#9a9a9a"

    // ---- 单元格大小：**由 QML 决定**（这就是「区块大小决定权在 QML」）----
    //   cellWidth  = 一个「区块长」= 一个半角字符宽
    //   cellHeight = 一个「区块高」= 一行高
    // C++ 只给 col/row（单位坐标）与 cols/rows（格子数），像素由这里换算。
    readonly property real cellWidth: Math.max(1, fontSize / 2)
    readonly property real cellHeight: lineHeight

    // 三个层各自的区块模型（C++ 提供，坐标已算好）
    readonly property var textModel: backend ? backend.textBlocks : []
    readonly property var imageModel: backend ? backend.imageBlocks : []
    readonly property var shapeModel: backend ? backend.shapeBlocks : []
    readonly property int contentHeight: backend ? backend.contentHeight : 0

    // 可见区块数（供测试）
    readonly property int textBlockCount: textInst.count
    readonly property int imageBlockCount: imageInst.count
    readonly property int shapeBlockCount: shapeInst.count
    function textBlockAt(i) { return textInst.objectAt(i) }
    function blockAt(i) { return textBlockAt(i) }

    function submit() {
        if (!backend) return;
        backend.submitInput(parseInt(inputField.text) || 0);
        inputField.text = "";
    }

    // 把字号/行高/字体推给 C++（C++ 据此动态重算所有区块的位置与尺寸）
    function syncLayout() {
        if (!backend) return;
        backend.setFontSize(fontSize);
        backend.setLineHeight(lineHeight);
        backend.setWindowWidth(width);
    }
    onFontSizeChanged: syncLayout()
    onLineHeightChanged: syncLayout()
    onWidthChanged: syncLayout()

    Item {
        id: viewport
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: inputBar.top
        clip: true

        // ---- text 层 ----
        Item {
            id: textLayer
            anchors.fill: parent

            Instantiator {
                id: textInst
                model: root.textModel
                delegate: ConsoleBlock {
                    blockData: modelData
                    backend: root.backend
                    cellWidth: root.cellWidth
                    cellHeight: root.cellHeight
                    fontName: root.fontName
                    fontSize: root.fontSize
                    foreColor: root.foreColor
                    focusColor: root.focusColor
                    logColor: root.logColor
                    isBacklog: root.backend ? !root.backend.followTail : false
                }
                // Instantiator 不把对象挂进可视树：显式设 parent；销毁由它负责
                onObjectAdded: (index, object) => { object.parent = textLayer; }
                onObjectRemoved: (index, object) => { object.parent = null; }
            }
        }

        // ---- image 层 ----
        Item {
            id: imageLayer
            anchors.fill: parent

            Instantiator {
                id: imageInst
                model: root.imageModel
                delegate: ConsoleBlock {
                    blockData: modelData
                    backend: root.backend
                    cellWidth: root.cellWidth
                    cellHeight: root.cellHeight
                    fontName: root.fontName
                    fontSize: root.fontSize
                    foreColor: root.foreColor
                    focusColor: root.focusColor
                    logColor: root.logColor
                    isBacklog: root.backend ? !root.backend.followTail : false
                }
                onObjectAdded: (index, object) => { object.parent = imageLayer; }
                onObjectRemoved: (index, object) => { object.parent = null; }
            }
        }

        // ---- shape 层 ----
        Item {
            id: shapeLayer
            anchors.fill: parent

            Instantiator {
                id: shapeInst
                model: root.shapeModel
                delegate: ConsoleBlock {
                    blockData: modelData
                    backend: root.backend
                    cellWidth: root.cellWidth
                    cellHeight: root.cellHeight
                    fontName: root.fontName
                    fontSize: root.fontSize
                    foreColor: root.foreColor
                    focusColor: root.focusColor
                    logColor: root.logColor
                    isBacklog: root.backend ? !root.backend.followTail : false
                }
                onObjectAdded: (index, object) => { object.parent = shapeLayer; }
                onObjectRemoved: (index, object) => { object.parent = null; }
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
                onAccepted: root.submit()
            }
            Button {
                text: qsTr("确定")
                onClicked: root.submit()
            }
        }
    }

    onHeightChanged: {
        if (backend) backend.visibleCount = Math.max(1, Math.floor(viewport.height / lineHeight));
    }
    Component.onCompleted: {
        syncLayout();
        if (backend) backend.visibleCount = Math.max(1, Math.floor(viewport.height / lineHeight));
    }
}
