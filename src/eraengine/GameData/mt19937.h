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
#ifndef MT19937_H
#define MT19937_H

#include <QList>
#include <QtGlobal>

// ---------------------------------------------------------------------------
// Mt19937 —— 梅森旋转（MT19937）伪随机数发生器
//
// 对齐 C# Emuera 的 `_Library/SFMT.cs`（该文件以 MT19937 编译）：
//   * 状态 = 624 个字 + 1 个下标（= 系统变量 RANDDATA 的 625 项）；
//   * 启动种子：C# 用 `Environment.TickCount`，这里用系统随机源
//     （`randomSeed()`）；
//   * `RANDOMIZE seed` -> reseed(seed)；
//   * 取数：GetNextRand(max) == NextUInt64() % max，
//     而 NextUInt64() = (NextUInt32() << 32) + NextUInt32()
//     —— **一次取数消耗两个 32 位输出**（与 C# 完全一致）。
//
// 有了「启动种子 + 可回溯的种子」就能复现：test_cli 的 `--seed N`
// 可以固定一整局的随机序列，方便调试与回归。
// ---------------------------------------------------------------------------
class Mt19937 {
public:
    static constexpr int N = 624;        // 状态字数
    static constexpr int M = 397;
    static constexpr int StateLength = N + 1;   // RANDDATA 的长度（含下标）

    Mt19937();                            // 启动时随机种子
    explicit Mt19937(quint32 seed);

    // 启动种子：取系统随机源（每次进程启动不同）
    [[nodiscard]] static quint32 randomSeed();

    void reseed(quint32 seed);
    [[nodiscard]] quint32 seedUsed() const { return m_seed; }

    [[nodiscard]] quint32 nextU32();
    [[nodiscard]] quint64 nextU64();
    // [0, max) —— 对齐 C# MTRandom::NextInt64(max)
    [[nodiscard]] qint64 nextInt(qint64 max);
    // RANDOM(a, b) -> [a, b]
    [[nodiscard]] qint64 range(qint64 minValue, qint64 maxValue);

    // RANDDATA（保存/恢复随机状态；长度必须是 StateLength）
    [[nodiscard]] QList<qint64> state() const;
    bool setState(const QList<qint64>& state);

private:
    void generate();

    quint32 m_state[N];
    int     m_index = N + 1;      // N+1 = 需要重新生成
    quint32 m_seed = 0;
};

#endif // MT19937_H
