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
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QApplication>
#include <QQmlContext>
#include "GameView/resource_image_provider.h"
int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	#ifdef Q_OS_LINUX
	app.setApplicationName("emuera");
	const QString serviceName = "io.yigekuyou.emuera";
	if (!QDBusConnection::sessionBus().registerService(serviceName)) {
		//注册失败自动触发
		qWarning() << "SingleApp is running!" << __FUNCTION__;
		return 0;
	}
	#endif
	QQmlApplicationEngine engine;
	// 资源图（C# ConstImage 的「资源名 -> 图片」）：QML 侧用 Image { source: "image://emuera/<name>" }
	engine.addImageProvider(QStringLiteral("emuera"), new ResourceImageProvider);
	QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
									 &app, []() { QCoreApplication::exit(-1); },
	Qt::QueuedConnection);
	engine.rootContext()->setContextProperty("currentDir", QDir::currentPath());
	engine.loadFromModule("io.yigekuoyou.appemuera", "Main");

	return app.exec();
}
