#ifndef ERAENGINE_H
#define ERAENGINE_H
#include <qqmlintegration.h>
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
public:
	EraEngine();
private:
		bool m_isInitialized;
		QString m_exeDir;
		QString m_csvDir;
		QString m_erbDir;
		QString m_configPath;
		int m_currentLayout;
		void setGameDirectory(const QString &path);

		QVariantMap m_configMap;
		QVariantMap m_gameBaseData;

		bool initializeEngine();
		void loadConfiguration();
		void loadGameBaseCsv();
signals:
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
