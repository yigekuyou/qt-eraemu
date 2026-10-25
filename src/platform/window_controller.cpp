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

#if defined(Q_OS_LINUX) && defined(EMUERA_HAS_DBUS)
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#endif
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

namespace {
constexpr int kWindowSizeMax = 0x00FFFFFF;   // QWINDOWSIZE_MAX（qwindow_p.h 未导出）
constexpr const char* kModeWindowed   = "windowed";
constexpr const char* kModeFullscreen = "fullscreen";
constexpr const char* kModeBorderless = "borderless";

// Qt.labs.platform 的原生菜单栏只在**真实桌面合成器**平台上有实现：
// xcb（X11/XWayland）与 wayland 会创建 QPlatformMenuBar；offscreen /
// minimal / vnc / eglfs / linuxfb 没有原生菜单接口。旧实现只看 D-Bus
// 宿主是否注册，于是 `QT_QPA_PLATFORM=offscreen` 也会走进 labs 的
// createObject()，在 setInitialProperty 里解引用空平台菜单栏 → SIGSEGV。
// 判据集中在这一个函数里（平台能力），D-Bus 只决定「有宿主时用不用」。
bool platformSupportsNativeMenuBar() {
    const QString p = QGuiApplication::platformName();
    return p == QLatin1String("xcb") || p == QLatin1String("wayland");
}
}  // namespace

WindowController::WindowController(QObject* parent) : QObject(parent) {}

WindowController::~WindowController() {
#if defined(Q_OS_LINUX) && defined(EMUERA_HAS_DBUS)
    if (m_window)
        QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/emuera"));
#endif
}

void WindowController::setWindow(QQuickWindow* w) {
    if (m_window == w)
        return;
    m_window = w;
    if (m_window) {
#if defined(Q_OS_LINUX) && defined(EMUERA_HAS_DBUS)
        // 单实例服务（main.cpp 已注册 io.yigekuyou.emuera）：把唤起窗口
        // 导出为 D-Bus 方法，第二次启动的程序调 raiseWindow() 把窗口拉回前台
        if (!QDBusConnection::sessionBus().registerObject(
                QStringLiteral("/emuera"), this, QDBusConnection::ExportAllSlots))
            qWarning("WindowController: 注册 D-Bus 对象 /emuera 失败");
#endif
        // 跨屏 / 分辨率变化后重新按新屏幕钳制窗口
        connect(m_window, &QQuickWindow::screenChanged, this, [this](QScreen*) {
            applyWindowSize();
            emit screenRefreshRateChanged();   // 刷新率随屏幕变化（QScreen::refreshRate）
        });
        // 尺寸唯一来源在 applyWindowSize()。这里记录每一次几何变化，启动后
        // 的事件循环第 0 拍与 300 ms 再核对一次（窗口管理器可能在 map 时
        // 应用「记住的几何/最大化」规则，如 KWin 的窗口规则；Wayland 的
        // 初始尺寸也由合成器决定）。对账失败只在窗口化模式下纠正一次。
        connect(m_window, &QQuickWindow::widthChanged, this, [this](int w) {
            qDebug() << "[window] widthChanged" << w << "visible" << m_window->isVisible()
                     << "state" << int(m_window->windowStates());
        });
        connect(m_window, &QQuickWindow::heightChanged, this, [this](int h) {
            qDebug() << "[window] heightChanged" << h << "visible" << m_window->isVisible()
                     << "state" << int(m_window->windowStates());
        });
        connect(m_window, &QQuickWindow::visibleChanged, this, [this](bool visible) {
            if (!visible || m_initialGeometryChecked)
                return;
            QTimer::singleShot(0, this, [this] { verifyWindowSize(); });
            QTimer::singleShot(300, this, [this] {
                verifyWindowSize();
                m_initialGeometryChecked = true;
            });
        });
        // 装配完成立刻由 C++ 定尺寸，避免 QML 默认尺寸闪一帧
        applyWindowSize();
    }
    emit windowChanged();
    emit screenRefreshRateChanged();
}

// 当前屏幕刷新率（Hz）：Qt 文档 QScreen::refreshRate()，未知返回 0。
// QML 的 Screen 附着类型没有这个属性（见 window_controller.h 的说明）。
int WindowController::screenRefreshRate() const {
    if (!m_window || !m_window->screen())
        return 0;
    return qRound(m_window->screen()->refreshRate());
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
#elif defined(Q_OS_LINUX) && defined(EMUERA_HAS_DBUS)
    // 手动回退开关：EMUERA_INLINE_MENU=1 强制用窗口内菜单栏（不喜欢全局
    // 菜单、或桌面环境的全局菜单宿主不完整时用）
    if (qEnvironmentVariableIsSet("EMUERA_INLINE_MENU"))
        return false;
    // ① 平台必须真的提供原生菜单栏接口。offscreen/minimal/vnc 等 QPA
    //    插件没有 QPlatformMenuBar，创建 Qt.labs.platform MenuBar 会 SIGSEGV
    //    （setInitialProperty → QQuickLabsPlatformMenuItem）。这是能力判定。
    if (!platformSupportsNativeMenuBar())
        return false;
    // ② Linux 上还要有全局菜单宿主（Qt 文档 Qt.labs.platform）。没有宿主时
    //    labs MenuBar 什么都不显示、会「吃掉」菜单，退到窗口内菜单栏。
    QDBusConnectionInterface* bus = QDBusConnection::sessionBus().interface();
    if (!bus)
        return false;
    return bus->isServiceRegistered(QStringLiteral("com.canonical.AppMenu.Registrar"));
#else
    return false;
#endif
}

QSize WindowController::targetWindowSize() const {
    // 窗口尺寸 = GuiManager 的配置尺寸（emuera.config 的ウィンドウ幅/高さ），
    // **全程不做屏幕钳制**：原版 Emuera 也允许窗口比屏幕大（Win32 无限制），
    // 而任何「本进程缩小窗口」都会让 gui.windowWidth ≠ 实际 QWindow，
    // 把配置和画面对不上。屏幕装不下是用户/窗口管理器的选择，不是本进程的。
    // 只有下限保护（320x240，防止 0 尺寸窗口）。
    constexpr int kMinW = 320;
    constexpr int kMinH = 240;
    const int w = qMax(kMinW, m_gui ? m_gui->windowWidth() : 760);
    const int h = qMax(kMinH, m_gui ? m_gui->windowHeight() : 640);
    return QSize(w, h);
}

void WindowController::applyWindowSize() {
    if (!m_window || !m_gui)
        return;
    if (m_windowMode != QLatin1String(kModeWindowed))
        return;   // 全屏/无边框全屏时几何由窗口模式接管

    constexpr int kMinW = 320;
    constexpr int kMinH = 240;
    const QSize target = targetWindowSize();
    const int w = target.width();
    const int h = target.height();

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

void WindowController::verifyWindowSize() {
    if (m_initialGeometryChecked || !m_window || !m_gui)
        return;
    if (m_windowMode != QLatin1String(kModeWindowed) || !m_window->isVisible())
        return;
    // 配置要求最大化时，实际几何本来就该由窗口管理器决定，不做对账。
    if (m_gui->maximized())
        return;

    const QSize target = targetWindowSize();
    const QSize actual(m_window->width(), m_window->height());
    const bool maximized = (m_window->windowStates() & Qt::WindowMaximized) != 0;
    if (actual == target && !maximized)
        return;

    // 典型来源：KWin 等窗口管理器把「上次关闭时的几何/最大化」当作新窗口的
    // 初始状态；Wayland 合成器也可能改写初始 toplevel 尺寸。本进程不再有
    // 第二处 setWidth/setHeight（QML 不绑定 width/height），所以这里纠正后
    // 若仍不一致，就是平台/合成器的行为。
    qWarning() << "[window] 启动几何被窗口管理器改写，纠正一次: actual" << actual
               << "target" << target << "maximized" << maximized
               << "platform" << QGuiApplication::platformName();
    if (maximized)
        m_window->showNormal();
    applyWindowSize();

    qDebug() << "[window] 纠正后" << QSize(m_window->width(), m_window->height())
             << "payload target" << target;
}

void WindowController::raiseWindow() {
    if (!m_window)
        return;
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}
