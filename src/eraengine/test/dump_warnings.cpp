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
// 用法： dump_warnings <gameDir> [outFile] [--async]
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QElapsedTimer>
#include <QEventLoop>
#include "eraengine.h"
#include "jump_markings.h"   // jumpmark::countBadJumpTargets（--jumpcheck 探针）

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        qWarning() << "usage: dump_warnings <gameDir> [outFile] [--async]";
        return 2;
    }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    const QString outPath = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();

    QElapsedTimer timer;
    timer.start();

    EraEngine engine;
    if (app.arguments().contains(QStringLiteral("--async"))) {
        QEventLoop loop;
        bool ok = false;
        QObject::connect(&engine, &EraEngine::scriptsLoaded, &loop, [&](bool loaded) {
            ok = loaded;
            loop.quit();
        });
        engine.loadAsync(dir);
        if (engine.isLoadingScripts()) loop.exec();
        if (!ok) return 1;
    } else {
        engine.setGameDirectory(dir);
    }

    const bool detail = app.arguments().contains(QStringLiteral("--detail"));
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
    if (app.arguments().contains(QStringLiteral("--jumpcheck"))) {
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
    if (app.arguments().contains(QStringLiteral("--sizeof"))) {
        fprintf(stderr, "SIZEOF\tScriptData=%zu\tLogicalLine=%zu\n",
                sizeof(ScriptData), sizeof(LogicalLine));
    }
    // ROM 编码探测（只读；用于展示「这个游戏是什么编码」）
    if (argc > 3 && QString::fromLocal8Bit(argv[3]) == QLatin1String("--probe")) {
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
    if (app.arguments().contains(QStringLiteral("--kind-stats"))) {
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
    for (int i = 2; i + 1 < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) != QLatin1String("--var")) continue;
        const QString name = QString::fromLocal8Bit(argv[++i]);
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
    for (int i = 2; i + 2 < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) != QLatin1String("--line")) continue;
        const QString script = QString::fromLocal8Bit(argv[i + 1]);
        const int index = QString::fromLocal8Bit(argv[i + 2]).toInt();
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
