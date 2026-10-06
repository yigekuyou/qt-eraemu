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

    // 打印 n 行并等模型/视图同步（flush 一帧 + 事件循环）
    function printLines(n) {
        backend.clearAll();
        for (let i = 0; i < n; ++i) {
            backend.print("L" + i);
            backend.newline();
        }
        backend.flush(); view.syncView();
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
        compare(view.cellWidth, (view.width - view.scrollBarWidth) / backend.gridColumns);
        const block = view.blockAt(0, 0);
        const row = findChild(block, "textCells");
        let total = 0;
        let chars = 0;
        for (let i = 0; i < row.children.length; ++i) {
            const cell = row.children[i];
            const glyph = findChild(cell, "gridGlyph");
            if (!glyph) continue;
            // 一个「段」承载相邻同格宽的若干字符（ConsoleBlock.glyphRuns）。
            // 不变式：段宽 == 字数 × 格宽、缩放后正好铺满段宽、缩放原点在
            // Left（不外溢到相邻 span —— 取代 delegate 内 clip：Qt 文档
            // 「Performance considerations」明确禁止在 delegate 里用 clip）。
            const run = cell.modelData;
            verify(Math.abs(glyph.implicitWidth * glyph.transform[0].xScale - cell.width) < 0.01);
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
        compare(view.cellWidth, (view.width - view.scrollBarWidth) / backend.gridColumns);
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

        const item = view.blockAt(0, 0);
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
            verify(lv.contentY + lv.height >= lv.contentHeight - 1,
                   "第 " + i + " 行刷新后仍停在末尾（contentY=" + lv.contentY
                   + " contentHeight=" + lv.contentHeight + "）");
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
            verify(lv.contentY + lv.height >= lv.contentHeight - 1,
                   "第 " + i + " 次刷新后停在末尾（contentY=" + lv.contentY
                   + " contentHeight=" + lv.contentHeight + "）");
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
        verify(lv.contentY + lv.height >= lv.contentHeight - 1,
               "新行一来即贴回最新行（contentY=" + lv.contentY + "）");
        verify(lv.atTail, "回到末尾");

        // 菜单/C++ 的「滚动到底部」（requestScrollToBottom）同样贴底
        backend.print("L" + (n + 1));
        backend.newline(); backend.flush(); wait(1);
        backend.requestScrollBy(3);               // D-Bus/菜单滚动：向上 3 行
        verify(lv.contentY + lv.height < lv.contentHeight - 1, "向上滚动后离开末尾");
        backend.requestScrollToBottom(); wait(1);
        verify(lv.contentY + lv.height >= lv.contentHeight - 1, "滚动到底部请求生效");
        verify(lv.atTail, "回到末尾");
    }

}
