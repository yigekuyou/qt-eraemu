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
#include "expression_lexer.h"
#include "strform_parser.h"   // findPercentEnd：@"…%"expr,width,align"…" 的跨度切分

ExpressionToken::ExpressionToken(TokenType type, const QString& value, int line, int column)
    : m_type(type), m_value(value), m_line(line), m_column(column) {}

ExpressionLexer::ExpressionLexer() : m_position(0), m_line(1), m_column(0) {}

QList<ExpressionToken> ExpressionLexer::tokenize(const QString& input, int line) {
    m_input = input;
    m_position = 0;
    m_line = line;
    m_column = 0;
    
    QList<ExpressionToken> tokens;
    
    while (!isAtEnd()) {
        skipWhitespace();
        const int before = m_position;
        ExpressionToken token = readNextToken();
        if (m_position == before) {
            // 防御：任何未推进位置的路径都不能造成死循环
            m_position++;
            m_column++;
            continue;
        }
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
    const QChar next = (m_position + 1 < m_input.length()) ? m_input[m_position + 1] : QChar();

    // 两字符运算符（与 C# OperatorManager 的符号表一一对应）
    auto two = [&](TokenType t, const QString& text) {
        m_position += 2;
        m_column += 2;
        return ExpressionToken(t, text, m_line, m_column);
    };
    auto one = [&](TokenType t, const QString& text) {
        m_position += 1;
        m_column += 1;
        return ExpressionToken(t, text, m_line, m_column);
    };

    switch (ch.toLatin1()) {
    case '(': return one(TokenType::LEFT_PAREN, "(");
    case ')': return one(TokenType::RIGHT_PAREN, ")");
    case '[':
        // Emuera 改名语法 [[...]]（[[NAME]]）：按字符串字面量处理
        if (next == '[') {
            const int close = m_input.indexOf("]]", m_position + 2);
            if (close > 0) {
                const QString inner = m_input.mid(m_position + 2, close - m_position - 2);
                m_position = close + 2;
                m_column += inner.length() + 4;
                return ExpressionToken(TokenType::STRING, inner, m_line, m_column);
            }
        }
        return one(TokenType::LEFT_BRACKET, "[");
    case ']': return one(TokenType::RIGHT_BRACKET, "]");
    case ',': return one(TokenType::COMMA, ",");
    case ':': return one(TokenType::COLON, ":");
    case ';': return one(TokenType::SEMICOLON, ";");
    case '?': return one(TokenType::QUESTION, "?");
    case '#': return one(TokenType::TERNARY_SEP, "#");
    case '~': return one(TokenType::BIT_NOT, "~");
    case '+':
        if (next == '+') return two(TokenType::INCREMENT, "++");
        return one(TokenType::PLUS, "+");
    case '-':
        if (next == '-') return two(TokenType::DECREMENT, "--");
        return one(TokenType::MINUS, "-");
    case '*': return one(TokenType::MULTIPLY, "*");
    case '/': return one(TokenType::DIVIDE, "/");
    case '%': return one(TokenType::MODULO, "%");
    case '^':
        if (next == '^') return two(TokenType::LOGICAL_XOR, "^^");
        return one(TokenType::BIT_XOR, "^");
    case '=':
        if (next == '=') return two(TokenType::EQUALS, "==");
        return one(TokenType::ASSIGN, "=");
    case '!':
        if (next == '=') return two(TokenType::NOT_EQUALS, "!=");
        if (next == '&') return two(TokenType::LOGICAL_NAND, "!&");
        if (next == '|') return two(TokenType::LOGICAL_NOR, "!|");
        return one(TokenType::NOT, "!");
    case '&':
        if (next == '&') return two(TokenType::AND, "&&");
        return one(TokenType::BIT_AND, "&");
    case '|':
        if (next == '|') return two(TokenType::OR, "||");
        return one(TokenType::BIT_OR, "|");
    case '<':
        if (next == '=') return two(TokenType::LESS_EQUAL, "<=");
        if (next == '<') return two(TokenType::SHIFT_LEFT, "<<");
        return one(TokenType::LESS_THAN, "<");
    case '>':
        if (next == '=') return two(TokenType::GREATER_EQUAL, ">=");
        if (next == '>') return two(TokenType::SHIFT_RIGHT, ">>");
        return one(TokenType::GREATER_THAN, ">");
    case '\'':
        // "'=" 字符串赋值；否则是单引号字符串字面量
        if (next == '=') return two(TokenType::ASSIGN_STR, "'=");
        return readString();
    case '"':
        return readString();
    case '@':
        // @"..." —— 带内嵌表达式的格式化串
        if (next == '"') return readStrFormAt();
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::UNKNOWN, ch, m_line, m_column);
    case '\\':
        // \@ cond ? A # B \@ —— 条件三元格式化串
        if (next == '@') return readYenAt();
        m_position++;
        m_column++;
        return ExpressionToken(TokenType::UNKNOWN, ch, m_line, m_column);
    case '{':
        return readCurlyBracedIdentifier();
    default:
        if (ch.isDigit() && ch.unicode() < 128) {
            // 仅 ASCII 数字才是数值字面量（C# LexicalAnalyzer.Analyse 的 case '0'..'9'）；
            // 全角数字（如 「１moreフラグ」）属于标识符。
            return readNumber();
        } else if (ch.isLetter() || ch == '_' || ch.unicode() > 127) {
            // 允许非 ASCII 标识符（日文/全角变量名，对齐 C# LexicalAnalyzer）
            return readIdentifier();
        } else {
            m_position++;
            m_column++;
            return ExpressionToken(TokenType::UNKNOWN, ch, m_line, m_column);
        }
    }
}

ExpressionToken ExpressionLexer::readNumber() {
    const int start = m_position;

    // 进制前缀：0x/0X（16）、0b/0B（2）—— 对齐 C# LexicalAnalyzer.ReadInt64
    int base = 10;
    if (m_input[m_position] == '0' && m_position + 1 < m_input.length()
        && (m_input[m_position + 1] == 'x' || m_input[m_position + 1] == 'X'
            || m_input[m_position + 1] == 'b' || m_input[m_position + 1] == 'B')) {
        base = (m_input[m_position + 1] == 'x' || m_input[m_position + 1] == 'X') ? 16 : 2;
        m_position += 2;
        m_column += 2;
    }

    const auto digitOk = [base](QChar c) {
        if (c.isDigit()) return base == 2 ? (c == '0' || c == '1') : true;
        if (base == 16) return c.isLetter() && c.toLower() >= 'a' && c.toLower() <= 'f';
        return false;
    };

    while (m_position < m_input.length() && digitOk(m_input[m_position])) {
        m_position++;
        m_column++;
    }

    // 指数：p/P 为 2 的幂、e/E 为 10 的幂（C# 支持 "1p0" 这种 2 进制指数写法）
    if (base == 10 && m_position < m_input.length()
        && (m_input[m_position] == 'p' || m_input[m_position] == 'P'
            || m_input[m_position] == 'e' || m_input[m_position] == 'E')) {
        m_position++;
        m_column++;
        if (m_position < m_input.length()
            && (m_input[m_position] == '+' || m_input[m_position] == '-')) {
            m_position++;
            m_column++;
        }
        while (m_position < m_input.length() && m_input[m_position].isDigit()) {
            m_position++;
            m_column++;
        }
    }

    QString value = m_input.mid(start, m_position - start);
    return ExpressionToken(TokenType::NUMBER, value, m_line, m_column);
}

ExpressionToken ExpressionLexer::readString() {
    // 打开引号可以是 " 或 '？C# 中 "..." 是普通字符串；'...' 另作 StrForm，
    // 此处统一按字符串字面量读取，并支持转义（\n \t \s \\ \"）。
    const QChar quote = m_input[m_position];
    ++m_position;   // skip opening quote
    ++m_column;

    QString value;
    while (m_position < m_input.length() && m_input[m_position] != quote) {
        QChar c = m_input[m_position];
        if (c == '\\' && m_position + 1 < m_input.length()) {
            const QChar esc = m_input[m_position + 1];
            switch (esc.toLatin1()) {
            case 'n':  value += '\n'; break;
            case 't':  value += '\t'; break;
            case 's':  value += ' ';  break;
            case 'r':  value += '\r'; break;
            case '\\': value += '\\'; break;
            case '"':  value += '"';  break;
            case '\'': value += '\''; break;
            default:   value += esc;  break;
            }
            m_position += 2;
            m_column += 2;
            continue;
        }
        value += c;
        ++m_position;
        ++m_column;
    }

    if (m_position < m_input.length()) {
        ++m_position;   // skip closing quote
        ++m_column;
    }

    return ExpressionToken(TokenType::STRING, value, m_line, m_column);
}

ExpressionToken ExpressionLexer::readIdentifier() {
    int start = m_position;
    while (m_position < m_input.length() &&
            (m_input[m_position].isLetter() || m_input[m_position].isDigit() ||
             m_input[m_position] == '_' || m_input[m_position].unicode() > 127)) {
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

// @"..." —— 终止引号由「顶层」的引号决定：括号/引号内出现的 " 不算（如 @"%A+(" "*(B))%"）
ExpressionToken ExpressionLexer::readStrFormAt() {
    const int start = m_position + 2;
    const int after = StrFormParser::expressionSpanEnd(m_input, m_position);
    const bool closed = after > start && m_input[after - 1] == '"';
    const QString value = m_input.mid(start, after - start - (closed ? 1 : 0));
    m_column += after - m_position;
    m_position = after;
    return ExpressionToken(TokenType::STRFORM_AT, value, m_line, m_column);
}

// \@ cond ? A # B \@ —— 取到下一个 \@ 为止的内部文本
ExpressionToken ExpressionLexer::readYenAt() {
    m_position += 2;   // 跳过 \@
    m_column += 2;

    const int start = m_position;
    int end = -1;
    while (m_position + 1 < m_input.length()) {
        if (m_input.at(m_position) == QLatin1Char('\\')
            && m_input.at(m_position + 1) == QLatin1Char('@')) {
            end = m_position;
            break;
        }
        ++m_position;
        ++m_column;
    }

    QString value;
    if (end >= 0) {
        value = m_input.mid(start, end - start);
        m_position = end + 2;
        m_column += 2;
    } else {
        value = m_input.mid(start);
        m_position = m_input.length();
    }
    return ExpressionToken(TokenType::YEN_AT, value, m_line, m_column);
}


bool ExpressionLexer::isAtEnd() const {
    return m_position >= m_input.length();
}