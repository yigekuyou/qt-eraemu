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
#include "ast_builder.h"
#include "strform_parser.h"
#include "argument_parser.h"

namespace {

// 剥离行尾 ';' 注释（Emuera 注释语义），尊重引号
QString stripLineComment(const QString& text) {
    QChar quote;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (!quote.isNull()) {
            if (c == quote) quote = QChar();
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; continue; }
        if (c == QLatin1Char(';')) return text.left(i);
    }
    return text;
}

// 构建完成后做「语句参数类型化 + 解析期校验」
LogicalLine finalized(LogicalLine line) {
    ArgumentParser::build(line);
    return line;
}

bool isOperatorStart(QChar c) {
    static const QString ops = QStringLiteral("+-*/%=!<>&|^~?#");
    return ops.contains(c);
}

bool isSymbolChar(QChar c) {
    static const QString syms = QStringLiteral("()[],:;");
    return syms.contains(c);
}

// 顶层逗号切分（跳过引号 / 括号嵌套）——用于形参列表
QStringList splitTopLevelComma(const QString& text) {
    QStringList out;
    QString current;
    int depth = 0;
    QChar quote;
    for (const QChar ch : text) {
        if (!quote.isNull()) { current += ch; if (ch == quote) quote = QChar(); continue; }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) { quote = ch; current += ch; continue; }
        if (ch == QLatin1Char('(') || ch == QLatin1Char('[')) { ++depth; current += ch; continue; }
        if (ch == QLatin1Char(')') || ch == QLatin1Char(']')) { if (depth > 0) --depth; current += ch; continue; }
        if (depth == 0 && ch == QLatin1Char(',')) { out << current.trimmed(); current.clear(); continue; }
        current += ch;
    }
    if (!current.trimmed().isEmpty()) out << current.trimmed();
    return out;
}

// 顶层 '=' 的位置（-1 = 无）；用于剥离形参默认值
int topLevelEquals(const QString& text) {
    int depth = 0;
    QChar quote;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (!quote.isNull()) { if (ch == quote) quote = QChar(); continue; }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) { quote = ch; continue; }
        if (ch == QLatin1Char('(') || ch == QLatin1Char('[')) { ++depth; continue; }
        if (ch == QLatin1Char(')') || ch == QLatin1Char(']')) { if (depth > 0) --depth; continue; }
        if (depth == 0 && ch == QLatin1Char('=')) return i;
    }
    return -1;
}

} // namespace

bool AstBuilder::isConditionInstruction(const QString& upperName) {
    return upperName == QLatin1String("IF")
        || upperName == QLatin1String("SIF")
        || upperName == QLatin1String("ELSEIF")
        || upperName == QLatin1String("WHILE")
        || upperName == QLatin1String("REPEAT");
}

// 首操作数是标签名的指令族（CALL/CALLFORM/CALLF/TRYCALL*/JUMP*/BEGIN）
bool AstBuilder::isCallFamilyInstruction(const QString& upperName) {
    static const char* kNames[] = {
        "CALL", "CALLFORM", "CALLF", "CALLFORMF",
        "TRYCALL", "TRYCALLFORM", "TRYCALLF", "TRYCALLFORMF",
        "JUMP", "JUMPFORM", "TRYJUMP", "TRYJUMPFORM",
        "GOTOFORM", "TRYGOTOFORM", "CALLEVENT", "TRYCALLEVENT",
        "BEGIN",
    };
    for (const char* n : kNames) {
        if (upperName == QLatin1String(n)) return true;
    }
    return false;
}

// PRINT 族的参数形态（对齐 C# PRINT_Instruction 扫描后缀选 ArgumentBuilder）：
//   PRINT…(V)     -> SP_PRINTV        逗号分隔的整型表达式
//   PRINT…(S)     -> STR_EXPRESSION   字符串表达式
//   PRINT…(FORMS) -> STR_EXPRESSION   字符串表达式（结果再按格式串展开）
//   PRINT…(FORM)  -> FORM_STR_NULLABLE 格式化串（文本 + {…}/%…%）
//   PRINT…(其它)  -> STR_NULLABLE     整行**字面文本**（不是表达式！）
// 最后一条很关键：eramaker 风格的 `PRINTL [0] 结缘(\1000)` 整行是文本。
AstBuilder::PrintArgMode AstBuilder::classifyPrintArg(const QString& upperName) {
    QString rest;
    if (upperName.startsWith(QLatin1String("PRINTPLAINFORM"))) return PrintArgMode::Literal;
    if (upperName.startsWith(QLatin1String("PRINTPLAIN"))) return PrintArgMode::Literal;
    if (upperName.startsWith(QLatin1String("PRINTSINGLE"))) rest = upperName.mid(11);
    else if (upperName.startsWith(QLatin1String("PRINT"))) rest = upperName.mid(5);
    else return PrintArgMode::NotPrint;

    // 排除 PRINTBUTTON / PRINTDATA（各自有专用参数族）
    for (const QChar c : rest) {
        if (!QLatin1String("VSLWCKDFORM").contains(c)) return PrintArgMode::NotPrint;
    }
    if (rest.startsWith(QLatin1String("FORMS"))) return PrintArgMode::StrExpression;
    if (rest.startsWith(QLatin1String("FORM"))) return PrintArgMode::FormStr;
    if (rest.startsWith(QLatin1Char('V'))) return PrintArgMode::PrintV;
    if (rest.startsWith(QLatin1Char('S'))) return PrintArgMode::StrExpression;
    return PrintArgMode::Literal;
}

bool AstBuilder::isStrFormInstruction(const QString& upperName) {
    // 对齐 C#：这些指令的操作数是 StrForm（文本 + {expr}/%expr%）
    if (classifyPrintArg(upperName) == PrintArgMode::FormStr) return true;
    return upperName == QLatin1String("DRAWLINEFORM")
        || upperName == QLatin1String("PRINTPLAINFORM")
        || upperName == QLatin1String("STRFORM");
}

// ---------------------------------------------------------------------------
// tokenize —— 对齐 C# LexicalAnalyzer.Analyse 的简化版本
// ---------------------------------------------------------------------------
WordCollection AstBuilder::tokenize(const QString& line) {
    WordCollection wc;
    const int n = line.length();
    int i = 0;

    while (i < n) {
        const QChar ch = line.at(i);

        if (ch.isSpace()) { ++i; continue; }

        // 字符串字面量（"..." 或 '...'）
        if (ch == '"' || ch == '\'') {
            const QChar quote = ch;
            const int start = i;
            ++i;
            QString value;
            while (i < n && line.at(i) != quote) {
                if (line.at(i) == '\\' && i + 1 < n) {
                    const QChar esc = line.at(i + 1);
                    switch (esc.toLatin1()) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 's': value += ' ';  break;
                    case '\\': value += '\\'; break;
                    default: value += esc; break;
                    }
                    i += 2;
                    continue;
                }
                value += line.at(i);
                ++i;
            }
            if (i < n) ++i;   // closing quote
            wc.addWord(WordKind::String, value, line.mid(start, i - start), true);
            continue;
        }

        // %VAR% / $VAR 变量引用
        if (ch == '%' || ch == '$') {
            const int start = i;
            ++i;
            while (i < n && !line.at(i).isSpace()
                   && !isSymbolChar(line.at(i)) && line.at(i) != '%') {
                ++i;
            }
            if (i < n && line.at(i) == '%') ++i;   // closing %
            const QString raw = line.mid(start, i - start);
            wc.addWord(WordKind::Identifier, raw, raw);
            continue;
        }

        // 数字字面量
        if (ch.isDigit()) {
            const int start = i;
            if (ch == '0' && i + 1 < n && (line.at(i + 1) == 'x' || line.at(i + 1) == 'X'
                                           || line.at(i + 1) == 'b' || line.at(i + 1) == 'B')) {
                i += 2;
                while (i < n && line.at(i).isLetterOrNumber()) ++i;
            } else {
                while (i < n && line.at(i).isDigit()) ++i;
                // p/P（2 的幂）或 e/E（10 的幂）指数：如 1p0
                if (i < n && (line.at(i) == 'p' || line.at(i) == 'P'
                              || line.at(i) == 'e' || line.at(i) == 'E')) {
                    ++i;
                    if (i < n && (line.at(i) == '+' || line.at(i) == '-')) ++i;
                    while (i < n && line.at(i).isDigit()) ++i;
                }
            }
            wc.addWord(WordKind::Number, line.mid(start, i - start));
            continue;
        }

        // 标识符 / 关键词（含非 ASCII 文本）
        if (ch.isLetter() || ch == '_' || ch.unicode() > 127) {
            const int start = i;
            while (i < n) {
                const QChar c = line.at(i);
                if (c.isLetterOrNumber() || c == '_' || c.unicode() > 127) {
                    ++i;
                } else {
                    break;
                }
            }
            wc.addWord(WordKind::Identifier, line.mid(start, i - start));
            continue;
        }

        // 运算符（贪婪取最长）
        if (isOperatorStart(ch)) {
            const int start = i;
            ++i;
            if (i < n) {
                const QChar nxt = line.at(i);
                const QString two = line.mid(start, 2);
                static const QStringList twoCharOps = {
                    "==", "!=", "<=", ">=", "&&", "||", "^^", "!&", "!|",
                    "<<", ">>", "++", "--", "?=", "'="
                };
                if (twoCharOps.contains(two)) {
                    ++i;
                } else if (nxt == '=' || nxt == '&' || nxt == '|' || nxt == '^'
                           || nxt == '<' || nxt == '>' || nxt == '+' || nxt == '-') {
                    // 非标准组合，按单字符处理
                }
            }
            wc.addWord(WordKind::Operator, line.mid(start, i - start));
            continue;
        }

        // 符号
        if (isSymbolChar(ch)) {
            wc.addWord(WordKind::Symbol, QString(ch));
            ++i;
            continue;
        }

        // 其它：原样保留（如 { } 等）
        wc.addWord(WordKind::Term, QString(ch));
        ++i;
    }

    return wc;
}

// ---------------------------------------------------------------------------
// splitAssignment —— 顶层赋值切分（原 ErbLoader::extractInstruction 的赋值分支）
// ---------------------------------------------------------------------------
bool AstBuilder::splitAssignment(const QString& line, QString& lhs, QString& op, QString& rhs) {
    int depth = 0;
    QChar quote;
    const int n = line.length();

    for (int i = 0; i < n; ++i) {
        const QChar ch = line.at(i);

        if (!quote.isNull()) {
            if (ch == quote) quote = QChar();
            continue;
        }
        if (ch == '"') { quote = ch; continue; }

        // 字符串赋值运算符 '=
        if (ch == '\'') {
            if (i + 1 < n && line.at(i + 1) == '=') {
                lhs = line.left(i).trimmed();
                op = QStringLiteral("'=");
                rhs = line.mid(i + 2).trimmed();
                for (const QChar c : lhs) {
                    if (c.isSpace()) return false;
                }
                return !lhs.isEmpty();
            }
            quote = ch;
            continue;
        }

        if (ch == '(' || ch == '[' || ch == '{') { ++depth; continue; }
        if (ch == ')' || ch == ']' || ch == '}') { if (depth > 0) --depth; continue; }
        if (depth != 0 || ch != '=') continue;

        const QChar next = (i + 1 < n) ? line.at(i + 1) : QChar();
        if (next == '=') { ++i; continue; }   // ==
        if (i > 0) {
            const QChar prev = line.at(i - 1);
            if (prev == '!' || prev == '<' || prev == '>') continue;

            if (prev == '+' || prev == '-' || prev == '*' || prev == '/') {
                lhs = line.left(i - 1).trimmed();
                op = QString(prev) + '=';
                rhs = line.mid(i + 1).trimmed();
            } else {
                lhs = line.left(i).trimmed();
                op = QStringLiteral("=");
                rhs = line.mid(i + 1).trimmed();
            }
        } else {
            continue;
        }

        // LHS 必须是不含空白的单个符号（含空白说明是 IF/PRINT 等指令）
        if (lhs.isEmpty()) return false;
        for (const QChar c : lhs) {
            if (c.isSpace()) return false;
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// splitOperands —— 顶层空白/逗号切分（原 ErbLoader 的 splitTopLevel）
// ---------------------------------------------------------------------------
QStringList AstBuilder::splitOperands(const QString& text, bool splitWhitespace) {
    QStringList tokens;
    QString current;
    int depth = 0;
    QChar quote;
    const int n = text.length();

    auto flush = [&]() {
        const QString t = current.trimmed();
        if (!t.isEmpty()) tokens.append(t);
        current.clear();
    };

    for (int i = 0; i < n; ++i) {
        const QChar ch = text.at(i);

        if (!quote.isNull()) {
            current += ch;
            if (ch == quote) quote = QChar();
            continue;
        }
        if (ch == '"' || ch == '\'') { quote = ch; current += ch; continue; }

        if (ch == '(' || ch == '[' || ch == '{') { ++depth; current += ch; continue; }
        if (ch == ')' || ch == ']' || ch == '}') { if (depth > 0) --depth; current += ch; continue; }

        if (depth == 0 && ((splitWhitespace && ch.isSpace()) || ch == ',')) {
            flush();
            if (ch == ',') tokens.append(QStringLiteral(","));
            continue;
        }
        current += ch;
    }
    flush();
    return tokens;
}

// ---------------------------------------------------------------------------
// build —— 行 -> LogicalLine
// ---------------------------------------------------------------------------
LogicalLine AstBuilder::build(const QString& rawLine,
                              const ScriptPosition& position,
                              const AstResolver& resolve) {
    LogicalLine line;
    line.raw = rawLine;
    line.position = position;

    // ';' 之后是注释（字符串内除外）
    const QString trimmed = stripLineComment(rawLine).trimmed();

    // 空行 / 注释（C# NullLine）
    if (trimmed.isEmpty() || trimmed.startsWith(';')) {
        line.kind = LineKind::Null;
        return finalized(std::move(line));
    }

    // 预处理指令（C# ParseSharpLine 的输入；此处仅保留）
    if (trimmed.startsWith('#')) {
        line.kind = LineKind::Preprocessor;
        return finalized(std::move(line));
    }

    // 标签行
    if (trimmed.startsWith('@') || trimmed.startsWith('$')) {
        line.kind = trimmed.startsWith('@') ? LineKind::FunctionLabel : LineKind::GotoLabel;
        const QString label = trimmed.mid(1);

        // 标签名 = 首个标识符 token（对齐 C# LexicalAnalyzer：名字不能含
        // 空格 / 全角空格 / 制表符 / ( , [ : = 等符号）
        int cut = label.size();
        for (int i = 0; i < label.size(); ++i) {
            const QChar c = label.at(i);
            if (c.isSpace() || c == QLatin1Char('(') || c == QLatin1Char(',')
                || c == QLatin1Char('[') || c == QLatin1Char(':') || c == QLatin1Char('=')) {
                cut = i;
                break;
            }
        }
        line.labelName = label.left(cut).trimmed();

        // 形参列表：Emuera 支持两种写法（ErbLoader.parseLabel 的 symbol.Type）
        //   @F(A, B)               -> '(' 形式
        //   @F, ARG, ARGS:1, X = 1 -> ',' 形式（eramaker 遗产，eraTW 里大量存在）
        // 每项取顶层 '=' 之前的名字（'=' 之后是默认值）。
        if (line.kind == LineKind::FunctionLabel) {
            QString rest = label.mid(cut).trimmed();
            if (rest.startsWith(QLatin1Char(','))) rest = rest.mid(1).trimmed();
            else if (rest.startsWith(QLatin1Char('('))) {
                const int rp = rest.lastIndexOf(QLatin1Char(')'));
                rest = (rp > 0) ? rest.mid(1, rp - 1) : rest.mid(1);
                rest = rest.trimmed();
            } else {
                rest.clear();
            }
            if (!rest.isEmpty()) {
                for (const QString& part : splitTopLevelComma(rest)) {
                    QString item = part.trimmed();
                    const int eq = topLevelEquals(item);
                    if (eq >= 0) item = item.left(eq).trimmed();
                    if (!item.isEmpty()) line.labelArgs.append(item);
                }
            }
        }
        line.arguments = { Operand(line.labelName) };
        return finalized(std::move(line));
    }

    // 赋值语句（函数名 = 特例：AssignOperator）
    QString lhs, op, rhs;
    if (splitAssignment(trimmed, lhs, op, rhs)) {
        line.kind = LineKind::Instruction;
        line.functionName = op;
        line.assignOperator = op;
        Operand dest(lhs);
        Operand value(rhs);
        if (resolve) value.ast = resolve(rhs);
        line.arguments = { dest, value };
        return finalized(std::move(line));
    }

    // 命令文
    // 构建期临时使用词法 token；不再存入 LogicalLine（省内存：2M 行级别差异巨大）
    const WordCollection wc = tokenize(trimmed);
    if (wc.isEmpty()) {
        line.kind = LineKind::Null;
        return finalized(std::move(line));
    }

    line.kind = LineKind::Instruction;
    const Word& first = wc.words().first();
    line.functionName = first.text.toUpper();

    // CALL 族特例：CALL / CALLFORM / CALLF / TRYCALL* / JUMP* / BEGIN
    // 第一个操作数是**标签名**（可含 %...% 格式串），不是表达式 ——
    // 对齐 C# 的 SP_CALL / SP_CALLFORM / CALLF_Instruction（目标按标签解析）。
    // 若按表达式解析，TRYCALLFORM NAME_%X%_K30(...) 会被误当成函数调用。
    if (isCallFamilyInstruction(line.functionName)) {
        QString rest = trimmed.mid(first.text.length()).trimmed();
        QString funcName = rest;
        QString args;
        const int paren = rest.indexOf('(');
        if (paren >= 0) {
            funcName = rest.left(paren).trimmed();
            const int close = rest.lastIndexOf(')');
            args = (close > paren) ? rest.mid(paren + 1, close - paren - 1) : rest.mid(paren + 1);
        }
        if (funcName.startsWith('@')) funcName = funcName.mid(1);

        line.arguments.append(Operand(funcName));
        if (!args.trimmed().isEmpty()) {
            // 对齐 C# ExpressionParser.ReduceArguments：只按**顶层**逗号切分
            // （CALL F(GET_STR(a, b, c), "x") 是 2 个实参，不是 5 个）
            const QStringList argList = splitTopLevelComma(args);
            for (const QString& a : argList) {
                const QString t = a.trimmed();
                Operand operand(t);
                if (t.length() >= 2 && t.startsWith('"') && t.endsWith('"')) {
                    operand.isString = true;
                    operand.raw = t.mid(1, t.length() - 2);
                } else if (resolve) {
                    operand.ast = resolve(t);
                }
                line.arguments.append(operand);
            }
        }
        return finalized(std::move(line));
    }

    // 纯文本打印指令（PRINT/PRINTL/PRINTC/PRINTSINGLE/PRINTPLAIN…）：
    // 整行剩余部分就是**字面文本**，不做表达式归约（对齐 C# STR_ArgumentBuilder）
    if (classifyPrintArg(line.functionName) == PrintArgMode::Literal) {
        // C# 只吞掉命令名后的**一个**字符（通常是空格），其余原样作为文本
        QString rest = trimmed.mid(first.text.length());
        if (!rest.isEmpty() && rest.at(0).isSpace()) rest = rest.mid(1);
        Operand operand(rest);
        operand.isString = true;      // 字面文本（引号也照样输出，C# 不剥引号）
        line.arguments.append(operand);
        return finalized(std::move(line));
    }

    // 格式化串指令：整行操作数按 StrForm 解析（文本 + {expr}/%expr%）
    if (isStrFormInstruction(line.functionName)) {
        const QString rest = trimmed.mid(first.text.length()).trimmed();
        if (!rest.isEmpty()) {
            Operand operand(rest);
            if (resolve) {
                operand.ast = StrFormParser::parse(rest, resolve);
            }
            line.arguments.append(operand);
        }
        return finalized(std::move(line));
    }

    // 其余指令：按指令规范决定切分方式（对齐 C# ArgumentParser）
    const QString remainder = trimmed.mid(first.text.length());
    int mn = 0, mx = -1;
    const ArgKind argKind = classifyInstructionKind(line.functionName.toStdString(), mn, mx);

    // 单表达式参数（INT/STR/EXPRESSION/FORM_STR）：整行作为一个表达式
    // （C# 也是「取剩余全部词」），避免把 "LINECOUNT + 1" 拆成 3 个 token。
    if (argKind == ArgKind::IntExpression || argKind == ArgKind::StrExpression
        || argKind == ArgKind::Expression || argKind == ArgKind::FormStr) {
        QString rest = remainder.trimmed();
        // 对齐 C# ReduceArguments：逗号是「项」的终结符，行尾多余的逗号被忽略
        // （eraTW 中确有 `SIF <cond>,` 这种写法）
        while (rest.endsWith(QLatin1Char(','))) {
            rest.chop(1);
            rest = rest.trimmed();
        }
        if (!rest.isEmpty()) {
            Operand operand(rest);
            if (rest.length() >= 2 && rest.startsWith('"') && rest.endsWith('"')) {
                operand.isString = true;
                operand.raw = rest.mid(1, rest.length() - 2);
            } else if (argKind == ArgKind::FormStr) {
                // FORM_STR：整行是格式化串（文本 + {…}/%…%），不是表达式。
                // 否则 DATAFORM 開玩笑也要有个限度啊！(在心里) 会被当成函数调用。
                Operand form(rest);
                if (resolve) form.ast = StrFormParser::parse(rest, resolve);
                line.arguments.append(form);
                return finalized(std::move(line));
            } else if (rest.startsWith('%') || rest.startsWith('$')) {
                operand.isVariable = true;
            } else if (resolve) {
                operand.ast = resolve(rest);
            }
            line.arguments.append(operand);
            // 条件指令：整行归约为条件表达式（强类型校验用）
            if (isConditionInstruction(line.functionName) && resolve) {
                line.condition = resolve(rest);
            }
        }
        return finalized(std::move(line));
    }

    // 逗号族（CALL/ForNext/Case/Expressions）：仅按顶层逗号切分；Raw：空白+逗号
    const QStringList tokens = splitOperands(remainder, argKind == ArgKind::Raw);
    for (const QString& token : tokens) {
        Operand operand(token);
        if (token.length() >= 2 && token.startsWith('"') && token.endsWith('"')) {
            operand.isString = true;
            operand.raw = token.mid(1, token.length() - 2);
        } else if (token.startsWith('%') || token.startsWith('$')) {
            operand.isVariable = true;
        } else if (token != QLatin1String(",") && token != QLatin1String(":")) {
            if (resolve) operand.ast = resolve(token);
        }
        line.arguments.append(operand);
    }

    // 条件指令：用「原始整行文本」作为条件表达式归约（保留引号等字面量）
    if (isConditionInstruction(line.functionName)) {
        const QString cond = remainder.trimmed();
        if (resolve && !cond.isEmpty()) line.condition = resolve(cond);
    }

    return finalized(std::move(line));
}
