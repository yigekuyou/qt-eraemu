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
