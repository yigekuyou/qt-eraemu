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
#ifndef AST_WORD_H
#define AST_WORD_H

#include <QString>
#include <QList>

// ---------------------------------------------------------------------------
// Word / WordCollection —— 词法 token 模型
//
// 对齐 C# Emuera 的 Sub/Word.cs 与 Sub/WordCollection.cs：
//   * LexicalAnalyzer 把一行切成 Word 序列（WordCollection）；
//   * 每个 Word 带一个类别（标识符 / 字面量 / 运算符 / 符号 / 格式化串 / 宏 …）；
//   * LogicalLine 保留整行原始 WordCollection（C# 的 argprimitive）以便二次编译。
//
// 本 C++ 实现只保留「行 -> token」所需的最小信息（类别 + 文本），
// 供 AstBuilder 归类与表达式归约使用。
// ---------------------------------------------------------------------------
enum class WordKind {
    Identifier,   // 变量名 / 指令名 / 函数名
    Number,       // 整数字面量
    String,       // "..." 字符串字面量
    Operator,     // + - * / == 等
    Symbol,       // ( ) [ ] , : 等
    StrForm,      // '...'/格式化串 { ... }（保留，用于后续 StrForm 解析）
    Macro,        // 宏展开结果
    Term          // 无法归类的词（原样保留）
};

struct Word {
    WordKind kind = WordKind::Term;
    QString  text;         // 规范化文本（字符串为去掉引号后的内容）
    QString  raw;          // 原始文本（含引号 / % 等）
    bool     quoted = false;   // 是否来自引号字面量

    Word() = default;
    Word(WordKind k, const QString& t, const QString& r = QString(), bool q = false)
        : kind(k), text(t), raw(r.isEmpty() ? t : r), quoted(q) {}

    bool isOperator() const { return kind == WordKind::Operator; }
    bool isSymbol() const { return kind == WordKind::Symbol; }
    bool isIdentifier() const { return kind == WordKind::Identifier; }
};

// C# WordCollection：一个可前进游标的 Word 序列。
class WordCollection {
public:
    WordCollection() = default;
    explicit WordCollection(QList<Word> words) : m_words(std::move(words)) {}

    void add(const Word& w) { m_words.append(w); }
    void addWord(WordKind k, const QString& text, const QString& raw = QString(), bool quoted = false) {
        m_words.append(Word(k, text, raw, quoted));
    }

    const QList<Word>& words() const { return m_words; }
    int size() const { return m_words.size(); }
    bool isEmpty() const { return m_words.isEmpty(); }
    bool hasNext() const { return m_cursor < m_words.size(); }
    int cursor() const { return m_cursor; }
    void reset() { m_cursor = 0; }

    const Word& peek() const {
        static const Word empty;
        return (m_cursor < m_words.size()) ? m_words.at(m_cursor) : empty;
    }

    Word next() {
        if (m_cursor >= m_words.size()) {
            return Word();
        }
        return m_words.at(m_cursor++);
    }

    // 剩余文本（原始 token 以空格连接），用于把整行还原为可求值表达式。
    QString remainingText() const {
        QString out;
        for (int i = m_cursor; i < m_words.size(); ++i) {
            if (!out.isEmpty()) out += ' ';
            out += m_words.at(i).raw;
        }
        return out;
    }

private:
    QList<Word> m_words;
    int m_cursor = 0;
};

#endif // AST_WORD_H
