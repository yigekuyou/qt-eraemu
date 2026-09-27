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
#ifndef ERA_ENCODING_PROBE_H
#define ERA_ENCODING_PROBE_H

#include <QHash>
#include <QString>
#include <QStringList>

#include "text_encoding.h"

// ---------------------------------------------------------------------------
// EncodingProbe —— 探测「这个游戏（ROM）用的是什么编码」
//
// 只做一件事：把目录里的文本文件逐个按「**能不能严格解码**」来判定，
// 统计每个编码命中了多少文件/字节，取命中最多者。没有评分、没有置信度阈值。
//
// 为什么够用：同一份游戏里的文本几乎总是同一种编码（日文原版 = CP932、
// 简中 = GBK/GB18030、繁中 = Big5、韩文 = EUC-KR、汉化版 = UTF-8），
// 而「严格解码」对**非本编码**的字节流失败率很高，投票即可收敛。
// 唯一的固有歧义是 GBK/Big5 与 CP932 的字节范围重叠（0xA1-0xDF 在 CP932 里是
// 半角片假名）—— 这种样本两种编码都能解码，按优先级取 CP932（日文 era 游戏占多数），
// 需要中文/韩文时用 `内部で使用する東アジア言語` / `TextEncoding` 显式声明。
//
// 结果有两个用途：
//   1. 报告给前端（`summary()`）；
//   2. 配置里什么都没写时，作为嗅探的回退编码（见 EraEngine::resolveTextConfig）。
// ---------------------------------------------------------------------------
struct EncodingProbeCount {
    TextEncoding encoding = TextEncoding::Auto;
    int files = 0;
    qint64 bytes = 0;      // 该编码命中的「非 ASCII 字节数」
};

struct EncodingProbeResult {
    bool ran = false;
    TextEncoding dominant = TextEncoding::Utf8;   // 命中文件最多的编码
    qint64 informativeBytes = 0;
    int filesScanned = 0;
    int filesInformative = 0;                     // 含非 ASCII 的文件
    QList<EncodingProbeCount> counts;             // 按文件数降序
    QStringList samples;                          // 少量样例："编码 路径"

    [[nodiscard]] QString dominantName() const {
        return QString::fromLatin1(TextCodecUtil::name(dominant));
    }
    [[nodiscard]] QString summary() const;
};

namespace EncodingProbe {

// 候选顺序（固定优先级；同时能解码时取靠前的）
[[nodiscard]] const QList<TextEncoding>& candidateOrder();
[[nodiscard]] QStringList defaultSuffixes();
[[nodiscard]] bool hasNonAscii(const QByteArray& data);

// 单样本判定：在候选里挑第一个「能严格解码」的（都不行返回 Latin1）
[[nodiscard]] TextEncoding classify(const QByteArray& sample);

// 探测给定目录（可多个）
[[nodiscard]] EncodingProbeResult probe(const QStringList& dirs,
                                       const QStringList& suffixes = defaultSuffixes(),
                                       bool recursive = true,
                                       int maxFiles = 400,
                                       int sampleSize = 65536);

} // namespace EncodingProbe

#endif // ERA_ENCODING_PROBE_H
