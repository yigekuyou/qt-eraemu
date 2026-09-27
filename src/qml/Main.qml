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
    visibility: eraEngine.gui.maximized ? Window.Maximized : Window.Windowed
    color: eraEngine.gui.backColor

    property bool fullscreen: false

    // 引擎单例（QML 中实例化；也可作为 qmlRegisterSingletonInstance 注入）
    EraEngine {
        id: eraEngine
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
        if (obj.closed) obj.closed.connect(function () { if (window.activeDialog === obj) window.activeDialog = null; });
        if (obj.open) obj.open();
        return obj;
    }
    function openSettings() { openLazyDialog("SettingsDialog.qml", { "gui": eraEngine.gui, "engine": eraEngine }); }
    function openAbout()    { openLazyDialog("AboutDialog.qml", { "gameBase": eraEngine.gameBaseData }); }

    // ---- 菜单动作（QtQuick.Controls 的 MenuItem 用 action 承载快捷键）----
    Action { id: actOpen;   text: qsTr("打开目录…"); shortcut: StandardKey.Open;    onTriggered: folderDialog.open() }
    Action { id: actReload; text: qsTr("重新加载");  shortcut: StandardKey.Refresh; onTriggered: eraEngine.reload() }
    Action { id: actSaveLog; text: qsTr("保存日志…"); shortcut: StandardKey.Save;   onTriggered: logDialog.open() }
    Action { id: actTitle;  text: qsTr("返回标题");  onTriggered: eraEngine.gotoTitle() }
    Action { id: actSettings; text: qsTr("设置…");   onTriggered: window.openSettings() }
    Action { id: actQuit;   text: qsTr("退出");      shortcut: StandardKey.Quit;    onTriggered: Qt.quit() }

    Action { id: actClear;  text: qsTr("清屏");      onTriggered: eraEngine.console.clearAll() }
    Action { id: actBottom; text: qsTr("滚动到底部"); shortcut: "End";               onTriggered: eraEngine.console.scrollToBottom() }

    Action { id: actZoomIn;  text: qsTr("放大字号"); shortcut: "Ctrl+="; onTriggered: eraEngine.gui.adjustFontSize(1) }
    Action { id: actZoomOut; text: qsTr("缩小字号"); shortcut: "Ctrl+-"; onTriggered: eraEngine.gui.adjustFontSize(-1) }
    Action { id: actAbout;   text: qsTr("关于…");   onTriggered: window.openAbout() }

    // ---- 菜单栏 ----
    menuBar: MenuBar {
        Menu {
            title: qsTr("文件(&F)")
            MenuItem { action: actOpen }
            MenuItem { action: actReload }
            MenuItem { action: actSaveLog }
            MenuItem { action: actTitle }
            MenuSeparator {}
            MenuItem { action: actSettings }
            MenuSeparator {}
            MenuItem { action: actQuit }
        }

        Menu {
            title: qsTr("编辑(&E)")
            MenuItem { action: actClear }
            MenuItem { action: actBottom }
        }

        Menu {
            title: qsTr("视图(&V)")
            MenuItem { action: actZoomIn }
            MenuItem { action: actZoomOut }
            MenuItem {
                text: qsTr("全屏")
                checkable: true
                checked: window.fullscreen
                onTriggered: window.fullscreen = !window.fullscreen
            }
            MenuSeparator {}
            MenuItem { text: qsTr("刷新帧率 +"); onTriggered: eraEngine.gui.fps = eraEngine.gui.fps + 1 }
            MenuItem { text: qsTr("刷新帧率 −"); onTriggered: eraEngine.gui.fps = eraEngine.gui.fps - 1 }
        }

        Menu {
            title: qsTr("帮助(&H)")
            MenuItem { action: actAbout }
        }
    }

    // ---- 控制台（渲染层）----
    EraRender {
        id: eraRender
        anchors.fill: parent
        engine: eraEngine
    }

    // ---- 状态栏 ----
    footer: ToolBar {
        visible: eraEngine.console.waitingInput

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 8
            spacing: 12
            Label {
                text: eraEngine.console.waitingInput
                      ? qsTr("等待输入：") + eraEngine.console.inputKind
                      : ""
                color: eraEngine.gui.foreColor
            }
        }
    }

    // ---- 快捷键：全屏 ----
    Shortcut {
        sequence: "F11"
        onActivated: window.fullscreen = !window.fullscreen
    }

    // 全屏切换
    onFullscreenChanged: visibility = fullscreen ? Window.FullScreen
                                                 : (eraEngine.gui.maximized ? Window.Maximized
                                                                            : Window.Windowed)

    // 支持命令行直接带游戏目录启动：appemuera <dir>
    Component.onCompleted: {
        const args = Qt.application.arguments;
        for (let i = 1; i < args.length; ++i) {
            if (args[i].startsWith("-")) continue;
            eraEngine.gameDirectory = args[i];
            break;
        }
    }

    // 装载完成后自动进入系统状态机（标题画面 → 等待输入）
    Connections {
        target: eraEngine
        function onScriptsLoaded(ok) { if (ok) eraEngine.runSystem() }
    }
}
