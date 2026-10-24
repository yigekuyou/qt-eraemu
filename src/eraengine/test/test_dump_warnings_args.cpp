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
// test_dump_warnings_args.cpp —— `dump_warnings` 的**参数形状**护栏（QProcess）
//
// 被钉住的 bug：outFile 位置无条件取 argv[2]，于是 `dump_warnings eraTW --jumpcheck`
// 把 `--jumpcheck` 当成输出文件名，在 cwd 造出一个同名文件（内容 = 告警列表），
// 而 jumpcheck 统计照常打到 stderr、退出码 0 —— 命令「看起来成功」，实际只留垃圾。
//
// 契约（报错文本是诊断契约，逐字断言；不含时间戳/随机量）：
//   1. `<dir> --jumpcheck`      -> 退出码 2，stderr 为固定的两行（原因 + 用法），
//                                  cwd 里**不产生任何文件**；
//   2. `<dir> x.raw --jumpcheck`-> 退出码 0，x.raw 与 `<dir> y.raw` 逐字节相同，
//                                  stderr 含 JUMPCHECK（合法用法原样保留）；
//   3. `<dir>`（无 outFile）     -> 退出码 0，stdout 与写进文件的字节相同；
//   4. 未知开关 / 缺参数 / 多余位置参数 -> 退出码 2 + 固定文本；
//   5. 其余合法开关（--var / --line / --sizeof / --kind-stats / --detail /
//      --probe / --async / 空 outFile）照旧被接受。
//      `--async` 还断言「与同步路径的告警字节相同」：它此前恒退出 1（等待条件写成了
//      `if (isLoadingScripts()) loop.exec()`，而 loadAsync 之后 ERB loader 还没开跑），
//      属于同一文件里「看起来有这条路径、实际一次也没跑通」的第二处。
//
// 语料不依赖仓库里的游戏目录：临时目录里现造一个最小 ERB/CSV 目录（一文件、
// 恰好一条确定性告警），cwd 也是临时目录 —— 「没造垃圾」是对文件列表的直接比较。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTemporaryDir>

static int g_failures = 0;
static int g_assertions = 0;

static void check(bool cond, const QString& what) {
    ++g_assertions;
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

namespace {

QString g_binary;   // dump_warnings 可执行文件（与测试二进制同目录）
QString g_gameDir;  // 临时「游戏目录」（ERB/ + CSV/）
QString g_cwd;      // 临时 cwd：护栏要证明这里不出现文件
QString g_outDir;   // 输出文件目录（与 cwd 分开，避免把输出误判成垃圾）

struct RunResult {
    bool finished = false;
    int exitCode = -1;
    QByteArray out;
    QByteArray err;
};

RunResult run(const QStringList& args) {
    QProcess process;
    process.setWorkingDirectory(g_cwd);
    // 输出侧有中文告警：固定子进程 locale，stdout/file 的字节比较才与开发机无关。
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    process.setProcessEnvironment(env);
    process.start(g_binary, args);
    RunResult r;
    if (!process.waitForStarted(10000)) return r;
    if (!process.waitForFinished(180000)) {
        process.kill();
        process.waitForFinished();
        return r;
    }
    r.finished = true;
    r.exitCode = process.exitCode();
    r.out = process.readAllStandardOutput();
    r.err = process.readAllStandardError();
    return r;
}

QStringList cwdEntries() {
    return QDir(g_cwd).entryList(QDir::AllEntries | QDir::Hidden | QDir::System
                                     | QDir::NoDotAndDotDot,
                                 QDir::Name);
}

QByteArray readAll(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    return f.readAll();
}

QString firstLine(const QByteArray& bytes) {
    return QString::fromUtf8(bytes.left(bytes.indexOf('\n')));
}

// 参数错误的两行输出（原因 + 用法）。逐字断言：诊断文本是契约。
QByteArray usageText(const QString& reason) {
    return QStringLiteral("dump_warnings: error: %1\n"
                          "usage: dump_warnings <gameDir> [outFile] [--async] [--detail] "
                          "[--jumpcheck] [--sizeof] [--probe] [--kind-stats] [--var NAME] "
                          "[--line SCRIPT IDX]\n")
        .arg(reason)
        .toUtf8();
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "dump_warnings argument shape (no files in cwd for flag typos)";
    qDebug() << "==============================================================";

    g_binary = QCoreApplication::applicationDirPath() + QStringLiteral("/dump_warnings");
    check(QFileInfo::exists(g_binary), QString("前置：找到 dump_warnings（%1）").arg(g_binary));

    QTemporaryDir gameDir;
    QTemporaryDir cwdDir;
    QTemporaryDir outDir;
    check(gameDir.isValid() && cwdDir.isValid() && outDir.isValid(), "前置：临时目录可用");
    g_gameDir = QFileInfo(gameDir.path()).canonicalFilePath();
    g_cwd = QFileInfo(cwdDir.path()).canonicalFilePath();
    g_outDir = QFileInfo(outDir.path()).canonicalFilePath();

    // 最小语料：一个脚本、一条确定性告警（CLEAR 不是本引擎的指令）。
    // 需要 ERB/ 与 CSV/ 两个子目录（EraEngine::resolveGameDirs 的前提）。
    QDir().mkpath(g_gameDir + QStringLiteral("/ERB"));
    QDir().mkpath(g_gameDir + QStringLiteral("/CSV"));
    {
        QFile script(g_gameDir + QStringLiteral("/ERB/a.ERB"));
        check(script.open(QIODevice::WriteOnly | QIODevice::Truncate), "前置：写最小 ERB 脚本");
        script.write("@MAIN\n#DIM MYV, 2\nMYV:0 = 1\nPRINTL \"hello\"\nCLEAR\nRETURN\n"
                     "$LABEL\nGOTO LABEL\n");
        script.close();
    }

    const QByteArray kClearWarning = QByteArrayLiteral("\xE6\x9C\xAA\xE8\xAF\x86\xE5\x88\xAB"
                                                       "\xE7\x9A\x84\xE6\x8C\x87\xE4\xBB\xA4: CLEAR");

    // =====================================================================
    qDebug() << "\n1) `dir --jumpcheck`：报错退出，且 cwd 里不产生任何文件";
    {
        const QStringList before = cwdEntries();
        const RunResult r = run({g_gameDir, QStringLiteral("--jumpcheck")});
        const QStringList after = cwdEntries();
        check(r.finished, "1) 进程正常结束");
        check(r.exitCode == 2,
              QString("1) 退出码非 0（得到 %1，期望 2）").arg(r.exitCode));
        check(QString::fromUtf8(r.err) == QString::fromUtf8(usageText(
                  QStringLiteral("output file must not start with \"--\": --jumpcheck"))),
              QString("1) stderr 是固定的两行报错（首行=\"%1\"）").arg(firstLine(r.err)));
        check(!QFileInfo::exists(g_cwd + QStringLiteral("/--jumpcheck")),
              "1) cwd 里没有名为 `--jumpcheck` 的文件");
        check(after == before,
              QString("1) cwd 文件列表不变（前 %1 项 / 后 %2 项）")
                  .arg(before.size()).arg(after.size()));
    }

    // =====================================================================
    qDebug() << "\n2) `dir x.raw --jumpcheck` 与 `dir y.raw`：合法用法原样保留";
    {
        const QString xRaw = g_outDir + QStringLiteral("/x.raw");
        const QString yRaw = g_outDir + QStringLiteral("/y.raw");
        const RunResult a = run({g_gameDir, xRaw, QStringLiteral("--jumpcheck")});
        check(a.finished && a.exitCode == 0,
              QString("2) 带 outFile + --jumpcheck 退出码 0（得到 %1）").arg(a.exitCode));
        check(a.err.contains("JUMPCHECK\t"), "2) stderr 含 JUMPCHECK 统计");
        check(a.err.contains("bad=0"), "2) JUMPCHECK bad=0");

        const RunResult b = run({g_gameDir, yRaw});
        check(b.finished && b.exitCode == 0,
              QString("2) 不带 --jumpcheck 退出码 0（得到 %1）").arg(b.exitCode));
        check(!b.err.contains("JUMPCHECK\t"), "2) 不带 --jumpcheck 时没有 JUMPCHECK 统计");
        const QByteArray xBytes = readAll(xRaw);
        const QByteArray yBytes = readAll(yRaw);
        check(!xBytes.isEmpty() && xBytes == yBytes,
              QString("2) x.raw 与 y.raw 逐字节相同（%1 / %2 字节）")
                  .arg(xBytes.size()).arg(yBytes.size()));
        check(xBytes.contains(kClearWarning), "2) 输出里确有那条确定性告警");
        check(QFileInfo(xRaw).size() == xBytes.size() && !xBytes.isEmpty(),
              "2) x.raw 确实被写入（非空）");
    }

    // =====================================================================
    qDebug() << "\n3) `dir`（无 outFile）：告警写 stdout，退出码 0";
    {
        const RunResult r = run({g_gameDir});
        check(r.finished && r.exitCode == 0,
              QString("3) 退出码 0（得到 %1）").arg(r.exitCode));
        check(r.out.contains(kClearWarning), "3) stdout 里有告警");
        check(r.out == readAll(g_outDir + QStringLiteral("/y.raw")),
              "3) stdout 字节 == 写进 outFile 的字节（同一份告警文本）");
        check(!r.err.contains("dump_warnings: error:"), "3) 没有报错");
    }

    // =====================================================================
    qDebug() << "\n4) 参数错误的固定文本（未知开关 / 缺参数 / 多余位置参数）";
    {
        struct Case {
            QStringList args;
            QString reason;
            QString label;
        };
        const QList<Case> cases = {
            {{g_gameDir, g_outDir + QStringLiteral("/z.raw"), QStringLiteral("--bogus")},
             QStringLiteral("unknown option: --bogus"), QStringLiteral("未知开关（开关位置）")},
            {{}, QStringLiteral("missing <gameDir>"), QStringLiteral("无 dir")},
            {{g_gameDir, g_outDir + QStringLiteral("/z.raw"), QStringLiteral("--var")},
             QStringLiteral("option --var requires a value"), QStringLiteral("--var 缺值")},
            {{g_gameDir, g_outDir + QStringLiteral("/z.raw"), QStringLiteral("--line"),
              QStringLiteral("a.ERB")},
             QStringLiteral("option --line requires SCRIPT and INDEX"),
             QStringLiteral("--line 缺 INDEX")},
            {{g_gameDir, g_outDir + QStringLiteral("/z.raw"), QStringLiteral("extra")},
             QStringLiteral("unexpected argument: extra"), QStringLiteral("多余位置参数")},
            {{g_gameDir, QStringLiteral("--async")},
             QStringLiteral("output file must not start with \"--\": --async"),
             QStringLiteral("outFile 位置写 --async")},
        };
        for (const Case& c : cases) {
            const QStringList before = cwdEntries();
            const RunResult r = run(c.args);
            check(r.finished && r.exitCode == 2,
                  QString("4) %1 -> 退出码 2（得到 %2）").arg(c.label).arg(r.exitCode));
            check(QString::fromUtf8(r.err) == QString::fromUtf8(usageText(c.reason)),
                  QString("4) %1 -> stderr 文本确定（首行=\"%2\"）")
                      .arg(c.label, firstLine(r.err)));
            check(cwdEntries() == before,
                  QString("4) %1 -> cwd 文件列表不变").arg(c.label));
        }
    }

    // =====================================================================
    qDebug() << "\n5) 其余合法开关照旧被接受";
    {
        const QString out = g_outDir + QStringLiteral("/use.raw");
        {
            const RunResult r = run({g_gameDir, out, QStringLiteral("--var"), QStringLiteral("MYV")});
            check(r.finished && r.exitCode == 0, "5) --var NAME 退出码 0");
            check(r.err.contains("VAR MYV"), "5) --var MYV 打印了声明");
        }
        {
            const RunResult r = run({g_gameDir, out, QStringLiteral("--line"),
                                     QStringLiteral("a.ERB"), QStringLiteral("1")});
            check(r.finished && r.exitCode == 0, "5) --line SCRIPT IDX 退出码 0");
            check(r.err.contains("LINE a.ERB 1"), "5) --line 打印了该行");
        }
        {
            const RunResult r = run({g_gameDir, out, QStringLiteral("--sizeof")});
            check(r.finished && r.exitCode == 0 && r.err.contains("SIZEOF\t"),
                  "5) --sizeof 退出码 0 并打印结构大小");
        }
        {
            const RunResult r = run({g_gameDir, out, QStringLiteral("--kind-stats")});
            check(r.finished && r.exitCode == 0 && r.err.contains("KIND\tTOTAL"),
                  "5) --kind-stats 退出码 0 并打印 KIND 统计");
        }
        {
            const QString detail = out + QStringLiteral(".detail");
            QFile::remove(detail);
            const RunResult r = run({g_gameDir, out, QStringLiteral("--detail")});
            check(r.finished && r.exitCode == 0, "5) --detail 退出码 0");
            check(QFileInfo::exists(detail), "5) --detail 写出 <outFile>.detail");
        }
        {
            const RunResult r = run({g_gameDir, out, QStringLiteral("--probe")});
            check(r.finished && r.exitCode == 0, "5) --probe 退出码 0");
        }
        {
            const QStringList before = cwdEntries();
            const RunResult r = run({g_gameDir, QString(), QStringLiteral("--jumpcheck")});
            check(r.finished && r.exitCode == 0,
                  QString("5) 空 outFile + --jumpcheck 退出码 0（得到 %1）").arg(r.exitCode));
            check(r.err.contains("JUMPCHECK\t"), "5) 空 outFile 时开关照样生效");
            check(cwdEntries() == before, "5) 空 outFile 不写文件、不留垃圾");
        }
        {
            // --async 是异步装载路径：告警集合必须与同步路径一致。
            const RunResult r = run({g_gameDir, QString(), QStringLiteral("--async")});
            check(r.finished && r.exitCode == 0,
                  QString("5) --async 退出码 0（得到 %1）").arg(r.exitCode));
            check(r.out == readAll(g_outDir + QStringLiteral("/y.raw")),
                  "5) --async 的告警字节与同步路径相同");
        }
    }

    // =====================================================================
    qDebug() << "\n================================";
    qDebug().noquote() << "断言数:" << g_assertions;
    if (g_failures == 0) qDebug() << "ALL PASS";
    else qDebug() << g_failures << "FAILURE(S)";
    return g_failures == 0 ? 0 : 1;
}
