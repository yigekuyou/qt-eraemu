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
// test_parse_table_ast.cpp
//
// 验证「完整 AST + 拍平」改造：
//   1. AstBuilder 把每个物理行编译为 ast::LogicalLine（实参/条件为表达式 AST）
//   2. EraParseTable 只读区保存扁平 AST 行；标记区给出 O(1) 跳转
//   3. AST 缓存去重共享（同一表达式只解析一次）
//   4. 表达式解析优先级与运算符对齐 C# OperatorCode / OperatorManager
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QString>
#include <QStringList>

#include "ast/logical_line.h"
#include "ast/ast_builder.h"
#include "process_state.h"
#include "era_parse_table.h"
#include "variable_storage.h"
#include "ast/expression_lexer.h"
#include "ast/expression_parser.h"
#include "ast/expression_evaluator.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) {
        qDebug().noquote() << "  [ok ]" << what;
    } else {
        qDebug().noquote() << "  [FAIL]" << what;
        ++g_failures;
    }
}

static QString parseToString(const QString& expr) {
    ExpressionLexer lexer;
    const QList<ExpressionToken> tokens = lexer.tokenize(expr, 1);
    ExpressionParser parser;
    const QSharedPointer<ExpressionNode> ast = parser.parse(tokens);
    return ast ? ast->toString() : QStringLiteral("<null>");
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "EraParseTable complete-AST test";
    qDebug() << "===============================";

    ProcessState state;
    EraParseTable table(&state);
    VariableStorage storage;
    table.setVariableStorage(&storage);

    // ---- 用 AstBuilder 从源码构建完整 AST（装载与表达式解析同一流水线）----
    const QStringList source = {
        "@MAIN",            // 0
        "A = 1 + 2 * 3",    // 1
        "IF 1 == 1",        // 2
        "PRINT seven",      // 3
        "ELSE",             // 4
        "PRINT \"other\"",  // 5
        "ENDIF",            // 6
        "REPEAT 3",         // 7
        "PRINT \"x\"",      // 8
        "LOOP",             // 9
        "WHILE 1 == 0",     // 10
        "PRINT \"w\"",      // 11
        "WEND",             // 12
        "FOR LOCAL, 0, 2",  // 13
        "PRINT \"f\"",      // 14
        "NEXT",             // 15
        "SIF 1 == 0",       // 16
        "PRINT \"sif\""     // 17
    };

    const AstResolver resolve = [&table](const QString& e) {
        return table.expressionAst(e);
    };

    QList<LogicalLine> script;
    for (int i = 0; i < source.size(); ++i) {
        script.append(AstBuilder::build(source.at(i), ScriptPosition("test.ERB", i, 0), resolve));
    }

    check(table.loadScript("test", script), "loadScript(test)");

    const ScriptData* data = table.script("test");
    check(data != nullptr, "script(test) available");
    if (!data) { return 1; }

    qDebug() << "\n1) 完整 AST：lines 即 LogicalLine 数组";
    check(data->lines.size() == source.size(), "lines.size() == source lines");
    check(data->lines[0].kind == LineKind::FunctionLabel, "line0 is FunctionLabel");
    check(data->lines[0].labelName == "MAIN", "line0 labelName == MAIN");
    check(data->labelPositions.value("MAIN", -1) == 0, "labelPositions[MAIN] == 0");
    check(data->lines[3].kind == LineKind::Instruction, "line3 is Instruction");
    check(data->lines[3].functionName == "PRINT", "line3 functionName == PRINT");
    check(data->lines[3].arguments.size() == 1 && data->lines[3].arguments[0].isString,
          "line3 arg is string literal");
    check(data->lines[3].arguments[0].raw == "seven", "PRINT 字面文本 == seven（无引号）");

    qDebug() << "\n2) AST：赋值语句实参预编译";
    const LogicalLine& assign = data->lines[1];
    check(assign.isInstruction() && assign.functionName == "=", "line1 functionName == '='");
    check(assign.assignOperator == "=", "line1 assignOperator == '='");
    check(assign.arguments.size() == 2, "assignment has lhs/rhs");
    check(assign.arguments[0].raw == "A", "lhs raw == A");
    check(!assign.arguments[1].ast.isNull(), "rhs has compiled AST");
    if (assign.arguments[1].ast) {
        check(assign.arguments[1].ast->toString() == "BinaryOp(Literal(1), +, BinaryOp(Literal(2), *, Literal(3)))",
              "rhs AST is 1 + (2 * 3)");
    }

    qDebug() << "\n3) 条件预编译为 AST";
    check(!data->lines[2].condition.isNull(), "IF condition compiled");
    check(!data->lines[10].condition.isNull(), "WHILE condition compiled");
    check(!data->lines[16].condition.isNull(), "SIF condition compiled");

    qDebug() << "\n4) 标记区 / O(1) 跳转";
    check(data->elseLines.value(2, -1) == 4, "elseLines[IF] == ELSE line");
    check(data->ifBranches.value(2, QList<int>()) == QList<int>{4}, "ifBranches[IF] == [ELSE]");
    check(data->jumpTo.value(2, -1) == 7, "jumpTo[IF] == ENDIF+1 (no branch taken)");
    check(data->endifLines.value(2, -1) == 7, "endifLines[IF] == ENDIF+1");
    check(data->jumpTo.value(4, -1) == 7, "jumpTo[ELSE] == ENDIF+1");
    check(data->loopEndLines.value(7, -1) == 9, "loopEndLines[REPEAT] == LOOP");
    check(data->jumpToEnd.value(7, -1) == 9, "jumpToEnd[REPEAT] == LOOP");
    check(data->jumpTo.value(9, -1) == 7, "jumpTo[LOOP] back-edge == REPEAT");
    check(data->loopEndLines.value(10, -1) == 12, "loopEndLines[WHILE] == WEND");
    check(data->jumpTo.value(12, -1) == 10, "jumpTo[WEND] back-edge == WHILE");
    check(data->loopEndLines.value(13, -1) == 15, "loopEndLines[FOR] == NEXT");
    check(data->jumpTo.value(15, -1) == 13, "jumpTo[NEXT] back-edge == FOR");
    check(data->jumpTo.value(16, -1) == 18, "jumpTo[SIF] == skip next line");
    // 扁平控制流字段（等价 C# NextLine / JumpTo）
    check(data->lines[2].jumpTo == 7, "line[IF].jumpTo == 7 (ENDIF+1)");
    check(data->lines[1].nextLine == 2, "line1.nextLine == 2");

    qDebug() << "\n5) 文档语法的按钮、输入与 CASE AST";
    {
        const QStringList documented = {
            "PRINTBUTTON \"开始\", 42",
            "INPUT",
            "INPUTS",
            "CASE IS >= 10",
            "CASE 1 TO 3",
            "CASE 4, 5"
        };
        QList<LogicalLine> documentedLines;
        for (int i = 0; i < documented.size(); ++i) {
            documentedLines.append(AstBuilder::build(documented.at(i),
                                                      ScriptPosition("docs.ERB", i), resolve));
        }
        check(documentedLines[0].argument.kind == ArgKind::Button,
              "PRINTBUTTON is typed as Button");
        check(documentedLines[0].argument.typeOk
                  && documentedLines[0].argument.params.size() == 2,
              "PRINTBUTTON keeps text/value operands");
        check(documentedLines[1].functionName == "INPUT"
                  && documentedLines[1].argument.kind == ArgKind::Expressions,
              "INPUT is preserved as an input instruction");
        check(documentedLines[2].functionName == "INPUTS"
                  && documentedLines[2].argument.kind == ArgKind::Expressions,
              "INPUTS is preserved as an input instruction");
        check(documentedLines[3].argument.kind == ArgKind::Case
                  && documentedLines[3].argument.cases.size() == 1,
              "CASE IS keeps one structured case operand");
        check(documentedLines[4].argument.kind == ArgKind::Case
                  && documentedLines[4].argument.cases.size() == 1,
              "CASE TO keeps one structured case operand");
        check(documentedLines[5].argument.kind == ArgKind::Case
                  && documentedLines[5].argument.cases.size() == 2,
              "CASE comma list keeps both case operands");
    }

    qDebug() << "\n6) AST 缓存去重共享";
    const QSharedPointer<ExpressionNode> a1 = table.expressionAst("1 + 2 * 3");
    const QSharedPointer<ExpressionNode> a2 = table.expressionAst(" 1 + 2 * 3 ");
    check(!a1.isNull() && a1 == a2, "expressionAst cache returns shared AST");
    check(a1 == assign.arguments[1].ast, "compiled arg shares cached AST");

    qDebug() << "\n6) 条件求值走缓存 AST";
    bool condTrue = false, condFalse = true;
    check(table.evaluateCondition("test", 2, condTrue), "evaluateCondition(IF 1==1) ok");
    check(condTrue, "IF 1 == 1 -> true");
    check(table.evaluateCondition("test", 16, condFalse), "evaluateCondition(SIF 1==0) ok");
    check(!condFalse, "SIF 1 == 0 -> false");

    qDebug() << "\n7) 字符串赋值：原文、插值、作用域与真正未定义函数";
    {
        ProcessState stringState;
        EraParseTable stringTable(&stringState);
        const AstResolver stringResolve = [&stringTable](const QString& e) {
            return stringTable.expressionAst(e);
        };
        const QStringList stringSource = {
            "@TEXT_CASE(ARG)",
            "#DIMS LOCAL_TEXT",
            "LOCAL_TEXT = 白狼天狗服(色固定)",
            "LOCAL_TEXT = 妖怪之山 {ARG}",
            "LOCAL_TEXT '= 既定の文字列",
            "LOCAL_TEXT = %TOSTR(ARG)%",
            "LOCAL_TEXT '= missing_function(ARG)"
        };
        QList<LogicalLine> stringLines;
        for (int i = 0; i < stringSource.size(); ++i) {
            stringLines.append(AstBuilder::build(stringSource.at(i),
                                                  ScriptPosition("strings.ERB", i),
                                                  stringResolve));
        }
        check(stringTable.loadScript("strings", stringLines), "load string assignment script");
        stringTable.finalizeParse();
        const ScriptData* strings = stringTable.script("strings");
        check(strings != nullptr, "string assignment script available");
        if (strings) {
            check(strings->lines[2].arguments[1].ast
                      && strings->lines[2].arguments[1].ast->kind() == NodeKind::StrForm,
                  "plain parenthesized text is a StrForm, not a function call");
            check(strings->lines[3].arguments[1].ast
                      && strings->lines[3].arguments[1].ast->kind() == NodeKind::StrForm,
                  "brace interpolation keeps the surrounding text as StrForm");
            check(strings->lines[4].assignOperator == "'=",
                  "C# string expression assignment preserves '= operator");
            check(strings->lines[4].arguments[1].ast
                      && strings->lines[4].arguments[1].ast->kind() != NodeKind::StrForm,
                  "'= keeps typed expression semantics");
            check(strings->lines[5].arguments[1].ast
                      && strings->lines[5].arguments[1].ast->kind() == NodeKind::StrForm,
                  "percent interpolation is parsed as StrForm");
            bool hasMissing = false;
            for (const QString& warning : stringTable.parseWarnings()) {
                if (warning.contains("MISSING_FUNCTION", Qt::CaseInsensitive)) hasMissing = true;
                check(!warning.contains("白狼天狗服") && !warning.contains("妖怪之山"),
                      "plain string text has no undefined-function warning");
            }
            check(hasMissing, "true undefined function remains diagnosed");
        }
    }

    {
        ProcessState ps;
        VariableStorage vars;
        ExpressionEvaluator evaluator;
        EraParseTable boundary(&ps);
        boundary.setVariableStorage(&vars);
        boundary.setExpressionEvaluator(&evaluator);
        const QStringList source = {"@BOUNDARY", "#DIM GRID, 2, 3", "A = 0 && (1 / 0)",
            "B = 1 ? 7 # (1 / 0)", "C = 1 ? 1 # \"bad\"", "D = 1,,2",
            "E = GRID:1", "F = GRID:2:0"};
        QList<LogicalLine> lines;
        for (int i = 0; i < source.size(); ++i)
            lines.append(AstBuilder::build(source[i], ScriptPosition("boundary.ERB", i),
                [&](const QString& e) { return boundary.expressionAst(e); }));
        boundary.loadScript("boundary", lines);
        boundary.finalizeParse();
        const auto* data = boundary.script("boundary");
        for (int i : {2, 3, 4, 5, 6, 7})
            check(data && !data->lines[i].argument.typeOk, QStringLiteral("load rejects boundary line %1").arg(i));
    }

    qDebug() << "\n8) 表达式优先级/运算符对齐 C#";
    check(parseToString("1 + 2 * 3") == "BinaryOp(Literal(1), +, BinaryOp(Literal(2), *, Literal(3)))",
          "'*' 高于 '+'");
    check(parseToString("(1 + 2) * 3") == "BinaryOp(BinaryOp(Literal(1), +, Literal(2)), *, Literal(3))",
          "括号分组");
    check(parseToString("A == B && C") == "BinaryOp(BinaryOp(Variable(A), ==, Variable(B)), &&, Variable(C))",
          "'==' 高于 '&&'");
    check(parseToString("A && B || C") == "BinaryOp(BinaryOp(Variable(A), &&, Variable(B)), ||, Variable(C))",
          "'&&' 与 '||' 同优先级左结合");
    check(parseToString("A & B | C") == "BinaryOp(BinaryOp(Variable(A), &, Variable(B)), |, Variable(C))",
          "位运算同优先级左结合");
    check(parseToString("~A") == "UnaryOp(~, Variable(A))", "按位取反");
    check(parseToString("A ? 1 # 2") == "If(Variable(A), Literal(1), Literal(2))", "三元 cond ? a # b");
    check(parseToString("1 << 2") == "BinaryOp(Literal(1), <<, Literal(2))", "移位运算符");
    check(parseToString("A = B") == "<null>", "'=' 不是表达式运算符（赋值由指令层处理）");
    check(parseToString("") == "<null>", "空表达式解析为空");

    qDebug() << "\n9) 字符串赋值 '= 的行内注释 + 变量槽实参";
    {
        // `'=` 是字符串赋值运算符，不是引号：其后的 ';' 必须仍然剥成注释，
        // 否则右值整段解析失败（eraTW _List.ERB「赋值右值无法解析或包含空项」）。
        ProcessState st2;
        EraParseTable t2(&st2);
        VariableStorage vs2;
        t2.setVariableStorage(&vs2);
        const AstResolver r2 = [&t2](const QString& e) { return t2.expressionAst(e); };
        const LogicalLine assign =
            AstBuilder::build("X '= SUBSTRINGU(Y, 0, 1); 这里是注释",
                              ScriptPosition("s.ERB", 1, 0), r2);
        check(assign.isInstruction() && assign.assignOperator == "'=",
              "'= 识别为字符串赋值运算符");
        check(assign.arguments.size() == 2 && assign.arguments[1].raw == "SUBSTRINGU(Y, 0, 1)",
              QString("'= 右值剥离行内注释（实得 %1）")
                  .arg(assign.arguments.size() > 1 ? assign.arguments[1].raw : QStringLiteral("<无>")));
    }
    {
        // 2 维数组整数组出现在「变量槽」实参（VARSET / CALL 的 REF 实参）里不该要求下标；
        // MAXARRAY 的 RefInt1D 也接受角色一维数组（TCVAR）。
        ProcessState st3;
        EraParseTable t3(&st3);
        VariableStorage vs3;
        t3.setVariableStorage(&vs3);
        const AstResolver r3 = [&t3](const QString& e) { return t3.expressionAst(e); };
        const QStringList vsrc = {
            "@MAIN",
            "#DIM 集合, 100, 3",
            "VARSET 集合",
            "RESULT = MAXARRAY(TCVAR, 390, 394)"
        };
        QList<LogicalLine> vlines;
        for (int i = 0; i < vsrc.size(); ++i)
            vlines.append(AstBuilder::build(vsrc.at(i), ScriptPosition("v.ERB", i + 1, 0), r3));
        t3.loadScript("v", vlines);
        t3.finalizeParse();
        for (const QString& w : t3.parseWarnings()) qDebug().noquote() << "      warn:" << w;
        check(t3.parseWarningCount() == 0,
              QString("变量槽实参不误报（实得 %1 条告警）").arg(t3.parseWarningCount()));
    }

    qDebug() << "\n===============================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] parse-table complete-AST tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
