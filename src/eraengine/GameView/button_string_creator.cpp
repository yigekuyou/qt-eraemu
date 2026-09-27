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
#include "button_string_creator.h"

#include <QRegularExpression>
#include <QStringList>

namespace {

// C# LexicalAnalyzer.IsWhiteSpace：半角空格 / 制表符 / 全角空格
inline bool isWhiteSpace(QChar c) {
    return c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QChar(0x3000);
}

// C# 的整数解析：支持 0x / 0b 前缀（LexicalAnalyzer.ReadInt64）
bool readInt64(const QString& raw, qint64* out) {
    QString s = raw.trimmed();
    if (s.isEmpty()) return false;
    bool neg = false;
    if (s.startsWith(QLatin1Char('+'))) s = s.mid(1);
    else if (s.startsWith(QLatin1Char('-'))) { neg = true; s = s.mid(1); }
    if (s.isEmpty()) return false;

    int base = 10;
    if (s.size() > 2 && s.at(0) == QLatin1Char('0')
        && (s.at(1) == QLatin1Char('x') || s.at(1) == QLatin1Char('X'))) {
        base = 16;
        s = s.mid(2);
    } else if (s.size() > 2 && s.at(0) == QLatin1Char('0')
               && (s.at(1) == QLatin1Char('b') || s.at(1) == QLatin1Char('B'))) {
        base = 2;
        s = s.mid(2);
    }
    if (s.isEmpty()) return false;

    bool ok = false;
    const qlonglong v = s.toLongLong(&ok, base);
    if (!ok) return false;
    if (out) *out = neg ? -v : v;
    return true;
}

} // namespace

// C# ButtonStringCreator.numReg
//   @"\[\s*([0][xXbB])?[+-]?[0-9]+([eEpP][0-9]+)?\s*\]"
bool ButtonStringCreator::isNumericBracket(const QString& token) {
    static const QRegularExpression re(
        QStringLiteral(R"(\[\s*([0][xXbB])?[+-]?[0-9]+([eEpP][0-9]+)?\s*\])"));
    return re.match(token).hasMatch();
}

bool ButtonStringCreator::isButtonCore(const QString& token, qint64* input) {
    if (token.size() < 3) return false;
    if (token.at(0) != QLatin1Char('[') || token.at(token.size() - 1) != QLatin1Char(']'))
        return false;
    if (!isNumericBracket(token)) return false;
    const QString inner = token.mid(1, token.size() - 2);
    qint64 v = 0;
    if (!readInt64(inner, &v)) return false;
    if (input) *input = v;
    return true;
}

namespace {

// C# ButtonStringCreator.lex：把文本切成
//   "[1]", " ", "あ", " ", "[2]", " ", "いうえ", " "
// 紧邻的非空白文本合并成一个 token；空白连续段单独成 token。
// 含不配对的 `[` / `]` 返回 false（= C# 的 unanalyzable）。
bool lexTokens(const QString& text, QStringList* out) {
    out->clear();
    int start = 0;
    int state = 0;                 // 0 = 括号外，1 = 括号内
    const auto reduce = [&](int pos) {
        if (pos == start) return;
        out->append(text.mid(start, pos - start));
        start = pos;
    };

    int i = 0;
    const int n = text.size();
    while (i < n) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('[')) {
            if (state == 1) return false;
            reduce(i);
            state = 1;
            ++i;
        } else if (c == QLatin1Char(']')) {
            if (state != 1) return false;
            ++i;
            reduce(i);
            state = 0;
        } else if (state == 0 && isWhiteSpace(c)) {
            reduce(i);
            while (i < n && isWhiteSpace(text.at(i))) ++i;
            reduce(i);
        } else {
            ++i;
        }
    }
    reduce(n);
    return true;
}

} // namespace

QList<ButtonPrimitive> ButtonStringCreator::split(const QString& lineText) {
    QList<ButtonPrimitive> ret;

    const auto single = [&ret](const QString& s, bool canSelect, qint64 input) {
        ButtonPrimitive b;
        b.str = s;
        b.canSelect = canSelect;
        b.input = input;
        ret.append(b);
    };

    if (lineText.isEmpty()) { single(lineText, false, 0); return ret; }
    if (!lineText.contains(QLatin1Char('[')) || !lineText.contains(QLatin1Char(']'))) {
        single(lineText, false, 0);          // C# nonButton
        return ret;
    }

    QStringList strs;
    if (!lexTokens(lineText, &strs)) {
        single(lineText, false, 0);          // C# unanalyzable
        return ret;
    }

    // ---- 第一遍：数「核」的个数，并记录前后是否还有别的文字 ----
    bool beforeButton = false;
    bool afterButton = false;
    int buttonCount = 0;
    qint64 inpL = 0;
    for (const QString& s : strs) {
        if (s.isEmpty()) continue;
        if (isWhiteSpace(s.at(0))) continue;             // ただの空白
        if (isButtonCore(s, &inpL)) {
            ++buttonCount;
            afterButton = false;
        } else {
            afterButton = true;
            if (buttonCount == 0) beforeButton = true;
        }
    }

    if (buttonCount <= 1) {
        // 核が1つ以下 → 一行まるごと1ボタン（核があれば選択可能）
        single(lineText, buttonCount >= 1, inpL);
        return ret;
    }

    // ---- 第二遍：状态机切段（对齐 C# syn 的后半）----
    const bool alignmentRight = !beforeButton && afterButton;   // 说明在按钮右边
    const bool alignmentLeft  = beforeButton && !afterButton;   // 说明在按钮左边
    const bool alignmentEtc   = !alignmentRight && !alignmentLeft;

    bool canSelect = false;
    qint64 input = 0;
    QString buffer;
    const auto reduce = [&]() {
        if (buffer.isEmpty()) return;
        ButtonPrimitive b;
        b.str = buffer;
        b.canSelect = canSelect;
        b.input = input;
        ret.append(b);
        buffer.clear();
        canSelect = false;
        input = 0;
    };

    int state = 0;
    for (const QString& s : strs) {
        if (s.isEmpty()) continue;
        if (isWhiteSpace(s.at(0))) {
            if (((state & 3) == 3) && alignmentEtc && s.size() >= 2) {
                // 核と説明を含んだものが完成していればボタン生成（1文字以下の空白は無視）
                reduce();
                buffer += s;
                state = 0;
            } else {
                buffer += s;
            }
            continue;
        }
        if (isButtonCore(s, &inpL)) {
            ++buttonCount;
            if (((state & 1) == 1) || alignmentRight) {
                reduce();
                buffer += s;
                input = inpL;
                canSelect = true;
                state = 1;
            } else if (alignmentLeft) {
                buffer += s;
                input = inpL;
                canSelect = true;
                reduce();
                state = 0;
            } else {
                buffer += s;
                input = inpL;
                canSelect = true;
                state = 1;
            }
            continue;
        }
        // 説明になりうる文字列
        buffer += s;
        state |= 2;
    }
    reduce();
    return ret;
}

QStringList ButtonStringCreator::splitText(const QString& lineText) {
    QStringList out;
    for (const ButtonPrimitive& b : split(lineText)) out.append(b.str);
    return out;
}
