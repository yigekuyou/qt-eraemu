import QtQuick
import io.yigekuoyou.eraengine

Item {
    id: eraRender

    // 声明一个属性用来关联外部的 eraEngine 实例
    property EraEngine engine: null

    // 用于暂存当前屏幕文本行的 ListModel
    ListModel {
        id: screenModel
    }

    // 用于保存历史记录的 ListModel
    ListModel {
        id: historyModel
    }

    Connections {
        target: eraRender.engine

        function onAppendLine(text, isHtml) {
            screenModel.append({
                "lineText": text
            });
            historyModel.append({
                "lineText": text
            });
        }

        function onClearScreen() {
            screenModel.clear();
        }
    }

    // 这里可以放置用于显示 screenModel 内容的视图组件，例如 ListView
    ListView {
        id: screenListView
        anchors.fill: parent
        model: screenModel
        delegate: Text {
            text: model.lineText
            color: "#ffffff"
        }
    }
}
