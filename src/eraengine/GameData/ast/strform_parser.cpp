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

bool StrFormParser::hasForm(const QString& text) {
    if (text.contains(QLatin1String("\\@"))) return true;
    if (text.contains(QLatin1Char('{')) && text.contains(QLatin1Char('}'))) return true;
    // %...% 形式：结束的 % 必须位于顶层（括号外），避免把 "100%" 或取模当成格式串
    const int first = text.indexOf(QLatin1Char('%'));
    return first >= 0 && findTopLevel(text, QLatin1Char('%'), first + 1, -1) > 0;
}

int StrFormParser::findTopLevel(const QString& text, QChar c, int from, int end) {
    if (end < 0 || end > text.length()) end = text.length();
    int depth = 0;
    QChar quote;
    for (int i = from; i < end; ++i) {
        const QChar ch = text.at(i);
        if (!quote.isNull()) {
            if (ch == QLatin1Char('\\')) { ++i; continue; }
            if (ch == quote) quote = QChar();
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) { quote = ch; continue; }
        if (ch == QLatin1Char('(') || ch == QLatin1Char('{') || ch == QLatin1Char('[')) { ++depth; continue; }
        if (ch == QLatin1Char(')') || ch == QLatin1Char('}') || ch == QLatin1Char(']')) { if (depth > 0) --depth; continue; }
        if (depth == 0 && ch == c) return i;
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
    const int h = findTopLevel(inner, QLatin1Char('#'), q + 1, -1);

    const QString condText = inner.left(q).trimmed();
    QString leftText = (h >= 0) ? inner.mid(q + 1, h - q - 1) : inner.mid(q + 1);
    QString rightText = (h >= 0) ? inner.mid(h + 1) : QString();

    // C# AnalyseFormattedString(trim:true)：左侧 TrimStart、右侧 TrimEnd
    leftText = leftText.trimmed();
    rightText = rightText.trimmed();

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
        QSharedPointer<ExpressionNode> expr = resolve ? resolve(inner.trimmed()) : nullptr;
        if (!expr) expr = QSharedPointer<LiteralNode>::create(0);
        b.addExpr(expr);
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
            // 匹配 '}'（允许嵌套）
            int depth = 1;
            int j = i + 1;
            for (; j < n; ++j) {
                if (text.at(j) == QLatin1Char('{')) ++depth;
                else if (text.at(j) == QLatin1Char('}')) { if (--depth == 0) break; }
            }
            if (j >= n) { b.pending += ch; continue; }   // 未闭合 -> 普通文本
            addExpr(text.mid(i + 1, j - i - 1));
            i = j;
            continue;
        }

        if (ch == QLatin1Char('%')) {
            const int j = findTopLevel(text, QLatin1Char('%'), i + 1, -1);
            if (j < 0) { b.pending += ch; continue; }    // 无成对 % -> 普通文本
            addExpr(text.mid(i + 1, j - i - 1));
            i = j;
            continue;
        }

        // 带引号字符串字面量：按普通文本处理（剥掉引号，处理转义），
        // 对齐 C# LexicalAnalyzer 把 LiteralStringWord 归入 StrForm 文本的做法。
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            const QChar quote = ch;
            int j = i + 1;
            for (; j < n && text.at(j) != quote; ++j) {
                if (text.at(j) == QLatin1Char('\\') && j + 1 < n) {
                    const QChar esc = text.at(j + 1);
                    switch (esc.toLatin1()) {
                    case 'n': b.pending += QLatin1Char('\n'); break;
                    case 't': b.pending += QLatin1Char('\t'); break;
                    case 's': b.pending += QLatin1Char(' ');  break;
                    default:  b.pending += esc;               break;
                    }
                    ++j;
                } else {
                    b.pending += text.at(j);
                }
            }
            if (j >= n) { b.pending += quote; continue; }  // 未闭合
            i = j;                                         // 跳过闭合引号
            continue;
        }

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
