#include "expression_token.h"

ExpressionToken::ExpressionToken(TokenType type, const QString& value, int line, int column)
    : m_type(type), m_value(value), m_line(line), m_column(column) {}