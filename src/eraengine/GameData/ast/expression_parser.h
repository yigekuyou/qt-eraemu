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
#ifndef EXPRESSION_PARSER_H
#define EXPRESSION_PARSER_H

#include <QList>
#include <QSharedPointer>
#include <functional>
#include "expression_lexer.h"
#include "expression_ast.h"
#include "operand_type.h"

// 递归下降（优先级爬升 / precedence climbing）表达式解析器。
//
// 运算符优先级与结合性对齐 C# OperatorCode 的 __PRIORITY_MASK__：
//   '*' '/' '%'            (0x90)
//   '+' '-'                (0x80)
//   '>>' '<<'              (0x70)
//   '<' '<=' '>' '>='      (0x65)
//   '==' '!='              (0x60)
//   '&' '|' '^'            (0x50)   位运算
//   '&&' '||' '^^' '!&' '!|' (0x40) 逻辑运算
//   '?' '#' 三元           (0x05/0x10)
// 一元运算符优先级高于所有二元运算符。
class ExpressionParser {
public:
    ExpressionParser();

    QSharedPointer<ExpressionNode> parse(const QList<ExpressionToken>& tokens);

    // 解析诊断输出开关（默认关闭；装载期批量解析时避免噪音）。
    void setVerbose(bool verbose) { m_verbose = verbose; }

    // 函数返回类型提供者（内置表 + 用户自定义函数 #FUNCTION(S)），用于强类型推断。
    using FunctionTypeProvider = std::function<OperandType(const QString&)>;
    void setFunctionTypeProvider(FunctionTypeProvider provider) { m_functionTypeProvider = std::move(provider); }

    // 常量名判定：变量下标里出现「已知 CSV 常量名」的标识符时，按字符串处理
    // （对齐 C# ExpressionParser.reduceIdentifier：ConstantData.isDefined → SingleTerm(string)）
    using ConstantNameProvider = std::function<bool(const QString& variable, const QString& name)>;
    void setConstantNameProvider(ConstantNameProvider provider) {
        m_constantNameProvider = std::move(provider);
    }

    // 格式化串提供者（@"..." / \@...#...\@）：交由 StrFormParser 归约。
    // yenAt=true 表示 token 来自 \@...\@。
    using FormProvider = std::function<QSharedPointer<ExpressionNode>(const QString&, bool yenAt)>;
    void setFormProvider(FormProvider provider) { m_formProvider = std::move(provider); }

    // 二元运算符优先级（0 表示非二元运算符）；供测试与调试。
    static int binaryPrecedence(TokenType type);

private:
    QSharedPointer<ExpressionNode> parseExpression();
    QSharedPointer<ExpressionNode> parseBinary(int minPrecedence);
    QSharedPointer<ExpressionNode> parseUnary();
    QSharedPointer<ExpressionNode> parsePrimary();
    QSharedPointer<ExpressionNode> parseIndexTerm(const QString& variableName);
    QSharedPointer<ExpressionNode> parseFormTerm();
    QSharedPointer<ExpressionNode> parseVariable();
    QSharedPointer<ExpressionNode> parseFunctionCall();

    bool match(TokenType type);
    bool check(TokenType type);
    ExpressionToken consume(TokenType type, const QString& message);
    ExpressionToken peek();
    bool isAtEnd();
    ExpressionToken advance();

    QList<ExpressionToken> m_tokens;
    int m_current = 0;
    int m_depth = 0;
    bool m_verbose = false;
    FunctionTypeProvider m_functionTypeProvider;
    FormProvider m_formProvider;
    ConstantNameProvider m_constantNameProvider;
};

#endif // EXPRESSION_PARSER_H
