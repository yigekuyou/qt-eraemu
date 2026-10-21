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
    //   `A$ ++`          -> 3/6 finalizeValidation 的 RejectLine（origin=Load）
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
                                     return x.origin == DiagOrigin::Load;
                                 }),
              "3/6 的 RejectLine 归 origin=Load（写步骤不切片，见边界说明）");
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