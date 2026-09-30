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
#ifndef GUI_MANAGER_H
#define GUI_MANAGER_H

#include <QObject>
#include <QColor>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class ConfigLoader;
class ConsoleBackend;

// ---------------------------------------------------------------------------
// GuiManager —— 界面（GUI）配置与行为的集中管理
//
// 对齐 C# Emuera 的「界面相关 Config」+ `Forms/MainWindow` 的窗口/菜单行为：
//   * 字体 / 字号 / 行高 / 前景色 / 背景色 / 选中色 / 历史色；
//   * 刷新帧率（FPS）、历史保留行数（MaxLog）、滚轮行数（ScrollHeight）；
//   * 窗口宽高、可调高、启动最大化；
//   * 日志保存（`saveLog`，等价 C# 菜单「ログを保存」）；
//   * 读取/写回 `emuera.config`（键名沿用 C# ConfigData 的日文键）。
//
// 数据模型：QML 只读绑定；设置对话框写回后 `settingsChanged` 触发控制台重绘。
// 纯逻辑（默认值/配置往返/颜色解析/日志文本）可无 UI 单测（见 test_gui_manager）。
// ---------------------------------------------------------------------------
class GuiManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("GuiManager 由 EraEngine 提供")

    Q_PROPERTY(QString fontName READ fontName WRITE setFontName NOTIFY settingsChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY settingsChanged)
    Q_PROPERTY(int lineHeight READ lineHeight WRITE setLineHeight NOTIFY settingsChanged)
    Q_PROPERTY(QColor foreColor READ foreColor WRITE setForeColor NOTIFY settingsChanged)
    Q_PROPERTY(QColor backColor READ backColor WRITE setBackColor NOTIFY settingsChanged)
    Q_PROPERTY(QColor focusColor READ focusColor WRITE setFocusColor NOTIFY settingsChanged)
    Q_PROPERTY(QColor logColor READ logColor WRITE setLogColor NOTIFY settingsChanged)
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY settingsChanged)
    Q_PROPERTY(int maxLog READ maxLog WRITE setMaxLog NOTIFY settingsChanged)
    Q_PROPERTY(int scrollLines READ scrollLines WRITE setScrollLines NOTIFY settingsChanged)
    Q_PROPERTY(bool sizableWindow READ sizableWindow WRITE setSizableWindow NOTIFY settingsChanged)
    Q_PROPERTY(bool maximized READ maximized WRITE setMaximized NOTIFY settingsChanged)
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY settingsChanged)
    Q_PROPERTY(int windowHeight READ windowHeight WRITE setWindowHeight NOTIFY settingsChanged)
    // 只读：窗口最小尺寸。引擎的逻辑网格（脚本看到的列/行数）不随窗口变化，
    // 窗口小于「网格 × 单元格像素」就会裁掉内容，故以下限约束窗口与设置值。
    Q_PROPERTY(int minimumWindowWidth READ minimumWindowWidth NOTIFY settingsChanged)
    Q_PROPERTY(int minimumWindowHeight READ minimumWindowHeight NOTIFY settingsChanged)
    Q_PROPERTY(QString windowTitle READ windowTitle WRITE setWindowTitle NOTIFY windowTitleChanged)
    Q_PROPERTY(QString gameDirectory READ gameDirectory WRITE setGameDirectory NOTIFY gameDirectoryChanged)
    // 只读：启动目录（QML 文件对话框的初始路径）
    Q_PROPERTY(QString startDirectory READ startDirectory NOTIFY startDirectoryChanged)

public:
    explicit GuiManager(QObject* parent = nullptr);

    // 控制台（应用行高/帧率/历史容量）
    void setConsole(ConsoleBackend* console);
    [[nodiscard]] ConsoleBackend* console() const { return m_console; }

    // ---- 配置读写（键名对齐 C# ConfigData；英文别名也接受）----
    void loadFromConfig(const ConfigLoader& cfg);
    int  saveToConfig(ConfigLoader& cfg) const;   // 返回写入的键数

    // ---- 访问器 ----
    [[nodiscard]] QString fontName() const { return m_fontName; }
    [[nodiscard]] int fontSize() const { return m_fontSize; }
    [[nodiscard]] int lineHeight() const { return m_lineHeight; }
    [[nodiscard]] QColor foreColor() const { return m_foreColor; }
    [[nodiscard]] QColor backColor() const { return m_backColor; }
    [[nodiscard]] QColor focusColor() const { return m_focusColor; }
    [[nodiscard]] QColor logColor() const { return m_logColor; }
    [[nodiscard]] int fps() const { return m_fps; }
    [[nodiscard]] int maxLog() const { return m_maxLog; }
    [[nodiscard]] int scrollLines() const { return m_scrollLines; }
    [[nodiscard]] bool sizableWindow() const { return m_sizableWindow; }
    [[nodiscard]] bool maximized() const { return m_maximized; }
    [[nodiscard]] int windowWidth() const { return m_windowWidth; }
    [[nodiscard]] int windowHeight() const { return m_windowHeight; }
    // 最小尺寸 = 网格能完整显示所需像素（单元格宽按「字号/2」的半角口径，
    // 与引擎 maxLineUnits 一致），再加菜单栏/输入条/滚动条的余量。
    [[nodiscard]] int minimumWindowWidth() const;
    [[nodiscard]] int minimumWindowHeight() const;
    // 字号/行高变化后把窗口尺寸抬回下限之上（内部使用）
    void reclampWindowSize();
    [[nodiscard]] QString windowTitle() const { return m_windowTitle; }
    [[nodiscard]] QString gameDirectory() const { return m_gameDirectory; }
    [[nodiscard]] QString startDirectory() const { return m_startDirectory; }

    void setFontName(const QString& v);
    void setFontSize(int v);
    void setLineHeight(int v);
    void setForeColor(const QColor& v);
    void setBackColor(const QColor& v);
    void setFocusColor(const QColor& v);
    void setLogColor(const QColor& v);
    void setFps(int v);
    void setMaxLog(int v);
    void setScrollLines(int v);
    void setSizableWindow(bool v);
    void setMaximized(bool v);
    void setWindowWidth(int v);
    void setWindowHeight(int v);
    void setWindowTitle(const QString& v);
    void setGameDirectory(const QString& v);
    void setStartDirectory(const QString& v);

    // 刷新间隔（毫秒）：由 FPS 换算（1Hz 兜底）
    [[nodiscard]] int frameMs() const { return m_fps > 0 ? qMax(16, 1000 / m_fps) : 1000; }

public slots:
    // ---- 界面行为（供 QML 菜单/对话框调用）----
    Q_INVOKABLE void resetToDefaults();
    // 把当前设置应用到控制台（行高/帧率/历史容量）
    Q_INVOKABLE void applyToConsole();
    // 保存控制台日志为 UTF-8 文本（等价 C#「ログを保存」）；path 为空则用默认名
    Q_INVOKABLE bool saveLog(const QString& path);
    // 系统可用字体（设置对话框用）
    Q_INVOKABLE QStringList availableFontFamilies() const;
    // 颜色 -> "#rrggbb"（设置对话框显示/编辑）
    Q_INVOKABLE QString colorToString(const QColor& color) const;
    Q_INVOKABLE QColor colorFromString(const QString& text) const;
    // 字号增减（View 菜单 Ctrl+= / Ctrl+-）
    Q_INVOKABLE void adjustFontSize(int delta);

signals:
    void settingsChanged();
    void windowTitleChanged();
    void gameDirectoryChanged();
    void startDirectoryChanged();
    void logSaved(const QString& path);
    void logSaveFailed(const QString& path, const QString& reason);

private:
    static QColor parseColor(const QString& text, const QColor& fallback);

    ConsoleBackend* m_console = nullptr;

    QString m_fontName;
    int     m_fontSize      = 18;
    int     m_lineHeight    = 19;
    QColor  m_foreColor     = QColor(192, 192, 192);
    QColor  m_backColor     = QColor(0, 0, 0);
    QColor  m_focusColor    = QColor(255, 255, 0);
    QColor  m_logColor      = QColor(192, 192, 192);
    int     m_fps           = 5;
    int     m_maxLog        = 5000;
    int     m_scrollLines   = 1;
    bool    m_sizableWindow = true;
    bool    m_maximized     = false;
    int     m_windowWidth   = 760;
    int     m_windowHeight  = 480;
    QString m_windowTitle;
    QString m_gameDirectory;
    QString m_startDirectory;
};

#endif // GUI_MANAGER_H
