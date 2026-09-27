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
        QStringLiteral("OUT1 = ARG"),                  // 2  ARG 应指向 LOCAL[0]
        QStringLiteral("OUT2 = STRLENS(ARGS)"),        // 3  ARGS 应指向 LOCALS[0]
        QStringLiteral("LOCAL = INT_FN(4)"),           // 4  式中调用
        QStringLiteral("RETURN"),                      // 5
        QStringLiteral("@STR_FN, ARG, ARGS"),          // 6  逗号式：arg0->ARG, arg1->ARGS
        QStringLiteral("#DIM A"),                      // 7
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
        check(strFn->labelLine == 6 && strFn->endLine == 8,
              QString("STR_FN 体区间 [%1, %2] == [6, 8]").arg(strFn->labelLine).arg(strFn->endLine));
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
    check(storage.getGlobalInt1D("LOCAL", 0) == 40, "式中调用 INT_FN(4) -> 40");

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
            QStringLiteral("#DIM A"),                // 20
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

    qDebug() << "\n================================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] user function tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
