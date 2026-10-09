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

#include <QVariant>
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
    void setIgnoreTripleSymbols(bool ignore) { m_ignoreTripleSymbols = ignore; }

    QSharedPointer<ExpressionNode> parse(const QList<ExpressionToken>& tokens);

    // 解析诊断输出开关（默认关闭；装载期批量解析时避免噪音）。
    void setVerbose(bool verbose) { m_verbose = verbose; }

    // 静默模式：仍返回同样的 AST，但不打印「表达式语法错误」。
    // 用于**赋值右值的临时解析**——装载期变量类型尚未定稿，字符串赋值的右值
    // （含 %…%/{…}/? 的文本）必然解析失败，随后 applyStringAssignments() 会用
    // StrFormParser 重新解释；此阶段的报错是纯噪音（eraMegaten 实测 705 条）。
    void setQuiet(bool quiet) { m_quiet = quiet; }

    // 函数返回类型提供者（内置表 + 用户自定义函数 #FUNCTION(S)），用于强类型推断。
    using FunctionTypeProvider = std::function<OperandType(const QString&)>;
    void setFunctionTypeProvider(FunctionTypeProvider provider) { m_functionTypeProvider = std::move(provider); }

    // 常量名判定：变量下标里出现「已知 CSV 常量名」的标识符时，按字符串处理
    // （对齐 C# ExpressionParser.reduceIdentifier：ConstantData.isDefined → SingleTerm(string)）
    // #DIM CONST 常量折叠：名字 -> 常量值（Qt6 QVariant，Int 或 Str）
    using ConstantValueProvider = std::function<QVariant(const QString& name)>;
    void setConstantValueProvider(ConstantValueProvider provider) {
        m_constantValueProvider = std::move(provider);
    }

    void setVariableTypeProvider(FunctionTypeProvider p) { m_variableTypeProvider = std::move(p); }

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

    // 二元结合性：(left, right) 绑定力。默认为运算符表（operator_table.h）；
    // 可用 setBindingPowerProvider 注入 —— 与其它 Provider 同风格（函数类型/格式化串/
    // 常量表），既便于测试右结合，也留出 EM/EE 私family 扩展运算符的接缝。
    using BindingPowerProvider = std::function<BindingPower(TokenType)>;
    static BindingPower binaryBindingPower(TokenType type);
    void setBindingPowerProvider(BindingPowerProvider provider) {
        m_bindingPowerProvider = std::move(provider);
    }
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
    bool check(TokenType type);    ExpressionToken consume(TokenType type, const QString& message);
    ExpressionToken peek();
    bool isAtEnd();
    ExpressionToken advance();

    // 用已消费的 token 区间 [startToken, m_current) 给节点打源码 span（表达式内偏移）。
    void stampSpan(const QSharedPointer<ExpressionNode>& node, int startToken);

    QList<ExpressionToken> m_tokens;
    int m_current = 0;
    int m_depth = 0;
    bool m_ignoreTripleSymbols = false;
    bool m_verbose = false;
    bool m_quiet = false;
    FunctionTypeProvider m_functionTypeProvider;
    FunctionTypeProvider m_variableTypeProvider;
    FormProvider m_formProvider;
    ConstantNameProvider m_constantNameProvider;
    ConstantValueProvider m_constantValueProvider;
    BindingPowerProvider m_bindingPowerProvider;   // 空 = 用 operator_table 默认
};

#endif // EXPRESSION_PARSER_H
