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
#include "user_function.h"

#include <QStringView>

namespace {

// 取 "ARG:2" 里的下标；无 ':' 时返回 0；下标不是数字时返回 -1（视为无法归类）
int parseLocalIndex(QStringView text, int from) {
    if (from >= text.size()) return 0;
    if (text.at(from) != QLatin1Char(':')) return -1;
    ++from;
    if (from >= text.size()) return -1;
    int value = 0;
    bool any = false;
    for (; from < text.size(); ++from) {
        const QChar c = text.at(from);
        if (!c.isDigit()) return -1;
        value = value * 10 + c.digitValue();
        any = true;
    }
    return any ? value : -1;
}

} // namespace

UserParamDecl classifyUserParam(const QString& rawName) {
    UserParamDecl p;
    p.name = rawName.trimmed();

    const QString upper = p.name.toUpper();
    if (upper == QLatin1String("ARG") || upper.startsWith(QLatin1String("ARG:"))) {
        const int idx = parseLocalIndex(QStringView(upper), 3);
        p.target = UserParamTarget::Arg;
        p.index = (idx >= 0) ? idx : 0;
        p.type = OperandType::Int;
        p.typeKnown = true;   // ARG 恒为整数局部槽
        return p;
    }
    if (upper == QLatin1String("ARGS") || upper.startsWith(QLatin1String("ARGS:"))) {
        const int idx = parseLocalIndex(QStringView(upper), 4);
        p.target = UserParamTarget::Args;
        p.index = (idx >= 0) ? idx : 0;
        p.type = OperandType::Str;
        p.typeKnown = true;   // ARGS 恒为字符串局部槽
        return p;
    }
    p.target = UserParamTarget::LocalVar;
    p.varName = p.name;
    return p;
}
