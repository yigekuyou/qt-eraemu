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
// test_sjis_game.cpp
//
// 端到端验证「非 UTF-8（Shift-JIS）游戏目录」：
//   1. Shift-JIS 的 emuera.config / GameBase.csv / *_ERB 都能被正确读出
//   2. 游戏内部（UTF-8）看到的日文标识符/标签/变量名正确（无乱码）
//   3. 常量名表（SJIS CSV）里的日文名可以映射为下标
//   4. 写回：UTF-8+BOM 保真；写成 Shift-JIS 时不可表示字符退化为 '?'
//
// 用法: test_sjis_game   （自建临时游戏目录，无外部依赖）
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include "eraengine.h"
#include "text_encoding.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static bool writeSjis(const QString& path, const QString& text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    return TextCodecUtil::writeFile(path, text, TextEncoding::ShiftJis);
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Shift-JIS game directory test";
    qDebug() << "=============================";

    QTemporaryDir tmp;
    const QString gameDir = tmp.filePath(QStringLiteral("game"));
    const QString csvDir = gameDir + QStringLiteral("/CSV");
    const QString erbDir = gameDir + QStringLiteral("/ERB");
    QDir().mkpath(csvDir);
    QDir().mkpath(erbDir);

    qDebug() << "\n1) 造一个 Shift-JIS 的小游戏";
    // emuera.config（Shift-JIS，key:value）
    check(writeSjis(gameDir + QStringLiteral("/emuera.config"),
                    QString::fromUtf8(";コメント\n"
                                      "ウィンドウ幅:1400\n"
                                      "サブディレクトリを検索する:YES\n"
                                      "TextEncoding:AUTO\n")),
          "写出 Shift-JIS emuera.config");
    // GameBase.csv（Shift-JIS）
    check(writeSjis(csvDir + QStringLiteral("/GameBase.csv"),
                    QString::fromUtf8("コード,7153\n"
                                      "バージョン,1\n"
                                      "タイトル,日本語のゲーム\n"
                                      "作者,テスト\n")),
          "写出 Shift-JIS GameBase.csv");
    // 常量名表（Shift-JIS）：名前 -> 行号
    check(writeSjis(csvDir + QStringLiteral("/FLAG.csv"),
                    QString::fromUtf8(";フラグ定義\n"
                                      "時間停止,0\n"
                                      "現在位置,0\n"
                                      "ようこそ,0\n")),
          "写出 Shift-JIS FLAG.csv");
    check(writeSjis(csvDir + QStringLiteral("/VariableSize.csv"),
                    QString::fromUtf8("FLAG,10000\n")),
          "写出 Shift-JIS VariableSize.csv");
    // ERB（Shift-JIS）：日文标签 + 日文变量 + 日文常量名下标
    check(writeSjis(erbDir + QStringLiteral("/MAIN.ERB"),
                    QString::fromUtf8("@SYSTEM_TITLE\n"
                                      "#DIM 個数\n"
                                      "#DIMS 挨拶\n"
                                      "個数 = 3\n"
                                      "挨拶 = \"こんにちは\"\n"
                                      "FLAG:現在位置 = 個数\n"
                                      "PRINTFORML %挨拶% 世界\n"
                                      "RETURN\n")),
          "写出 Shift-JIS MAIN.ERB");

    qDebug() << "\n2) 装载（Sync）";
    EraEngine engine;
    engine.setGameDirectory(gameDir);

    check(engine.getParseTable()->scriptNames().size() > 0, "脚本已装载");
    check(engine.getParseTable()->parseWarningCount() == 0,
          QString("告警 0（得到 %1）%2")
              .arg(engine.getParseTable()->parseWarningCount())
              .arg(engine.getParseTable()->parseWarningCount() > 0
                       ? engine.getParseTable()->parseWarnings().first() : QString()));
    check(engine.erbDir().endsWith(QLatin1String("ERB")), "ERB 目录解析正确");
    check(engine.csvDir().endsWith(QLatin1String("CSV")), "CSV 目录解析正确");

    qDebug() << "\n3) 日文内容无乱码";
    check(engine.getConfig(QString::fromUtf8("ウィンドウ幅")) == QStringLiteral("1400"),
          QString("SJIS 配置项读入正确（ウィンドウ幅=%1）")
              .arg(engine.getConfig(QString::fromUtf8("ウィンドウ幅"))));
    check(engine.searchSubdirectory(), "サブディレクトリを検索する=YES -> true");
    {
        // 变量表里应能看到日文变量名
        bool hasKosuu = false, hasAisatsu = false;
        for (const VariableDecl& d : engine.getParseTable()->variableTable().declarations()) {
            if (d.name == QString::fromUtf8("個数")) hasKosuu = true;
            if (d.name == QString::fromUtf8("挨拶") && d.type == OperandType::Str) hasAisatsu = true;
        }
        check(hasKosuu, "#DIM 個数 解析成功（日文变量名）");
        check(hasAisatsu, "#DIMS 挨拶 解析成功且为字符串型");
    }
    {
        // @SYSTEM_TITLE 标签（日文游戏里常有日文函数名，此处验英文标签 + SJIS 文本）
        // 脚本名 = **相对游戏目录的路径（含扩展名）**，对齐 C# Config.GetFiles 的
        // KeyValuePair<相対パス, 完全パス>；不再是 basename（否则同名 .ERB 会互相覆盖）。
        QString mainName;
        for (const QString& n : engine.getParseTable()->scriptNames()) {
            if (n.endsWith(QLatin1String("MAIN.ERB"))) { mainName = n; break; }
        }
        check(!mainName.isEmpty(),
              QStringLiteral("脚本名是相对路径且含扩展名（实得 %1）").arg(mainName));
        const ScriptData* sd = engine.getParseTable()->script(mainName);
        bool found = false;
        if (sd) {
            for (const LogicalLine& l : sd->lines) {
                if (l.kind == LineKind::FunctionLabel && l.labelName == QStringLiteral("SYSTEM_TITLE")) {
                    found = true;
                }
            }
        }
        check(found, "@SYSTEM_TITLE 标签存在");
    }
    check(engine.constantTable().indexOf(QStringLiteral("FLAG.csv"),
                                            QString::fromUtf8("現在位置")) == 1,
          "SJIS CSV 常量名 現在位置 -> 1");

    qDebug() << "\n4) 写回编码";
    {
        const QString utf8Path = tmp.filePath(QStringLiteral("out_utf8.config"));
        TextCodecUtil::writeFile(utf8Path,
                                 QString::fromUtf8("タイトル:日本語と中文混在\n"),
                                 TextEncoding::Utf8Bom);
        TextEncoding det = TextEncoding::Auto;
        const QString back = TextCodecUtil::readFile(utf8Path, TextEncoding::Auto, &det);
        check(det == TextEncoding::Utf8Bom && back == QString::fromUtf8("タイトル:日本語と中文混在\n"),
              "UTF-8+BOM 完整保真（中日文都不丢）");

        // 注意：JIS X 0208 收录了大量汉字（中文/混在 都能表示），
        // 真正不可表示的是简体专用字（标/题/你…）与 emoji。
        const QString sjisOk = QString::fromUtf8("タイトル:日本語と中文混在\n");
        const QString sjisPath = tmp.filePath(QStringLiteral("out_sjis.config"));
        check(TextCodecUtil::writeFile(sjisPath, sjisOk, TextEncoding::ShiftJis),
              "SJIS 可完整表示 -> 写入成功");
        const QString back2 = TextCodecUtil::readFile(sjisPath, TextEncoding::Auto, &det);
        check(det == TextEncoding::ShiftJis, "SJIS 写回被正确嗅探");
        check(back2 == sjisOk, QString("SJIS 写回往返一致（得到 %1）").arg(back2.trimmed()));

        // 不可表示 => 拒绝写入（而不是写出被悄悄改坏的文件）
        const QString sjisBad = QString::fromUtf8("タイトル:日本語と标题\n");
        const QString badPath = tmp.filePath(QStringLiteral("out_sjis_bad.config"));
        check(!TextCodecUtil::writeFile(badPath, sjisBad, TextEncoding::ShiftJis),
              "含简体专用字 -> writeFile 拒绝写入");
        check(!QFile::exists(badPath), "拒绝时不留半成品文件");

        // encode 仍可拿到「尽力而为」的字节（替换字节由 ICU 决定），但 ok=false
        bool encOk = true;
        const QByteArray replaced = TextCodecUtil::encode(QString::fromUtf8("A标题B"),
                                                          TextEncoding::ShiftJis, &encOk);
        check(!encOk && !replaced.isEmpty() && replaced.startsWith('A') && replaced.endsWith('B'),
              QString("不可表示 -> ok=false 且给出替换字节（得到 %1）")
                  .arg(QString::fromLatin1(replaced.toHex())));
    }

    qDebug() << "\n5) GBK（简中）游戏目录：靠 ROM 探测决定回退编码";
    if (!TextCodecUtil::hasCodec(QStringLiteral("GB18030"))) {
        qDebug().noquote() << "   [skip] 本机 Qt 未编入 ICU（无 GB18030 转换器）";
    } else {
        QTemporaryDir tmp2;
        const QString g = tmp2.filePath(QStringLiteral("gbkGame"));
        const QString gCsv = g + QStringLiteral("/CSV");
        const QString gErb = g + QStringLiteral("/ERB");
        QDir().mkpath(gCsv);
        QDir().mkpath(gErb);
        // 注意：这里**故意不写** 内部で使用する東アジア言語 —— 让 ROM 探测自己判断
        TextCodecUtil::writeFile(g + QStringLiteral("/emuera.config"),
                                 QString::fromUtf8("; 中文游戏的配置（键用 ASCII，值用中文）\n"
                                                   "WindowWidth:1024\n"
                                                   "追加信息:中文游戏的标题\n"),
                                 TextEncoding::Gbk);
        TextCodecUtil::writeFile(gCsv + QStringLiteral("/GameBase.csv"),
                                 QString::fromUtf8("コード,1\nタイトル,中文游戏\n"), TextEncoding::Gbk);
        for (int i = 0; i < 6; ++i) {
            TextCodecUtil::writeFile(QStringLiteral("%1/ERB/S%2.ERB").arg(g).arg(i),
                                     QString::fromUtf8("@函数%1\nPRINTFORML 你好，欢迎来到这个世界。请开始冒险吧。\n")
                                         .arg(i),
                                     TextEncoding::Gbk);
        }
        TextCodecUtil::writeFile(gErb + QStringLiteral("/MAIN.ERB"),
                                 QString::fromUtf8("@SYSTEM_TITLE\n"
                                                   "#DIM 数量\n"
                                                   "#DIMS 问候\n"
                                                   "数量 = 3\n"
                                                   "问候 = \"你好，世界\"\n"
                                                   "PRINTFORML %问候%\n"
                                                   "RETURN\n"),
                                 TextEncoding::Gbk);

        EraEngine gbkEngine;
        gbkEngine.setGameDirectory(g);

        check(gbkEngine.probedEncodingName() == QStringLiteral("GB18030")
                  || !gbkEngine.probedEncoding().ran,
              QString("ROM 探测结论（落在 GB18030 或未探测）: %1")
                  .arg(gbkEngine.probedEncodingName()));
        check(gbkEngine.getParseTable()->parseWarningCount() == 0,
              QString("告警 0（得到 %1）%2")
                  .arg(gbkEngine.getParseTable()->parseWarningCount())
                  .arg(gbkEngine.getParseTable()->parseWarningCount() > 0
                           ? gbkEngine.getParseTable()->parseWarnings().first() : QString()));
        bool hasShuliang = false, hasWenhou = false;
        for (const VariableDecl& d : gbkEngine.getParseTable()->variableTable().declarations()) {
            if (d.name == QString::fromUtf8("数量")) hasShuliang = true;
            if (d.name == QString::fromUtf8("问候") && d.type == OperandType::Str) hasWenhou = true;
        }
        check(hasShuliang, "#DIM 数量 解析成功（GBK 中文变量名）");
        check(hasWenhou, "#DIMS 问候 解析成功且为字符串型");
    }

    qDebug() << "\n=============================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] sjis game tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
