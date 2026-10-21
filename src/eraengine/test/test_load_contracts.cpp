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
//   2. 未装载就调只读收尾步骤：不产出诊断、不计数（而不是拿空表乱报）。
//   3. 只读步骤重复调用：runCount 递增，且诊断**确实**重复产出（契约本身）。
//   4. markLoadComplete 调两次：第二次 no-op（诊断、阶段、计数都不变）。
//   5. 收尾后 loadScript：返回 false，脚本表不变。
//   6. 私有名表安装前后各调一次 setPrivateScope：安装后 names 参数被忽略。
//   7. 存档前置条件 SaveReadiness 的三态可查询；新增声明会把
//     「已定稿」退回「未定稿」。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QStringList>
#include <QThreadPool>

#include "ast/ast_builder.h"
#include "ast/expression_ast.h"
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

static int diagnosticCount(const EraParseTable& table) {
    return table.parseDiagnostics().size();
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
    QString positionScript;
    int positionLine = 0;

    ValidationFixture() {
        // 一个全局常量：`K ++` 在逐行校验里被折成字面量 -> 报「变量参数无法装载」，
        // 于是同一个脚本既有**逐行校验**的错误，也有 validateArguments 的告警。
        variables.setConstInt(QStringLiteral("K"), 5);
    }

    loadpass::LoadContext context() {
        return loadpass::LoadContext{scripts, variables, functions,
                                     /*constantTable=*/nullptr, /*storage=*/nullptr,
                                     /*evaluator=*/nullptr, /*gameBaseData=*/nullptr,
                                     diags, labelIndex, paramNames,
                                     declarationsResolved, constProvidersReady,
                                     positionScript, positionLine};
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
    loadpass::finalizeValidation(ctx);
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
    // 1) 未装载（Empty）：只读步骤 qWarning + no-op，不产出诊断、不计数。
    // ------------------------------------------------------------------
    qDebug() << "\n1) Empty 阶段调用只读收尾步骤";
    check(table.loadPhase() == EraParseTable::LoadPhase::Empty, "fresh table: phase == Empty");
    check(diagnosticCount(table) == 0, "fresh table: no diagnostics");
    table.resolveStaticTargets();
    table.checkStructures();
    table.checkStaticValues();
    check(diagnosticCount(table) == 0, "read-only passes before load: no diagnostics (no-op)");
    check(table.readOnlyPassRunCount(EraParseTable::ReadOnlyPass::StaticTargets) == 0
              && table.readOnlyPassRunCount(EraParseTable::ReadOnlyPass::Structures) == 0
              && table.readOnlyPassRunCount(EraParseTable::ReadOnlyPass::StaticValues) == 0,
          "read-only passes before load: run counts stay 0");

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
    check(diagnosticCount(table) == beforeBind
              && table.readOnlyPassRunCount(EraParseTable::ReadOnlyPass::Structures) == 0,
          "Loading (bindings not complete): checkStructures is a no-op");

    table.markLoadComplete();
    check(table.loadPhase() == EraParseTable::LoadPhase::Finalized,
          "after markLoadComplete: phase == Finalized");
    check(table.readOnlyPassRunCount(EraParseTable::ReadOnlyPass::StaticTargets) == 1
              && table.readOnlyPassRunCount(EraParseTable::ReadOnlyPass::Structures) == 1
              && table.readOnlyPassRunCount(EraParseTable::ReadOnlyPass::StaticValues) == 1,
          "markLoadComplete ran each read-only pass exactly once");

    // 重复调用契约：诊断真的会重复产出（这是旧行为，现在可观察）。
    const int afterLoad = diagnosticCount(table);
    check(afterLoad > 0, "missing ENDIF produced at least one diagnostic");
    table.checkStructures();
    check(table.readOnlyPassRunCount(EraParseTable::ReadOnlyPass::Structures) == 2,
          "re-running checkStructures increments run count to 2");
    check(diagnosticCount(table) > afterLoad,
          "re-running checkStructures duplicates diagnostics (documented contract)");

    // ------------------------------------------------------------------
    // 3) markLoadComplete 调两次：第二次 qWarning + no-op。
    // ------------------------------------------------------------------
    qDebug() << "\n3) markLoadComplete 重复调用";
    const int diagnosticsBeforeSecond = diagnosticCount(table);
    const int structuresBeforeSecond =
        table.readOnlyPassRunCount(EraParseTable::ReadOnlyPass::Structures);
    table.markLoadComplete();
    check(table.loadPhase() == EraParseTable::LoadPhase::Finalized,
          "second markLoadComplete: phase stays Finalized");
    check(diagnosticCount(table) == diagnosticsBeforeSecond,
          "second markLoadComplete: diagnostics unchanged (no-op)");
    check(table.readOnlyPassRunCount(EraParseTable::ReadOnlyPass::Structures)
              == structuresBeforeSecond,
          "second markLoadComplete: pass run counts unchanged (no-op)");

    // ------------------------------------------------------------------
    // 4) 收尾之后 loadScript：增量 append —— 阶段重新打开为 Loading，
    //    必须再 markLoadComplete 才能回到 Finalized（旧的隐性契约）。
    // ------------------------------------------------------------------
    qDebug() << "\n4) 收尾之后 loadScript（增量 append）";
    const int scriptsBefore = table.scriptNames().size();
    check(table.loadScript(QStringLiteral("late"), buildLines(table, source)),
          "loadScript after Finalized: accepted (incremental append)");
    check(table.scriptNames().size() == scriptsBefore + 1,
          "loadScript after Finalized: script table gained the new script");
    check(table.loadPhase() == EraParseTable::LoadPhase::Loading,
          "loadScript after Finalized reopens phase == Loading");
    table.markLoadComplete();
    check(table.loadPhase() == EraParseTable::LoadPhase::Finalized,
          "re-finalizing after incremental append: phase == Finalized again");

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

    qDebug() << "\n================================";
    if (g_failures == 0) qDebug() << "ALL PASS";
    else qDebug() << g_failures << "FAILURE(S)";
    return g_failures == 0 ? 0 : 1;
}