import QtQuick
import QtQuick.Dialogs
import QtQuick.Controls
import Qt.labs.platform as Platform
import io.yigekuoyou.eraengine
import org.kde.kirigami as Kirigami

Window {
    id: window
    visible: true
    property string gameDirectory: ""
    signal init(string path)
    title: eraEngine.gameBaseData.windowTitle || "Emuera Engine"
    color: Kirigami.Theme.backgroundColor
    Platform.FolderDialog {
        id: folderDialog
        title: qsTr("Select Directory")
        folder: currentDir// 赋予一个安全的初始默认路径
        onAccepted: {
            window.gameDirectory = folderDialog.folder;
            window.init(window.gameDirectory);
            eraEngine.gameDirectory = folderDialog.folder;
        }
    }
    EraEngine {
        id: eraEngine
        //将 QML 的 window.init 信号绑定到 C++ 的初始化槽函数（或通过 Connections/直接调用）
    }
    EraRender {
        id: eraRender
        anchors.fill: parent
        engine: eraEngine
    }
    Window {
        id: aboutWindow
        title: qsTr("&aboutGame")
        visible: false
        modality: Qt.ApplicationModal // 设置为应用模态（可选，限制父子窗口焦点）
        flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint
        color: Kirigami.Theme.backgroundColor
        Column {
            width: 300
            spacing: 10

            Label {
                text: qsTr("Game Title: ") + (eraEngine.gameBaseData.title || qsTr("Unknown"))
                color: Kirigami.Theme.textColor
            }
            Label {
                text: qsTr("Author: ") + (eraEngine.gameBaseData.author || qsTr("Unknown"))
                color: Kirigami.Theme.textColor
            }
            Label {
                text: qsTr("Version: ") + (eraEngine.gameBaseData.version || qsTr("Unknown"))
                color: Kirigami.Theme.textColor
            }
            Label {
                text: qsTr("Release Year: ") + (eraEngine.gameBaseData.releaseYear || qsTr("Unknown"))
                color: Kirigami.Theme.textColor
            }
            Label {
                text: qsTr("Additional Info: ") + (eraEngine.gameBaseData.additionalInfo || qsTr("None"))
                color: Kirigami.Theme.textColor
            }
        }
    }
    Platform.MenuBar {
        Platform.Menu {
            id: fileMenu
            title: qsTr("File")
            Platform.MenuItem {
                text: qsTr("open")
                shortcut: StandardKey.Open
                onTriggered: folderDialog.open()
            }
            Platform.MenuItem {
                text: qsTr("reboot")
                shortcut: StandardKey.Open
            }
            Platform.MenuItem {
                text: qsTr("reboot")
                shortcut: StandardKey.Open
            }
            Platform.MenuItem {
                text: qsTr("save log")
            }
            Platform.MenuItem {
                text: qsTr("retunrn title")
                onTriggered: if (eraRender.engine)
                    eraRender.engine.gotoTitle()
            }
            Platform.MenuItem {
                text: qsTr("settings")
                onTriggered: configWindow.visible = true // 弹出配置窗口
            }
            Platform.MenuItem {
                text: qsTr("exit")
                onTriggered: Qt.quit()
            }
        }
        Platform.Menu {
            id: editMenu
            title: qsTr("&Edit")
            // ...
        }

        Platform.Menu {
            id: viewMenu
            title: qsTr("&View")
            // ...
        }

        Platform.Menu {
            id: helpMenu
            title: qsTr("&Help")
            Platform.MenuItem {
                text: qsTr("&about")
                onTriggered: {
                    aboutWindow.visible = true;
                    aboutWindow.raise();
                    aboutWindow.requestActivate();
                }
            }
        }
    }
}
