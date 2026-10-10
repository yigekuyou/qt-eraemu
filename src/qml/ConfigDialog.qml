import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import io.yigekuoyou.eraengine

// C# ConfigDialog 的 QML 实现：数据定义由 EraEngine::configItems() 提供，
// 控件只编辑字符串值；保存统一走 ConfigLoader，保证读写路径一致。
Dialog {
    id: dlg
    required property EraEngine engine
    property GuiManager gui: null
    title: qsTr("コンフィグ / 设置")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 24, 720)
    height: Math.min(parent.height - 24, 620)
    standardButtons: Dialog.Save | Dialog.Cancel

    contentItem: ScrollView {
        clip: true
        ColumnLayout {
            width: Math.max(0, dlg.width - 36)
            spacing: 6
            Repeater {
                model: dlg.engine.configItems()
                delegate: RowLayout {
                    id: row
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: row.modelData.key === "RealRounding" ? qsTr("浮点结果取整方式（TIMES）") : row.modelData.group + " / " + row.modelData.key; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    CheckBox {
                        visible: row.modelData.type === "bool"
                        checked: dlg.engine.configValue(row.modelData.key).toLowerCase() === "true"
                                  || dlg.engine.configValue(row.modelData.key) === "1"
                        onToggled: dlg.engine.setConfigValue(row.modelData.key, checked ? "true" : "false")
                    }
                    SpinBox {
                        visible: row.modelData.type === "int"
                        from: -2147483647; to: 2147483647
                        value: Number(dlg.engine.configValue(row.modelData.key) || 0)
                        onValueModified: dlg.engine.setConfigValue(row.modelData.key, String(value))
                    }
                    ComboBox {
                        visible: row.modelData.type === "enum"
                        model: row.modelData.key === "内部で使用する東アジア言語"
                               ? ["JAPANESE", "KOREAN", "CHINESE_HANS", "CHINESE_HANT"]
                               : ["AUTO", "UTF-8", "UTF-8-BOM", "SHIFT-JIS", "GB18030", "BIG5"]
                        currentIndex: Math.max(0, indexOfValue(dlg.engine.configValue(row.modelData.key)))
                        onActivated: dlg.engine.setConfigValue(row.modelData.key, currentText)
                    }
                    ComboBox {
                        objectName: "realRoundingCombo"
                        visible: row.modelData.type === "rounding"
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: qsTr("四舍五入"), value: "round" },
                            { text: qsTr("向下取整"), value: "floor" },
                            { text: qsTr("向上取整"), value: "ceil" }
                        ]
                        currentIndex: Math.max(0, indexOfValue(dlg.engine.configValue(row.modelData.key)))
                        onActivated: dlg.engine.setConfigValue(row.modelData.key, currentValue)
                    }
                    TextField {
                        visible: row.modelData.type === "string"
                        text: dlg.engine.configValue(row.modelData.key)
                        Layout.preferredWidth: 230
                        onEditingFinished: dlg.engine.setConfigValue(row.modelData.key, text)
                    }
                }
            }
        }
    }
    onAccepted: dlg.engine.saveConfigFiles()
}