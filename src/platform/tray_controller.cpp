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
#include "tray_controller.h"

#include <QApplication>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>

TrayController::TrayController(QObject* parent) : QObject(parent) {
    // QML 侧通常不写 enabled（默认 true 就够用），托盘必须在构造期建好；
    // 之后 enabled=false 再经 setEnabled 拆除
    if (m_enabled)
        ensureIcon();
}

TrayController::~TrayController() {
    delete m_icon;
    delete m_menu;
}

bool TrayController::available() const {
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayController::setEnabled(bool v) {
    if (m_enabled == v)
        return;
    m_enabled = v;
    if (m_enabled)
        ensureIcon();
    else
        releaseIcon();
    emit enabledChanged();
}

void TrayController::setCloseToTray(bool v) {
    if (m_closeToTray == v)
        return;
    m_closeToTray = v;
    emit closeToTrayChanged();
}

void TrayController::setTooltip(const QString& t) {
    if (m_tooltip == t)
        return;
    m_tooltip = t;
    if (m_icon)
        m_icon->setToolTip(t);
    emit tooltipChanged();
}

void TrayController::ensureIcon() {
    if (m_icon || !available())
        return;

    // 图标：应用图标 -> 主题图标 -> 样式内置图标（都不行也要有兜底）
    QIcon icon = QApplication::windowIcon();
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("applications-games"));
    if (icon.isNull())
        icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);

    m_menu = new QMenu;
    m_menu->addAction(QObject::tr("显示主窗口"), this, &TrayController::showWindowRequested);
    m_menu->addAction(QObject::tr("隐藏主窗口"), this, &TrayController::hideWindowRequested);
    m_menu->addSeparator();
    QAction* actClose = m_menu->addAction(QObject::tr("关闭时隐藏到托盘"));
    actClose->setCheckable(true);
    actClose->setChecked(m_closeToTray);
    connect(actClose, &QAction::toggled, this, &TrayController::setCloseToTray);
    m_menu->addSeparator();
    m_menu->addAction(QObject::tr("退出"), this, &TrayController::quitRequested);

    m_icon = new QSystemTrayIcon(icon, this);
    m_icon->setContextMenu(m_menu);
    m_icon->setToolTip(m_tooltip);
    // 单击/双击托盘图标 -> 唤起主窗口（QSystemTrayIcon::activated）
    connect(m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit showWindowRequested();
    });
    m_icon->show();
}

void TrayController::releaseIcon() {
    if (m_icon) {
        m_icon->hide();
        delete m_icon;
        m_icon = nullptr;
    }
    delete m_menu;
    m_menu = nullptr;
}
