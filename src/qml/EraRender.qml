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
        // 与 stage 同尺寸：网格行数按「配置高 / 行高」推导，控制台必须拿到
        // 整块 stage 高度，否则最后一行的行距会被 margin 吃掉（底部削字）。
        anchors.fill: parent
        lineHeight: eraRender.engine ? eraRender.engine.gui.lineHeight : 22
        fontName: eraRender.engine ? eraRender.engine.gui.fontName : ""
        fontSize: eraRender.engine ? eraRender.engine.gui.fontSize : 16
        foreColor: eraRender.engine ? eraRender.engine.gui.foreColor : "#e0e0e0"
        focusColor: eraRender.engine ? eraRender.engine.gui.focusColor : "#ffff00"
        logColor: eraRender.engine ? eraRender.engine.gui.logColor : "#9a9a9a"
        backend: eraRender.engine ? eraRender.engine.console : null
        // 当前舞台与历史日志使用独立模型；舞台快照在 C++ flush 帧边界发布。
        // stageMode 属于 ConsoleBackend，Console 通过 backend.stageMode 读取。
        onBackendChanged: if (backend) backend.stageMode = true
        Component.onCompleted: if (backend) backend.stageMode = true
        engine: eraRender.engine
        // 标准标题画面的输入由 TitleScreen 交付。
        titleActive: eraRender.engine ? eraRender.engine.defaultTitleVisible : false
    }

    // 标准标题画面（无 @SYSTEM_TITLE 时）：画在控制台之上，由 QML 全权渲染。
    TitleScreen {
        anchors.fill: parent
        engine: eraRender.engine
    }
}
