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
// test_getconfig.cpp
//
// GETCONFIG / GETCONFIGS 的取值语义（对齐 C# ConfigData.GetConfigValueInERB）：
//   1. **白名单**：只有列出的配置项能被 ERB 读出；白名单外 -> 未命中
//      （Caller: GETCONFIG -> 0 / GETCONFIGS -> ""）。白名单 = ConfigLoader
//      的那一张配置键表（键 → 默认值/类型/英文别名），GuiManager 也从它读
//      界面项 —— 窗口宽高与界面项同表，所以 ウィンドウ高さ 等 6 项可读。
//   2. 值形态按项类型：<bool> -> "1"/"0"；<Color> -> ((R*256)+G)*256+B；
//      整数/Int64 -> 数值文本；字符串/字符/描画インターフェース -> 原样
//   3. 配置项缺省时回落 C# 内建默认值（フォントサイズ 18 / 一行の高さ 19 …）
//
// 回归动机（eraTW 1,0,400,97「画像表示設定」的选项 4/5/6 看似无效）：
//   之前 GETCONFIG 完全没接线 -> 一律 0，eraTW 的像素换算全部除以 0：
//       `画像横幅 = 默认角色画像横幅 * 拡大比率 / GETCONFIG("フォントサイズ")`
//   得到 0，输出 `<img ... height='0' width='0'>`；于是「画像尺寸」的
//   档位切换（4）与拡大/縮小（5/6）改了状态却看不出任何画面变化。
//
// 用法: test_getconfig   （自建临时目录，无外部依赖）
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "config_loader.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static bool writeConfig(const QString& path, const QString& text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << text;
    return true;
}

static QString erbValue(const ConfigLoader& loader, const QString& key, bool* hit = nullptr) {
    QString out;
    const bool ok = loader.configValueInErb(key, out);
    if (hit) *hit = ok;
    return ok ? out : QString();
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "GETCONFIG / GETCONFIGS test";
    qDebug() << "==========================";

    const QString kFont   = QString::fromUtf8("フォントサイズ");
    const QString kLine   = QString::fromUtf8("一行の高さ");
    const QString kWidth  = QString::fromUtf8("ウィンドウ幅");
    const QString kIf     = QString::fromUtf8("描画インターフェース");
    const QString kAuto   = QString::fromUtf8("オートセーブを行なう");
    const QString kFore   = QString::fromUtf8("文字色");
    const QString kMoney  = QString::fromUtf8("お金の単位");
    const QString kHeight = QString::fromUtf8("ウィンドウ高さ");   // 界面项同表 -> 可读
    const QString kBook   = QString::fromUtf8("存在しない設定");   // 白名单外

    qDebug() << "\n1) 空配置：白名单命中 -> C# 内建默认值";
    {
        ConfigLoader loader;
        bool hit = false;
        check(erbValue(loader, kFont, &hit) == QStringLiteral("18") && hit,
              "GETCONFIG(フォントサイズ) 默认 18");
        check(erbValue(loader, kLine) == QStringLiteral("19"), "一行の高さ 默认 19");
        check(erbValue(loader, kWidth) == QStringLiteral("760"), "ウィンドウ幅 默认 760");
        check(erbValue(loader, kIf) == QStringLiteral("TEXTRENDERER"),
              "描画インターフェース 默认 TEXTRENDERER");
        check(erbValue(loader, kMoney) == QStringLiteral("$"), "お金の単位 默认 $");
        check(erbValue(loader, kFore) == QStringLiteral("12632256"),
              "文字色 默认 192,192,192 -> ((192*256)+192)*256+192 = 12632256");
    }

    qDebug() << "\n2) 白名单 = 键表：界面项可读，表外仍未命中（GETCONFIG -> 0）";
    {
        ConfigLoader loader;
        bool hit = true;
        // 契约变更（2026-10-25）：ウィンドウ高さ 等 6 个界面项进了同一张键表，
        // 于是也进 ERB 白名单 —— C# 的 ConfigData 同样把它们算作配置项；
        // 空配置下读到的是表内默认值（窗口默认高 480）。
        erbValue(loader, kHeight, &hit);
        check(hit && erbValue(loader, kHeight) == QStringLiteral("480"),
              "ウィンドウ高さ 可读且默认 480（与舞台缺省高同一事实）");
        check(erbValue(loader, QString::fromUtf8("フレーム毎秒")) == QStringLiteral("5"),
              "フレーム毎秒 可读，默认 5");
        check(erbValue(loader, QString::fromUtf8("履歴ログの行数")) == QStringLiteral("5000"),
              "履歴ログの行数 可读，默认 5000");
        check(erbValue(loader, QString::fromUtf8("スクロール行数")) == QStringLiteral("1"),
              "スクロール行数 可读，默认 1");
        check(erbValue(loader, QString::fromUtf8("ウィンドウの高さを可変にする")) == QStringLiteral("1"),
              "ウィンドウの高さを可変にする 可读，默认 1（bool）");
        check(erbValue(loader, QString::fromUtf8("起動時にウィンドウを最大化する")) == QStringLiteral("0"),
              "起動時にウィンドウを最大化する 可读，默认 0（bool）");
        erbValue(loader, kBook, &hit);
        check(!hit, "不存在的键 -> 未命中");
        check(!ConfigLoader::isErbConfigKey(kBook), "isErbConfigKey：白名单外 -> false");
        check(!ConfigLoader::isErbConfigKey(QStringLiteral("WindowX")),
              "英文别名不是 ERB 键（C# 只认日文键）");
        check(ConfigLoader::isErbConfigKey(kFont) && ConfigLoader::isErbConfigKey(kIf)
                  && ConfigLoader::isErbConfigKey(kFore),
              "isErbConfigKey：白名单内 -> true");
    }

    qDebug() << "\n3) 配置文件里的值覆盖默认（eraTW 风格：YES/NO 布尔、R,G,B 颜色）";
    {
        QTemporaryDir tmp;
        const QString path = tmp.filePath(QStringLiteral("emuera.config"));
        check(writeConfig(path, QString::fromUtf8(
                  ";eraTW 风格（键为日文，值为日文/数值）\n"
                  "フォントサイズ:16\n"
                  "一行の高さ:16\n"
                  "ウィンドウ幅:1400\n"
                  "ウィンドウ高さ:750\n"
                  "描画インターフェース:TEXTRENDERER\n"
                  "オートセーブを行なう:YES\n"
                  "文字色:10,20,30\n")),
              "写出 emuera.config");

        ConfigLoader loader;
        check(loader.loadConfigFile(path), "loadConfigFile 成功");
        check(erbValue(loader, kFont) == QStringLiteral("16"), "フォントサイズ 16（覆盖默认 18）");
        check(erbValue(loader, kLine) == QStringLiteral("16"), "一行の高さ 16");
        check(erbValue(loader, kWidth) == QStringLiteral("1400"), "ウィンドウ幅 1400");
        check(erbValue(loader, kIf) == QStringLiteral("TEXTRENDERER"), "描画インターフェース 文本");
        check(erbValue(loader, kAuto) == QStringLiteral("1"), "オートセーブ YES -> \"1\"（不是原样 YES）");
        check(erbValue(loader, kFore) == QStringLiteral("660510"),
              "文字色 10,20,30 -> ((10*256)+20)*256+30 = 660510");
        // 契约变更（2026-10-25）：ウィンドウ高さ 现在可读（此前恒 0 -> 修 bug）
        check(erbValue(loader, kHeight) == QStringLiteral("750"), "ウィンドウ高さ 750（可读）");
    }

    qDebug() << "\n4) 回归：eraTW 的像素换算不再除以 0"
             << "（画像横幅 = 広幅 * 比率 / GETCONFIG(フォントサイズ)）";
    {
        QTemporaryDir tmp;
        const QString path = tmp.filePath(QStringLiteral("emuera.config"));
        writeConfig(path, QString::fromUtf8("フォントサイズ:16\n一行の高さ:16\nウィンドウ幅:1400\n"));
        ConfigLoader loader;
        loader.loadConfigFile(path);

        const int fontSize = erbValue(loader, kFont).toInt();
        const int lineHeight = erbValue(loader, kLine).toInt();
        const int winWidth = erbValue(loader, kWidth).toInt();
        check(fontSize > 0, QString("フォントサイズ=%1 (>0)").arg(fontSize));
        check(lineHeight > 0, QString("一行の高さ=%1 (>0)").arg(lineHeight));
        check(winWidth > 0, QString("ウィンドウ幅=%1 (>0)").arg(winWidth));

        // eraTW OPTION_画像表示詳細設定：默认角色画像横幅=180、拡大比率=100+isModifier
        const int bannerW = 180;
        const int ratio = 100;                       // isModifier=0
        const int heightPx = bannerW * ratio / fontSize;   // 画像縦幅
        check(heightPx > 0, QString("画像縦幅 = 180*100/%1 = %2 (>0，修复前恒 0)")
                                .arg(fontSize).arg(heightPx));
        check(heightPx == 1125, "180*100/16 = 1125（对齐 Emuera 的 TextRenderer 像素）");

        // 拡大/縮小（选项 5/6）真的改变像素：+10% -> 1237
        const int ratio1 = 110;
        check(bannerW * ratio1 / fontSize != heightPx,
              QString("isModifier +10 -> %1 ≠ %2（画面尺寸才看得出变化）")
                  .arg(bannerW * ratio1 / fontSize).arg(heightPx));

        // 窗口宽超限时的回落：100*1400/1/180 = 777
        const int clamped = 100 * winWidth / 1 / bannerW;
        check(clamped > 0, QString("窗口宽限幅计算 = %1 (>0)").arg(clamped));
    }

    {
        QTemporaryDir tmp;
        const QString path = tmp.filePath(QStringLiteral("emuera.config"));
        writeConfig(path, QString::fromUtf8("フォントサイズ:21\n文字色:1,2,3\n"));
        ConfigLoader loader;
        loader.loadConfigFile(path);
        writeConfig(path, QString::fromUtf8("フォントサイズ:31\n文字色:3,2,1\n"));
        check(erbValue(loader, kFont) == "21", "runtime config reads loaded snapshot");
        loader.clearFiles(); loader.loadConfigFile(path);
        check(erbValue(loader, kFont) == "31", "explicit reload reads external changes");
        loader.setConfig(QString::fromUtf8("文字色"), "4,5,6");
        check(erbValue(loader, kFore) == "263430", "settings edit rebuilds converted values");
    }
    qDebug() << "\n==========================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] getconfig tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
