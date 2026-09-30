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
#include "encoding_probe.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace EncodingProbe {

const QList<TextEncoding>& candidateOrder() {
    // 固定优先级：日文 era 游戏占绝对多数，故 CP932 在最前；
    // 其次 GB18030（GBK/GB2312 超集）、Big5、EUC-KR。
    static const QList<TextEncoding> order = {
        TextEncoding::ShiftJis,
        TextEncoding::Gbk,
        TextEncoding::Big5,
        TextEncoding::EucKr,
    };
    return order;
}

QStringList defaultSuffixes() {
    return {QStringLiteral("erb"), QStringLiteral("erh"), QStringLiteral("csv"),
            QStringLiteral("config")};
}

bool hasNonAscii(const QByteArray& data) {
    for (int i = 0; i < data.size(); ++i) {
        if (static_cast<quint8>(data.at(i)) >= 0x80) return true;
    }
    return false;
}

TextEncoding classify(const QByteArray& sample) {
    if (sample.isEmpty() || !hasNonAscii(sample)) return TextEncoding::Utf8;

    // BOM / 标准编码：Qt 官方嗅探给确定结论
    const TextEncoding sniffed = TextCodecUtil::detect(sample);
    if (sniffed != TextEncoding::Latin1 && sniffed != TextEncoding::ShiftJis
        && sniffed != TextEncoding::Gbk && sniffed != TextEncoding::Big5
        && sniffed != TextEncoding::EucKr) {
        return sniffed;
    }

    // 逐个候选试「能不能严格解码」，取优先级最高的那个
    for (const TextEncoding c : candidateOrder()) {
        if (!TextCodecUtil::canDecode(c)) continue;
        if (TextCodecUtil::isValidInEncoding(sample, c)) return c;
    }
    return TextEncoding::Latin1;
}

EncodingProbeResult probe(const QStringList& dirs, const QStringList& suffixes,
                         bool recursive, int maxFiles, int sampleSize) {
    EncodingProbeResult result;
    result.ran = true;

    QHash<int, EncodingProbeCount> byEncoding;
    int scanned = 0;
    for (const QString& dir : dirs) {
        if (dir.isEmpty() || !QDir(dir).exists()) continue;
        const QDirIterator::IteratorFlags flags = recursive ? QDirIterator::Subdirectories
                                                           : QDirIterator::NoIteratorFlags;
        QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot, flags);
        while (it.hasNext()) {
            const QString path = it.next();
            if (scanned >= maxFiles) break;
            if (!suffixes.contains(QFileInfo(path).suffix().toLower())) continue;
            ++scanned;

            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray sample = f.read(sampleSize);
            f.close();
            if (!hasNonAscii(sample)) continue;   // 纯 ASCII 不投票

            const TextEncoding enc = classify(sample);
            const qint64 informative = std::count_if(
                sample.cbegin(), sample.cend(),
                [](char c) { return static_cast<quint8>(c) >= 0x80; });
            EncodingProbeCount& cnt = byEncoding[static_cast<int>(enc)];
            cnt.encoding = enc;
            cnt.files += 1;
            cnt.bytes += informative;

            ++result.filesInformative;
            result.informativeBytes += informative;
            if (result.samples.size() < 8) {
                result.samples.append(QStringLiteral("%1  %2")
                                          .arg(QString::fromLatin1(TextCodecUtil::name(enc)), path));
            }
        }
    }
    result.filesScanned = scanned;

    for (auto it = byEncoding.constBegin(); it != byEncoding.constEnd(); ++it) {
        result.counts.append(it.value());
    }
    std::sort(result.counts.begin(), result.counts.end(),
              [](const EncodingProbeCount& a, const EncodingProbeCount& b) {
                  if (a.files != b.files) return a.files > b.files;
                  return a.bytes > b.bytes;
              });

    if (!result.counts.isEmpty()) {
        result.dominant = result.counts.first().encoding;
    } else {
        result.dominant = TextEncoding::Utf8;   // 全是 ASCII
    }
    // 回退编码探测结论：编码选错会让整个游戏文本变乱码，值得留痕
    qDebug() << "[load] 编码探测：扫描" << result.filesScanned
             << "文件，主导编码" << TextCodecUtil::name(result.dominant)
             << "，候选" << result.counts.size() << "种";
    return result;
}

} // namespace EncodingProbe

QString EncodingProbeResult::summary() const {
    if (!ran) return QStringLiteral("（未探测）");
    QStringList lines;
    lines << QStringLiteral("探测文件 %1 个（含非 ASCII 的 %2 个，非 ASCII 字节 %3）")
                 .arg(filesScanned)
                 .arg(filesInformative)
                 .arg(informativeBytes);
    if (counts.isEmpty()) {
        lines << QStringLiteral("结论: 纯 ASCII（按 UTF-8 处理）");
    } else {
        for (const EncodingProbeCount& c : counts) {
            lines << QStringLiteral("  %1 : %2 文件 / %3 字节")
                         .arg(QString::fromLatin1(TextCodecUtil::name(c.encoding)))
                         .arg(c.files)
                         .arg(c.bytes);
        }
        lines << QStringLiteral("结论: %1").arg(dominantName());
    }
    return lines.join(QLatin1Char('\n'));
}
