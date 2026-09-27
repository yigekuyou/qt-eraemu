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
import QtQuick.Dialogs
import io.yigekuoyou.eraengine

// 设置对话框：字号 / 行高 / 字体 / 颜色（文字/背景/选中/历史）。
// 直接绑定 GuiManager 的属性（改即生效，控制台实时预览）；
// 「恢复默认」调 gui.resetToDefaults()，「保存」写回 emuera.config。
Dialog {
    id: dlg

    property GuiManager gui: null

    title: qsTr("设置")
    modal: true
    width: 460
    standardButtons: Dialog.Ok | Dialog.Cancel

    // 取色器：目标属性名（"fore"/"back"/"focus"/"log"）
    ColorDialog {
        id: picker
        property string which: "fore"
        onAccepted: dlg.applyColor(which, selectedColor)
    }

    function colorOf(which) {
        if (!gui) return "#000000";
        if (which === "fore")  return gui.foreColor;
        if (which === "back")  return gui.backColor;
        if (which === "focus") return gui.focusColor;
        return gui.logColor;
    }
    function applyColor(which, c) {
        if (!gui) return;
        if (which === "fore")  gui.foreColor = c;
        else if (which === "back")  gui.backColor = c;
        else if (which === "focus") gui.focusColor = c;
        else gui.logColor = c;
    }

    contentItem: Column {
        spacing: 8
        width: dlg.width - 40

        Grid {
            columns: 2
            columnSpacing: 12
            rowSpacing: 8

            Label { text: qsTr("字号") }
            SpinBox {
                from: 8; to: 72
                value: dlg.gui ? dlg.gui.fontSize : 18
                onValueModified: if (dlg.gui) dlg.gui.fontSize = value
            }

            Label { text: qsTr("行高") }
            SpinBox {
                from: 8; to: 96
                value: dlg.gui ? dlg.gui.lineHeight : 19
                onValueModified: if (dlg.gui) dlg.gui.lineHeight = value
            }

            Label { text: qsTr("刷新帧率") }
            SpinBox {
                from: 1; to: 60
                value: dlg.gui ? dlg.gui.fps : 5
                onValueModified: if (dlg.gui) dlg.gui.fps = value
            }

            Label { text: qsTr("历史行数") }
            SpinBox {
                from: 100; to: 100000
                stepSize: 100
                value: dlg.gui ? dlg.gui.maxLog : 5000
                onValueModified: if (dlg.gui) dlg.gui.maxLog = value
            }

            Label { text: qsTr("字体") }
            ComboBox {
                id: fontBox
                width: 240
                editable: true
                model: dlg.gui ? dlg.gui.availableFontFamilies() : []
                Component.onCompleted: if (dlg.gui) editText = dlg.gui.fontName
                onActivated: if (dlg.gui) dlg.gui.fontName = currentText
                onAccepted: if (dlg.gui) dlg.gui.fontName = editText
            }
        }

        // ---- 颜色 ----
        Repeater {
            model: [
                { key: "fore",  label: qsTr("文字色") },
                { key: "back",  label: qsTr("背景色") },
                { key: "focus", label: qsTr("选中文字色") },
                { key: "log",   label: qsTr("历史文字色") }
            ]
            delegate: Row {
                required property var modelData
                spacing: 8
                Label { text: modelData.label; width: 90 }
                Rectangle {
                    width: 48; height: 22
                    border.color: "#888888"
                    color: dlg.colorOf(modelData.key)
                }
                Label {
                    text: dlg.gui ? dlg.gui.colorToString(dlg.colorOf(modelData.key)) : ""
                    width: 90
                }
                Button {
                    text: qsTr("选择…")
                    onClicked: {
                        picker.which = modelData.key;
                        picker.selectedColor = dlg.colorOf(modelData.key);
                        picker.open();
                    }
                }
            }
        }

        Row {
            spacing: 8
            Button {
                text: qsTr("恢复默认")
                onClicked: if (dlg.gui) dlg.gui.resetToDefaults()
            }
            Button {
                text: qsTr("保存到配置文件")
                enabled: dlg.gui !== null
                onClicked: if (engine) engine.saveConfigFiles()
            }
        }

        Label {
            text: qsTr("※ 修改即时生效；「保存到配置文件」写入 emuera.config")
            color: "#909090"
            font.pixelSize: 11
        }
    }

    // 供「保存到配置文件」使用（由 Main.qml 注入）
    property var engine: null
}
