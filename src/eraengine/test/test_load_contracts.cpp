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
// test_load_contracts.cpp
//
// 装载/存档流程里「以前只靠注释与调用顺序」维持的条件，现在是可观察的状态。
// 本测试锁定**越界调用是定义好的行为**：
//
//   1. 阶段机：Empty -> Loading -> Bound -> Finalized；越界入口 qWarning + no-op。
//   2. 未装载就调只读收尾步骤：不产出诊断（而不是拿空表乱报）。
//   3. 只读步骤重复调用：诊断按 origin 切片**原地替换**，跑 N 次与跑 1 次
//      逐字节相同（幂等由数据表达，不靠调用方数次数/去重）。
//   4. markLoadComplete 调两次：第二次 no-op（诊断、阶段都不变）。
//   5. 收尾后 loadScript：返回 false / 增量 append 的显式契约。
//   6. 私有名表安装前后各调一次 setPrivateScope：安装后 names 参数被忽略。
//   7. 存档前置条件 SaveReadiness 的三态可查询；新增声明会把
//     「已定稿」退回「未定稿」。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QStringList>
#include <QThreadPool>

#include <algorithm>

#include "ast/ast_builder.h"
#include "ast/expression_ast.h"
#include "ast/expression_evaluator.h"
#include "ast/name_interner.h"
#include "ast/variable_table.h"
#include "era_parse_table.h"
#include "load_passes.h"
#include "process_state.h"
#include "variable_storage.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static QList<LogicalLine> buildLines(EraParseTable& table, const QStringList& source) {
    QList<LogicalLine> out;
    for (int i = 0; i < source.size(); ++i) {
        out.append(AstBuilder::build(source.at(i), ScriptPosition("contracts.ERB", i + 1, 1),
            [&table](const QString& text) { return table.expressionAst(text); }));
    }
    return out;
}

// 同上，但显式给出「文件身份」（ScriptPosition::filename）：
// 同名脚本替换时，Load 切片按文件身份精确移除旧行级诊断 —— 测试要能钉住它。
static QList<LogicalLine> buildLinesAs(EraParseTable& table, const QString& file,
                                       const QStringList& source) {
    QList<LogicalLine> out;
    for (int i = 0; i < source.size(); ++i) {
        out.append(AstBuilder::build(source.at(i), ScriptPosition(file, i + 1, 1),
            [&table](const QString& text) { return table.expressionAst(text); }));
    }
    return out;
}

static int countCode(const ParseDiagnostics& d, const char* code) {
    int n = 0;
    for (const ParseDiagnostic& x : d.all())
        if (x.code == QLatin1String(code)) ++n;
    return n;
}

static int countCodeInFile(const ParseDiagnostics& d, const char* code, const QString& file) {
    int n = 0;
    for (const ParseDiagnostic& x : d.all())
        if (x.code == QLatin1String(code) && x.file == file) ++n;
    return n;
}

static int diagnosticCount(const EraParseTable& table) {
    return table.parseDiagnostics().size();
}

static QList<ParseDiagnostic> withOrigin(const ParseDiagnostics& d, DiagOrigin origin) {
    QList<ParseDiagnostic> out;
    for (const ParseDiagnostic& x : d.all())
        if (x.origin == origin) out.append(x);
    return out;
}

// 把整份诊断序列化成字符串：**所有字段**（含 origin）与产生顺序都在里面。
// 幂等断言用它做逐字节比对。
static QString serializeDiagnostics(const ParseDiagnostics& d) {
    QString out;
    for (const ParseDiagnostic& x : d.all()) {
        const QStringList fields = {
            QString::fromLatin1(diagOriginName(x.origin)),
            QString::fromLatin1(diagSeverityName(x.severity)),
            QString::fromLatin1(diagEffectName(x.effect)),
            x.code, x.file, QString::number(x.line), QString::number(x.column),
            QString::number(x.length), x.position, x.message, x.snippet,
        };
        out += fields.join(QLatin1Char('\t')) + QLatin1Char('\n');
    }
    return out;
}

// texts() 是派生视图：条数一一对应，且每条 == 结构化条目的 toString()。
static bool textsConsistent(const ParseDiagnostics& d) {
    if (d.texts().size() != d.size()) return false;
    for (int i = 0; i < d.size(); ++i)
        if (d.texts().at(i) != d.all().at(i).toString()) return false;
    return true;
}

// ---------------------------------------------------------------------------
// 收尾步骤 3（finalizeValidation）的夹具
//
// finalizeValidation 收一个 loadpass::LoadContext —— 那是一个**视图**（引用/
// 指针），不是拥有者。这里给一份「全部字段都是本地对象」的最小视图：不需要
// EraParseTable，也就能脱离真实装载直接驱动这一步。
//
// 表达式由固定的 resolver 顶替（这不是偷懒：本步只消费**已经建好**的 AST，
// 不再自己解析源码）：
//   * `F…` 回 nullptr —— 造出「FUNC 的操作数没有装载期 AST」这条 kArgCheck 错误；
//   * 其余回整数 0 字面量 —— 赋值右值因此有 AST，走「可解析」路径。
// 求值器/存储/常量表留空（LoadContext 的三个可空指针）。
// ---------------------------------------------------------------------------
namespace {

struct ValidationFixture {
    QHash<QString, ScriptData> scripts;
    VariableTable variables;
    QHash<QString, UserFunctionInfo> functions;
    ParseDiagnostics diags;
    QHash<QString, QList<LabelRef>> labelIndex;
    QHash<QString, QSet<QString>> paramNames;
    bool declarationsResolved = true;
    bool constProvidersReady = true;

    ValidationFixture() {
        // 一个全局常量：`K ++` 在逐行校验里被折成字面量 -> 报「变量参数无法装载」，
        // 于是同一个脚本既有**逐行校验**的错误，也有 validateArguments 的告警。
        variables.setConstInt(QStringLiteral("K"), 5);
    }

    loadpass::LoadContext context() {
        return loadpass::LoadContext{scripts, variables, functions,
                                     /*constantTable=*/nullptr, /*storage=*/nullptr,
                                     /*evaluator=*/nullptr, /*gameBaseData=*/nullptr,
                                     labelIndex, paramNames,
                                     declarationsResolved, constProvidersReady};
    }

    // 按给定顺序登记脚本。QHash 的遍历序与装载序无关 —— 归并键是**脚本名**。
    void addScript(const QString& name, const QStringList& source) {
        const AstResolver resolve = [](const QString& text) -> QSharedPointer<ExpressionNode> {
            if (text.startsWith(QLatin1Char('F'))) return {};
            return QSharedPointer<LiteralNode>::create(qint64(0));
        };
        ScriptData sd;
        for (int i = 0; i < source.size(); ++i)
            sd.lines.append(AstBuilder::build(source.at(i), ScriptPosition(name, i + 1, 1), resolve));
        scripts.insert(name, sd);
    }
};

// 一个脚本的夹具源：三条指令 -> 诊断（顺序：逐行校验的错误、validateArguments
// 的告警、数字字面量）。
//   `1 ++`                    -> kArgCheck 错误（逐行校验：变量参数无法装载）
//                                + kArgCheck 告警（validateArguments 读到 typeOk=false）
//   `X = 999…`                -> kNumericConversion（逐行校验的赋值分支）
QStringList validationSource(int index) {
    return {QStringLiteral("@MAIN"),
            QStringLiteral("K ++"),
            QStringLiteral("X = %1").arg(QStringLiteral("9").repeated(22) + QString::number(index % 10))};
}

QList<ParseDiagnostic> runFinalizeValidation(ValidationFixture& fixture) {
    loadpass::LoadContext ctx = fixture.context();
    // 诊断 sink 是每次运行的显式参数（3/6 的切片由驱动端 replaceOrigin 负责）。
    loadpass::finalizeValidation(ctx, fixture.diags);
    return fixture.diags.all();
}

bool sameFields(const ParseDiagnostic& x, const ParseDiagnostic& y) {
    return x.file == y.file && x.line == y.line && x.column == y.column && x.code == y.code
        && x.severity == y.severity && x.effect == y.effect && x.message == y.message
        && x.snippet == y.snippet;
}

bool sameList(const QList<ParseDiagnostic>& a, const QList<ParseDiagnostic>& b) {
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i)
        if (!sameFields(a.at(i), b.at(i))) return false;
    return true;
}

QList<ParseDiagnostic> withCode(const QList<ParseDiagnostic>& in, const QString& code) {
    QList<ParseDiagnostic> out;
    for (const ParseDiagnostic& d : in)
        if (d.code == code) out.append(d);
    return out;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    qDebug() << "Load/save contracts: out-of-phase calls are defined behaviour";

    ProcessState state;
    VariableStorage storage;
    EraParseTable table(&state);
    table.setVariableStorage(&storage);

    // ------------------------------------------------------------------
    // 1) 未装载（Empty）：只读步骤 qWarning + no-op，不产出诊断。
    // ------------------------------------------------------------------
    qDebug() << "\n1) Empty 阶段调用只读收尾步骤";
    check(table.loadPhase() == EraParseTable::LoadPhase::Empty, "fresh table: phase == Empty");
    check(diagnosticCount(table) == 0, "fresh table: no diagnostics");
    table.resolveStaticTargets();
    table.checkStructures();
    table.checkStaticValues();
    check(diagnosticCount(table) == 0, "read-only passes before load: no diagnostics (no-op)");

    // ------------------------------------------------------------------
    // 2) Empty -> Loading -> Finalized（脚本故意缺 ENDIF，让结构检查有诊断）。
    // ------------------------------------------------------------------
    qDebug() << "\n2) 阶段迁移与只读步骤的重复调用契约";
    const QStringList source = {
        QStringLiteral("@MAIN"),
        QStringLiteral("IF 1"),
        QStringLiteral("PRINT 1"),
    };
    check(table.loadScript(QStringLiteral("contracts"), buildLines(table, source)),
          "loadScript in Empty -> accepted");
    check(table.loadPhase() == EraParseTable::LoadPhase::Loading,
          "after loadScript: phase == Loading");

    // 绑定完成前只读步骤仍应 no-op（Loading 不是 Bound）。
    const int beforeBind = diagnosticCount(table);
    table.checkStructures();
    check(diagnosticCount(table) == beforeBind,
          "Loading (bindings not complete): checkStructures is a no-op");

    table.markLoadComplete();
    check(table.loadPhase() == EraParseTable::LoadPhase::Finalized,
          "after markLoadComplete: phase == Finalized");

    // 重跑同一个 pass：诊断条数不增，整份诊断（含 origin/顺序）逐字节相同。
    const int afterLoad = diagnosticCount(table);
    check(afterLoad > 0, "missing ENDIF produced at least one diagnostic");
    {
        const QString once = serializeDiagnostics(table.parseDiagnostics());
        table.checkStructures();
        check(diagnosticCount(table) == afterLoad,
              "re-running checkStructures: diagnostic count unchanged");
        check(serializeDiagnostics(table.parseDiagnostics()) == once,
              "re-running checkStructures: 与跑一次逐字段/逐顺序相同");
        table.checkStructures();
        check(diagnosticCount(table) == afterLoad
                  && serializeDiagnostics(table.parseDiagnostics()) == once,
              "re-running checkStructures a third time: still byte-identical to one run");
    }

    // ------------------------------------------------------------------
    // 3) markLoadComplete 调两次：第二次 qWarning + no-op。
    // ------------------------------------------------------------------
    qDebug() << "\n3) markLoadComplete 重复调用";
    const int diagnosticsBeforeSecond = diagnosticCount(table);
    const QString serializedBeforeSecond = serializeDiagnostics(table.parseDiagnostics());
    table.markLoadComplete();
    check(table.loadPhase() == EraParseTable::LoadPhase::Finalized,
          "second markLoadComplete: phase stays Finalized");
    check(diagnosticCount(table) == diagnosticsBeforeSecond,
          "second markLoadComplete: diagnostics unchanged (no-op)");
    check(serializeDiagnostics(table.parseDiagnostics()) == serializedBeforeSecond,
          "second markLoadComplete: 诊断逐字段/逐顺序不变（no-op）");

    // ------------------------------------------------------------------
    // 3b) origin 切片：每个只读 pass 的诊断归自己的 origin；重跑/交替重跑
    //     只替换自己的切片，互不覆盖；3/6 的 RejectLine 不受影响。
    //
    // 夹具同时造出四类：
    //   `A$ ++`          -> 3/6 finalizeValidation 的 RejectLine（origin=Pass3）
    //   `ELSE` / `IF 1`  -> 5/6 checkStructures 的 kStructure（origin=Pass5）
    //   `CALL MISSING_FN`-> 4/6 resolveStaticTargets 的 kLabelNotFound（Pass4）
    //   `PRINTV 10 / 0`  -> 6/6 checkStaticValues 的 kStaticValue（Pass6）
    // ------------------------------------------------------------------
    qDebug() << "\n3b) origin 切片：重跑幂等 / 交替重跑 / 跨步骤不覆盖";
    {
        ProcessState originState;
        VariableStorage originStorage;
        EraParseTable originTable(&originState);
        originTable.setVariableStorage(&originStorage);
        const QStringList originSource = {
            QStringLiteral("@MAIN"),          // 1
            QStringLiteral("A$ ++"),          // 2 -> 3/6 RejectLine
            QStringLiteral("ELSE"),           // 3 -> 5/6 kStructure
            QStringLiteral("CALL MISSING_FN"),// 4 -> 4/6 kLabelNotFound
            QStringLiteral("PRINTV 10 / 0"),  // 5 -> 6/6 kStaticValue
            QStringLiteral("IF 1"),           // 6 -> 5/6 kStructure（未闭合）
            QStringLiteral("PRINTV 2"),       // 7
        };
        check(originTable.loadScript(QStringLiteral("origin.ERB"),
                                     buildLines(originTable, originSource)),
              "origin 夹具装载成功");
        originTable.markLoadComplete();

        const ParseDiagnostics& diags = originTable.parseDiagnostics();
        const QList<ParseDiagnostic> pass4 = withOrigin(diags, DiagOrigin::Pass4);
        const QList<ParseDiagnostic> pass5 = withOrigin(diags, DiagOrigin::Pass5);
        const QList<ParseDiagnostic> pass6 = withOrigin(diags, DiagOrigin::Pass6);
        const auto rejectLines = [](const ParseDiagnostics& d) {
            QList<ParseDiagnostic> out;
            for (const ParseDiagnostic& x : d.all())
                if (x.effect == DiagEffect::RejectLine) out.append(x);
            return out;
        };
        const QList<ParseDiagnostic> rejectsBefore = rejectLines(diags);

        // d) 首次运行（markLoadComplete）的 origin 分布：code -> origin。
        auto allWithCodeHaveOrigin = [&diags](const char* code, DiagOrigin origin) {
            bool any = false;
            for (const ParseDiagnostic& x : diags.all()) {
                if (x.code != QLatin1String(code)) continue;
                any = true;
                if (x.origin != origin) return false;
            }
            return any;
        };
        check(pass4.size() == 1
                  && allWithCodeHaveOrigin(DiagCode::kLabelNotFound, DiagOrigin::Pass4),
              "4/6 首次运行：1 条 kLabelNotFound，全部归 origin=Pass4");
        check(pass5.size() == 2
                  && allWithCodeHaveOrigin(DiagCode::kStructure, DiagOrigin::Pass5),
              "5/6 首次运行：2 条 kStructure，全部归 origin=Pass5");
        check(pass6.size() == 1
                  && allWithCodeHaveOrigin(DiagCode::kStaticValue, DiagOrigin::Pass6),
              "6/6 首次运行：1 条 kStaticValue，全部归 origin=Pass6");
        check(!rejectsBefore.isEmpty()
                  && std::all_of(rejectsBefore.cbegin(), rejectsBefore.cend(),
                                 [](const ParseDiagnostic& x) {
                                     return x.origin == DiagOrigin::Pass3;
                                 }),
              "3/6 的 RejectLine 归 origin=Pass3（写步骤也切片，不再是 Load）");
        check(!withOrigin(diags, DiagOrigin::Pass3).isEmpty(),
              "3/6 的切片存在（Pass3 可被独立替换）");
        check(!pass4.isEmpty() && !pass5.isEmpty() && !pass6.isEmpty(),
              QString("首次运行的切片都非空（Pass4=%1 / Pass5=%2 / Pass6=%3）")
                  .arg(pass4.size()).arg(pass5.size()).arg(pass6.size()));

        // a) 同一只读 pass 连跑两次/三次：整份诊断逐字段逐顺序不变。
        const QString once = serializeDiagnostics(diags);
        for (int i = 0; i < 2; ++i) originTable.resolveStaticTargets();
        check(serializeDiagnostics(originTable.parseDiagnostics()) == once,
              "resolveStaticTargets 连跑两次 -> 整份诊断与跑一次逐字节相同");
        for (int i = 0; i < 2; ++i) originTable.checkStructures();
        check(serializeDiagnostics(originTable.parseDiagnostics()) == once,
              "checkStructures 连跑两次 -> 整份诊断与跑一次逐字节相同");
        for (int i = 0; i < 2; ++i) originTable.checkStaticValues();
        check(serializeDiagnostics(originTable.parseDiagnostics()) == once,
              "checkStaticValues 连跑两次 -> 整份诊断与跑一次逐字节相同");

        // b) 交替重跑 4→5→4→6→5：每个 origin 切片仍等于各自跑一次，
        //    且互不覆盖（切片按 origin 过滤后逐字段相同）。
        originTable.resolveStaticTargets();
        originTable.checkStructures();
        originTable.resolveStaticTargets();
        originTable.checkStaticValues();
        originTable.checkStructures();
        check(!withOrigin(originTable.parseDiagnostics(), DiagOrigin::Pass4).isEmpty(),
              "交替重跑后 Pass4 切片仍在（未被其它 pass 覆盖）");
        auto sameSlice = [](const QList<ParseDiagnostic>& a, const QList<ParseDiagnostic>& b) {
            if (a.size() != b.size()) return false;
            for (int i = 0; i < a.size(); ++i) {
                if (!sameFields(a.at(i), b.at(i)) || a.at(i).origin != b.at(i).origin)
                    return false;
            }
            return true;
        };
        check(sameSlice(pass4, withOrigin(originTable.parseDiagnostics(),
                                          DiagOrigin::Pass4)),
              "4→5→4→6→5 后 Pass4 切片 == 首次运行切片");
        check(sameSlice(pass5, withOrigin(originTable.parseDiagnostics(),
                                          DiagOrigin::Pass5)),
              "4→5→4→6→5 后 Pass5 切片 == 首次运行切片");
        check(sameSlice(pass6, withOrigin(originTable.parseDiagnostics(),
                                          DiagOrigin::Pass6)),
              "4→5→4→6→5 后 Pass6 切片 == 首次运行切片");

        // c) 3/6 的 RejectLine 在重跑 4/5/6 后仍在（替换按 origin 切片，不是整体清空）。
        check(sameList(rejectsBefore, rejectLines(originTable.parseDiagnostics())),
              "重跑 4/5/6 后 3/6 的 RejectLine 仍逐字段存在");

        // 整份诊断也应与「首次运行」逐字节相同（replaceOrigin 原位替换）。
        check(serializeDiagnostics(originTable.parseDiagnostics()) == once,
              "交替重跑 4→5→4→6→5 后整份诊断与首次运行逐字节相同");
        check(textsConsistent(originTable.parseDiagnostics()),
              "replaceOrigin 重建后 texts()/all()/count() 一一对应");
    }

    // ------------------------------------------------------------------
    // 4) 收尾之后 loadScript：显式增量协议（Finalized 不接受隐式重开）。
    // ------------------------------------------------------------------
    qDebug() << "\n4) 收尾之后 loadScript：显式增量协议";
    const int scriptsBefore = table.scriptNames().size();
    check(!table.loadScript(QStringLiteral("late"), buildLines(table, source)),
          "loadScript after Finalized（未 beginIncrementalLoad）: rejected");
    check(table.loadPhase() == EraParseTable::LoadPhase::Finalized,
          "rejected loadScript leaves phase == Finalized");
    check(table.scriptNames().size() == scriptsBefore,
          "rejected loadScript does not touch the script table");
    check(table.beginIncrementalLoad(), "beginIncrementalLoad: Finalized -> Loading");
    check(!table.beginIncrementalLoad(), "beginIncrementalLoad twice: rejected（当前不是 Finalized）");
    check(table.loadScript(QStringLiteral("late"), buildLines(table, source)),
          "loadScript after beginIncrementalLoad: accepted");
    check(table.scriptNames().size() == scriptsBefore + 1,
          "incremental load: script table gained the new script");
    check(table.loadPhase() == EraParseTable::LoadPhase::Loading,
          "incremental load keeps phase == Loading until markLoadComplete");
    table.markLoadComplete();
    check(table.loadPhase() == EraParseTable::LoadPhase::Finalized,
          "re-finalizing after incremental append: phase == Finalized again");

    // ------------------------------------------------------------------
    // 4b) 增量重收尾 == 同样输入一次性装载（逐字段/逐顺序）。
    //     旧脚本的诊断条数不翻倍（1/2/3 的切片必须替换，不是追加）。
    // ------------------------------------------------------------------
    qDebug() << "\n4b) 增量重收尾：诊断集合 == 一次性装载";
    {
        const QStringList firstSrc = {QStringLiteral("@MAIN"),
                                      QStringLiteral("IF 1"),
                                      QStringLiteral("PRINT 1")};      // 未闭合 -> Pass5
        const QStringList secondSrc = {QStringLiteral("@SUB"),
                                       QStringLiteral("ELSE"),
                                       QStringLiteral("PRINT 2")};     // ELSE 无 IF -> Pass5

        ProcessState oneState;
        VariableStorage oneStorage;
        EraParseTable oneShot(&oneState);
        oneShot.setVariableStorage(&oneStorage);
        check(oneShot.loadScript(QStringLiteral("inc-a.ERB"),
                                 buildLinesAs(oneShot, QStringLiteral("inc-a.ERB"), firstSrc),
                                 false, QStringLiteral("inc-a.ERB")),
              "一次性装载：inc-a.ERB");
        check(oneShot.loadScript(QStringLiteral("inc-b.ERB"),
                                 buildLinesAs(oneShot, QStringLiteral("inc-b.ERB"), secondSrc),
                                 false, QStringLiteral("inc-b.ERB")),
              "一次性装载：inc-b.ERB");
        oneShot.markLoadComplete();
        const QString expected = serializeDiagnostics(oneShot.parseDiagnostics());

        ProcessState incState;
        VariableStorage incStorage;
        EraParseTable incremental(&incState);
        incremental.setVariableStorage(&incStorage);
        check(incremental.loadScript(QStringLiteral("inc-a.ERB"),
                                     buildLinesAs(incremental, QStringLiteral("inc-a.ERB"), firstSrc),
                                     false, QStringLiteral("inc-a.ERB")),
              "增量装载：先装 inc-a.ERB");
        incremental.markLoadComplete();
        const int firstPass5 = countCodeInFile(incremental.parseDiagnostics(),
                                               DiagCode::kStructure, QStringLiteral("inc-a.ERB"));
        check(firstPass5 > 0, "首次收尾：inc-a.ERB 有结构诊断");

        check(incremental.beginIncrementalLoad(), "增量装载：beginIncrementalLoad");
        check(incremental.loadScript(QStringLiteral("inc-b.ERB"),
                                     buildLinesAs(incremental, QStringLiteral("inc-b.ERB"), secondSrc),
                                     false, QStringLiteral("inc-b.ERB")),
              "增量装载：追加 inc-b.ERB");
        incremental.markLoadComplete();
        check(serializeDiagnostics(incremental.parseDiagnostics()) == expected,
              "增量重收尾后的整份诊断 == 一次性装载（逐字段/逐顺序）");
        check(countCodeInFile(incremental.parseDiagnostics(), DiagCode::kStructure,
                              QStringLiteral("inc-a.ERB")) == firstPass5,
              QStringLiteral("旧脚本的 Pass5 诊断条数不翻倍（%1）").arg(firstPass5));
        check(withOrigin(incremental.parseDiagnostics(), DiagOrigin::Pass5).size() >= 2,
              "Pass5 切片含两个脚本的诊断（切片身份是 pass 编号，互不覆盖）");
    }

    // ------------------------------------------------------------------
    // 4c) 同名脚本替换：旧 AST 的行级装载期诊断（Load）随之消失。
    //     这是「替换语义」的第二个 bug —— 只替换 ScriptData 而留下旧行级诊断，
    //     同一行会同时挂新旧两版事实。
    // ------------------------------------------------------------------
    qDebug() << "\n4c) 同名脚本替换：旧行级诊断不残留";
    {
        const QStringList oldSrc = {QStringLiteral("#DIM GLOBAL_X"),   // 函数声明之外 -> kSharpLine
                                    QStringLiteral("@MAIN"),
                                    QStringLiteral("ELSE"),            // Pass5
                                    QStringLiteral("RETURN")};
        const QStringList fixedSrc = {QStringLiteral("@MAIN"),
                                      QStringLiteral("RETURN")};

        ProcessState oneState;
        EraParseTable oneShot(&oneState);
        check(oneShot.loadScript(QStringLiteral("replace.ERB"),
                                 buildLinesAs(oneShot, QStringLiteral("replace.ERB"), fixedSrc),
                                 false, QStringLiteral("replace.ERB")),
              "一次性装载：替换后的版本");
        oneShot.markLoadComplete();
        const QString expected = serializeDiagnostics(oneShot.parseDiagnostics());

        ProcessState replaceState;
        EraParseTable replaceTable(&replaceState);
        check(replaceTable.loadScript(QStringLiteral("replace.ERB"),
                                      buildLinesAs(replaceTable, QStringLiteral("replace.ERB"), oldSrc),
                                      false, QStringLiteral("replace.ERB")),
              "先装载旧版本");
        replaceTable.markLoadComplete();
        check(countCodeInFile(replaceTable.parseDiagnostics(), DiagCode::kSharpLine,
                              QStringLiteral("replace.ERB")) == 1,
              "旧版本的 kSharpLine 是 Load 切片的一部分");
        check(countCodeInFile(replaceTable.parseDiagnostics(), DiagCode::kStructure,
                              QStringLiteral("replace.ERB")) > 0,
              "旧版本有结构诊断");

        check(replaceTable.beginIncrementalLoad(), "替换装载：beginIncrementalLoad");
        check(replaceTable.loadScript(QStringLiteral("replace.ERB"),
                                      buildLinesAs(replaceTable, QStringLiteral("replace.ERB"), fixedSrc),
                                      false, QStringLiteral("replace.ERB")),
              "替换装载同名脚本");
        replaceTable.markLoadComplete();
        check(countCodeInFile(replaceTable.parseDiagnostics(), DiagCode::kSharpLine,
                              QStringLiteral("replace.ERB")) == 0,
              "旧版本的行级 Load 诊断随旧 AST 消失");
        check(countCodeInFile(replaceTable.parseDiagnostics(), DiagCode::kStructure,
                              QStringLiteral("replace.ERB")) == 0,
              "旧版本的结构诊断被 Pass5 新切片替换掉");
        check(serializeDiagnostics(replaceTable.parseDiagnostics()) == expected,
              "替换装载后的诊断 == 直接装载当前版本（逐字段/逐顺序）");
    }

    // ------------------------------------------------------------------
    // 4d) Pass7（可选 gate）：开启收尾 -> 关闭 gate 再增量收尾，kNotCalled 不残留。
    // ------------------------------------------------------------------
    qDebug() << "\n4d) Pass7 gate 关闭：旧切片置空";
    {
        ProcessState ncState;
        EraParseTable nc(&ncState);
        nc.setFunctionNotCalledWarning(true);
        check(nc.loadScript(QStringLiteral("nc.ERB"),
                            buildLinesAs(nc, QStringLiteral("nc.ERB"),
                                         {QStringLiteral("@MAIN"), QStringLiteral("RETURN"),
                                          QStringLiteral("@UNUSED"), QStringLiteral("RETURN")}),
                            false, QStringLiteral("nc.ERB")),
              "装载未调用函数夹具");
        nc.markLoadComplete();
        check(countCode(nc.parseDiagnostics(), DiagCode::kNotCalled) > 0,
              "gate 开启：产生 kNotCalled");
        check(withOrigin(nc.parseDiagnostics(), DiagOrigin::Pass7).size()
                  == countCode(nc.parseDiagnostics(), DiagCode::kNotCalled),
              "kNotCalled 全部归 origin=Pass7");

        nc.setFunctionNotCalledWarning(false);
        check(nc.beginIncrementalLoad(), "关闭 gate 后 beginIncrementalLoad");
        check(nc.loadScript(QStringLiteral("nc2.ERB"),
                            buildLinesAs(nc, QStringLiteral("nc2.ERB"),
                                         {QStringLiteral("@EXTRA"), QStringLiteral("RETURN")}),
                            false, QStringLiteral("nc2.ERB")),
              "增量装载第二个脚本");
        nc.markLoadComplete();
        check(withOrigin(nc.parseDiagnostics(), DiagOrigin::Pass7).isEmpty(),
              "gate 关闭的增量收尾把 Pass7 切片替换为空（不残留）");
        check(countCode(nc.parseDiagnostics(), DiagCode::kNotCalled) == 0,
              "整份诊断里没有 kNotCalled");
    }

    // ------------------------------------------------------------------
    // 4e) 数字字面量去重 = 本次运行的局部键集合：首跑 1 条，重跑仍 1 条。
    //     （fixture 版断言见 7c；这里钉住「重跑 pass 3」不会因共享容器/累加而变 0/2。）
    // ------------------------------------------------------------------
    qDebug() << "\n4e) 数字字面量去重：重跑仍 1 条";
    {
        ProcessState numState;
        EraParseTable num(&numState);
        check(num.loadScript(QStringLiteral("num.ERB"),
                             buildLinesAs(num, QStringLiteral("num.ERB"),
                                          {QStringLiteral("@MAIN"),
                                           QStringLiteral("X = 9999999999999999999999, 9999999999999999999999")}),
                             false, QStringLiteral("num.ERB")),
              "装载同一行两处非法字面量");
        num.markLoadComplete();
        check(countCode(num.parseDiagnostics(), DiagCode::kNumericConversion) == 1,
              "首跑：同一行同一位置只报 1 条（局部键集合去重）");

        check(num.beginIncrementalLoad(), "重跑前 beginIncrementalLoad");
        check(num.loadScript(QStringLiteral("num2.ERB"),
                             buildLinesAs(num, QStringLiteral("num2.ERB"),
                                          {QStringLiteral("@OTHER"), QStringLiteral("RETURN")}),
                             false, QStringLiteral("num2.ERB")),
              "增量装载一个无关脚本");
        num.markLoadComplete();
        check(countCode(num.parseDiagnostics(), DiagCode::kNumericConversion) == 1,
              "重跑 pass 3 后仍为 1 条（切片替换 + 本次运行独立的去重键集合）");
    }

    // ------------------------------------------------------------------
    // 5) 私有名表：安装前后各调一次 setPrivateScope。
    // ------------------------------------------------------------------
    qDebug() << "\n5) setPrivateScope 安装前 / 安装后";
    {
        ProcessState privateState;
        VariableStorage privateStorage;
        EraParseTable privateTable(&privateState);
        privateTable.setVariableStorage(&privateStorage);
        check(!privateStorage.privateScopeTableInstalled(),
              "before load: private table not installed");
        privateStorage.setPrivateScope(QStringLiteral("F"), {QStringLiteral("A")});
        check(privateStorage.storageName(QStringLiteral("A"),
                                         eravar::internName(QStringLiteral("F")))
                  == QStringLiteral("F\x1f" "A"),
              "before install: setPrivateScope builds the legacy table");

        const QStringList privateSource = {
            QStringLiteral("@MAIN"),
            QStringLiteral("RETURN"),
            QStringLiteral("@F"),
            QStringLiteral("#DIM A"),
            QStringLiteral("RETURN"),
        };
        check(privateTable.loadScript(QStringLiteral("private"),
                                      buildLines(privateTable, privateSource)),
              "private script loadScript");
        privateTable.markLoadComplete();
        check(privateStorage.privateScopeTableInstalled(),
              "markLoadComplete installs the load-time private table");
        privateStorage.setPrivateScope(QStringLiteral("F"), {});   // names 被忽略
        check(privateStorage.storageName(QStringLiteral("A"),
                                         eravar::internName(QStringLiteral("F")))
                  == QStringLiteral("F\x1f" "A"),
              "after install: empty names list still resolves the declared private");
    }

    // ------------------------------------------------------------------
    // 6) 存档前置条件：三态可查；新增声明会退回“未定稿”。
    // ------------------------------------------------------------------
    qDebug() << "\n6) 存档前置条件 SaveReadiness";
    {
        VariableStorage saveStorage;
        check(saveStorage.saveReadiness() == VariableStorage::SaveReadiness::NoDeclarationTable,
              "no declaration table -> NoDeclarationTable");
        VariableTable saveTable;
        VariableDecl decl;
        decl.name = QStringLiteral("S");
        decl.isSavedata = true;
        decl.lengths = {2};
        saveTable.add(decl);
        saveStorage.setSaveVariableTable(&saveTable);
        check(saveStorage.saveReadiness()
                  == VariableStorage::SaveReadiness::DeclarationsNotResolved,
              "table attached but dimensions unresolved -> DeclarationsNotResolved");
        saveTable.resolveDimensions();
        check(saveStorage.saveReadiness() == VariableStorage::SaveReadiness::Ready,
              "after resolveDimensions -> Ready");
        check(saveStorage.checkSaveReadiness("TEST"), "checkSaveReadiness returns true when Ready");

        VariableDecl later;
        later.name = QStringLiteral("LATER");
        later.isSavedata = true;
        later.lengths = {1};
        saveTable.add(later);
        check(saveStorage.saveReadiness()
                  == VariableStorage::SaveReadiness::DeclarationsNotResolved,
              "a new declaration invalidates 'resolved' (must re-run resolveDimensions)");
    }

    // ------------------------------------------------------------------
    // 7) 收尾步骤 3（finalizeValidation）逐脚本并行
    //
    // 这一步以前是「逐脚本外层循环 + 之后 validateArguments 全库再跑一趟」，
    // 现在拆成「串行序幕（排序 + 预取指针）→ 每脚本一个任务（两步按原顺序跑
    // 同一个脚本）→ 按脚本名序归并」。下面三条钉住这次改动的契约：
    //   7a) 并行分支与串行分支在同一份输入上逐字段且逐位置一致；并行连跑两次也一致
    //       —— 归并顺序不依赖线程调度；
    //   7b) 归并顺序 = 脚本名序（QString 序），不是装载序；
    //   7c) diagnoseIntegerLiterals 的去重（code+file+line+column）不因「每任务
    //       独占 sink」而退化，也不会跨脚本误合并（键里带文件名）。
    // ------------------------------------------------------------------
    qDebug() << "\n7) 收尾步骤 3（finalizeValidation）逐脚本并行";
    {
        // 4 个脚本（== kMinScriptsForParallel）+ 装载序 D,C,B,A（与名字序相反）。
        const QStringList loadOrder = {QStringLiteral("D.ERB"), QStringLiteral("C.ERB"),
                                       QStringLiteral("B.ERB"), QStringLiteral("A.ERB")};
        const auto buildFixture = [&loadOrder]() {
            ValidationFixture f;
            for (int i = 0; i < loadOrder.size(); ++i)
                f.addScript(loadOrder.at(i), validationSource(i));
            return f;
        };

        // 7a) 并行分支（本机线程池 > 1，故 4 个脚本真的走 blockingMapped）。
        ValidationFixture parallelA = buildFixture();
        const QList<ParseDiagnostic> parallelRun1 = runFinalizeValidation(parallelA);
        ValidationFixture parallelB = buildFixture();
        const QList<ParseDiagnostic> parallelRun2 = runFinalizeValidation(parallelB);
        // 串行分支：脚本数仍是 4，只把线程池压到 1（驱动里看的就是这两个条件）。
        ValidationFixture serial = buildFixture();
        const int savedMaxThreads = QThreadPool::globalInstance()->maxThreadCount();
        QThreadPool::globalInstance()->setMaxThreadCount(1);
        const QList<ParseDiagnostic> serialRun = runFinalizeValidation(serial);
        QThreadPool::globalInstance()->setMaxThreadCount(savedMaxThreads);

        check(parallelRun1.size() == 12,
              QStringLiteral("4 个脚本各 3 条（逐行校验错误 + validateArguments 告警 + 数字字面量）"
                             " -> 共 %1 条").arg(parallelRun1.size()));

        // 同一脚本内的顺序也钉住：先跑完**逐行校验**（arg-check 错误、numeric-conversion），
        // 再跑 validateArguments（arg-check 告警）—— 与旧的「全库两趟」相比，唯一的
        // 可观察差异是两趟变成**逐脚本交替**（这正是「两步同处一个任务」的直接后果；
        // 诊断内容/条数/code/severity/effect 不变，五语料 dump_warnings 逐字节相同）。
        QStringList codeOrder;
        for (int i = 0; i < 3 && i < parallelRun1.size(); ++i)
            codeOrder.append(parallelRun1.at(i).code);
        check(codeOrder == QStringList({QString::fromLatin1(DiagCode::kArgCheck),
                                        QString::fromLatin1(DiagCode::kNumericConversion),
                                        QString::fromLatin1(DiagCode::kArgCheck)}),
              QStringLiteral("脚本内顺序 = 逐行校验(错误) / 逐行校验(数字字面量) / validateArguments(告警)"
                             "（得到 %1）").arg(codeOrder.join(QLatin1Char(','))));
        check(sameList(parallelRun1, parallelRun2),
              "并行分支连跑两次 -> 逐字段（file/line/column/code/severity/effect/message/"
              "snippet）且逐位置相同");
        check(sameList(parallelRun1, serialRun),
              "并行分支 == 串行分支（maxThreadCount=1）-> 逐字段且逐位置相同");

        // 7b) 归并顺序 = 脚本名序（装载序是 D,C,B,A）。
        QStringList fileOrder;
        for (const ParseDiagnostic& d : parallelRun1)
            if (fileOrder.isEmpty() || fileOrder.last() != d.file) fileOrder.append(d.file);
        check(fileOrder == QStringList({QStringLiteral("A.ERB"), QStringLiteral("B.ERB"),
                                        QStringLiteral("C.ERB"), QStringLiteral("D.ERB")}),
              QStringLiteral("归并顺序 = 脚本名序 A,B,C,D（得到 %1；装载序是 D,C,B,A）")
                  .arg(fileOrder.join(QLatin1Char(','))));

        // 最小复现：B.ERB **先**装载、A.ERB 后装载（2 个脚本 -> 串行分支）。
        ValidationFixture reversed;
        reversed.addScript(QStringLiteral("B.ERB"), validationSource(1));
        reversed.addScript(QStringLiteral("A.ERB"), validationSource(0));
        const QList<ParseDiagnostic> reversedRun = runFinalizeValidation(reversed);
        check(reversedRun.size() >= 2
                  && reversedRun.first().file == QStringLiteral("A.ERB")
                  && reversedRun.last().file == QStringLiteral("B.ERB"),
              QStringLiteral("B.ERB 先装载、A.ERB 后装载 -> A.ERB 的诊断排在 B.ERB 之前"));

        // 7c) 数字字面量去重（diagnoseIntegerLiterals 的键 = code+file+line+column）。
        //     **这条钉住 diagnoseIntegerLiterals 的 file+line+column 去重**：
        //     同一行两处同一个超范围字面量算出的列号相同（列号由
        //     `line.raw.indexOf(text)` 取首个出现）-> 仍然只报一条。
        ValidationFixture dupSameLine;
        dupSameLine.addScript(QStringLiteral("dup.ERB"),
                              {QStringLiteral("@MAIN"),
                               QStringLiteral("X = 9999999999999999999999, 9999999999999999999999")});
        const auto dupSameLineHits =
            withCode(runFinalizeValidation(dupSameLine), DiagCode::kNumericConversion);
        check(dupSameLineHits.size() == 1,
              QStringLiteral("同一行两处同一个非法数字字面量 -> 1 条（去重；得到 %1）")
                  .arg(dupSameLineHits.size()));

        // 两行各一处：行号不同 -> 两条（去重按位置，不是按值）。
        ValidationFixture dupTwoLines;
        dupTwoLines.addScript(QStringLiteral("dup2.ERB"),
                              {QStringLiteral("@MAIN"),
                               QStringLiteral("X = 9999999999999999999999"),
                               QStringLiteral("Y = 9999999999999999999999")});
        const auto dupTwoLineHits =
            withCode(runFinalizeValidation(dupTwoLines), DiagCode::kNumericConversion);
        check(dupTwoLineHits.size() == 2,
              QStringLiteral("两行各一处 -> 2 条（位置不同；得到 %1）").arg(dupTwoLineHits.size()));

        // 同一位置的字面量出现在**不同脚本**：键里的文件名不同 -> 各自保留 4 条。
        ValidationFixture dupCrossScript;
        for (const QString& n : loadOrder)
            dupCrossScript.addScript(n, {QStringLiteral("@MAIN"),
                                         QStringLiteral("X = 9999999999999999999999")});
        const auto dupCrossHits =
            withCode(runFinalizeValidation(dupCrossScript), DiagCode::kNumericConversion);
        check(dupCrossHits.size() == 4,
              QStringLiteral("4 个脚本各有一处同一位置的字面量 -> 4 条（键含文件名，不跨脚本去重；得到 %1）")
                  .arg(dupCrossHits.size()));
    }

    // ------------------------------------------------------------------
    // 8) 装载期常数求值的作用域 = **行所属函数**（求值参数），不是
    //    「解析表当前停在哪一行」。
    //
    // 旧实现里四个常数回调都用「解析表当前的脚本 + 行号」现算作用域。装载期
    // 那个位置只是「第一个装载的脚本、行 0」（从未执行过），与「这一行属于
    // 哪个函数」无关：这里 line 0 是 @A，于是 @B 的维数表达式会被按 @A 的
    // 私有常量求值。
    //
    // 常数写成表达式（`11 + 0`）而不是整数字面量：字面量在**解析期**就按
    // 显式 scope 折成 Literal（那条路径本来就带作用域参数），不会经过回调；
    // 表达式常量是惰性求值，1/6 解析维数时才会经 provider 查表 —— 正是
    // 旧实现用错作用域的那条路径。
    // ------------------------------------------------------------------
    qDebug() << "\n8) 装载期常数求值的作用域 = 行所属函数（不是当前位置）";
    {
        ProcessState scopeState;
        VariableStorage scopeStorage;
        EraParseTable scopeTable(&scopeState);
        ExpressionEvaluator scopeEvaluator;
        scopeTable.setExpressionEvaluator(&scopeEvaluator);
        scopeTable.setVariableStorage(&scopeStorage);

        const QStringList scopeSource = {
            QStringLiteral("@A"),                       // 0：旧实现装载期看到的「位置」
            QStringLiteral("#DIM CONST N = 11 + 0"),    // 1：@A 的表达式常量
            QStringLiteral("#DIM ARR_A, N + 1"),        // 2：维数 = 12
            QStringLiteral("RETURN"),                   // 3
            QStringLiteral("@B"),                       // 4
            QStringLiteral("#DIM CONST N = 22 + 0"),    // 5：@B 同名、不同值
            QStringLiteral("#DIM ARR_B, N + 1"),        // 6：维数必须是 23
            QStringLiteral("RETURN"),                   // 7
        };
        check(scopeTable.loadScript(QStringLiteral("scope.ERB"),
                                    buildLines(scopeTable, scopeSource)),
              "作用域夹具装载成功");
        // loadScript 只把 m_position.script 指向本脚本；line 仍是 0（@A 行）。
        scopeTable.markLoadComplete();

        const auto* declA =
            scopeTable.variableTable().find(QStringLiteral("ARR_A"), QStringLiteral("A"));
        const auto* declB =
            scopeTable.variableTable().find(QStringLiteral("ARR_B"), QStringLiteral("B"));
        check(declA && declA->lengths == QList<int>{12},
              QStringLiteral("@A 的 ARR_A 维数 = 12（N=11；得到 %1）")
                  .arg(declA ? declA->lengths.value(0) : -1));
        check(declB && declB->lengths == QList<int>{23},
              QStringLiteral("@B 的 ARR_B 维数 = 23（@B 的 N=22，不是 @A 的 11；得到 %1）")
                  .arg(declB ? declB->lengths.value(0) : -1));
        // 广域常量（scope 0）不受影响：文件级声明 + 维数引用。
        // （同一夹具里没有广域常量；用 provider 的直接断言见 test_statements 17。）
    }

    qDebug() << "\n================================";
    if (g_failures == 0) qDebug() << "ALL PASS";
    else qDebug() << g_failures << "FAILURE(S)";
    return g_failures == 0 ? 0 : 1;
}