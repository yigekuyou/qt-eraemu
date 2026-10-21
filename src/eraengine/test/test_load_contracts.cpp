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
#include <QStringList>

#include "ast/ast_builder.h"
#include "ast/name_interner.h"
#include "ast/variable_table.h"
#include "era_parse_table.h"
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

    qDebug() << "\n================================";
    if (g_failures == 0) qDebug() << "ALL PASS";
    else qDebug() << g_failures << "FAILURE(S)";
    return g_failures == 0 ? 0 : 1;
}