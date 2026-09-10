#ifndef ERAENGINE_H
#define ERAENGINE_H
#include <qqmlintegration.h>
#include "variable_storage.h"
#include <QQuickItem>
#include <QObject>
#include <QDBusInterface>
#include <QString>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
//引入这个头文件以使用 QML_ELEMENT
#include <qqmlregistration.h>
#include <QDBusPendingCallWatcher>
class EraEngine : public QObject
{
	Q_OBJECT
	QML_ELEMENT
	Q_PROPERTY(bool isInitialized READ isInitialized NOTIFY isInitializedChanged)
	Q_PROPERTY(QString exeDir READ exeDir NOTIFY exeDirChanged)
	Q_PROPERTY(QString csvDir READ csvDir NOTIFY csvDirChanged)
	Q_PROPERTY(QString erbDir READ erbDir NOTIFY erbDirChanged)
	Q_PROPERTY(QString contentDir READ contentDir NOTIFY contentDirChanged)
	Q_PROPERTY(QString configPath READ configPath NOTIFY configPathChanged)
	Q_PROPERTY(QVariantMap configMap READ configMap NOTIFY configMapChanged)
	Q_PROPERTY(QVariantMap gameBaseData READ gameBaseData NOTIFY gameBaseDataChanged)
	Q_PROPERTY(QString gameDirectory READ gameDirectory WRITE setGameDirectory NOTIFY gameDirectoryChanged)
	Q_PROPERTY(VariableStorage* variableStorage READ variableStorage CONSTANT)
public:
	EraEngine();
	bool isInitialized() const { return m_isInitialized; }
	QString exeDir() const { return m_exeDir; }
	QString csvDir() const { return m_csvDir; }
	QString erbDir() const { return m_erbDir; }
	QString contentDir() const { return m_contentDir; }
	QString configPath() const { return m_configPath; }
	QString gameDirectory() const { return m_gameDirectory; }
	int currentLayout() const { return m_currentLayout; }
	QVariantMap configMap() const { return m_configMap; }
	QVariantMap gameBaseData() const { return m_gameBaseData; }
	void setGameDirectory(const QString &path);
	// 可写属性的 Setter 函数声明
	void setCurrentLayout(int currentLayout);

	VariableStorage* variableStorage() { return &m_variableStorage; }
			const VariableStorage* variableStorage() const { return &m_variableStorage; }

	Q_INVOKABLE void setGlobalInt(const QString &name, int index, qint64 value) {
			m_variableStorage.setGlobalInt(name, index, value);
		}
		Q_INVOKABLE qint64 getGlobalInt(const QString &name, int index) const {
			return m_variableStorage.getGlobalInt(name, index);
		}
		Q_INVOKABLE void setGlobalStr(const QString &name, int index, const QString &value) {
			m_variableStorage.setGlobalStr(name, index, value);
		}
		Q_INVOKABLE QString getGlobalStr(const QString &name, int index) const {
			return m_variableStorage.getGlobalStr(name, index);
		}
		Q_INVOKABLE void setCharaInt(const QString &name, int charaId, int index, qint64 value) {
			m_variableStorage.setCharaInt(name, charaId, index, value);
		}
		Q_INVOKABLE qint64 getCharaInt(const QString &name, int charaId, int index) const {
			return m_variableStorage.getCharaInt(name, charaId, index);
		}
private:
		bool m_isInitialized;
		QString m_exeDir;
		QString m_csvDir;
		QString m_erbDir;
		QString m_configPath;
		int m_currentLayout;
		QString m_contentDir;
		QString m_gameDirectory;
		VariableStorage m_variableStorage;
		QVariantMap m_configMap;
		QVariantMap m_gameBaseData;

		bool initializeEngine();
		void loadConfiguration();
		void loadGameBaseCsv();
signals:
		void isInitializedChanged();
		void exeDirChanged();
		void csvDirChanged();
		void erbDirChanged();
		void contentDirChanged();
		void configPathChanged();
		void currentLayoutChanged();
		void configMapChanged();
		void gameBaseDataChanged();
		void gameDirectoryChanged();
		// 基础文本类
		void renderText(const QString &text, bool addNewline, bool needWait);

		// 图片类信号 (传递图片路径或 ID、宽高、以及后续控制符)
		void renderImage(const QString &imagePath, int width, int height, bool addNewline, bool needWait);

		// 按钮类信号 (传递按钮显示的文本、绑定的返回值)
		void renderButton(const QString &buttonText, const QVariant &returnValue, bool addNewline);

		// 条形图信号 (当前值，最大值，长度)
		void drawBar(int current, int max, int width, const QString &color);
		// 控制流式布局、绝对布局或网格布局
		void setlayout(int layoutMode);
		//音频信号
		void playMedia(int type, const QString &path, int volume);
		//等待信号
		void waitTimer(int milliseconds);
		//清除屏幕
		void clearScreen();
		//字体样式
		void setStyle(const QVariantMap &styleMap);
		//drawLine
		void drawLine(const QString &styleChar);
		//type 区分：0=任意键(WAIT), 1=数字输入(INPUT), 2=字符串输入(INPUTS), 3=鼠标点击(INPUTMOUSEKEY)。通知 QML 开放输入控件并显示提示。
		void requestInput(int type, int timeout, const QString &prompt);
		//渲染命令 cmdId 代表画布操作（如画线、贴图、移动 Sprite），args 包含坐标、大小、纹理 ID 等
		void renderCommand(int cmdId, const QVariantList &args);\
		//更新
		void globalDataUpdated(const QString &key, const QVariant &value);
public slots:
		void sendUserInputValue(const QVariant &value);
};
#endif // ERAENGINE_H
