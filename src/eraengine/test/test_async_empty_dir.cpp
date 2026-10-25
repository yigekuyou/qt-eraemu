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
// test_async_empty_dir.cpp —— 异步装载的「零工作量」边界（刀 56）
//
// 被钉住的缺陷（HEAD 上已复现）：`dump_warnings ecd x.txt --async` **永不返回**。
// 目录里没有 ERB 时，`EraEngine::reloadAsync` 在 `resolveGameDirs()` 失败处直接
// `emit scriptsLoaded(false)` —— 此时调用方还没进入自己的 QEventLoop，`quit()`
// 对未运行的循环是空操作，信号就此丢失，loop.exec() 永远不退出。
// 同一类空工作量的第二种表现：CSV/ERB 都在但 ERB 里 0 个文件时，同步
// `loadDirectory` 以「空集成功」返回，异步 `prepareLoad` 却留下 ok=false ->
// `loadCompleted(false)`，于是 `--async` 退出 1 且不产出输出文件。
//
// 契约（本测试逐条断言）：
//   1. 完成信号**恰好一次**，且**不在** loadAsync() 内部同步发出（否则调用方会
//      错过它并挂死）；
//   2. 「零工作量」= 成功装载空集：只有 .ERH / 1 个 ERB / 空 ERB 目录都 ok=true，
//      缺 CSV/ERB 目录的目录结构是失败（ok=false，与同步 hasError 同源）；
//   3. 同步与异步装载的告警输出逐字节相同，两条路径退出码都是 0。
//
// 六个夹具 ×（同步 / 异步）：只有 CSV（无 ERB 目录）、完全空目录、CSV+空 ERB、
// 只有 .ERH、1 个 ERB（脚本里带一条确定性告警，保证比较不是「0 == 0」）、
// 文件全被跳过（空文件 / 只有注释）。
// 完成信号走直接 API（才数得到次数）；输出/退出码走 QProcess 跑真二进制
// （端到端才能证明「返回了」）。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include "eraengine.h"
#include "erb_loader.h"

static int g_failures = 0;
static int g_assertions = 0;

static void check(bool cond, const QString& what) {
    ++g_assertions;
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

namespace {

QString g_binary;   // dump_warnings（与测试二进制同目录）

bool writeFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(data);
    return true;
}

QByteArray readAll(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

struct Fixture {
    QString name;          // 目录名
    QString dir;           // 绝对路径
    bool expectOk = true;  // 异步完成信号携带的 ok
    int expectLoaderCompletions = 0;   // ErbLoader::loadCompleted 次数
    bool expectWarnings = false;       // 输出是否非空（证明比较有意义）
};

struct RunResult {
    bool finished = false;
    int exitCode = -1;
    QByteArray out;
    QByteArray err;
};

RunResult runTool(const QStringList& args) {
    QProcess p;
    // 告警里有中文：固定 locale，字节比较才与开发机无关。
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    p.setProcessEnvironment(env);
    p.start(g_binary, args);
    RunResult r;
    if (!p.waitForStarted(10000)) return r;
    // 「永不返回」的回归必须在测试里表现为**红**（这里超时），而不是把 ctest 挂死。
    if (!p.waitForFinished(30000)) {
        p.kill();
        p.waitForFinished(5000);
        return r;
    }
    r.finished = true;
    r.exitCode = p.exitCode();
    r.out = p.readAllStandardOutput();
    r.err = p.readAllStandardError();
    return r;
}

// 直接 API：完成信号恰好一次，且只在进入事件循环之后到达。
void checkCompletionExactlyOnce(const Fixture& fx) {
    EraEngine engine;
    int scriptsLoadedCount = 0;
    bool scriptsLoadedOk = false;
    int loaderCompletions = 0;
    auto& loader = engine.getExecutionEngine()->getErbLoader();
    QObject::connect(&engine, &EraEngine::scriptsLoaded, &engine, [&](bool ok) {
        ++scriptsLoadedCount;
        scriptsLoadedOk = ok;
    });
    QObject::connect(&loader, &ErbLoader::loadCompleted, &engine,
                     [&](bool) { ++loaderCompletions; });

    engine.loadAsync(fx.dir);
    // 关键断言：发起调用内部不得同步发布完成。旧实现正是在这里发，于是
    // `connect(); loadAsync(); loop.exec()` 的调用方（dump_warnings --async）
    // 错过信号并永久挂起。
    check(scriptsLoadedCount == 0,
          QStringLiteral("%1: loadAsync() 内部不同步发布 scriptsLoaded").arg(fx.name));

    QEventLoop loop;
    bool timedOut = false;
    QObject::connect(&engine, &EraEngine::scriptsLoaded, &loop, &QEventLoop::quit);
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, [&] { timedOut = true; loop.quit(); });
    guard.start(20000);
    loop.exec();

    check(!timedOut, QStringLiteral("%1: 完成信号在事件循环内到达（未超时）").arg(fx.name));
    check(scriptsLoadedCount == 1,
          QStringLiteral("%1: scriptsLoaded 恰好一次（得到 %2 次）")
              .arg(fx.name).arg(scriptsLoadedCount));
    check(scriptsLoadedOk == fx.expectOk,
          QStringLiteral("%1: 完成结果 ok=%2（期望 %3）")
              .arg(fx.name).arg(scriptsLoadedOk).arg(fx.expectOk));
    check(loaderCompletions == fx.expectLoaderCompletions,
          QStringLiteral("%1: ErbLoader::loadCompleted %2 次（期望 %3）")
              .arg(fx.name).arg(loaderCompletions).arg(fx.expectLoaderCompletions));

    // 再转一圈事件循环：不许出现「提前发一次 + 结尾再发一次」。
    QCoreApplication::processEvents();
    check(scriptsLoadedCount == 1,
          QStringLiteral("%1: 后续事件循环里没有第二次完成").arg(fx.name));
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "async load: zero-work boundaries (sync == async, one completion)";
    qDebug() << "==================================================================";

    g_binary = QCoreApplication::applicationDirPath() + QStringLiteral("/dump_warnings");
    check(QFileInfo::exists(g_binary),
          QStringLiteral("前置：找到 dump_warnings（%1）").arg(g_binary));

    QTemporaryDir root;
    QTemporaryDir outDir;
    check(root.isValid() && outDir.isValid(), QStringLiteral("前置：临时目录可用"));
    if (!root.isValid() || !outDir.isValid()) return 1;
    const QString base = QFileInfo(root.path()).canonicalFilePath();

    // ---- 夹具（全部用 QTemporaryDir 现造，不依赖仓库里的游戏目录）----
    QList<Fixture> fixtures;
    {
        // ① 只有 CSV：没有 ERB 目录 -> resolveGameDirs 失败（与同步 hasError 同源）
        Fixture fx{QStringLiteral("onlycsv"), base + QStringLiteral("/onlycsv"), false, 0, false};
        QDir().mkpath(fx.dir + QStringLiteral("/CSV"));
        check(writeFile(fx.dir + QStringLiteral("/CSV/GameBase.csv"), ";empty\n"),
              QStringLiteral("① 写 GameBase.csv"));
        fixtures.append(fx);
    }
    {
        // ② 完全空目录 -> 同上（连 CSV 也没有）
        Fixture fx{QStringLiteral("empty"), base + QStringLiteral("/empty"), false, 0, false};
        QDir().mkpath(fx.dir);
        fixtures.append(fx);
    }
    {
        // ③ CSV 与 ERB 都在，但 ERB 里 0 个文件 —— 真正走到 ErbLoader 的零工作量。
        //    同步 loadDirectory 对空列表返回 true；异步必须同样成功（ok=true）。
        Fixture fx{QStringLiteral("emptyerb"), base + QStringLiteral("/emptyerb"), true, 1, false};
        QDir().mkpath(fx.dir + QStringLiteral("/CSV"));
        QDir().mkpath(fx.dir + QStringLiteral("/ERB"));
        check(writeFile(fx.dir + QStringLiteral("/CSV/GameBase.csv"), ";empty\n"),
              QStringLiteral("③ 写 GameBase.csv"));
        fixtures.append(fx);
    }
    {
        // ④ 只有 .ERH（一个全局 #DIM 头文件，无告警）
        Fixture fx{QStringLiteral("onlyerh"), base + QStringLiteral("/onlyerh"), true, 1, false};
        QDir().mkpath(fx.dir + QStringLiteral("/CSV"));
        QDir().mkpath(fx.dir + QStringLiteral("/ERB"));
        check(writeFile(fx.dir + QStringLiteral("/CSV/GameBase.csv"), ";empty\n"),
              QStringLiteral("④ 写 GameBase.csv"));
        check(writeFile(fx.dir + QStringLiteral("/ERB/a.ERH"), "#DIM HDRVAR, 1\n"),
              QStringLiteral("④ 写 a.ERH"));
        fixtures.append(fx);
    }
    {
        // ⑤ 1 个 ERB，带一条确定性告警（CLEAR 不是本引擎的指令）——
        //    输出非空，字节比较才有意义。
        Fixture fx{QStringLiteral("oneerb"), base + QStringLiteral("/oneerb"), true, 1, true};
        QDir().mkpath(fx.dir + QStringLiteral("/CSV"));
        QDir().mkpath(fx.dir + QStringLiteral("/ERB"));
        check(writeFile(fx.dir + QStringLiteral("/CSV/GameBase.csv"), ";empty\n"),
              QStringLiteral("⑤ 写 GameBase.csv"));
        check(writeFile(fx.dir + QStringLiteral("/ERB/a.ERB"),
                        "@MAIN\n#DIM MYV, 2\nMYV:0 = 1\nPRINTL \"hello\"\nCLEAR\nRETURN\n"),
              QStringLiteral("⑤ 写 a.ERB（含一条确定告警）"));
        fixtures.append(fx);
    }
    {
        // ⑥ 文件都在，但**全部被跳过**（空文件 / 只有注释）：total>0、merged=0
        //    是「有文件但没有脚本」，同样由 total 状态机收尾（不是另一条路径）。
        Fixture fx{QStringLiteral("blankerb"), base + QStringLiteral("/blankerb"), true, 1, false};
        QDir().mkpath(fx.dir + QStringLiteral("/CSV"));
        QDir().mkpath(fx.dir + QStringLiteral("/ERB"));
        check(writeFile(fx.dir + QStringLiteral("/CSV/GameBase.csv"), ";empty\n"),
              QStringLiteral("⑥ 写 GameBase.csv"));
        check(writeFile(fx.dir + QStringLiteral("/ERB/blank.ERB"), QByteArrayLiteral("")),
              QStringLiteral("⑥ 写空文件 blank.ERB"));
        check(writeFile(fx.dir + QStringLiteral("/ERB/comment.ERB"),
                        QByteArrayLiteral("; only a comment\n")),
              QStringLiteral("⑥ 写只有注释的 comment.ERB"));
        fixtures.append(fx);
    }

    for (const Fixture& fx : fixtures) {
        qDebug().noquote() << QStringLiteral("\n== %1 ==").arg(fx.name);

        // ---- 1. 直接 API：完成信号恰好一次、且不在发起调用内同步发出 ----
        checkCompletionExactlyOnce(fx);

        // ---- 2. QProcess：同步 / 异步都返回，退出码 0，输出逐字节相同 ----
        const QString syncOut = outDir.path() + QLatin1Char('/') + fx.name + QStringLiteral(".sync.txt");
        const QString asyncOut = outDir.path() + QLatin1Char('/') + fx.name + QStringLiteral(".async.txt");
        QFile::remove(syncOut);
        QFile::remove(asyncOut);
        const RunResult sync = runTool({fx.dir, syncOut});
        const RunResult async = runTool({fx.dir, asyncOut, QStringLiteral("--async")});
        check(sync.finished && sync.exitCode == 0,
              QStringLiteral("%1: 同步返回且退出码 0（finished=%2 rc=%3）")
                  .arg(fx.name).arg(sync.finished).arg(sync.exitCode));
        check(async.finished && async.exitCode == 0,
              QStringLiteral("%1: 异步返回且退出码 0（finished=%2 rc=%3）")
                  .arg(fx.name).arg(async.finished).arg(async.exitCode));
        const QByteArray syncBytes = readAll(syncOut);
        const QByteArray asyncBytes = readAll(asyncOut);
        check(QFileInfo::exists(syncOut) && QFileInfo::exists(asyncOut),
              QStringLiteral("%1: 两条路径都产出输出文件").arg(fx.name));
        check(syncBytes == asyncBytes,
              QStringLiteral("%1: 同步/异步输出逐字节相同（%2 / %3 字节）")
                  .arg(fx.name).arg(syncBytes.size()).arg(asyncBytes.size()));
        check(syncBytes.isEmpty() != fx.expectWarnings,
              QStringLiteral("%1: 输出内容符合预期（%2 字节）")
                  .arg(fx.name).arg(syncBytes.size()));
    }

    qDebug() << "\n================================";
    qDebug().noquote() << "断言数:" << g_assertions;
    if (g_failures == 0) qDebug() << "ALL PASS";
    else qDebug() << g_failures << "FAILURE(S)";
    return g_failures == 0 ? 0 : 1;
}