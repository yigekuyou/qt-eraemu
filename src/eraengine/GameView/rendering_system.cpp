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
#include "rendering_system.h"
#include <QVariantMap>

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

// ConsoleDisplay implementation
ConsoleDisplay::ConsoleDisplay(QObject *parent)
    : QObject(parent), m_currentLine(0)
{
}

void ConsoleDisplay::printText(const QString& text) {
    ConsoleLine line(text, "", false);
    line.lineNumber = m_currentLine;
    m_lines.append(line);
    
    emit textPrinted(text);
    emit linesChanged();
    
    m_currentLine++;
}

void ConsoleDisplay::printTextWithStyle(const QString& text, const TextStyle& style) {
    ConsoleLine line(text, "", false);
    line.textStyle = style;
    line.lineNumber = m_currentLine;
    m_lines.append(line);
    
    emit textStyled(text, textStyleToVariant(style));
    emit linesChanged();
    
    m_currentLine++;
}

void ConsoleDisplay::printButton(const QString& text, const QString& label) {
    if (!m_lines.isEmpty()) {
        QVariantMap button;
        button["text"] = text;
        button["label"] = label;
        m_lines.last().buttons.append(button);
        
        emit buttonAdded(text, label);
    }
}

void ConsoleDisplay::printSystemLine(const QString& text) {
    ConsoleLine line(text, "", false);
    line.textStyle.color = "blue";
    line.lineNumber = m_currentLine;
    m_lines.append(line);
    
    emit systemLinePrinted(text);
    emit linesChanged();
    
    m_currentLine++;
}

void ConsoleDisplay::printError(const QString& text) {
    ConsoleLine line(text, "", false);
    line.textStyle.color = "red";
    line.lineNumber = m_currentLine;
    m_lines.append(line);
    
    emit errorPrinted(text);
    emit linesChanged();
    
    m_currentLine++;
}

void ConsoleDisplay::println() {
    ConsoleLine line("", "", false);
    line.lineNumber = m_currentLine;
    m_lines.append(line);
    
    emit linesChanged();
    
    m_currentLine++;
}

void ConsoleDisplay::setLineNumber(int line) {
    m_currentLine = line;
}

int ConsoleDisplay::getLineNumber() const {
    return m_currentLine;
}

QList<QVariant> ConsoleDisplay::getLines() const {
    QList<QVariant> lines;
    for (const ConsoleLine& line : m_lines) {
        QVariantMap lineData;
        lineData["text"] = line.text;
        lineData["style"] = textStyleToVariant(line.textStyle);
        lineData["buttons"] = QVariant::fromValue(line.buttons);
        lineData["lineNumber"] = line.lineNumber;
        lines.append(lineData);
    }
    return lines;
}

void ConsoleDisplay::clear() {
    m_lines.clear();
    m_currentLine = 0;
    emit linesCleared();
    emit linesChanged();
}

QVariant ConsoleDisplay::textStyleToVariant(const TextStyle& style) const {
    QVariantMap variant;
    variant["bold"] = style.bold;
    variant["italic"] = style.italic;
    variant["underline"] = style.underline;
    variant["color"] = style.color;
    variant["backgroundColor"] = style.backgroundColor;
    return variant;
}