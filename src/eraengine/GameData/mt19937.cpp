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
#include "mt19937.h"
#include "eraengine_log.h"

#include <QDebug>
#include <QRandomGenerator>

namespace {
constexpr quint32 kLowerMask = 0x7FFFFFFFu;   // 最低 31 位
constexpr quint32 kUpperMask = 0x80000000u;   // 最高位
}

Mt19937::Mt19937()
    : Mt19937(randomSeed())
{
}

Mt19937::Mt19937(quint32 seed)
{
    reseed(seed);
}

quint32 Mt19937::randomSeed() {
    // 启动时的随机种子（对齐 C# 用 Environment.TickCount 的做法，
    // 但用系统随机源，避免同一毫秒启动得到同一个序列）
    quint32 s = QRandomGenerator::system()->generate();
    if (s == 0) s = 1;                        // 0 也是合法种子，但避开退化情况
    return s;
}

void Mt19937::reseed(quint32 seed) {
    // 注意：**默认构造**也会走到这里（引擎里每个临时 ExpressionEvaluator 都会
    // 构造一个 Mt19937），所以只留跟踪级日志，避免刷屏。
    qCDebug(eraTrace) << "[var] MT19937 重置种子:" << seed;
    m_seed = seed;
    m_state[0] = seed;
    for (int i = 1; i < N; ++i) {
        // mt[i] = 1812433253 * (mt[i-1] ^ (mt[i-1] >> 30)) + i
        m_state[i] = 1812433253u * (m_state[i - 1] ^ (m_state[i - 1] >> 30))
                     + static_cast<quint32>(i);
    }
    m_index = N;                              // 下一次取数触发 generate()
}

void Mt19937::generate() {
    for (int i = 0; i < N; ++i) {
        const quint32 y = (m_state[i] & kUpperMask)
                          | (m_state[(i + 1) % N] & kLowerMask);
        m_state[i] = m_state[(i + M) % N] ^ (y >> 1);
        if (y & 1u) {
            m_state[i] ^= 0x9908B0DFu;
        }
    }
    m_index = 0;
}

quint32 Mt19937::nextU32() {
    if (m_index >= N) {
        generate();
    }
    quint32 y = m_state[m_index++];
    // 淬炼（tempering）
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9D2C5680u;
    y ^= (y << 15) & 0xEFC60000u;
    y ^= (y >> 18);
    return y;
}

// 对齐 C# MTRandom::NextUInt64：先取高 32 位再取低 32 位
quint64 Mt19937::nextU64() {
    const quint64 hi = nextU32();
    const quint64 lo = nextU32();
    return (hi << 32) + lo;
}

qint64 Mt19937::nextInt(qint64 max) {
    if (max <= 0) return 0;
    return static_cast<qint64>(nextU64() % static_cast<quint64>(max));
}

qint64 Mt19937::range(qint64 minValue, qint64 maxValue) {
    if (maxValue < minValue) return minValue;
    return minValue + nextInt(maxValue - minValue + 1);
}

QList<qint64> Mt19937::state() const {
    QList<qint64> out;
    out.reserve(StateLength);
    for (int i = 0; i < N; ++i) out.append(static_cast<qint64>(m_state[i]));
    out.append(static_cast<qint64>(m_index));
    return out;
}

bool Mt19937::setState(const QList<qint64>& state) {
    if (state.size() != StateLength) return false;
    for (int i = 0; i < N; ++i) m_state[i] = static_cast<quint32>(state.at(i));
    m_index = static_cast<int>(state.at(N));
    if (m_index < 0 || m_index > N) m_index = N;   // 越界 -> 立即重生成
    return true;
}
