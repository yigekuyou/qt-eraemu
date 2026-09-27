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

    qDebug() << "\n5) AST 缓存去重共享";
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

    qDebug() << "\n7) 表达式优先级/运算符对齐 C#";
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

    qDebug() << "\n===============================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] parse-table complete-AST tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
