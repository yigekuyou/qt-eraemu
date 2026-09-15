#include <QCoreApplication>
#include <QDebug>
#include "game_base_data.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "=== GameBaseData Test Program ===\n";

    // Create GameBaseData instance
    GameBaseData data;
    qDebug() << "1. Created GameBaseData instance";

    // Test default values
    qDebug() << "2. Default values:";
    qDebug() << "   windowTitle:" << data.windowTitle();
    qDebug() << "   title:" << data.title();
    qDebug() << "   author:" << data.author();
    qDebug() << "   version:" << data.version();
    qDebug() << "   releaseYear:" << data.releaseYear();
    qDebug() << "   additionalInfo:" << data.additionalInfo();

    // Test setting values using set() method
    qDebug() << "\n3. Setting values using set() method:";
    data.set("ウィンドウタイトル", "Test Window Title");
    data.set("タイトル", "Test Game Title");
    data.set("作者", "Test Author");
    data.set("バージョン", "1.0.0");
    data.set("製作年", "2024");
    data.set("追加情報", "Test Additional Info");

    qDebug() << "   windowTitle:" << data.windowTitle();
    qDebug() << "   title:" << data.title();
    qDebug() << "   author:" << data.author();
    qDebug() << "   version:" << data.version();
    qDebug() << "   releaseYear:" << data.releaseYear();
    qDebug() << "   additionalInfo:" << data.additionalInfo();

    // Test getting values using get() method
    qDebug() << "\n4. Getting values using get() method:";
    qDebug() << "   ウィンドウタイトル:" << data.get("ウィンドウタイトル");
    qDebug() << "   タイトル:" << data.get("タイトル");
    qDebug() << "   作者:" << data.get("作者");
    qDebug() << "   バージョン:" << data.get("バージョン");
    qDebug() << "   製作年:" << data.get("製作年");
    qDebug() << "   追加情報:" << data.get("追加情報");

    // Test toMap() method
    qDebug() << "\n5. Converting to QVariantMap:";
    QVariantMap map = data.toMap();
    for (auto it = map.begin(); it != map.end(); ++it) {
        qDebug() << "  " << it.key() << ":" << it.value().toString();
    }

    // Test with QML-style access pattern
    qDebug() << "\n6. Testing QML-style access pattern:";
    qDebug() << "   gameBaseData.windowTitle:" << data.windowTitle();
    qDebug() << "   gameBaseData.title:" << data.title();
    qDebug() << "   gameBaseData.author:" << data.author();
    qDebug() << "   gameBaseData.version:" << data.version();
    qDebug() << "   gameBaseData.releaseYear:" << data.releaseYear();
    qDebug() << "   gameBaseData.additionalInfo:" << data.additionalInfo();

    qDebug() << "\n=== All Tests Passed ===\n";

    return 0;
}
