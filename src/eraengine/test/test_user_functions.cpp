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
// test_user_functions.cpp
//
// 验证「用户自定义函数」在 AST 中的建模（对齐 C# FunctionLabelLine +
// ErbLoader.parseLabel / Process.callFunction）：
//   1. 形参归类 classifyUserParam（ARG / ARGS / 私有变量）
//   2. 标签两种语法的解析：@F(A,B) 与 @F, ARGS, ARG, ARGS:1
//   3. 声明节点：名字 / 形参表 / #FUNCTION(S) 返回类型 / #SINGLE 等 / 体区间
//   4. 作用域与参数绑定：CALL 语句调用 + 式中调用（表达式函数）
//   5. 实参个数校验（解析期告警）
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include "ast/ast_builder.h"
#include "ast/user_function.h"
#include "ast/expression_evaluator.h"
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

static QList<LogicalLine> buildLines(EraParseTable& table, const QStringList& src) {
    const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
    QList<LogicalLine> out;
    for (int i = 0; i < src.size(); ++i) {
        out.append(AstBuilder::build(src.at(i), ScriptPosition("test.ERB", i, 0), resolve));
    }
    return out;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "User-defined function (AST) test";
    qDebug() << "================================";

    // =====================================================================
    qDebug() << "\n1) 形参归类 classifyUserParam";
    {
        const UserParamDecl a = classifyUserParam(QStringLiteral("ARG"));
        check(a.target == UserParamTarget::Arg && a.index == 0 && a.type == OperandType::Int,
              "ARG -> Arg(0) Int");
        const UserParamDecl a3 = classifyUserParam(QStringLiteral("ARG:2"));
        check(a3.target == UserParamTarget::Arg && a3.index == 2, "ARG:2 -> Arg(2)");
        const UserParamDecl s = classifyUserParam(QStringLiteral("ARGS"));
        check(s.target == UserParamTarget::Args && s.index == 0 && s.type == OperandType::Str,
              "ARGS -> Args(0) Str");
        const UserParamDecl s1 = classifyUserParam(QStringLiteral("ARGS:1"));
        check(s1.target == UserParamTarget::Args && s1.index == 1, "ARGS:1 -> Args(1)");
        const UserParamDecl v = classifyUserParam(QStringLiteral("PNAME"));
        check(v.target == UserParamTarget::LocalVar && v.varName == QStringLiteral("PNAME"),
              "PNAME -> 私有变量");
    }

    // =====================================================================
    qDebug() << "\n2) 标签语法解析（两种形参写法）";
    {
        ProcessState state;
        EraParseTable table(&state);
        VariableStorage storage;
        table.setVariableStorage(&storage);

        const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };

        const LogicalLine paren = AstBuilder::build(
            QStringLiteral("@F(A, B)"), ScriptPosition(QStringLiteral("t.ERB"), 0, 0), resolve);
        check(paren.kind == LineKind::FunctionLabel && paren.labelName == QStringLiteral("F"),
              "@F(A, B) -> labelName == F");
        check(paren.labelArgs == QStringList({QStringLiteral("A"), QStringLiteral("B")}),
              "@F(A, B) -> labelArgs == [A, B]");

        const LogicalLine comma = AstBuilder::build(
            QStringLiteral("@IN_ROOM_MEMBER, ARGS, ARG, ARGS:1, ARG:1"),
            ScriptPosition(QStringLiteral("t.ERB"), 0, 0), resolve);
        check(comma.labelName == QStringLiteral("IN_ROOM_MEMBER"),
              "@IN_ROOM_MEMBER, ARGS, ... -> labelName == IN_ROOM_MEMBER");
        check(comma.labelArgs
                  == QStringList({QStringLiteral("ARGS"), QStringLiteral("ARG"),
                                  QStringLiteral("ARGS:1"), QStringLiteral("ARG:1")}),
              "逗号式形参表解析正确");

        const LogicalLine defv = AstBuilder::build(
            QStringLiteral("@K17_RS, ARGS, ARGS:1 = \"/\", ARG = 1"),
            ScriptPosition(QStringLiteral("t.ERB"), 0, 0), resolve);
        check(defv.labelName == QStringLiteral("K17_RS"), "带默认值的形参表 -> labelName == K17_RS");
        check(defv.labelArgs
                  == QStringList({QStringLiteral("ARGS"), QStringLiteral("ARGS:1"),
                                  QStringLiteral("ARG")}),
              "默认值 '=' 被剥离（ARGS / ARGS:1 / ARG）");

        const LogicalLine noArg = AstBuilder::build(
            QStringLiteral("@PLAIN"), ScriptPosition(QStringLiteral("t.ERB"), 0, 0), resolve);
        check(noArg.labelName == QStringLiteral("PLAIN") && noArg.labelArgs.isEmpty(),
              "@PLAIN -> 无形参");
    }

    // =====================================================================
    qDebug() << "\n3) 声明节点（形参 / 返回类型 / 标志 / 体区间）";
    VariableStorage storage;
    ProcessState state;
    EraParseTable table(&state);
    ExecutionEngine engine(&storage, nullptr);
    engine.setParseTable(&table);
    ExpressionEvaluator evaluator;
    engine.setExpressionEvaluator(&evaluator);
    table.setExpressionEvaluator(&evaluator);
    table.setVariableStorage(&storage);
    ScriptRunner runner(&table, &engine, &state, &storage);
    runner.setExpressionEvaluator(&evaluator);
    QObject::connect(&state, &ProcessState::continueExecution,
                     &runner, &ScriptRunner::onContinueExecution);

    const QStringList src = {
        QStringLiteral("@MAIN"),                       // 0
        QStringLiteral("CALL STR_FN(3, \"hi\")"),       // 1  ARG<-3, ARGS<-"hi"
        QStringLiteral("AFTER_ARG = ARG"),                  // 2  ARG 应指向 LOCAL[0]
        QStringLiteral("AFTER_ARGS = STRLENS(ARGS)"),        // 3  ARGS 应指向 LOCALS[0]
        QStringLiteral("LOCAL = INT_FN(4)"),           // 4  式中调用
        QStringLiteral("RETURN"),                      // 5
        QStringLiteral("@STR_FN, ARG, ARGS"),          // 6  逗号式：arg0->ARG, arg1->ARGS
        QStringLiteral("OUT1 = ARG"),
        QStringLiteral("OUT2 = STRLENS(ARGS)"),                      // 7
        QStringLiteral("RETURN"),                      // 8
        QStringLiteral("@INT_FN(X)"),                  // 9  #FUNCTION
        QStringLiteral("#DIM X"),                      // 私有整型变量 -> Int
        QStringLiteral("#FUNCTION"),                   // 10
        QStringLiteral("RETURN X * 10"),               // 11
        QStringLiteral("@EVT()"),                      // 12 #SINGLE
        QStringLiteral("#SINGLE"),                     // 13
        QStringLiteral("RETURN")                       // 14
    };

    check(table.loadScript("main", buildLines(table, src)), "loadScript(main)");
    // 形参/返回类型要在 finalizeParse（变量声明全部就绪）之后才定下来
    table.finalizeParse();

    const UserFunctionDecl* strFn = table.userFunction("STR_FN");
    check(strFn != nullptr, "STR_FN 已注册");
    if (strFn) {
        check(strFn->paramCount() == 2, "STR_FN 形参 2 个");
        check(strFn->params.at(0).target == UserParamTarget::Arg
                  && strFn->params.at(0).index == 0, "形参0 -> ARG:0");
        check(strFn->params.at(1).target == UserParamTarget::Args
                  && strFn->params.at(1).index == 0, "形参1 -> ARGS:0");
        check(strFn->maxArgIndex == 0 && strFn->maxArgsIndex == 0, "maxArg/maxArgs 下标 == 0");
        check(!strFn->isMethod, "STR_FN 不是 #FUNCTION（语句函数）");
        check(strFn->labelLine == 6 && strFn->endLine == 9,
              QString("STR_FN 体区间 [%1, %2] == [6, 9]").arg(strFn->labelLine).arg(strFn->endLine));
    }

    const UserFunctionDecl* intFn = table.userFunction("INT_FN");
    check(intFn && intFn->isMethod && intFn->returnType == OperandType::Int,
          "INT_FN 是 #FUNCTION，返回 Int");
    check(intFn && intFn->paramCount() == 1 && intFn->params.at(0).target == UserParamTarget::LocalVar,
          "INT_FN 形参 X 归为私有变量");
    check(intFn && intFn->params.at(0).typeKnown && intFn->params.at(0).type == OperandType::Int,
          "INT_FN 形参 X 类型由 #DIM 回填为 Int（强类型）");

    const UserFunctionDecl* evt = table.userFunction("EVT");
    check(evt && evt->isSingle, "EVT 带 #SINGLE 标志");

    qDebug() << "\n4) 作用域与参数绑定（CALL 语句 + 式中调用）";
    table.setEntryPoint("MAIN");
    const ExecState st = runner.runToCompletion();
    check(st == ExecState::Halt, "执行到 Halt");
    check(storage.getGlobalInt1D("OUT1", 0) == 3, "ARG 收到第 1 个实参 3（ARG 目标）");
    check(storage.getGlobalInt1D("OUT2", 0) == 2,
          "ARGS 收到第 2 个实参 \"hi\"（STRLENS == 2）");
    check(storage.getLocalInt(0) == 40, "式中调用 INT_FN(4) -> 40（LOCAL 是用户函数局部槽）");

    check(storage.getGlobalInt1D("AFTER_ARG", 0) == 0, "CALL restores caller integer locals");
    check(storage.getGlobalInt1D("AFTER_ARGS", 0) == 0, "CALL restores caller string locals");

    // =====================================================================
    qDebug() << "\n5) 实参个数校验（解析期）";
    {
        ProcessState s2;
        EraParseTable t2(&s2);
        VariableStorage st2;
        t2.setVariableStorage(&st2);
        const QStringList bad = {
            QStringLiteral("@A"),
            QStringLiteral("B = ONE(1, 2, 3)"),   // ONE 只有 1 个形参 -> 实参过多
            QStringLiteral("RETURN"),
            QStringLiteral("@ONE(X)"),
            QStringLiteral("#FUNCTION"),
            QStringLiteral("RETURN X")
        };
        t2.loadScript("bad", buildLines(t2, bad));
        t2.finalizeParse();
        const QString joined = t2.parseWarnings().join(QLatin1Char('\n'));
        check(joined.contains(QStringLiteral("实参过多")), "式中调用实参过多 -> 解析告警");
        check(joined.contains(QStringLiteral("ONE")), "告警含函数名 ONE");
    }

    // =====================================================================
    qDebug() << "\n6) 强类型：形参类型 / 返回类型 / 调用点校验";
    {
        ProcessState s3;
        EraParseTable t3(&s3);
        VariableStorage st3;
        t3.setVariableStorage(&st3);
        const QStringList typed = {
            QStringLiteral("@MAIN"),                 // 0
            QStringLiteral("R1 = TYPED(1, \"s\")"),  // 1 正确
            QStringLiteral("R2 = BAD(\"str\")"),     // 2 Str -> Int 形参：错误
            QStringLiteral("R3 = AUTOCONV(42)"),     // 3 Int -> Str 形参：允许（自动 TOSTR）
            QStringLiteral("R4 = TOOMANY(1, 2, 3)"), // 4 实参过多
            QStringLiteral("RETURN"),                // 5
            QStringLiteral("@TYPED(N, S)"),          // 6
            QStringLiteral("#DIM N"),                // 7
            QStringLiteral("#DIMS S"),               // 8
            QStringLiteral("#FUNCTION"),             // 9
            QStringLiteral("RETURN N"),              // 10
            QStringLiteral("@BAD(X)"),               // 11
            QStringLiteral("#DIM X"),                // 12
            QStringLiteral("#FUNCTION"),             // 13
            QStringLiteral("RETURN X"),              // 14
            QStringLiteral("@AUTOCONV(S2)"),         // 15
            QStringLiteral("#DIMS S2"),              // 16
            QStringLiteral("#FUNCTIONS"),            // 17
            QStringLiteral("RETURNF S2"),            // 18
            QStringLiteral("@TOOMANY(A)"),           // 19
            QStringLiteral("OUT1 = ARG"),
        QStringLiteral("OUT2 = STRLENS(ARGS)"),                // 20
            QStringLiteral("#FUNCTION"),             // 21
            QStringLiteral("RETURN A")               // 22
        };
        t3.loadScript(QStringLiteral("typed"), buildLines(t3, typed));
        t3.finalizeParse();

        const UserFunctionDecl* tdecl = t3.userFunction("TYPED");
        check(tdecl && tdecl->paramCount() == 2, "TYPED 形参 2 个");
        check(tdecl && tdecl->params.at(0).typeKnown && tdecl->params.at(0).type == OperandType::Int,
              "形参 N（#DIM）-> Int");
        check(tdecl && tdecl->params.at(1).typeKnown && tdecl->params.at(1).type == OperandType::Str,
              "形参 S（#DIMS）-> Str");
        check(tdecl && tdecl->isMethod && tdecl->returnType == OperandType::Int,
              "TYPED #FUNCTION -> 返回 Int");

        const UserFunctionDecl* adecl = t3.userFunction("AUTOCONV");
        check(adecl && adecl->isMethod && adecl->returnType == OperandType::Str,
              "AUTOCONV #FUNCTIONS -> 返回 Str");
        check(adecl && adecl->params.at(0).typeKnown && adecl->params.at(0).type == OperandType::Str,
              "AUTOCONV 形参 S2（#DIMS）-> Str");

        const QString joined = t3.parseWarnings().join(QLatin1Char('\n'));

        check(joined.contains(QStringLiteral("BAD")) && joined.contains(QStringLiteral("字符串")),
              "Str 实参 -> Int 形参：报错（不能从字符串转换为整数）");
        check(!joined.contains(QStringLiteral("AUTOCONV")),
              "Int 实参 -> Str 形参：允许（自动 TOSTR，无告警）");
        check(!joined.contains(QStringLiteral("TYPED")), "类型匹配的调用无告警");
        check(joined.contains(QStringLiteral("TOOMANY")) && joined.contains(QStringLiteral("实参过多")),
              "实参过多：报错");

        // 式中调用：返回类型进入 AST 强类型
        const QSharedPointer<ExpressionNode> intCall = t3.expressionAst(QStringLiteral("TYPED(1, \"s\")"));
        check(intCall && intCall->valueType() == OperandType::Int, "TYPED(...) 的 AST 类型 == Int");
        const QSharedPointer<ExpressionNode> strCall = t3.expressionAst(QStringLiteral("AUTOCONV(1)"));
        check(strCall && strCall->valueType() == OperandType::Str, "AUTOCONV(...) 的 AST 类型 == Str");
        // 字符串返回值可直接参与字符串运算（强类型传播）
        const QSharedPointer<ExpressionNode> concat =
            t3.expressionAst(QStringLiteral("AUTOCONV(1) + \"x\""));
        check(concat && concat->valueType() == OperandType::Str, "AUTOCONV(1) + \"x\" -> Str");
    }

    // Rendering-style nested calls: parameters must survive LOCAL loops and
    // both implicit return forms (next function label and physical EOF).
    {
        VariableStorage vars;
        ProcessState ps;
        EraParseTable pt(&ps);
        ExecutionEngine ex(&vars, nullptr);
        ExpressionEvaluator ev;
        ex.setParseTable(&pt);
        ex.setExpressionEvaluator(&ev);
        pt.setVariableStorage(&vars);
        pt.setExpressionEvaluator(&ev);
        ScriptRunner run(&pt, &ex, &ps, &vars);
        run.setExpressionEvaluator(&ev);
        run.setStepLimit(10000);
        const QStringList program = {
            "@MAIN", "LOCAL = 99", "CALL ROW(7, 9)",
            "PRESERVED = LOCAL", "CALL COUNTER", "CALL COUNTER", "RETURN",
            "@COUNTER", "LOCAL = LOCAL + 1", "PERSISTED_LOCAL = LOCAL", "RETURN",
            "@ROW(ROWNUM, OTHER)", "#DIM ROWNUM", "#DIM OTHER",
            "FOR LOCAL:0, 0, 12", "FOR LOCAL:1, 0, 3",
            "CALL COLOR(ROWNUM, OTHER)", "CELLS = CELLS + 1", "NEXT", "NEXT",
            "AFTER_ROW = ROWNUM", "RETURN",
            "@COLOR(COLORNUM, SECOND)", "#DIM COLORNUM", "#DIM SECOND",
            "LOCAL = 123", "COLORNUM = COLORNUM + 1", "CALL LAST(COLORNUM, SECOND)",
            "AFTER_COLOR = COLORNUM",
            "@LAST(VALUE, SECOND)", "#DIM VALUE", "#DIM SECOND",
            "SEEN = VALUE * 100 + SECOND", "LOCAL = 456"
        };
        check(pt.loadScript("regression", buildLines(pt, program)), "load nested rendering regression");
        pt.finalizeParse();
        pt.setEntryPoint("MAIN");
        check(run.runToCompletion() == ExecState::Halt, "nested rendering terminates");
        check(vars.getGlobalInt1D("CELLS", 0) == 36, "12 columns x 3 cells despite callee LOCAL writes");
        check(vars.getGlobalInt1D("SEEN", 0) == 809, "all call arguments evaluated in caller context");
        check(vars.getGlobalInt1D("AFTER_COLOR", 0) == 8, "EOF return restores named parameter");
        check(vars.getGlobalInt1D("AFTER_ROW", 0) == 7, "function-label return restores named parameter");
        check(vars.getGlobalInt1D("PRESERVED", 0) == 99, "explicit return restores caller LOCAL");
        check(vars.getGlobalInt1D("PERSISTED_LOCAL", 0) == 2, "function LOCAL persists between calls");
        check(!vars.hasParameter("ROWNUM") && !vars.hasParameter("VALUE"), "parameters do not leak after return");
    }
    {
        VariableStorage vars;
        ProcessState ps;
        EraParseTable pt(&ps);
        ExecutionEngine ex(&vars, nullptr);
        ExpressionEvaluator ev;
        ex.setParseTable(&pt); ex.setExpressionEvaluator(&ev);
        pt.setVariableStorage(&vars); pt.setExpressionEvaluator(&ev);
        ScriptRunner run(&pt, &ex, &ps, &vars);
        run.setExpressionEvaluator(&ev); run.setStepLimit(1000);
        const QStringList program = {
            "@MAIN", "FOR LOCAL, 0, 12", "ASCENDING_COUNT = ASCENDING_COUNT + 1", "NEXT",
            "FOR LOCAL, 3, -1, -1", "DESCENDING_COUNT = DESCENDING_COUNT + 1", "NEXT",
            "FOR LOCAL, 4, 4", "BAD = BAD + 1", "NEXT",
            "FOR LOCAL, 4, 4, -1", "BAD = BAD + 1", "NEXT",
            "FOR LOCAL, 0, 10, 0", "BAD = BAD + 1", "NEXT", "RETURN"
        };
        pt.loadScript("bounds", buildLines(pt, program)); pt.finalizeParse(); pt.setEntryPoint("MAIN");
        check(run.runToCompletion() == ExecState::Halt, "FOR edge cases terminate");
        check(vars.getGlobalInt1D("ASCENDING_COUNT", 0) == 12, "FOR positive bound is exclusive");
        check(vars.getGlobalInt1D("DESCENDING_COUNT", 0) == 4, "FOR negative bound is exclusive");
        check(vars.getGlobalInt1D("BAD", 0) == 0, "equal bounds and zero step skip loop body");
    }

    {
        VariableStorage vars;
        ProcessState ps;
        EraParseTable pt(&ps);
        ExecutionEngine ex(&vars, nullptr);
        ExpressionEvaluator ev;
        ex.setParseTable(&pt); ex.setExpressionEvaluator(&ev);
        pt.setVariableStorage(&vars); pt.setExpressionEvaluator(&ev);
        ScriptRunner run(&pt, &ex, &ps, &vars);
        run.setExpressionEvaluator(&ev); run.setStepLimit(1000);
        const QStringList program = {
            "@MAIN", "CALL OUTER(6, 3)", "CALL OUTER(8, 4)", "RETURN",
            "@OUTER(POS:0, POS:1)", "#DIM POS, 2", "#DIM TEMP, 2",
            "TEMP:0 = POS:0 + 1", "TEMP:1 = POS:1 + 2",
            "CALL INNER(TEMP:0, TEMP:1)",
            "SEEN_X = POS:0", "SEEN_Y = POS:1", "SEEN_TEMP = TEMP:0", "RETURN",
            "@INNER(POS:0, POS:1)", "#DIM POS, 2", "#DIM TEMP, 2",
            "INNER_X = POS:0", "INNER_Y = POS:1", "TEMP:0 = 99", "RETURN"
        };
        pt.loadScript("private_arrays", buildLines(pt, program)); pt.finalizeParse(); pt.setEntryPoint("MAIN");
        check(run.runToCompletion() == ExecState::Halt, "private array calls terminate");
        check(vars.getGlobalInt1D("SEEN_X", 0) == 8 && vars.getGlobalInt1D("SEEN_Y", 0) == 4,
              "nested collision helper preserves caller coordinate array");
        check(vars.getGlobalInt1D("SEEN_TEMP", 0) == 9, "same-name private scratch arrays isolated");
        check(vars.getGlobalInt1D("INNER_X", 0) == 9 && vars.getGlobalInt1D("INNER_Y", 0) == 6,
              "repeated CALL evaluates original argument AST on each invocation");
    }

    // NEXT must advance the actual variable after assignments in the body,
    // including nonzero LOCAL/ARG and global array slots.
    for (const QString& counter : QStringList{"LOCAL:0", "LOCAL:2", "ARG:1", "LOOP_COUNTER:2"}) {
        VariableStorage vars;
        ProcessState ps;
        EraParseTable pt(&ps);
        ExecutionEngine ex(&vars, nullptr);
        ExpressionEvaluator ev;
        ex.setParseTable(&pt); ex.setExpressionEvaluator(&ev);
        pt.setVariableStorage(&vars); pt.setExpressionEvaluator(&ev);
        ScriptRunner run(&pt, &ex, &ps, &vars);
        run.setExpressionEvaluator(&ev); run.setStepLimit(1000);
        const QStringList program = {
            "@MAIN", QString("FOR %1, 3, 0, -1").arg(counter),
            "DESCENDING_COUNT += 1", "SIF DESCENDING_COUNT == 1",
            QString("%1 += 1").arg(counter), "NEXT",
            QString("DESCENDING_FINAL = %1").arg(counter),
            QString("FOR %1, 0, 3, 1").arg(counter),
            "ASCENDING_COUNT += 1", "SIF ASCENDING_COUNT == 1",
            QString("%1 -= 1").arg(counter), "NEXT",
            QString("ASCENDING_FINAL = %1").arg(counter), "RETURN"
        };
        check(pt.loadScript("modified_counter", buildLines(pt, program)), "load counter assignment regression");
        pt.finalizeParse(); pt.setEntryPoint("MAIN");
        check(run.runToCompletion() == ExecState::Halt, counter + " modified loops terminate");
        check(vars.getGlobalInt1D("DESCENDING_COUNT", 0) == 4,
              counter + " descending FOR rechecks counter incremented by first body");
        check(vars.getGlobalInt1D("ASCENDING_COUNT", 0) == 4,
              counter + " ascending FOR rechecks counter decremented by first body");
        check(vars.getGlobalInt1D("DESCENDING_FINAL", 0) == 0
                  && vars.getGlobalInt1D("ASCENDING_FINAL", 0) == 3,
              counter + " modified loops retain exclusive bounds");
    }

    // eraTetris CHECK_STAGE_LINE / DELETE_STAGE_LINE pattern, with each
    // STAGE entry holding the occupied-cell count of one row. Adjacent full
    // rows must both be cleared by revisiting the row shifted down by CALL.
    {
        VariableStorage vars;
        ProcessState ps;
        EraParseTable pt(&ps);
        ExecutionEngine ex(&vars, nullptr);
        ExpressionEvaluator ev;
        ex.setParseTable(&pt); ex.setExpressionEvaluator(&ev);
        pt.setVariableStorage(&vars); pt.setExpressionEvaluator(&ev);
        ScriptRunner run(&pt, &ex, &ps, &vars);
        run.setExpressionEvaluator(&ev); run.setStepLimit(1000);
        const QStringList program = {
            "@MAIN", "STAGE:0 = 4", "STAGE:1 = 10", "STAGE:2 = 10",
            "CALL CHECK_STAGE_LINE", "RETURN",
            "@CHECK_STAGE_LINE", "FOR LOCAL:0, 2, -1, -1",
            "VISITS += 1", "CALL CHECK_STAGE_LINE_MINO_COUNT(LOCAL:0)",
            "IF RESULT:0 == 10", "CALL DELETE_STAGE_LINE(LOCAL:0)",
            "CLEARED += 1", "LOCAL:0 += 1", "ENDIF", "NEXT", "RETURN",
            "@CHECK_STAGE_LINE_MINO_COUNT(ROW)", "#DIM ROW", "RETURN STAGE:ROW",
            "@DELETE_STAGE_LINE(ROW)", "#DIM ROW", "FOR LOCAL:0, ROW, 0, -1",
            "STAGE:(LOCAL:0) = STAGE:(LOCAL:0 - 1)", "NEXT", "STAGE:0 = 0", "RETURN"
        };
        check(pt.loadScript("clear_lines", buildLines(pt, program)), "load shifted-row regression");
        pt.finalizeParse(); pt.setEntryPoint("MAIN");
        check(run.runToCompletion() == ExecState::Halt, "shifted-row clearing terminates");
        check(vars.getGlobalInt1D("CLEARED", 0) == 2, "both adjacent full rows are cleared");
        check(vars.getGlobalInt1D("VISITS", 0) == 5, "each cleared row is rechecked after shifting");
        check(vars.getGlobalInt1D("STAGE", 0) == 0 && vars.getGlobalInt1D("STAGE", 1) == 0
                  && vars.getGlobalInt1D("STAGE", 2) == 4,
              "partial row shifts to bottom and vacated rows are empty");
    }

    // eraTW new-game menu: indexed string parameters are collected into a
    // private array and passed by REF to a loop bounded by VARSIZE.
    {
        VariableStorage vars;
        ProcessState ps;
        EraParseTable pt(&ps);
        ExecutionEngine ex(&vars, nullptr);
        ExpressionEvaluator ev;
        ex.setParseTable(&pt); ex.setExpressionEvaluator(&ev);
        pt.setVariableStorage(&vars); pt.setExpressionEvaluator(&ev);
        ScriptRunner run(&pt, &ex, &ps, &vars);
        run.setExpressionEvaluator(&ev); run.setStepLimit(1000);
        const QStringList program = {
            "@MAIN", "CALL COLLECT(\"START\", \"ROLE\")", "RETURN",
            "@COLLECT(choices:0=\"\", choices:1=\"\")", "#DIMS choices, 10",
            "OUTER_SIZE = VARSIZE(\"choices\")", "CALL INSPECT(choices)", "RETURN",
            "@INSPECT(refChoices, cancel=-1)", "#DIMS REF refChoices, 0", "#DIMS html", "#DIM DYNAMIC cancel",
            "REF_SIZE = VARSIZE(\"refChoices\")", "FOR LOCAL, 0, VARSIZE(\"refChoices\")",
            "IF refChoices:LOCAL != \"\"", "html += refChoices:LOCAL", "NONEMPTY += 1", "ENDIF",
            "NEXT", "HTML_LEN = STRLENS(html)", "RETURN"
        };
        check(pt.loadScript("ref_menu", buildLines(pt, program)), "load REF menu regression");
        pt.finalizeParse(); pt.setEntryPoint("MAIN");
        check(run.runToCompletion() == ExecState::Halt, "REF menu loop terminates");
        qDebug() << "  measured OUTER_SIZE=" << vars.getGlobalInt1D("OUTER_SIZE", 0)
                 << "REF_SIZE=" << vars.getGlobalInt1D("REF_SIZE", 0)
                 << "NONEMPTY=" << vars.getGlobalInt1D("NONEMPTY", 0);
        check(vars.getGlobalInt1D("REF_SIZE", 0) == 10, "VARSIZE follows REF private array");
        qDebug() << "  measured HTML_LEN=" << vars.getGlobalInt1D("HTML_LEN", 0);
        check(vars.getGlobalInt1D("NONEMPTY", 0) == 2, "REF reads indexed string parameters");
        const UserFunctionDecl* inspect = pt.userFunction("INSPECT");
        check(inspect && inspect->params.value(0).isReference, "#DIMS REF annotates matching parameter");
        check(inspect && inspect->params.value(1).hasDefault
                  && inspect->params.value(1).defaultInt == -1,
              "function default parameter is retained in AST");
        const auto ast = pt.expressionAst(QStringLiteral("\"ROLE\" + \"!\""));
        check(ast && ast->valueType() == OperandType::Str && ast->isStaticallyTyped(),
              "string concatenation remains statically string-typed");
    }

    {
        ProcessState ps;
        EraParseTable pt(&ps);
        VariableStorage vs;
        ExpressionEvaluator ev;
        pt.setVariableStorage(&vs);
        pt.setExpressionEvaluator(&ev);
        ExecutionEngine ex(&vs, nullptr);
        ex.setParseTable(&pt);
        ex.setExpressionEvaluator(&ev);
        ScriptRunner run(&pt, &ex, &ps, &vs);
        run.setExpressionEvaluator(&ev);
        const QStringList source = {
            "@MAIN", "CALL TWO", "CALL THREE", "RETURN",
            "@TWO", "#DIM GRID, 2, 3", "GRID:1:0 = 17", "GRID:1:2 = 29",
            "OUT2D = GRID:1:2", "OUT2DINC = ++GRID:1:2", "OUT2DZERO = GRID:1:0", "RETURN",
            "@THREE", "#DIM GRID, 2, 3, 4", "GRID:1:2:0 = 41", "GRID:1:2:3 = 53",
            "OUT3D = GRID:1:2:3", "OUT3DINC = ++GRID:1:2:3", "OUT3DZERO = GRID:1:2:0", "RETURN"
        };
        pt.loadScript("scoped", buildLines(pt, source));
        pt.finalizeParse();
        pt.setEntryPoint("MAIN");
        check(run.runToCompletion() == ExecState::Halt, "scoped arrays complete");
        check(vs.getGlobalInt1D("OUT2D", 0) == 29 && vs.getGlobalInt1D("OUT2DINC", 0) == 30
                  && vs.getGlobalInt1D("OUT2DZERO", 0) == 17, "private 2D read and increment preserve second index");
        check(vs.getGlobalInt1D("OUT3D", 0) == 53 && vs.getGlobalInt1D("OUT3DINC", 0) == 54
                  && vs.getGlobalInt1D("OUT3DZERO", 0) == 41, "same name in another scope uses 3D indices");
    }

    qDebug() << "\n================================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] user function tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
