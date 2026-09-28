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
#ifndef CONSOLE_PLANE_H
#define CONSOLE_PLANE_H

#include <QString>
#include <QStringList>
#include <QList>
#include "console_types.h"
#include "console_buffer.h"
#include "console_layout.h"

// ---------------------------------------------------------------------------
// ConsolePlane —— 把「分层区块模型」还原成一张二维平面（调试/自检用）
//
// 前提（QML 侧也是这样）：root / text / image / shape 各层**铺满同一个平面**，
// 所以区块的 x/y 在任意层里都等于「相对 root 的绝对坐标」。于是可以：
//
//   * render()  ：按 (x / 列宽) 定列、(y / 行高) 定行，把区块画成字符矩阵
//                 —— 文本照抄，图片用 ▨ 占位，图形用 ─ 占位；
//                 全角字符占 2 列，与 C# 的「显示宽度」一致。
//   * inspect() ：对同一份区块数据做几何检查（越界/重叠/未对齐/尺寸缺失…）。
//
// 这样 test_cli / 单元测试不需要 GUI 就能验证「2 维排版」是否正确。
// ---------------------------------------------------------------------------

struct ConsolePlaneOptions {
    int windowWidth = 760;      // root 宽度（区块越过即判为越界）
    int maxLines    = 0;        // 0 = 不限（打印多少行）
    bool withRuler  = false;    // 加一行列标尺
    // 终端安全：把「东亚宽度有歧义」的符号（□ ■ ▨ ─ ◆ 等，Unicode
    // East-Asian Ambiguous）替换成 **2 个 ASCII 字符**。
    // 理由：模型里这些符号占 2 个单位长（MS ゴシック 下确为全角），
    // 但终端/等宽字体常按 1 格显示 → 平面会「看起来字符长度不同」。
    // 换成 2 个 ASCII 字符以后，任何终端都严格「1 字符 = 1 个区块长」。
    bool terminalSafe = true;
    // 每行末尾附上「本行的单位宽度」（便于核对，不依赖终端字体）
    bool withWidths = false;
    // 调试对比模式：用 [ ] 替换方块字符，便于对比两个输出
    bool debugCompare = false;
    // 调试颜色模式：用 ANSI 颜色表示矩阵位置
    bool debugColor = false;
    // 按 ConsoleSpan.style.color 输出 ANSI 真色；不参与网格宽度计算
    bool ansiColors = false;
    QString imageMark;          // 图片占位符（默认 ▨）
    QString shapeMark;          // 图形占位符（默认 ─）

    ConsolePlaneOptions()
        : imageMark(QChar(0x25A8)), shapeMark(QChar(0x2500)) {}
};

struct ConsolePlaneIssue {
    int     line = -1;          // 显示行下标（缓冲内绝对行号）
    QString message;
};

class ConsolePlane {
public:
    using Options = ConsolePlaneOptions;
    using Issue   = ConsolePlaneIssue;

    // 平面重建：每行一个 QString（列宽 = fontSize/2，全角占 2 列）
    [[nodiscard]] static QStringList render(const ConsoleBuffer& buffer,
                                            const ConsoleLayout& layout,
                                            const Options& opt = ConsolePlaneOptions());

    // 带列标尺的版本（每 10 列一个刻度，便于肉眼对齐）
    [[nodiscard]] static QStringList renderWithRuler(const ConsoleBuffer& buffer,
                                                     const ConsoleLayout& layout,
                                                     const Options& opt = ConsolePlaneOptions());

    // 几何自检（返回可疑项；空 = 没发现问题）
    [[nodiscard]] static QList<Issue> inspect(const ConsoleBuffer& buffer,
                                              const ConsoleLayout& layout,
                                              const Options& opt = ConsolePlaneOptions());

    // 便捷：把 Issue 转成 "第 N 行: …" 文本
    [[nodiscard]] static QStringList issueTexts(const QList<Issue>& issues);

    // 终端安全化：宽度有歧义的符号 → 2 个 ASCII 字符（保证 1 字符 = 1 单位）
    [[nodiscard]] static QString toTerminalSafe(const QString& text);
    // 「宽字符」判定：占 2 个单位长
    [[nodiscard]] static bool isWideChar(QChar c);
    // East-Asian Ambiguous：终端按 1 格显示的可能性很高
    [[nodiscard]] static bool isAmbiguousWidth(QChar c);
};

#endif // CONSOLE_PLANE_H
