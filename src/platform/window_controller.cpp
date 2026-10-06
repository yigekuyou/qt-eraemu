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
#include "window_controller.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QGuiApplication>
#include <QScreen>

namespace {
constexpr int kWindowSizeMax = 0x00FFFFFF;   // QWINDOWSIZE_MAX（qwindow_p.h 未导出）
constexpr const char* kModeWindowed   = "windowed";
constexpr const char* kModeFullscreen = "fullscreen";
constexpr const char* kModeBorderless = "borderless";
}  // namespace

WindowController::WindowController(QObject* parent) : QObject(parent) {}

WindowController::~WindowController() {
#ifdef Q_OS_LINUX
    if (m_window)
        QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/emuera"));
#endif
}

void WindowController::setWindow(QQuickWindow* w) {
    if (m_window == w)
        return;
    m_window = w;
    if (m_window) {
#ifdef Q_OS_LINUX
        // 单实例服务（main.cpp 已注册 io.yigekuyou.emuera）：把唤起窗口
        // 导出为 D-Bus 方法，第二次启动的程序调 raiseWindow() 把窗口拉回前台
        if (!QDBusConnection::sessionBus().registerObject(
                QStringLiteral("/emuera"), this, QDBusConnection::ExportAllSlots))
            qWarning("WindowController: 注册 D-Bus 对象 /emuera 失败");
#endif
        // 跨屏 / 分辨率变化后重新按新屏幕钳制窗口
        connect(m_window, &QQuickWindow::screenChanged, this, [this](QScreen*) { applyWindowSize(); });
        // 装配完成立刻由 C++ 定尺寸，避免 QML 默认尺寸闪一帧
        applyWindowSize();
    }
    emit windowChanged();
}

void WindowController::setGui(GuiManager* g) {
    if (m_gui == g)
        return;
    if (m_gui)
        disconnect(m_gui, &GuiManager::settingsChanged, this, &WindowController::applyWindowSize);
    m_gui = g;
    if (m_gui) {
        // 设置/config 变化 -> C++ 直接改变窗口大小（QML 侧不再绑 width/height）
        connect(m_gui, &GuiManager::settingsChanged, this, &WindowController::applyWindowSize);
        applyWindowSize();
    }
    emit guiChanged();
}

void WindowController::setWindowMode(const QString& mode) {
    const QString m = mode.toLower();
    if (m != QLatin1String(kModeWindowed) && m != QLatin1String(kModeFullscreen) &&
        m != QLatin1String(kModeBorderless)) {
        qWarning("WindowController: 未知窗口模式 \"%s\"", qPrintable(mode));
        return;
    }
    if (m == m_windowMode)
        return;
    m_windowMode = m;
    applyWindowState();
    emit windowModeChanged();
}

void WindowController::toggleFullscreen() {
    setWindowMode(m_windowMode == QLatin1String(kModeFullscreen)
                      ? QLatin1String(kModeWindowed)
                      : QLatin1String(kModeFullscreen));
}

bool WindowController::hasGlobalMenuBar() {
#if defined(Q_OS_MACOS)
    return true;   // macOS 系统菜单栏总是可用
#elif defined(Q_OS_LINUX)
    // 手动回退开关：EMUERA_INLINE_MENU=1 强制用窗口内菜单栏（不喜欢全局
    // 菜单、或桌面环境的全局菜单宿主不完整时用）
    if (qEnvironmentVariableIsSet("EMUERA_INLINE_MENU"))
        return false;
    // Qt 文档 Qt.labs.platform：Linux 上原生菜单栏只在桌面环境提供系统
    // D-Bus 菜单栏时可用；没有宿主时 labs MenuBar 什么都不显示，会「吃掉」
    // 菜单。探测宿主服务（与 Qt 内部 QDBusMenuBar 相同的服务名），
    // 有 Plasma/Unity 的全局菜单宿主才走 labs。
    return QDBusConnection::sessionBus().interface()->isServiceRegistered(
        QStringLiteral("com.canonical.AppMenu.Registrar"));
#else
    return false;
#endif
}

void WindowController::applyWindowSize() {
    if (!m_window || !m_gui)
        return;
    if (m_windowMode != QLatin1String(kModeWindowed))
        return;   // 全屏/无边框全屏时几何由窗口模式接管

    // 舞台（stage）= GuiManager 的配置尺寸（emuera.config 推导），QML 会把它
    // 等比缩放到窗口内。这里把**窗口**初始化为「舞台等比缩到屏幕内」的结果：
    // 通常 = 舞台 1:1（无信箱）；舞台比屏幕大时窗口更小，画面由 QML 缩小呈现。
    // Qt 文档 QScreen::availableGeometry 已扣除任务栏/系统菜单。
    QScreen* screen = m_window->screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect();

    constexpr int kMinW = 320;
    constexpr int kMinH = 240;
    const double stageW = qMax(1, m_gui->windowWidth());
    const double stageH = qMax(1, m_gui->windowHeight());
    double k = 1.0;
    if (avail.isValid())
        k = qMin(1.0, qMin(avail.width() / stageW, avail.height() / stageH));

    int w = qMax(kMinW, static_cast<int>(stageW * k + 0.5));
    int h = qMax(kMinH, static_cast<int>(stageH * k + 0.5));
    if (avail.isValid()) {
        w = qMin(w, avail.width());
        h = qMin(h, avail.height());
    }

    m_window->setMinimumSize(QSize(kMinW, kMinH));
    m_window->setMaximumSize(m_gui->sizableWindow()
                                 ? QSize(kWindowSizeMax, kWindowSizeMax)
                                 : QSize(w, h));   // 尺寸固定：min=max 锁死
    // C++ 直控：直接改变窗口大小
    m_window->setWidth(w);
    m_window->setHeight(h);

    // Wayland 规避（xdg-toplevel 没有客户端强制几何的协议）：对已显示的
    // 窗口 setWidth/setHeight 只是「请求」，组合器不改变实际渲染表面，Qt
    // 却乐观地更新内部几何 —— 表现为「QML 按新尺寸排版、表面还是旧尺寸」
    // → 舞台被窗口边缘裁掉（config 1400x750、窗口仍停在初始 760x480 时，
    // 居中的立絵右半会被裁）。可靠的办法：撤下窗口再以新尺寸重新映射，
    // 重新 map 的初始提交尺寸组合器会接受。X11 直接 resize 即可，不走这里。
    const QSize requested(w, h);
    if (QGuiApplication::platformName() == QLatin1String("wayland")
            && m_window->isVisible() && m_appliedSize != requested) {
        m_window->hide();
        m_window->resize(w, h);
        m_window->show();
    }
    m_appliedSize = requested;
    qDebug() << "[window] applyWindowSize" << requested << "platform"
             << QGuiApplication::platformName() << "visible" << m_window->isVisible()
             << "qtGeom" << m_window->width() << "x" << m_window->height();

    // 最大化只跟随设置开关（用户手动还原后不回弹）
    const bool wantMax = m_gui->maximized();
    if (wantMax && !m_appliedMaximized)
        m_window->showMaximized();
    else if (!wantMax && m_appliedMaximized)
        m_window->showNormal();
    m_appliedMaximized = wantMax;
}

void WindowController::applyWindowState() {
    if (!m_window)
        return;
    if (m_windowMode == QLatin1String(kModeFullscreen)) {
        clearFramelessHint();
        m_window->showFullScreen();
        return;
    }
    if (m_windowMode == QLatin1String(kModeBorderless)) {
        // 无边框全屏：去掉窗口装饰并铺满整块屏幕（含任务栏区域）。
        // Wayland 客户端不允许自己定位窗口（无全局坐标），退化成
        // showFullScreen——Wayland 上的全屏本来就无边框。
        if (QGuiApplication::platformName() == QLatin1String("wayland")) {
            clearFramelessHint();
            m_window->showFullScreen();
        } else {
            const bool wasVisible = m_window->isVisible();
            if (wasVisible)
                m_window->hide();   // 改 flags 需要先撤下窗口
            m_window->setFlags(m_window->flags() | Qt::FramelessWindowHint);
            if (QScreen* screen = m_window->screen())
                m_window->setGeometry(screen->geometry());
            m_window->show();
        }
        return;
    }
    // 窗口化
    clearFramelessHint();
    applyWindowSize();
    if (!m_window->isVisible())
        m_window->show();
}

void WindowController::clearFramelessHint() {
    if (!(m_window->flags() & Qt::FramelessWindowHint))
        return;
    const bool wasVisible = m_window->isVisible();
    if (wasVisible)
        m_window->hide();
    m_window->setFlags(m_window->flags() & ~Qt::FramelessWindowHint);
    if (wasVisible)
        m_window->show();
}

void WindowController::raiseWindow() {
    if (!m_window)
        return;
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}
