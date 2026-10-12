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
    // C# InputRequest.OneInput（ONEINPUT/ONEINPUTS/TONEINPUT/TONEINPUTS）：
    // 单字符即时提交 —— 收到第一个字符就结束等待，不需要回车。
    readonly property bool oneInputKind: backend && backend.oneInput
    // C# MainWindow.last_inputed：等待开始时的输入栏内容。只有**新增**字符
    // 才算「输入」（删除或内容不变不提交）。
    property string lastInputed: ""
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
        submitText(inputField.text);
        inputField.text = "";
        lastInputed = "";
    }

    // 提交一段文本（不读输入栏）：回车路径与单字符即时提交共用。
    function submitText(commandText) {
        if (!backend)
            return;
        if (commandText.startsWith("\\")) {
            if (root.engine)
                root.engine.executeConsoleCommand(commandText);
            return;
        }
        if (!backend.waitingInput || root.titleActive || backend.inputKind === "INPUTMOUSEKEY")
            return;
        if (anyKeyInput) {
            backend.submitAnyKey();
            return;
        }
        // 空输入 / 非法输入一律交给 C++ 按 C# doInputToEmueraProgram 裁决。
        if (stringInputKind)
            backend.submitStringText(commandText);
        else
            backend.submitIntegerText(commandText);
    }

    // C# MainWindow.richTextBox1_TextChanged（单字符即时提交）：
    //   * 只在 ONEINPUT 系等待或「任意键等待」时生效；
    //   * 输入栏为空不提交（C# string.IsNullOrEmpty 直接返回）；
    //   * 删除或内容没有变化不算输入（长度不增长）；
    //   * 否则收到第一个字符就提交，不用回车。
    // 单字符截断（C# inputs.Remove(1)）在 C++ 的 submitStringText/submitIntegerText
    // 里按 OneInput 统一处理 —— 鼠标点按钮（changedByMouse）不截断。
    function handleTextEdited() {
        if (!backend || !backend.waitingInput)
            return;
        if (!oneInputKind && !anyKeyInput)
            return;
        if (inputField.text.length === 0)
            return;
        if (inputField.text.length <= lastInputed.length) {
            lastInputed = inputField.text;      // 删除/无变化：只更新基线
            return;
        }
        const typed = inputField.text.slice(lastInputed.length);
        lastInputed = inputField.text;
        inputField.text = "";
        lastInputed = "";
        submitText(typed);
    }

    // 等待类型变化（开始等待 / 结束等待）以及脚本改文本框（SETTEXTBOX）时
    // 重设基线，避免把「上一轮残留的文本」当成这一轮的输入。
    Connections {
        target: root.backend
        function onWaitingInputChanged() { root.lastInputed = inputField.text; }
        function onTextboxTextChanged() { root.lastInputed = inputField.text; }
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
        placeholderText: root.backend && root.backend.waitingInput ? (root.anyKeyInput ? qsTr("回车/点击继续（%1）").arg(root.backend.inputKind) : qsTr("输入（%1）").arg(root.backend.inputKind)) : ""
        // 输入类型分支限制：整数型输入只接受数字（INPUT 可负）
        validator: root.backend && root.backend.waitingInput && !root.stringInputKind ? intOnly : null
        inputMethodHints: root.stringInputKind ? Qt.ImhNone : Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
        onAccepted: root.submit()
        // 用户编辑（程序性赋值不触发）：单字符即时提交入口
        onTextEdited: root.handleTextEdited()
    }
    RegularExpressionValidator {
        id: intOnly
        regularExpression: /-?[0-9]+/
    }

}
