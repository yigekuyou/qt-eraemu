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
#ifndef WINDOW_CONTROLLER_H
#define WINDOW_CONTROLLER_H

#include <QObject>
#include <QQuickWindow>
#include <QQmlEngine>
#include <QString>

#include "../eraengine/GameView/gui_manager.h"

// ---------------------------------------------------------------------------
// WindowController —— 窗口几何与窗口状态的 C++ 直控端
//
// QML 的 Window 属性绑定（width/height/visibility）一多，外部（设置/config）
// 想改窗口就要「改 C++ 状态 -> 通知 QML -> QML 绑定回写窗口」，链路绕且容易
// 和用户手动拖拽打架。这里改为 C++ 直控：
//   * applyWindowSize()：读取 GuiManager 的 windowWidth/Height，按当前屏幕
//     availableGeometry 钳制后直接 setWidth/setHeight（Qt 文档 QWindow：
//     width/height 是窗口几何，直接改即触发窗口管理器 resize）；
//   * setWindowMode("windowed"|"fullscreen"|"borderless")：窗口化 / 全屏 /
//     无边框全屏。无边框全屏 = Qt::FramelessWindowHint + 覆盖整屏几何
//     （Wayland 客户端不允许自定位窗口，退化为合成器全屏 showFullScreen，
//     在 Wayland 上本来就是无边框的）；
//   * GuiManager::settingsChanged -> 自动 applyWindowSize（改设置/字号时
//     窗口直接由这里改变大小，不再经过 QML 绑定）；
//   * sizableWindow=false 时锁死最小=最大，窗口不可拉伸（对齐 C# 的
//     「サイズ変更不可」配置）。
//
// Linux 上同时把 raiseWindow() 导出为 D-Bus 接口 io.yigekuyou.emuera
// （/emuera），单实例的第二次启动可以唤起已有窗口（见 main.cpp）。
// ---------------------------------------------------------------------------
class WindowController : public QObject {
    Q_OBJECT
    QML_ELEMENT
#ifdef Q_OS_LINUX
    Q_CLASSINFO("D-Bus Interface", "io.yigekuyou.emuera")
#endif

    // 窗口由 QML 侧注入（Main.qml 根 ApplicationWindow）
    Q_PROPERTY(QQuickWindow* window READ window WRITE setWindow NOTIFY windowChanged FINAL)
    Q_PROPERTY(GuiManager* gui READ gui WRITE setGui NOTIFY guiChanged FINAL)
    // "windowed" | "fullscreen" | "borderless"
    Q_PROPERTY(QString windowMode READ windowMode WRITE setWindowMode NOTIFY windowModeChanged FINAL)

public:
    explicit WindowController(QObject* parent = nullptr);
    ~WindowController() override;

    [[nodiscard]] QQuickWindow* window() const { return m_window; }
    void setWindow(QQuickWindow* w);
    [[nodiscard]] GuiManager* gui() const { return m_gui; }
    void setGui(GuiManager* g);
    [[nodiscard]] QString windowMode() const { return m_windowMode; }
    void setWindowMode(const QString& mode);

    // 按 GuiManager 的设置直接改变窗口大小（钳制在当前屏幕内）
    Q_INVOKABLE void applyWindowSize();
    // 全屏 <-> 窗口化（F11）
    Q_INVOKABLE void toggleFullscreen();

    // 本会话是否提供原生/全局菜单栏宿主：macOS 总是 true；Linux 探测
    // com.canonical.AppMenu.Registrar（Plasma/Unity 的 D-Bus 全局菜单服务，
    // 与 Qt 内部 QDBusMenuBar 使用同一服务名）；其它平台 false。
    // Main.qml 据此决定创建 Qt.labs.platform 原生菜单栏还是窗口内 MenuBar。
    Q_INVOKABLE static bool hasGlobalMenuBar();

public Q_SLOTS:
    // D-Bus（Linux 单实例）：唤起已有主窗口
    void raiseWindow();

Q_SIGNALS:
    void windowChanged();
    void guiChanged();
    void windowModeChanged();

private:
    void applyWindowState();
    void clearFramelessHint();

    QQuickWindow* m_window = nullptr;
    GuiManager*   m_gui = nullptr;
    QString       m_windowMode = QStringLiteral("windowed");
    // 只跟随设置里的「最大化」开关：用户手动还原后不会因设置刷新被强行回弹
    bool m_appliedMaximized = false;
    // 上次实际应用的窗口尺寸（Wayland 上 QWindow::width 是「请求值」，
    // 与组合器的实际表面可以不一致 —— 判断是否需要 hide/show 重映射时
    // 只能信自己记录的值）
    QSize m_appliedSize;
};

#endif // WINDOW_CONTROLLER_H
