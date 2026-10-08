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
// ---------------------------------------------------------------------------
// test_qml_console.cpp —— Qt Quick Test 入口（QML 侧测试）
//
// 只测“接线与呈现”这类 QML 责任：
//   * Console.qml 的可见窗口重建（createObject/复用）数量是否正确
//   * ConsoleLine.qml 的按钮 span 命中后是否转发到 ConsoleBackend::clickAt
//
// 纯逻辑（缓冲/窗口/滚动/generation）已在 test_console_backend 用 C++ 覆盖。
//
// 运行：QT_QPA_PLATFORM=offscreen ./test_qml_console -input <qml 目录>
// ---------------------------------------------------------------------------

#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <QQmlContext>
#include <QMessageLogContext>
#include <QObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStringList>
#include <QtTest/QTest>
#include "console_backend.h"
#include "resource_image_provider.h"

// QML 的绑定成环（"Binding loop detected for property …"）只在运行期以 qWarning
// 形式出现，QML 侧看不见也断言不了 —— 挂了这两个函数的绑定的取值可能仍然是
// 「对的」，所以没有告警捕获就无法给它写回归测试。
// 这里串一个消息处理器把它收集起来，供 QML 测试取用；其余消息原样转发给
// QuickTest 自己的处理器（否则测试输出会被吃掉）。
namespace {
QStringList g_bindingLoops;
QtMessageHandler g_previousHandler = nullptr;

void captureBindingLoopWarnings(QtMsgType type, const QMessageLogContext& context,
                                const QString& message);

// 接管消息处理器，并把当时生效的（框架的）处理器存起来用于转发 ——
// 不转发的话用例的普通输出/告警就被吃掉了。
// 可以重复调用：QTest/QuickTest 之后会把自己的处理器装回去，把这次顶掉，
// 所以每次开启捕获都要重新接管一次。重复接管拿到的「前一个」可能是我们自己，
// 那种情况下不能把它链给对方，否则消息会在两个处理器之间来回递归。
void installCapture() {
    const QtMessageHandler previous = qInstallMessageHandler(captureBindingLoopWarnings);
    if (previous != captureBindingLoopWarnings)
        g_previousHandler = previous;
}

void captureBindingLoopWarnings(QtMsgType type, const QMessageLogContext& context,
                                const QString& message) {
    if (type == QtWarningMsg && message.contains(QStringLiteral("Binding loop detected")))
        g_bindingLoops.append(message);
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}
}  // namespace

// 测试夹具：把不可创建的 ConsoleBackend 交给 QML 使用
class ConsoleFixture : public QObject {
    Q_OBJECT
public:
    explicit ConsoleFixture(QObject* parent = nullptr) : QObject(parent) {}

    // 本次（清空后）收集到的绑定成环告警。
    // 注意安装时机：QuickTest/QTest 会在用例执行期间自己反复接管消息处理器，
    // 在 qmlEngineAvailable（引擎创建时）装的会被覆盖掉 —— 实测收不到任何消息；
    // 所以改由用例显式开启（发生在用例函数体内，晚于框架的最后一次接管）。
    Q_INVOKABLE QStringList bindingLoopWarnings() const { return g_bindingLoops; }
    Q_INVOKABLE void clearBindingLoopWarnings() {
        installCapture();
        g_bindingLoops.clear();
    }
    Q_INVOKABLE void pressLeft(QObject* object) {
        auto* item = qobject_cast<QQuickItem*>(object);
        QTest::keyClick(item->window(), Qt::Key_Left);
    }
    Q_INVOKABLE void request(QObject* object, const QString& kind) {
        static_cast<ConsoleBackend*>(object)->notifyInputRequested(kind);
    }
    Q_INVOKABLE void finishInput(QObject* object) {
        static_cast<ConsoleBackend*>(object)->notifyInputDone();
    }
    Q_INVOKABLE QObject* create(QObject* parent = nullptr) {
        return new ConsoleBackend(parent);
    }
};

class QmlConsoleSetup : public QObject {
    Q_OBJECT
public:
    explicit QmlConsoleSetup(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    // QuickTest 会在引擎就绪时调用该槽（必须是实例槽，而非静态函数）
    void qmlEngineAvailable(QQmlEngine* engine) {
        // 引擎就绪、用例尚未执行：此刻接管消息处理器，才能收到用例期间的告警。
        installCapture();
        // 用 context property 提供夹具与路径（QuickTest 会对 import 做静态检查，
        // 因此不引入自定义模块，避免 "module not installed"）。
        engine->rootContext()->setContextProperty("consoleFixture", new ConsoleFixture(engine));
        // Console.qml 的绝对路径（ConsoleLine.qml 与它同目录，相对导入可用）
        engine->rootContext()->setContextProperty(
            "consoleQmlPath", QStringLiteral(CONSOLE_QML_PATH));
        // 图片 provider：让 ConsoleBlock 的 `Image { source: "image://emuera/…" }`
        // 真的走一遍解码并画出像素（否则只有 AltText 回退，测不到“渲染”）。
        // 根目录指向 test/example/resources（里面有 offset_atlas.png 等夹具）。
        ResourceImageProvider::setRoot(QStringLiteral(EXAMPLE_RESOURCES_DIR));
        engine->addImageProvider(QStringLiteral("emuera"), new ResourceImageProvider);
    }
};

QUICK_TEST_MAIN_WITH_SETUP(tst_qml_console, QmlConsoleSetup)

#include "test_qml_console.moc"
