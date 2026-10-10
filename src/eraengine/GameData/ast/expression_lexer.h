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
#ifndef EXPRESSION_LEXER_H
#define EXPRESSION_LEXER_H

#include <QString>
#include <QList>

// ---------------------------------------------------------------------------
// Token types + token value.
// Formerly a separate expression_token.h/.cpp pair; a token only ever exists as
// the output of the lexer and the input of the parser, so it lives with them.
// ---------------------------------------------------------------------------

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

    // ---- operators (与 C# OperatorCode/OperatorManager 对齐）----
    EQUALS,        // ==
    NOT_EQUALS,    // !=
    LESS_THAN,     // <
    LESS_EQUAL,    // <=
    GREATER_THAN,  // >
    GREATER_EQUAL, // >=
    PLUS,          // +
    MINUS,         // -
    MULTIPLY,      // *
    DIVIDE,        // /
    MODULO,        // %
    POWER,         // (保留：Emuera 无幂运算符，词法不再产生)
    AND,           // &&
    OR,            // ||
    NOT,           // !
    ASSIGN,        // =
    ASSIGN_STR,    // '=
    BIT_AND,       // &
    BIT_OR,        // |
    BIT_XOR,       // ^
    BIT_NOT,       // ~
    SHIFT_LEFT,    // <<
    SHIFT_RIGHT,   // >>
    LOGICAL_XOR,   // ^^
    LOGICAL_NAND,  // !&
    LOGICAL_NOR,   // !|
    INCREMENT,     // ++
    DECREMENT,     // --
    QUESTION,      // ?
    TERNARY_SEP,   // #

    // ---- 格式化串（StrForm）----
    STRFORM_AT,    // @"...."      —— 带内嵌表达式的字符串（C# AnalyseFormattedString）
    YEN_AT,        // \@ c ? A # B \@ —— 条件三元格式化串（C# AnalyseYenAt）

    UNKNOWN,
    END_OF_FILE
};

class ExpressionToken {
public:
    ExpressionToken() = default;   // 供 AST 节点默认构造
    ExpressionToken(TokenType type, const QString& value, int line = 0, int column = 0);

    TokenType type() const { return m_type; }
    QString value() const { return m_value; }
    int line() const { return m_line; }
    int column() const { return m_column; }

    bool integerValid() const { return m_integerValid; }
    qint64 integerValue() const { return m_integerValue; }

    bool isOperator() const { return m_type >= TokenType::EQUALS && m_type < TokenType::UNKNOWN; }
    bool isLiteral() const { return m_type == TokenType::NUMBER || m_type == TokenType::STRING; }
    bool isIdentifier() const { return m_type == TokenType::IDENTIFIER; }

private:
    TokenType m_type = TokenType::UNKNOWN;
    QString m_value;
    qint64 m_integerValue = 0;
    bool m_integerValid = false;
    int m_line;
    int m_column;
};

class ExpressionLexer {
public:
    ExpressionLexer();
    void setAllowSingleQuotation(bool allow) { m_allowSingleQuotation = allow; }
    QList<ExpressionToken> tokenize(const QString& input, int line = 1);
    
private:
    void skipWhitespace();
    ExpressionToken readNextToken();
    ExpressionToken readNumber();
    ExpressionToken readString();
    ExpressionToken readIdentifier();
    ExpressionToken readCurlyBracedIdentifier();
    ExpressionToken readStrFormAt();
    ExpressionToken readYenAt();
    bool isAtEnd() const;
    
    bool m_allowSingleQuotation = false;
    QString m_input;
    int m_position;
    int m_line;
    int m_column;
};

#endif // EXPRESSION_LEXER_H