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
        GroupBox {
            title: qsTr("变量与运行状态")
            Layout.fillWidth: true
            Layout.fillHeight: true
            Label {
                anchors.fill: parent
                text: qsTr("调试窗口已打开。变量和运行状态通过 EraEngine C++ 接口提供。\n当前目录：") + dlg.engine.gameDirectory
                wrapMode: Text.WordWrap
            }
        }
    }
}