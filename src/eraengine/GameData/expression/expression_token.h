#ifndef EXPRESSION_TOKEN_H
#define EXPRESSION_TOKEN_H

#include <QString>
#include <QHash>

enum class TokenType {
    NUMBER,
    STRING,
    IDENTIFIER,
    OPERATOR,
    LEFT_PAREN,
    RIGHT_PAREN,
    LEFT_BRACKET,
    RIGHT_BRACKET,
    COMMA,
    COLON,
    SEMICOLON,
    IF,
    THEN,
    ELSE,
    ENDIF,
    EQUALS,
    NOT_EQUALS,
    LESS_THAN,
    LESS_EQUAL,
    GREATER_THAN,
    GREATER_EQUAL,
    PLUS,
    MINUS,
    MULTIPLY,
    DIVIDE,
    MODULO,
    POWER,
    AND,
    OR,
    NOT,
    UNKNOWN,
    END_OF_FILE
};

class ExpressionToken {
public:
    ExpressionToken(TokenType type, const QString& value, int line = 0, int column = 0);
    
    TokenType type() const { return m_type; }
    QString value() const { return m_value; }
    int line() const { return m_line; }
    int column() const { return m_column; }
    
    bool isOperator() const { return m_type >= TokenType::EQUALS && m_type <= TokenType::NOT; }
    bool isLiteral() const { return m_type == TokenType::NUMBER || m_type == TokenType::STRING; }
    bool isIdentifier() const { return m_type == TokenType::IDENTIFIER; }

private:
    TokenType m_type;
    QString m_value;
    int m_line;
    int m_column;
};

#endif // EXPRESSION_TOKEN_H