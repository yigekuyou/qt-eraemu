#include "eraengine.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QCoreApplication>
#include <QVariantMap>

EraEngine::EraEngine()
		: m_isInitialized(false), m_currentLayout(0)
{
		// 默认将游戏根目录设为应用程序运行目录下的 game 文件夹，或通过环境变量指定
		m_exeDir = QCoreApplication::applicationDirPath() + "/";
		m_csvDir = m_exeDir + "csv/";
		m_erbDir = m_exeDir + "erb/";
		m_configPath = m_exeDir + "emuera.config";
}

void EraEngine::setGameDirectory(const QString &path)
{
		if (path.isEmpty()) return;
		m_exeDir = path.endsWith("/") ? path : path + "/";
		m_csvDir = m_exeDir + "csv/";
		m_erbDir = m_exeDir + "erb/";
		m_configPath = m_exeDir + "emuera.config";

		qDebug() << "Game directory set to:" << m_exeDir;
		initializeEngine();
}

bool EraEngine::initializeEngine()
{
		// 1. 检查基础目录是否存在
		if (!QDir(m_csvDir).exists() || !QDir(m_erbDir).exists()) {
				qWarning() << "CSV or ERB directory missing in:" << m_exeDir;
				emit renderText("错误: 未找到 csv 或 erb 文件夹，请检查游戏路径。", true, false);
				return false;
		}

		// 2. 加载配置文件与基础数据 (对应 C# ConfigData & GameBase.csv)
		loadConfiguration();
		loadGameBaseCsv();

		m_isInitialized = true;
		emit clearScreen();
		emit renderText("EraEngine C++ 核心初始化成功。", true, false);

		return true;
}

void EraEngine::loadConfiguration()
{
		QFile file(m_configPath);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
				qDebug() << "Config file not found, using default settings.";
				return;
		}

		QTextStream in(&file);
		while (!in.atEnd()) {
				QString line = in.readLine().trimmed();
				if (line.isEmpty() || line.startsWith(";")) continue;
				// 解析配置键值对
				int idx = line.indexOf('=');
				if (idx != -1) {
						QString key = line.left(idx).trimmed();
						QString val = line.mid(idx + 1).trimmed();
						m_configMap[key] = val;
				}
		}
		file.close();
}

void EraEngine::loadGameBaseCsv()
{
		QString gameBaseFile = m_csvDir + "GameBase.csv";
		QFile file(gameBaseFile);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
				qDebug() << "GameBase.csv not found.";
				return;
		}

		QTextStream in(&file);
		while (!in.atEnd()) {
				QString line = in.readLine().trimmed();
				if (line.isEmpty() || line.startsWith(";")) continue;
				// 解析 GameBase 定义
				QStringList parts = line.split(',');
				if (parts.size() >= 2) {
						m_gameBaseData[parts[0].trimmed()] = parts[1].trimmed();
				}
		}
		file.close();

		// 通知 QML 更新游戏标题等元数据
		if (m_gameBaseData.contains("タイトル")) {
				emit globalDataUpdated("gameTitle", m_gameBaseData["タイトル"]);
		}
}

void EraEngine::sendUserInputValue(const QVariant &value)
{
		qDebug() << "User input received from QML:" << value;

		// 根据当前输入请求状态分发输入值
		// 后续在此处对接 Era 解释器的 INPUT / INPUTS / WAIT 状态机

		// 示例：回显输入并解除等待
		emit renderText(">> " + value.toString(), true, false);
}