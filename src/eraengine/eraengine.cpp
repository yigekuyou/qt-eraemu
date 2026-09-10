#include "eraengine.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QCoreApplication>
#include <QVariantMap>
#include <QRegularExpression>
EraEngine::EraEngine()
		: m_isInitialized(false), m_currentLayout(0)
{
	m_variableStorage.initialize(150, 100);
}
// 辅助函数：在指定目录下不分大小写查找真实文件夹名
QString findActualDir(const QString &basePath, const QString &targetName) {
		QDir dir(basePath);
		if (!dir.exists()) {
				qDebug() << "Game directory not exists :" << basePath;
				return targetName; // 基础路径不存在，返回原目标名
		}

		QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
		for (const QString &subDir : subDirs) {
				qDebug() << "Game directory entryList :" << subDirs;
				if (subDir.compare(targetName, Qt::CaseInsensitive) == 0) {
						return subDir; // 返回磁盘上真实的目录名（大小写一致）
				}
		}
		return targetName; // 没找到则返回原目标名
}
void EraEngine::setGameDirectory(const QString &path)
{
		if (path.isEmpty()) return;
		QUrl url(path);
		QString localPath = url.isLocalFile() ? url.toLocalFile() : path;		// 统一将输入的路径转换为本地规范路径，并确保以 '/' 结尾
		QString normalizedPath = QDir::cleanPath(localPath);
		m_exeDir = normalizedPath.endsWith("/") ? normalizedPath : normalizedPath + "/";
		QString csvFolder = findActualDir(m_exeDir, "csv");
		QString erbFolder = findActualDir(m_exeDir, "erb");
		QString contentDir = findActualDir(m_exeDir, "resources");
		m_csvDir = m_exeDir + csvFolder + "/";
		m_erbDir = m_exeDir + erbFolder + "/";
		m_contentDir = m_exeDir + contentDir + "/";
		m_configPath = m_exeDir + "emuera.config";
		initializeEngine();
}

bool EraEngine::initializeEngine()
{
		//检查基础目录是否存在
		if (!QDir(m_csvDir).exists() || !QDir(m_erbDir).exists()) {
				qWarning() << "CSV or ERB directory missing in:" << m_exeDir;
				emit renderText("错误: 未找到 csv 或 erb 文件夹，请检查游戏路径。", true, false);
				return false;
		}

		if (!QDir(m_csvDir).exists() || !QDir(m_erbDir).exists()) {
						qWarning() << "CSV or ERB directory missing in:" << m_exeDir;
						emit renderText("错误: 未找到 csv 或 erb 文件夹，请检查游戏路径。", true, false);
						return false;
				}

				loadConfiguration();
				loadGameBaseCsv();

				// 参照 C# 补充后续初始化步骤
				// loadReplaceFile();
				// loadConstantsData();
				// loadHeaderFiles(); // ERH
				// loadErbFiles();    // ERB

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
		// 设置编码，防止中文路径或注释乱码（根据实际情况调整，通常为 Utf8）
		#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
				in.setCodec("UTF-8");
		#else
				in.setEncoding(QStringConverter::Utf8);
		#endif

		while (!in.atEnd()) {
				QString line = in.readLine().trimmed();
				if (line.isEmpty() || line.startsWith(";")) continue;

				// 解析 GameBase 定义
				QStringList parts = line.split(',');
				if (parts.size() < 2) continue;

				QString key = parts[0].trimmed();
				QString val = parts[1].trimmed();
				m_gameBaseData[key] = val;

				// 针对特定键值进行逻辑处理（对标 C# 的 switch 逻辑）
				if (key == "コード") {
						bool ok = false;
						long long code = val.toLongLong(&ok);
						if (ok && code == 0) {
								qWarning() << "代码:0的セーブデータはいかなるコードのスクリプトからも読めるデータとして扱われます";
						}
				}
				else if (key == "動作に必要なEmueraのバージョン") {
						// 正则校验版本格式 (例如: 1.824.0 等)
						QRegularExpression rx("^\\d+\\.\\d+\\.\\d+\\.\\d+$");
						if (!rx.match(val).hasMatch()) {
								qWarning() << "版本指定指标无法识别，跳过处理：" << val;
								continue;
						}
						// 可在此处对比当前引擎版本
						// Version currentVersion(InternalEmueraVer);
						// Version targetVersion(val);
						// if (currentVersion < targetVersion) { ... }
				}
		}
		file.close();

		// 处理窗口标题默认回退逻辑
		if (!m_gameBaseData.contains("ウィンドウタイトル") || m_gameBaseData["ウィンドウタイトル"].toString().isEmpty()) {
				QString title = m_gameBaseData.value("タイトル", "").toString();
				QString versionText = m_gameBaseData.value("バージョン", "").toString();
				if (title.isEmpty()) {
						m_gameBaseData["ウィンドウタイトル"] = "Emuera";
				} else {
						m_gameBaseData["ウィンドウタイトル"] = title + " " + versionText;
				}
		}

		// 通知 QML 更新游戏标题等元数据
		if (m_gameBaseData.contains("タイトル")) {
				emit globalDataUpdated("gameTitle", m_gameBaseData["タイトル"]);
		}
		if (m_gameBaseData.contains("ウィンドウタイトル")) {
				emit globalDataUpdated("windowTitle", m_gameBaseData["ウィンドウタイトル"]);
		}

		emit gameBaseDataChanged();
}

void EraEngine::sendUserInputValue(const QVariant &value)
{
		qDebug() << "User input received from QML:" << value;

		// 根据当前输入请求状态分发输入值
		// 后续在此处对接 Era 解释器的 INPUT / INPUTS / WAIT 状态机

		// 示例：回显输入并解除等待
		emit renderText(">> " + value.toString(), true, false);
}