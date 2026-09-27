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
import io.yigekuoyou.eraengine

// 渲染层：把引擎的 ConsoleBackend 接到控制台视图。
Item {
    id: eraRender
    anchors.fill: parent

    // 绑定到 C++ 引擎
    property EraEngine engine: null

    Rectangle {
        anchors.fill: parent
        color: eraRender.engine ? eraRender.engine.gui.backColor : "#101010"
    }

    Console {
        anchors.fill: parent
        anchors.margins: 6
        lineHeight: eraRender.engine ? eraRender.engine.gui.lineHeight : 22
        fontName: eraRender.engine ? eraRender.engine.gui.fontName : ""
        fontSize: eraRender.engine ? eraRender.engine.gui.fontSize : 16
        foreColor: eraRender.engine ? eraRender.engine.gui.foreColor : "#e0e0e0"
        focusColor: eraRender.engine ? eraRender.engine.gui.focusColor : "#ffff00"
        logColor: eraRender.engine ? eraRender.engine.gui.logColor : "#9a9a9a"
        backend: eraRender.engine ? eraRender.engine.console : null
    }

    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        font.pixelSize: 10
        color: "#666666"
        text: eraRender.engine ? "frames: " + eraRender.engine.console.frameMs + "ms" : ""
    }
}
