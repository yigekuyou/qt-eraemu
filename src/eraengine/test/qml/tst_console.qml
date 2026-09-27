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

    // 可见窗口重建：live item 数 == backend.visibleLineCount()
    function test_windowRebuild() {
        backend.print("A"); backend.newline();
        backend.print("B"); backend.newline();
        backend.print("C"); backend.newline();
        backend.flush();                       // flush 点 -> windowChanged -> rebuild

        verify(backend.visibleLineCount() === 3, "visibleLineCount == 3");
        compare(view.visibleCount, backend.visibleLineCount());
    }

    // 有界窗口：行数超过可见数时，只建 visibleCount 个 item
    function test_boundedWindow() {
        for (let i = 0; i < 50; ++i) {
            backend.print("line " + i);
            backend.newline();
        }
        backend.flush();
        compare(view.visibleCount, backend.visibleCount);
        verify(view.visibleCount <= 10);        // 200px / 20px
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

        const line = view.lineAt(view.visibleCount - 1);
        verify(line !== null && line !== undefined, "no line item");

        const mouse = findChild(line, "spanButtonMouse");
        verify(mouse !== null, "找不到按钮 MouseArea");

        mouseClick(mouse);
        compare(spy.count, 1, "点击应触发 1 次 inputSubmitted");
        compare(spy.signalArguments[0][0], 1, "按钮值应为 1");
    }
}
