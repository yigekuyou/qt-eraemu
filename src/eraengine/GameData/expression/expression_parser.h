#ifndef EXPRESSION_PARSER_H
#define EXPRESSION_PARSER_H

#include <QList>
#include <QMap>
#include "expression_lexer.h"
#include "expression_ast.h"

class ExpressionParser {
public:
    ExpressionParser();
    ExpressionNode* parse(const QList<ExpressionToken>& tokens);
    
private:
    // Parsing methods
    ExpressionNode* parseExpression();
    ExpressionNode* parseLogicalOr();
    ExpressionNode* parseLogicalAnd();
    ExpressionNode* parseEquality();
    ExpressionNode* parseComparison();
    ExpressionNode* parseAdditive();
    ExpressionNode* parseMultiplicative();
    ExpressionNode* parseUnary();
    ExpressionNode* parsePrimary();
    ExpressionNode* parseVariable();
    ExpressionNode* parseFunctionCall();
    ExpressionNode* parseConditional();
    ExpressionNode* parseIfStatement();
    
    // Helper methods
    bool match(TokenType type);
    bool check(TokenType type);
    ExpressionToken consume(TokenType type, const QString& message);
    ExpressionToken peek();
    bool isAtEnd();
    ExpressionToken advance();
    
    QList<ExpressionToken> m_tokens;
    int m_current;
};

#endif // EXPRESSION_PARSER_H
