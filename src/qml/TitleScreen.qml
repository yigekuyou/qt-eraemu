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
// Qt 文档（qmllint / ComponentBehavior: Bound）：委托等嵌套组件里用外层 id
// 改为编译期绑定（静态查找），代价是必须显式写 id（见菜单项 delegate）。
pragma ComponentBehavior: Bound

import QtQuick
import io.yigekuoyou.eraengine

// ---------------------------------------------------------------------------
// 标准标题画面（QML 渲染）
//
// 没有 @SYSTEM_TITLE 的 ROM 会走这里 —— **画在 QML 里，不再由 C++ 逐行打印**。
// C++（SystemStateMachine::beginTitle）只在「没有 @SYSTEM_TITLE」时翻开
// eraEngine.defaultTitleVisible，并仍用同一套 openingInput() 状态流等待输入；
// 本组件点 [0]/[1] 时调 eraEngine.chooseTitle(n)，走的是与手动输入完全相同的
// provideInput 通路（写 RESULT → 状态机 resume）。
//
// 版式对齐 C# Process.SystemProc.cs beginTitle() 的标准标题分支：
//   PrintBar / 居中标题 / (版本非 0 时) 版本行 / 作者 / "(年份)" / 空行 /
//   说明 / PrintBar / "[0] 最初から" / "[1] ロード" / openingInput。
// 字符串来自 gameBaseData（GameBase.csv）与 eraEngine.titleMenu0/1。
// ---------------------------------------------------------------------------
Item {
    id: root
    objectName: "titleScreen"

    // 由 EraRender 注入的引擎
    property EraEngine engine: null
    property bool active: root.engine ? root.engine.defaultTitleVisible : false
    visible: active
    z: 500   // 盖在控制台（及其输入行）之上

    readonly property GameBaseData gb:  root.engine ? root.engine.gameBaseData : null
    readonly property GuiManager  gui:  root.engine ? root.engine.gui : null
    readonly property color fore:  gui ? gui.foreColor : "#c0c0c0"
    readonly property color focusColor: gui ? gui.focusColor : "#ffff00"
    readonly property string family: gui ? gui.fontName : ""
    readonly property int  px:     gui ? gui.fontSize : 18
    readonly property real lineH:  px + 4

    // C#：gamebase.ScriptVersion != 0 时才打印版本行（版本串解析成整数；解析失败 = 0）
    readonly property bool showVersion: {
        const v = (gb && gb.version !== undefined) ? gb.version : "";
        if (v === "")
            return false;
        const n = Number(v);
        return !isNaN(n) && n !== 0;
    }

    function label(value) {
        return value === undefined || value === null ? "" : value;
    }

    // 背景（舞台本身已有底色；这里再铺一层，避免控制台残影透出）
    Rectangle {
        anchors.fill: parent
        color: root.gui ? root.gui.backColor : "#000000"
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        width: Math.min(parent.width - 20, 60 * root.px)

        // 顶部分隔条（C# PrintBar）
        Rectangle {
            width: parent.width
            height: 1
            color: root.fore
            opacity: 0.8
        }

        // 居中标题 / 版本 / 作者 / (年份)
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
            text: root.label(root.gb ? root.gb.title : "")
            color: root.fore
            font.family: root.family
            font.pixelSize: root.px
            font.bold: true
        }
        Text {
            visible: root.showVersion
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
            text: root.label(root.gb ? root.gb.version : "")
            color: root.fore
            font.family: root.family
            font.pixelSize: root.px
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
            text: root.label(root.gb ? root.gb.author : "")
            color: root.fore
            font.family: root.family
            font.pixelSize: root.px
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
            text: "(" + root.label(root.gb ? root.gb.releaseYear : "") + ")"
            color: root.fore
            font.family: root.family
            font.pixelSize: root.px
        }

        // 空行（C# console.NewLine）
        Item { width: 1; height: root.lineH }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
            text: root.label(root.gb ? root.gb.additionalInfo : "")
            color: root.fore
            font.family: root.family
            font.pixelSize: root.px
            wrapMode: Text.WordWrap
        }

        // 中部分隔条
        Rectangle {
            width: parent.width
            height: 1
            color: root.fore
            opacity: 0.8
        }
        Item { width: 1; height: root.lineH / 2 }

        // [0] 从头开始 / [1] 读档 —— 与 C# 的 SelectCase 值一致（0 / 1）
        Repeater {
            model: [
                { value: 0, text: root.engine ? root.engine.titleMenu0 : "" },
                { value: 1, text: root.engine ? root.engine.titleMenu1 : "" }
            ]
            delegate: Text {
                id: menuEntry                 // 子对象（MouseArea）引用委托自己的属性要走它的 id
                required property var modelData
                width: parent.width
                textFormat: Text.PlainText
                text: "[" + modelData.value + "] " + root.label(modelData.text)
                color: itemMouse.containsMouse ? root.focusColor : root.fore
                font.family: root.family
                font.pixelSize: root.px

                MouseArea {
                    id: itemMouse
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (root.engine) root.engine.chooseTitle(menuEntry.modelData.value)
                }
            }
        }

        // 底部分隔条
        Rectangle {
            width: parent.width
            height: 1
            color: root.fore
            opacity: 0.8
        }
    }
}
