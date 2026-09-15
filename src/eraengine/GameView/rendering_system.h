#ifndef RENDERING_SYSTEM_H
#define RENDERING_SYSTEM_H

#include <QObject>
#include <QString>
#include <QList>

struct ConsoleLine {
		QString text;
		QString style;
		bool isHtml;
		ConsoleLine(const QString& txt = "", const QString& styleName = "", bool html = false)
				: text(txt), style(styleName), isHtml(html) {}
};

class RenderingSystem : public QObject
{
		Q_OBJECT
		// 暴露屏幕总文本供 QML 绑定更新
		Q_PROPERTY(QString screenContent READ getScreenContent NOTIFY screenContentChanged)

public:
		explicit RenderingSystem(QObject *parent = nullptr);

		Q_INVOKABLE void renderConsole(const QString& text, const QString& style = "");
		Q_INVOKABLE void clearConsole();
		Q_INVOKABLE QString getScreenContent() const;
		Q_INVOKABLE void renderTitle(const QString& title);

signals:
		void consoleRendered(const QString& text, const QString& style);
		void consoleCleared();
		void screenContentChanged();

private:
		QList<ConsoleLine> m_screenLines;
};

#endif // RENDERING_SYSTEM_H