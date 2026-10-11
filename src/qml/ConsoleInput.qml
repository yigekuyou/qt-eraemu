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
import io.yigekuoyou.eraengine

// 窗口底栏：与游戏舞台为兄弟节点，使用屏幕坐标，不继承舞台缩放。
FocusScope {
    id: root
    property ConsoleBackend backend: null
    property EraEngine engine: null
    property bool titleActive: false
    property string fontName: ""
    property int fontSize: 18
    property string foreColor: ""
    readonly property bool isMobile: Qt.platform.os === "android" || Qt.platform.os === "ios"
    readonly property bool anyKeyInput: backend && backend.anyKeyInput
    readonly property bool stringInputKind: backend && backend.stringInput
    // 固定高度，placeholder、validator、waitingInput 均不能改变底栏尺寸。
    implicitHeight: Math.max(24, fontSize + 8)
    focus: true

    function submit() {
        if (!backend)
            return;
        const commandText = inputField.text.trim();
        if (commandText.startsWith("\\")) {
            if (root.engine)
                root.engine.executeConsoleCommand(commandText);
            inputField.text = "";
            return;
        }
        if (!backend.waitingInput || root.titleActive || backend.inputKind === "INPUTMOUSEKEY")
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

    // CLEARTEXTBOX（C# Console.ClearTextBox）：清空输入栏内容
    Connections {
        target: root.backend
        function onClearTextBoxRequested() { inputField.text = ""; }
        function onTextboxTextChanged(text) { inputField.text = text; }
    }

    // 底部输入行：等待输入时贴着最下面出现——无边框、无背景、无按钮，
    // 高度只有一行文字；直接键入、回车即提交。非等待状态高度为 0。
    TextField {
        id: inputField
        objectName: "consoleInputField"
        // 输入状态仅用于提示、校验和提交。
        focus: !root.isMobile
        enabled: true
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.height
        visible: true
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
        placeholderText: root.backend && root.backend.waitingInput ? (root.anyKeyInput ? ("回车/点击继续（" + root.backend.inputKind + "）") : ("输入（" + root.backend.inputKind + "）")) : ""
        // 输入类型分支限制：整数型输入只接受数字（INPUT 可负）
        validator: root.backend && root.backend.waitingInput && !root.stringInputKind ? intOnly : null
        inputMethodHints: root.stringInputKind ? Qt.ImhNone : Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
        onAccepted: root.submit()
    }
    RegularExpressionValidator {
        id: intOnly
        regularExpression: /-?[0-9]+/
    }

}
