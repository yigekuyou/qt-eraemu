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
#ifndef TRAY_CONTROLLER_H
#define TRAY_CONTROLLER_H

#include <QObject>
#include <QQmlEngine>
#include <QString>

class QMenu;
class QSystemTrayIcon;

// ---------------------------------------------------------------------------
// TrayController —— 后台托盘（QSystemTrayIcon 的 QML 封装）
//
// Qt 文档 QSystemTrayIcon：托盘图标属于 Widgets 模块（应用已链接 Qt6::Widgets，
// main.cpp 用 QApplication，两个前提都满足）。
//   * available：本平台是否有系统托盘（桌面环境提供托盘才有意义；
//     Android/iOS 没有，Main.qml 据此决定是否创建/显示相关入口）；
//   * enabled：是否启用托盘（false 时隐藏并释放托盘图标）；
//   * closeToTray：勾选后「关闭窗口 = 隐藏到托盘」，Main.qml 的 onClosing
//     据此吞掉 close 事件改 hide()，应用退到后台常驻（托盘菜单可再唤起）；
//   * 信号全部转发给 QML：QML 拥有窗口，show/hide/退出都由窗口侧执行。
// 托盘菜单在 C++ 里构建（QMenu + QAction，原生菜单）。
// ---------------------------------------------------------------------------
class TrayController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY closeToTrayChanged FINAL)
    Q_PROPERTY(QString tooltip READ tooltip WRITE setTooltip NOTIFY tooltipChanged FINAL)

public:
    explicit TrayController(QObject* parent = nullptr);
    ~TrayController() override;

    [[nodiscard]] bool available() const;
    [[nodiscard]] bool enabled() const { return m_enabled; }
    void setEnabled(bool v);
    [[nodiscard]] bool closeToTray() const { return m_closeToTray; }
    void setCloseToTray(bool v);
    [[nodiscard]] QString tooltip() const { return m_tooltip; }
    void setTooltip(const QString& t);

Q_SIGNALS:
    void enabledChanged();
    void closeToTrayChanged();
    void tooltipChanged();
    void showWindowRequested();
    void hideWindowRequested();
    void quitRequested();

private:
    void ensureIcon();
    void releaseIcon();

    QSystemTrayIcon* m_icon = nullptr;
    QMenu* m_menu = nullptr;
    bool    m_enabled = true;
    bool    m_closeToTray = false;
    QString m_tooltip;
};

#endif // TRAY_CONTROLLER_H
