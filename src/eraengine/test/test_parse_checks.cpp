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
//   2. clearDiagnostics()；
//   3. 只用 EraParseTable 的**只读 getter** 手工拼一个 parsecheck::ParseIrView
//      （scripts / variables / constantTable / storage / functions，全部 const）；
//   4. 直接调 parsecheck::checkXxx(view, diags)，断言条数 / code / 行号；
//   5. 再走 EraParseTable 的同名 forwarder，逐字段比对两条路径的诊断
//      —— 证明「搬出去」是逐字搬运，没有改逻辑。
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

static QList<ParseDiagnostic> withCode(const ParseDiagnostics& d, const QString& code) {
    QList<ParseDiagnostic> out;
    for (const ParseDiagnostic& x : d.all())
        if (x.code == code) out.append(x);
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
    //   3 10 / 0     -> 可静态判定的运行期错误
    //   4 CALL 缺失  -> 静态目标不存在
    //   5 IF 1       -> 结构（函数末尾未闭合）
    const QString source = QStringLiteral(
        "@MAIN\n"        // 1
        "ELSE\n"         // 2
        "PRINTV 10 / 0\n"// 3
        "CALL MISSING_FN\n" // 4
        "IF 1\n"         // 5
        "PRINTV 2\n");   // 6
    loadInto(table, QStringLiteral("a.ERB"), source);

    const int baselineTotal = table.parseDiagnostics().size();
    check(baselineTotal > 0, QString("收尾本身产生了诊断（%1 条）").arg(baselineTotal));

    // ---- 手工构造只读视图：只用 public getter，不碰任何私有状态 ----
    parsecheck::ParseIrView view;
    view.scripts = &table.scripts();
    view.variables = &table.variableTable();
    view.constantTable = table.constantTable();
    view.storage = table.variableStorage();
    view.functions = &table.userFunctions();

    // ---- 1) 结构检查：直接调自由函数 ----
    table.clearDiagnostics();
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
    table.clearDiagnostics();
    ParseDiagnostics targets;
    parsecheck::resolveStaticTargets(view, targets);
    const auto labelHits = withCode(targets, DiagCode::kLabelNotFound);
    check(labelHits.size() == 1,
          QString("resolveStaticTargets(view, diags) 单独调用 -> 1 条（得到 %1）").arg(labelHits.size()));
    check(labelHits.size() == 1 && labelHits.first().line == 4
              && labelHits.first().message.contains(QStringLiteral("@MISSING_FN")),
          "定位到第 4 行、消息含目标名");

    // ---- 3) 可静态判定的运行期错误：直接调自由函数 ----
    table.clearDiagnostics();
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

    // ---- 5) forwarder 逐字段等价（证明搬出去是逐字搬运）----
    table.clearDiagnostics();
    table.checkStructures();
    check(sameDiagnostics(structures, table.parseDiagnostics()),
          "EraParseTable::checkStructures() 与自由函数逐字段（含顺序）相同");

    table.clearDiagnostics();
    table.resolveStaticTargets();
    check(sameDiagnostics(targets, table.parseDiagnostics()),
          "EraParseTable::resolveStaticTargets() 与自由函数逐字段相同");

    table.clearDiagnostics();
    table.checkStaticValues();
    check(sameDiagnostics(values, table.parseDiagnostics()),
          "EraParseTable::checkStaticValues() 与自由函数逐字段相同");

    // ---- 6) 三步合起来的条数 == 收尾时的条数（这三步就是收尾里的那三步）----
    ParseDiagnostics all;
    parsecheck::resolveStaticTargets(view, all);
    parsecheck::checkStructures(view, all);
    parsecheck::checkStaticValues(view, all);
    check(all.size() == baselineTotal,
          QString("4+5+6 三步合计 %1 条 == 收尾诊断总数 %2 条")
              .arg(all.size()).arg(baselineTotal));

    if (g_failures == 0) qDebug() << "\nALL PASSED";
    else qDebug() << "\nFAILURES:" << g_failures;
    return g_failures == 0 ? 0 : 1;
}