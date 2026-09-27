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
// test_async_load.cpp
//
// 验证「目录限定 + 异步装载（ERB -> AST）」：
//   1. 只从 ERB 目录装载（不遍历游戏根目录）
//   2. CSV 只从 CSV 目录读取（常量名表 / VariableSize）
//   3. 异步：loadAsync() 立即返回，事件循环中收到进度与完成信号
//   4. 完成后 finalizeParse 已执行（告警/变量数可查）
//
// 用法: test_async_load <gameDir>
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QTimer>

#include "eraengine.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        qWarning() << "usage: test_async_load <gameDir>";
        return 2;
    }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    if (!QDir(dir).exists()) {
        qWarning() << "目录不存在:" << dir;
        return 2;
    }

    qDebug() << "Async ERB->AST load test";
    qDebug() << "=======================";

    EraEngine engine;

    int progressEvents = 0;
    int lastProcessed = 0, lastTotal = 0;
    bool done = false;
    bool completedOk = false;
    bool startedSignalled = false;

    QObject::connect(&engine, &EraEngine::scriptsLoadStarted, [&] { startedSignalled = true; });
    QObject::connect(&engine, &EraEngine::scriptsLoadProgress, [&](int p, int t) {
        ++progressEvents;
        lastProcessed = p;
        lastTotal = t;
    });
    QObject::connect(&engine, &EraEngine::scriptsLoaded, [&](bool ok) {
        completedOk = ok;
        done = true;
    });

    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, [&] { done = true; });
    guard.start(300000);   // 5 分钟上限

    qDebug() << "\n1) 异步启动";
    engine.loadAsync(dir);
    check(startedSignalled, "立即收到 scriptsLoadStarted");
    check(!done, "loadAsync() 返回时装载尚未完成（不阻塞）");
    check(engine.isLoadingScripts(), "isLoadingScripts() == true");

    qDebug() << "\n2) 目录限定";
    check(!engine.erbDir().isEmpty(), QString("ERB 目录已解析: %1").arg(engine.erbDir()));
    check(!engine.csvDir().isEmpty(), QString("CSV 目录已解析: %1").arg(engine.csvDir()));
    check(QDir(engine.erbDir()).exists(), "ERB 目录存在");
    check(engine.erbDir().endsWith(QLatin1String("ERB"), Qt::CaseInsensitive),
          "ERB 目录就是 ERB/（不是游戏根目录）");

    qDebug() << "\n3) 事件循环等待完成";
    QEventLoop loop;
    QObject::connect(&engine, &EraEngine::scriptsLoaded, &loop, &QEventLoop::quit);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    loop.exec();

    check(done && completedOk, "scriptsLoaded(true)");
    check(!engine.isLoadingScripts(), "装载结束后 isLoadingScripts() == false");
    check(progressEvents > 0, QString("收到进度信号 %1 次").arg(progressEvents));
    check(lastTotal > 0 && lastProcessed == lastTotal,
          QString("进度到达终值 %1/%2").arg(lastProcessed).arg(lastTotal));

    qDebug() << "\n4) 装载结果（finalizeParse 已执行）";
    const int scripts = engine.getParseTable()->scriptNames().size();
    const int vars = engine.getParseTable()->variableTable().count();
    const int warns = engine.getParseTable()->parseWarningCount();
    qDebug().noquote() << QString("  脚本 %1 / 变量 %2 / 告警 %3").arg(scripts).arg(vars).arg(warns);
    check(scripts > 0, "脚本数 > 0");
    check(vars > 0, "变量表非空（说明变量声明已解析）");

    qDebug() << "\n5) CSV 常量名表（只来自 CSV 目录）";
    const ConstantTable& ct = engine.constantTable();
    qDebug().noquote() << QString("  表 %1 / 名字项 %2").arg(ct.tableCount()).arg(ct.nameCount());
    check(ct.tableCount() > 0, "常量表非空");
    // 往返一致：indexOf(nameAt(i)) == i
    {
        const QStringList tables = ct.tableNames();
        const QString t0 = tables.isEmpty() ? QString() : tables.first();
        const int n0 = ct.count(t0);
        bool roundTrip = n0 > 0;
        for (int i = 0; i < n0 && roundTrip; ++i) {
            const QString name = ct.nameAt(t0, i);
            // 表内可能有重名（如 Chara CSV），故断言「查回来的名字一致」而非「下标一致」
            const int back = ct.indexOf(t0, name);
            if (back < 0 || ct.nameAt(t0, back) != name) roundTrip = false;
        }
        check(roundTrip, QString("表 %1 的 nameAt/indexOf 一致（%2 项）").arg(t0).arg(n0));
    }
    check(ct.indexOf(QStringLiteral("NO.SUCH.TABLE"), QStringLiteral("x")) == -1,
          "不存在的表 -> -1");
    check(ConstantTable::csvForVariable(QStringLiteral("CFLAG")) == QStringLiteral("CFLAG.CSV"),
          "CFLAG -> CFLAG.CSV");
    check(ConstantTable::csvForVariable(QStringLiteral("NOSUCH")) == QString(),
          "未知变量 -> 无对应表");

    qDebug() << "\n6) 入口点（来自 AST，不二次扫盘）";
    qDebug().noquote() << QString("  SYSTEM=%1 SYSTEM_TITLE=%2")
                              .arg(engine.getSystemEntryPoint(), engine.getSystemTitleEntry());
    check(!engine.getSystemEntryPoint().isEmpty() || !engine.getSystemTitleEntry().isEmpty(),
          "找到 SYSTEM / SYSTEM_TITLE 入口");

    qDebug() << "\n=======================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] async load tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
