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
import QtQuick.Window

// 控制台视图 —— root 层 + 分层渲染
//
//   root 层（本组件）     ：可见窗口容器（滚动/裁剪/输入条/原生滚动条）
//   text 层（textLayer）  ：所有文本区块
//   image 层（imageLayer）：所有图片区块
//   shape 层（shapeLayer）：所有图形区块
//
// 三个层用 inline component `BlockLayer` 建模（结构相同、模型不同），
// 都是 root 的子 Item，坐标同源：
//   * 区块的**绝对位置**就是它在层内的 x/y（同 root 坐标系）；
//   * 尺寸（cols/rows）由 C++ 按当前字体/字号/资源**动态测量**后给出。
//
// **位置与尺寸都是 C++ 说了算**（对齐 C# 的 SetAlignment / CalcPointX / SetWidth）：
// QML 只负责「按数据把区块对象创建到对应的层里」。层内对象用 Instantiator 创建，
// 模型变化（滚动/输出/换字号）时自动增删。
Item {
    id: root

    // Timer pacing follows the display cadence. QML may combine notifications in
    // one rendered frame; a model update is not a promise of a physical frame.
    property int refreshIntervalMs: Math.max(1, Math.ceil(1000 / (Screen.refreshRate > 0 ? Screen.refreshRate : 60)))
    function syncCadence() {
        if (backend)
            backend.frameMs = refreshIntervalMs;
    }
    onRefreshIntervalMsChanged: syncCadence()
    onBackendChanged: {
        syncCadence();
        syncLayout();
    }

    // ---- 滚动合并（滚动卡顿的主因之一）----
    // 以前每个 wheel 事件直接 `backend.scrollBy()`：C++ 立刻重算整窗并重发
    // text/image/shape 三个模型 → QML 三个 Instantiator 把**整屏**区块对象销毁重建
    // （每个文本区块内部还有按段/字的 Repeater）。触摸板一秒能发上百个事件，
    // 就变成每秒上百次全屏重建。这里按屏幕刷新周期合并：一帧最多提交一次，
    // 期间累计的增量一次性应用（Qt 文档推荐的「把多次更新合并到一个渲染帧」）。
    property int pendingScroll: 0
    function scrollByLines(lines) {
        if (!backend)
            return;
        pendingScroll += lines;
        if (!scrollCoalesce.running)
            scrollCoalesce.start();
    }
    Timer {
        id: scrollCoalesce
        interval: root.refreshIntervalMs
        repeat: true
        onTriggered: {
            if (!root.backend || root.pendingScroll === 0) {
                scrollCoalesce.stop();
                return;
            }
            const d = root.pendingScroll;
            root.pendingScroll = 0;
            root.backend.scrollBy(d);
        }
    }
    readonly property bool primitiveInput: backend && backend.waitingInput && backend.inputKind === "INPUTMOUSEKEY"
    // 当前等待的是否为「任意键」型（WAIT/WAITANYKEY/FORCEWAIT/ANYKEY）：
    // 点击控制台任意处或回车即继续（C# IsWaitingEnterKey）；不弹数字/文本输入框
    readonly property bool anyKeyInput: {
        if (!backend || !backend.waitingInput)
            return false;
        const k = backend.inputKind.toUpperCase();
        return k === "WAIT" || k === "WAITANYKEY" || k === "FORCEWAIT" || k === "ANYKEY";
    }
    // 当前等待的是否为字符串型输入（INPUTS 系）；整数型 INPUT 一律走数字校验
    readonly property bool stringInputKind: {
        if (!backend || !backend.waitingInput)
            return false;
        const k = backend.inputKind.toUpperCase();
        return k.indexOf("INPUTS") >= 0 || k.indexOf("ARGS") >= 0;
    }
    onPrimitiveInputChanged: {
        if (primitiveInput)
            viewport.forceActiveFocus();
    }
    property var backend: null              // ConsoleBackend
    property int lineHeight: 19
    property string fontName: ""            // 来自 GuiManager
    property int fontSize: 18
    // 空颜色交给 ConsoleBlock/Qt Controls palette 使用系统主题。
    property string foreColor: ""
    property string focusColor: ""
    property string logColor: ""

    // ---- 固定逻辑网格，固定像素格子 ----
    // 网格与格子尺寸都只由舞台（配置）决定：本组件的宽高 = 舞台尺寸，
    // 窗口缩放是外层的整体 transform，不会触发这里任何绑定重算。
    readonly property int gridColumns: backend && backend.gridColumns > 0 ? backend.gridColumns : 80
    readonly property int gridRows: backend && backend.gridRows > 0 ? backend.gridRows : 25
    readonly property real cellWidth: Math.max(1, width / gridColumns)
    readonly property real cellHeight: Math.max(1, viewport.height / gridRows)

    // 单一「增量区块模型」（C++：QAbstractListModel，滚动/追加只产生
    // insertRows/removeRows，未变化的行原地复用 —— 不再随每帧整屏重建）。
    // Qt 文档（Performance considerations：Sequence tips）：值序列
    // （QVariantList）每次变化都整表通知，delegate 全量重建；模型行 +
    // 细粒度信号才是增量路径。
    readonly property var blockModel: backend ? backend.blockModel : null
    // 窗口顶行的绝对行号：区块 y = (row - windowTopRow + offsetRows) × 行高。
    // 滚动只改这一个值（绑定重求值），模型内容不动。
    readonly property int windowTopRow: backend ? backend.windowTopRow : 0
    readonly property int contentHeight: backend ? backend.contentHeight : 0

    // 可见区块数 / 按 kind 取区块（供测试与调试；运行时 QML 不读）
    function countBlocksOfKind(kind) {
        let n = 0;
        for (let i = 0; i < blockInst.count; ++i) {
            const o = blockInst.objectAt(i);
            if (o && o.blockData && o.blockData.kind === kind)
                ++n;
        }
        return n;
    }
    function blockOfKindAt(kind, i) {
        let n = 0;
        for (let k = 0; k < blockInst.count; ++k) {
            const o = blockInst.objectAt(k);
            if (o && o.blockData && o.blockData.kind === kind) {
                if (n === i)
                    return o;
                ++n;
            }
        }
        return null;
    }
    readonly property int textBlockCount: blockInst.count >= 0 ? countBlocksOfKind("text") : 0
    readonly property int imageBlockCount: blockInst.count >= 0 ? countBlocksOfKind("image") : 0
    readonly property int shapeBlockCount: blockInst.count >= 0 ? countBlocksOfKind("shape") : 0
    function textBlockAt(i) {
        return blockOfKindAt("text", i);
    }
    function imageBlockAt(i) {
        return blockOfKindAt("image", i);
    }
    function shapeBlockAt(i) {
        return blockOfKindAt("shape", i);
    }
    function blockAt(i) {
        return textBlockAt(i);
    }

    function submit() {
        if (!backend)
            return;
        // 任意键型等待（WAIT/WAITANYKEY/FORCEWAIT/ANYKEY）：回车即继续，输入内容不使用
        if (anyKeyInput) {
            backend.submitAnyKey();
            inputField.text = "";
            return;
        }
        // 与 C++ 的 inputExpectsString 同源：INPUTS / SINPUTS / TONEINPUTS / ARGS 系
        // 都是字符串型输入，其余（INPUT/TINPUT/ONEINPUT…）走整数校验。
        // 交由 C++ 判定空输入 / 非法输入（对齐 C# doInputToEmueraProgram）：
        // 空回车在「有缺省值」时交缺省值，没有缺省值则忽略 —— 以前这里写
        // `parseInt(text) || 0`，空回车会交 0（eraTW 外出列表里 0 == MAIN_MAP
        // 就是「从外面回家」，即「没操作就自动返回」）。
        if (stringInputKind)
            backend.submitStringText(inputField.text);
        else
            backend.submitIntegerText(inputField.text);
        inputField.text = "";
    }

    // 把字号/行高/字体推给 C++（C++ 据此动态重算所有区块的位置与尺寸）
    function syncLayout() {
        if (!backend)
            return;
        backend.setFontSize(fontSize);
        backend.setLineHeight(lineHeight);
    }
    onFontSizeChanged: syncLayout()
    onLineHeightChanged: syncLayout()

    // ---- 分层（text/image/shape 结构相同，仅模型不同）----
    // delegate 抽成 inline component，消除三份重复；层本身保留显式 id
    // （Instantiator 的测试/调试入口：view.textBlockCount / blockAt(i)）。
    component BlockDelegate: ConsoleBlock {
        // 模型角色名 "block"（ConsoleBlockModel::roleNames）。
        // 用 required property 走 Qt 文档的模型角色绑定路径。
        required property var block
        blockData: block
        windowTopRow: root.windowTopRow
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

    Item {
        id: viewport
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: inputField.top
        clip: true
        focus: root.primitiveInput

        // ---- 单一区块层（text/image/shape 共用一个父 Item）----
        // Qt 文档：Item 的叠放顺序只在**兄弟之间**由 z 决定 —— 以前分成三个
        // 兄弟 Item 层，「后打印的文字盖住先打印的图」「图层特效压在立绘上」
        // 这类跨 kind 叠加永远做不到（整层整体上下）。现在三个 Instantiator
        // 都把对象挂进同一个 blockLayer，每个区块带 C++ 给的扁平 z
        // （= 控制台打印顺序，见 ConsoleBlock.z），叠加与 C# 逐 part 绘制一致。
        Item {
            id: blockLayer
            anchors.fill: parent

            // 单一 Instantiator：模型是 QAbstractListModel，行级增删信号驱动
            // delegate 的增量创建/销毁（未变化的行原地复用）。
            Instantiator {
                id: blockInst
                model: root.blockModel
                delegate: BlockDelegate {}
                // Instantiator 不把对象挂进可视树：显式设 parent；销毁由它负责
                onObjectAdded: (index, object) => {
                    object.parent = blockLayer;
                }
                onObjectRemoved: (index, object) => {
                    object.parent = null;
                }
            }
        }

        // ---- 原生纵向滚动条（绑定 C++ 的滚动状态，可拖拽）----
        ScrollBar {
            id: vbar
            orientation: Qt.Vertical
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            visible: backend && backend.lineCount > backend.visibleCount
            property bool syncing: false      // 程序性回设不当作用户拖动
            function syncFromBackend() {
                syncing = true;
                const total = backend ? backend.lineCount : 0;
                if (total <= 0) {
                    size = 1;
                    position = 0;
                    syncing = false;
                    return;
                }
                const maxOffset = Math.max(0, total - backend.visibleCount);
                const first = Math.max(0, total - backend.visibleCount - backend.scrollOffset);
                size = Math.max(0.02, Math.min(1, backend.visibleCount / total));
                position = Math.min(1 - size, Math.max(0, first / total));
                syncing = false;
            }
            onPositionChanged: {
                if (syncing || !backend)
                    return;
                const total = backend.lineCount;
                const maxOffset = Math.max(0, total - backend.visibleCount);
                // 窗口顶行 = position × 总行数；换算回「距底部」的 scrollOffset
                const first = position * total;
                backend.scrollOffset = Math.max(0, Math.min(maxOffset, Math.round(maxOffset - first)));
                syncFromBackend();
            }
            Connections {
                target: backend
                function onWindowChanged() {
                    vbar.syncFromBackend();
                }
            }
            Component.onCompleted: syncFromBackend()
        }

        MouseArea {
            objectName: "primitiveMouse"
            anchors.fill: parent
            z: 10
            enabled: root.primitiveInput
            acceptedButtons: Qt.AllButtons
            onPressed: e => {
                // WinForms MouseButtons values; coordinates relative to the lower left.
                const button = e.button === Qt.LeftButton ? 1048576 : e.button === Qt.RightButton ? 2097152 : e.button === Qt.MiddleButton ? 4194304 : e.button === Qt.BackButton ? 8388608 : 16777216;
                backend.submitMouseKey(1, button, Math.round(e.x), Math.round(e.y - viewport.height), -1);
            }
            onWheel: e => backend.submitMouseKey(2, e.angleDelta.y, Math.round(e.x), Math.round(e.y - viewport.height), 0)
        }

        // 任意键型等待（WAIT/WAITANYKEY/FORCEWAIT/ANYKEY）：点击控制台任意处即继续。
        // 对齐 C# MainWindow：IsWaitingEnterKey 时左/右键都走 PressEnterKey —— 此前
        // 点击控制台毫无响应（MouseArea 只在 INPUTMOUSEKEY 启用），DQPRINT 结尾的
        // WAIT 只能靠底部输入框提交数字才能通过，看起来「一直等待」。
        MouseArea {
            objectName: "anyKeyMouse"
            anchors.fill: parent
            z: 10
            enabled: root.anyKeyInput
            acceptedButtons: Qt.AllButtons
            onPressed: e => {
                backend.submitAnyKey();
                e.accepted = true;
            }
        }

        WheelHandler {
            enabled: !root.primitiveInput
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: e => {
                if (!backend)
                    return;
                root.scrollByLines(e.angleDelta.y > 0 ? 3 : -3);
            }
        }

        Keys.onPressed: e => {
            if (!backend)
                return;
            if (root.primitiveInput) {
                const special = {};
                special[Qt.Key_Return] = 13;
                special[Qt.Key_Enter] = 13;
                special[Qt.Key_Escape] = 27;
                special[Qt.Key_Backspace] = 8;
                special[Qt.Key_Tab] = 9;
                special[Qt.Key_Left] = 37;
                special[Qt.Key_Up] = 38;
                special[Qt.Key_Right] = 39;
                special[Qt.Key_Down] = 40;
                special[Qt.Key_PageUp] = 33;
                special[Qt.Key_PageDown] = 34;
                special[Qt.Key_End] = 35;
                special[Qt.Key_Home] = 36;
                special[Qt.Key_Insert] = 45;
                special[Qt.Key_Delete] = 46;
                let key = special[e.key] !== undefined ? special[e.key] : e.key;
                if (e.key >= Qt.Key_F1 && e.key <= Qt.Key_F24)
                    key = 112 + e.key - Qt.Key_F1;
                const mods = ((e.modifiers & Qt.ShiftModifier) ? 65536 : 0) | ((e.modifiers & Qt.ControlModifier) ? 131072 : 0) | ((e.modifiers & Qt.AltModifier) ? 262144 : 0);
                backend.submitMouseKey(3, key, key | mods, 0, 0);
                e.accepted = true;
                return;
            }
            if (root.anyKeyInput) {
                // 任意键型等待：回车即继续（点击由 anyKeyMouse 处理）
                if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                    backend.submitAnyKey();
                    e.accepted = true;
                }
                return;
            }
            if (e.key === Qt.Key_PageUp)
                root.scrollByLines(10);
            if (e.key === Qt.Key_PageDown)
                root.scrollByLines(-10);
            if (e.key === Qt.Key_End)
                backend.scrollToBottom();
        }
    }

    // CLEARTEXTBOX（C# Console.ClearTextBox）：清空输入栏内容
    Connections {
        target: root.backend
        function onClearTextBoxRequested() {
            inputField.text = "";
        }
    }

    // 底部输入行：等待输入时贴着最下面出现——无边框、无背景、无按钮，
    // 高度只有一行文字（去掉旧 inputBar 的 38px 背景条，不再撑大控制台）；
    // 直接键入、回车即提交。非等待状态高度为 0，不影响布局。
    TextField {
        id: inputField
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: visible ? implicitHeight : 0
        visible: backend && backend.waitingInput && !root.primitiveInput
        background: null                 // 无边框：去掉 TextField 默认描边
        topPadding: 0
        bottomPadding: 0
        leftPadding: 4
        rightPadding: 4
        font.family: root.fontName
        font.pixelSize: root.fontSize
        color: root.foreColor !== "" ? root.foreColor : palette.windowText
        placeholderText: backend && backend.waitingInput
            ? (root.anyKeyInput ? ("回车/点击继续（" + backend.inputKind + "）")
                                : ("输入（" + backend.inputKind + "）"))
            : ""
        // 输入类型分支限制：整数型输入只接受数字（INPUT 可负）
        validator: backend && backend.waitingInput && !root.stringInputKind ? intOnly : null
        onVisibleChanged: if (visible)
            inputField.forceActiveFocus()
        onAccepted: root.submit()
    }
    RegularExpressionValidator {
        id: intOnly
        regularExpression: /-?[0-9]+/
    }

    Component.onCompleted: {
        syncCadence();
        syncLayout();
        // visibleCount 由 C++ 按 gridRows 固定（可见行数 = 逻辑行数），
        // 不再按视口像素高度回写
    }
}
