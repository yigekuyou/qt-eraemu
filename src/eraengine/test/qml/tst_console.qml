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
import QtTest

// QML 侧测试：只验证“接线/呈现”，逻辑在 C++（test_console_backend）已覆盖。
// 视图形态：ListView（一行一委托，行内 Repeater 建区块）——
// 夹具经 context property 注入（consoleFixture / consoleQmlPath）。
TestCase {
    id: tc
    name: "ConsoleView"
    when: windowShown

    // 需要一个真实窗口，鼠标事件才能送达（并触发 MouseArea）
    Window {
        id: win
        width: 400
        height: 300
        visible: true
        Item { id: host; anchors.fill: parent }
    }

    property var backend: consoleFixture.create(tc)
    property var comp: null
    property var view: null
    property var inputBar: null

    function init() {
        comp = Qt.createComponent("file://" + consoleQmlPath);
        verify(comp.status === Component.Ready, comp.errorString());
        view = comp.createObject(host, {
            "backend": backend,
            "lineHeight": 20,
            "width": 320,
            "height": 200
        });
        verify(view !== null, "Console.qml 创建失败");
        // Console 只读网格（唯一推导在 C++：列 = 舞台宽 ÷ 半角字宽、
        // 行 = 舞台高 ÷ 行高）。夹具必须给出与 320x200 舞台一致的一组，
        // 否则测的就不是生产里那种「舞台 ↔ 网格同源」的状态。
        backend.setGridColumns(Math.floor(320 / Math.max(1, view.fontSize / 2)));
        backend.setGridRows(Math.floor(200 / Math.max(1, view.lineHeight)));
        const inputComp = Qt.createComponent("file://" + consoleQmlPath.replace("Console.qml", "ConsoleInput.qml"));
        verify(inputComp.status === Component.Ready, inputComp.errorString());
        inputBar = inputComp.createObject(host, { backend: backend, width: 320, height: 26, y: 250 });
        verify(inputBar !== null);

    }

    function cleanup() {
        consoleFixture.finishInput(backend);
        consoleFixture.redraw(backend, 1);
        backend.stageMode = false;
        backend.stageInputRowReserved = false;
        if (inputBar) { inputBar.destroy(); inputBar = null; }
        if (view) { view.destroy(); view = null; }
        backend.clearAll();
    }

    // 打印 n 行并等模型/视图同步（flush 一帧 + 事件循环）
    function printLines(n) {
        backend.clearAll();
        for (let i = 0; i < n; ++i) {
            backend.print("L" + i);
            backend.newline();
        }
        backend.flush(); view.syncView();
    }

    function test_inputOutsideScaledStageKeepsGeometry() {
        const field = findChild(inputBar, "consoleInputField");
        verify(findChild(view, "consoleInputField") === null);
        compare(inputBar.parent, view.parent, "input and stage are siblings");
        view.scale = 0.6;
        const width = view.cellWidth;
        const height = view.cellHeight;
        const bounds = field.mapToItem(host, 0, 0);
        const spy = Qt.createQmlObject('import QtTest; SignalSpy {}', tc);
        spy.target = view;
        spy.signalName = "cellHeightChanged";
        for (let i = 0; i < 5; ++i) {
            consoleFixture.request(backend, i % 2 === 0 ? "TONEINPUT" : "INPUTS");
            wait(1);
            consoleFixture.finishInput(backend);
            wait(1);
            compare(view.cellWidth, width);
            compare(view.cellHeight, height);
            compare(field.mapToItem(host, 0, 0), bounds);
            compare(field.visible, true);
            compare(field.enabled, true);
        }
        compare(spy.count, 0);
        view.scale = 1.2;
        compare(field.mapToItem(host, 0, 0), bounds, "input does not inherit stage scale");
        spy.destroy();
    }

    // 正文**最底一行**（溢出到窗口最底那一行的那一行）属于输入栏、不属于舞台：
    // 预留输入行时 C++ 把这一行从舞台快照里摘出来单独递交（inputEchoLine），
    // ConsoleInput 把它渲染在输入框左侧；没溢出时（标题这种定页画面）不摘。
    function test_echoLineBelongsToInputBarNotStage() {
        backend.stageInputRowReserved = true;
        backend.stageMode = true;
        backend.clearAll();
        const cap = backend.stageRows;          // 舞台正文行数（gridRows - 输入行）
        const echo = findChild(inputBar, "consoleInputEcho");
        const field = findChild(inputBar, "consoleInputField");
        verify(echo !== null, "输入栏有回显行容器");
        // 正好装满舞台：不摘行（eraTW 标题就是 45 行定页，摘了会少一行）
        for (let i = 0; i < cap; ++i) {
            backend.print("E" + i);
            backend.newline();
        }
        backend.flush();
        view.syncView();
        compare(backend.inputEchoLine.length, 0, "填满舞台时不摘行（回显栏为空）");
        compare(view.rowCount, cap, "全都在舞台上");
        compare(echo.width, 0);

        // 再打一行 -> 溢出：最新一行（＞）进回显栏，舞台仍占满 cap 行
        backend.print("＞");
        backend.flush();
        view.syncView();
        compare(backend.inputEchoLine.length, 1, "回显行 = 溢出那一行");
        compare(backend.inputEchoLine[0].text, "＞");
        compare(view.rowCount, cap, "舞台仍是 cap 行（内容不整体上移）");
        compare(view.blockAt(cap - 1, 0).blockData.text, "E" + (cap - 1));
        verify(echo.width > 0, "回显行占据宽度");
        compare(field.mapToItem(host, 0, 0).x,
                echo.mapToItem(host, 0, 0).x + echo.width,
                "输入框排在回显行右侧");

        // 清屏后回显行清空：输入框回到贴左边缘（无游戏时的常态）
        backend.clearAll();
        backend.flush();
        view.syncView();
        compare(backend.inputEchoLine.length, 0);
        compare(echo.width, 0, "无正文时回显行为空");
        compare(field.mapToItem(host, 0, 0).x, echo.mapToItem(host, 0, 0).x);
    }

    function test_timedRedrawKeepsInputGeometryAndFocus() {
        consoleFixture.request(backend, "TINPUTS");
        const field = findChild(inputBar, "consoleInputField");
        tryCompare(field, "visible", true);
        field.forceActiveFocus();
        const height = view.cellHeight;
        for (let i = 0; i < 20; ++i) {
            consoleFixture.finishInput(backend);
            backend.clearAll();
            backend.print("RESET: " + i);
            backend.newline();
            consoleFixture.request(backend, "TINPUTS");
            compare(field.visible, true);
            compare(view.cellHeight, height);
            verify(field.activeFocus);
            wait(1);
            compare(view.rowCount, 1);
        }
        // A subsequent input request must not steal focus from another control.
        host.forceActiveFocus();
        consoleFixture.finishInput(backend);
        consoleFixture.request(backend, "TINPUTS");
        wait(1);
        verify(!field.activeFocus);
    }

    function test_inputKindsFollowBackend() {
        const kinds = ["WAIT", "WAITANYKEY", "FORCEWAIT", "ANYKEY", "TWAIT", "INPUT", "INPUTS", "TINPUTS"];
        for (let i = 0; i < kinds.length; ++i) {
            consoleFixture.request(backend, kinds[i]);
            compare(view.anyKeyInput, i < 5);
            compare(view.stringInputKind, i >= 6);
            consoleFixture.finishInput(backend);
            compare(view.anyKeyInput, false);
            compare(view.stringInputKind, false);
        }
    }

    // 提交后的「回显」归属：**只在输入栏**，正文只留一条空白记账行。
    // C# 的做法是 richTextBox1 清空 + 正文 Print(str)（正文是唯一能留下值的地方）；
    // 本移植的输入栏一直存在且 TextInput.text 就是回显，正文再打印就是第二份副本。
    // eraTW 的 CLEARLINE 16（15 行菜单 + 1 行回显）按**行**记账，所以行必须留下。
    function test_submitKeepsValueInInputBarNotInBody() {
        const field = findChild(inputBar, "consoleInputField");
        verify(field !== null, "输入栏里的输入框");
        // 模拟引擎：提交值 -> 唯一的提交收尾入口（正文定型占位行 + 结束等待）
        const onSubmit = function(v) { consoleFixture.finishInputCommitted(backend); };
        backend.inputSubmitted.connect(onSubmit);

        backend.clearAll();
        backend.print("[A]"); backend.newline();
        consoleFixture.request(backend, "INPUT");
        compare(backend.lineCount, 1);

        field.text = "123";
        inputBar.submit();
        compare(backend.lineCount, 2, "提交后正文多一条逻辑行（CLEARLINE 记账）");
        compare(backend.lineText(0), "[A]");
        compare(backend.lineText(1), "", "正文里没有提交值的副本");
        compare(field.text, "123", "提交值留在输入框里（这就是回显）");
        compare(backend.waitingInput, false, "提交结束等待");
        // 下一次等待开始 = 上一轮回显到期：清空，避免空回车把旧值再交一次
        consoleFixture.request(backend, "INPUT");
        compare(field.text, "", "新一次等待开始时清空输入框");
        backend.inputSubmitted.disconnect(onSubmit);
    }

    // 模型行数与视图行数一致（小内容时委托全部存活）
    function test_modelRowsAndDelegates() {
        printLines(3);
        compare(view.rowCount, 3, "模型 3 行");
        for (let r = 0; r < 3; ++r)
            verify(view.lineItem(r) !== null, "第 " + r + " 行委托已创建");
        const b = view.blockAt(0, 0);
        verify(b !== null, "第 0 行有区块对象");
        verify(b.blockData.text === "L0", "区块数据来自模型");
    }

    // 位置/尺寸来自 C++（绝对列 x、动态尺寸），委托行高恒一格
    function test_blockGeometryFromCpp() {
        backend.clearAll();
        backend.print("hello");
        backend.newline();
        backend.flush(); view.syncView();

        const line = view.lineItem(0);
        verify(line !== null, "第 0 行委托");
        compare(line.height, view.cellHeight, "行高恒为一格（图片溢出绘制，不推挤后续行）");
        const b = view.blockAt(0, 0);
        verify(b !== null, "no block item");
        verify(b.blockData.col !== undefined && b.blockData.row !== undefined, "区块带网格坐标 col/row");
        compare(b.x, b.blockData.col * view.cellWidth);
        compare(b.y, 0, "无 ypos 的区块 y == 0（锚点行即所属委托）");
        compare(b.width, b.blockData.cols * view.cellWidth);
    }

    function test_gridGlyphBounds() {
        view.fontSize = 19;
        view.fontName = "DejaVu Sans";
        backend.clearAll();
        backend.print("WWiii■■□　");
        backend.printButton("[HOLD]", 8);
        backend.newline(); backend.flush(); view.syncView();
        compare(view.cellWidth, view.width / backend.gridColumns);
        const block = view.blockAt(0, 0);
        const row = findChild(block, "textCells");
        let total = 0;
        let chars = 0;
        for (let i = 0; i < row.children.length; ++i) {
            const cell = row.children[i];
            const glyph = findChild(cell, "gridGlyph");
            if (!glyph) continue;
            // 一个「段」承载相邻同格宽的若干字符（ConsoleBlock.glyphRuns）。
            // 不变式：段宽 == 字数 × 格宽、缩放**绝不超过**段宽（不外溢到
            // 相邻 span —— 取代 delegate 内 clip：Qt 文档「Performance
            // considerations」明确禁止在 delegate 里用 clip）。
            // 契约变化（2026-10-25）：不再强制「放大铺满」。窗口变矮/变窄时
            // Console 会把渲染字号降到 cellHeight-1，此时若把 glyph 横向拉回
            // 格宽会把字撑变形；现在只允许压缩（xScale ≤ 1），宁可留亚像素
            // 空隙。
            const run = cell.modelData;
            verify(glyph.implicitWidth * glyph.transform[0].xScale <= cell.width + 0.01);
            verify(glyph.transform[0].xScale <= 1.0);
            verify(Math.abs(cell.width - run.count * run.units * view.cellWidth) < 0.01);
            verify(glyph.transform[0].origin.x === 0);
            verify(!cell.clip);
            total += cell.width;
            chars += run.count;
        }
        compare(chars, 9);
        compare(total, block.width);
        const next = view.blockAt(0, 1);
        verify(next !== null, "同一行还有第二个区块（按钮）");
        compare(next.x, block.x + block.width);
    }

    // 契约（2026-10-25 二次修正）：舞台是 C++ 的事实，窗口只决定呈现缩放。
    //   * 舞台尺寸 = 配置 ウィンドウ幅/高さ（C++ 推导网格/maxLineUnits/GETCLIENTSIZE）；
    //   * Console 读取 backend 的网格，再摊到舞台逻辑像素上得到 cell；
    //   * **同一个舞台在任意窗口尺寸下 grid / cell / 字号逐字相同**
    //     （窗口尺寸不是网格的输入 —— 这正是本测试钉住的回归点）；
    //   * 内容不越出舞台（舞台固定 ⇒ 与窗口无关）。
    // 旧契约（上一版）把窗口像素当网格输入，四尺寸的 grid 各不相同 —— 那正是
    // 用户指出的错误：同一游戏在不同设备上脚本看到不同画布。
    function test_stageGridInvariantAcrossWindowSizes() {
        // 舞台 = 两个真实游戏的配置；窗口 = 验收要求的四例（含 Android 小窗和大屏）
        const stages = [
            { w: 760,  h: 640, f: 18, lh: 19 },   // eraTetris 配置
            { w: 1400, h: 750, f: 16, lh: 16 },   // eraTW 配置（行高 == 字号）
        ];
        const windows = [[475, 400], [760, 640], [1400, 750], [1901, 1033]];
        for (const st of stages) {
            view.stageWidth = st.w;
            view.stageHeight = st.h;
            view.fontSize = st.f;
            view.lineHeight = st.lh;
            // C++（EraEngine::syncConsoleGrid）从舞台推导的那份网格
            backend.setGridColumns(Math.floor(st.w / Math.max(1, st.f / 2)));
            backend.setGridRows(Math.floor(st.h / Math.max(1, st.lh)));
            wait(1);
            const cols = view.gridColumns;
            const rows = view.gridRows;
            const cw = view.cellWidth;
            const ch = view.cellHeight;
            const fs = view.renderFontSize;
            const stageTag = "stage " + st.w + "x" + st.h + " font " + st.f + " lh " + st.lh;
            compare(cols, backend.gridColumns, "QML 网格取自 C++ @ " + stageTag);
            compare(rows, backend.gridRows, "QML 网格取自 C++ @ " + stageTag);
            verify(Math.abs(fs - st.f) < 1e-6,
                   "字号被缩小 " + fs + " != " + st.f + " @ " + stageTag);
            verify(ch * 1.0 >= st.lh - 0.1, "行距 " + ch + " 小于配置行高 " + st.lh + " @ " + stageTag);
            for (const win of windows) {
                view.width = win[0];
                view.height = win[1];
                wait(1);
                const tag = stageTag + " in window " + win[0] + "x" + win[1];
                compare(view.gridColumns, cols, "列随窗口变了 @ " + tag);
                compare(view.gridRows, rows, "行随窗口变了 @ " + tag);
                verify(Math.abs(view.cellWidth - cw) < 1e-9, "格宽随窗口变了 @ " + tag);
                verify(Math.abs(view.cellHeight - ch) < 1e-9, "格高随窗口变了 @ " + tag);
                verify(Math.abs(view.renderFontSize - fs) < 1e-9, "字号随窗口变了 @ " + tag);
                verify(cols * cw <= st.w + 1e-6, "内容右缘越过舞台 @ " + tag);
                verify(rows * ch <= st.h + 1e-6, "内容下缘越过舞台 @ " + tag);
                verify(Math.abs(view.stageRows * ch - st.h) < 1e-6,
                       "正文行覆盖完整舞台高度 @ " + tag);
            }
        }
    }

    // 契约（2026-10-25 二次修正）：呈现层 = 舞台的一个等比缩放（contain）。
    //   * scale = min(窗口宽/舞台宽, 窗口高/舞台高)，窗口可小于或远大于舞台；
    //   * 舞台完整可见（缩放后包围盒不超出窗口，至少一边贴边 = 无内容裁剪）；
    //   * 居中：缩放后包围盒中心 == 窗口中心；letterbox = 多出来的那一边的一半。
    // 变异：把 scale 改成非等比（例如两个方向各取各的）→ 本用例红。
    function test_stageScaleContainsStage() {
        const comp = Qt.createComponent("file://" + consoleQmlPath.replace("Console.qml", "Stage.qml"));
        verify(comp.status === Component.Ready, comp.errorString ? comp.errorString() : "Stage.qml");
        const host = Qt.createQmlObject('import QtQuick; Item { width: 0; height: 0 }', tc);
        const stage = comp.createObject(host, { "stageWidth": 760, "stageHeight": 640 });
        verify(stage !== null, "Stage.qml 创建失败");
        compare(stage.width, 760, "舞台逻辑宽 = C++ 配置值");
        compare(stage.height, 640, "舞台逻辑高 = C++ 配置值");

        const windows = [[475, 400], [760, 640], [1400, 750], [1901, 1033]];
        for (const win of windows) {
            host.width = win[0];
            host.height = win[1];
            wait(1);
            const expect = Math.min(win[0] / 760, win[1] / 640);
            const tag = "stage 760x640 in " + win[0] + "x" + win[1];
            verify(Math.abs(stage.stageScale - expect) < 1e-9,
                   "scale " + stage.stageScale + " != " + expect + " @ " + tag);
            // 等比：两个方向同一个系数（非等比缩放会在这里红）
            verify(Math.abs(stage.scale - expect) < 1e-9, "Item.scale != contain scale @ " + tag);
            verify(Math.abs(760 * stage.stageScale - stage.width * stage.scale) < 1e-6);
            // 完整可见（contain 永不裁内容）
            verify(760 * stage.stageScale <= win[0] + 1e-6, "舞台宽越界 @ " + tag);
            verify(640 * stage.stageScale <= win[1] + 1e-6, "舞台高越界 @ " + tag);
            // 居中：缩放后包围盒中心 == 窗口中心
            const boxCx = stage.x + stage.width / 2;
            const boxCy = stage.y + stage.height / 2;
            verify(Math.abs(boxCx - win[0] / 2) < 1e-6, "水平未居中 @ " + tag);
            verify(Math.abs(boxCy - win[1] / 2) < 1e-6, "垂直未居中 @ " + tag);
            // 黑边：至少一边为 0（贴边），另一边 = 差值的一半
            verify(Math.abs(stage.letterboxX) < 1e-6 || Math.abs(stage.letterboxY) < 1e-6,
                   "contain 应至少贴住一边 @ " + tag);
            verify(Math.abs(stage.letterboxX - (win[0] - 760 * stage.stageScale) / 2) < 1e-6,
                   "左右黑边 @ " + tag);
            verify(Math.abs(stage.letterboxY - (win[1] - 640 * stage.stageScale) / 2) < 1e-6,
                   "上下黑边 @ " + tag);
        }
        // 小窗 vs 大窗：缩放确实跨过 1（舞台可以缩小显示、也可以放大显示）
        host.width = 475; host.height = 400; wait(1);
        verify(stage.stageScale < 1, "小窗应缩小舞台");
        host.width = 1901; host.height = 1033; wait(1);
        verify(stage.stageScale > 1, "大窗应放大舞台（不设上限）");
        stage.destroy();
        host.destroy();
    }

    // 契约（2026-10-25 二次修正）：缩放后的舞台仍按「舞台本地坐标」收事件。
    // 舞台缩小显示（Android）时如果命中/坐标没走逆变换，按钮会点不中或点偏。
    // 这里用**窗口坐标**发一次点击（与舞台本地坐标相差 scale 倍），验证
    // Qt 文档（Item::scale）承诺的逆变换：MouseArea 收到的仍是舞台本地坐标。
    function test_stageScaledClickMapsToStageCoordinates() {
        const comp = Qt.createComponent("file://" + consoleQmlPath.replace("Console.qml", "Stage.qml"));
        verify(comp.status === Component.Ready);
        const stage = comp.createObject(host, { "stageWidth": 760, "stageHeight": 640 });
        verify(stage !== null, "Stage.qml 创建失败");
        const area = Qt.createQmlObject(
            'import QtQuick; MouseArea { objectName: "probeArea"; x: 100; y: 200; width: 200; height: 60;'
            + ' property real hitX: -1; property real hitY: -1;'
            + ' onClicked: function(m) { hitX = m.x; hitY = m.y; } }', stage);
        wait(1);
        const s = stage.stageScale;
        verify(s > 0 && s < 1, "夹具应缩小显示（scale=" + s + "）");
        // 目标：舞台本地 (150, 230) -> MouseArea 本地 (50, 30)。
        // 缩放原点是舞台中心（transformOrigin: Item.Center），所以舞台本地
        // 坐标到窗口坐标的映射 = 中心 + (本地 - 中心) × scale。
        const wx = stage.x + stage.width / 2 + (150 - stage.width / 2) * s;
        const wy = stage.y + stage.height / 2 + (230 - stage.height / 2) * s;
        mouseClick(host, wx, wy);      // host 未缩放：host 局部坐标 == 窗口坐标
        wait(1);
        verify(Math.abs(area.hitX - 50) <= 1, "命中坐标 x=" + area.hitX + "（应为 50）");
        verify(Math.abs(area.hitY - 30) <= 1, "命中坐标 y=" + area.hitY + "（应为 30）");
        stage.destroy();
    }

    // 文本按「段」渲染（性能回归）：相邻同格宽的字符合成一个 Text。
    function test_glyphRunsAreBatched() {
        backend.clearAll();
        backend.print("ABCDEFGHIJ");                       // 10 个半角 -> 1 段
        backend.newline();
        backend.print("あいうえお");                        // 5 个全角 -> 1 段
        backend.newline();
        backend.print("AあB");                              // 混排 -> 3 段
        backend.newline(); backend.flush(); view.syncView();

        const ascii = view.blockAt(0, 0);
        compare(ascii.glyphRuns.length, 1);
        compare(ascii.glyphRuns[0].count, 10);
        compare(ascii.glyphRuns[0].units, 1);

        const wide = view.blockAt(1, 0);
        compare(wide.glyphRuns.length, 1);
        compare(wide.glyphRuns[0].units, 2);

        const mixed = view.blockAt(2, 0);
        compare(mixed.glyphRuns.length, 3);
        compare(mixed.glyphRuns[0].text, "A");
        compare(mixed.glyphRuns[1].text, "あ");
        compare(mixed.glyphRuns[2].text, "B");

        // 段宽之和 == 区块宽（布局不变式，与逐字版一致）
        let sum = 0;
        for (let i = 0; i < mixed.glyphRuns.length; ++i)
            sum += mixed.glyphRuns[i].units * mixed.glyphRuns[i].count * view.cellWidth;
        compare(sum, mixed.width);
    }

    // 按钮 span 按「网格文字」渲染（不用原生 Button —— 对不齐）。
    function test_buttonSpanIsGridText() {
        backend.clearAll();
        backend.printButton("[HOLD]", 8);
        backend.newline(); backend.flush(); view.syncView();

        const block = view.blockAt(0, 0);
        verify(block !== null && block.blockData.isButton === true, "按钮区块");
        const row = findChild(block, "textCells");
        verify(row !== null && row.visible, "按钮 span 用网格文字容器渲染");
        let chars = 0;
        for (let i = 0; i < row.children.length; ++i) {
            const cell = row.children[i];
            if (findChild(cell, "gridGlyph"))
                chars += cell.modelData.count;   // 段内的字符数
        }
        compare(chars, block.blockData.text.length);
        compare(block.width, block.blockData.cols * view.cellWidth);
        compare(block.height, view.cellHeight);
    }

    function test_spanStyleOverridesAndDefaults() {
        backend.clearAll();
        backend.print("A"); backend.newline(); backend.flush(); view.syncView();
        const block = view.blockAt(0, 0);
        const original = block.blockData;
        block.blockData = {
            kind: "text", text: "A", cols: 1, rows: 1,
            fontName: "DejaVu Serif", color: "#123456", buttonColor: "#abcdef",
            bold: true, italic: true, underline: true, strike: true
        };
        const glyph = findChild(block, "gridGlyph");
        verify(glyph !== null);
        compare(glyph.font.family, "DejaVu Serif");
        compare(glyph.color, "#123456");
        compare(block.effectiveFocusColor, "#abcdef");
        verify(glyph.font.bold && glyph.font.italic && glyph.font.underline && glyph.font.strikeout);
        compare(block.height, view.cellHeight);
        block.fontName = "DejaVu Sans";
        block.blockData = original;
        compare(glyph.font.family, "DejaVu Sans");
        verify(!glyph.font.bold && !glyph.font.italic);
    }

    // 图片区块：image://emuera/<资源名>（QQuickImageProvider），非 ASCII 资源名
    // 必须 encodeURIComponent；真实资源能解码并画出像素。
    function test_imageLayer() {
        backend.clearAll();
        backend.print("A");
        backend.printImage("face_01", 40, 40);
        backend.newline();
        backend.flush(); view.syncView();

        // 同一行两个区块：文本 + 图片
        const block = view.blockAt(0, 1);
        verify(block !== null, "第 0 行第 2 个区块是图片");
        compare(block.blockData.text, "face_01");
        const image = findChild(block, "blockImage");
        verify(image !== null, "图片区块里有 Image 元素");
        compare(image.source, "image://emuera/face_01");
        compare(image.fillMode, Image.PreserveAspectFit);
        verify(image.asynchronous, "异步加载（QImage provider 支持）");

        // 非 ASCII（eraTW 的立絵资源名就是日文）：编码一次，provider 侧还原
        backend.clearAll();
        backend.printImage("立絵_服_通常_55", 40, 40);
        backend.newline(); backend.flush(); view.syncView();
        const image2 = findChild(view.blockAt(0, 0), "blockImage");
        verify(image2 !== null);
        // QUrl 会自己规范化百分号编码，所以比较「前缀 + 解码后的名字」。
        const src2 = ("" + image2.source);
        verify(src2.indexOf("image://emuera/") === 0, "provider scheme 前缀");
        compare(decodeURIComponent(src2.substring("image://emuera/".length)),
                "立絵_服_通常_55");

        // 「真的能渲染」：example/resources/offset_atlas.png 是 8×8 的真实文件
        backend.clearAll();
        backend.printImage("offset_atlas", 100, 100);
        backend.newline(); backend.flush(); view.syncView();
        const real = findChild(view.blockAt(0, 0), "blockImage");
        verify(real !== null);
        tryCompare(real, "status", Image.Ready);
        verify(real.paintedWidth > 0 && real.paintedHeight > 0,
               "真实图片被解码并绘制（paintedWidth/Height > 0）");
    }

    // QUIT -> closeGame() 会 setRoot("") 卸载资源根，而最后一屏的 <img> 区块
    // 仍留在窗口上（QML 是异步按名取图）。区块生成时必须把像素钉住，否则就是
    // 「QML Image: Failed to get image from provider」。
    function test_imageSurvivesResourceTeardown() {
        backend.clearAll();
        backend.printImage("offset_atlas", 100, 100);
        backend.newline(); backend.flush(); view.syncView();
        const block = view.blockAt(0, 0);
        verify(block !== null, "钉住用例：图片区块存在");
        const image = findChild(block, "blockImage");
        verify(image !== null);
        tryCompare(image, "status", Image.Ready);

        // 模拟 QUIT：资源根清空（图集/解码缓存随之释放）
        consoleFixture.unloadResources();
        view.syncView();
        const image2 = findChild(view.blockAt(0, 0), "blockImage");
        verify(image2 !== null);
        tryCompare(image2, "status", Image.Ready);
        verify(image2.paintedWidth > 0,
               "资源根卸载后仍画出像素（区块生成时已钉住）");

        // 清屏后钉住的副本释放，再取图应失败（不常驻）
        backend.clearAll();
        verify(!consoleFixture.hasImage("offset_atlas"),
               "清屏后钉住的副本已释放");
        consoleFixture.loadResources();      // 复原资源根，后续用例还要取图
    }

    function test_stageKeepsOverflowingImageAcrossRefresh() {
        backend.stageMode = true;
        backend.printImage("offset_atlas", 400, 400);
        backend.newline();
        // Move the anchor above the last gridRows lines while its bottom
        // still reaches into the stage. The anchor must remain in the model.
        for (let i = 0; i < backend.gridRows; ++i) {
            backend.print("line " + i);
            backend.newline();
        }
        backend.flush(); view.syncView();
        compare(backend.stageStart, 0);
        const stage = findChild(view, "consoleStageView");
        tryCompare(stage, "atYEnd", true);
        const block = view.blockAt(0, 0);
        verify(block !== null, "stage retains the overlapping image anchor");
        compare(block.visible, false, "stage image is rendered only by the overlay");
        compare("" + findChild(block, "blockImage").source, "");
        const image = findChild(view.stageImageAt(0), "blockImage");
        tryCompare(image, "status", Image.Ready);
        const point = image.mapToItem(stage, 0, image.height);
        verify(point.y > 0, "image bottom still overlaps the viewport");

        // An unchanged frame must not reposition; explicitly returning to the
        // current stage restores the viewport through the Qt positioning API.
        stage.contentY = 0;
        backend.flush();
        wait(1);
        compare(stage.contentY, 0);
        backend.requestScrollToBottom();
        tryCompare(stage, "atYEnd", true);

        // Once its entire extent is above the viewport, drop the anchor.
        for (let i = 0; i < 4; ++i) {
            backend.print("next " + i);
            backend.newline();
        }
        backend.flush(); view.syncView();
        verify(backend.stageStart > 0);
    }

    function test_stageTextNeverRequestsImages() {
        backend.stageMode = true;
        const lines = ["━━━━━━━━━━━━", "eraThe World", "2013～2022",
                       "汉化版仅为学习日语的交流之用", "[0] 初次遊玩", "[1] 存檔再開"];
        for (const text of lines) {
            backend.print(text);
            backend.newline();
        }
        backend.flush(); view.syncView();
        tryCompare(view, "showingStage", true);
        compare(backend.stageImageBlocks.length, 0);
        for (let row = 0; row < lines.length; ++row) {
            const block = view.blockAt(row, 0);
            verify(block !== null);
            compare(block.kind, "text");
            compare("" + findChild(block, "blockImage").source, "",
                    "text must not be passed to the image provider");
            compare("" + findChild(block, "blockImageButton").source, "");
        }
    }

    function test_stageImageOverlayAlignmentAndHistory() {
        view.fontSize = view.lineHeight;
        backend.stageMode = true;
        const stage = findChild(view, "consoleStageView");
        const overlay = findChild(view, "stageImageLayer");
        for (const align of ["left", "center"]) {
            backend.clearAll();
            backend.print("▼示例▼"); backend.newline();
            consoleFixture.printHtml(backend, "<p align='" + align
                + "'><nobr><img src='offset_atlas' height='400'></nobr></p>");
            consoleFixture.printHtml(backend, "<p align='" + align
                + "'><nobr><img src='offset_atlas' height='400' ypos='-100'></nobr></p>");
            backend.flush(); view.syncView();
            tryCompare(view, "showingStage", true);
            compare(backend.stageImageBlocks.length, 2);
            const face = view.stageImageAt(0);
            const frame = view.stageImageAt(1);
            verify(face !== null && frame !== null);
            tryCompare(findChild(face, "blockImage"), "status", Image.Ready);
            compare(face.parent, overlay);
            compare(frame.parent, overlay);
            verify(frame.z > face.z, "later printed image covers earlier image");
            // 图片层整层套了一个 Scale（消除逐条取整造成的接缝），因此断言
            // 必须取「渲染后的舞台坐标」，不能直接读未缩放子项的 y。
            const faceTop = face.mapToItem(overlay.parent, 0, 0).y;
            const frameTop = frame.mapToItem(overlay.parent, 0, 0).y;
            verify(Math.abs(faceTop - frameTop) < 0.01, "negative ypos aligns frame with portrait");
            // 尺寸用图片**原始尺寸**（C++ 量出来的 pxWidth/pxHeight），
            // 不是「满格」：满格会把 180×180 的画像枠画成 184×176。
            verify(Math.abs(face.width - face.blockData.pxWidth) < 0.01,
                   "宽度 = 图片原始尺寸（不是满格 cols×列宽）");
            verify(Math.abs(face.height - face.blockData.pxHeight) < 0.01,
                   "高度 = 图片原始尺寸（不是满格 rows×行高）");
            const title = view.blockAt(0, 0).mapToItem(overlay.parent, 0, 0);
            verify(Math.abs(faceTop - title.y - view.cellHeight) < 0.01,
                   "portrait starts exactly one line below title for both alignments");
            const oldTop = face.mapToItem(overlay.parent, 0, 0).y;
            stage.contentY += view.cellHeight;
            verify(Math.abs(face.mapToItem(overlay.parent, 0, 0).y - (oldTop - view.cellHeight)) < 0.01,
                   "overlay follows the same viewport displacement");
            // 历史仍重建同一来源行和 ypos；切换不会丢掉图片。
            backend.stageMode = false; view.syncView();
            tryCompare(overlay, "visible", false);
            const historyFace = view.blockAt(1, 0);
            const historyFrame = view.blockAt(2, 0);
            verify(historyFace !== null && historyFrame !== null);
            tryCompare(findChild(historyFace, "blockImage"), "status", Image.Ready);
            compare(historyFace.blockData.text, "offset_atlas");
            compare(historyFrame.blockData.offsetRows, -1);
            compare(backend.stageImageBlocks.length, 0);
            backend.stageMode = true;
        }
    }

    // 可点击部分：`<div rect>` 里的 `<button><img>` 必须「看得见的图片矩形」
    // 和「鼠标命中矩形」是同一个矩形。
    // 以前 x 走 col×格宽（80/7 逐段取整 -> 0,11,23,34…），图片层又被整体
    // Scale 缩放，于是命中区和画面在第 3 张以后差出一整格：看起来点中了，
    // 实际落在隔壁按钮上（或哪里都不是）。现在 C++ 只给一次累计像素 pxLeft，
    // QML 折算一次，图片层与命中区都读同一个值。
    function test_divButtonHitRectMatchesVisibleImage() {
        view.fontSize = 14;
        view.lineHeight = 14;      // imageScale = 1 -> 未缩放空间 == 舞台像素
        backend.stageMode = true;
        backend.clearAll();
        backend.print("▼"); backend.newline();
        consoleFixture.printHtml(backend,
            "<div rect='160px,0px,80px,80px'><button value='7'>"
            + "<img src='offset_atlas' width='80px' height='80px'></button></div>");
        backend.flush(); view.syncView();
        tryCompare(view, "showingStage", true);
        compare(backend.stageImageBlocks.length, 1);
        const block = view.stageImageAt(0);
        verify(block !== null, "div 按钮图片进入舞台图片层");
        compare(block.blockData.pxLeft, 160);
        compare(block.blockData.clickable, true);
        // 图片层整层套一个 Scale，子项坐标是**未缩放**空间。x 必须等于
        // 「累计像素 / 列像素宽 × 未缩放列宽」——只是把 C++ 的像素原点换回本层
        // 的单位，中间不再有任何一次取整（col 那份量化值不参与定位）。
        const overlayLayer = findChild(view, "stageImageLayer");
        const expected = block.blockData.pxLeft / (view.fontSize / 2) * overlayLayer.unitCol;
        verify(Math.abs(block.x - expected) < 0.01,
               "图片左边界 = 累计像素折算值（不是 col×格宽）：x=" + block.x
               + " 期望=" + expected + " col×格宽=" + (block.blockData.col * overlayLayer.unitCol));
        verify(Math.abs(block.width - block.blockData.pxWidth) < 0.01,
               "命中宽度 = 图片原始宽度（按钮矩形 == 图片矩形）");
        const mouse = findChild(block, "blockButtonMouse");
        verify(mouse !== null, "div 按钮图片带命中区");
        // 命中区必须铺满整个可见图片矩形（否则图片右/下边缘点不动）。
        // 是否**当下**可点由 generation 闸门决定（另有专门测试），这里只锁几何。
        verify(Math.abs(mouse.width - block.width) < 0.01
               && Math.abs(mouse.height - block.height) < 0.01,
               "命中区与图片同尺寸：" + mouse.width + "x" + mouse.height
               + " vs " + block.width + "x" + block.height);
        compare(mouse.width, block.width);
        compare(mouse.height, block.height);
    }

    // ypos 是**像素**位移，加在行首之上；不是「负几格再乘行高」。
    // eraTW 的 字号 16 / 行高 20 就是这样：画像枠被拉回 16px，而不是 20px，
    // 所以它比立絵低 4px（C# 同样如此，差的是 4px，不是一整格）。
    function test_yposIsPixelShiftOnGridNotRows() {
        backend.stageMode = true;
        backend.clearAll();
        view.lineHeight = 20;
        view.fontSize = 16;
        backend.print("▼"); backend.newline();
        consoleFixture.printHtml(backend, "<p align='left'><nobr><img src='offset_atlas' height='400'></nobr></p>");
        consoleFixture.printHtml(backend, "<p align='left'><nobr><img src='offset_atlas' height='400' ypos='-100'></nobr></p>");
        backend.flush(); view.syncView();
        tryCompare(view, "showingStage", true);
        const overlay = findChild(view, "stageImageLayer");
        const face = view.stageImageAt(0);
        const frame = view.stageImageAt(1);
        verify(face !== null && frame !== null);
        compare(backend.stageImageBlocks[1].pxTop, -16, "C++ 递交的是像素位移 -16");
        const scale = view.cellHeight / 20;
        const gap = frame.mapToItem(overlay.parent, 0, 0).y - face.mapToItem(overlay.parent, 0, 0).y;
        verify(Math.abs(gap - (20 - 16) * scale) < 0.01,
               "位移按像素（4px × 整层缩放），而不是被当成负一格（应为 0）");
        verify(Math.abs(gap) > 0.5, "确实不是「正好一格」的对齐");
        view.lineHeight = view.fontSize;
        backend.stageMode = false;
        backend.clearAll(); view.syncView();
    }

    // eraTW 的画像枠（图像边框）在**另一行**打印：@画像セット 把立絵放到
    // レイヤー階数 0、枠放到 最大レイヤー数（=1），两个层各是一次 HTML_PRINT，
    // 所以框在立絵的下一行，靠 ypos 的 `-(LINECOUNT-描画開始行数+階数)*(行高/字号)`
    // 拉回来。左右对齐（<p align='left'|'center'>）作用于**整行**，而 eraTW 给
    // 立絵和框的 width/height 是同一个值（都是 默认角色画像横幅*比率/字号），
    // 两行的内容宽度一模一样 —— 所以居中和左对齐都必须让框贴着立絵走，
    // 不能因为「按行宽平移」把框推开。
    // 行高 == 字号（eraTW 的 emuera.config：フォントサイズ16 / 一行の高さ16）时，
    // 一层正好是一行、ypos 正好抵消一行，框与立絵上下也重合。
    function test_alignmentKeepsFrameOnPortrait() {
        backend.stageMode = true;
        view.lineHeight = 16;              // eraTW：一行の高さ 16
        view.fontSize = 16;                // eraTW：フォントサイズ 16
        backend.setGridColumns(Math.floor(320 / 8));
        const overlay = findChild(view, "stageImageLayer");
        const left = {};
        for (const align of ["left", "center"]) {
            backend.clearAll();
            backend.print("▼"); backend.newline();
            // 第 0 层：立絵（资源固有 180×180 = eraTW の立絵サイズ）
            consoleFixture.printHtml(backend, "<shape type='space' param='0'><p align='" + align + "'>"
                + "<img src='transparent' width='1125' height='1125'>");
            // 第 1 层：画像枠（资源固有 16×16，和立絵完全不同 —— 若把固有尺寸
            // 漏进行宽/摆放，两行的宽度就会不同，居中时框会被推歪）
            consoleFixture.printHtml(backend, "<shape type='space' param='0'><p align='" + align + "'>"
                + "<img src='webp_atlas' width='1125' height='1125' ypos='-100'>");
            backend.flush(); view.syncView();
            tryCompare(view, "showingStage", true);
            const face = view.stageImageAt(0);
            const frame = view.stageImageAt(1);
            verify(face !== null && frame !== null);
            tryCompare(findChild(face, "blockImage"), "status", Image.Ready);
            compare(frame.blockData.col, face.blockData.col,
                    align + "：框与立絵同一列（行宽相同，整行平移相同）");
            compare(frame.blockData.cols, face.blockData.cols, "框与立絵占同样多的格子");
            const fx = face.mapToItem(overlay.parent, 0, 0).x;
            const gx = frame.mapToItem(overlay.parent, 0, 0).x;
            verify(Math.abs(fx - gx) < 0.01, align + "：框与立絵左边缘重合");
            const fy = face.mapToItem(overlay.parent, 0, 0).y;
            const gy = frame.mapToItem(overlay.parent, 0, 0).y;
            verify(Math.abs(fy - gy) < 0.01,
                   align + "：行高 == 字号时 ypos 正好抵消一整行");
            left[align] = fx;
            left[align + "Frame"] = gx;
        }
        // 居中相对左对齐整体右移，而且立絵和框**移动同样的距离**。
        const shiftFace = left["center"] - left["left"];
        const shiftFrame = left["centerFrame"] - left["leftFrame"];
        verify(shiftFace > 0.5, "居中确实把整行右移（不是没生效）");
        verify(Math.abs(shiftFace - shiftFrame) < 0.01,
               "立絵和框被平移同样的距离（对齐不改变两者相对位置）");
        view.lineHeight = 20;
        view.fontSize = 18;
        backend.setGridColumns(Math.floor(320 / 9));
        backend.stageMode = false;
        backend.clearAll(); view.syncView();
    }

    function test_stageImageOverlayFollowsTrimmedOrigin() {
        backend.stageMode = true;
        backend.setMaxLog(12);
        for (let i = 0; i < 7; ++i) {
            backend.print("before " + i); backend.newline();
        }
        backend.printImage("offset_atlas", 200, 400); backend.newline();
        backend.flush(); view.syncView();
        const stage = findChild(view, "consoleStageView");
        const overlay = findChild(view, "stageImageLayer");
        for (let i = 0; i < 8; ++i) {
            backend.print("after " + i); backend.newline(); backend.flush(); view.syncView();
            wait(1);
            const face = view.stageImageAt(0);
            verify(face !== null, "overlapping portrait survives trimming");
            const index = face.blockData.sourceRow - backend.stageStart;
            const line = stage.itemAtIndex(index);
            verify(line !== null);
            const anchor = line.mapToItem(overlay, 0, 0);
            verify(Math.abs(face.y - anchor.y - face.offsetRows * view.cellHeight) < 0.01,
                   "image and source row use the same ListView origin after trimming");
        }
        backend.setMaxLog(5000);
    }

    function test_positionedWindowBackgroundStaysBehindLabels() {
        backend.clearAll();
        consoleFixture.printHtml(backend,
            "<nobr><button value='0' pos='0'><shape type='rect' param='0,0,1500,100' color='#000000'></button>"
            + "<nonbutton pos='100' title='label tip'>Guild</nonbutton>"
            + "<button pos='800' value='7'>Quest</button></nobr>");
        consoleFixture.request(backend, "INPUT");
        backend.flush(); view.syncView();
        const background = view.blockAt(0, 0);
        const label = view.blockAt(0, 1);
        const task = view.blockAt(0, 2);
        verify(background !== null && label !== null && task !== null);
        verify(label.z > background.z, "background button must not cover nonbutton text");
        verify(task.z > background.z, "task button stays above window background");
        compare(label.clickable, false);
        verify(findChild(label, "blockButtonMouse").hoverEnabled, "nonbutton title supports hover");
        const spy = Qt.createQmlObject('import QtTest 1.0; SignalSpy {}', tc);
        spy.target = backend;
        spy.signalName = "inputSubmitted";
        mouseClick(findChild(task, "blockButtonMouse"), task.width / 2, task.height / 2);
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], 7);
        spy.destroy();
    }

    // 同一 rect 内先画非按钮底图、再画按钮图片时，按钮命中区必须位于
    // 底图之上；这是 FL3.8 肖像缩略图的实际 HTML 结构。
    function test_overlappingThumbnailButtonWinsHitTest() {
        backend.stageMode = false;
        backend.clearAll();
        consoleFixture.printHtml(backend,
            "<div rect='0px,0px,80px,80px'><img src='offset_atlas' width='80px' height='80px'></div>"
            + "<div rect='0px,0px,80px,80px'><button value='7'><img src='transparent' width='80px' height='80px'></button></div>");
        backend.flush(); view.syncView();
        consoleFixture.request(backend, "INPUT");
        backend.flush(); view.syncView();
        const base = view.blockAt(0, 0);
        const button = view.blockAt(0, 1);
        verify(base !== null && button !== null, "重叠缩略图区块存在");
        compare(base.blockData.clickable, false);
        compare(button.blockData.clickable, true);
        verify(button.z > base.z, "按钮区块位于底图区块之上");
        const spy = Qt.createQmlObject('import QtTest 1.0; SignalSpy {}', tc);
        spy.target = backend;
        spy.signalName = "inputSubmitted";
        mouseClick(findChild(button, "blockButtonMouse"), button.width / 2, button.height / 2);
        compare(spy.count, 1, "重叠图片上的按钮仍可点击");
        compare(spy.signalArguments[0][0], 7);
        spy.destroy();
    }

    function test_positionedThumbnailLayersDoNotWrap() {
        backend.stageMode = true;
        let html = "";
        for (let i = 0; i < 7; ++i) {
            const rect = "<div rect='" + (i * 80) + "px,0px,80px,80px'>";
            html += rect + "<img src='offset_atlas' width='80px' height='80px'></div>";
            html += rect + "<button value='" + i + "'><img src='transparent' width='80px' height='80px'></button></div>";
        }
        consoleFixture.printHtml(backend, html);
        backend.flush(); view.syncView();
        compare(backend.stageImageBlocks.length, 14);
        for (let i = 0; i < 7; ++i) {
            const base = backend.stageImageBlocks[i * 2];
            const feature = backend.stageImageBlocks[i * 2 + 1];
            compare(base.sourceRow, 0, "positioned children stay on the source line");
            compare(feature.sourceRow, 0);
            compare(base.col, feature.col);
            compare(base.col, Math.round(i * 80 / Math.floor(view.fontSize / 2)));
        }
    }

    function test_divClipCutsOversizedDrawing() {
        backend.stageMode = false;
        // C# ConsoleDivPart.DrawTo：SetClip(内容框)。300px 宽的图放进 200px 宽的
        // rect，只画前 200px；排版推进仍是 300px（后面的兄弟 div 不会左移）。
        consoleFixture.printHtml(backend,
            "<div rect='0px,0px,200px,60px'>"
            + "<img src='offset_atlas' width='300px' height='60px'></div>"
            + "<div rect='400px,0px,200px,60px'>"
            + "<img src='transparent' width='150px' height='60px'></div>");
        backend.flush(); view.syncView();
        const clipped = view.blockAt(0, 0);
        verify(clipped !== null, "rect 内的图区块存在");
        compare(clipped.clip, true);
        compare(clipped.divClipped, true);
        compare(clipped.blockData.clipWidth, 200);
        // 图片区块的宽高就是图片原始**引擎像素**（divImageWidth），所以裁剪上限
        // 也是引擎像素：min(300px 原始宽, 200px 裁剪上限) = 200px。
        verify(Math.abs(clipped.width - 200) < 0.51,
               "rect 外的绘制被裁到 200px（实得 " + clipped.width + "）");
        // 没越界的图：同样在 div 里、clip: true，但裁剪上限要大于自身宽高 ——
        // 一个像素都不能被切掉（上限是 200px，150px 的图还剩 50px 余量）。
        const intact = view.blockAt(0, 1);
        verify(intact !== null, "第二个 div 的图存在");
        compare(intact.divClipped, true);
        compare(intact.blockData.clipWidth, 200);
        verify(intact.width <= intact.clipWidthLimit + 0.01 && intact.width > 100,
               "没越界时宽度不受裁剪影响（实得 " + intact.width + "/" + intact.clipWidthLimit + "）");
        // div 外的正文不裁剪。
        backend.clearAll();
        consoleFixture.printHtml(backend, "<div><img src='offset_atlas' width='300px' height='60px'></div>");
        backend.flush(); view.syncView();
        const free = view.blockAt(0, 0);
        verify(free !== null);
        compare(free.clip, false);
        compare(free.blockData.clipWidth, -1);
    }

    function test_divImagesShareAnchorInStageAndHistory() {
        backend.stageMode = true;
        consoleFixture.printHtml(backend,
            "<div rect='80px,20px,80px,80px'><img src='offset_atlas' width='80px' height='80px'></div>"
            + "<div rect='80px,20px,80px,80px'><img src='transparent' width='80px' height='80px'></div>"
            + "<div rect='80px,20px,80px,80px'><button value='7'><img src='webp_atlas' width='80px' height='80px'></button></div>");
        consoleFixture.request(backend, "INPUT");
        backend.flush(); view.syncView();
        tryCompare(view, "showingStage", true);
        compare(backend.stageImageBlocks.length, 3);
        const images = [];
        for (let i = 0; i < 3; ++i) {
            const block = view.stageImageAt(i);
            verify(block.visible, "overlay image is visible");
            tryCompare(findChild(block, "blockImage"), "status", Image.Ready);
            compare(block.width, 80);
            compare(block.height, 80);
            compare(block.blockData.pxTop, 20);
            images.push(block);
        }
        compare(images[0].x, images[1].x);
        compare(images[0].y, images[1].y);
        compare(images[0].x, images[2].x);
        compare(images[0].y, images[2].y);
        verify(images[2].z > images[1].z && images[1].z > images[0].z);
        mouseMove(images[2], 40, 40);
        tryVerify(function() { return images[2].hovered; });
        backend.stageMode = false; view.syncView();
        // C# ConsoleDivPart：div 的 rect.y 决定它落在**哪一行**（PointY 是绝对像素），
        // 同一 rect 的兄弟 div 落在同一行、同一位置。这里不再假定是第 0 行 ——
        // 行号由 rect.y/行高 决定，三个 div 必须同源同行。
        let histRow = -1;
        for (let r = 0; r < 12 && histRow < 0; ++r) {
            let n = 0;
            for (let i = 0; i < 8; ++i) if (view.blockAt(r, i) !== null) ++n;
            if (n >= 3) histRow = r;
        }
        verify(histRow >= 0, "sibling divs with the same rect share one row (C# PointY)");
        for (let i = 0; i < 3; ++i) {
            const block = view.blockAt(histRow, i);
            verify(block !== null && block.visible, "history retains each image");
            tryCompare(findChild(block, "blockImage"), "status", Image.Ready);
            compare(block.x, view.blockAt(histRow, 0).x);
            compare(block.y, view.blockAt(histRow, 0).y);
        }
        backend.stageMode = true; view.syncView();
        tryCompare(findChild(view.stageImageAt(0), "blockImage"), "status", Image.Ready);
    }

    function test_stageImageSnapshotHonorsRedrawAndButtons() {
        backend.stageMode = true;
        consoleFixture.printHtml(backend,
            "<button value='7' title='portrait'><img src='offset_atlas' srcb='face_01_sel' height='200'></button>");
        consoleFixture.request(backend, "INPUT");
        view.syncView();
        tryCompare(view, "showingStage", true);
        const face = view.stageImageAt(0);
        verify(face !== null);
        compare(face.blockData.tooltip, "portrait");
        const swap = findChild(face, "blockImageButton");
        mouseMove(face, face.width / 2, face.height / 2);
        tryVerify(function() { return face.hovered; });
        compare("" + swap.source, "image://emuera/face_01_sel");
        const submitted = Qt.createQmlObject('import QtTest; SignalSpy {}', tc);
        submitted.target = backend;
        submitted.signalName = "inputSubmitted";
        mousePress(face, face.width / 2, face.height / 2, Qt.LeftButton);
        compare(submitted.count, 1, "overlay button submits input");
        compare(submitted.signalArguments[0][0], 7);
        mouseRelease(face, face.width / 2, face.height / 2, Qt.LeftButton);
        submitted.destroy();
        consoleFixture.finishInput(backend);
        const published = backend.stageImageBlocks.length;
        consoleFixture.redraw(backend, 0);
        backend.printImage("offset_atlas", 100, 100); backend.newline(); backend.flush();
        compare(backend.stageImageBlocks.length, published, "REDRAW 0 keeps published images");
        consoleFixture.redraw(backend, 3);
        compare(backend.stageImageBlocks.length, published + 1, "forced frame publishes image and text together");
    }

    function test_stageAndHistoryAreExclusiveAndShortStageStartsAtTop() {
        backend.stageMode = true;
        printLines(2);
        const history = findChild(view, "consoleListView");
        const stage = findChild(view, "consoleStageView");
        tryCompare(stage, "visible", true);
        compare(history.visible, false);
        tryVerify(function() {
            const first = stage.itemAtIndex(0);
            return first !== null && Math.abs(first.y - stage.contentY) < 0.01;
        });
        backend.flush();
        wait(1);
        compare(history.visible, false);
        compare(stage.itemAtIndex(0).y - stage.contentY, 0);
        view.height = 250;
        wait(1);
        compare(stage.itemAtIndex(0).y - stage.contentY, 0);
    }

    function test_frameUpdatesDoNotChangeGeometryOrPosition() {
        backend.stageMode = true;
        printLines(8);
        wait(1);
        const stage = findChild(view, "consoleStageView");
        const width = view.cellWidth;
        const height = view.cellHeight;
        const changed = Qt.createQmlObject('import QtTest; SignalSpy {}', tc);
        changed.target = backend;
        changed.signalName = "gridChanged";
        const moved = Qt.createQmlObject('import QtTest; SignalSpy {}', tc);
        moved.target = stage;
        moved.signalName = "contentYChanged";
        const published = Qt.createQmlObject('import QtTest; SignalSpy {}', tc);
        published.target = backend;
        published.signalName = "stageChanged";
        for (let frame = 0; frame < 5; ++frame) {
            consoleFixture.finishInput(backend);
            consoleFixture.redraw(backend, 0);
            backend.clearLines(8);
            for (let row = 0; row < 8; ++row) {
                backend.print(row === 4 ? "piece " + frame : "L" + row);
                backend.newline();
            }
            consoleFixture.request(backend, "TONEINPUT");
            wait(20);
            compare(view.cellWidth, width);
            compare(view.cellHeight, height);
        }
        compare(changed.count, 0, "frames do not notify UI grid dimensions");
        compare(published.count, 0, "same stage boundary does not request positioning");
        compare(moved.count, 0, "content updates do not move the viewport");
        changed.destroy();
        moved.destroy();
        published.destroy();
    }

    function test_stageDoubleBufferPreservesUnchangedDelegates() {
        backend.stageMode = true;
        printLines(3);
        const first = view.lineItem(0);
        const block = view.blockAt(0, 0);
        const reset = Qt.createQmlObject('import QtTest; SignalSpy {}', tc);
        reset.target = backend.stageModel;
        reset.signalName = "modelReset";
        const changed = Qt.createQmlObject('import QtTest; SignalSpy {}', tc);
        changed.target = backend.stageModel;
        changed.signalName = "dataChanged";
        backend.flush();
        wait(1);
        compare(changed.count, 0, "unchanged snapshot emits no data update");
        backend.print("new tail");
        backend.newline();
        backend.flush();
        view.syncView();
        wait(1);
        compare(reset.count, 0, "stage publication never resets all delegates");
        compare(changed.count, 0, "append leaves existing rows unchanged");
        compare(view.lineItem(0), first);
        compare(view.blockAt(0, 0), block);
        reset.destroy();
        changed.destroy();
    }

    function test_bufferedBoardPublishesOnlyAtInputBoundary() {
        backend.stageMode = true;
        printLines(8);
        consoleFixture.request(backend, "TONEINPUT");
        view.syncView();
        const first = view.lineItem(0);
        const firstBlock = view.blockAt(0, 0);
        const changed = Qt.createQmlObject('import QtTest; SignalSpy {}', tc);
        changed.target = backend.stageModel;
        changed.signalName = "dataChanged";
        const reset = Qt.createQmlObject('import QtTest; SignalSpy {}', tc);
        reset.target = backend.stageModel;
        reset.signalName = "modelReset";
        consoleFixture.redraw(backend, 0);
        consoleFixture.finishInput(backend);
        backend.clearLines(8);
        backend.flush();
        wait(30);
        compare(view.rowCount, 8, "previous full stage remains during CLEARLINE");
        for (let i = 0; i < 8; ++i) {
            backend.print(i === 4 ? "moved piece" : "L" + i);
            backend.newline();
            backend.flush();
            wait(20); // Allow multiple timer/render frames during board construction.
            compare(view.rowCount, 8);
            compare(changed.count, 0, "partially built board is never published");
            compare(view.blockAt(0, 0), firstBlock);
        }
        consoleFixture.request(backend, "TONEINPUT");
        view.syncView();
        wait(1);
        compare(changed.count, 1, "complete frame updates only the changed row");
        compare(reset.count, 0);
        compare(view.lineItem(0), first);
        compare(view.blockAt(0, 0), firstBlock);
        changed.destroy();
        reset.destroy();
    }

    function test_inputTransitionsKeepStageAndButtonObjects() {
        backend.stageMode = true;
        backend.print("board");
        backend.newline();
        backend.printButton("left", 4);
        backend.newline();
        consoleFixture.request(backend, "TONEINPUT");
        view.syncView();
        const field = findChild(inputBar, "consoleInputField");
        const stage = findChild(view, "consoleStageView");
        const line = view.lineItem(1);
        const button = view.blockAt(1, 0);
        const height = stage.height;
        const y = stage.contentY;
        const changed = Qt.createQmlObject('import QtTest; SignalSpy {}', tc);
        changed.target = backend.stageModel;
        changed.signalName = "dataChanged";
        verify(button.clickable);
        consoleFixture.finishInput(backend);
        wait(1);
        compare(button.clickable, false);
        compare(changed.count, 0, "input completion only updates interaction properties");
        compare(view.lineItem(1), line);
        compare(view.blockAt(1, 0), button);
        compare(field.visible, true);
        compare(stage.height, height);
        compare(stage.contentY, y);
        consoleFixture.request(backend, "TONEINPUT");
        wait(1);
        compare(changed.count, 0, "next input request preserves unchanged drawing data");
        compare(view.blockAt(1, 0), button);
        compare(button.clickable, false, "previous generation remains disabled");
        // Reproduce CLEARLINE + full board redraw with only one text row changed.
        consoleFixture.finishInput(backend);
        backend.clearLines(2);
        backend.print("board moved");
        backend.newline();
        backend.printButton("left", 4);
        backend.newline();
        consoleFixture.request(backend, "TONEINPUT");
        view.syncView();
        wait(1);
        compare(view.lineItem(1), line, "same stage row survives board replacement");
        compare(view.blockAt(1, 0).clickable, true);
        compare(stage.height, height);
        compare(field.visible, true);
        changed.destroy();
    }

    function test_historyPositionSurvivesStagePublication() {
        backend.stageMode = true;
        printLines(80);
        const history = findChild(view, "consoleListView");
        const stage = findChild(view, "consoleStageView");
        backend.requestScrollBy(10);
        tryCompare(history, "visible", true);
        compare(stage.visible, false);
        history.forceLayout();
        const y = history.contentY;
        const first = history.indexAt(1, y + 1);
        const reset = Qt.createQmlObject('import QtTest; SignalSpy {}', tc);
        reset.target = backend.historyModel;
        reset.signalName = "modelReset";
        backend.flush();
        wait(1);
        compare(reset.count, 0, "unchanged stage must not reset history");
        compare(history.contentY, y);
        backend.print("next");
        backend.newline();
        backend.flush();
        wait(1);
        compare(reset.count, 0, "moving stage boundary uses filter row updates");
        compare(history.indexAt(1, history.contentY + 1), first);
        compare(stage.visible, false);
        backend.requestScrollToBottom();
        tryCompare(stage, "visible", true);
        compare(history.visible, false);
        backend.clearAll();
        backend.print("short page");
        backend.newline();
        backend.flush();
        wait(1);
        compare(stage.itemAtIndex(0).y - stage.contentY, 0);
        reset.destroy();
    }

    function test_wheelSwitchesBetweenStageAndHistory() {
        backend.stageMode = true;
        printLines(80);
        const history = findChild(view, "consoleListView");
        const stage = findChild(view, "consoleStageView");
        tryCompare(stage, "visible", true);
        mouseWheel(stage, stage.width / 2, stage.height / 2, 0, 120);
        tryCompare(history, "visible", true);
        compare(stage.visible, false);
        history.positionViewAtEnd();
        mouseWheel(history, history.width / 2, history.height / 2, 0, -120);
        tryCompare(stage, "visible", true);
        compare(history.visible, false);
    }

    // `<img srcb='…'>`：按钮选中/悬停态的替换图（C# ConsoleImagePart.cImageB，
    // 悬停时**改画 srcb**，不是叠加 —— 同一时刻只有一张可见）。
    //
    // 回归背景：两张 Image 的 visible 曾经互为输入（A 读 `!imageItemB.visible`、
    // B 读 `imageItem.visible && …`），运行期报
    //   QML Image: Binding loop detected for property "visible"
    // 成环时 QML 仍会算出一个看得过去的值，所以只断言取值抓不到回归 —— 告警
    // 必须一并断言（夹具把 qWarning 收集起来，见 test_qml_console.cpp）。
    function test_imageButtonSwapsOnHoverWithoutBindingLoop() {
        consoleFixture.clearBindingLoopWarnings();
        backend.clearAll();
        backend.print("A");
        backend.newline(); backend.flush(); view.syncView();

        consoleFixture.request(backend, "INPUT");
        const block = view.blockAt(0, 0);
        verify(block !== null, "第 0 行有区块对象");
        const sel = "face_01_sel";
        block.blockData = {
            kind: "image", text: "face_01", imageButton: sel, isInteger: true,
            clickable: true, generation: backend.generation,
            col: 0, row: 0, cols: 4, rows: 2
        };

        const plain = findChild(block, "blockImage");
        const swap = findChild(block, "blockImageButton");
        verify(plain !== null && swap !== null, "src 与 srcb 各一张 Image");
        compare(block.hasImageButton, true);
        compare(block.showImageButton, false, "未悬停：画 src");
        compare(plain.visible, true);
        compare(swap.visible, false, "未悬停：srcb 不可见");
        compare("" + plain.source, "image://emuera/face_01");
        compare("" + swap.source, "", "不可见的那张不加载 source");

        // 悬停 -> 改画 srcb
        mouseMove(block, block.width / 2, block.height / 2);
        tryVerify(function() { return block.hovered; });
        compare(block.showImageButton, true, "悬停：改画 srcb");
        compare(plain.visible, false, "悬停：src 让位");
        compare(swap.visible, true);
        compare("" + swap.source, "image://emuera/" + sel);
        compare("" + plain.source, "", "让位的那张不再持有 source");

        // 移开 -> 回到 src（换一张 Image 承载，不叠加）
        mouseMove(view, view.width - 2, view.height - 2);
        tryVerify(function() { return !block.hovered; });
        compare(plain.visible, true);
        compare(swap.visible, false);

        // 没有 srcb 的图片区块退化成单张：B 永远不参与
        block.blockData = { kind: "image", text: "face_01", col: 0, row: 0, cols: 1, rows: 1 };
        compare(block.hasImageButton, false);
        compare(block.showImageButton, false);
        compare(swap.visible, false);
        compare(plain.visible, true);

        const loops = consoleFixture.bindingLoopWarnings();
        compare(loops.length, 0, "Image.visible 不得成环：" + loops.join(" | "));
    }

    // 跨行图片：区块按 C++ 给的 rows 撑开（否则 PreserveAspectFit 把整张立絵
    // 压进一行），并应用 ypos 的纵向偏移（画像枠/特效靠它盖在立絵边缘）。
    // 行高恒一格：图片溢出绘制，不推挤后续行。
    function test_imageBlockSpansRowsAndYpos() {
        backend.clearAll();
        backend.printImage("face_01", 400, 400);   // 400% * 16px = 64px = 4 行
        backend.newline();
        backend.flush(); view.syncView();

        const b = view.blockAt(0, 0).blockData;
        compare(b.rows, 4, "C++ 给出 rows == 4");
        const item = view.blockAt(0, 0);
        compare(item.height, b.rows * view.cellHeight, "区块高度 = rows × 行高（跨行）");
        compare(view.lineItem(0).height, view.cellHeight, "行高仍是一格");

        // ypos：C++ 折算成行数偏移（负 = 往上盖）；y 在**行内**相对定位
        backend.clearAll();
        backend.printImage("frame", 400, 400, -800);
        backend.newline();
        backend.flush(); view.syncView();
        const f = view.blockAt(0, 0).blockData;
        // 夹具用 fontSize 18 / lineHeight 20：top = -800*18/100 = -144px -> -144/20 = -7.2 行
        verify(Math.abs(f.offsetRows - (-7.2)) < 0.001, "ypos=-800 -> offsetRows == -7.2");
        const fitem = view.blockAt(0, 0);
        compare(fitem.y, f.offsetRows * view.cellHeight,
                "y 应用 offsetRows（行内相对坐标，负 = 往上探出本行）");
    }

    // 模型行数随缓冲受控；虚拟化下委托按需创建
    function test_boundedWindow() {
        printLines(50);
        compare(view.rowCount, 50, "模型行数 == 缓冲行数");
        compare(view.cellWidth, view.width / backend.gridColumns);
        // 虚拟化：只实例化可见 + cacheBuffer 的委托，50 行不会全部创建
        let alive = 0;
        for (let r = 0; r < 50; ++r)
            if (view.lineItem(r) !== null) ++alive;
        verify(alive < 50, "虚拟化：存活委托 < 总行数（reuseItems 生效前提）");
        verify(alive >= 1, "至少可见行已创建");
    }

    // 按钮命中 -> ConsoleBackend::clickAt -> inputSubmitted
    function test_buttonClick() {
        const spy = Qt.createQmlObject('import QtTest 1.0; SignalSpy {}', tc);
        spy.target = backend;
        spy.signalName = "inputSubmitted";

        backend.clearAll();
        backend.printButton("[1] 选择", 1);
        backend.newline();
        backend.flush(); view.syncView();

        consoleFixture.request(backend, "INPUT");
        backend.flush(); view.syncView();
        const item = view.blockAt(0, 0);
        verify(item !== null && item !== undefined, "no block item");
        compare(item.clickable, true, "区块应可点击");

        const mouse = findChild(item, "blockButtonMouse");
        verify(mouse !== null, "找不到区块 MouseArea");

        // 等待整数输入后，按钮可命中并提交
        mouseClick(mouse);
        compare(spy.count, 1, "点击应触发 1 次 inputSubmitted");
        compare(spy.signalArguments[0][0], 1, "按钮值应为 1");
    }
    // 舞台模式（eraTetris 的 PRINTBUTTON + TONEINPUT 循环）下按钮必须可点：
    // 游戏先打印 [←][↓][→] 等按钮，再进入限时输入；玩家看到的是**舞台**快照，
    // 点击命中的也是舞台委托，历史视图此时完全不可见。
    function test_stageButtonsAreClickableDuringTimedInput() {
        const spy = Qt.createQmlObject('import QtTest 1.0; SignalSpy {}', tc);
        spy.target = backend;
        spy.signalName = "inputSubmitted";

        backend.stageMode = true;
        backend.clearAll();
        backend.printButton("[←]", 4);
        backend.printButton("[→]", 6);
        backend.newline();
        backend.flush(); view.syncView();

        consoleFixture.request(backend, "TONEINPUT");
        backend.flush(); view.syncView();

        const stage = findChild(view, "consoleStageView");
        compare(stage.visible, true, "舞台视图应可见");
        const item = view.blockAt(0, 0);
        verify(item !== null && item !== undefined, "舞台没有区块");
        compare(item.clickable, true, "舞台按钮应可点击");
        compare(item.blockData.isInteger, true, "PRINTBUTTON 是整数按钮");

        mouseClick(findChild(item, "blockButtonMouse"));
        compare(spy.count, 1, "舞台按钮点击应提交 1 次");
        compare(spy.signalArguments[0][0], 4, "按钮值应为 4");
        spy.destroy();
    }

    // 按下即提交必须挺过「按下之后整屏重画、按钮委托被销毁重建」：
    // eraTetris 每次落下或输入超时都会 CLEARLINE 重画（PRINTBUTTON 重新打印），
    // 绑在 clicked（按下 + 松开）上的提交会因为松开时委托已换人而丢失。
    function test_pressCommitsEvenWhenFrameIsRedrawnBeforeRelease() {
        const spy = Qt.createQmlObject('import QtTest 1.0; SignalSpy {}', tc);
        spy.target = backend;
        spy.signalName = "inputSubmitted";

        backend.stageMode = true;
        backend.clearAll();
        backend.printButton("[←]", 4);
        backend.newline();
        consoleFixture.request(backend, "TONEINPUT");
        view.syncView();

        const item = view.blockAt(0, 0);
        verify(item !== null, "舞台没有区块");
        const mouse = findChild(item, "blockButtonMouse");
        const px = mouse.width / 2;
        const py = mouse.height / 2;
        const at = mouse.mapToItem(host, px, py);   // 松开时按屏幕坐标投递

        mousePress(mouse, px, py);
        compare(spy.count, 1, "按下就应提交");
        compare(spy.signalArguments[0][0], 4);

        // 下一帧：CLEARLINE + 重新打印 + 新的限时输入（委托全部重建）
        backend.clearAll();
        backend.print("frame");
        backend.newline();
        backend.printButton("[→]", 6);
        backend.newline();
        consoleFixture.request(backend, "TONEINPUT");
        backend.flush(); view.syncView();

        mouseRelease(host, at.x, at.y);
        compare(spy.count, 1, "松开不得再提交一次");
        spy.destroy();
    }

    // 「字符串等待时整数按钮不可点」曾经是**写反的**规则：C# EmueraConsole.SelectedString
    // 接受任意按钮（整数按钮的 Inputs = 十进制文本），只有 SelectedInteger 才拒
    // 字符串按钮。这里按 C# 语义核对：整数背景按钮在 TINPUTS 下可点，点击交 "0"。
    function test_stringWaitKeepsIntegerButtonsClickable() {
        backend.clearAll();
        backend.printButton("                                                                                ", 0);
        backend.newline();
        consoleFixture.request(backend, "TINPUTS");
        backend.flush(); view.syncView();

        const spy = Qt.createQmlObject('import QtTest 1.0; SignalSpy {}', tc);
        spy.target = backend;
        spy.signalName = "inputSubmittedString";

        const item = view.blockAt(0, 0);
        verify(item !== null && item.blockData.isButton === true);
        compare(item.blockData.isInteger, true);
        compare(item.clickable, true, "字符串等待时整数背景按钮可点击（C# SelectedString）");

        const mouse = findChild(item, "blockButtonMouse");
        verify(mouse !== null, "按钮没有命中区");
        mouseClick(mouse, 2, mouse.height / 2);
        compare(spy.count, 1, "点击整数按钮交付字符串");
        compare(spy.signalArguments[0][0], "0", "交付按钮值的十进制文本");

        backend.clearAll();
        backend.printButton("next", 1);
        backend.newline();
        backend.flush(); view.syncView();
        compare(view.blockAt(0, 0).clickable, true,
                "字符串等待时整数菜单按钮同样可点");
        spy.destroy();
        consoleFixture.finishInput(backend);
    }

    function test_consecutiveInputKinds() {
        let ints = 0;
        let strings = 0;
        function next() {
            ++ints;
            backend.clearAll();
            backend.printButtonStr("next", "accepted");
            backend.newline();
            consoleFixture.request(backend, "INPUTS");
            view.syncView();   // 信号回调里重建的委托要同步布局
        }
        view.syncView();
        function done(value) { compare(value, "accepted"); ++strings; }
        backend.inputSubmitted.connect(next);
        backend.inputSubmittedString.connect(done);
        backend.printButton("start", 0);
        backend.newline();
        consoleFixture.request(backend, "INPUT");
        view.syncView();
        mouseClick(findChild(view.blockAt(0, 0), "blockButtonMouse"));
        compare(ints, 1);
        compare(view.blockAt(0, 0).clickable, true);
        mouseClick(findChild(view.blockAt(0, 0), "blockButtonMouse"));
        compare(strings, 1);
        compare(view.blockAt(0, 0).clickable, false, "submitted buttons are visibly inactive");
        backend.inputSubmitted.disconnect(next);
        backend.inputSubmittedString.disconnect(done);
    }

    // C# MainWindow.richTextBox1_TextChanged：ONEINPUT 系等待时，收到第一个
    // 字符就提交（不需要回车）—— eraTetris 的标题 / 游戏结束都是 ONEINPUT。
    // 帧节拍闸门：视图收到 presentationRequested 后必须处理完这一帧并回报
    // notifyFramePresented()（否则 C++ 只能等 50ms 兜底，等于把计时器推迟到
    // 画面之外）。窗口取 30ms —— 小于兜底的 50ms，所以只有 QML 自己回报
    // 才可能通过；handler 里任何 JS 错误都会让这次回报丢失。
    function test_inputFrameIsReportedBackToBackend() {
        const spy = Qt.createQmlObject('import QtTest 1.0; SignalSpy {}', tc);
        spy.target = backend;
        spy.signalName = "framePresented";
        compare(backend.framePacingAttached, true, "视图挂上后闸门置真");
        // 必须走舞台分支（eraTetris 就是舞台模式）：只有这时 handler 才会
        // 走到「摆好这一帧」的那段代码。
        backend.stageMode = true;
        backend.printButton("[←]", 4);
        backend.newline();
        backend.flush(); view.syncView();
        compare(view.showingStage, true, "舞台模式生效");
        consoleFixture.request(backend, "TONEINPUT");
        spy.wait(30);
        compare(spy.count, 1, "QML 必须回报这一帧已呈现");
        spy.destroy();
    }

    function test_oneInputSubmitsOnFirstCharacter() {
        const spy = Qt.createQmlObject('import QtTest 1.0; SignalSpy {}', tc);
        spy.target = backend;
        spy.signalName = "inputSubmitted";
        const field = findChild(inputBar, "consoleInputField");
        win.requestActivate();
        tryCompare(win, "active", true);

        consoleFixture.request(backend, "TONEINPUT");
        tryCompare(field, "visible", true);
        field.forceActiveFocus();
        tryCompare(field, "activeFocus", true);

        keyClick("7");
        compare(spy.count, 1, "收到第 1 个字符就提交");
        compare(spy.signalArguments[0][0], 7);
        // 回显归属（刀 68）：提交值留在输入栏（TextInput.text 就是回显），
        // 正文只留一条空白记账行；下一次等待开始时才清空。
        compare(field.text, "7", "提交的字符留在输入栏（回显）");
        // 真实路径里提交会经引擎收尾（notifyInputCommitted -> waitingInput=false），
        // 这里补上那一步，再造「下一次等待开始」的上升沿。
        consoleFixture.finishInput(backend);
        consoleFixture.request(backend, "TONEINPUT");
        compare(field.text, "", "下一次等待开始时清空输入栏");
        spy.destroy();
    }

    // 字符串型 ONEINPUT（ONEINPUTS / TONEINPUTS）同为单字符即时提交
    function test_oneInputStringSubmitsOnFirstCharacter() {
        const spy = Qt.createQmlObject('import QtTest 1.0; SignalSpy {}', tc);
        spy.target = backend;
        spy.signalName = "inputSubmittedString";
        const field = findChild(inputBar, "consoleInputField");
        win.requestActivate();
        tryCompare(win, "active", true);

        consoleFixture.request(backend, "TONEINPUTS");
        tryCompare(field, "visible", true);
        field.forceActiveFocus();
        tryCompare(field, "activeFocus", true);

        keyClick("a");
        compare(spy.count, 1, "字符串型 OneInput 也是单字符即时提交");
        compare(spy.signalArguments[0][0], "a");
        spy.destroy();
    }

    // 普通输入（TINPUT/INPUT）必须回车：单字符不得提前提交
    function test_normalInputWaitsForEnter() {
        const spy = Qt.createQmlObject('import QtTest 1.0; SignalSpy {}', tc);
        spy.target = backend;
        spy.signalName = "inputSubmitted";
        const field = findChild(inputBar, "consoleInputField");
        win.requestActivate();
        tryCompare(win, "active", true);

        consoleFixture.request(backend, "TINPUT");
        tryCompare(field, "visible", true);
        field.forceActiveFocus();
        tryCompare(field, "activeFocus", true);

        keyClick("7");
        compare(spy.count, 0, "普通输入打完字符不能即时提交");
        keyClick(Qt.Key_Return);
        compare(spy.count, 1, "回车才提交");
        compare(spy.signalArguments[0][0], 7);
        spy.destroy();
    }

    function test_primitiveMouseAndKey() {
        const spy = Qt.createQmlObject('import QtTest 1.0; SignalSpy {}', tc);
        spy.target = backend;
        spy.signalName = "mouseKeySubmitted";
        consoleFixture.request(backend, "INPUTMOUSEKEY");
        const mouse = findChild(view, "primitiveMouse");
        mouseClick(mouse, 12, 15, Qt.LeftButton);
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], 1);
        compare(spy.signalArguments[0][1], 1048576);
        compare(spy.signalArguments[0][2], 12);
        compare(spy.signalArguments[0][3], 15 - mouse.height);
        consoleFixture.request(backend, "INPUTMOUSEKEY");
        win.requestActivate();
        tryCompare(win, "active", true);
        mouse.parent.forceActiveFocus();
        tryCompare(mouse.parent, "activeFocus", true);
        consoleFixture.pressLeft(mouse);
        compare(spy.count, 2);
        compare(spy.signalArguments[1][0], 3);
        compare(spy.signalArguments[1][1], 37);
        spy.destroy();
    }

    // 历史满（MaxLog）时头部逐行裁剪也在改 contentY —— 每次刷新照样停在末尾。
    function test_followsTailWhileTrimming() {
        const lv = findChild(view, "consoleListView");
        backend.setMaxLog(30);          // <= kTrimBatch：溢出即逐行裁头部
        backend.clearAll();
        for (let i = 0; i < 80; ++i) {
            backend.print("L" + i);
            backend.newline();
            backend.flush();
            wait(1);
            // 坐标系：Flickable 的 contentY 相对 contentItem 顶端，而 ListView
            // 的 contentHeight 相对「模型原点」；头部委托被 cacheBuffer 释放后
            // originY 会带走一段偏移（格高为非整数时是亚像素量，本例 -1.1px）。
            // 判「在末尾」必须把 originY 补回去（与 Console.qml 的 atTail 同式）。
            verify(lv.contentY - lv.originY + lv.height >= lv.contentHeight - 1,
                   "第 " + i + " 行刷新后仍停在末尾（contentY=" + lv.contentY
                   + " originY=" + lv.originY + " contentHeight=" + lv.contentHeight + "）");
        }
        verify(lv.atTail, "头部裁剪没有打断跟随");
        backend.setMaxLog(5000);
    }

    // 每次刷新（flush 发布新行）之后都停在末尾：最新行在视口里，而不是每次
    // 差最新那一行（ListView::forceLayout 之后再 positionViewAtEnd）。
    // 走真实路径：只 flush，不手动 syncView，等 Qt.callLater 的锚底跑完。
    function test_followsTailOnEveryFlush() {
        const lv = findChild(view, "consoleListView");
        verify(lv !== null, "ListView 已创建");
        backend.clearAll();
        view.syncView();

        const n = 60;   // 远超一屏
        for (let i = 0; i < n; ++i) {
            backend.print("L" + i);
            backend.newline();
            backend.flush();
            wait(1);    // 事件循环：Qt.callLater(scrollToTail)
            // 同上：contentHeight 相对模型原点，contentY 相对 contentItem，
            // 两者之间差一个 originY（头部委托释放后非零）。
            verify(lv.contentY - lv.originY + lv.height >= lv.contentHeight - 1,
                   "第 " + i + " 次刷新后停在末尾（contentY=" + lv.contentY
                   + " originY=" + lv.originY + " contentHeight=" + lv.contentHeight + "）");
        }

        compare(view.rowCount, n, "模型行数");
        const last = view.lineItem(n - 1);
        verify(last !== null, "最新行委托已创建");
        const top = last.y - lv.contentY;
        verify(top >= 0 && top + view.cellHeight <= lv.height + 1,
               "最新行落在视口内（top=" + top + " viewport=" + lv.height + "）");

        // 用户上滚 = 履历（回看）状态；但下一次输出（C# verticalScrollBarUpdate
        // 的 move > 0）立刻贴回最新行 —— C# 没有「上滚就退出跟随」的粘滞开关。
        mouseDrag(lv, lv.width / 2, lv.height / 2, 0, 60,
                  Qt.LeftButton, Qt.NoModifier, 200);
        tryCompare(lv, "moving", false);
        verify(lv.contentY + lv.height < lv.contentHeight - 1, "上滚后离开末尾");
        verify(!lv.atTail, "上滚 = 履历（isBacklog）状态");

        backend.print("L" + n);
        backend.newline(); backend.flush(); wait(1);
        verify(lv.contentY - lv.originY + lv.height >= lv.contentHeight - 1,
               "新行一来即贴回最新行（contentY=" + lv.contentY
               + " originY=" + lv.originY + "）");
        verify(lv.atTail, "回到末尾");

        // 菜单/C++ 的「滚动到底部」（requestScrollToBottom）同样贴底
        backend.print("L" + (n + 1));
        backend.newline(); backend.flush(); wait(1);
        backend.requestScrollBy(3);               // D-Bus/菜单滚动：向上 3 行
        verify(lv.contentY + lv.height < lv.contentHeight - 1, "向上滚动后离开末尾");
        backend.requestScrollToBottom(); wait(1);
        verify(lv.contentY - lv.originY + lv.height >= lv.contentHeight - 1,
               "滚动到底部请求生效（contentY=" + lv.contentY + " originY=" + lv.originY + "）");
        verify(lv.atTail, "回到末尾");
    }

    // ---------------------------------------------------------------------
    // 运行期精灵「像素变了就重取」的接线（FL3.8 合成像 FACE_*）。
    //
    // provider 侧每次现读源 G 的最新像素，但 QQuickImageProvider 文档写明
    // 「同一个 image:// URL 命中缓存后不再回调 provider」——URL 不变，Image
    // 就不会再问。Image 没有 invalidate API，唯一手段是让 source 变一次。
    //
    // 曾经的写法是 `imageItem.source = ""` 再在 Qt.callLater 里用 Qt.binding
    // 还原：卸载后 delegate 被模型刷新销毁 → 还原永不发生，合成像整块空白。
    // 这里锁住新语义：source 的绑定**始终有效**，卸载只是一个布尔量的中间态，
    // 翻回去时不需要任何人重新赋值。
    function test_spriteImageReloadKeepsSourceBound() {
        const blockComp = Qt.createComponent(
            "file://" + consoleQmlPath.replace("Console.qml", "ConsoleBlock.qml"));
        verify(blockComp.status === Component.Ready, blockComp.errorString());
        const blk = blockComp.createObject(host, {
            "blockData": ({ kind: "image", text: "FACE_0", col: 0, row: 0,
                            cols: 1, rows: 1, pxLeft: 0, pxTop: 0,
                            pxWidth: 200, pxHeight: 200 }),
            "cellWidth": 7, "cellHeight": 20, "fontSize": 14
        });
        verify(blk !== null, "ConsoleBlock.qml 创建失败");

        let img = null;
        for (let i = 0; i < blk.children.length; ++i) {
            if (blk.children[i].objectName === "blockImage")
                img = blk.children[i];
        }
        verify(img !== null, "找到 blockImage");
        compare(String(img.source), "image://emuera/FACE_0", "初次绑定到资源名");
        verify(img.cache === false, "cache:false —— 重取才会真的回调 provider");

        blk.imagesUnloaded = true;
        compare(String(img.source), "", "卸载态：source 为空（一次真正的免缓存重取）");
        blk.imagesUnloaded = false;
        compare(String(img.source), "image://emuera/FACE_0",
                "翻回来即恢复：绑定没被打断，不依赖任何回调再赋值");

        // 资源名换了也要跟着变（绑定没被 source="" 打断的反证）
        blk.blockData = { kind: "image", text: "FACE_1", col: 0, row: 0,
                          cols: 1, rows: 1, pxLeft: 0, pxTop: 0,
                          pxWidth: 200, pxHeight: 200 };
        compare(String(img.source), "image://emuera/FACE_1", "换名跟随");
        blk.destroy();
    }

}
