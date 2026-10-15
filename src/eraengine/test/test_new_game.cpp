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
// test_new_game.cpp
//
// 「[0] 从头开始」必须登记初始角色（对齐 C# Process.SystemProc.cs endOpenning）：
//
//     vEvaluator.ResetData();
//     vEvaluator.AddCharacterFromCsvNo(0);
//     if (gamebase.DefaultCharacter > 0) vEvaluator.AddCharacterFromCsvNo(DefaultCharacter);
//
// 回归点：EraEngine 的 SystemHost 此前**从未接线** addCharacterFromCsvNo /
// defaultCharacter —— 状态机里的 `if (m_host.addCharacterFromCsvNo)` 恒为假，
// 一个角色都没登记，CHARANUM 停在 0；于是脚本惯例的
//
//     ;あなた登録
//     DELCHARA 0            ; 删掉开局就存在的 0 号角色
//     CALL C_ADD_CHARA(0)
//
// 直接报「DELCHARA 的番号超出角色范围: 0」（真实汉化游戏 SYSTEM.ERB:58 即此）。
//
// 用法: test_new_game   （自建临时游戏目录，无外部依赖）
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "eraengine.h"
#include "variable_storage.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static bool writeText(const QString& path, const QString& text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write(text.toUtf8()) >= 0;
}

// 造一个「没有 @SYSTEM_TITLE」的小游戏：装载后停在标准标题画面等输入，
// 选 [0] 从头开始才会走 endOpenning 的登记逻辑。
static bool makeGame(const QString& dir, const QString& gameBaseExtra) {
    return writeText(dir + QStringLiteral("/CSV/GameBase.csv"),
                     QString::fromUtf8("コード,1\nバージョン,1\nタイトル,t\n") + gameBaseExtra)
        && writeText(dir + QStringLiteral("/ERB/MAIN.ERB"),
                     QString::fromUtf8("@EVENTFIRST\nRETURN\n"));
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "New game initial character test";
    qDebug() << "==============================";

    qDebug() << "\n1) 默认：开局登记 0 号角色 -> CHARANUM == 1";
    {
        QTemporaryDir tmp;
        const QString dir = tmp.filePath(QStringLiteral("game"));
        check(makeGame(dir, QString()), "造出无 @SYSTEM_TITLE 的小游戏");

        EraEngine engine;
        engine.setGameDirectory(dir);
        engine.runSystem();      // 从 Title_Begin 起步 -> 标准标题画面 -> 等输入
        check(engine.getVariableStorage()->charaNum() == 0,
              QString("开局前 CHARANUM == 0（得到 %1）").arg(engine.getVariableStorage()->charaNum()));
        check(engine.defaultTitleVisible(), "无 @SYSTEM_TITLE -> 翻开标准标题画面");

        engine.chooseTitle(0);   // [0] 从头开始

        const int n = engine.getVariableStorage()->charaNum();
        check(n == 1, QString("开局后 CHARANUM == 1（得到 %1）").arg(n));
        // 0 号角色存在 => 脚本惯例的 DELCHARA 0 不再越界
        check(engine.getVariableStorage()->charaCsvNo(0) == 0,
              QString("第 0 个角色来自 CSV 编号 0（得到 %1）")
                  .arg(engine.getVariableStorage()->charaCsvNo(0)));
    }

    qDebug() << "\n2) GameBase.csv「最初からいるキャラ」> 0 -> 再登记一个";
    {
        QTemporaryDir tmp;
        const QString dir = tmp.filePath(QStringLiteral("game"));
        check(makeGame(dir, QString::fromUtf8("最初からいるキャラ,1\n")),
              "GameBase.csv 声明 最初からいるキャラ:1");

        EraEngine engine;
        engine.setGameDirectory(dir);
        engine.runSystem();
        engine.chooseTitle(0);

        const int n = engine.getVariableStorage()->charaNum();
        check(n == 2, QString("开局后 CHARANUM == 2（0 号 + 默认角色；得到 %1）").arg(n));
        check(engine.getVariableStorage()->charaCsvNo(1) == 1,
              QString("第 1 个角色来自 CSV 编号 1（得到 %1）")
                  .arg(engine.getVariableStorage()->charaCsvNo(1)));
    }

    // -----------------------------------------------------------------------
    // 3) 角色 CSV 的「发现范围」（对齐 C# Config.GetFiles(csvDir, "CHARA*.CSV")）
    //
    // 回归点：旧实现固定拼 `<Csv>/Chara` 且不递归，eraH 这种把角色 CSV 按种族
    // 放在 CSV/人間/**・CSV/悪魔/** 的游戏一个角色都读不到 -> EXISTCSV(0) 为假
    // -> C_ADD_CHARA(0) 直接 return 2 -> CHARANUM 停在 0 -> 战斗队伍为空
    //（委托 11 的战斗开场即「全灭」）。
    // -----------------------------------------------------------------------
    const auto charaCsv = [](const QString& name) {
        return QString::fromUtf8("番号,0\n名前,") + name + QString::fromUtf8("\n呼び名,") + name
               + QString::fromUtf8("\n");
    };

    qDebug() << "\n3) 角色 CSV 布局：顶层 CSV/Chara*.csv 与 CSV/Chara/*.csv（eraTW 式）";
    {
        // 顶层 Chara*.csv：无论是否检索子目录都应当装载
        QTemporaryDir tmp;
        const QString dir = tmp.filePath(QStringLiteral("game"));
        check(makeGame(dir, QString()), "造出小游戏");
        check(writeText(dir + QStringLiteral("/CSV/Chara0.csv"), charaCsv(QStringLiteral("あ"))),
              "写入顶层 CSV/Chara0.csv");
        EraEngine engine;
        engine.setGameDirectory(dir);
        check(!engine.searchSubdirectory(), "未配置 -> SearchSubdirectory 为假");
        check(engine.getVariableStorage()->existCsv(0),
              "顶层 CSV/Chara0.csv -> EXISTCSV(0) 为真（不依赖子目录检索）");
    }
    {
        // eraTW 式：CSV/Chara/Chara*.csv + 检索子目录:YES
        QTemporaryDir tmp;
        const QString dir = tmp.filePath(QStringLiteral("game"));
        check(makeGame(dir, QString()), "造出小游戏");
        check(writeText(dir + QStringLiteral("/CSV/Chara/Chara0.csv"), charaCsv(QStringLiteral("あ"))),
              "写入 CSV/Chara/Chara0.csv");
        check(writeText(dir + QStringLiteral("/CSV/_fixed.config"),
                        QString::fromUtf8("サブディレクトリを検索する:YES\n")),
              "日文键 サブディレクトリを検索する:YES（eraTW 就是这样）");
        EraEngine engine;
        engine.setGameDirectory(dir);
        check(engine.getVariableStorage()->existCsv(0), "CSV/Chara/ + 检索子目录 -> EXISTCSV(0) 为真");
    }

    qDebug() << "\n4) 角色 CSV 布局：CSV/<种族>/<分类>/Chara*.csv（eraH 式）+ 中文键「检索子目录」";
    {
        QTemporaryDir tmp;
        const QString dir = tmp.filePath(QStringLiteral("game"));
        check(makeGame(dir, QString()), "造出小游戏");
        check(writeText(dir + QStringLiteral("/CSV/人間/00_人間/Chara0000_あなた.csv"),
                        charaCsv(QStringLiteral("あなた"))),
              "写入两层子目录里的角色 CSV");
        check(writeText(dir + QStringLiteral("/CSV/_fixed.config"),
                        QString::fromUtf8("检索子目录:YES\n")),
              "配置用中文键 检索子目录:YES（汉化版 era 游戏就是这样）");
        EraEngine engine;
        engine.setGameDirectory(dir);
        check(engine.searchSubdirectory(), "中文键被识别 -> SearchSubdirectory 为真");
        check(engine.getVariableStorage()->existCsv(0),
              "嵌套子目录 + 中文键 -> EXISTCSV(0) 为真");
    }

    qDebug() << "\n5) 关掉子目录检索时应当只扫顶层（不误报）";
    {
        QTemporaryDir tmp;
        const QString dir = tmp.filePath(QStringLiteral("game"));
        check(makeGame(dir, QString()), "造出小游戏");
        check(writeText(dir + QStringLiteral("/CSV/人間/00_人間/Chara0000_あなた.csv"),
                        charaCsv(QStringLiteral("あなた"))),
              "写入两层子目录里的角色 CSV");
        check(writeText(dir + QStringLiteral("/CSV/_fixed.config"),
                        QString::fromUtf8("サブディレクトリを検索する:NO\n")),
              "日文键 サブディレクトリを検索する:NO");
        EraEngine engine;
        engine.setGameDirectory(dir);
        check(!engine.searchSubdirectory(), "SearchSubdirectory 为假");
        check(!engine.getVariableStorage()->existCsv(0),
              "不检索子目录 -> 嵌套角色 CSV 不装载（与 C# 的 toponly 一致）");
    }

    qDebug() << "\n==============================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] new game tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
