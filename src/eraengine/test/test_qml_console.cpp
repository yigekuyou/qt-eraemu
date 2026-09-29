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
#include <QObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest/QTest>
#include "console_backend.h"

// 测试夹具：把不可创建的 ConsoleBackend 交给 QML 使用
class ConsoleFixture : public QObject {
    Q_OBJECT
public:
    explicit ConsoleFixture(QObject* parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void pressLeft(QObject* object) {
        auto* item = qobject_cast<QQuickItem*>(object);
        QTest::keyClick(item->window(), Qt::Key_Left);
    }
    Q_INVOKABLE void request(QObject* object, const QString& kind) {
        static_cast<ConsoleBackend*>(object)->notifyInputRequested(kind);
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
        // 用 context property 提供夹具与路径（QuickTest 会对 import 做静态检查，
        // 因此不引入自定义模块，避免 "module not installed"）。
        engine->rootContext()->setContextProperty("consoleFixture", new ConsoleFixture(engine));
        // Console.qml 的绝对路径（ConsoleLine.qml 与它同目录，相对导入可用）
        engine->rootContext()->setContextProperty(
            "consoleQmlPath", QStringLiteral(CONSOLE_QML_PATH));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(tst_qml_console, QmlConsoleSetup)

#include "test_qml_console.moc"
