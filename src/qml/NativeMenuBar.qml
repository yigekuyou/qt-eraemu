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
import Qt.labs.platform

// ---------------------------------------------------------------------------
// 原生菜单栏（Qt.labs.platform）：macOS 系统菜单栏 / Linux 全局菜单
//
// 平台差异（Qt 文档 Qt.labs.platform MenuBar）：
//   * labs 的 MenuBar 走 QPA 原生菜单：macOS 挂在系统菜单栏顶部；Linux 通过
//     D-Bus 全局菜单（com.canonical.AppMenu.Registrar，Plasma/Unity 提供）
//     注册到面板，应用窗口内不占空间。没有全局菜单宿主时它什么也不显示，
//     所以是否使用由 Main.qml 探测宿主后决定，探测在 WindowController
//     的 hasGlobalMenuBar()（和 Qt 内部 QDBusMenuBar 用同一个服务名）；
//   * 它是 QObject 而非 Item，不能赋给 ApplicationWindow.menuBar，也不能用
//     Loader 装载 —— 由 Main.qml 按 needNativeMenuBar 创建；
//   * labs 的 MenuItem 没有 action 属性（见 plugins.qmltypes），逐项绑定
//     Main.qml 共享 Action 的状态，触发统一转发 action.trigger()。
// 其它情况用 Main.qml 里的 QtQuick.Controls MenuBar（Linux 上由
// org.kde.desktop/Kirigami 风格提供 KDE 观感）。
// ---------------------------------------------------------------------------
MenuBar {
    id: root

    required property var actions

    Menu {
        title: qsTr("文件")
        MenuItem {
            text: root.actions.open.text
            enabled: root.actions.open.enabled
            shortcut: root.actions.open.shortcut
            onTriggered: root.actions.open.trigger()
        }
        MenuItem {
            text: root.actions.reload.text
            enabled: root.actions.reload.enabled
            shortcut: root.actions.reload.shortcut
            onTriggered: root.actions.reload.trigger()
        }
        MenuItem { text: root.actions.reloadFolder.text; onTriggered: root.actions.reloadFolder.trigger() }
        MenuItem { text: root.actions.reloadFile.text; onTriggered: root.actions.reloadFile.trigger() }
        MenuItem {
            text: root.actions.saveLog.text
            enabled: root.actions.saveLog.enabled
            shortcut: root.actions.saveLog.shortcut
            onTriggered: root.actions.saveLog.trigger()
        }
        MenuItem {
            text: root.actions.copyLog.text
            enabled: root.actions.copyLog.enabled
            onTriggered: root.actions.copyLog.trigger()
        }
        MenuItem {
            text: root.actions.title.text
            enabled: root.actions.title.enabled
            onTriggered: root.actions.title.trigger()
        }
        MenuSeparator {}
        MenuItem {
            text: root.actions.settings.text
            enabled: root.actions.settings.enabled
            onTriggered: root.actions.settings.trigger()
        }
        MenuItem {
            text: root.actions.config.text
            enabled: root.actions.config.enabled
            onTriggered: root.actions.config.trigger()
        }
        MenuItem {
            text: root.actions.debug.text
            enabled: root.actions.debug.enabled
            onTriggered: root.actions.debug.trigger()
        }
        MenuSeparator {}
        MenuItem {
            text: root.actions.quit.text
            enabled: root.actions.quit.enabled
            shortcut: root.actions.quit.shortcut
            onTriggered: root.actions.quit.trigger()
        }
    }

    Menu {
        title: qsTr("编辑")
        MenuItem {
            text: root.actions.clear.text
            enabled: root.actions.clear.enabled
            onTriggered: root.actions.clear.trigger()
        }
        MenuItem {
            text: root.actions.bottom.text
            enabled: root.actions.bottom.enabled
            shortcut: root.actions.bottom.shortcut
            onTriggered: root.actions.bottom.trigger()
        }
    }

    Menu {
        title: qsTr("视图")
        MenuItem {
            text: root.actions.zoomIn.text
            enabled: root.actions.zoomIn.enabled
            shortcut: root.actions.zoomIn.shortcut
            onTriggered: root.actions.zoomIn.trigger()
        }
        MenuItem {
            text: root.actions.zoomOut.text
            enabled: root.actions.zoomOut.enabled
            shortcut: root.actions.zoomOut.shortcut
            onTriggered: root.actions.zoomOut.trigger()
        }
        MenuSeparator {}
        MenuItem {
            text: root.actions.windowed.text
            checkable: true
            checked: root.actions.windowed.checked
            enabled: root.actions.windowed.enabled
            onTriggered: root.actions.windowed.trigger()
        }
        MenuItem {
            text: root.actions.fullscreen.text
            checkable: true
            checked: root.actions.fullscreen.checked
            enabled: root.actions.fullscreen.enabled
            shortcut: root.actions.fullscreen.shortcut
            onTriggered: root.actions.fullscreen.trigger()
        }
        MenuItem {
            text: root.actions.borderless.text
            checkable: true
            checked: root.actions.borderless.checked
            enabled: root.actions.borderless.enabled
            onTriggered: root.actions.borderless.trigger()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("刷新帧率 +")
            onTriggered: root.actions.fpsUp()
        }
        MenuItem {
            text: qsTr("刷新帧率 −")
            onTriggered: root.actions.fpsDown()
        }
    }

    Menu {
        title: qsTr("マクロ")
        MenuItem { text: root.actions.macro01.text; onTriggered: root.actions.macro01.trigger() }
        MenuItem { text: root.actions.macro02.text; onTriggered: root.actions.macro02.trigger() }
        MenuItem { text: root.actions.macro03.text; onTriggered: root.actions.macro03.trigger() }
        MenuItem { text: root.actions.macro04.text; onTriggered: root.actions.macro04.trigger() }
        MenuItem { text: root.actions.macro05.text; onTriggered: root.actions.macro05.trigger() }
        MenuItem { text: root.actions.macro06.text; onTriggered: root.actions.macro06.trigger() }
        MenuItem { text: root.actions.macro07.text; onTriggered: root.actions.macro07.trigger() }
    }

    Menu {
        title: qsTr("帮助")
        MenuItem {
            text: root.actions.about.text
            enabled: root.actions.about.enabled
            onTriggered: root.actions.about.trigger()
        }
    }
}
