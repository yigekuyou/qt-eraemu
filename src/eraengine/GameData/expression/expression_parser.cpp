#include "expression_parser.h"
#include <QRegularExpression>
#include <QDebug>

ExpressionParser::ExpressionParser() : m_current(0) {}

ExpressionNode* ExpressionParser::parse(const QList<ExpressionToken>& tokens) {
    m_tokens = tokens;
    m_current = 0;
    
    try {
        return parseExpression();
    } catch (const std::exception& e) {
        qDebug() << "Parse error: " << e.what();
        return nullptr;
    }
}

ExpressionNode* ExpressionParser::parseExpression() {
    return parseLogicalOr();
}

ExpressionNode* ExpressionParser::parseLogicalOr() {
    ExpressionNode* left = parseLogicalAnd();
    
    while (check(TokenType::OR)) {
        ExpressionToken op = consume(TokenType::OR, "Expected OR operator");
        ExpressionNode* right = parseLogicalAnd();
        left = new BinaryOpNode(left, op, right);
    }
    
    return left;
}

ExpressionNode* ExpressionParser::parseLogicalAnd() {
    ExpressionNode* left = parseEquality();
    
    while (check(TokenType::AND)) {
        ExpressionToken op = consume(TokenType::AND, "Expected AND operator");
        ExpressionNode* right = parseEquality();
        left = new BinaryOpNode(left, op, right);
    }
    
    return left;
}

ExpressionNode* ExpressionParser::parseEquality() {
    ExpressionNode* left = parseComparison();
    
    while (check(TokenType::EQUALS) || check(TokenType::NOT_EQUALS)) {
        ExpressionToken op = peek();
        if (op.type() == TokenType::EQUALS) {
            consume(TokenType::EQUALS, "Expected == operator");
            ExpressionNode* right = parseComparison();
            left = new BinaryOpNode(left, op, right);
        } else if (op.type() == TokenType::NOT_EQUALS) {
            consume(TokenType::NOT_EQUALS, "Expected != operator");
            ExpressionNode* right = parseComparison();
            left = new BinaryOpNode(left, op, right);
        }
    }
    
    return left;
}

ExpressionNode* ExpressionParser::parseComparison() {
    ExpressionNode* left = parseAdditive();
    
    while (check(TokenType::LESS_THAN) || check(TokenType::LESS_EQUAL) ||
           check(TokenType::GREATER_THAN) || check(TokenType::GREATER_EQUAL)) {
        ExpressionToken op = peek();
        if (op.type() == TokenType::LESS_THAN) {
            consume(TokenType::LESS_THAN, "Expected < operator");
            ExpressionNode* right = parseAdditive();
            left = new BinaryOpNode(left, op, right);
        } else if (op.type() == TokenType::LESS_EQUAL) {
            consume(TokenType::LESS_EQUAL, "Expected <= operator");
            ExpressionNode* right = parseAdditive();
            left = new BinaryOpNode(left, op, right);
        } else if (op.type() == TokenType::GREATER_THAN) {
            consume(TokenType::GREATER_THAN, "Expected > operator");
            ExpressionNode* right = parseAdditive();
            left = new BinaryOpNode(left, op, right);
        } else if (op.type() == TokenType::GREATER_EQUAL) {
            consume(TokenType::GREATER_EQUAL, "Expected >= operator");
            ExpressionNode* right = parseAdditive();
            left = new BinaryOpNode(left, op, right);
        }
    }
    
    return left;
}

ExpressionNode* ExpressionParser::parseAdditive() {
    ExpressionNode* left = parseMultiplicative();
    
    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        ExpressionToken op = peek();
        if (op.type() == TokenType::PLUS) {
            consume(TokenType::PLUS, "Expected + operator");
            ExpressionNode* right = parseMultiplicative();
            left = new BinaryOpNode(left, op, right);
        } else if (op.type() == TokenType::MINUS) {
            consume(TokenType::MINUS, "Expected - operator");
            ExpressionNode* right = parseMultiplicative();
            left = new BinaryOpNode(left, op, right);
        }
    }
    
    return left;
}

ExpressionNode* ExpressionParser::parseMultiplicative() {
		ExpressionNode* left = parseUnary();

		while (check(TokenType::MULTIPLY) || check(TokenType::DIVIDE) || check(TokenType::MODULO)) {
				ExpressionToken op = peek();
				if (op.type() == TokenType::MULTIPLY) {
						consume(TokenType::MULTIPLY, "Expected * operator");
						ExpressionNode* right = parseUnary();
						left = new BinaryOpNode(left, op, right);
				} else if (op.type() == TokenType::DIVIDE) {
						consume(TokenType::DIVIDE, "Expected / operator");
						ExpressionNode* right = parseUnary();
						left = new BinaryOpNode(left, op, right);
				} else if (op.type() == TokenType::MODULO) {
						consume(TokenType::MODULO, "Expected % operator");
						ExpressionNode* right = parseUnary();
						left = new BinaryOpNode(left, op, right);
				}
		}

		return left;
}

ExpressionNode* ExpressionParser::parseUnary() {
    if (check(TokenType::MINUS)) {
        ExpressionToken op = consume(TokenType::MINUS, "Expected unary minus");
        ExpressionNode* operand = parseUnary();
        return new UnaryOpNode(op, operand);
    }
    
    if (check(TokenType::NOT)) {
        ExpressionToken op = consume(TokenType::NOT, "Expected NOT operator");
        ExpressionNode* operand = parseUnary();
        return new UnaryOpNode(op, operand);
    }
    
    return parsePrimary();
}

ExpressionNode* ExpressionParser::parsePrimary() {
		if (check(TokenType::NUMBER)) {
				return new LiteralNode(consume(TokenType::NUMBER, "Expected number"));
		}

		if (check(TokenType::STRING)) {
				return new LiteralNode(consume(TokenType::STRING, "Expected string"));
		}

		if (check(TokenType::IDENTIFIER)) {
				// 检查是否为函数调用（标识符后跟左括号或冒号）
				if (m_current + 1 < m_tokens.size() &&
						(m_tokens[m_current + 1].type() == TokenType::LEFT_PAREN ||
						 m_tokens[m_current + 1].type() == TokenType::COLON)) {
						return parseFunctionCall();
				}
				return parseVariable();
		}

		if (check(TokenType::LEFT_PAREN)) {
				consume(TokenType::LEFT_PAREN, "Expected (");
				ExpressionNode* expr = parseExpression();
				consume(TokenType::RIGHT_PAREN, "Expected )");
				return expr;
		}

		// 检查是否为 IF 条件表达式
		if (check(TokenType::IF)) {
				return parseConditional();
		}

		// 处理布尔字面量
		ExpressionToken token = peek();
		if (token.type() == TokenType::IDENTIFIER) {
				QString val = token.value();
				if (val == "true" || val == "false") {
						consume(token.type(), "Expected boolean");
						return new LiteralNode(ExpressionToken(TokenType::NUMBER, val, token.line(), token.column()));
				}
		}

		return nullptr;
}

ExpressionNode* ExpressionParser::parseVariable() {
    ExpressionToken token = consume(TokenType::IDENTIFIER, "Expected variable name");
    QString varName = token.value();
    
    // Check for array access syntax (e.g., DAY:1 or TALENT:MASTER:恋慕)
    VariableNode* varNode = new VariableNode(varName);
    
    // Parse multiple array indices (for multi-dimensional arrays)
    while (check(TokenType::COLON)) {
        consume(TokenType::COLON, "Expected : after variable name");
        
        // Parse the index expression
        ExpressionNode* index = parseExpression();
        if (index) {
            varNode->addIndex(index);
        }
    }
    
    return varNode;
}

ExpressionNode* ExpressionParser::parseFunctionCall() {
    ExpressionToken token = consume(TokenType::IDENTIFIER, "Expected function name");
    QString funcName = token.value();
    
    // Handle both ( and : as function call operators
    TokenType argOp = TokenType::LEFT_PAREN;
    if (check(TokenType::LEFT_PAREN)) {
        consume(TokenType::LEFT_PAREN, "Expected ( after function name");
    } else if (check(TokenType::COLON)) {
        consume(TokenType::COLON, "Expected : after function name");
        argOp = TokenType::COLON;
    } else {
        qDebug() << "Parse error: Expected ( or : after function name";
        return nullptr;
    }
    
    QList<ExpressionNode*> args;
    if (!check(TokenType::RIGHT_PAREN)) {
        args.append(parseExpression());
        while (check(TokenType::COMMA)) {
            consume(TokenType::COMMA, "Expected ,");
            args.append(parseExpression());
        }
    }
    consume(TokenType::RIGHT_PAREN, "Expected )");
    
    return new FunctionNode(funcName, args);
}

ExpressionNode* ExpressionParser::parseConditional() {
		if (!check(TokenType::IF)) {
				return nullptr;
		}

		consume(TokenType::IF, "Expected IF");
		ExpressionNode* condition = parseExpression();

		consume(TokenType::THEN, "Expected THEN");
		ExpressionNode* thenExpr = parseExpression();

		ExpressionNode* elseExpr = nullptr;
		if (check(TokenType::ELSE)) {
				consume(TokenType::ELSE, "Expected ELSE");
				elseExpr = parseExpression();
		}

		if (check(TokenType::ENDIF)) {
				consume(TokenType::ENDIF, "Expected ENDIF");
		}

		return new IfNode(condition, thenExpr, elseExpr);
}

ExpressionNode* ExpressionParser::parseIfStatement() {
		return parseConditional();
}

bool ExpressionParser::match(TokenType type) {
    if (isAtEnd()) return false;
    if (m_tokens[m_current].type() == type) {
        m_current++;
        return true;
    }
    return false;
}

bool ExpressionParser::check(TokenType type) {
    if (isAtEnd()) return false;
    return m_tokens[m_current].type() == type;
}

ExpressionToken ExpressionParser::consume(TokenType type, const QString& message) {
    if (check(type)) {
        return advance();
    }
    qDebug() << "Parse error: " << message;
    // Return an invalid token for error handling
    return ExpressionToken(TokenType::END_OF_FILE, "", -1, -1);
}

ExpressionToken ExpressionParser::peek() {
    return m_tokens[m_current];
}

bool ExpressionParser::isAtEnd() {
    return m_current >= m_tokens.size();
}

ExpressionToken ExpressionParser::advance() {
    if (!isAtEnd()) {
        m_current++;
    }
    if (m_current > 0) {
        return m_tokens[m_current - 1];
    }
    return ExpressionToken(TokenType::END_OF_FILE, "", -1, -1);
}
