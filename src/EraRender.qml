import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import io.yigekuoyou.eraengine 1.0
import org.kde.kirigami 2.19 as Kirigami

Item {
    id: eraRender
    anchors.fill: parent

    // 绑定外部传入的 EraEngine 实例
    property EraEngine engine: null
}
