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
//     平台差异：桌面用 QtQuick.Controls MenuBar（Linux=KDE/Kirigami 风格、
//     Windows=系统默认、macOS=系统风格），macOS 另建 Qt.labs.platform 原生
//     菜单栏挂系统菜单栏 / Linux 全局菜单（NativeMenuBar.qml），Android 用悬浮按钮 + 弹出菜单；
//   * 窗口几何/模式（窗口化/全屏/无边框全屏）由 C++ WindowController 直控，
//     QML 不再绑定 width/height/visibility；
//   * 后台托盘由 C++ TrayController 提供（关闭可隐藏到托盘常驻）；
//   * 对话框：目录选择 / 保存日志 / 设置 / 关于；
//   * 窗口标题、字体、颜色全部来自 GuiManager（可在设置里改、可持久化）。
// ---------------------------------------------------------------------------
ApplicationWindow {
    id: window

    visible: true
    title: eraEngine.gui.windowTitle || "Emuera Engine"
    color: eraEngine.gui.backColor

    // 平台差异总开关：移动端没有菜单栏/托盘/F11，走触屏适配
    readonly property bool isMobile: Qt.platform.os === "android" || Qt.platform.os === "ios"
    // 原生菜单栏：macOS 总是走系统菜单栏；Linux 探测 D-Bus 全局菜单宿主
    // （com.canonical.AppMenu.Registrar，Plasma/Unity），有宿主走 labs 原生
    // 菜单栏（NativeMenuBar.qml），否则窗口内 MenuBar
    readonly property bool useNativeMenuBar: Qt.platform.os === "macos"
                                             || (Qt.platform.os === "linux"
                                                 && windowController.hasGlobalMenuBar())

    // 引擎单例（QML 中实例化；也可作为 qmlRegisterSingletonInstance 注入）
    EraEngine {
        id: eraEngine
        // 脚本 QUIT（C# 侧等价关闭游戏窗口）—— 卸载当前游戏并释放内存，
        // 回到「未装载」状态（应用常驻，可重新打开目录）
        onQuitRequested: eraEngine.closeGame()
    }

    // ---- 窗口几何/模式：C++ 直控 ----
    // 尺寸来源（GuiManager 的 config/设置）变化时由 WindowController 直接
    // 改变窗口大小（含屏幕钳制、sizableWindow=false 锁死尺寸、启动最大化）；
    // 全屏(F11)/无边框全屏/窗口化也在那边管理。这里只负责把窗口交给它。
    WindowController {
        id: windowController
        window: window
        gui: eraEngine.gui
    }

    // ---- 后台托盘 ----
    // available=false（Android/iOS 或无托盘的桌面环境）时 enabled 自动无效，
    // C++ 侧不会创建图标。信号全部由窗口侧处理：窗口归 QML 所有。
    TrayController {
        id: tray
        tooltip: window.title
        onShowWindowRequested: {
            window.show();
            window.raise();
            window.requestActivate();
        }
        onHideWindowRequested: window.hide()
        onQuitRequested: Qt.quit()
    }
    // 托盘菜单勾选「关闭时隐藏到托盘」后：关闭 = 退到后台常驻（吞掉 close）
    onClosing: function(close) {
        if (tray.enabled && tray.closeToTray) {
            close.accepted = false;
            window.hide();   // 退到托盘常驻（hide 不触发 quitOnLastWindowClosed）
        }
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

    // ------------------------------------------------------------------
    // 每帧抓取渲染（D-Bus /debug startFrameCapture|stopFrameCapture）
    //
    // Qt 文档（Item::grabToImage / ItemGrabResult）：grabToImage(cb) **异步**把该
    // Item 子树渲染进离屏并回调，回调里 ItemGrabResult.saveToFile(path) 落盘 ——
    // 桌面截屏工具抓不到 QML 合成内容，这是「渲染结果」的唯一可靠来源
    // （QQuickWindow::grabWindow 走的是另一条同步路径，见 EraDBusDebug）。
    //
    // 「每帧」= 引擎每产生一次新画面（ConsoleBackend::windowChanged）抓一张：
    // 画面刷新与渲染抓取一一对应，才能定位「点按钮后画面没刷新」这类回归。
    // 上一张尚未落盘时只记一个「待抓」标记（合并到最新一帧），避免逐帧堆积。
    // ------------------------------------------------------------------
    property bool frameCaptureOn: false
    property string frameCapturePrefix: ""
    property int frameCaptureLimit: 0        // <=0 = 不限，靠 stopFrameCapture 停
    property int frameCaptureSaved: 0
    property bool frameCaptureBusy: false
    property bool frameCaptureQueued: false

    function frameCaptureStart(prefix, limit) {
        frameCapturePrefix = prefix;
        frameCaptureLimit = limit;
        frameCaptureSaved = 0;
        frameCaptureBusy = false;
        frameCaptureQueued = false;
        frameCaptureOn = true;
        // 先把「当前这一帧」抓下来，再随 windowChanged 逐帧抓
        Qt.callLater(window.frameCaptureGrab);
    }
    function frameCaptureStop() {
        if (!frameCaptureOn)
            return;
        frameCaptureOn = false;
        frameCaptureBusy = false;
        frameCaptureQueued = false;
        console.log("frameCapture: 共保存 " + frameCaptureSaved + " 帧 -> "
                    + frameCapturePrefix + "NNNNN.png");
    }
    function frameCaptureGrab() {
        if (!frameCaptureOn)
            return;
        if (frameCaptureBusy) {      // 上一帧还没落盘 -> 合并到最新一帧
            frameCaptureQueued = true;
            return;
        }
        frameCaptureBusy = true;
        const path = frameCapturePrefix
                   + String(frameCaptureSaved).padStart(5, "0") + ".png";
        // 抓渲染层（eraRender），与 saveScreenshot 的整窗抓取区分开
        eraRender.grabToImage(function(result) {
            if (!result.saveToFile(path))
                console.warn("frameCapture: 保存失败 " + path);
            ++frameCaptureSaved;
            frameCaptureBusy = false;
            if (frameCaptureLimit > 0 && frameCaptureSaved >= frameCaptureLimit) {
                window.frameCaptureStop();
                return;
            }
            if (frameCaptureQueued) {
                frameCaptureQueued = false;
                window.frameCaptureGrab();
            }
        });
    }

    Connections {
        target: eraEngine
        function onFrameCaptureRequested(prefix, limit) {
            window.frameCaptureStart(prefix, limit);
        }
        function onFrameCaptureStopRequested() {
            window.frameCaptureStop();
        }
    }
    // 引擎每刷新一次画面（windowChanged）= 一帧渲染 -> 抓一张
    Connections {
        target: eraEngine.console
        function onWindowChanged() {
            if (window.frameCaptureOn)
                window.frameCaptureGrab();
        }
    }

    // ---- 对话框 ----
    // 原生文件对话框（平台对话框，常驻即可）
    FolderDialog {
        id: folderDialog
        title: qsTr("选择游戏目录")
        currentFolder: "file://" + eraEngine.gui.startDirectory
        // 走异步装载（后台解析 ERB -> AST，主线程只按块合并）：eraTW 有 2200+
        // 个 ERB、200 万行，同步装载会把 GUI 线程锁死十几秒。loadAsync() 立即返回。
        onAccepted: eraEngine.loadAsync(selectedFolder)
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
        onTriggered: eraEngine.reloadAsync()
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
        // 滚动状态归 QML 视图所有：C++ 只发请求，Console.qml 消费
        onTriggered: eraEngine.console.requestScrollToBottom()
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
    // ---- 窗口模式（C++ WindowController 执行）----
    Action {
        id: actWindowed
        text: qsTr("窗口化")
        checkable: true
        checked: windowController.windowMode === "windowed"
        onTriggered: windowController.setWindowMode("windowed")
    }
    Action {
        id: actFullscreen
        text: qsTr("全屏")
        shortcut: StandardKey.FullScreen
        checkable: true
        checked: windowController.windowMode === "fullscreen"
        onTriggered: windowController.setWindowMode("fullscreen")
    }
    Action {
        id: actBorderless
        text: qsTr("无边框全屏")
        checkable: true
        checked: windowController.windowMode === "borderless"
        onTriggered: windowController.setWindowMode("borderless")
    }
    Action {
        id: actAbout
        text: qsTr("关于…")
        onTriggered: window.openAbout()
    }

    // 菜单结构共享给两套菜单实现：
    //   * 桌面 MenuBar / 移动端弹出菜单：直接用 id（action: actOpen）；
    //   * 原生菜单栏 NativeMenuBar.qml：labs MenuItem 没有 action 属性，
    //     靠这个映射逐项绑定状态、转发 trigger()。
    readonly property var menuActions: ({
        open: actOpen, reload: actReload, saveLog: actSaveLog, title: actTitle,
        settings: actSettings, quit: actQuit, clear: actClear, bottom: actBottom,
        zoomIn: actZoomIn, zoomOut: actZoomOut, about: actAbout,
        windowed: actWindowed, fullscreen: actFullscreen, borderless: actBorderless,
        fpsUp: function() { eraEngine.gui.fps = eraEngine.gui.fps + 1; },
        fpsDown: function() { eraEngine.gui.fps = eraEngine.gui.fps - 1; },
    })

    // ---- 菜单栏（平台差异）----
    // * Windows/Linux 桌面：QtQuick.Controls MenuBar（Linux 由 main.cpp 选的
    //   org.kde.desktop/Kirigami 风格着色，Windows 走系统默认风格）；
    // * macOS：窗口内不放，Component.onCompleted 里创建 Qt.labs.platform 的
    //   NativeMenuBar 挂到系统菜单栏 / Linux 全局菜单；
    // * Android/iOS：不创建，改用下方悬浮按钮 + 弹出菜单。
    // labs MenuBar 是 QObject，Loader 装不了，这里只装 Controls MenuBar。
    menuBar: menuBarLoader.item

    Loader {
        id: menuBarLoader
        active: !window.isMobile && !window.useNativeMenuBar
        sourceComponent: MenuBar {
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
                MenuSeparator {}
                MenuItem { action: actWindowed }
                MenuItem { action: actFullscreen }
                MenuItem { action: actBorderless }
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
                MenuItem { action: actAbout }
            }
        }
    }

    // ---- 移动端（Android/iOS）触屏菜单 ----
    // 桌面菜单栏的触控目标太小；改悬浮圆按钮 + 弹出菜单（Material/系统风格
    // 的 MenuItem 自带触屏级尺寸）。动作与桌面菜单完全同一套。
    RoundButton {
        visible: window.isMobile
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 12
        width: 48
        height: 48
        radius: 24
        z: 1000
        text: qsTr("☰")
        font.pixelSize: 24
        onClicked: mobileMenu.open()
    }
    Menu {
        id: mobileMenu
        width: Math.min(window.width - 24, 320)
        MenuItem { action: actOpen }
        MenuItem { action: actReload }
        MenuItem { action: actSaveLog }
        MenuItem { action: actTitle }
        MenuSeparator {}
        MenuItem { action: actSettings }
        MenuItem { action: actAbout }
        MenuSeparator {}
        MenuItem { action: actWindowed }
        MenuItem { action: actFullscreen }
        MenuItem { action: actBorderless }
        MenuSeparator {}
        MenuItem { action: actQuit }
    }

    // ---- 控制台（渲染层）：虚拟舞台 ----
    // 舞台尺寸 = 配置舞台（emuera.config 的ウィンドウ幅/高さ推导），QML 对整个
    // 舞台做**一次等比 transform** 缩放并居中进窗口。窗口拖拽/全屏/跨分辨率
    // 都只是变焦：折行、网格、C++ 排版永远只由舞台尺寸决定，边缘露出
    // window.color（背景色）= 信箱。Qt 文档：Item.scale 级联到全部子项，
    // 输入事件坐标自动按逆变换映射（MouseArea 拿到的仍是舞台本地坐标）。
    Item {
        anchors.centerIn: parent
        width: eraEngine.gui.windowWidth
        height: eraEngine.gui.windowHeight
        scale: Math.min(window.contentItem.width / width,
                        window.contentItem.height / height)

        EraRender {
            id: eraRender
            anchors.fill: parent
            engine: eraEngine
        }
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
        property int loadDone: 0          // 装载进度（已处理文件数）
        property int loadTotal: 0         // 装载进度（总文件数）
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

    // ---- 快捷键：全屏/窗口化切换（执行在 C++ WindowController）----
    Shortcut {
        sequence: "F11"
        onActivated: windowController.toggleFullscreen()
    }
    // Esc 退出全屏（移动端/无边框全屏下没有菜单可点，留这条退路）
    Shortcut {
        sequence: StandardKey.Cancel
        enabled: windowController.windowMode !== "windowed"
        onActivated: windowController.setWindowMode("windowed")
    }

    // ---- 启动收尾 ----
    Component.onCompleted: {
        // macOS：挂 Qt.labs.platform 原生菜单栏到系统菜单栏
        if (useNativeMenuBar) {
            const comp = Qt.createComponent(Qt.resolvedUrl("NativeMenuBar.qml"));
            if (comp.status === Component.Error)
                console.error("加载 NativeMenuBar 失败:", comp.errorString());
            else
                comp.createObject(window, { window: window, actions: menuActions });
        }
        // 移动端默认全屏（窗口模式在 C++，Wayland 之外的桌面不受影响）
        if (isMobile)
            windowController.setWindowMode("fullscreen");

        // 支持命令行直接带游戏目录启动：appemuera <dir>
        const args = Qt.application.arguments;
        for (let i = 1; i < args.length; ++i) {
            if (args[i].startsWith("-"))
                continue;
            // 异步装载：命令行的第一次装载同样不能在 GUI 线程里做全量解析
            eraEngine.loadAsync(args[i]);
            break;
        }
    }

    // 装载完成后自动进入系统状态机（标题画面 → 等待输入）
    Connections {
        target: eraEngine
        function onScriptsLoadStarted() {
            statusStrip.loading = true;
            statusStrip.loadDone = 0;
            statusStrip.loadTotal = 0;
        }
        function onScriptsLoadProgress(processed, total) {
            statusStrip.loadDone = processed;
            statusStrip.loadTotal = total;
        }
        function onScriptsLoaded(ok) {
            statusStrip.loading = false;
            statusStrip.warningCount = eraEngine.parseWarnings().length;
            if (ok)
                eraEngine.runSystem();
        }
    }

    // ---- 装载进度遮罩 ----
    // 异步装载期间 GUI 线程是活的（可以在后台解析的同时重绘），但仍然需要
    // 明确告知「正在装载 + 进度」，否则用户面对黑屏会以为卡死。
    // 遮罩只在装载时出现，装载完成后自动消失（statusStrip.loading）。
    Rectangle {
        id: loadingOverlay
        anchors.fill: parent
        z: 5000
        visible: statusStrip.loading
        color: Qt.rgba(0, 0, 0, 0.55)

        Column {
            anchors.centerIn: parent
            spacing: 12

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("正在装载脚本…")
                color: "white"
                font.pixelSize: 20
            }
            ProgressBar {
                id: loadBar
                width: 320
                from: 0
                to: Math.max(1, statusStrip.loadTotal)
                value: statusStrip.loadDone
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: statusStrip.loadTotal > 0
                      ? statusStrip.loadDone + " / " + statusStrip.loadTotal
                      : qsTr("准备中…")
                color: "#cccccc"
                font.pixelSize: 14
            }
        }
    }
}
