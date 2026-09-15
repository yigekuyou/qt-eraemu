#include "expression_lexer.h"
#include "expression_token.h"
#include <QRegularExpression>
#include <QSet>

ExpressionLexer::ExpressionLexer() : m_position(0), m_line(1), m_column(0) {}

QList<ExpressionToken> ExpressionLexer::tokenize(const QString& input, int line) {
    m_input = input;
    m_position = 0;
    m_line = line;
    m_column = 0;
    
    QList<ExpressionToken> tokens;
    
    while (!isAtEnd()) {
        skipWhitespace();
        ExpressionToken token = readNextToken();
        if (token.type() != TokenType::UNKNOWN) {
            tokens.append(token);
        }
    }
    
    // Add end of file token
    tokens.append(ExpressionToken(TokenType::END_OF_FILE, "", m_line, m_column));
    
    return tokens;
}

void ExpressionLexer::skipWhitespace() {
    while (!isAtEnd()) {
        QChar ch = m_input[m_position];
        if (ch.isSpace() || ch == '\t' || ch == '\r' || ch == '\n') {
            if (ch == '\n') {
                m_line++;
                m_column = 0;
            } else {
                m_column++;
            }
            m_position++;
        } else {
            break;
        }
    }
}

ExpressionToken ExpressionLexer::readNextToken() {
    if (isAtEnd()) {
        return ExpressionToken(TokenType::END_OF_FILE, "", m_line, m_column);
    }
    
    QChar ch = m_input[m_position];
    
    // Handle different token types
    switch (ch.toLatin1()) {
    case '(':
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::LEFT_PAREN, "(", m_line, m_column);
    case ')':
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::RIGHT_PAREN, ")", m_line, m_column);
    case '[':
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::LEFT_BRACKET, "[", m_line, m_column);
    case ']':
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::RIGHT_BRACKET, "]", m_line, m_column);
    case ',':
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::COMMA, ",", m_line, m_column);
    case ':':
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::COLON, ":", m_line, m_column);
    case ';':
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::SEMICOLON, ";", m_line, m_column);
    case '+':
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::PLUS, "+", m_line, m_column);
    case '-':
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::MINUS, "-", m_line, m_column);
    case '*':
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::MULTIPLY, "*", m_line, m_column);
    case '/':
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::DIVIDE, "/", m_line, m_column);
    case '%':
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::MODULO, "%", m_line, m_column);
    case '^':
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::POWER, "^", m_line, m_column);
    case '=':
        if (m_position + 1 < m_input.length() && m_input[m_position + 1] == '=') {
            m_position += 2;
            m_column += 2;
            return ExpressionToken(TokenType::EQUALS, "==", m_line, m_column);
        }
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::EQUALS, "=", m_line, m_column);
    case '!':
        if (m_position + 1 < m_input.length() && m_input[m_position + 1] == '=') {
            m_position += 2;
            m_column += 2;
            return ExpressionToken(TokenType::NOT_EQUALS, "!=", m_line, m_column);
        }
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::NOT, "!", m_line, m_column);
    case '<':
        if (m_position + 1 < m_input.length() && m_input[m_position + 1] == '=') {
            m_position += 2;
            m_column += 2;
            return ExpressionToken(TokenType::LESS_EQUAL, "<=", m_line, m_column);
        }
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::LESS_THAN, "<", m_line, m_column);
    case '>':
        if (m_position + 1 < m_input.length() && m_input[m_position + 1] == '=') {
            m_position += 2;
            m_column += 2;
            return ExpressionToken(TokenType::GREATER_EQUAL, ">=", m_line, m_column);
        }
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::GREATER_THAN, ">", m_line, m_column);
    case '"':
        return readString();
    case '{':
        return readCurlyBracedIdentifier();
    default:
        if (ch.isDigit()) {
            return readNumber();
        } else if (ch.isLetter() || ch == '_') {
            return readIdentifier();
        } else {
            m_position++;
            m_column++;
            return ExpressionToken(TokenType::UNKNOWN, ch, m_line, m_column);
        }
    }
}

ExpressionToken ExpressionLexer::readNumber() {
    int start = m_position;
    while (m_position < m_input.length() && 
           (m_input[m_position].isDigit() || m_input[m_position] == '.')) {
        m_position++;
        m_column++;
    }
    
    QString value = m_input.mid(start, m_position - start);
    return ExpressionToken(TokenType::NUMBER, value, m_line, m_column);
}

ExpressionToken ExpressionLexer::readString() {
    int start = ++m_position;  // Skip the opening quote
    m_column++;
    
    while (m_position < m_input.length() && m_input[m_position] != '"') {
        m_position++;
        m_column++;
    }
    
    if (m_position < m_input.length()) {
        m_position++;  // Skip the closing quote
        m_column++;
    }
    
    QString value = m_input.mid(start, m_position - start - 1);
    return ExpressionToken(TokenType::STRING, value, m_line, m_column);
}

ExpressionToken ExpressionLexer::readIdentifier() {
    int start = m_position;
    while (m_position < m_input.length() && 
            (m_input[m_position].isLetter() || m_input[m_position].isDigit() || 
             m_input[m_position] == '_')) {
        m_position++;
        m_column++;
    }
    
    QString value = m_input.mid(start, m_position - start);
    return ExpressionToken(TokenType::IDENTIFIER, value, m_line, m_column);
}

ExpressionToken ExpressionLexer::readCurlyBracedIdentifier() {
    m_position++;  // Skip the opening {
    m_column++;
    
    int start = m_position;
    while (m_position < m_input.length() && m_input[m_position] != '}') {
        m_position++;
        m_column++;
    }
    
    if (m_position < m_input.length()) {
        m_position++;  // Skip the closing }
        m_column++;
    }
    
    QString value = m_input.mid(start, m_position - start - 1);
    return ExpressionToken(TokenType::IDENTIFIER, value, m_line, m_column);
}

TokenType ExpressionLexer::getOperatorType(const QString& op) {
    static QHash<QString, TokenType> operators = {
        {"==", TokenType::EQUALS},
        {"!=", TokenType::NOT_EQUALS},
        {"<", TokenType::LESS_THAN},
        {"<=", TokenType::LESS_EQUAL},
        {">", TokenType::GREATER_THAN},
        {">=", TokenType::GREATER_EQUAL},
        {"+", TokenType::PLUS},
        {"-", TokenType::MINUS},
        {"*", TokenType::MULTIPLY},
        {"/", TokenType::DIVIDE},
        {"%", TokenType::MODULO},
        {"^", TokenType::POWER},
        {"!", TokenType::NOT},
        {"&&", TokenType::AND},
        {"||", TokenType::OR}
    };
    
    return operators.value(op, TokenType::UNKNOWN);
}

bool ExpressionLexer::isAtEnd() const {
    return m_position >= m_input.length();
}