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
    }

    function cleanup() {
        if (view) { view.destroy(); view = null; }
        backend.clearAll();
    }

    // 分层：text 层的区块数 == C++ 给的 textBlocks 数（C++ 说了算）
    function test_layers() {
        backend.clearAll();
        backend.print("A"); backend.newline();
        backend.print("B"); backend.newline();
        backend.print("C"); backend.newline();
        backend.flush();

        compare(view.textBlockCount, backend.textBlocks.length);
        compare(view.imageBlockCount, backend.imageBlocks.length);
        // 每行一个文本区块
        verify(backend.textBlocks.length === 3, "3 行 -> 3 个文本区块");
    }

    // 位置/尺寸来自 C++（绝对位置 x/y、相对位置 relX/relY、动态尺寸 w/h）
    function test_blockGeometryFromCpp() {
        backend.clearAll();
        backend.print("hello");
        backend.newline();
        backend.flush();

        const blocks = backend.textBlocks;
        verify(blocks.length === 1, "1 个区块");
        const b = blocks[0];
        verify(b.col !== undefined && b.row !== undefined, "区块带绝对网格坐标 col/row");
        verify(b.relCol !== undefined && b.relRow !== undefined, "区块带相对网格坐标 relCol/relRow");
        verify(b.cols > 0 && b.rows > 0, "区块带格子数（单位：区块长/区块高）");

        // 像素大小由 QML 决定：QML 用自己的 cell 宽高 × 格子数
        const item = view.blockAt(0);
        verify(item !== null, "no block item");
        compare(item.x, b.col * view.cellWidth);
        compare(item.y, b.row * view.cellHeight);
        compare(item.width, b.cols * view.cellWidth);
        compare(item.height, b.rows * view.cellHeight);
    }

    function test_gridGlyphBounds() {
        view.fontSize = 19;
        view.fontName = "DejaVu Sans";
        backend.clearAll();
        backend.print("WWiii■■□　");
        backend.printButton("[HOLD]", 8);
        backend.newline(); backend.flush();
        compare(view.cellWidth, view.width / backend.gridColumns);
        const block = view.blockAt(0);
        const row = findChild(block, "textCells");
        let total = 0;
        let glyphs = 0;
        for (let i = 0; i < row.children.length; ++i) {
            const cell = row.children[i];
            const glyph = findChild(cell, "gridGlyph");
            if (!glyph) continue;
            console.log("grid advance", glyph.text, glyph.implicitWidth, "cell", cell.width);
            verify(Math.abs(glyph.implicitWidth * glyph.transform[0].xScale - cell.width) < 0.01);
            verify(cell.clip);
            total += cell.width;
            ++glyphs;
        }
        compare(glyphs, 9);
        compare(total, block.width);
        compare(view.blockAt(1).x, block.x + block.width);
    }

    // 按钮 span 按「网格文字」渲染：00097c1 曾把它换成原生 Button
    // （内边距/最小尺寸让按钮比文字宽，和相邻区块对不齐），现已移除。
    // 这里锁住：逐字格子数 == 文字长度、宽高 == 网格尺寸。
    function test_buttonSpanIsGridText() {
        backend.clearAll();
        backend.printButton("[HOLD]", 8);
        backend.newline(); backend.flush();

        const block = view.blockAt(0);
        verify(block !== null && block.blockData.isButton === true, "按钮区块");
        const row = findChild(block, "textCells");
        verify(row !== null && row.visible, "按钮 span 用网格文字容器渲染");
        let glyphs = 0;
        for (let i = 0; i < row.children.length; ++i)
            if (findChild(row.children[i], "gridGlyph")) ++glyphs;
        compare(glyphs, block.blockData.text.length);
        compare(block.width, block.blockData.cols * view.cellWidth);
        compare(block.height, view.cellHeight);
    }

    function test_spanStyleOverridesAndDefaults() {
        backend.clearAll();
        backend.print("A"); backend.newline(); backend.flush();
        const block = view.blockAt(0);
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

    // 图片层：image 区块进 imageLayer
    function test_imageLayer() {
        backend.clearAll();
        backend.print("A");
        backend.printImage("face_01", 40, 40);
        backend.newline();
        backend.flush();

        verify(backend.imageBlocks.length === 1, "1 个图片区块");
        compare(view.imageBlockCount, 1);
        compare(backend.imageBlocks[0].text, "face_01");
    }

    // 跨行图片：QML 侧必须按 C++ 给的 rows 撑开（否则 PreserveAspectFit 把整张
    // 立絵压进一行，eraTW 的「画像尺寸 拡大/縮小」在画面上看不出任何变化），
    // 并应用 ypos 的纵向偏移（eraTW 的画像枠/特效靠它盖在立絵边缘）。
    function test_imageBlockSpansRowsAndYpos() {
        backend.clearAll();
        backend.printImage("face_01", 400, 400);   // 400% * 16px = 64px = 4 行
        backend.newline();
        backend.flush();

        verify(backend.imageBlocks.length === 1, "1 个图片区块");
        const b = backend.imageBlocks[0];
        compare(b.rows, 4, "C++ 给出 rows == 4");
        const item = view.imageBlockAt(0);
        verify(item !== null, "no image block item");
        compare(item.width, b.cols * view.cellWidth);
        compare(item.height, b.rows * view.cellHeight, "区块高度 = rows × 行高（跨行）");

        // ypos：C++ 折算成行数偏移（负 = 往上盖）。
        // 先垫 8 行文本，让 ypos=-8 的框正好落在窗口里（全在窗口上方时会被裁掉）。
        backend.clearAll();
        for (let i = 0; i < 8; ++i) { backend.print("x"); backend.newline(); }
        backend.printImage("frame", 400, 400, -800);
        backend.newline();
        backend.flush();
        verify(backend.imageBlocks.length === 1, "1 个 ypos 图片区块");
        const f = backend.imageBlocks[0];
        // 夹具用 fontSize 18 / lineHeight 20：top = -800*18/100 = -144px -> -144/20 = -7.2 行
        verify(Math.abs(f.offsetRows - (-7.2)) < 0.001, "ypos=-800 -> offsetRows == -7.2");
        const fitem = view.imageBlockAt(0);
        compare(fitem.y, (f.row + f.offsetRows) * view.cellHeight, "y 应用 offsetRows");
    }

    // 有界窗口：区块数随可见行数受控
    function test_boundedWindow() {
        backend.clearAll();
        for (let i = 0; i < 50; ++i) {
            backend.print("line " + i);
            backend.newline();
        }
        backend.flush();
        // 逻辑列数由 C++ 按「窗口宽 ÷ 单元格宽（FontSize/2）」推导（与 QML 同源），
        // 所以这里按公式核对，而不是写死旧的默认 80 列。
        // 窗口宽取 ConsoleLayout 的默认值（760px；QML 侧不暴露该属性）。
        const colPx = Math.max(1, Math.floor(view.fontSize / 2));
        compare(backend.gridColumns, Math.max(1, Math.floor(760 / colPx)));
        compare(backend.gridRows, 25);
        compare(view.cellWidth, view.width / backend.gridColumns);
        compare(view.cellHeight, 8);
        compare(view.textBlockCount, backend.textBlocks.length);
    }

    // 按钮命中 -> ConsoleBackend::clickAt -> inputSubmitted
    function test_buttonClick() {
        const spy = Qt.createQmlObject('import QtTest 1.0; SignalSpy {}', tc);
        spy.target = backend;
        spy.signalName = "inputSubmitted";

        backend.clearAll();
        backend.printButton("[1] 选择", 1);
        backend.newline();
        backend.flush();

        const item = view.blockAt(0);
        verify(item !== null && item !== undefined, "no block item");
        compare(item.clickable, true, "区块应可点击");

        const mouse = findChild(item, "blockButtonMouse");
        verify(mouse !== null, "找不到区块 MouseArea");

        // 输入裁决：未等待输入时点击不产生提交，也不失效按钮
        mouseClick(mouse);
        compare(spy.count, 0, "未等待输入时点击被忽略");
        compare(item.clickable, true, "被忽略的点击不应使按钮失效");

        // 等待整数输入后，同一按钮的点击生效
        consoleFixture.request(backend, "INPUT");
        mouseClick(mouse);
        compare(spy.count, 1, "点击应触发 1 次 inputSubmitted");
        compare(spy.signalArguments[0][0], 1, "按钮值应为 1");
    }
    function test_consecutiveInputKinds() {
        let ints = 0;
        let strings = 0;
        function next(value) {
            ++ints;
            backend.clearAll();
            backend.printButtonStr("next", "accepted");
            backend.newline();
            consoleFixture.request(backend, "INPUTS");
        }
        function done(value) { compare(value, "accepted"); ++strings; }
        backend.inputSubmitted.connect(next);
        backend.inputSubmittedString.connect(done);
        backend.printButton("start", 0);
        backend.newline();
        consoleFixture.request(backend, "INPUT");
        mouseClick(findChild(view.blockAt(0), "blockButtonMouse"));
        compare(ints, 1);
        compare(view.blockAt(0).clickable, true);
        mouseClick(findChild(view.blockAt(0), "blockButtonMouse"));
        compare(strings, 1);
        compare(view.blockAt(0).clickable, false, "submitted buttons are visibly inactive");
        backend.inputSubmitted.disconnect(next);
        backend.inputSubmittedString.disconnect(done);
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

}
