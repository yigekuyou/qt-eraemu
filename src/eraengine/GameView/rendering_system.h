#ifndef RENDERING_SYSTEM_H
#define RENDERING_SYSTEM_H

#include <QObject>
#include <QString>
#include <QList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>
#include <QtQml/qqmlregistration.h>

// Text style information
struct TextStyle {
    bool bold;
    bool italic;
    bool underline;
    QString color;
    QString backgroundColor;
    
    TextStyle(bool b = false, bool i = false, bool u = false, 
              const QString& c = "", const QString& bg = "")
        : bold(b), italic(i), underline(u), color(c), backgroundColor(bg) {}
};

struct ConsoleLine {
		QString text;
		QString style;
		bool isHtml;
		TextStyle textStyle;  // Use TextStyle instead of simple style string
		QList<QVariantMap> buttons;  // Interactive buttons
		int lineNumber;
		ConsoleLine(const QString& txt = "", const QString& styleName = "", bool html = false)
				: text(txt), style(styleName), isHtml(html), lineNumber(0) {}
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
    
    // Additional signals for console display
    void textPrinted(const QString& text);
    void textStyled(const QString& text, const QVariant& styleData);
    void buttonAdded(const QString& text, const QString& label);
    void systemLinePrinted(const QString& text);
    void errorPrinted(const QString& text);
    void linesCleared();
    void linesChanged();

private:
		QList<ConsoleLine> m_screenLines;
    
    int m_currentLine;
    
    // Convert TextStyle to QVariant for QML
    QVariant textStyleToVariant(const TextStyle& style) const;
};

// Console display class - QML singleton
class ConsoleDisplay : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit ConsoleDisplay(QObject *parent = nullptr);
    
    // Output methods (callable from C++)
    Q_INVOKABLE void printText(const QString& text);
    Q_INVOKABLE void printTextWithStyle(const QString& text, const TextStyle& style);
    Q_INVOKABLE void printButton(const QString& text, const QString& label);
    Q_INVOKABLE void printSystemLine(const QString& text);
    Q_INVOKABLE void printError(const QString& text);
    Q_INVOKABLE void println();
    
    // Line management
    Q_INVOKABLE void setLineNumber(int line);
    Q_INVOKABLE int getLineNumber() const;
    
    // Get all lines (for QML)
    Q_INVOKABLE QList<QVariant> getLines() const;
    Q_INVOKABLE void clear();
    
signals:
    // Signals for QML integration
    void textPrinted(const QString& text);
    void textStyled(const QString& text, const QVariant& styleData);
    void buttonAdded(const QString& text, const QString& label);
    void systemLinePrinted(const QString& text);
    void errorPrinted(const QString& text);
    void linesCleared();
    void linesChanged();

private:
    QList<ConsoleLine> m_lines;
    int m_currentLine;
    
    // Convert TextStyle to QVariant for QML
    QVariant textStyleToVariant(const TextStyle& style) const;
};

#endif // RENDERING_SYSTEM_H