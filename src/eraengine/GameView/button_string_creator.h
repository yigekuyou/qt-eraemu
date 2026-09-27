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
#ifndef BUTTON_STRING_CREATOR_H
#define BUTTON_STRING_CREATOR_H

#include <QString>
#include <QList>
#include <QtGlobal>

// ---------------------------------------------------------------------------
// ButtonStringCreator —— 「一行文本 → 若干段」的切分器
//
// 对齐 C# GameView/ButtonStringCreator.cs。规则：
//   * 用正则 \[\s*([0][xXbB])?[+-]?[0-9]+([eEpP][0-9]+)?\s*\] 识别「按钮核」
//     （`[0]` `[+5]` `[ 123 ]` `[0x10]` `[1e2]` 是核；`[abc]` `[]` 不是）；
//   * 整行里**只有一个**核时，整行作为一段（CanSelect = true）——
//     这就是 Emuera「只有一个选项时整行可点」的行为；
//   * 多个核时按空白/核的位置切段：说明文字在核右（alignmentRight）/
//     核左（alignmentLeft）/ 临机（alignmentEtc）；
//   * 含不配对或嵌套的 `[` `]` → 整行退化成一段不可点击文本。
//
// `Str` 是所有段拼接后的原文（切分不改变字符总数），因此调用方可以用
// 「字符长度」把 ConsoleSpan 数组按段边界切开（对齐 C# createButtons）。
// ---------------------------------------------------------------------------
struct ButtonPrimitive {
    QString str;
    qint64  input = 0;
    bool    canSelect = false;
};

class ButtonStringCreator {
public:
    // 是否有「按钮核」（供宽松判定用）
    [[nodiscard]] static bool isButtonCore(const QString& token, qint64* input = nullptr);

    // 判定 `[±?digits]` 形态（允许 0x/0b 前缀、正负号、指数、内部空白）
    [[nodiscard]] static bool isNumericBracket(const QString& token);

    // 主入口：把一整行文本切成段
    [[nodiscard]] static QList<ButtonPrimitive> split(const QString& lineText);

    // 便利：只要段文本
    [[nodiscard]] static QStringList splitText(const QString& lineText);
};

#endif // BUTTON_STRING_CREATOR_H
