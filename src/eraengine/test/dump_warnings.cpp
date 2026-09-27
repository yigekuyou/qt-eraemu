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
// 用法： dump_warnings <gameDir> [outFile]
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QElapsedTimer>
#include "eraengine.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        qWarning() << "usage: dump_warnings <gameDir> [outFile]";
        return 2;
    }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    const QString outPath = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();

    QElapsedTimer timer;
    timer.start();

    EraEngine engine;
    engine.setGameDirectory(dir);

    const int n = engine.getParseTable()->parseWarningCount();
    const QStringList warns = engine.getParseTable()->parseWarnings();
    const int vars = engine.getParseTable()->variableTable().count();
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
