import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import io.yigekuoyou.eraengine

Dialog {
    id: dlg
    required property EraEngine engine
    title: qsTr("デバッグウインドウ / 调试")
    modal: false
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 24, 700)
    height: Math.min(parent.height - 24, 500)
    standardButtons: Dialog.Close

    ColumnLayout {
        anchors.fill: parent
        TextField {
            id: expression
            Layout.fillWidth: true
            placeholderText: qsTr("输入表达式")
            onAccepted: {
                const value = dlg.engine.variableStorage.evaluateExpression(text);
                result.text = text + " = " + value;
            }
        }
        Label { id: result; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        GroupBox {
            title: qsTr("变量与运行状态")
            Layout.fillWidth: true
            Layout.fillHeight: true
            Label {
                anchors.fill: parent
                text: qsTr("调试窗口已打开。变量表和表达式求值通过同一个 EraEngine C++ 接口扩展。\n当前目录：") + dlg.engine.gameDirectory
                wrapMode: Text.WordWrap
            }
        }
    }
}