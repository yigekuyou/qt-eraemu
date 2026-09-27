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
// ---------------------------------------------------------------------------
// text_convert —— 把游戏目录里的文本文件规范化成 UTF-8（默认 UTF-8+BOM）
//
// 为什么需要：era 游戏（尤其日文原版）的 .ERB/.ERH/.CSV/*.config 默认是
// SHIFT-JIS（Windows 的 ANSI 代码页），而 Linux/macOS 与 Qt/QML 侧统一走 UTF-8。
// 引擎本身能按文件嗅探（见 Content/encoding/text_encoding.*），这个工具用于
// 「一次性把整棵树转成 UTF-8」，之后所有工具链都只面对 UTF-8。
//
// 用法:
//   text_convert <目录|文件> [选项]
//     --write            真正写回（缺省为 dry-run，只报告）
//     --backup           写回前把原文件另存为 <原名>.bak
//     --encoding NAME    目标编码（默认 UTF-8-BOM；可给 UTF-8 / SHIFT-JIS …）
//     --ext .erb,.csv    处理的后缀（默认 .erb,.erh,.csv,.config,.txt,.md）
//
// 安全保证：写回前会「解码 → 重编码 → 再解码」比对，只有完全一致（或目标编码
// 无法表示而按预期退化为 '?'）才写入；二进制/无法解码的文件一律跳过。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>

#include "text_encoding.h"

namespace {

struct Options {
    QStringList paths;
    bool write = false;
    bool backup = false;
    TextEncoding target = TextEncoding::Utf8Bom;
    QStringList extensions = {QStringLiteral("erb"), QStringLiteral("erh"),
                              QStringLiteral("csv"), QStringLiteral("config"),
                              QStringLiteral("txt"), QStringLiteral("md")};
};

void usage() {
    qWarning().noquote() <<
        "usage: text_convert <dir|file> [--write] [--backup] [--encoding NAME] [--ext .erb,.csv]";
}

bool isTargetFile(const QFileInfo& fi, const Options& opt) {
    return opt.extensions.contains(fi.suffix().toLower());
}

// 收集待处理文件（递归、确定性顺序）
QStringList collect(const QStringList& paths, const Options& opt) {
    QStringList out;
    for (const QString& p : paths) {
        const QFileInfo fi(p);
        if (fi.isFile()) {
            if (isTargetFile(fi, opt)) out << fi.absoluteFilePath();
            continue;
        }
        if (!fi.isDir()) continue;
        QDirIterator it(p, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            if (isTargetFile(QFileInfo(f), opt)) out << f;
        }
    }
    out.sort();
    return out;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    Options opt;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--write")) { opt.write = true; continue; }
        if (a == QLatin1String("--backup")) { opt.backup = true; continue; }
        if (a == QLatin1String("--help") || a == QLatin1String("-h")) { usage(); return 0; }
        if (a == QLatin1String("--encoding") && i + 1 < argc) {
            opt.target = TextCodecUtil::fromName(QString::fromLocal8Bit(argv[++i]));
            if (opt.target == TextEncoding::Auto) {
                qWarning() << "--encoding 无法识别，回退 UTF-8-BOM";
                opt.target = TextEncoding::Utf8Bom;
            }
            continue;
        }
        if (a == QLatin1String("--ext") && i + 1 < argc) {
            opt.extensions.clear();
            for (QString e : QString::fromLocal8Bit(argv[++i]).split(QLatin1Char(','))) {
                e = e.trimmed();
                if (e.startsWith(QLatin1Char('.'))) e.remove(0, 1);
                if (!e.isEmpty()) opt.extensions << e.toLower();
            }
            continue;
        }
        if (a.startsWith(QLatin1Char('-'))) { usage(); return 2; }
        opt.paths << a;
    }
    if (opt.paths.isEmpty()) { usage(); return 2; }

    // 目标编码必须有对应的转换器（Qt 无 ICU 时 GB18030/Big5/EUC-KR 会不可用）
    if (!TextCodecUtil::canEncode(opt.target)) {
        qWarning() << "本机 Qt 不支持目标编码" << TextCodecUtil::name(opt.target)
                   << "（可用编解码器" << TextCodecUtil::availableCodecs().size() << "个）";
        return 3;
    }

    const QStringList files = collect(opt.paths, opt);
    if (files.isEmpty()) {
        qWarning() << "没有匹配的文本文件";
        return 0;
    }

    qDebug().noquote() << QString("模式: %1   目标编码: %2   文件数: %3")
                              .arg(opt.write ? QStringLiteral("写入") : QStringLiteral("dry-run"),
                                   QString::fromLatin1(TextCodecUtil::name(opt.target)))
                              .arg(files.size());

    QHash<QString, int> histogram;   // 源编码 -> 文件数
    int converted = 0, skipped = 0, unchanged = 0;

    for (const QString& path : files) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning() << "  跳过（打不开）:" << path;
            ++skipped;
            continue;
        }
        const QByteArray raw = f.readAll();
        f.close();

        TextEncoding source = TextEncoding::Auto;
        const QString text = TextCodecUtil::decode(raw, TextEncoding::Auto, &source);
        histogram[QString::fromLatin1(TextCodecUtil::name(source))]++;

        bool encodeOk = true;
        const QByteArray target = TextCodecUtil::encode(text, opt.target, &encodeOk);
        if (!encodeOk) {
            qWarning() << "  跳过（目标编码不可用）:" << path;
            ++skipped;
            continue;
        }
        if (target == raw) {   // 已经是目标编码（含 BOM）
            ++unchanged;
            continue;
        }

        // 安全校验：写回的字节必须能解回同一段文本（目标编码无法表示的字符会退化，
        // 因此只在「原始文本完全一致」时写入；否则报告并跳过，避免悄悄改坏内容）
        TextEncoding back = TextEncoding::Auto;
        const QString roundTrip = TextCodecUtil::decode(target, TextEncoding::Auto, &back);
        if (roundTrip != text) {
            qWarning().noquote()
                << QString("  跳过（%1 无法完整表示该文件内容）: %2")
                       .arg(QString::fromLatin1(TextCodecUtil::name(opt.target)), path);
            ++skipped;
            continue;
        }

        ++converted;
        if (!opt.write) {
            qDebug().noquote() << QString("  %1 -> %2  %3")
                                      .arg(QString::fromLatin1(TextCodecUtil::name(source)),
                                           QString::fromLatin1(TextCodecUtil::name(opt.target)),
                                           path);
            continue;
        }

        if (opt.backup) {
            const QString bak = path + QStringLiteral(".bak");
            QFile::remove(bak);
            if (!QFile::rename(path, bak)) {
                qWarning() << "  备份失败（跳过）:" << path;
                --converted;
                ++skipped;
                continue;
            }
        }
        if (!TextCodecUtil::writeFile(path, text, opt.target)) {
            qWarning() << "  写入失败:" << path;
            --converted;
            ++skipped;
            continue;
        }
        qDebug().noquote() << QString("  已转换 %1 -> %2  %3")
                                  .arg(QString::fromLatin1(TextCodecUtil::name(source)),
                                       QString::fromLatin1(TextCodecUtil::name(opt.target)),
                                       path);
    }

    qDebug().noquote() << "\n---- 统计 ----";
    QStringList keys = histogram.keys();
    keys.sort();
    for (const QString& k : keys) {
        qDebug().noquote() << QString("  源编码 %1 : %2 个文件").arg(k).arg(histogram.value(k));
    }
    qDebug().noquote() << QString("  需要转换: %1   已是目标编码: %2   跳过: %3")
                              .arg(converted).arg(unchanged).arg(skipped);
    if (!opt.write) {
        qDebug().noquote() << "  （dry-run：加 --write 才会真正写回）";
    }
    return 0;
}
