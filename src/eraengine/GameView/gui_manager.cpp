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

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>

namespace {
// 窗口宽高的健全性上限（只挡荒唐值；下限是窗口的事，见 WindowController::targetWindowSize）
constexpr int kMaxWindowSide = 20000;
}  // namespace

GuiManager::GuiManager(QObject* parent)
    : QObject(parent)
{
    m_startDirectory = QDir::currentPath();
    // 成员初值不在这里写：默认值只在配置键表里有一份
    resetToDefaults();
}

void GuiManager::setConsole(ConsoleBackend* console) {
    m_console = console;
    applyToConsole();
}

// ---------------------------------------------------------------------------
// 配置读写
// ---------------------------------------------------------------------------

void GuiManager::loadFromConfig(const ConfigLoader& cfg) {
    // 键名/别名/默认值/类型全在 ConfigLoader 的那一张配置键表里：这里只按名问它，
    // 每一项都只查表一次（以前先 lookup 别名、再用 hasConfig/getBool 按某个名字
    // 重查一遍，于是写正确拼法 WindowMaximized 时值被拼错的键名吞掉）。
    setFontName(cfg.configText(QStringLiteral("フォント名")));
    setFontSize(cfg.configInt(QStringLiteral("フォントサイズ")));
    setLineHeight(cfg.configInt(QStringLiteral("一行の高さ")));
    setForeColor(parseColor(cfg.configText(QStringLiteral("文字色")), m_foreColor));
    setBackColor(parseColor(cfg.configText(QStringLiteral("背景色")), m_backColor));
    setFocusColor(parseColor(cfg.configText(QStringLiteral("選択中文字色")), m_focusColor));
    setLogColor(parseColor(cfg.configText(QStringLiteral("履歴文字色")), m_logColor));
    setFps(cfg.configInt(QStringLiteral("フレーム毎秒")));
    setMaxLog(cfg.configInt(QStringLiteral("履歴ログの行数")));
    setScrollLines(cfg.configInt(QStringLiteral("スクロール行数")));
    setSizableWindow(cfg.configBool(QStringLiteral("ウィンドウの高さを可変にする")));
    setMaximized(cfg.configBool(QStringLiteral("起動時にウィンドウを最大化する")));
    setWindowWidth(cfg.configInt(QStringLiteral("ウィンドウ幅")));
    setWindowHeight(cfg.configInt(QStringLiteral("ウィンドウ高さ")));

    applyToConsole();
    emit settingsChanged();
}

int GuiManager::saveToConfig(ConfigLoader& cfg) const {
    int n = 0;
    // 写回用的就是键表里的规范键（GuiManager 不自带键名清单/别名）
    const auto put = [&cfg, &n](const QString& key, const QString& value) {
        if (cfg.getConfig(key) == value && cfg.hasConfig(key)) return;
        cfg.setConfig(key, value);
        ++n;
    };
    put(QStringLiteral("フォント名"), m_fontName);
    put(QStringLiteral("フォントサイズ"), QString::number(m_fontSize));
    put(QStringLiteral("一行の高さ"), QString::number(m_lineHeight));
    put(QStringLiteral("文字色"), colorToString(m_foreColor));
    put(QStringLiteral("背景色"), colorToString(m_backColor));
    put(QStringLiteral("選択中文字色"), colorToString(m_focusColor));
    put(QStringLiteral("履歴文字色"), colorToString(m_logColor));
    put(QStringLiteral("フレーム毎秒"), QString::number(m_fps));
    put(QStringLiteral("履歴ログの行数"), QString::number(m_maxLog));
    put(QStringLiteral("スクロール行数"), QString::number(m_scrollLines));
    put(QStringLiteral("ウィンドウの高さを可変にする"),
        m_sizableWindow ? QStringLiteral("YES") : QStringLiteral("NO"));
    put(QStringLiteral("起動時にウィンドウを最大化する"),
        m_maximized ? QStringLiteral("YES") : QStringLiteral("NO"));
    put(QStringLiteral("ウィンドウ幅"), QString::number(m_windowWidth));
    put(QStringLiteral("ウィンドウ高さ"), QString::number(m_windowHeight));
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
    // 字号变化时若未单独设置行高，则行高跟随（EraEngine 随后按新字号重推网格）
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
// 舞台尺寸 = 配置 ウィンドウ幅/高さ（也是窗口的目标几何）。这里**不套窗口
// 下限**：320x240 是**窗口**的下限，只有 WindowController::targetWindowSize
// 用它；舞台是整块逻辑画布，可以比窗口小（Android 小窗）也可以比窗口大（桌面
// 放大）。只挡非正数与荒唐值，也不做屏幕钳制 —— 窗口尺寸唯一来源是配置。
void GuiManager::setWindowWidth(int v) {
    v = qBound(1, v, kMaxWindowSide);
    if (m_windowWidth == v) return;
    m_windowWidth = v;
    emit settingsChanged();
}
void GuiManager::setWindowHeight(int v) {
    v = qBound(1, v, kMaxWindowSide);
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
    // 复位 = 从**空配置**装载一遍：每一项都落回 ConfigLoader 键表里的默认值，
    // 这里不再抄一份 18/19/5000/760x480（此前成员初值、reset、键表三处各一份）。
    const ConfigLoader noConfig;
    loadFromConfig(noConfig);
}

void GuiManager::applyToConsole() {
    if (!m_console) return;
    qDebug() << "[render] 应用界面设置：舞台" << m_windowWidth << "x" << m_windowHeight
             << "字号" << m_fontSize << "行高" << m_lineHeight
             << "帧率" << frameMs() << "ms 历史" << m_maxLog;
    m_console->setFrameMs(frameMs());
    m_console->setMaxLog(m_maxLog);
    // 排版参数：字号/行高变化时 C++ 重算区块位置与尺寸；
    // 网格（列/行）由 EraEngine::syncConsoleGrid 按舞台尺寸推导，与窗口无关
    m_console->setFontSize(m_fontSize);
    m_console->setLineHeight(m_lineHeight);
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
    // C# emuera.config 的颜色是 "R,G,B"（表内默认值也是这个形态），
    // "#rrggbb" 是本引擎自己写回的格式；两种都认。
    const QStringList rgb = t.split(QLatin1Char(','));
    if (rgb.size() == 3) {
        bool r = false, g = false, b = false;
        const int rv = rgb.at(0).trimmed().toInt(&r);
        const int gv = rgb.at(1).trimmed().toInt(&g);
        const int bv = rgb.at(2).trimmed().toInt(&b);
        if (r && g && b) return QColor(rv, gv, bv);
    }
    bool ok = false;
    const uint v = t.toUInt(&ok, 16);  // "RRGGBB"（无 #）
    if (ok) return QColor::fromRgb(v);
    return fallback;
}
