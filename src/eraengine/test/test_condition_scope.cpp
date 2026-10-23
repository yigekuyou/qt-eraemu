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
// test_condition_scope.cpp
//
// 刀 B1 护栏：IF/SIF/ELSEIF/WHILE/REPEAT 条件的「是否需要执行位置」在
// **装载期**定完（LogicalLine::conditionNeedsScope），运行期只读一次。
//
// 判据 = 条件 AST 里任一 VariableNode 的 kind 属于
//        {Param, Chara, Const, Dynamic}（VariableNode::needsExecutionScope）。
// 其余 kind（10 类自带键 + Pseudo/System）忽略 scope。
//
// 这里钉住：
//   1. 纯键类条件（FLAG:0 / 函数内 #DIM 私有数组 / LOCAL）-> false
//   2. 私有 CHARADATA / 内建 CFLAG -> true
//   3. 私有 #DIM CONST（标量与数组）-> true
//   4. Dynamic 回退（TA） -> true；无变量条件 -> false
//   5. 混合条件（FLAG && CFLAG）-> true
//   6. 语义靶子：同名私有量在 @A/@B 各取各的（可写私有量 / CONST / CONST 数组）；
//      CHARADATA 条件在两个角色上各取各的。
//   7. 字符串形参的**带下标**读取（readStringParameterAlias 的 aliased && Str
//      分支）消费 scope：`S:1` 必须取本函数私有槽，不能取 m_parameters 的值绑定
//      或裸名槽 —— 三者给三个不同的值，用 GP 记录实际读到哪一个。
//
// 结构证据（见报告，不在生产代码里放计数器）：
//   evaluateCondition 里 executionScopeId() **只出现一次**，就是
//   `ll->conditionNeedsScope ? executionScopeId() : eravar::kGlobalScope`
//   这一处三元判定；evaluateAst 显式收 ScopeId（无默认参数）。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include "ast/ast_builder.h"
#include "ast/expression_evaluator.h"
#include "ast/expression_ast.h"
#include "ast/name_interner.h"
#include "process_state.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "script_runner.h"
#include "variable_storage.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

// ---- 编译期：判定表本身（needsExecutionScope 是 constexpr）----------------
static_assert(!VariableNode::needsExecutionScope(VariableNode::AccessKind::GlobalInt1),
              "键自足的 GlobalInt1 不需要执行位置");
static_assert(!VariableNode::needsExecutionScope(VariableNode::AccessKind::GlobalStr3),
              "键自足的 GlobalStr3 不需要执行位置");
static_assert(!VariableNode::needsExecutionScope(VariableNode::AccessKind::LocalSlot),
              "LOCAL 槽位不需要执行位置");
static_assert(!VariableNode::needsExecutionScope(VariableNode::AccessKind::System),
              "System（有存储槽）不需要执行位置");
static_assert(VariableNode::needsExecutionScope(VariableNode::AccessKind::Chara),
              "Chara 需要执行位置");
static_assert(VariableNode::needsExecutionScope(VariableNode::AccessKind::Const),
              "Const 需要执行位置");
static_assert(VariableNode::needsExecutionScope(VariableNode::AccessKind::Dynamic),
              "Dynamic 回退需要执行位置");
// 新契约：字符串形参的私有槽解析（readStringParameterAlias 的
// resolvedStorageName / getStringSlot(…, scope)）要 scope，所以 Param 属于
// true 分支 —— 这条 static_assert 就是「Param 不许再退回 false」的编译期证据。
static_assert(VariableNode::needsExecutionScope(VariableNode::AccessKind::Param),
              "形参（字符串私有槽解析）需要执行位置");
static_assert(!VariableNode::needsExecutionScope(VariableNode::AccessKind::Pseudo),
              "Pseudo 由名字命中的原语处理，不需要执行位置");

static QSharedPointer<ExpressionNode> compileFixture(EraParseTable& runtime, const QString& text) {
    // 与 test_statements 同型：AST 由独立的装载表编译，运行表提供声明/类型。
    EraParseTable compiler(nullptr);
    auto ast = compiler.expressionAst(text);
    const auto* current = runtime.lineAt(runtime.currentScript(), runtime.currentLine());
    if (ast) VariableTable::applyTypes(*ast, runtime.variableTable(),
                                       current ? current->scopeKey : QString());
    return ast;
}

static QList<LogicalLine> buildLines(EraParseTable& table, const QStringList& src) {
    const AstResolver resolve = [&table](const QString& e) { return compileFixture(table, e); };
    QList<LogicalLine> out;
    for (int i = 0; i < src.size(); ++i) {
        out.append(AstBuilder::build(src.at(i), ScriptPosition("cond_scope.ERB", i, 0), resolve));
    }
    return out;
}

// 按源码原文找装载后的行（raw 是装载期原文，裁剪后逐字比较）。
static const LogicalLine* findLine(const EraParseTable& table, const QString& script,
                                   const QString& raw) {
    const ScriptData* sd = table.script(script);
    if (!sd) return nullptr;
    for (const LogicalLine& ll : sd->lines) {
        if (ll.raw.trimmed() == raw) return &ll;
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Condition scope datum (load-time) regression";
    qDebug() << "===========================================";

    VariableStorage storage;
    ProcessState state;
    EraParseTable table(&state);
    ExpressionEvaluator evaluator;
    table.setExpressionEvaluator(&evaluator);
    table.setVariableStorage(&storage);
    ExecutionEngine engine(&storage, nullptr);
    engine.setParseTable(&table);
    engine.setExpressionEvaluator(&evaluator);
    ScriptRunner runner(&table, &engine, &state, &storage);
    runner.setExpressionEvaluator(&evaluator);
    runner.setStepLimit(200000);

    // 两个运行时角色（CHARADATA 语义靶子用）。
    storage.addChara(100);
    storage.addChara(200);

    const QStringList src = {
        QStringLiteral("@MAIN"),
        QStringLiteral("CALL A"),
        QStringLiteral("CALL B"),
        QStringLiteral("RETURN"),
        QStringLiteral("@A"),
        QStringLiteral("#DIM SHAREDV = 11"),          // 私有可写变量（键自带作用域）
        QStringLiteral("#DIM CONST PRIVC = 11"),      // 私有常数（标量）
        QStringLiteral("#DIM CONST ARR, 2 = 11, 12"), // 私有常数数组
        QStringLiteral("#DIM CHARADATA CD, 2"),       // 私有 CHARADATA
        QStringLiteral("CD:0:0 = 111"),
        QStringLiteral("CD:1:0 = 222"),
        QStringLiteral("GA = 0"),
        QStringLiteral("GA2 = 0"),
        QStringLiteral("GA3 = 0"),
        QStringLiteral("GA4 = 0"),
        QStringLiteral("GA5 = 0"),
        QStringLiteral("GA6 = 0"),
        QStringLiteral("GA7 = 0"),
        QStringLiteral("GA8 = 0"),
        QStringLiteral("IF SHAREDV == 11"),           // 纯键类
        QStringLiteral("GA = 1"),
        QStringLiteral("ENDIF"),
        QStringLiteral("IF PRIVC == 11"),             // Const
        QStringLiteral("GA2 = 1"),
        QStringLiteral("ENDIF"),
        QStringLiteral("IF ARR:0 == 11"),             // Const 数组
        QStringLiteral("GA3 = 1"),
        QStringLiteral("ENDIF"),
        QStringLiteral("TARGET = 0"),
        QStringLiteral("IF CD:0 == 111"),             // 私有 CHARADATA（角色 0）
        QStringLiteral("GA4 = 1"),
        QStringLiteral("ENDIF"),
        QStringLiteral("TARGET = 1"),
        QStringLiteral("IF CD:0 == 222"),             // 私有 CHARADATA（角色 1）
        QStringLiteral("GA5 = 1"),
        QStringLiteral("ENDIF"),
        QStringLiteral("IF FLAG:0 == 0"),             // System
        QStringLiteral("GA6 = 1"),
        QStringLiteral("ENDIF"),
        QStringLiteral("IF CFLAG:0 == 0"),            // 内建角色变量
        QStringLiteral("GA7 = 1"),
        QStringLiteral("ENDIF"),
        QStringLiteral("IF TA:0:0:0 == 0"),           // Dynamic 回退（TA 是 3 维）
        QStringLiteral("GA8 = 1"),
        QStringLiteral("ENDIF"),
        QStringLiteral("TARGET = 0"),
        QStringLiteral("RETURN"),
        QStringLiteral("@B"),
        QStringLiteral("#DIM SHAREDV = 22"),
        QStringLiteral("#DIM CONST PRIVC = 22"),
        QStringLiteral("#DIM CONST ARR, 2 = 21, 22"),
        QStringLiteral("GB = 0"),
        QStringLiteral("GB2 = 0"),
        QStringLiteral("GB3 = 0"),
        QStringLiteral("IF SHAREDV == 22"),
        QStringLiteral("GB = 1"),
        QStringLiteral("ENDIF"),
        QStringLiteral("IF PRIVC == 22"),
        QStringLiteral("GB2 = 1"),
        QStringLiteral("ENDIF"),
        QStringLiteral("IF ARR:0 == 21"),
        QStringLiteral("GB3 = 1"),
        QStringLiteral("ENDIF"),
        QStringLiteral("RETURN"),
    };

    check(table.loadScript("cond_scope", buildLines(table, src)), "loadScript(cond_scope)");
    table.setEntryPoint("MAIN");
    table.markLoadComplete();

    // ------------------------------------------------------------------
    qDebug() << "\n1) 装载期 datum：哪些条件需要执行位置";
    // ------------------------------------------------------------------
    struct Expect { const char* raw; bool needs; const char* why; };
    const Expect expects[] = {
        {"IF SHAREDV == 11",     false, "函数私有可写变量：键自带作用域"},
        {"IF FLAG:0 == 0",       false, "FLAG 是 System（有存储槽）"},
        {"IF PRIVC == 11",       true,  "私有 #DIM CONST 标量"},
        {"IF ARR:0 == 11",       true,  "私有 #DIM CONST 数组"},
        {"IF CD:0 == 111",       true,  "私有 #DIM CHARADATA"},
        {"IF CFLAG:0 == 0",      true,  "内建角色变量 CFLAG"},
        {"IF TA:0:0:0 == 0",     true,  "Dynamic 回退（有系统元数据、无存储槽）"},
        {"IF SHAREDV == 22",     false, "@B 的私有可写变量同样不需要位置"},
        {"IF PRIVC == 22",       true,  "@B 的私有常数需要位置"},
        {"IF ARR:0 == 21",       true,  "@B 的私有常数数组需要位置"},
    };
    for (const Expect& e : expects) {
        const LogicalLine* ll = findLine(table, "cond_scope", QString::fromUtf8(e.raw));
        check(ll != nullptr, QString("找到条件行：%1").arg(QString::fromUtf8(e.raw)));
        if (ll) check(ll->conditionNeedsScope == e.needs,
                      QString("%1 -> conditionNeedsScope=%2（%3）")
                          .arg(QString::fromUtf8(e.raw))
                          .arg(ll->conditionNeedsScope ? "true" : "false")
                          .arg(QString::fromUtf8(e.why)));
    }

    // 混合条件：任一引用需要位置 -> true。
    {
        // 在独立脚本里加一行混合条件（A/B 两段已定型，不能再增行）。
        VariableStorage ms;
        ProcessState mps;
        EraParseTable mt(&mps);
        ExpressionEvaluator mev;
        mt.setExpressionEvaluator(&mev);
        mt.setVariableStorage(&ms);
        const QStringList mixed = {
            QStringLiteral("@M"),
            QStringLiteral("IF FLAG:0 == 0 && CFLAG:0 == 0"),
            QStringLiteral("MX = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("IF 1 == 1"),
            QStringLiteral("MX2 = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("RETURN"),
        };
        check(mt.loadScript("mixed", buildLines(mt, mixed)), "loadScript(mixed)");
        mt.setEntryPoint("M");
        mt.markLoadComplete();
        const LogicalLine* mix = findLine(mt, "mixed", QStringLiteral("IF FLAG:0 == 0 && CFLAG:0 == 0"));
        check(mix && mix->conditionNeedsScope, "混合条件 FLAG && CFLAG -> true");
        const LogicalLine* literals = findLine(mt, "mixed", QStringLiteral("IF 1 == 1"));
        check(literals && !literals->conditionNeedsScope,
              "无变量条件 IF 1 == 1 -> false（不取位置）");
        // 非条件行没有 datum（保持默认 false）。
        const LogicalLine* setter = findLine(mt, "mixed", QStringLiteral("MX = 1"));
        check(setter && !setter->conditionNeedsScope, "非条件行 conditionNeedsScope 恒 false");
    }

    // ------------------------------------------------------------------
    qDebug() << "\n1b) 字符串形参（带下标）：readStringParameterAlias 消费 scope";
    // ------------------------------------------------------------------
    {
        VariableStorage ps;
        ProcessState pps;
        EraParseTable pt(&pps);
        ExpressionEvaluator pev;
        pt.setExpressionEvaluator(&pev);
        pt.setVariableStorage(&ps);
        ExecutionEngine peng(&ps, nullptr);
        peng.setParseTable(&pt);
        peng.setExpressionEvaluator(&pev);
        ScriptRunner prun(&pt, &peng, &pps, &ps);
        prun.setExpressionEvaluator(&pev);
        prun.setStepLimit(200000);

        // 三份不同的值，GP 记录条件实际读到哪一份：
        //   m_parameters 的值绑定 = "実引数"（无 scope 时 readParameter 先命中）
        //   全局裸名槽 S:1        = "裸名槽"（无 scope 时按名解析的终局）
        //   本函数私有槽 S:1      = "私槽"  ← 唯一正确的一份
        const QStringList psrc = {
            QStringLiteral("@MAIN"),
            QStringLiteral("#DIMS GLOBAL S, 3"),      // 全局（裸名）字符串数组
            QStringLiteral("S:1 '= \"裸名槽\""),
            QStringLiteral("GP = 0"),
            QStringLiteral("CALL PSCOPE(\"実引数\")"),
            QStringLiteral("IF S:1 == \"裸名槽\""),    // 形参读写没有碰过裸名槽
            QStringLiteral("GQ = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("RETURN"),
            QStringLiteral("@PSCOPE(S)"),             // 字符串形参（传值绑定）
            // 私有字符串数组（形参私有槽）：初值 = 私有槽的内容（每次进函数按计划
            // 重写；形参的实参另走 m_parameters 值绑定，两者互不覆盖 —— 注意
            // writeStructuredValue 对 hasParameter 的名字一律写值绑定，
            // 所以私有槽只能靠声明初值，不能靠 S:1 '= …）
            QStringLiteral("#DIMS S, 3 = \"x\", \"私槽\", \"y\""),
            QStringLiteral("SIF S:1 == \"私槽\""),     // ← 本缺口：带下标的字符串形参
            QStringLiteral("GP = 1"),
            QStringLiteral("SIF S:1 == \"実引数\""),   // 无 scope 的旧读法 1：值绑定
            QStringLiteral("GP = 2"),
            QStringLiteral("SIF S:1 == \"裸名槽\""),   // 无 scope 的旧读法 2：裸名槽
            QStringLiteral("GP = 3"),
            QStringLiteral("RETURN"),
        };
        check(pt.loadScript("param_scope", buildLines(pt, psrc)), "loadScript(param_scope)");
        pt.setEntryPoint("MAIN");
        pt.markLoadComplete();

        const LogicalLine* paramCond =
            findLine(pt, "param_scope", QStringLiteral("SIF S:1 == \"私槽\""));
        check(paramCond != nullptr, "找到字符串形参条件行");
        if (paramCond)
            check(paramCond->conditionNeedsScope,
                  "字符串形参 S:1 的条件 -> conditionNeedsScope=true（Param 消费 scope）");

        check(prun.runToCompletion() == ExecState::Halt, "param_scope 程序正常结束");
        const qint64 gp = ps.getGlobalInt1D("GP", 0);
        check(gp == 1,
              QStringLiteral("条件里的 S:1 取到形参私有槽 \"私槽\"（GP=%1；"
                             "2 = 读成了 m_parameters 值绑定，"
                             "3 = 读成了全局裸名槽）").arg(gp));
        check(ps.getGlobalInt1D("GQ", 0) == 1, "裸名槽 S:1 未被形参的读写碰过");
        check(ps.getStringSlot(QStringLiteral("S"), {1}, eravar::kGlobalScope).text
                  == QStringLiteral("裸名槽"),
              "全局裸名槽 S:1 仍是 \"裸名槽\"");
    }

    // ------------------------------------------------------------------
    qDebug() << "\n2) 语义靶子：同名私有量在 @A/@B 各取各的";
    // ------------------------------------------------------------------
    check(runner.runToCompletion() == ExecState::Halt, "cond_scope 程序正常结束");
    check(storage.getGlobalInt1D("GA", 0) == 1,
          "@A 的 IF SHAREDV 取到本段私有值 11（key 自带作用域）");
    check(storage.getGlobalInt1D("GB", 0) == 1,
          "@B 的 IF SHAREDV 取到本段私有值 22（key 自带作用域）");
    check(storage.getGlobalInt1D("GA2", 0) == 1,
          "@A 的 IF PRIVC 取到本段私有常数 11（scope 参数）");
    check(storage.getGlobalInt1D("GB2", 0) == 1,
          "@B 的 IF PRIVC 取到本段私有常数 22（scope 参数）");
    check(storage.getGlobalInt1D("GA3", 0) == 1,
          "@A 的 IF ARR:0 取到本段私有常数数组 11（scope 参数）");
    check(storage.getGlobalInt1D("GB3", 0) == 1,
          "@B 的 IF ARR:0 取到本段私有常数数组 21（scope 参数）");

    // ------------------------------------------------------------------
    qDebug() << "\n3) 语义靶子：CHARADATA 条件在两个角色上各取各的";
    // ------------------------------------------------------------------
    check(storage.getGlobalInt1D("GA4", 0) == 1,
          "TARGET=0 时 IF CD:0 读到角色 0 的 111");
    check(storage.getGlobalInt1D("GA5", 0) == 1,
          "TARGET=1 时 IF CD:0 读到角色 1 的 222（角色隔离）");

    // ------------------------------------------------------------------
    qDebug() << "\n4) 语义靶子：忽略位置的 kind 条件仍然为真";
    // ------------------------------------------------------------------
    check(storage.getGlobalInt1D("GA6", 0) == 1, "IF FLAG:0 == 0（System）为真");
    check(storage.getGlobalInt1D("GA7", 0) == 1, "IF CFLAG:0 == 0（Chara）为真");
    check(storage.getGlobalInt1D("GA8", 0) == 1, "IF TA:0:0:0 == 0（Dynamic 回退）为真");

    // ------------------------------------------------------------------
    qDebug() << "\n5) evaluateAst 显式作用域：同一条件 AST 两个 scope 各取各的";
    // ------------------------------------------------------------------
    {
        EraParseTable astCompiler(nullptr);
        const QSharedPointer<ExpressionNode> sharedAst = astCompiler.expressionAst("PRIVC");
        const QString scopeA = table.segmentKeyOfLabel(QStringLiteral("cond_scope"), QStringLiteral("A"));
        const QString scopeB = table.segmentKeyOfLabel(QStringLiteral("cond_scope"), QStringLiteral("B"));
        bool needs = false;
        VariableTable::applyTypes(*sharedAst, table.variableTable(), scopeA,
                                  nullptr, {}, &needs);
        check(needs, "applyTypes 的 needsScope 出参：私有常数 -> true");
        const eravar::ScopeId idA = eravar::internName(scopeA);
        const eravar::ScopeId idB = eravar::internName(scopeB);
        check(evaluator.evaluate(*sharedAst, &storage, nullptr, idA).toLongLong() == 11,
              "显式 scope=A -> PRIVC=11");
        check(evaluator.evaluate(*sharedAst, &storage, nullptr, idB).toLongLong() == 22,
              "显式 scope=B -> PRIVC=22");
    }

    qDebug() << "\n===========================================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] condition scope tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
