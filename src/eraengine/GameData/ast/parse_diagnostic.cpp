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
#include "parse_diagnostic.h"
#include <QCoreApplication>

#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <utility>

void ParseDiagnostics::replaceOrigin(DiagOrigin origin, QList<ParseDiagnostic> items) {
    // 盖章点唯一：调用方只给 origin，不逐条改字段。
    for (ParseDiagnostic& d : items) d.origin = origin;

    // 旧切片的**位置**数组（all() 里可能已被 sortForOutput 按位置排开，
    // 例如 pass5 的两条会夹在 pass4/pass6 之间）。新切片逐位覆盖这些位置：
    // 条数相同（重跑幂等的主路径）时序列逐字节不变；多余的新条目接在最后
    // 一个旧位置之后，多余的旧位置删除。首跑（无旧切片）仍是追加到末尾。
    QList<int> positions;
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).origin == origin) positions.append(i);

    QList<ParseDiagnostic> merged;
    merged.reserve(m_items.size() + items.size());
    if (positions.isEmpty()) {
        merged = m_items;
        merged.append(items);
    } else {
        int next = 0;
        const int lastOld = positions.last();
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items.at(i).origin != origin) {
                merged.append(m_items.at(i));
                continue;
            }
            if (next < items.size()) merged.append(items.at(next++));
            if (i == lastOld)
                while (next < items.size()) merged.append(items.at(next++));
        }
    }

    m_items = std::move(merged);
    // 文本视图是派生数据：与 m_items 保持一一对应（texts()/size() 语义不变）。
    m_texts.clear();
    m_texts.reserve(m_items.size());
    for (const ParseDiagnostic& d : m_items) m_texts.append(d.toString());
}

void ParseDiagnostics::addText(DiagSeverity sev, const QString& code, const QString& text) {
    // 既有形态："文件:行: 文本" / "文件:行:列: 文本"；拆出位置便于按位置检索。
    static const QRegularExpression prefix(
        QStringLiteral(R"(^(.+?):(\d+)(?::(\d+))?:\s+(.*)$)"));
    const QRegularExpressionMatch m = prefix.match(text);
    if (m.hasMatch()) {
        add(sev, code, m.captured(1), m.captured(2).toInt(),
            m.captured(3).isEmpty() ? -1 : m.captured(3).toInt(), 0, m.captured(4));
    } else {
        add(sev, code, QString(), text);
    }
}

int ParseDiagnostics::count(DiagSeverity sev) const {
    int n = 0;
    for (const ParseDiagnostic& d : m_items) {
        if (d.severity == sev) ++n;
    }
    return n;
}

QMap<QString, int> ParseDiagnostics::countByCode() const {
    QMap<QString, int> byCode;
    for (const ParseDiagnostic& d : m_items) {
        byCode[d.code.isEmpty() ? QCoreApplication::translate("ParseDiagnostics", "(未分类)") : d.code] += 1;
    }
    return byCode;
}

QString ParseDiagnostics::summarize() const {
    if (m_items.isEmpty()) {
        return QCoreApplication::translate("ParseDiagnostics", "无");
    }
    QStringList head;
    if (const int e = count(DiagSeverity::Error); e > 0) {
        head << QCoreApplication::translate("ParseDiagnostics", "错误 %1").arg(e);
    }
    if (const int w = count(DiagSeverity::Warning); w > 0) {
        head << QCoreApplication::translate("ParseDiagnostics", "警告 %1").arg(w);
    }
    if (const int i = count(DiagSeverity::Info); i > 0) {
        head << QCoreApplication::translate("ParseDiagnostics", "提示 %1").arg(i);
    }

    // 按类计数：数量降序，取前 5 类，其余并入「其它」
    QList<QPair<int, QString>> ranked;
    const QMap<QString, int> byCode = countByCode();
    for (auto it = byCode.constBegin(); it != byCode.constEnd(); ++it) {
        ranked.append({it.value(), it.key()});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
    });
    QStringList cats;
    int shown = 0;
    int rest = 0;
    for (const auto& r : ranked) {
        if (shown < 5) {
            cats << QStringLiteral("%1 %2").arg(r.second).arg(r.first);
            ++shown;
        } else {
            rest += r.first;
        }
    }
    if (rest > 0) cats << QCoreApplication::translate("ParseDiagnostics", "其它 %1").arg(rest);

    return QCoreApplication::translate("ParseDiagnostics", "%1（%2）").arg(head.join(QStringLiteral(" / ")),
                                          cats.join(QStringLiteral(" / ")));
}

#include "expression_lexer.h"

void diagnoseIntegerLiterals(const QString& expression, const QString& file, int line,
                             int column, ParseDiagnostics& diagnostics) {
    ExpressionLexer lexer;
    for (const auto& token : lexer.tokenize(expression)) {
        if (token.type() == TokenType::STRFORM_AT || token.type() == TokenType::YEN_AT) {
            // FORM 文本本身不是数值表达式；只在插值 resolver 中检查数字。
            continue;
        }
        if (token.type() != TokenType::NUMBER || token.integerValid()) continue;
        const int tokenColumn = column + token.column() - token.value().size();
        bool alreadyReported = false;
        for (const auto& d : diagnostics.all()) {
            if (d.code == QLatin1String(DiagCode::kNumericConversion) && d.file == file
                && d.line == line && d.column == tokenColumn) { alreadyReported = true; break; }
        }
        if (alreadyReported) continue;
        diagnostics.add(DiagSeverity::Error, DiagCode::kNumericConversion, file, line,
            tokenColumn, token.value().size(),
            QCoreApplication::translate("ParseDiagnostics", "整数转换失败（格式错误或超出 64 位整数表示范围）：%1")
                .arg(token.value()), expression);
    }
}
