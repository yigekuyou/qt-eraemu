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
// test_parse_checks.cpp
//
// 验收物：装载收尾的三个**只读**步骤（4 静态目标 / 5 结构配对 / 6 可静态判定的
// 运行期错误）可以**脱离 EraParseTable** 单独调用。
//
// 做法：
//   1. 正常装载一小段脚本并收尾（拿定型的 IR）；
//   2. 手工拼只读视图（或直接用 EraParseTable 的 public getter）；
//   3. 直接调 parsecheck::checkXxx(view, diags)，断言条数 / code / 行号；
//   4. 再走 EraParseTable 的同名 forwarder，按 origin 切片比对两条路径的诊断
//      —— 证明「搬出去」是逐字搬运，没有改逻辑；重跑只替换自己的切片。
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDebug>
#include <QList>
#include <QString>
#include <QStringList>

#include "ast/ast_builder.h"
#include "ast/parse_diagnostic.h"
#include "era_parse_table.h"
#include "parse_checks.h"
#include "process_state.h"
#include "variable_storage.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

// 装载一个脚本（完整 AST + 收尾），返回已定型的表。
// 用栈上的对象会不好返回，故返回调用方传进来的引用，函数只负责填充。
static void loadInto(EraParseTable& table, const QString& name, const QString& source) {
    const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
    const QStringList lines = source.split(QLatin1Char('\n'));
    QList<LogicalLine> built;
    for (int i = 0; i < lines.size(); ++i)
        built.append(AstBuilder::build(lines.at(i), ScriptPosition(name, i + 1, 1), resolve));
    table.loadScript(name, built);
    table.markLoadComplete();
}

// 只登记脚本、不做收尾（用于「装载顺序 != 名字序」的多脚本夹具）。
static void loadOnly(EraParseTable& table, const QString& name, const QString& source) {
    const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
    const QStringList lines = source.split(QLatin1Char('\n'));
    QList<LogicalLine> built;
    for (int i = 0; i < lines.size(); ++i)
        built.append(AstBuilder::build(lines.at(i), ScriptPosition(name, i + 1, 1), resolve));
    table.loadScript(name, built);
}

static QList<ParseDiagnostic> withCode(const ParseDiagnostics& d, const QString& code) {
    QList<ParseDiagnostic> out;
    for (const ParseDiagnostic& x : d.all())
        if (x.code == code) out.append(x);
    return out;
}

static ParseDiagnostics withOrigin(const ParseDiagnostics& d, DiagOrigin origin) {
    ParseDiagnostics out;
    for (const ParseDiagnostic& x : d.all())
        if (x.origin == origin) out.add(x);
    return out;
}

// 两条路径的诊断逐字段比对（含**产生顺序**）：文件/行/列/code/severity/effect/message。
static bool sameDiagnostics(const ParseDiagnostics& a, const ParseDiagnostics& b) {
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) {
        const ParseDiagnostic& x = a.all().at(i);
        const ParseDiagnostic& y = b.all().at(i);
        if (x.file != y.file || x.line != y.line || x.column != y.column || x.code != y.code
            || x.severity != y.severity || x.effect != y.effect
            || x.message != y.message || x.snippet != y.snippet)
            return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    qDebug() << "Read-only parse checks (free functions over ParseIrView)";
    qDebug() << "=======================================================";

    ProcessState state;
    EraParseTable table(&state);
    VariableStorage storage;
    table.setVariableStorage(&storage);

    // 故意造出三类问题：
    //   2 ELSE       -> 结构（IF～ENDIF 之外）
    //   3 A / 0      -> 可静态判定的运行期错误（被除数含变量 -> 6/6 的残余）
    //   4 CALL 缺失  -> 静态目标不存在
    //   5 IF 1       -> 结构（函数末尾未闭合）
    const QString source = QStringLiteral(
        "@MAIN\n"        // 1
        "ELSE\n"         // 2
        "PRINTV A / 0\n" // 3
        "CALL MISSING_FN\n" // 4
        "IF 1\n"         // 5
        "PRINTV 2\n");   // 6
    loadInto(table, QStringLiteral("a.ERB"), source);

    const int baselineTotal = table.parseDiagnostics().size();
    check(baselineTotal > 0, QString("收尾本身产生了诊断（%1 条）").arg(baselineTotal));

    // ---- 手工构造只读视图：只用 public getter，不碰任何私有状态 ----
    parsecheck::ParseIrView view;
    view.scripts = &table.scripts();
    view.labelIndex = &table.labelIndex();
    view.variables = &table.variableTable();
    view.constantTable = table.constantTable();
    view.storage = table.variableStorage();
    view.functions = &table.userFunctions();

    // ---- 1) 结构检查：直接调自由函数 ----
    ParseDiagnostics structures;
    parsecheck::checkStructures(view, structures);
    const auto structureHits = withCode(structures, DiagCode::kStructure);
    check(structures.size() == 2,
          QString("checkStructures(view, diags) 单独调用 -> 2 条（得到 %1）").arg(structures.size()));
    check(structureHits.size() == 2, "两条都是 structure code");
    check(structureHits.size() == 2 && structureHits.at(0).line == 2
              && structureHits.at(1).line == 5,
          "定位到第 2 行（ELSE 在 IF 外）与第 5 行（IF 未闭合）");
    check(structureHits.size() == 2
              && structureHits.at(0).message.contains(QStringLiteral("IF～ENDIFの外"))
              && structureHits.at(1).message.contains(QStringLiteral("ENDIF")),
          "消息与改造前同源（IF～ENDIFの外 / 対応する ENDIF…）");

    // ---- 2) 静态目标：直接调自由函数 ----
    ParseDiagnostics targets;
    parsecheck::resolveStaticTargets(view, targets);
    const auto labelHits = withCode(targets, DiagCode::kLabelNotFound);
    check(labelHits.size() == 1,
          QString("resolveStaticTargets(view, diags) 单独调用 -> 1 条（得到 %1）").arg(labelHits.size()));
    check(labelHits.size() == 1 && labelHits.first().line == 4
              && labelHits.first().message.contains(QStringLiteral("@MISSING_FN")),
          "定位到第 4 行、消息含目标名");

    // ---- 3) 可静态判定的运行期错误：直接调自由函数 ----
    ParseDiagnostics values;
    parsecheck::checkStaticValues(view, values);
    const auto valueHits = withCode(values, DiagCode::kStaticValue);
    check(valueHits.size() == 1,
          QString("checkStaticValues(view, diags) 单独调用 -> 1 条（得到 %1）").arg(valueHits.size()));
    check(valueHits.size() == 1 && valueHits.first().line == 3
              && valueHits.first().severity == DiagSeverity::Error
              && valueHits.first().effect == DiagEffect::ReportOnly,
          "定位到第 3 行；severity=Error、effect=ReportOnly（与改造前一致）");

    // ---- 4) 视图只读：同一视图重复调用，结果恒定（没有隐藏状态被写坏）----
    ParseDiagnostics again;
    parsecheck::checkStructures(view, again);
    check(sameDiagnostics(structures, again), "同一只读视图重复调用 -> 诊断完全相同");

    // ---- 5) forwarder 按 origin 切片等价（证明搬出去是逐字搬运，且重跑只换自己的切片）----
    table.checkStructures();
    check(sameDiagnostics(structures,
                          withOrigin(table.parseDiagnostics(), DiagOrigin::Pass5)),
          "EraParseTable::checkStructures() 的 Pass5 切片与自由函数逐字段（含顺序）相同");

    table.resolveStaticTargets();
    check(sameDiagnostics(targets,
                          withOrigin(table.parseDiagnostics(), DiagOrigin::Pass4)),
          "EraParseTable::resolveStaticTargets() 的 Pass4 切片与自由函数逐字段相同");

    table.checkStaticValues();
    check(sameDiagnostics(values,
                          withOrigin(table.parseDiagnostics(), DiagOrigin::Pass6)),
          "EraParseTable::checkStaticValues() 的 Pass6 切片与自由函数逐字段相同");

    // ---- 6) 三步合起来的条数 == 收尾时的条数（这三步就是收尾里的那三步）----
    ParseDiagnostics all;
    parsecheck::resolveStaticTargets(view, all);
    parsecheck::checkStructures(view, all);
    parsecheck::checkStaticValues(view, all);
    check(all.size() == baselineTotal,
          QString("4+5+6 三步合计 %1 条 == 收尾诊断总数 %2 条")
              .arg(all.size()).arg(baselineTotal));

    // ---- 7) 并行化之后的三条契约：确定性 / 归并顺序 = 脚本名序 / 逐脚本入口 ----
    //
    // 上面 1~6 测的是「拆函数没改内容」；这里测「拆成并行驱动后仍然可复现」：
    //   * 同一份只读输入连跑两次，诊断**逐字段且逐位置**相同（并行化之前这条
    //     恒成立只是因为单线程；现在它钉住「归并顺序不依赖线程调度」）；
    //   * 归并顺序 = 脚本名序（QString::compare 序），不是装载序、也不是哈希序；
    //   * 逐脚本入口只处理自己那个脚本（新 public API 的契约）。
    //
    // 4 个脚本 + 装载序 D,C,B,A（倒序）：
    //   脚本数 >= kMinScriptsForParallel(4)，会真的走并行驱动（本机线程池 > 1）；
    //   万一只有 1 个核，走的是串行直跑分支 —— 此时归并顺序断言仍然有意义
    //   （串行分支也按排好序的 names 走）。
    {
        ProcessState state2;
        EraParseTable table2(&state2);
        VariableStorage storage2;
        table2.setVariableStorage(&storage2);

        // 每个脚本各造一条：结构（ELSE 在 IF 外）、静态目标（CALL 缺失）、
        // 可静态判定的运行期错误（A / 0；常量 10/0 归 3/6）。
        const QString multi = QStringLiteral(
            "@MAIN\n"          // 1
            "ELSE\n"           // 2 -> checkStructures
            "CALL MISSING_FN\n"// 3 -> resolveStaticTargets
            "PRINTV A / 0\n"); // 4 -> checkStaticValues
        // **装载顺序与名字序相反**：D 先、A 最后。
        for (const QString& n : {QStringLiteral("D.ERB"), QStringLiteral("C.ERB"),
                                 QStringLiteral("B.ERB"), QStringLiteral("A.ERB")})
            loadOnly(table2, n, multi);
        table2.markLoadComplete();   // 全部脚本登记完只收尾一次

        parsecheck::ParseIrView view2;
        view2.scripts = &table2.scripts();
        view2.labelIndex = &table2.labelIndex();
        view2.variables = &table2.variableTable();
        view2.constantTable = table2.constantTable();
        view2.storage = table2.variableStorage();
        view2.functions = &table2.userFunctions();
        check(table2.scripts().size() == 4, "多脚本夹具装载了 4 个脚本");

        // 7a) 确定性：每个 pass 连跑两次，逐字段 + 逐位置相同。
        struct PassCase {
            const char* name;
            QString code;
            void (*run)(const parsecheck::ParseIrView&, ParseDiagnostics&);
        };
        const PassCase cases[] = {
            {"resolveStaticTargets", DiagCode::kLabelNotFound, &parsecheck::resolveStaticTargets},
            {"checkStructures", DiagCode::kStructure, &parsecheck::checkStructures},
            {"checkStaticValues", DiagCode::kStaticValue, &parsecheck::checkStaticValues},
        };
        for (const PassCase& c : cases) {
            ParseDiagnostics d1;
            ParseDiagnostics d2;
            c.run(view2, d1);
            c.run(view2, d2);
            const auto hits = withCode(d1, c.code);
            check(hits.size() == 4,
                  QString("%1 在 4 个脚本上各产生 1 条（得到 %2）")
                      .arg(QLatin1String(c.name)).arg(hits.size()));
            check(sameDiagnostics(d1, d2),
                  QString("%1 连跑两次 -> 逐字段（file/line/column/code/severity/effect/"
                          "message/snippet）且逐位置相同")
                      .arg(QLatin1String(c.name)));
            // 归并顺序 = 脚本名序 A,B,C,D（而不是装载序 D,C,B,A / 哈希序）。
            const bool ordered = hits.size() == 4
                && hits.at(0).file == QStringLiteral("A.ERB")
                && hits.at(1).file == QStringLiteral("B.ERB")
                && hits.at(2).file == QStringLiteral("C.ERB")
                && hits.at(3).file == QStringLiteral("D.ERB");
            check(ordered,
                  QString("%1 的归并顺序 = 脚本名序（A.ERB 在 B.ERB 之前；装载序是 D,C,B,A）")
                      .arg(QLatin1String(c.name)));
        }

        // 7b) 逐脚本入口：只产出自己那个脚本的诊断（新 public API 的契约），
        //     且与「全库驱动里该脚本那一段」逐字段相同。
        const auto aIt = table2.scripts().constFind(QStringLiteral("A.ERB"));
        check(aIt != table2.scripts().constEnd(), "能按名字取到 A.ERB 的 ScriptData");
        if (aIt != table2.scripts().constEnd()) {
            ParseDiagnostics oneScript;
            parsecheck::checkStructuresInScript(view2, aIt.value(), oneScript);
            check(oneScript.size() == 1 && oneScript.all().first().file == QStringLiteral("A.ERB")
                      && oneScript.all().first().code == DiagCode::kStructure,
                  "checkStructuresInScript(A.ERB) -> 只有 A.ERB 自己那条结构诊断");

            ParseDiagnostics whole;
            parsecheck::checkStructures(view2, whole);
            ParseDiagnostics justA;
            for (const ParseDiagnostic& d : whole.all())
                if (d.file == QStringLiteral("A.ERB")) justA.add(d);
            check(sameDiagnostics(oneScript, justA),
                  "逐脚本结果 == 全库驱动结果中属于 A.ERB 的那一段");
        }
    }

    if (g_failures == 0) qDebug() << "\nALL PASSED";
    else qDebug() << "\nFAILURES:" << g_failures;
    return g_failures == 0 ? 0 : 1;
}