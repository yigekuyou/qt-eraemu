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
// 拍平行表导出工具（qdbug 桩）
//
// 背景（eraTW 实测差距）：AST 拍平（装载 -> 平铺行表）应该是**顺序**的；
// 并行分块装载存在插入顺序不确定的问题（见 era_parse_table.cpp 注释），
// 装载默认已改为串行。本工具把 parseTable 的**全部拍平行**按
// 「确定性顺序（脚本名字典序 + 行号升序）」导出，供离线核对：
//   * 拍平是否保序（行号 == 源码行号-1 的一致偏移）；
//   * 行内容与原始 ERB 是否一致（串行装载时）；
//   * 函数标签 / 循环 / SELECTCASE 标记区的完整性。
//
// 每行格式：脚本名<TAB>行号<TAB>kind<TAB>labelName<TAB>原文(截断)
//
// 用法： dump_lines <gameDir> [outFile] [--script NAME] [--parallel]
//   --script NAME 只导出指定脚本（大库全量导出会很慢，先定位再导）
//   --parallel 用并行分块装载（默认串行）：与顺序装载对比，验证
//   「并行拍平 == 顺序拍平」—— 两次导出走同一确定性顺序（脚本名字典序
//   + 行号升序），diff 两份导出即可核对一致性。
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include "eraengine.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        qWarning() << "usage: dump_lines <gameDir> [outFile] [--script NAME] [--parallel]";
        return 2;
    }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    const QString outPath = argc > 2 && !QString::fromLocal8Bit(argv[2]).startsWith(QLatin1String("--"))
                                ? QString::fromLocal8Bit(argv[2]) : QString();

    QString onlyScript;
    bool useParallel = false;
    for (int i = 2; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--script")
            && i + 1 < argc) {
            onlyScript = QString::fromLocal8Bit(argv[i + 1]);
        } else if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--parallel")) {
            useParallel = true;
        }
    }

    QElapsedTimer timer;
    timer.start();

    EraEngine engine;
    // 并行模式：装载前显式开启（验证并行拍平与顺序拍平结果一致）
    if (useParallel) engine.getExecutionEngine()->getErbLoader().setParallelLoad(true);
    engine.setGameDirectory(dir);

    const EraParseTable* table = engine.getParseTable();
    const QStringList scripts = table->scriptNames();
    // 确定性顺序：脚本名字典序（scriptNames 应已有序；这里显式排序兜底）
    QStringList ordered = scripts;
    std::sort(ordered.begin(), ordered.end());

    qint64 totalLines = 0;
    QFile out(outPath);
    const bool toFile = !outPath.isEmpty() && out.open(QIODevice::WriteOnly | QIODevice::Text);
    // QTextStream(FILE*) 构造器直接接 stdout；文件输出用 QIODevice
    QTextStream ts(stdout);
    if (toFile) ts.setDevice(&out);

    for (const QString& name : ordered) {
        if (!onlyScript.isEmpty() && name.compare(onlyScript, Qt::CaseInsensitive) != 0) continue;
        const ScriptData* sd = table->script(name);
        if (!sd) continue;
        ts << QStringLiteral("== 脚本 %1  (%2)  行 %3  标签 %4\n")
              .arg(name, sd->path).arg(sd->lines.size()).arg(sd->labelPositions.size());
        for (int i = 0; i < sd->lines.size(); ++i) {
            const LogicalLine& l = sd->lines.at(i);
            ts << QStringLiteral("%1\t%2\t%3\t%4\t%5\n")
                  .arg(name, QString::number(i), QString::number(int(l.kind)),
                       l.labelName, l.raw.trimmed().left(120));
            ++totalLines;
        }
    }
    ts.flush();
    qWarning() << "dump_lines 完成：脚本" << ordered.size() << "个（过滤:"
               << (onlyScript.isEmpty() ? QStringLiteral("无") : onlyScript)
               << "）拍平行" << totalLines << "行，耗时" << timer.elapsed() << "ms";
    return 0;
}
