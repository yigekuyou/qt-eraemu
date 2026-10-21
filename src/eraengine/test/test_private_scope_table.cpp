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
// test_private_scope_table.cpp
//
// 私有作用域名表：**装载期建表 == 运行期旧表**。
//
// 旧实现（VariableStorage::setPrivateScope）在第一次进入某函数时，按调用方
// 传进来的名字表构建「名字 -> FUNC\x1fNAME」；新实现由声明（VariableTable 的
// Local 声明）在装载收尾一次性建好并 install 给存储层。
//
// 本测试锁定三件事：
//   1. 两条路径对同一 (函数, 名字) 得到的存储键**逐字节相同**（含大小写规范化）；
//   2. 运行期写进去的值，在旧路径算出的键下也能读到（值没有搬到别的槽位）；
//   3. 安装后 setPrivateScope 的 names 参数被忽略（空名字表也能解析私有名），
//      但安装前仍按旧行为建表（测试的手工路径不崩）；REF 形参语义不变。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>

#include "ast/ast_builder.h"
#include "ast/expression_evaluator.h"
#include "ast/name_interner.h"
#include "ast/variable_table.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "process_state.h"
#include "script_runner.h"
#include "variable_storage.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static QList<LogicalLine> buildLines(EraParseTable& table, const QStringList& source) {
    QList<LogicalLine> out;
    for (int i = 0; i < source.size(); ++i) {
        out.append(AstBuilder::build(source.at(i), ScriptPosition("private.ERB", i, 0),
            [&table](const QString& text) { return table.expressionAst(text); }));
    }
    return out;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    qDebug() << "Private scope name table: load-time == runtime(legacy)";

    VariableStorage storage;
    ProcessState state;
    EraParseTable table(&state);
    ExecutionEngine engine(&storage, nullptr);
    ExpressionEvaluator evaluator;
    engine.setParseTable(&table);
    engine.setExpressionEvaluator(&evaluator);
    table.setExpressionEvaluator(&evaluator);
    table.setVariableStorage(&storage);
    ScriptRunner runner(&table, &engine, &state, &storage);
    runner.setExpressionEvaluator(&evaluator);
    runner.setStepLimit(100000);

    const eravar::ScopeId firstScope = eravar::internName(QStringLiteral("FIRST"));
    const eravar::ScopeId secondScope = eravar::internName(QStringLiteral("SECOND"));

    // ---- 1) 安装前：旧路径仍可用（测试手工 setPrivateScope 的入口） ----
    qDebug() << "\n1) 安装前的旧路径";
    check(!storage.privateScopeTableInstalled(), "fresh storage: load-time table not installed");
    check(storage.storageName(QStringLiteral("ALPHA"), firstScope) == QStringLiteral("ALPHA"),
          "no table yet: private name resolves to the bare key");
    storage.setPrivateScope(QStringLiteral("FIRST"),
                            {QStringLiteral("ALPHA"), QStringLiteral("GHOST")});
    check(!storage.privateScopeTableInstalled(), "manual setPrivateScope does not mark the table installed");
    check(storage.storageName(QStringLiteral("ALPHA"), firstScope) == QStringLiteral("FIRST\x1f" "ALPHA"),
          "pre-install manual setPrivateScope builds the legacy table");

    // ---- 2) 装载 + 收尾（安装点 = finalizeBindings 末尾） ----
    qDebug() << "\n2) 装载收尾安装";
    const QStringList source = {
        QStringLiteral("@MAIN"),
        QStringLiteral("#DIM ARR, 3"),
        QStringLiteral("ARR:1 = 7"),
        QStringLiteral("CALL FIRST"),
        QStringLiteral("CALL SECOND"),
        QStringLiteral("CALL TAKE(ARR)"),
        QStringLiteral("RETURN"),
        QStringLiteral("@FIRST"),
        QStringLiteral("#DIM ALPHA"),
        QStringLiteral("#DIMS BETA"),
        QStringLiteral("ALPHA = 11"),
        QStringLiteral("BETA '= \"aaa\""),
        QStringLiteral("RETURN"),
        QStringLiteral("@SECOND"),
        QStringLiteral("#DIM ALPHA"),
        QStringLiteral("#DIM TWO, 2, 2"),
        QStringLiteral("ALPHA = 22"),
        QStringLiteral("RETURN"),
        QStringLiteral("@TAKE(A)"),
        QStringLiteral("#DIM REF A, 3"),
        QStringLiteral("CHECK = A:1"),
        QStringLiteral("RETURN")
    };
    check(table.loadScript(QStringLiteral("private"), buildLines(table, source)), "loadScript");
    table.setEntryPoint(QStringLiteral("MAIN"));
    table.markLoadComplete();
    check(storage.privateScopeTableInstalled(), "markLoadComplete installs the load-time table");
    check(runner.runToCompletion() == ExecState::Halt, "script completes");

    // ---- 3) 安装后 names 参数被忽略（单一真相 = 声明） ----
    qDebug() << "\n3) 安装后 setPrivateScope 只记录当前帧";
    storage.setPrivateScope(QStringLiteral("FIRST"), {});   // 空名字表
    check(storage.storageName(QStringLiteral("ALPHA"), firstScope) == QStringLiteral("FIRST\x1f" "ALPHA"),
          "installed table: empty names list still resolves FIRST/ALPHA");
    check(storage.getGlobalInt1D(QStringLiteral("ALPHA"), 0) == 11,
          "FIRST/ALPHA == 11 (written by the frame that ran with the load-time table)");
    check(storage.getStringSlot(QStringLiteral("BETA"), {}).text == QStringLiteral("aaa"),
          "FIRST/BETA == \"aaa\"");
    // 安装前手工塞进去的 GHOST 不在声明里 -> 安装整表替换后不再有映射。
    check(storage.storageName(QStringLiteral("GHOST"), firstScope) == QStringLiteral("GHOST"),
          "pre-install manual-only name GHOST is dropped by the installed table");
    storage.setPrivateScope(QStringLiteral("SECOND"), {});
    check(storage.getGlobalInt1D(QStringLiteral("ALPHA"), 0) == 22,
          "SECOND/ALPHA == 22 (same name, own private slot)");
    check(storage.storageName(QStringLiteral("ALPHA"), firstScope) !=
              storage.storageName(QStringLiteral("ALPHA"), secondScope),
          "cross-function same-name privates get distinct storage keys");
    check(storage.getGlobalInt1D(QStringLiteral("CHECK"), 0) == 7,
          "REF formal still aliases the caller array (#DIM REF A, 3 -> ARR:1 == 7)");

    // ---- 4) 装载期表 == 旧路径重建的表（逐项） ----
    qDebug() << "\n4) 装载期表 == 运行期旧表（逐项）";
    const VariableTable& vt = table.variableTable();
    int compared = 0;
    int mismatched = 0;
    const QStringList functions = {QStringLiteral("MAIN"), QStringLiteral("FIRST"),
                                   QStringLiteral("SECOND"), QStringLiteral("TAKE")};
    for (const QString& function : functions) {
        // 旧路径：一个干净的存储对象，按调用方（ScriptRunner 同款）名字表建表。
        VariableStorage legacy;
        legacy.setPrivateScope(function, vt.localNamesOfRef(function));
        const eravar::ScopeId scope = eravar::internName(function);
        int declared = 0;
        for (const VariableDecl& decl : vt.localsOf(function)) {
            if (decl.isConst) continue;
            ++declared;
            const QString keyOld = legacy.storageName(decl.name, scope);
            const QString keyNew = storage.storageName(decl.name, scope);
            ++compared;
            if (keyOld != keyNew) {
                ++mismatched;
                qCritical().noquote() << "  MISMATCH" << function << decl.name
                                      << "old=" << keyOld << "new=" << keyNew;
            }
        }
        check(declared > 0, QStringLiteral("%1 declares private variables (%2)").arg(function).arg(declared));
    }
    check(mismatched == 0 && compared >= 4,
          QStringLiteral("old vs load-time table: %1 keys compared, %2 mismatches")
              .arg(compared).arg(mismatched));

    // 负向：一个函数里没声明的名字在另一函数作用域内仍是裸名（两边一致）。
    VariableStorage legacyFirst;
    legacyFirst.setPrivateScope(QStringLiteral("SECOND"), vt.localNamesOfRef(QStringLiteral("SECOND")));
    check(legacyFirst.storageName(QStringLiteral("BETA"), secondScope) ==
              storage.storageName(QStringLiteral("BETA"), secondScope),
          "name declared only in FIRST stays bare in SECOND (both paths agree)");

    // 值也对齐：旧路径算出的键在安装后的存储上读得到运行期写入的值。
    VariableStorage legacyValues;
    legacyValues.setPrivateScope(QStringLiteral("FIRST"),
                                 vt.localNamesOfRef(QStringLiteral("FIRST")));
    const QString legacyAlphaKey = legacyValues.storageName(QStringLiteral("ALPHA"), firstScope);
    check(storage.getGlobalInt1D(legacyAlphaKey, 0) == 11,
          "value written at runtime is readable under the legacy-computed key");

    // ---- 5) RESETDATA 不能把装载期名表一起清掉 ----
    // resetForNewGame 会 purge 私有存储键（数据回默认），名表是**装载期元数据**，
    // 必须留下 —— 否则安装标记还为 true 而表已空，所有私有名静默退化成裸名。
    qDebug() << "\n5) RESETDATA 之后名表仍在";
    storage.resetForNewGame();
    check(storage.privateScopeTableInstalled(), "resetForNewGame keeps the table installed");
    storage.setPrivateScope(QStringLiteral("FIRST"), {});
    check(storage.storageName(QStringLiteral("ALPHA"), firstScope) == QStringLiteral("FIRST\x1f" "ALPHA"),
          "private name still resolves after resetForNewGame");
    check(storage.getGlobalInt1D(QStringLiteral("ALPHA"), 0) == 0,
          "private value is back to its declared default after resetForNewGame");

    qDebug() << "\n================================";
    if (g_failures == 0) qDebug() << "ALL PASS";
    else qDebug() << g_failures << "FAILURE(S)";
    return g_failures == 0 ? 0 : 1;
}