#include "rendering_system.h"

RenderingSystem::RenderingSystem(QObject *parent) : QObject(parent) {}

void RenderingSystem::renderConsole(const QString& text, const QString& style) {
		m_screenLines.append(ConsoleLine(text, style));
		emit consoleRendered(text, style);
		emit screenContentChanged();
}

void RenderingSystem::clearConsole() {
		m_screenLines.clear();
		emit consoleCleared();
		emit screenContentChanged();
}

QString RenderingSystem::getScreenContent() const {
		QString content;
		for (const auto& line : m_screenLines) {
				content += line.text + "\n";
		}
		return content;
}

void RenderingSystem::renderTitle(const QString& title) {
		renderConsole("=== " + title + " ===", "title");
}