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
#include "strform_parser.h"

#include <QDebug>

int StrFormParser::expressionSpanEnd(const QString& text, int start) {
    if (start >= text.size()) return start;
    const QChar ch = text[start];
    if (ch == '\\' && text.mid(start, 2) == QLatin1String("\\@")) {
        const int close = text.indexOf(QLatin1String("\\@"), start + 2);
        return close < 0 ? text.size() : close + 2;
    }
    const bool form = ch == '@' && start + 1 < text.size() && text[start + 1] == '"';
    if (!form && ch != '"' && ch != '\'') return start;
    const QChar quote = form ? QChar('"') : ch;
    for (int i = start + (form ? 2 : 1); i < text.size(); ++i) {
        if (text[i] == quote) return i + 1;
        if (form) {
            if (text.mid(i, 2) == QLatin1String("\\@")) {
                i = expressionSpanEnd(text, i) - 1;
                continue;
            }
            if (text[i] == '%' || text[i] == '{') {
                const int end = text[i] == '%' ? findPercentEnd(text, i + 1)
                    : findTopLevel(text, '}', i + 1, -1);
                if (end >= 0) { i = end; continue; }
            }
        }
        if (text[i] == '\\') ++i;
    }
    return text.size();
}

bool StrFormParser::hasForm(const QString& text) {
    if (text.contains(QLatin1String("\\@"))) return true;
    if (text.contains(QLatin1Char('{')) && text.contains(QLatin1Char('}'))) return true;
    // %...% 形式：结束的 % 位于顶层（括号外）即可 —— 对齐 C# AnalyseFormattedString，
    // 引号在格式串里**不是**特殊字符（`"a%X%b"` 里的 %X% 照样展开）
    const int first = text.indexOf(QLatin1Char('%'));
    return first >= 0 && findPercentEnd(text, first + 1) > 0;
}

// `%expr%` 的右端 `%`：跳过表达式中的括号、字符串和 FORM 跨度
// （C# 用 LexEndWith.Percent 单独做词法，引号只在表达式内部有含义）
//
// [qdbug] 修复（eraTW 实测差距）：此前不知道 \@...\@（条件三元）跨度，
// `%\@ cond ? A # %ARGS% \@%` 的内层 %ARGS% 的 % 被当成顶层结束符，
// 三元的右分支被截断、变量名落成字面文本 —— MOBGIRL_GENERATOR.ERB:145
// `ARGS = %\@ ARGS == "販売員" ? 因幡 # %ARGS% \@%` 被求值成 "ARGS"
// 并写进 ARGS:0，下一行 `CSTR:ARG:路人子種族 = %ARGS%` 再读出毒化值，
// 路人子素質栏显示「種族：[…][ARGS]」。对齐 C# LexicalAnalyzer：
// AnalyseFormattedString 的 \@ 跨度先于 % 词法，右分支完整保留。
int StrFormParser::findPercentEnd(const QString& text, int from) {
    return findTopLevel(text, '%', from, -1);
}

int StrFormParser::findTopLevel(const QString& text, QChar c, int from, int end) {
    if (end < 0 || end > text.length()) end = text.length();
    int depth = 0;
    for (int i = from; i < end; ++i) {
        const QChar ch = text.at(i);
        const int span = expressionSpanEnd(text, i);
        if (span > i) { i = span - 1; continue; }
        if (depth == 0 && ch == c) return i;
        if (ch == '(' || ch == '{' || ch == '[') { ++depth; continue; }
        if (ch == ')' || ch == '}' || ch == ']') { if (depth > 0) --depth; }
    }
    return -1;
}

namespace {

// 文本片段收集（供 parse / parseYenAt 复用）
struct PartBuilder {
    QList<StrFormPart> parts;
    QString pending;

    void flush() {
        if (!pending.isEmpty()) {
            parts.append(StrFormPart::makeText(pending));
            pending.clear();
        }
    }
    void addExpr(QSharedPointer<ExpressionNode> e) {
        flush();
        if (!e) e = QSharedPointer<LiteralNode>::create(0);
        parts.append(StrFormPart::makeExpr(e));
    }
    void trimStart() {
        while (!pending.isEmpty() && (pending.at(0) == QLatin1Char(' ') || pending.at(0) == QLatin1Char('\t'))) {
            pending.remove(0, 1);
        }
    }
    void trimEnd() {
        while (!pending.isEmpty()
               && (pending.at(pending.size() - 1) == QLatin1Char(' ')
                   || pending.at(pending.size() - 1) == QLatin1Char('\t'))) {
            pending.chop(1);
        }
    }
};

} // namespace

QSharedPointer<ExpressionNode> StrFormParser::parseYenAt(const QString& inner,
                                                         const ExprResolver& resolve) {
    // \@ cond ? left # right \@
    const int q = findTopLevel(inner, QLatin1Char('?'), 0, -1);
    if (q < 0) {
        // 缺少 '?'：C# 视为错误；这里退化为普通文本串
        return parse(inner, resolve);
    }
    // Branches are FORM text: quotes and parentheses are literal. Only
    // interpolation spans hide a branch separator.
    int h = -1;
    for (int i = q + 1; i < inner.size(); ++i) {
        if (inner[i] == '#') { h = i; break; }
        if (inner[i] == '%' || inner[i] == '{') {
            const int end = inner[i] == '%' ? findPercentEnd(inner, i + 1)
                : findTopLevel(inner, '}', i + 1, -1);
            if (end >= 0) i = end;
        } else if (inner[i] == '\\') ++i;
    }
    if (h < 0) {
        // 对齐 C# LexicalAnalyzer.cs:1287-1293 -> StrForm.cs:103-104：
        // `\@ cond ? left \@` 缺 `#` **只产生警告**，假值按空串处理
        // （表达式.md 已注明这与「`#` 不能省略」的表述有出入）。
        qWarning() << "[parse] \\@…?…\\@ 缺少 '#'：假值按空串处理：" << inner.left(120);
    }

    const QString condText = inner.left(q).trimmed();
    QString leftText = (h >= 0) ? inner.mid(q + 1, h - q - 1) : inner.mid(q + 1);
    QString rightText = (h >= 0) ? inner.mid(h + 1) : QString();

    // C# AnalyseFormattedString(trim:true)：左侧 TrimStart、右侧 TrimEnd
    // C# trims only ASCII space/tab, preserving full-width layout spaces.
    const auto trimBranch = [](QString text) {
        int begin = 0, end = text.size();
        while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
        while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
        return text.mid(begin, end - begin);
    };
    leftText = trimBranch(leftText);
    rightText = trimBranch(rightText);

    QSharedPointer<ExpressionNode> cond = resolve ? resolve(condText) : nullptr;
    if (!cond) cond = QSharedPointer<LiteralNode>::create(0);

    // 空分支也要是「空字符串」而不是 nullptr（求值器会直接解引用分支）
    const auto branch = [&resolve](const QString& t) -> QSharedPointer<ExpressionNode> {
        if (t.isEmpty()) {
            QList<StrFormPart> parts;
            parts.append(StrFormPart::makeText(QString()));
            return QSharedPointer<StrFormNode>::create(parts);
        }
        return QSharedPointer<ExpressionNode>(parse(t, resolve));
    };
    QSharedPointer<ExpressionNode> left = branch(leftText);
    QSharedPointer<ExpressionNode> right = branch(rightText);

    return QSharedPointer<IfNode>::create(cond, left, right);
}

QSharedPointer<StrFormNode> StrFormParser::parse(const QString& text, const ExprResolver& resolve) {
    PartBuilder b;
    const int n = text.length();

    const auto addExpr = [&](const QString& inner) {
        const int comma = findTopLevel(inner, QLatin1Char(','), 0, -1);
        const QString value = comma < 0 ? inner : inner.left(comma);
        QSharedPointer<ExpressionNode> expr = resolve ? resolve(value.trimmed()) : nullptr;
        if (!expr) expr = QSharedPointer<LiteralNode>::create(0);
        b.addExpr(expr);
        if (comma >= 0) {
            const int second = findTopLevel(inner, QLatin1Char(','), comma + 1, -1);
            const QString width = second < 0 ? inner.mid(comma + 1) : inner.mid(comma + 1, second-comma-1);
            b.parts.last().width = resolve ? resolve(width.trimmed()) : nullptr;
            b.parts.last().leftAlign = second >= 0 && inner.mid(second+1).trimmed().compare("LEFT", Qt::CaseInsensitive) == 0;
        }
    };

    for (int i = 0; i < n; ++i) {
        const QChar ch = text.at(i);

        // \@ cond ? A # B \@ —— 条件三元（对齐 C# AnalyseYenAt）
        if (ch == QLatin1Char('\\') && i + 1 < n && text.at(i + 1) == QLatin1Char('@')) {
            int j = i + 2;
            int end = -1;
            while (j + 1 < n) {
                if (text.at(j) == QLatin1Char('\\') && text.at(j + 1) == QLatin1Char('@')) { end = j; break; }
                ++j;
            }
            if (end < 0) { b.pending += ch; continue; }   // 未闭合 -> 普通文本
            b.addExpr(parseYenAt(text.mid(i + 2, end - i - 2), resolve));
            i = end + 1;
            continue;
        }

        if (ch == QLatin1Char('{')) {
            const int j = findTopLevel(text, '}', i + 1, -1);
            if (j < 0 || j >= n) { b.pending += ch; continue; }   // 未闭合 -> 普通文本
            addExpr(text.mid(i + 1, j - i - 1));
            i = j;
            continue;
        }

        if (ch == QLatin1Char('%')) {
            const int j = findPercentEnd(text, i + 1);
            if (j < 0) { b.pending += ch; continue; }    // 无成对 % -> 普通文本
            addExpr(text.mid(i + 1, j - i - 1));
            i = j;
            continue;
        }

        // 转义（对齐 C# AnalyseFormattedString 的 `\` 分支）：
        //   \s -> 半角空格   \S -> 全角空格   \t -> TAB   \n -> 换行
        //   \@ -> 条件式（上面已处理）        其它 -> 直接取该字符
        if (ch == QLatin1Char('\\') && i + 1 < n) {
            const QChar esc = text.at(i + 1);
            if (esc == QLatin1Char('s'))      b.pending += QLatin1Char(' ');
            else if (esc == QLatin1Char('S')) b.pending += QChar(0x3000);
            else if (esc == QLatin1Char('t')) b.pending += QLatin1Char('\t');
            else if (esc == QLatin1Char('n')) b.pending += QLatin1Char('\n');
            else                              b.pending += esc;
            ++i;
            continue;
        }

        // 引号在格式串里**只是普通字符**（C# 仅在 @"…" 上下文才把 " 当终止符）；
        // 所以 `PRINTFORML "分数={S}"` 的 {} 照样展开，引号也照样输出。
        b.pending += ch;
    }

    if (!b.pending.isEmpty()) {
        b.parts.append(StrFormPart::makeText(b.pending));
    }
    if (b.parts.isEmpty()) {
        b.parts.append(StrFormPart::makeText(QString()));
    }

    return QSharedPointer<StrFormNode>::create(b.parts);
}
