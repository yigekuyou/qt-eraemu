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
#include "gui_manager.h"

#include "config_loader.h"
#include "console_backend.h"
#include "console_buffer.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>

namespace {

// C# ConfigData 的日文键（唯一权威）+ 常见英文别名
const QString kKeyFontName    = QStringLiteral("フォント名");
const QString kKeyFontSize    = QStringLiteral("フォントサイズ");
const QString kKeyLineHeight  = QStringLiteral("一行の高さ");
const QString kKeyForeColor   = QStringLiteral("文字色");
const QString kKeyBackColor   = QStringLiteral("背景色");
const QString kKeyFocusColor  = QStringLiteral("選択中文字色");
const QString kKeyLogColor    = QStringLiteral("履歴文字色");
const QString kKeyFps         = QStringLiteral("フレーム毎秒");
const QString kKeyMaxLog      = QStringLiteral("履歴ログの行数");
const QString kKeyScrollLines = QStringLiteral("スクロール行数");
const QString kKeySizable     = QStringLiteral("ウィンドウの高さを可変にする");
const QString kKeyMaximized   = QStringLiteral("起動時にウィンドウを最大化する");
const QString kKeyWindowWidth = QStringLiteral("ウィンドウ幅");
const QString kKeyWindowHeight = QStringLiteral("ウィンドウ高さ");

const QStringList kAliasFontName    = {QStringLiteral("FontName")};
const QStringList kAliasFontSize    = {QStringLiteral("FontSize")};
const QStringList kAliasLineHeight  = {QStringLiteral("LineHeight")};
const QStringList kAliasForeColor   = {QStringLiteral("ForeColor")};
const QStringList kAliasBackColor   = {QStringLiteral("BackColor")};
const QStringList kAliasFocusColor  = {QStringLiteral("FocusColor")};
const QStringList kAliasLogColor    = {QStringLiteral("LogColor")};
const QStringList kAliasFps         = {QStringLiteral("FPS")};
const QStringList kAliasMaxLog      = {QStringLiteral("MaxLog")};
const QStringList kAliasScrollLines = {QStringLiteral("ScrollHeight")};
const QStringList kAliasSizable     = {QStringLiteral("SizableWindow")};
const QStringList kAliasMaximized   = {QStringLiteral("WindowMaximixed"),
                                       QStringLiteral("WindowMaximized")};
const QStringList kAliasWindowWidth = {QStringLiteral("WindowX")};
const QStringList kAliasWindowHeight = {QStringLiteral("WindowY")};

// 依次尝试：日文键 + 英文别名；命中第一个存在的键
bool lookup(const ConfigLoader& cfg, const QString& key, const QStringList& aliases, QString& out) {
    if (cfg.hasConfig(key)) { out = cfg.getConfig(key); return true; }
    for (const QString& a : aliases) {
        if (cfg.hasConfig(a)) { out = cfg.getConfig(a); return true; }
    }
    return false;
}

} // namespace

GuiManager::GuiManager(QObject* parent)
    : QObject(parent)
{
    m_startDirectory = QDir::currentPath();
}

void GuiManager::setConsole(ConsoleBackend* console) {
    m_console = console;
    applyToConsole();
}

// ---------------------------------------------------------------------------
// 配置读写
// ---------------------------------------------------------------------------

void GuiManager::loadFromConfig(const ConfigLoader& cfg) {
    QString text;

    if (lookup(cfg, kKeyFontName, kAliasFontName, text))  setFontName(text);
    if (lookup(cfg, kKeyFontSize, kAliasFontSize, text))  setFontSize(text.toInt());
    if (lookup(cfg, kKeyLineHeight, kAliasLineHeight, text)) setLineHeight(text.toInt());
    if (lookup(cfg, kKeyForeColor, kAliasForeColor, text))  setForeColor(parseColor(text, m_foreColor));
    if (lookup(cfg, kKeyBackColor, kAliasBackColor, text))  setBackColor(parseColor(text, m_backColor));
    if (lookup(cfg, kKeyFocusColor, kAliasFocusColor, text)) setFocusColor(parseColor(text, m_focusColor));
    if (lookup(cfg, kKeyLogColor, kAliasLogColor, text))    setLogColor(parseColor(text, m_logColor));
    if (lookup(cfg, kKeyFps, kAliasFps, text))              setFps(text.toInt());
    if (lookup(cfg, kKeyMaxLog, kAliasMaxLog, text))        setMaxLog(text.toInt());
    if (lookup(cfg, kKeyScrollLines, kAliasScrollLines, text)) setScrollLines(text.toInt());
    if (lookup(cfg, kKeySizable, kAliasSizable, text))      setSizableWindow(cfg.getBool(kKeySizable, m_sizableWindow)
                                                                             || cfg.getBool(kAliasSizable.value(0), m_sizableWindow));
    if (lookup(cfg, kKeyMaximized, kAliasMaximized, text))  setMaximized(cfg.getBool(kKeyMaximized, m_maximized)
                                                                          || cfg.getBool(kAliasMaximized.value(0), m_maximized));
    if (lookup(cfg, kKeyWindowWidth, kAliasWindowWidth, text))  setWindowWidth(text.toInt());
    if (lookup(cfg, kKeyWindowHeight, kAliasWindowHeight, text)) setWindowHeight(text.toInt());

    applyToConsole();
    emit settingsChanged();
}

int GuiManager::saveToConfig(ConfigLoader& cfg) const {
    int n = 0;
    const auto put = [&cfg, &n](const QString& key, const QString& value) {
        if (cfg.getConfig(key) == value && cfg.hasConfig(key)) return;
        cfg.setConfig(key, value);
        ++n;
    };
    put(kKeyFontName, m_fontName);
    put(kKeyFontSize, QString::number(m_fontSize));
    put(kKeyLineHeight, QString::number(m_lineHeight));
    put(kKeyForeColor, colorToString(m_foreColor));
    put(kKeyBackColor, colorToString(m_backColor));
    put(kKeyFocusColor, colorToString(m_focusColor));
    put(kKeyLogColor, colorToString(m_logColor));
    put(kKeyFps, QString::number(m_fps));
    put(kKeyMaxLog, QString::number(m_maxLog));
    put(kKeyScrollLines, QString::number(m_scrollLines));
    put(kKeySizable, m_sizableWindow ? QStringLiteral("YES") : QStringLiteral("NO"));
    put(kKeyMaximized, m_maximized ? QStringLiteral("YES") : QStringLiteral("NO"));
    put(kKeyWindowWidth, QString::number(m_windowWidth));
    put(kKeyWindowHeight, QString::number(m_windowHeight));
    return n;
}

// ---------------------------------------------------------------------------
// setter（统一发 settingsChanged，避免 QML 多处绑定遗漏）
// ---------------------------------------------------------------------------

void GuiManager::setFontName(const QString& v) {
    if (m_fontName == v) return;
    m_fontName = v;
    emit settingsChanged();
}
void GuiManager::setFontSize(int v) {
    v = qBound(6, v, 128);
    if (m_fontSize == v) return;
    m_fontSize = v;
    // 字号变化时若未单独设置行高，则行高跟随
    emit settingsChanged();
}
void GuiManager::setLineHeight(int v) {
    v = qBound(6, v, 200);
    if (m_lineHeight == v) return;
    m_lineHeight = v;
    emit settingsChanged();
}
void GuiManager::setForeColor(const QColor& v) { if (m_foreColor == v) return; m_foreColor = v; emit settingsChanged(); }
void GuiManager::setBackColor(const QColor& v) { if (m_backColor == v) return; m_backColor = v; emit settingsChanged(); }
void GuiManager::setFocusColor(const QColor& v) { if (m_focusColor == v) return; m_focusColor = v; emit settingsChanged(); }
void GuiManager::setLogColor(const QColor& v) { if (m_logColor == v) return; m_logColor = v; emit settingsChanged(); }
void GuiManager::setFps(int v) {
    v = qBound(1, v, 120);
    if (m_fps == v) return;
    m_fps = v;
    applyToConsole();
    emit settingsChanged();
}
void GuiManager::setMaxLog(int v) {
    v = qBound(100, v, 1000000);
    if (m_maxLog == v) return;
    m_maxLog = v;
    applyToConsole();
    emit settingsChanged();
}
void GuiManager::setScrollLines(int v) {
    v = qBound(1, v, 100);
    if (m_scrollLines == v) return;
    m_scrollLines = v;
    emit settingsChanged();
}
void GuiManager::setSizableWindow(bool v) { if (m_sizableWindow == v) return; m_sizableWindow = v; emit settingsChanged(); }
void GuiManager::setMaximized(bool v) { if (m_maximized == v) return; m_maximized = v; emit settingsChanged(); }
void GuiManager::setWindowWidth(int v) {
    v = qBound(200, v, 20000);
    if (m_windowWidth == v) return;
    m_windowWidth = v;
    emit settingsChanged();
}
void GuiManager::setWindowHeight(int v) {
    v = qBound(200, v, 20000);
    if (m_windowHeight == v) return;
    m_windowHeight = v;
    emit settingsChanged();
}
void GuiManager::setWindowTitle(const QString& v) {
    if (m_windowTitle == v) return;
    m_windowTitle = v;
    emit windowTitleChanged();
}
void GuiManager::setGameDirectory(const QString& v) {
    if (m_gameDirectory == v) return;
    m_gameDirectory = v;
    emit gameDirectoryChanged();
}
void GuiManager::setStartDirectory(const QString& v) {
    if (m_startDirectory == v) return;
    m_startDirectory = v;
    emit startDirectoryChanged();
}

// ---------------------------------------------------------------------------
// 行为
// ---------------------------------------------------------------------------

void GuiManager::resetToDefaults() {
    m_fontName      = QString();
    m_fontSize      = 18;
    m_lineHeight    = 19;
    m_foreColor     = QColor(192, 192, 192);
    m_backColor     = QColor(0, 0, 0);
    m_focusColor    = QColor(255, 255, 0);
    m_logColor      = QColor(192, 192, 192);
    m_fps           = 5;
    m_maxLog        = 5000;
    m_scrollLines   = 1;
    m_sizableWindow = true;
    m_maximized     = false;
    m_windowWidth   = 760;
    m_windowHeight  = 480;
    applyToConsole();
    emit settingsChanged();
}

void GuiManager::applyToConsole() {
    if (!m_console) return;
    m_console->setFrameMs(frameMs());
    m_console->setMaxLog(m_maxLog);
}

bool GuiManager::saveLog(const QString& path) {
    QString target = path;
    if (target.isEmpty()) {
        target = QDir(m_startDirectory).filePath(QStringLiteral("emuera.log"));
    }
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit logSaveFailed(target, file.errorString());
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    if (m_console) {
        const QList<ConsoleDisplayLine>& lines = m_console->buffer().lines();
        for (const ConsoleDisplayLine& line : lines) {
            out << line.plainText() << '\n';
        }
    }
    if (!file.commit()) {
        emit logSaveFailed(target, file.errorString());
        return false;
    }
    emit logSaved(target);
    return true;
}

QStringList GuiManager::availableFontFamilies() const {
    return QFontDatabase::families();
}

QString GuiManager::colorToString(const QColor& color) const {
    return color.isValid() ? color.name(QColor::HexRgb) : QString();
}

QColor GuiManager::colorFromString(const QString& text) const {
    return parseColor(text, QColor());
}

void GuiManager::adjustFontSize(int delta) {
    setFontSize(m_fontSize + delta);
    if (m_lineHeight == 0) return;
    // 行高与字号保持同一比例（C# 默认 19/18）
    const int lh = qMax(m_fontSize + 1, static_cast<int>(m_lineHeight * (m_fontSize + delta) / qMax(1, m_fontSize) + 0.5));
    setLineHeight(lh);
}

QColor GuiManager::parseColor(const QString& text, const QColor& fallback) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return fallback;
    QColor c(t);                       // "#RRGGBB" / "#AARRGGBB" / 颜色名
    if (c.isValid()) return c;
    bool ok = false;
    const uint v = t.toUInt(&ok, 16);  // "RRGGBB"（无 #）
    if (ok) return QColor::fromRgb(v);
    return fallback;
}
