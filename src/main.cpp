#include <QQmlApplicationEngine>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QApplication>
#include <QQmlContext>
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
	QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
									 &app, []() { QCoreApplication::exit(-1); },
	Qt::QueuedConnection);
	engine.rootContext()->setContextProperty("currentDir", QDir::currentPath());
	engine.loadFromModule("io.yigekuoyou.appemuera", "Main");

	return app.exec();
}
