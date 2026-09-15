#ifndef EXPRESSION_LEXER_H
#define EXPRESSION_LEXER_H

#include <QString>
#include <QList>
#include "expression_token.h"

class ExpressionLexer {
public:
    ExpressionLexer();
    QList<ExpressionToken> tokenize(const QString& input, int line = 1);
    
private:
    void skipWhitespace();
    ExpressionToken readNextToken();
    ExpressionToken readNumber();
    ExpressionToken readString();
    ExpressionToken readIdentifier();
    ExpressionToken readCurlyBracedIdentifier();
    TokenType getOperatorType(const QString& op);
    bool isAtEnd() const;
    
    QString m_input;
    int m_position;
    int m_line;
    int m_column;
};

#endif // EXPRESSION_LEXER_H