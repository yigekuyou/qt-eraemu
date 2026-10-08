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
import io.yigekuoyou.eraengine

// 关于对话框：显示 GameBase.csv 的作者/版本/年份等信息。
Dialog {
    id: dlg

    property GameBaseData gameBase: null

    title: qsTr("关于")
    modal: true
    width: 360
    standardButtons: Dialog.Ok

    contentItem: Grid {
        columns: 2
        columnSpacing: 12
        rowSpacing: 8
        width: dlg.width - 40

        Label { text: qsTr("标题") }
        Label { text: (dlg.gameBase && dlg.gameBase.title) || qsTr("未知") }
        Label { text: qsTr("作者") }
        Label { text: (dlg.gameBase && dlg.gameBase.author) || qsTr("未知") }
        Label { text: qsTr("版本") }
        Label { text: (dlg.gameBase && dlg.gameBase.version) || qsTr("未知") }
        Label { text: qsTr("发布年") }
        Label { text: (dlg.gameBase && dlg.gameBase.releaseYear) || qsTr("未知") }
        Label { text: qsTr("附加信息") }
        Label {
            text: (dlg.gameBase && dlg.gameBase.additionalInfo) || qsTr("无")
            wrapMode: Text.WordWrap
            width: 220
        }
    }
}
