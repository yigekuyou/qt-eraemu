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

// ---------------------------------------------------------------------------
// 主窗口（界面 GUI 的装配点）
//
//   * EraEngine 单例 + GuiManager（设置）+ ConsoleBackend（控制台数据）；
//   * 菜单：文件 / 编辑 / 视图 / 帮助（对齐 C# MainWindow 的 ToolStrip）；
//   * 对话框：目录选择 / 保存日志 / 设置 / 关于；
//   * 窗口标题、尺寸、字体、颜色全部来自 GuiManager（可在设置里改、可持久化）。
// ---------------------------------------------------------------------------
ApplicationWindow {
    id: window

    visible: true
    title: eraEngine.gui.windowTitle || "Emuera Engine"
    width: eraEngine.gui.windowWidth
    height: eraEngine.gui.windowHeight
    // 全屏(F11)优先；否则遵循设置里的「最大化」
    visibility: fullscreen ? Window.FullScreen : (eraEngine.gui.maximized ? Window.Maximized : Window.Windowed)
    color: eraEngine.gui.backColor

    // ---- 最小窗口尺寸 ----
    // 引擎的逻辑网格是固定的（脚本看到的列/行数不随窗口变化），窗口小于
    // 「网格 × 单元格像素」时内容会被裁剪。下限由 GuiManager 依据当前字号
    // 与网格统一给出（那里也对保存的设置值做同样的钳制）。
    minimumWidth: eraEngine.gui.minimumWindowWidth
    minimumHeight: eraEngine.gui.minimumWindowHeight

    property bool fullscreen: false

    // 引擎单例（QML 中实例化；也可作为 qmlRegisterSingletonInstance 注入）
    EraEngine {
        id: eraEngine
        // 脚本 QUIT（C# 侧等价关闭游戏窗口）—— 卸载当前游戏并释放内存，
        // 回到「未装载」状态（应用常驻，可重新打开目录）
        onQuitRequested: eraEngine.closeGame()
    }

    // D-Bus /debug saveScreenshot(path)：QML 自己抓取渲染结果存盘。
    // 桌面截屏工具抓不到 QML 场景（合成窗口可能拿到空帧），渲染自检必须
    // 走 Item.grabToImage —— Qt 文档：grabToImage(cb) 异步渲染该 Item 子树，
    // 回调里的 ItemGrabResult.saveToFile(path) 落盘。
    Connections {
        target: eraEngine
        function onScreenshotRequested(path) {
            window.contentItem.grabToImage(function(result) {
                if (!result.saveToFile(path))
                    console.warn("saveScreenshot: 保存失败 " + path);
            });
        }
    }

    // ---- 对话框 ----
    // 原生文件对话框（平台对话框，常驻即可）
    FolderDialog {
        id: folderDialog
        title: qsTr("选择游戏目录")
        currentFolder: "file://" + eraEngine.gui.startDirectory
        onAccepted: eraEngine.gameDirectory = selectedFolder
    }

    FileDialog {
        id: logDialog
        title: qsTr("保存日志")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "log"
        currentFolder: "file://" + eraEngine.gui.startDirectory
        nameFilters: [qsTr("日志文件 (*.log *.txt)"), qsTr("所有文件 (*)")]
        onAccepted: eraEngine.gui.saveLog(selectedFile)
    }

    // ---- 自建窗口/对话框（Qt6 QML 惯用法：Qt.createComponent + createObject）----
    // QML 自己按需创建对象并持有引用；重开时先销毁旧的，关闭即释放。
    property var activeDialog: null

    function openLazyDialog(url, props) {
        if (activeDialog) {
            activeDialog.destroy();
            activeDialog = null;
        }
        const comp = Qt.createComponent(Qt.resolvedUrl(url));
        if (comp.status === Component.Error) {
            console.error("加载组件失败:", url, comp.errorString());
            return null;
        }
        const obj = comp.createObject(window, props || {});
        if (obj === null) {
            console.error("实例化对象失败:", url);
            return null;
        }
        activeDialog = obj;
        if (obj.closed)
            obj.closed.connect(function () {
                if (window.activeDialog === obj)
                    window.activeDialog = null;
            });
        if (obj.open)
            obj.open();
        return obj;
    }
    function openSettings() {
        openLazyDialog("SettingsDialog.qml", {
            "gui": eraEngine.gui,
            "engine": eraEngine
        });
    }
    function openAbout() {
        openLazyDialog("AboutDialog.qml", {
            "gameBase": eraEngine.gameBaseData
        });
    }

    // ---- 菜单动作（QtQuick.Controls 的 MenuItem 用 action 承载快捷键）----
    Action {
        id: actOpen
        text: qsTr("打开目录…")
        shortcut: StandardKey.Open
        onTriggered: folderDialog.open()
    }
    Action {
        id: actReload
        text: qsTr("重新加载")
        shortcut: StandardKey.Refresh
        onTriggered: eraEngine.reload()
    }
    Action {
        id: actSaveLog
        text: qsTr("保存日志…")
        shortcut: StandardKey.Save
        onTriggered: logDialog.open()
    }
    Action {
        id: actTitle
        text: qsTr("返回标题")
        onTriggered: eraEngine.gotoTitle()
    }
    Action {
        id: actSettings
        text: qsTr("设置…")
        onTriggered: window.openSettings()
    }
    Action {
        id: actQuit
        text: qsTr("退出")
        shortcut: StandardKey.Quit
        onTriggered: Qt.quit()
    }

    Action {
        id: actClear
        text: qsTr("清屏")
        onTriggered: eraEngine.console.clearAll()
    }
    Action {
        id: actBottom
        text: qsTr("滚动到底部")
        shortcut: "End"
        onTriggered: eraEngine.console.scrollToBottom()
    }

    Action {
        id: actZoomIn
        text: qsTr("放大字号")
        shortcut: "Ctrl+="
        onTriggered: eraEngine.gui.adjustFontSize(1)
    }
    Action {
        id: actZoomOut
        text: qsTr("缩小字号")
        shortcut: "Ctrl+-"
        onTriggered: eraEngine.gui.adjustFontSize(-1)
    }
    Action {
        id: actAbout
        text: qsTr("关于…")
        onTriggered: window.openAbout()
    }

    // ---- 菜单栏 ----
    menuBar: MenuBar {
        Menu {
            title: qsTr("文件(&F)")
            MenuItem {
                action: actOpen
            }
            MenuItem {
                action: actReload
            }
            MenuItem {
                action: actSaveLog
            }
            MenuItem {
                action: actTitle
            }
            MenuSeparator {}
            MenuItem {
                action: actSettings
            }
            MenuSeparator {}
            MenuItem {
                action: actQuit
            }
        }

        Menu {
            title: qsTr("编辑(&E)")
            MenuItem {
                action: actClear
            }
            MenuItem {
                action: actBottom
            }
        }

        Menu {
            title: qsTr("视图(&V)")
            MenuItem {
                action: actZoomIn
            }
            MenuItem {
                action: actZoomOut
            }
            MenuItem {
                text: qsTr("全屏")
                checkable: true
                checked: window.fullscreen
                onTriggered: window.fullscreen = !window.fullscreen
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("刷新帧率 +")
                onTriggered: eraEngine.gui.fps = eraEngine.gui.fps + 1
            }
            MenuItem {
                text: qsTr("刷新帧率 −")
                onTriggered: eraEngine.gui.fps = eraEngine.gui.fps - 1
            }
        }

        Menu {
            title: qsTr("帮助(&H)")
            MenuItem {
                action: actAbout
            }
        }
    }

    // ---- 控制台（渲染层）----
    EraRender {
        id: eraRender
        anchors.fill: parent
        engine: eraEngine
    }

    // ---- 音频播放维护层（QML 按 C++ 登记的管线数量维护播放器）----
    AudioPlayers {
        id: audioPlayers
        audio: eraEngine.audio
    }

    // ---- 底部「红绿灯」状态条 ----
    // 高度最小、悬浮在内容之上（不占布局、不挤动控制台）；每个灯一个含义，
    // 悬浮（Hover）弹出 ToolTip 说明。灯亮/灭 = 该状态此刻是否成立。
    Item {
        id: statusStrip
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 4
        width: lightRow.width + 10
        height: 12
        z: 1000

        property bool loading: false      // 脚本装载中
        property bool errorState: eraEngine.hasError
        property int warningCount: 0      // 装载告警条数
        readonly property bool waiting: eraEngine.console.waitingInput
        readonly property int audioChannels: audioPlayers.activeChannels

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Qt.rgba(0, 0, 0, 0.5)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.15)
        }

        Row {
            id: lightRow
            anchors.centerIn: parent
            spacing: 5

            // ① 错误：最近一次执行/装载出错
            StatusLight {
                litColor: "#e0483c"
                lit: statusStrip.errorState
                label: qsTr("错误")
                detail: lit ? qsTr("最近一次执行出错") : qsTr("正常")
            }
            // ② 载入：正在装载脚本
            StatusLight {
                litColor: "#e0c040"
                lit: statusStrip.loading
                label: qsTr("载入")
                detail: lit ? qsTr("正在装载脚本") : qsTr("空闲")
            }
            // ③ 等待输入：脚本停在 INPUT 等玩家操作
            StatusLight {
                litColor: "#48c048"
                lit: statusStrip.waiting
                label: qsTr("等待输入")
                detail: lit ? qsTr("等待：%1").arg(eraEngine.console.inputKind)
                            : qsTr("脚本运行中")
            }
            // ④ 音频：有音频管线正在出声
            StatusLight {
                litColor: "#48a0e0"
                lit: statusStrip.audioChannels > 0
                label: qsTr("音频")
                detail: lit ? qsTr("播放中：%1 路").arg(statusStrip.audioChannels)
                            : qsTr("静音")
            }
            // ⑤ 装载告警：解析期告警条数
            StatusLight {
                litColor: "#e08a30"
                lit: statusStrip.warningCount > 0
                label: qsTr("装载告警")
                detail: statusStrip.warningCount > 0
                        ? qsTr("%1 条").arg(statusStrip.warningCount) : qsTr("无")
            }
        }
    }

    // 状态灯（最小尺寸圆点；悬浮显示含义 + 当前状态）
    component StatusLight: Rectangle {
        id: light
        property color litColor: "#48c048"
        property bool lit: false
        property string label: ""
        property string detail: ""

        width: 7
        height: 7
        radius: width / 2
        color: lit ? litColor : Qt.rgba(1, 1, 1, 0.16)
        border.width: 1
        border.color: lit ? Qt.rgba(1, 1, 1, 0.6) : Qt.rgba(1, 1, 1, 0.22)

        HoverHandler {
            id: hover
        }
        ToolTip.visible: hover.hovered
        ToolTip.delay: 200
        ToolTip.text: light.label + "：" + light.detail
    }

    // ---- 快捷键：全屏 ----
    Shortcut {
        sequence: "F11"
        onActivated: window.fullscreen = !window.fullscreen
    }

    // 支持命令行直接带游戏目录启动：appemuera <dir>
    Component.onCompleted: {
        const args = Qt.application.arguments;
        for (let i = 1; i < args.length; ++i) {
            if (args[i].startsWith("-"))
                continue;
            eraEngine.gameDirectory = args[i];
            break;
        }
    }

    // 装载完成后自动进入系统状态机（标题画面 → 等待输入）
    Connections {
        target: eraEngine
        function onScriptsLoadStarted() {
            statusStrip.loading = true;
        }
        function onScriptsLoaded(ok) {
            statusStrip.loading = false;
            statusStrip.warningCount = eraEngine.parseWarnings().length;
            if (ok)
                eraEngine.runSystem();
        }
    }
}
