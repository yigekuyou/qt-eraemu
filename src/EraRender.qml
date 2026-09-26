import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import io.yigekuoyou.eraengine
import org.kde.kirigami as Kirigami

Item {
    id: eraRender
    anchors.fill: parent

    // Bind to the C++ engine
    property EraEngine engine: null

    // Access ConsoleDisplay singleton directly (no instantiation needed)
    // ConsoleDisplay is used internally by the rendering system

    // Main content area with scrolling
    ScrollView {
        id: scrollView
        anchors.fill: parent
        anchors.margins: 10
        contentWidth: availableWidth
        contentHeight: contentItem.implicitHeight

        Column {
            id: contentColumn
            spacing: 5
            width: parent.width

            // Main display area for script output
            // TODO: Replace with actual rendering from engine
            Item {
                id: displayArea
                width: parent.width
                height: Math.max(300, parent.height - 100)

                Rectangle {
                    id: displayBox
                    anchors.fill: parent
                    color: Kirigami.Theme.backgroundColor
                    border.color: Kirigami.Theme.disabledTextColor
                    border.width: 1
                    radius: 5

                    // Text display - placeholder until rendering system is complete
                    Column {
                        id: textDisplay
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 5

                        // TODO: Replace with actual console output
                        Label {
                            text: "Console output will appear here"
                            color: Kirigami.Theme.disabledTextColor
                            font.pixelSize: 12
                        }
                    }
                }
            }

            // Button area for interactive buttons
            Item {
                id: buttonArea
                width: parent.width
                height: 50
                visible: false  // Disable until button system is implemented
            }
        }
    }

    // Debug info
    Item {
        id: debugInfo
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 5
        width: 200
        height: 50

        Label {
            anchors.fill: parent
            font.pixelSize: 10
            color: Kirigami.Theme.disabledTextColor
            text: "Engine: " + (engine ? "Connected" : "Disconnected")
        }
    }
}
