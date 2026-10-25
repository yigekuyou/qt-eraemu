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
// 告警导出工具：装载一个游戏目录的全部脚本，把 parseTable 的所有告警
// 按「脚本:行号: 文本」写到 stdout（或 --out 指定的文件），便于逐条核对。
//
// 用法：
//   dump_warnings <gameDir> [outFile] [--async] [--detail] [--jumpcheck]
//                 [--sizeof] [--probe] [--kind-stats] [--var NAME] [--line SCRIPT IDX]
//
// 位置带隐含含义的地方**只有两处**：argv[1] 是游戏目录，argv[2]（若存在且不以
// `--` 开头）是输出文件。此后每个词都必须是一个已知的 `--开关`（`--var` / `--line`
// 后面跟它们的参数）。outFile 位置上的 `--xxx` 是**错误**而不是文件名 ——
// 旧实现无条件把它当输出文件：`dump_warnings eraTW --jumpcheck` 于是把告警写进
// cwd 下一个名叫 `--jumpcheck` 的文件，而 jumpcheck 照常执行、退出码 0，
// 命令「看起来成功」，实际只留下一个垃圾文件。
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPair>
#include <QStringList>
#include "eraengine.h"
#include "jump_markings.h"   // jumpmark::countBadJumpTargets（--jumpcheck 探针）

namespace {

// 参数错误统一退出码（与旧实现的「无参数」路径一致）。
constexpr int kUsageErrorExit = 2;

const char kUsage[] =
    "usage: dump_warnings <gameDir> [outFile] [--async] [--detail] [--jumpcheck] "
    "[--sizeof] [--probe] [--kind-stats] [--var NAME] [--line SCRIPT IDX]";

// 报错文本是**契约**（test_dump_warnings_args 逐字断言）：一行原因 + 一行用法，
// 不含时间戳 / 随机量 / 环境相关内容。
int usageError(const QString& reason) {
    fprintf(stderr, "dump_warnings: error: %s\n%s\n", reason.toUtf8().constData(), kUsage);
    return kUsageErrorExit;
}

struct Options {
    QString dir;
    QString outPath;
    bool async = false;
    bool detail = false;
    bool jumpcheck = false;
    bool sizeofProbe = false;
    bool probe = false;
    bool kindStats = false;
    QStringList vars;                 // --var NAME（可重复）
    QList<QPair<QString, int>> lines; // --line SCRIPT INDEX（可重复）
};

// 参数形状的**唯一**解析点。返回 false 表示参数错误（已打印报错）。
bool parseOptions(const QStringList& args, Options& opt, int& exitCode) {
    if (args.isEmpty()) {
        exitCode = usageError(QStringLiteral("missing <gameDir>"));
        return false;
    }
    opt.dir = args.at(0);
    int i = 1;
    // argv[2]：输出文件。以 `--` 开头的词**不是**文件名（旧实现正是在这里
    // 把 `--jumpcheck` 当成了输出文件）。要让开关在没有输出文件时生效，
    // 把 outFile 写成空串：`dump_warnings <dir> "" --jumpcheck`。
    if (i < args.size()) {
        if (args.at(i).startsWith(QLatin1String("--"))) {
            exitCode = usageError(QStringLiteral("output file must not start with \"--\": %1")
                                      .arg(args.at(i)));
            return false;
        }
        opt.outPath = args.at(i);
        ++i;
    }
    for (; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a == QLatin1String("--async")) { opt.async = true; continue; }
        if (a == QLatin1String("--detail")) { opt.detail = true; continue; }
        if (a == QLatin1String("--jumpcheck")) { opt.jumpcheck = true; continue; }
        if (a == QLatin1String("--sizeof")) { opt.sizeofProbe = true; continue; }
        if (a == QLatin1String("--probe")) { opt.probe = true; continue; }
        if (a == QLatin1String("--kind-stats")) { opt.kindStats = true; continue; }
        if (a == QLatin1String("--var")) {
            if (i + 1 >= args.size()) {
                exitCode = usageError(QStringLiteral("option --var requires a value"));
                return false;
            }
            opt.vars.append(args.at(++i));
            continue;
        }
        if (a == QLatin1String("--line")) {
            if (i + 2 >= args.size()) {
                exitCode = usageError(QStringLiteral("option --line requires SCRIPT and INDEX"));
                return false;
            }
            const QString script = args.at(++i);
            const int index = args.at(++i).toInt();
            opt.lines.append({script, index});
            continue;
        }
        if (a.startsWith(QLatin1String("--"))) {
            exitCode = usageError(QStringLiteral("unknown option: %1").arg(a));
            return false;
        }
        // 多余的位置参数：旧实现静默忽略（与「写错文件名」同类的坑）。
        exitCode = usageError(QStringLiteral("unexpected argument: %1").arg(a));
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QStringList rawArgs;
    rawArgs.reserve(argc > 0 ? argc - 1 : 0);
    for (int i = 1; i < argc; ++i) rawArgs.append(QString::fromLocal8Bit(argv[i]));
    Options opt;
    int usageExit = 0;
    if (!parseOptions(rawArgs, opt, usageExit)) return usageExit;
    const QString dir = opt.dir;
    const QString outPath = opt.outPath;

    QElapsedTimer timer;
    timer.start();

    EraEngine engine;
    if (opt.async) {
        QEventLoop loop;
        bool ok = false;
        QObject::connect(&engine, &EraEngine::scriptsLoaded, &loop, [&](bool loaded) {
            ok = loaded;
            loop.quit();
        });
        engine.loadAsync(dir);
        // 这里必须**无条件**进事件循环等 scriptsLoaded：loadAsync 先把 CSV 准备
        // 丢到后台未来（QtConcurrent），此刻 ErbLoader 还没开跑，isLoadingScripts()
        // 恒 false —— 旧写法 `if (isLoadingScripts()) loop.exec()` 于是直接跳过，
        // ok 永远是 false，`--async` 恒退出 1 且不输出任何告警。
        // 完成信号由 EraEngine::completeAsyncLoad 保证在返回事件循环之后投递，
        // 因此「发起调用内部同步完成」的窗口（目录里没有 ERB / 没有游戏目录结构）
        // 不会再丢掉信号、把本进程挂死。
        loop.exec();
        if (!ok) {
            // 装载没成功（例如目录里没有 CSV/ERB）：同步路径（setGameDirectory ->
            // reload()）同样只留一条日志，然后照样输出「当前解析表」（空表）并
            // 退出 0。两条路径的输出/退出码必须逐字节一致（--async 只是装载方式
            // 不同，不是另一套判定），所以这里不提前 return。
            qWarning() << "dump_warnings: 异步装载未成功，按当前解析表（可能为空）输出";
        }
    } else {
        engine.setGameDirectory(dir);
    }

    const bool detail = opt.detail;
    const int n = engine.getParseTable()->parseWarningCount();
    const QStringList warns = engine.getParseTable()->parseWarnings();
    const int vars = engine.getParseTable()->variableTable().count();
    if (detail) {
        // 明细视图：同一文本视图下可能有多条结构化诊断（severity/code 不同），
        // 用于排查「同一行告警重复出现」到底是哪个生产者。
        const auto& diags = engine.getParseTable()->parseDiagnostics();
        QFile detailOut(outPath.isEmpty() ? QStringLiteral("/dev/stdout") : outPath + QStringLiteral(".detail"));
        if (detailOut.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&detailOut);
            for (const auto& d : diags.all()) {
                ts << diagSeverityName(d.severity) << '\t' << d.code << '\t' << d.position << '\t'
                   << d.message << '\t' << d.snippet << '\n';
            }
        }
    }
    // 可选探针：--jumpcheck 遍历**全部脚本的全部行**，核对行字段 lines[i].jumpTo
    // 的合法性（0 <= t <= lines.size()；t < size 时目标行同脚本）。读侧唯一事实
    // 就是行字段，所以这里 0 违例即「跳转事实自洽」。不影响告警输出。
    if (opt.jumpcheck) {
        int scripts = 0, rows = 0, links = 0, bad = 0;
        const auto& allScripts = engine.getParseTable()->scripts();
        for (auto it = allScripts.constBegin(); it != allScripts.constEnd(); ++it) {
            const ScriptData& sd = it.value();
            ++scripts;
            rows += int(sd.lines.size());
            for (const LogicalLine& ll : sd.lines) {
                if (ll.jumpTo >= 0) ++links;
            }
            QStringList details;
            bad += jumpmark::countBadJumpTargets(sd, &details);
            for (const QString& s : details) {
                fprintf(stderr, "JUMPBAD\t%s\t%s\n", it.key().toUtf8().constData(),
                        s.toUtf8().constData());
            }
        }
        fprintf(stderr, "JUMPCHECK\tscripts=%d\trows=%d\tlinks=%d\tbad=%d\n",
                scripts, rows, links, bad);
        if (bad != 0) return 3;
    }
    // 可选探针：--sizeof 打印关键结构大小（只读，不影响告警输出）。
    if (opt.sizeofProbe) {
        fprintf(stderr, "SIZEOF\tScriptData=%zu\tLogicalLine=%zu\n",
                sizeof(ScriptData), sizeof(LogicalLine));
    }
    // ROM 编码探测（只读；用于展示「这个游戏是什么编码」）
    if (opt.probe) {
        fputs((engine.probeGameEncoding() + QLatin1Char('\n')).toUtf8().constData(), stdout);
    }
    qWarning() << "load ms =" << timer.elapsed() << " warnings =" << n << " variables =" << vars;

    QFile out(outPath);
    if (!outPath.isEmpty() && out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&out);
        for (int i = 0; i < n; ++i) ts << warns.at(i) << '\n';
        ts.flush();
    } else {
        for (int i = 0; i < n; ++i) {
            fputs((warns.at(i) + QLatin1Char('\n')).toLocal8Bit().constData(), stdout);
        }
    }

    // 可选（调试，默认不走）：--kind-stats 打印「变量分词」的 kind 判定分布。
    // 需要环境变量 EMUERA_KIND_STATS=1 才会计数（否则恒空）。
    if (opt.kindStats) {
        const auto hist = engine.getParseTable()->variableTable().accessKindHistogram();
        qint64 total = 0;
        for (auto it = hist.constBegin(); it != hist.constEnd(); ++it) total += it.value();
        for (auto it = hist.constBegin(); it != hist.constEnd(); ++it) {
            fprintf(stderr, "KIND\t%s\t%lld\t%.2f%%\n", it.key().toUtf8().constData(),
                    static_cast<long long>(it.value()),
                    total ? 100.0 * double(it.value()) / double(total) : 0.0);
        }
        fprintf(stderr, "KIND\tTOTAL\t%lld\t100.00%%\n", static_cast<long long>(total));
        VariableTable::resetAccessKindHistogram();
    }
    // 可选：--var NAME 打印变量表里的声明
    for (const QString& name : opt.vars) {
        for (const VariableDecl& d : engine.getParseTable()->variableTable().declarations()) {
            if (d.name.compare(name, Qt::CaseInsensitive) != 0) continue;
            qWarning().noquote() << "VAR" << d.name
                                 << "type=" << operandTypeName(d.type)
                                 << "scope=" << (d.scope == VarScope::Global ? "Global" : "Local")
                                 << "fn=" << d.function << "dim=" << d.dimension
                                 << "len=" << d.lengths;
        }
    }
    // 可选：--line SCRIPT INDEX 打印该行的 ownerFunction 与实参 AST 类型
    for (const auto& query : opt.lines) {
        const QString& script = query.first;
        const int index = query.second;
        if (const ScriptData* sd = engine.getParseTable()->script(script)) {
            if (index >= 0 && index < sd->lines.size()) {
                const LogicalLine& l = sd->lines.at(index);
                qWarning().noquote() << "LINE" << script << index << "raw=" << l.raw
                                     << "fn=" << l.functionName
                                     << "owner=" << l.ownerFunction
                                     << "kind=" << int(l.kind);
                for (const Operand& o : l.arguments) {
                    qWarning().noquote() << "   arg raw=" << o.raw
                                         << "ast=" << (o.ast ? o.ast->toString() : QStringLiteral("<null>"))
                                         << "type=" << (o.ast ? QString::fromUtf8(operandTypeName(o.ast->valueType()))
                                                              : QStringLiteral("?"));
                }
                qWarning().noquote() << "   argument.kind=" << int(l.argument.kind)
                                     << "exprs=" << l.argument.exprs.size()
                                     << "err=" << l.argument.typeError;
                for (const auto& e : l.argument.exprs) {
                    qWarning().noquote() << "      expr ast=" << (e ? e->toString() : QStringLiteral("<null>"))
                                         << "type=" << (e ? QString::fromUtf8(operandTypeName(e->valueType()))
                                                          : QStringLiteral("?"))
                                         << "ptr=" << (quintptr)e.get();
                }
                for (const Operand& c : l.argument.cases) {
                    qWarning().noquote() << "      case raw=" << c.raw
                                         << "ast=" << (c.ast ? c.ast->toString() : QStringLiteral("<null>"));
                }
            }
        }
    }
    return 0;
}
