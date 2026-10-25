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
    objectName: "consoleRoot"     // D-Bus diagBlocks 读取实际 cell/font 用
    focus: true
    readonly property bool isMobile: Qt.platform.os === "android" || Qt.platform.os === "ios"

    // C++ ConsoleBackend.frameMs 是游戏配置的逻辑刷新节拍（C# FPS）。
    // 它不能绑定屏幕刷新率：eraTetris 的 FPS=5 意味着 200ms 一帧，
    // 若按显示器的 60Hz 改成 16ms，TONEINPUT/按钮循环会比原版快很多。
    // Qt Quick 自己负责合成和屏幕刷新；控制台模型只按游戏节拍发布快照。

    // ---- 帧节拍闸门（C# EmueraConsole.need_settimer）----
    // C++ 的限时输入不在语句处启动计时器：先请视图处理完这一帧（建委托、定位），
    // 视图回报 notifyFramePresented() 后才开始倒计时。视图挂上即置真，
    // 销毁时摘掉（此后 C++ 退回「立即计时」，不会因为没人应答而卡死）。
    function attachFramePacing(): void {
        if (backend)
            backend.framePacingAttached = true;
    }
    onBackendChanged: {
        attachFramePacing();
        syncLayout();
        updateGrid();
    }
    Component.onDestruction: if (backend) backend.framePacingAttached = false

    // 输入等待这一帧：把新帧的委托同步建出来并摆好位置，再放行 C++ 的计时器。
    // Qt.callLater：等模型的 rowsInserted/removeRows 通知在本轮事件循环处理完，
    // 且晚于 C++ 在 emit 之后设置「等待中的计时器」。
    Connections {
        target: root.backend
        function onPresentationRequested() {
            Qt.callLater(function() {
                if (!root.backend)
                    return;
                root.syncView();
                if (root.showingStage)
                    stageView.positionStage();   // id 不是属性：不能写 root.stageView
                root.backend.notifyFramePresented();
            });
        }
    }

    property ConsoleBackend backend: null   // ConsoleBackend（兼行模型）

    // ---- 工具提示样式（C# TOOLTIP_* 指令族）----
    // Qt 文档 qml-qtquick-controls-tooltip 的「Attached Tool Tips」：所有用
    // 附加属性 ToolTip 的项**共用一个实例**（Qt 为此省资源），`ToolTip.toolTip`
    // 拿到那个共享实例 —— 字体/延迟/颜色写到它上面即可对全部按钮生效。
    // 延迟与显示时长按**每项**存储，所以按钮委托各自绑定 backend.toolTipDelay。
    function applyToolTipStyle(): void {
        if (!backend)
            return;
        const tip = ToolTip.toolTip;
        if (!tip)
            return;
        // OwnerDraw（TOOLTIP_CUSTOM）打开时才用脚本设的字体（C# 的语义：
        // 不打开 OwnerDraw 就是系统默认外观，SetToolTipFont* 只是记下值）。
        // Qt 6.9 起 `Qt.font({})` 会报 “no valid font subproperties specified”
        // 并在首屏日志里打一条 Error：空对象不再被接受。给出明确的 family /
        // pointSize（缺省跟随应用字体）后再赋值。
        if (backend.toolTipCustom) {
            tip.font = Qt.font({
                family: backend.toolTipFontFamily !== ""
                            ? backend.toolTipFontFamily : Qt.application.font.family,
                pointSize: backend.toolTipPointSize > 0
                            ? backend.toolTipPointSize : Qt.application.font.pointSize
            });
        } else {
            tip.font = Qt.application.font;
        }
        tip.delay = backend.toolTipDelay;
        tip.timeout = backend.toolTipDuration;
        // 颜色（TOOLTIP_SETCOLOR）：palette 影响样式提供的标签文字/底色；
        // 主题是否采纳取决于控件样式（Basic/Fusion 会采纳）。
        if (backend.toolTipForeColor.a > 0 || backend.toolTipBackColor.a > 0) {
            if (backend.toolTipForeColor.a > 0)
                tip.palette.windowText = backend.toolTipForeColor;
            if (backend.toolTipBackColor.a > 0)
                tip.palette.window = backend.toolTipBackColor;
        }
    }
    Connections {
        target: root.backend
        function onToolTipStyleChanged() { root.applyToolTipStyle() }
    }
    property EraEngine engine: null
    readonly property var lineModel: backend ? backend.historyModel : null
    readonly property var stageModel: backend ? backend.stageModel : null
    // 标准标题画面（QML）是否显示：为真时隐藏底部输入行与原始输入层。
    property bool titleActive: false
    property int lineHeight: 19
    property string fontName: ""            // 来自 GuiManager
    property int fontSize: 18
    // 空颜色交给 ConsoleBlock/Qt Controls palette 使用系统主题。
    property string foreColor: ""
    property string focusColor: ""
    property string logColor: ""

    // ---- 网格与字号同源（窗口多大，就显示多少列/行）----
    // 单元格像素 = 字号/2（半角宽）× max(行高, 字号+1)（行距至少比字高 1px）。
    // 列/行 = 视口像素 ÷ 单元格像素；再回算 cellWidth/cellHeight 把余量摊给
    // 单元格，保证 columns*cellWidth ≤ 视口宽、rows*cellHeight ≤ 视口高。
    //
    // 这是「字号不随格子乱来」的唯一来源：
    //   * 以前 grid 固定为配置值、整块 stage 再做 Item.scale —— 窗口放大时
    //     格子被拉成 22×31 px 而字号仍是 18（字距散开、横线铺满整屏）；
    //     窗口缩小时格子小于字高（文字上下重叠、按钮挤成一条）。
    //   * 现在窗口放大 → 列/行变多，cell 仍是字号量级；窗口缩小 → 列/行
    //     变少，字号不变（只有极端尺寸才按 cellHeight-1 自动缩字号）。
    // C++ 排版用的 grid 由 updateGrid() 同步推过去（含折行宽度），
    // QML 不再读取 backend.gridColumns 作为显示几何。
    readonly property real scrollBarWidth: 10
    // 底部留 1px：最后一行文字的下缘不会被视口 clip 切掉。
    readonly property real gridWidthPx: Math.max(1, width - scrollBarWidth)
    readonly property real gridHeightPx: Math.max(1, height - 1)
    // 基准格宽/格高不单独存在：格宽来自 fontSize（半角 = 字号/2），
    // 格高的下界 lineHeight 与「装得下字高」的 fontSize+1 都写在 rowsFor() 里，
    // 保证「派生网格」与「推给 C++ 的网格」永远同一份公式。
    // 网格推导做成纯函数：属性绑定与 updateGrid() 用**同一份**公式。
    // （QML 的属性变更信号早于派生绑定重算，在 onFontSizeChanged 里直接读
    //  gridColumns 会拿到上一次的值 —— 测试里就表现为「字号 19、网格还是 18」。）
    function columnsFor(px, fontPx): int {
        return Math.max(1, Math.floor(Math.max(1, px - scrollBarWidth)
                                      / Math.max(1, fontPx / 2)));
    }
    function rowsFor(px, linePx, fontPx): int {
        return Math.max(1, Math.floor(Math.max(1, px - 1)
                                      / Math.max(1, linePx, fontPx + 1)));
    }
    readonly property int gridColumns: columnsFor(width, fontSize)
    readonly property int gridRows: rowsFor(height, lineHeight, fontSize)
    readonly property real cellWidth: Math.max(1, gridWidthPx / gridColumns)
    readonly property real cellHeight: Math.max(1, gridHeightPx / gridRows)
    // 渲染字号：不超过配置字号，也不超过单元格能容纳的大小（行高留 1px）。
    // 极端窄/矮窗口下字号自动缩小，绝不与相邻行重叠、也不越过格宽。
    readonly property real renderFontSize: Math.max(1, Math.min(fontSize,
                                                     Math.min(2 * cellWidth, cellHeight - 1)))

    // 当前舞台和历史是互斥页面；历史刷新不能覆盖用户的回看位置。
    property bool reviewingHistory: false
    readonly property bool stageActive: backend && backend.stageMode && stageView.count > 0
    function showCurrent(): void {
        reviewingHistory = false;
        if (stageActive)
            Qt.callLater(stageView.positionStage);
        else
            view.scrollToTail();
    }
    function scrollHistory(lines): void {
        if (stageActive && !reviewingHistory) {
            if (lines <= 0 || view.count === 0)
                return;
            reviewingHistory = true;
            view.forceLayout();
            view.positionViewAtEnd();
        }
        view.scrollByLines(lines);
    }

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
                fontSize: root.renderFontSize
                foreColor: root.foreColor
                focusColor: root.focusColor
                logColor: root.logColor
                isBacklog: root.reviewingHistory || (!root.showingStage && !view.atTail)    // 舞台始终是当前画面；历史视图按滚动位置回看
            }
        }
    }

    // 历史与舞台共用几何、委托和跨行图片显示范围。
    component ConsoleView: ListView {
        anchors.fill: parent
        anchors.rightMargin: root.scrollBarWidth
        clip: true
        reuseItems: true
        delegateModelAccess: DelegateModel.ReadOnly
        // cacheBuffer 只保留实例；displayMargin 才允许离屏锚点绘制。
        displayMarginBeginning: root.cellHeight * Math.max(2, root.backend ? root.backend.maxSpanReach : 2)
        displayMarginEnd: displayMarginBeginning
        cacheBuffer: displayMarginBeginning
        delegate: LineDelegate {}
    }

    // ---- 测试/调试入口（委托按需实例化，可能为 null）----
    // 模型行数（ListView.count 的透传；根 Item 上没有 count）
    readonly property bool showingStage: root.backend && root.backend.stageMode && stageView.visible
    readonly property int rowCount: showingStage ? stageView.count : view.count
    // 立即完成 ListView 的挂起布局（同步建出模型变更对应的委托；测试用）
    function syncView() {
        (showingStage ? stageView : view).forceLayout();
    }
    function lineItem(row) {
        return (showingStage ? stageView : view).itemAtIndex(row);
    }
    // 第 row 行的第 i 个区块对象（行内顺序 = C++ 打平顺序）
    function blockAt(row, i) {
        const li = (showingStage ? stageView : view).itemAtIndex(row);
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
        const activeView = showingStage ? stageView : view;
        for (let r = 0; r < activeView.count; ++r) {
            const li = activeView.itemAtIndex(r);
            if (!li)
                continue;
            for (let k = 0; k < li.children.length; ++k)
                if (li.children[k] && li.children[k].blockData)
                    ++n;
        }
        return n;
    }

    // 把字号/行高/字体推给 C++（C++ 据此动态重算所有区块的位置与尺寸）
    function syncLayout(): void {
        if (!backend)
            return;
        backend.setFontSize(fontSize);
        backend.setLineHeight(lineHeight);
    }
    // 把「视口像素 + 字号」推导出的列/行推给 C++（同一份网格用于折行、
    // DRAWLINE 与舞台模型）。幂等：值相同就 no-op，C++ 的 gridChanged
    // 再回调到这里时不会形成循环。
    function updateGrid(): void {
        if (!backend)
            return;
        const cols = columnsFor(width, fontSize);
        const rows = rowsFor(height, lineHeight, fontSize);
        if (backend.gridColumns !== cols)
            backend.setGridColumns(cols);
        if (backend.gridRows !== rows)
            backend.setGridRows(rows);
    }
    onFontSizeChanged: { syncLayout(); updateGrid(); }
    onLineHeightChanged: { syncLayout(); updateGrid(); }
    onWidthChanged: updateGrid()
    onHeightChanged: updateGrid()
    // 装载/设置变化时 C++ 会按配置重设网格（EraEngine::syncConsoleGrid）；
    // 显示层再按实际视口推回一次，保证两边始终同源。
    Connections {
        target: root.backend
        function onGridChanged() { root.updateGrid() }
    }

    // ---- SETBGIMAGE：背景图层（C# ConsoleBackground）----
    // 贴在正文**后面**：列表深度降序（后画的深度小、盖在上面），保持长宽比
    // 完整放入视口（C# BakeBackground:726 取宽高缩放比较小者；本移植用
    // PreserveAspectFit 的居中版本 —— 无测试游戏使用，视觉差可忽略）。
    Item {
        id: bgLayer
        anchors.fill: parent
        z: -1            // 一切正文之前
        Repeater {
            model: root.backend ? root.backend.backgroundImages : []
            delegate: Image {
                required property var modelData
                anchors.fill: parent
                source: "image://emuera/" + modelData.name
                // C# BakeBackground：按「填满窗口」缩放（`cropHorizontally =
                // bh*scaleW < H` -> 按宽或高取大者），即 cover 而不是 fit；
                // 横向居中（paddingX = (W - newWidth) / 2）、纵向从 y=0 起画。
                fillMode: Image.PreserveAspectCrop
                opacity: modelData.alpha / 255
                asynchronous: true
                verticalAlignment: Image.AlignTop
            }
        }
    }

    // ---- 可视区 ----
    Item {
        id: viewport
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        focus: root.primitiveInput || (root.isMobile && root.anyKeyInput)

        // ---- HTML_PRINT_ISLAND：完全独立的 HTML 层（C# _htmlElementList）----
        // 不随正文滚动：从渲染区顶端 y=0 起按行高排列；C# 在普通文本/CBG 之后
        // 绘制（盖在文字上）、工具提示之前。岛层按钮已由 C++ 禁用（enabled=false），
        // 点击只属于正文 —— 这里按「每岛行 = 一个行委托」复用 ConsoleBlock 渲染。
        Item {
            id: islandLayer
            anchors.top: viewport.top
            anchors.left: viewport.left
            anchors.right: viewport.right
            height: root.cellHeight * (root.backend ? root.backend.htmlIsland.length : 0)
            z: 10                      // 正文（ListView）之上
            visible: height > 0
            Repeater {
                model: root.backend ? root.backend.htmlIsland : []
                delegate: Item {
                    id: islandRow
                    required property var modelData
                    required property int index
                    width: islandLayer.width
                    height: root.cellHeight
                    y: index * root.cellHeight
                    Repeater {
                        model: islandRow.modelData.blocks
                        delegate: ConsoleBlock {
                            required property var modelData
                            blockData: modelData
                            backend: root.backend
                            cellWidth: root.cellWidth
                            cellHeight: root.cellHeight
                            fontName: root.fontName
                            fontSize: root.renderFontSize
                            foreColor: root.foreColor
                            focusColor: root.focusColor
                            logColor: root.logColor
                            isBacklog: false
                        }
                    }
                }
            }
        }

        // 独立的滚动条槽位。两个 ListView 都让出这段宽度，滚动条不会
        // 覆盖舞台最右列，也不会因为滚动条出现/消失而改变舞台网格宽度。
        Item {
            id: scrollBarSlot
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: root.scrollBarWidth
        }

        ConsoleView {
            id: view
            objectName: "consoleListView"
            visible: !root.showingStage
            model: root.lineModel
            interactive: !root.primitiveInput
            boundsBehavior: Flickable.StopAtBounds

            // 普通日志模式跟随新增输出；舞台模式下历史回看位置独立保存。
            // Qt ListView 删除头部行后 originY 可以改变，末尾判断必须减去它。
            readonly property bool atTail: count === 0 || atYEnd || contentY - originY >= contentHeight - height - 1

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

            function scrollByLines(lines) {
                forceLayout();
                const first = indexAt(1, contentY + 1);
                const row = first >= 0 ? first : 0;
                positionViewAtIndex(Math.max(0, Math.min(count - 1, row - lines)), ListView.Beginning);
            }

            function followOutput() {
                if (!root.backend || !root.backend.stageMode)
                    scrollToTail();
            }

            onHeightChanged: if (atTail && !root.reviewingHistory)
                Qt.callLater(followOutput)
            Component.onCompleted: scrollToTail()

            // 合并模型通知，布局完成后仅为普通日志模式定位到末尾。
            Connections {
                target: root.lineModel
                function onRowsInserted() {
                    Qt.callLater(view.followOutput);
                }
                // CLEARLINE 先删除旧页再插入新页。仅监听 rowsInserted 时，
                // ListView 可能在删除阶段保留上一页的 contentY，导致新选择项
                // 只有鼠标悬停时才暴露出来。
                function onRowsRemoved() {
                    Qt.callLater(view.followOutput);
                }
                function onModelReset() {
                    Qt.callLater(view.followOutput);
                }
            }

            // C++/D-Bus/菜单的滚动请求（滚动状态归本视图所有）
            Connections {
                target: root.backend
                function onScrollRequested(lines) {
                    root.scrollHistory(lines);
                }
                function onScrollToBottomRequested() {
                    root.showCurrent();
                }
            }

            WheelHandler {
                enabled: root.reviewingHistory && view.atTail && !root.primitiveInput
                target: null
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => {
                    if (event.angleDelta.y < 0 || event.pixelDelta.y < 0)
                        root.showCurrent();
                    else
                        event.accepted = false;
                }
            }

            ScrollBar.vertical: ScrollBar {
                objectName: "consoleScrollBar"
                // attached ScrollBar 仍由 view 提供位置和范围，但实际控件
                // 放在独立槽位中；因此它不会盖住最后一列舞台内容。
                parent: scrollBarSlot
                anchors.fill: parent
                visible: view.visible
            }
        }

        // 当前舞台固定在视口，历史模型独立保留；同一源行的点击信息保留 source row。
        ConsoleView {
            id: stageView
            objectName: "consoleStageView"
            visible: root.stageActive && !root.reviewingHistory
            model: root.stageModel
            interactive: false
            function positionStage() {
                forceLayout();
                if (count <= root.gridRows)
                    positionViewAtBeginning();
                else
                    positionViewAtIndex(count - root.gridRows, ListView.Beginning);
            }
            // 仅结构或视口变化需要定位；普通行数据更新保持位置和缩放。
            onCountChanged: Qt.callLater(positionStage)
            onHeightChanged: Qt.callLater(positionStage)
            Component.onCompleted: positionStage()
            Connections {
                target: root.backend
                function onStageChanged() {
                    Qt.callLater(stageView.positionStage);
                }
                function onCleared() {
                    root.reviewingHistory = false;
                }
            }
            WheelHandler {
                enabled: root.showingStage && !root.primitiveInput
                target: null
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => {
                    if (event.angleDelta.y > 0 || event.pixelDelta.y > 0)
                        root.scrollHistory(3);
                }
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
                root.scrollHistory(page);
                e.accepted = true;
            } else if (e.key === Qt.Key_PageDown) {
                if (root.reviewingHistory && view.atTail)
                    root.showCurrent();
                else
                    root.scrollHistory(-page);
                e.accepted = true;
            } else if (e.key === Qt.Key_End) {
                root.showCurrent();
                e.accepted = true;
            }
        }
    }

    // 等待类型分支（与旧实现一致）
    readonly property bool primitiveInput: backend && backend.waitingInput && backend.inputKind === "INPUTMOUSEKEY"
    readonly property bool anyKeyInput: backend && backend.anyKeyInput
    readonly property bool stringInputKind: backend && backend.stringInput

    Component.onCompleted: {
        attachFramePacing();
        syncLayout();
        updateGrid();          // 初版网格按当前视口推给 C++
        applyToolTipStyle();   // TOOLTIP_SET* 可能在界面出现前就已下发
    }
}
