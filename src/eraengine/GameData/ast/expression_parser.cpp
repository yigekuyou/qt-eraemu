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
#include <QVariant>
#include "expression_parser.h"
#include "operator_table.h"
#include "function_types.h"
#include "system_variables.h"
#include <QDebug>

namespace {
constexpr int kMaxDepth = 2048;   // 防止畸形输入导致栈溢出
}

ExpressionParser::ExpressionParser() = default;

int ExpressionParser::binaryPrecedence(TokenType type) {
    // 单一来源：运算符表（operator_table.h）
    return operatorPrecedence(type);
}

QSharedPointer<ExpressionNode> ExpressionParser::parse(const QList<ExpressionToken>& tokens) {
    m_tokens = tokens;
    m_current = 0;
    m_depth = 0;

    if (m_tokens.isEmpty()) {
        return nullptr;
    }

    QSharedPointer<ExpressionNode> ast = parseExpression();
    // 容忍尾随的 END_OF_FILE；其余多余 token 视为解析失败。
    if (ast && !isAtEnd() && !check(TokenType::END_OF_FILE)) {
        if (m_verbose) qDebug() << "Parse error: unexpected trailing token" << peek().value();
        return nullptr;
    }
    return ast;
}

QSharedPointer<ExpressionNode> ExpressionParser::parseExpression() {
    if (++m_depth > kMaxDepth) {
        --m_depth;
        return nullptr;
    }

    QSharedPointer<ExpressionNode> node = parseBinary(1);

    // 三元运算符：cond ? a # b （对应 OperatorCode Ternary_a / Ternary_b）
    if (node && check(TokenType::QUESTION)) {
        consume(TokenType::QUESTION, "Expected ?");
        QSharedPointer<ExpressionNode> thenExpr = parseExpression();
        consume(TokenType::TERNARY_SEP, "Expected #");
        QSharedPointer<ExpressionNode> elseExpr = parseExpression();
        if (thenExpr && elseExpr) {
            node = QSharedPointer<IfNode>::create(node, thenExpr, elseExpr);
        } else {
            node = nullptr;
        }
    }

    --m_depth;
    return node;
}

QSharedPointer<ExpressionNode> ExpressionParser::parseBinary(int minPrecedence) {
    QSharedPointer<ExpressionNode> left = parseUnary();
    if (!left) {
        return nullptr;
    }

    while (true) {
        const ExpressionToken op = peek();
        const int prec = binaryPrecedence(op.type());
        if (prec == 0 || prec < minPrecedence) {
            break;
        }
        advance();  // consume operator

        QSharedPointer<ExpressionNode> right = parseBinary(prec + 1);  // 左结合
        if (!right) {
            return nullptr;
        }
        left = QSharedPointer<BinaryOpNode>::create(left, op, right);
    }

    return left;
}

QSharedPointer<ExpressionNode> ExpressionParser::parseUnary() {
    // 前置一元运算符
    if (check(TokenType::MINUS) || check(TokenType::PLUS) || check(TokenType::NOT)
        || check(TokenType::BIT_NOT) || check(TokenType::INCREMENT)
        || check(TokenType::DECREMENT)) {
        const ExpressionToken op = advance();
        QSharedPointer<ExpressionNode> operand = parseUnary();
        if (!operand) {
            return nullptr;
        }
        return QSharedPointer<UnaryOpNode>::create(op, operand);
    }

    QSharedPointer<ExpressionNode> node = parsePrimary();
    if (!node) {
        return nullptr;
    }

    // 后置一元运算符（++ / --）：返回值是**自增前**的值，但变量本身要自增
    if (check(TokenType::INCREMENT) || check(TokenType::DECREMENT)) {
        const ExpressionToken op = advance();
        node = QSharedPointer<UnaryOpNode>::create(op, node, /*postfix=*/true);
    }
    return node;
}

QSharedPointer<ExpressionNode> ExpressionParser::parsePrimary() {
    if (check(TokenType::STRFORM_AT) || check(TokenType::YEN_AT)) {
        return parseFormTerm();
    }
    if (check(TokenType::NUMBER)) {
        return QSharedPointer<LiteralNode>::create(consume(TokenType::NUMBER, "Expected number"));
    }
    if (check(TokenType::STRING)) {
        return QSharedPointer<LiteralNode>::create(consume(TokenType::STRING, "Expected string"));
    }
    if (check(TokenType::IDENTIFIER)) {
        if (m_current + 1 < m_tokens.size() &&
            m_tokens[m_current + 1].type() == TokenType::LEFT_PAREN) {
            return parseFunctionCall();
        }
        // `#DIM CONST NAME = value`：在解析期折叠为字面量（对齐 C# 的常数）
        if (m_constantValueProvider) {
            const bool hasIndex = (m_current + 1 < m_tokens.size()
                                   && m_tokens[m_current + 1].type() == TokenType::COLON);
            const QVariant cv = m_constantValueProvider(m_tokens[m_current].value());
            if (cv.isValid() && !hasIndex) {
                advance();
                if (cv.typeId() == QMetaType::QString) {
                    return QSharedPointer<LiteralNode>::create(cv.toString());
                }
                return QSharedPointer<LiteralNode>::create(cv.toLongLong());
            }
        }
        return parseVariable();
    }
    if (check(TokenType::LEFT_PAREN)) {
        advance();
        QSharedPointer<ExpressionNode> expr = parseExpression();
        if (!check(TokenType::RIGHT_PAREN)) {
            if (m_verbose) qDebug() << "Parse error: Expected )";
            return nullptr;
        }
        advance();
        return expr;
    }
    return nullptr;
}

QSharedPointer<ExpressionNode> ExpressionParser::parseVariable() {
    const ExpressionToken token = consume(TokenType::IDENTIFIER, "Expected variable name");

    // 强类型：$ 前缀 = 字符串变量；否则查系统变量表（RESULTS/LOCALS/GLOBALS/…）
    const QString name = token.value();
    OperandType varType = OperandType::Int;
    if (name.startsWith(QLatin1Char('$'))) {
        varType = OperandType::Str;
    } else {
        const OperandType sys = sysvar::systemVariableType(name.toStdString());
        if (isKnown(sys)) varType = sys;
        // 未登记的标识符按 Int 处理（对齐 C#：未知标识符在归约期报错）
    }

    auto varNode = QSharedPointer<VariableNode>::create(name, varType);

    // 数组下标：VAR:index[:index2...]
    //
    // 对齐 C# VariableParser.ReduceVariable + ExpressionParser.ReduceVariableArgument：
    // 下标是**单个项**（字面量 / 标识符 / 函数调用 / 变量 / 括号表达式），
    // 而不是任意表达式。否则 `LOCALS:LOCAL == "0"` 会被解析成
    // `LOCALS:(LOCAL == "0")` 从而丢掉比较运算（类型也随之错判）。
    while (check(TokenType::COLON)) {
        advance();
        QSharedPointer<ExpressionNode> index = parseIndexTerm(name);
        if (!index) {
            break;   // 防止畸形输入导致死循环
        }
        varNode->addIndex(index);
    }

    return varNode;
}

// 变量下标的单个项（C#：reduceTerm(..., varArg:true) 只取一个项）
QSharedPointer<ExpressionNode> ExpressionParser::parseIndexTerm(const QString& variableName) {
    if (check(TokenType::NUMBER)) {
        return QSharedPointer<LiteralNode>::create(consume(TokenType::NUMBER, "Expected number"));
    }
    if (check(TokenType::STRING)) {
        return QSharedPointer<LiteralNode>::create(consume(TokenType::STRING, "Expected string"));
    }
    // 字符串下标：VAR:MASTER:@"技能{LCOUNT}"（C# LiteralStringWT/FormattedStringWT
    // 可作为变量的「字符串引数」，运行时按 CSV/常数名解析）
    if (check(TokenType::STRFORM_AT) || check(TokenType::YEN_AT)) {
        return parseFormTerm();
    }
    if (check(TokenType::IDENTIFIER)) {
        if (m_current + 1 < m_tokens.size() &&
            m_tokens[m_current + 1].type() == TokenType::LEFT_PAREN) {
            return parseFunctionCall();
        }
        // 已知 CSV 常量名（如 CFLAG:現在的場所）→ 字符串字面量
        // （对齐 C#：ConstantData.isDefined(varCode, idStr) → SingleTerm(string)）
        if (m_constantNameProvider) {
            const QString ident = m_tokens.at(m_current).value();
            if (m_constantNameProvider(variableName, ident)) {
                advance();
                return QSharedPointer<LiteralNode>::create(ident);
            }
        }
        // 变量下标项：只取标识符本身。后续 `:下标` 链属于**外层**变量
        // （C# ReduceVariable 从左到右归约：TALENT:選択中角色ID:LOCAL =
        //   TALENT[選択中角色ID][LOCAL]）。若这里走 parseVariable()，
        // 内层变量会把 `:LOCAL` 吞成自己的下标，外层只剩一个下标，
        // 角色变量读取全部塌到 [0][0]。
        const ExpressionToken tok = consume(TokenType::IDENTIFIER, "Expected variable name");
        const QString identName = tok.value();
        OperandType varType = OperandType::Int;
        if (identName.startsWith(QLatin1Char('$'))) {
            varType = OperandType::Str;
        } else {
            const OperandType sys = sysvar::systemVariableType(identName.toStdString());
            if (isKnown(sys)) varType = sys;
        }
        return QSharedPointer<VariableNode>::create(identName, varType);
    }
    if (check(TokenType::LEFT_PAREN)) {
        advance();
        QSharedPointer<ExpressionNode> expr = parseExpression();
        if (!check(TokenType::RIGHT_PAREN)) {
            if (m_verbose) qDebug() << "Parse error: Expected ) in variable index";
            return nullptr;
        }
        advance();
        return expr;
    }
    return nullptr;
}

// @"..." / \@...#...\@（格式化串项）
QSharedPointer<ExpressionNode> ExpressionParser::parseFormTerm() {
    const bool yenAt = check(TokenType::YEN_AT);
    const ExpressionToken tok = advance();
    if (m_formProvider) {
        if (QSharedPointer<ExpressionNode> node = m_formProvider(tok.value(), yenAt)) {
            return node;
        }
    }
    // 无提供者时退化为字符串字面量
    return QSharedPointer<LiteralNode>::create(tok.value());
}

QSharedPointer<ExpressionNode> ExpressionParser::parseFunctionCall() {
    const ExpressionToken token = consume(TokenType::IDENTIFIER, "Expected function name");
    if (!match(TokenType::LEFT_PAREN)) {
        if (m_verbose) qDebug() << "Parse error: Expected ( after function name";
        return nullptr;
    }

    QList<QSharedPointer<ExpressionNode>> args;
    QList<int> omittedArgs;   // 空实参位置（语义上「省略」，不是显式 0）
    if (!check(TokenType::RIGHT_PAREN)) {
        while (true) {
            // 允许空实参（Emuera 常见：GET_INT(, "x", ...)、FUNC(a,)）
            if (check(TokenType::COMMA)) {
                omittedArgs.append(args.size());
                args.append(QSharedPointer<LiteralNode>::create(0));
                advance();
                continue;
            }
            if (check(TokenType::RIGHT_PAREN)) {
                omittedArgs.append(args.size());
                args.append(QSharedPointer<LiteralNode>::create(0));
                break;
            }
            QSharedPointer<ExpressionNode> arg = parseExpression();
            if (!arg) {
                return nullptr;
            }
            args.append(arg);
            if (!match(TokenType::COMMA)) {
                break;
            }
        }
    }

    if (!match(TokenType::RIGHT_PAREN)) {
        if (m_verbose) qDebug() << "Parse error: Expected )";
        return nullptr;
    }

    auto fn = QSharedPointer<FunctionNode>::create(token.value(), args);
    for (int oi : omittedArgs) fn->markArgOmitted(oi);

    // 解析函数调用（对齐 C# IdentifierDictionary.GetFunctionMethod）：
    //   用户自定义函数（#FUNCTION(S)）优先 → 内置函数（内部命令）→ 未定义。
    const FunctionResolution res = resolveFunctionCall(token.value(), m_functionTypeProvider);
    if (res.isUserFunction) {
        fn->setUserFunction(true);
        fn->setValueType(res.returnType);
    } else if (res.isBuiltin) {
        fn->setBuiltinIndex(res.builtinIndex);
        fn->setValueType(res.returnType);
        // 解析期先做一次校验（此时变量类型多为 Unknown，类型检查宽松；
        // finalizeParse 会在类型回填后再校验一遍）
        fn->setArityError(validateBuiltinCall(kBuiltinFunctions[res.builtinIndex], args));
    } else {
        // C#：IdentifierDictionary.ThrowException(idStr, true) —— 未定义的関数
        fn->setValueType(OperandType::Unknown);
        fn->setArityError(QStringLiteral("未定义的函数 %1").arg(token.value()));
    }
    return fn;
}

bool ExpressionParser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool ExpressionParser::check(TokenType type) {
    if (isAtEnd()) {
        return false;
    }
    return m_tokens[m_current].type() == type;
}

ExpressionToken ExpressionParser::consume(TokenType type, const QString& message) {
    if (check(type)) {
        return advance();
    }
    // 解析失败点：调用方通常只拿到 nullptr，这里给出「期望什么/实际读到什么」
    qWarning() << "[parse] 表达式语法错误:" << message
               << "实际 token:" << peek().value();
    return ExpressionToken(TokenType::END_OF_FILE, "", -1, -1);
}

ExpressionToken ExpressionParser::peek() {
    if (isAtEnd()) {
        return ExpressionToken(TokenType::END_OF_FILE, "", -1, -1);
    }
    return m_tokens[m_current];
}

bool ExpressionParser::isAtEnd() {
    return m_current >= m_tokens.size();
}

ExpressionToken ExpressionParser::advance() {
    if (!isAtEnd()) {
        ++m_current;
        return m_tokens[m_current - 1];
    }
    return ExpressionToken(TokenType::END_OF_FILE, "", -1, -1);
}
