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
// Qt 文档（qmllint / ComponentBehavior: Bound）：LineDelegate、行内区块委托等
// 「嵌套组件」里引用外层 id（root / view / lineItem）在 Bound 下改为**编译期绑定
// 的静态查找**——不再依赖动态作用域，可被 qmlsc 编译进 C++，qmllint 的
// unqualified access 也随之消失。配套要求：外层“属性”（如 backend）在嵌套对象里
// 必须写成 root.backend，模型注入的 model/index 必须声明成 required property。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQml.Models
import io.yigekuoyou.eraengine

// 控制台视图 —— ListView 虚拟化渲染
//
//   C++（ConsoleBackend，本身就是 QAbstractListModel）只负责「规划如何绘制」：
//   打印/排版/摊平某一行，缓冲变更直接翻译成区间信号
//   （insertRows / removeRows / dataChanged / modelReset）。
//   「哪些行需要存在、委托何时创建/复用、滚到哪」全部交给 ListView：
//     * 虚拟化：只实例化可见 + cacheBuffer 范围内的委托（Qt 文档：
//       "ListView will only load as many delegate items as needed"）；
//     * reuseItems: true：滚出视口的委托进池复用（pooled/reused）；
//     * cacheBuffer = maxSpanReach × 行高：跨行立絵/负 ypos 图层在锚点行
//       滚出视口后仍保持可见（池化的前提是「完全滚出视口 + cacheBuffer」）。
//
// 委托 = 一个**显示行**（高度恒为一格）：行内每个最小单位区块一个
// ConsoleBlock（Repeater 建出），跨行图/图层靠溢出绘制 + 视口 clip 裁剪。
// 跨行叠放（后打印的图盖住先打印的立絵）由「后打印的行 = 后创建的委托」
// 天然保序 —— 打印顺序 z 随行号单调，与 C# 逐 part 绘制一致。
FocusScope {
    id: root
    focus: true
    readonly property bool isMobile: Qt.platform.os === "android" || Qt.platform.os === "ios"

    // 屏幕刷新率（Hz）由 C++ 注入（QScreen::refreshRate）——Qt 的 QML Screen
    // 附着类型**没有** refreshRate 属性（Qt 文档 Screen QML Type），旧代码写的
    // `Screen.refreshRate` 恒为 undefined，等于永远按 60 兜底。0 = 未知，按 60。
    property int screenRefreshRate: 0
    property int refreshIntervalMs: Math.max(1, Math.ceil(1000 / (screenRefreshRate > 0 ? screenRefreshRate : 60)))
    function syncCadence(): void {
        if (backend)
            backend.frameMs = refreshIntervalMs;
    }
    onRefreshIntervalMsChanged: syncCadence()
    onBackendChanged: {
        syncCadence();
        syncLayout();
        Qt.callLater(root.syncInputPresentation);
    }

    property ConsoleBackend backend: null   // ConsoleBackend（兼行模型）
    readonly property ConsoleBackend lineModel: backend
    // 标准标题画面（QML）是否显示：为真时隐藏底部输入行与原始输入层。
    property bool titleActive: false
    property int lineHeight: 19
    property string fontName: ""            // 来自 GuiManager
    property int fontSize: 18
    // 空颜色交给 ConsoleBlock/Qt Controls palette 使用系统主题。
    property string foreColor: ""
    property string focusColor: ""
    property string logColor: ""

    // ---- 固定逻辑网格，固定像素格子 ----
    readonly property int gridColumns: backend && backend.gridColumns > 0 ? backend.gridColumns : 80
    readonly property int gridRows: backend && backend.gridRows > 0 ? backend.gridRows : 25
    // 滚动条独占的宽度：视口右缘常驻让出一条空档，滚动条画在空档里
    // 而非盖在内容上（Qt 文档：attached ScrollBar 自动贴边安放且不预留
    // 空间，会遮住最右列文字）。常驻预留而不是 AsNeeded 时才让位，
    // 否则滚动条一出现网格就重排，正在读的行会跳。
    readonly property real scrollBarWidth: 10
    readonly property real cellWidth: Math.max(1, view.width / gridColumns)
    readonly property real cellHeight: Math.max(1, viewport.height / gridRows)

    // ---- 委托（一行 = 一个 delegate；行内区块用 Repeater 建出）----
    component LineDelegate: Item {
        id: lineItem
        required property var model
        required property int index
        readonly property var blocks: model.blocks
        width: ListView.view.width
        height: root.cellHeight
        // 行内区块：文本在行首（col 从 0 算），跨行图/ypos 图层溢出本行绘制，
        // 委托不 clip（Qt 文档禁止 delegate 内 clip），交给 ListView 的 clip。
        Repeater {
            model: lineItem.blocks
            delegate: ConsoleBlock {
                required property var modelData
                blockData: modelData
                backend: root.backend
                cellWidth: root.cellWidth
                cellHeight: root.cellHeight
                fontName: root.fontName
                fontSize: root.fontSize
                foreColor: root.foreColor
                focusColor: root.focusColor
                logColor: root.logColor
                isBacklog: !view.atTail    // C# isBackLog：不在末尾 = 履历（回看）
            }
        }
    }

    // ---- 测试/调试入口（委托按需实例化，可能为 null）----
    // 模型行数（ListView.count 的透传；根 Item 上没有 count）
    readonly property int rowCount: view.count
    // 立即完成 ListView 的挂起布局（同步建出模型变更对应的委托；测试用）
    function syncView() {
        view.forceLayout();
    }
    function lineItem(row) {
        return view.itemAtIndex(row);
    }
    // 第 row 行的第 i 个区块对象（行内顺序 = C++ 打平顺序）
    function blockAt(row, i) {
        const li = view.itemAtIndex(row);
        if (!li)
            return null;
        let n = 0;
        for (let k = 0; k < li.children.length; ++k) {
            const c = li.children[k];
            if (c && c.blockData !== undefined && c.blockData !== null) {
                if (n === i)
                    return c;
                ++n;
            }
        }
        return null;
    }
    // 存活委托里所有区块（诊断用；与 C++ 的 screenBlocks 不同源 —— 只含已建出的）
    function aliveBlockCount() {
        let n = 0;
        for (let r = 0; r < view.count; ++r) {
            const li = view.itemAtIndex(r);
            if (!li)
                continue;
            for (let k = 0; k < li.children.length; ++k)
                if (li.children[k] && li.children[k].blockData)
                    ++n;
        }
        return n;
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
        // 空回车在「有缺省值」时交缺省值，没有缺省值则忽略。
        if (stringInputKind)
            backend.submitStringText(inputField.text);
        else
            backend.submitIntegerText(inputField.text);
        inputField.text = "";
    }

    // 把字号/行高/字体推给 C++（C++ 据此动态重算所有区块的位置与尺寸）
    function syncLayout(): void {
        if (!backend)
            return;
        backend.setFontSize(fontSize);
        backend.setLineHeight(lineHeight);
    }
    onFontSizeChanged: syncLayout()
    onLineHeightChanged: syncLayout()

    // ---- 可视区 ----
    Item {
        id: viewport
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: inputField.top
        focus: root.primitiveInput || (root.isMobile && root.anyKeyInput)

        ListView {
            id: view
            objectName: "consoleListView"
            // 右侧让出滚动条宽度：view 收窄到滚动条左缘，最右列文字
            // 不再被滑条盖住；cellWidth 按收窄后的宽度重算，网格仍然满宽
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.rightMargin: root.scrollBarWidth
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            clip: true                     // 溢出的跨行图在视口边缘裁剪
            model: root.lineModel
            // Qt 文档（ListView::delegateModelAccess，Qt 6.10+）：模型是只读的
            // C++ 模型（ConsoleBackend），委托只**读** role，不写回模型 —— 用
            // DelegateModel.ReadOnly 省掉默认 Qt5ReadWrite 的写保护开销。
            delegateModelAccess: DelegateModel.ReadOnly
            reuseItems: true               // 滚出视口的委托进池复用（Qt 文档 Reusing items）
            cacheBuffer: root.cellHeight * Math.max(2, root.backend ? root.backend.maxSpanReach : 2)
            interactive: !root.primitiveInput
            boundsBehavior: Flickable.StopAtBounds

            // 每次刷新只要有新行就贴到最新行 —— 对齐 C# EmueraConsole 的
            // verticalScrollBarUpdate（每次 RefreshStrings 调用：move > 0 即
            // `ScrollBar.Value += move`，把滚动条推到最底）。C# 没有「上滚就退出
            // 跟随」的粘滞开关：上滚只是回看（履历），下一次输出立刻贴回最新行。
            //
            // 「是否停在末尾」= C# 的 isBackLog（`ScrollBar.Value != Maximum`）：
            // 用 Qt 文档定义的 Flickable.atYEnd（"true if the flickable view is
            // positioned at the end"）；再容一像素 —— 委托高度由视图估算，末尾
            // 可能差一点。
            readonly property bool atTail: count === 0 || atYEnd
                    || contentY >= contentHeight - height - 1

            // 锚到末尾（= C# 把 ScrollBar.Value 设成 Maximum）。
            // Qt 文档（ListView::forceLayout）：ListView 对模型变更的响应按帧批处理
            // —— 收到 rowsInserted 时，新行往往还没被算进 contentHeight，此时
            // positionViewAtEnd() 只能停在**上一帧的末尾**，最新那一行落在视口外；
            // 每次刷新都差一行。先 forceLayout() 让视图立即处理挂起的行插入/删除，
            // 再锚底才落到真正的末尾。
            function scrollToTail() {
                forceLayout();
                positionViewAtEnd();
            }

            onHeightChanged: if (atTail) Qt.callLater(scrollToTail)
            Component.onCompleted: scrollToTail()

            // 新行 / 清屏（C# verticalScrollBarUpdate 的 move > 0 分支）：贴到最新行。
            // Qt 文档：模型变更中不要直接 positionView*，用 Qt.callLater 推迟到事件
            // 循环；推迟里再 forceLayout（见 scrollToTail）。
            Connections {
                target: root.lineModel
                function onRowsInserted() {
                    Qt.callLater(view.scrollToTail);
                }
                // CLEARLINE 先删除旧页再插入新页。仅监听 rowsInserted 时，
                // ListView 可能在删除阶段保留上一页的 contentY，导致新选择项
                // 只有鼠标悬停时才暴露出来。
                function onRowsRemoved() {
                    Qt.callLater(view.scrollToTail);
                }
                function onModelReset() {
                    Qt.callLater(view.scrollToTail);
                }
            }

            // C++/D-Bus/菜单的滚动请求（滚动状态归本视图所有）
            Connections {
                target: root.backend
                function onScrollRequested(lines) {
                    const max = Math.max(0, view.contentHeight - view.height);
                    view.contentY = Math.max(0, Math.min(max, view.contentY - lines * root.cellHeight));
                }
                function onScrollToBottomRequested() {
                    view.scrollToTail();
                }
            }

            delegate: LineDelegate {}

            ScrollBar.vertical: ScrollBar {
                objectName: "consoleScrollBar"
                // Qt 文档（ScrollBar Attached Properties）：attached 滚动条
                // 默认贴着 Flickable 边缘安放且不预留空间（盖在内容上）；
                // 指定别的 parent 即关闭自动几何管理，改为自行布局。
                // 这里挪进右缘让出的空档：不遮内容，高度随视口。
                parent: viewport
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: root.scrollBarWidth
            }
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
                root.backend.submitMouseKey(1, button, Math.round(e.x), Math.round(e.y - viewport.height), -1);
            }
            onWheel: e => root.backend.submitMouseKey(2, e.angleDelta.y, Math.round(e.x), Math.round(e.y - viewport.height), 0)
        }

        // 任意键型等待（WAIT/WAITANYKEY/FORCEWAIT/ANYKEY）：点击控制台任意处即继续。
        // 对齐 C# MainWindow：IsWaitingEnterKey 时左/右键都走 PressEnterKey。
        MouseArea {
            objectName: "anyKeyMouse"
            anchors.fill: parent
            z: 10
            enabled: root.anyKeyInput
            acceptedButtons: Qt.AllButtons
            onPressed: e => {
                root.backend.submitAnyKey();
                e.accepted = true;
            }
        }

        // 滚轮：交给 ListView（Flickable）原生处理 —— 像素级平滑滚动，
        // 触摸板手势直接可用；委托增删由模型信号驱动，不触碰任何数据。

        Keys.onPressed: e => {
            if (!root.backend)
                return;
            if (root.primitiveInput) {
                // INPUTMOUSEKEY：把**原始 Qt 键码与 Qt 修饰符**交给后端，
                // 由 C++ 的键码模型（GameView/key_map.h）在**编译期**已定死的
                // Qt→Emuera 对应关系换算成 (keycode, keydata)（对齐 C#
                // PressPrimitiveKey）。这里不再出现任何数字键码 —— 旧实现把
                // Qt::Key → WinForms Keys 的映射表、F 键算术、修饰位都硬编码在
                // 本文件里，Windows 之外无处复用；现在对应关系只在 C++ 一份。
                root.backend.submitQtKey(e.key, e.modifiers);
                e.accepted = true;
                return;
            }
            if (root.anyKeyInput) {
                // 任意键型等待：回车即继续（点击由 anyKeyMouse 处理）
                if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                    root.backend.submitAnyKey();
                    e.accepted = true;
                }
                return;
            }
            // 网格滚动的键盘语义（旧实现的 PageUp/PageDown/End）
            const page = root.gridRows;
            if (e.key === Qt.Key_PageUp) {
                view.contentY = Math.max(0, view.contentY - page * root.cellHeight);
                e.accepted = true;
            } else if (e.key === Qt.Key_PageDown) {
                view.contentY = Math.min(Math.max(0, view.contentHeight - view.height),
                                         view.contentY + page * root.cellHeight);
                e.accepted = true;
            } else if (e.key === Qt.Key_End) {
                view.scrollToTail();
                e.accepted = true;
            }
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
    // 高度只有一行文字；直接键入、回车即提交。非等待状态高度为 0。
    TextField {
        id: inputField
        objectName: "consoleInputField"
        // 输入控件始终存在并保留焦点目标；只有真正等待 INPUT 时才可见、可编辑。
        // 这样重绘/翻页期间不会因控件销毁重建抢走鼠标事件。
        focus: root.inputActive && !root.isMobile
        enabled: root.inputActive && !(root.isMobile && root.anyKeyInput)
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: visible ? implicitHeight : 0
        visible: root.inputRowVisible
        background: null                 // 无边框：去掉 TextField 默认描边
        topPadding: 0
        bottomPadding: 0
        leftPadding: 4
        rightPadding: 4
        font.family: root.fontName
        font.pixelSize: root.fontSize
        // TextField 的 palette.windowText 在深色主题下可能与控制台背景相同；
        // 使用控制台前景色，并让 placeholder 明确采用 disabled/次级颜色。
        color: root.foreColor !== "" ? root.foreColor : palette.text
        placeholderTextColor: root.foreColor !== "" ? root.foreColor : palette.placeholderText
        placeholderText: root.backend && root.backend.waitingInput
            ? (root.anyKeyInput ? ("回车/点击继续（" + root.backend.inputKind + "）")
                                : ("输入（" + root.backend.inputKind + "）"))
            : ""
        // 输入类型分支限制：整数型输入只接受数字（INPUT 可负）
        validator: root.backend && root.backend.waitingInput && !root.stringInputKind ? intOnly : null
        inputMethodHints: root.stringInputKind ? Qt.ImhNone
                          : Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
        onAccepted: root.submit()
    }
    RegularExpressionValidator {
        id: intOnly
        regularExpression: /-?[0-9]+/
    }

    // 等待类型分支（与旧实现一致）
    readonly property bool inputActive: backend && backend.waitingInput && !primitiveInput && !titleActive
    readonly property bool primitiveInput: backend && backend.waitingInput && backend.inputKind === "INPUTMOUSEKEY"
    readonly property bool anyKeyInput: {
        if (!backend || !backend.waitingInput)
            return false;
        const k = backend.inputKind.toUpperCase();
        return k === "WAIT" || k === "WAITANYKEY" || k === "FORCEWAIT" || k === "ANYKEY";
    }
    readonly property bool stringInputKind: {
        if (!backend || !backend.waitingInput)
            return false;
        const k = backend.inputKind.toUpperCase();
        return k.indexOf("INPUTS") >= 0 || k.indexOf("ARGS") >= 0;
    }
    // Timed input briefly completes before the script redraws and requests it
    // again. Coalesce these transitions so the viewport and focus stay stable.
    // 输入栏始终保留在布局中，避免等待状态切换时 viewport 高度和页面位置
    // 突然变化；真正的提交权限由 enabled / submitInput* 的 C++ 状态校验决定。
    property bool inputRowVisible: true
    function syncInputPresentation(): void {
        inputRowVisible = true;
    }
    onTitleActiveChanged: Qt.callLater(root.syncInputPresentation)
    Connections {
        target: root.backend
        function onWaitingInputChanged() { Qt.callLater(root.syncInputPresentation); }
        function onInputRequested() { Qt.callLater(root.syncInputPresentation); }
    }

    Component.onCompleted: {
        syncCadence();
        syncLayout();
        syncInputPresentation();
    }
}
