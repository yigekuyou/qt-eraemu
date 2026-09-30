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
// ---------------------------------------------------------------------------
// test_gui_manager.cpp
//
// 界面（GUI）管理系统的纯逻辑单测（无 UI）：
//   1. 默认值（对齐 C# ConfigData）
//   2. 配置读入（日文键 + 英文别名 + 颜色格式）
//   3. 配置写回（saveToConfig 的键与值）
//   4. 应用到控制台（帧率 / 历史容量）
//   5. 颜色串解析 / 序列化
//   6. 保存日志（UTF-8，逐行）
//   7. 字号增减与钳制
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "gui_manager.h"
#include "config_loader.h"
#include "console_backend.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "GuiManager (界面管理) test";
    qDebug() << "==========================";

    // =====================================================================
    qDebug() << "\n1) 默认值（对齐 C# ConfigData）";
    {
        GuiManager gui;
        check(gui.fontSize() == 18, "fontSize 默认 18");
        check(gui.lineHeight() == 19, "lineHeight 默认 19");
        check(gui.fps() == 5, "fps 默认 5");
        check(gui.maxLog() == 5000, "maxLog 默认 5000");
        check(gui.scrollLines() == 1, "scrollLines 默认 1");
        check(gui.windowWidth() == 760 && gui.windowHeight() == 480, "窗口默认 760x480");
        check(gui.foreColor() == QColor(192, 192, 192), "文字色默认 #C0C0C0");
        check(gui.backColor() == QColor(0, 0, 0), "背景色默认 #000000");
        check(gui.focusColor() == QColor(255, 255, 0), "选中色默认 #FFFF00");
        check(gui.sizableWindow(), "可调高默认 true");
        check(gui.frameMs() == 200, "5 FPS -> 200ms 刷新间隔");
    }

    // =====================================================================
    qDebug() << "\n2) 配置读入（日文键 + 英文别名 + 颜色）";
    {
        ConfigLoader cfg;
        cfg.setConfig(QStringLiteral("フォント名"), QStringLiteral("Noto Sans CJK JP"));
        cfg.setConfig(QStringLiteral("フォントサイズ"), QStringLiteral("24"));
        cfg.setConfig(QStringLiteral("一行の高さ"), QStringLiteral("30"));
        cfg.setConfig(QStringLiteral("文字色"), QStringLiteral("#112233"));
        cfg.setConfig(QStringLiteral("背景色"), QStringLiteral("#000010"));
        cfg.setConfig(QStringLiteral("選択中文字色"), QStringLiteral("#00FF00"));
        cfg.setConfig(QStringLiteral("フレーム毎秒"), QStringLiteral("10"));
        cfg.setConfig(QStringLiteral("履歴ログの行数"), QStringLiteral("1234"));
        // 英文别名（尺寸会被抬到「最小窗口」之上：字号 24/行高 30 时
        // 最小高 = 25 行 × 30 + 96 = 846，所以给 900 以免被钳制掩盖别名是否生效）
        cfg.setConfig(QStringLiteral("WindowX"), QStringLiteral("1024"));
        cfg.setConfig(QStringLiteral("WindowY"), QStringLiteral("900"));

        GuiManager gui;
        gui.loadFromConfig(cfg);

        check(gui.fontName() == QStringLiteral("Noto Sans CJK JP"), "字体从日文键读入");
        check(gui.fontSize() == 24, "字号 24");
        check(gui.lineHeight() == 30, "行高 30");
        check(gui.foreColor() == QColor(0x11, 0x22, 0x33), "文字色 #112233");
        check(gui.backColor() == QColor(0x00, 0x00, 0x10), "背景色 #000010");
        check(gui.focusColor() == QColor(0, 255, 0), "选中色 #00FF00");
        check(gui.fps() == 10, "fps 10");
        check(gui.maxLog() == 1234, "maxLog 1234");
        check(gui.windowWidth() == 1024 && gui.windowHeight() == 900, "窗口 1024x900（英文别名）");
    }

    // =====================================================================
    qDebug() << "\n3) 配置写回";
    {
        GuiManager gui;
        gui.setFontSize(20);
        gui.setLineHeight(26);
        gui.setForeColor(QColor(0xAA, 0xBB, 0xCC));
        gui.setSizableWindow(false);

        ConfigLoader cfg;
        const int n = gui.saveToConfig(cfg);
        check(n > 0, "写回键数 > 0");
        check(cfg.getConfig(QStringLiteral("フォントサイズ")) == QStringLiteral("20"), "字号回写 20");
        check(cfg.getConfig(QStringLiteral("一行の高さ")) == QStringLiteral("26"), "行高回写 26");
        check(cfg.getConfig(QStringLiteral("文字色")).compare(QStringLiteral("#aabbcc"), Qt::CaseInsensitive) == 0,
              "文字色回写 #aabbcc");
        check(cfg.getConfig(QStringLiteral("ウィンドウの高さを可変にする")) == QStringLiteral("NO"),
              "可调高回写 NO");

        // 往返：写回后再读入应一致
        GuiManager again;
        again.loadFromConfig(cfg);
        check(again.fontSize() == 20 && again.lineHeight() == 26, "往返字号/行高一致");
        check(again.foreColor() == QColor(0xAA, 0xBB, 0xCC), "往返文字色一致");
    }

    // =====================================================================
    qDebug() << "\n4) 应用到控制台（帧率 / 历史容量）";
    {
        ConsoleBackend console;
        GuiManager gui;
        gui.setConsole(&console);
        gui.setFps(20);
        gui.setMaxLog(300);
        check(console.frameMs() == 50, "20 FPS -> 控制台 50ms");
        check(console.maxLog() == 300, "控制台历史容量 300");

        gui.setMaxLog(100000);
        check(console.maxLog() == 100000, "容量可增大");
    }

    // =====================================================================
    qDebug() << "\n5) 颜色解析 / 序列化";
    {
        GuiManager gui;
        check(gui.colorFromString(QStringLiteral("#ff8800")) == QColor(255, 136, 0), "#ff8800");
        check(gui.colorFromString(QStringLiteral("ff8800")) == QColor(255, 136, 0), "无#的 ff8800");
        check(gui.colorFromString(QStringLiteral("red")) == QColor(Qt::red), "颜色名 red");
        check(!gui.colorFromString(QStringLiteral("not-a-color")).isValid(), "非法颜色 -> 无效");
        check(gui.colorToString(QColor(1, 2, 3)) == QStringLiteral("#010203"), "序列化 #010203");
    }

    // =====================================================================
    qDebug() << "\n6) 保存日志（UTF-8 逐行）";
    {
        ConsoleBackend console;
        console.print(QStringLiteral("第一行"));
        console.newline();
        console.print(QStringLiteral("second line"));
        console.newline();
        console.printButton(QStringLiteral("[1] 选项"), 1);
        console.newline();

        GuiManager gui;
        gui.setConsole(&console);

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("out.log"));
        bool savedSignal = false;
        QObject::connect(&gui, &GuiManager::logSaved, [&savedSignal](const QString&) { savedSignal = true; });

        const bool ok = gui.saveLog(path);
        check(ok, "saveLog 返回 true");
        check(savedSignal, "发出 logSaved 信号");

        QFile f(path);
        check(f.open(QIODevice::ReadOnly | QIODevice::Text), "日志文件可读");
        QTextStream in(&f);
        in.setEncoding(QStringConverter::Utf8);
        const QString content = in.readAll();
        check(content.contains(QStringLiteral("第一行")), "含中文行");
        check(content.contains(QStringLiteral("second line")), "含英文行");
        check(content.contains(QStringLiteral("[1] 选项")), "含按钮文本（按钮标记不含控制符）");
    }

    // =====================================================================
    qDebug() << "\n7) 字号增减与钳制";
    {
        GuiManager gui;
        gui.resetToDefaults();
        gui.adjustFontSize(2);
        check(gui.fontSize() == 20, "18 + 2 -> 20");
        check(gui.lineHeight() > 19, "行高随字号增大");
        gui.setFontSize(4);          // 越界 -> 钳制到 6
        check(gui.fontSize() == 6, "字号下限钳制 6");
        gui.setFontSize(1000);
        check(gui.fontSize() == 128, "字号上限钳制 128");
        gui.resetToDefaults();
        check(gui.fontSize() == 18 && gui.lineHeight() == 19, "resetToDefaults 复原");
    }

    qDebug() << "\n==========================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] gui-manager tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
