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
#include <QQmlApplicationEngine>
// 单实例唤起 / 全局菜单探针用 D-Bus；平台 Qt 未提供 QtDBus 时（如 Android）
// 整块编译掉，程序照常运行（多开不再互斥）。
#if defined(Q_OS_LINUX) && defined(EMUERA_HAS_DBUS)
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#endif
#include <QApplication>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QQuickStyle>
#include "GameView/resource_image_provider.h"
#include "Config/i18n.h"

// ---------------------------------------------------------------------------
// 分平台 Quick Controls 样式（QQuickStyle::setStyle 必须在加载 QML 前调用）：
//   * Linux：KDE/Kirigami 桌面风格 —— qqc2-desktop-style 的 org.kde.desktop
//     （基于 Kirigami Theme/PlatformTheme，跟随系统配色；api.kde.org/kirigami）。
//     Qt 6.11 没有 QQuickStyle::availableStyles()，按 QML import path 探测
//     样式模块是否存在，缺失（纯 Qt 环境）时回退 Fusion；
//   * macOS：macos 系统风格；
//   * Windows：不设置 —— Qt 默认即 windows 风格；
//   * Android：Material（Google 触屏规范，跟随系统深浅色，触控目标更大）。
// ---------------------------------------------------------------------------
static void applyPlatformStyle()
{
#if defined(Q_OS_ANDROID)
    QQuickStyle::setStyle(QStringLiteral("Material"));
#elif defined(Q_OS_MACOS)
    QQuickStyle::setStyle(QStringLiteral("macos"));
#elif defined(Q_OS_LINUX)
    const QString qmlRoot = QLibraryInfo::path(QLibraryInfo::QmlImportsPath);
    // QQC2 样式两种落点：QtQuick/Controls/<style>（打包进 Qt）或模块路径
    // <style>（发行版单独安装，如 qqc2-desktop-style -> org/kde/desktop）
    const QStringList styleIds = { QStringLiteral("org.kde.kirigami"), QStringLiteral("org.kde.desktop") };
    for (const QString& id : styleIds) {
        if (QFileInfo::exists(qmlRoot + QStringLiteral("/QtQuick/Controls/") + id + QStringLiteral("/qmldir")) ||
            QFileInfo::exists(qmlRoot + QLatin1Char('/') + id + QStringLiteral("/qmldir"))) {
            QQuickStyle::setStyle(id);
            break;
        }
    }
    // 样式缺组件时用 Fusion 兜底（org.kde.desktop 只覆盖常用控件）
    QQuickStyle::setFallbackStyle(QStringLiteral("Fusion"));
#endif
}

int main(int argc, char *argv[])
{
	applyPlatformStyle();
	QApplication app(argc, argv);
	// 尽早安装翻译器：QML 装载前装好，界面与装载期告警才会命中 .qm。
	// 未提供 .qm 时无副作用（源文本即中文）。
	eraengine::installTranslators();
	#ifdef Q_OS_LINUX
	app.setApplicationName("emuera");
	#endif
	#if defined(Q_OS_LINUX) && defined(EMUERA_HAS_DBUS)
	const QString serviceName = "io.yigekuyou.emuera";
	if (!QDBusConnection::sessionBus().registerService(serviceName)) {
		//注册失败=已有实例在跑：唤起它的窗口（D-Bus /emuera raiseWindow）后退出
		qWarning() << "SingleApp is running!" << __FUNCTION__;
		QDBusInterface existing(serviceName, "/emuera", "io.yigekuyou.emuera",
								QDBusConnection::sessionBus());
		if (existing.isValid())
			existing.asyncCall("raiseWindow");
		return 0;
	}
	#endif
	QQmlApplicationEngine engine;
	// 资源图（C# ConstImage 的「资源名 -> 图片」）：QML 侧用 Image { source: "image://emuera/<name>" }
	engine.addImageProvider(QStringLiteral("emuera"), new ResourceImageProvider);
	QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
									 &app, []() { QCoreApplication::exit(-1); },
	Qt::QueuedConnection);
	// 不再往根上下文塞 context property（旧代码注入过 currentDir，QML 侧从未使用）：
	// Qt 文档《QML and C++ Integration》明确「Context properties should be avoided」——
	// 它们不可被静态检查（qmllint 的 ContextProperties / unqualified access 都报它们），
	// 也绕开了类型系统。要暴露给 QML 的东西应走单例类型或 required property。
	engine.loadFromModule("io.yigekuoyou.appemuera", "Main");

	return app.exec();
}
