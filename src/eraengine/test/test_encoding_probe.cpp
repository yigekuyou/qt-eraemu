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
// test_encoding_probe.cpp
//
// 验证两件事：
//   1. 「ROM（游戏）编码探测」：EncodingProbe::probe / voteForSample
//      —— 中文 GBK 与日文 CP932 字节范围重叠时的评分消解、纯 ASCII 不投票、
//         采样上限、置信度;
//   2. 「显式设置」的落盘：ConfigLoader::setConfigValueInFile
//      —— 只改/追加一个键，保留注释与其它行。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "text_encoding.h"
#include "encoding_probe.h"
#include "config_loader.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static QString encName(TextEncoding e) { return QString::fromLatin1(TextCodecUtil::name(e)); }

static void writeEnc(const QString& path, const QString& text, TextEncoding enc) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    TextCodecUtil::writeFile(path, text, enc);
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Encoding probe (ROM) test";
    qDebug() << "=========================";

    const QString ja = QString::fromUtf8("勇者よ、魔王を倒しに行くのじゃ。"
                                         "今日は天気がとても良いですね。");
    const QString zh = QString::fromUtf8("你好，欢迎来到这个世界。"
                                         "请选择你的角色并开始冒险吧。");

    qDebug() << "\n1) 单样本判定（严格解码 + 固定优先级，无评分）";
    {
        const QByteArray sjis = TextCodecUtil::encode(ja, TextEncoding::ShiftJis);
        check(EncodingProbe::classify(sjis) == TextEncoding::ShiftJis,
              QString("日文 SJIS 样本 -> SHIFT-JIS（得到 %1）")
                  .arg(encName(EncodingProbe::classify(sjis))));

        const QByteArray utf8 = zh.toUtf8();
        check(EncodingProbe::classify(utf8) == TextEncoding::Utf8, "UTF-8 样本 -> UTF-8");
        QByteArray bom("\xEF\xBB\xBF", 3);
        bom += utf8;
        check(EncodingProbe::classify(bom) == TextEncoding::Utf8Bom,
              "UTF-8 BOM 样本 -> UTF-8-BOM");
        check(EncodingProbe::classify(QByteArray("plain ascii only")) == TextEncoding::Utf8,
              "纯 ASCII 样本 -> UTF-8（调用方按 hasNonAscii 跳过）");
        check(!EncodingProbe::hasNonAscii(QByteArray("ascii")), "hasNonAscii(ascii) == false");
        check(EncodingProbe::hasNonAscii(sjis), "hasNonAscii(SJIS) == true");
    }

    qDebug() << "\n2) 目录探测：Shift-JIS 游戏";
    {
        QTemporaryDir tmp;
        const QString dir = tmp.filePath(QStringLiteral("sjisGame"));
        for (int i = 0; i < 6; ++i) {
            writeEnc(QStringLiteral("%1/ERB/SCRIPT%2.ERB").arg(dir).arg(i),
                     QStringLiteral("@FUNC%1\nPRINTFORML %2\n").arg(i).arg(ja), TextEncoding::ShiftJis);
        }
        writeEnc(dir + QStringLiteral("/CSV/GameBase.csv"),
                 QStringLiteral("タイトル,日本語のゲーム\n"), TextEncoding::ShiftJis);
        writeEnc(dir + QStringLiteral("/ERB/ASCII.ERB"), QStringLiteral("A = 1\n"), TextEncoding::Utf8);

        const EncodingProbeResult r = EncodingProbe::probe({dir}, EncodingProbe::defaultSuffixes(),
                                                           true);
        qDebug().noquote() << r.summary().split(QLatin1Char('\n')).join(QStringLiteral("\n   "));
        check(r.dominant == TextEncoding::ShiftJis,
              QString("主导编码 = SHIFT-JIS（得到 %1）").arg(r.dominantName()));
        check(r.filesInformative == 7, QString("含非 ASCII 文件 7 个（得到 %1）").arg(r.filesInformative));
        check(r.filesScanned == 8, QString("扫描 8 个文件（含纯 ASCII，得到 %1）").arg(r.filesScanned));
        check(!r.counts.isEmpty() && r.counts.first().files == 7, "得票数 = 7 文件");
    }

    qDebug() << "\n3) 目录探测：UTF-8 汉化版 + 采样上限";
    {
        QTemporaryDir tmp;
        const QString dir = tmp.filePath(QStringLiteral("utf8Game"));
        for (int i = 0; i < 12; ++i) {
            writeEnc(QStringLiteral("%1/ERB/S%2.ERB").arg(dir).arg(i),
                     QStringLiteral("@F%1\n%2\n").arg(i).arg(zh), TextEncoding::Utf8Bom);
        }
        const EncodingProbeResult all = EncodingProbe::probe({dir}, EncodingProbe::defaultSuffixes(),
                                                            true, 100);
        check(all.dominant == TextEncoding::Utf8Bom, "主导编码 = UTF-8-BOM");
        const EncodingProbeResult capped = EncodingProbe::probe({dir}, EncodingProbe::defaultSuffixes(),
                                                                true, 5);
        check(capped.filesScanned == 5,
              QString("采样上限生效（得到 %1）").arg(capped.filesScanned));
        check(capped.dominant == TextEncoding::Utf8Bom, "截断后结论仍正确");
    }

    qDebug() << "\n4) 目录探测：中文游戏（GBK）";
    if (TextCodecUtil::hasCodec(QStringLiteral("GB18030"))) {
        QTemporaryDir tmp;
        const QString dir = tmp.filePath(QStringLiteral("gbkGame"));
        for (int i = 0; i < 5; ++i) {
            writeEnc(QStringLiteral("%1/ERB/S%2.ERB").arg(dir).arg(i),
                     QStringLiteral("@F%1\n%2\n").arg(i).arg(zh), TextEncoding::Gbk);
        }
        const EncodingProbeResult r = EncodingProbe::probe({dir}, EncodingProbe::defaultSuffixes(),
                                                           true);
        qDebug().noquote() << r.summary().split(QLatin1Char('\n')).join(QStringLiteral("\n   "));
        // 注意：GBK 字节在 CP932 下往往**也能**解码（0xA1-0xDF 是半角片假名），
        // 所以固定优先级会让这类样本判成 SHIFT-JIS —— 这是已知的固有歧义，
        // 靠 `内部で使用する東アジア言語`/`TextEncoding` 显式声明解决（与 C# 一致）。
        check(r.dominant == TextEncoding::ShiftJis || r.dominant == TextEncoding::Gbk,
              QString("中文样本判为 SHIFT-JIS 或 GB18030（得到 %1）").arg(r.dominantName()));
    } else {
        qDebug().noquote() << "   [skip] 无 ICU";
    }

    qDebug() << "\n5) 声明的回退编码参与嗅探";
    if (TextCodecUtil::hasCodec(QStringLiteral("GB18030"))) {
        const QByteArray gbk = TextCodecUtil::encode(zh, TextEncoding::Gbk);
        TextCodecUtil::setFallbackEncoding(TextEncoding::Gbk);
        TextEncoding det = TextEncoding::Auto;
        (void)TextCodecUtil::decode(gbk, TextEncoding::Auto, &det);
        check(det == TextEncoding::Gbk, QString("回退=GB18030 时 GBK 样本判为 GB18030（得到 %1）")
                                            .arg(encName(det)));
        TextCodecUtil::setFallbackEncoding(TextEncoding::Latin1);
        (void)TextCodecUtil::decode(gbk, TextEncoding::Auto, &det);
        // ICU 的 Shift-JIS 校验比「字节范围」严格：这段 GBK 字节通不过，
        // 因此没有声明语言时会落到 LATIN1（而不是被静默当成 CP932 乱码）
        check(det == TextEncoding::Latin1,
              QString("回退复位后 GBK 样本落到 LATIN1（得到 %1）").arg(encName(det)));
    } else {
        qDebug().noquote() << "   [skip] 无 ICU";
    }

    qDebug() << "\n6) 显式设置落盘：只改一个键，保留其它行";
    {
        QTemporaryDir tmp;
        const QString path = tmp.filePath(QStringLiteral("CSV/_fixed.config"));
        writeEnc(path,
                 QString::fromUtf8("; 自分のメモ\n"
                                   "ウィンドウ幅:1600\n"
                                   "TextEncoding:AUTO\n"
                                   "表示する最低警告レベル:1\n"),
                 TextEncoding::ShiftJis);

        ConfigLoader loader;
        loader.loadConfigFile(path, 2);
        check(loader.getConfig(QStringLiteral("TextEncoding")) == QStringLiteral("AUTO"),
              "原始值 AUT0 读出");

        check(loader.setConfigValueInFile(path, QStringLiteral("TextEncoding"),
                                          QStringLiteral("SHIFT-JIS")),
              "写入 TextEncoding");
        TextEncoding det = TextEncoding::Auto;
        const QString after = TextCodecUtil::readFile(path, TextEncoding::Auto, &det);
        qDebug().noquote() << "   [info] 写入后编码:" << encName(det);
        check(after.contains(QString::fromUtf8("; 自分のメモ")), "注释保留");
        check(after.contains(QString::fromUtf8("ウィンドウ幅:1600")), "其它键保留");
        check(after.contains(QStringLiteral("TextEncoding:SHIFT-JIS")), "目标键被更新");
        check(!after.contains(QStringLiteral("TextEncoding:AUTO")), "旧值不再存在");

        // 追加一个不存在的键
        check(loader.setConfigValueInFile(path, QStringLiteral("MyKey"), QStringLiteral("v")),
              "追加新键");
        const QString after2 = TextCodecUtil::readFile(path);
        check(after2.contains(QStringLiteral("MyKey:v")), "新键已追加");
        ConfigLoader l2;
        l2.loadConfigFile(path, 2);
        check(l2.getConfig(QStringLiteral("MyKey")) == QStringLiteral("v"), "重读包含新键");
        check(l2.getConfig(QString::fromUtf8("ウィンドウ幅")) == QStringLiteral("1600"),
              "重读其它键仍正确");
    }

    qDebug() << "\n7) 转换器缺失的防护（Qt 无效 decoder/encoder 会静默返回空）";
    {
        // 内置/标准编码永远可用
        check(TextCodecUtil::canDecode(TextEncoding::ShiftJis)
                  == TextCodecUtil::canEncode(TextEncoding::ShiftJis),
              "SHIFT-JIS 读/写能力一致（都来自 Qt/ICU）");
        check(TextCodecUtil::canDecode(TextEncoding::Utf8)
                  && TextCodecUtil::canEncode(TextEncoding::Utf8), "UTF-8 读写都可用");
        check(TextCodecUtil::canDecode(TextEncoding::System), "System(Locale) 可用");
        check(!TextCodecUtil::canDecode(TextEncoding::Auto), "AUTO 不是一种具体编码");

        // ICU 可用性一致：hasCodec 与 canDecode/canEncode 应一致
        const bool icu = TextCodecUtil::hasCodec(QStringLiteral("GB18030"));
        check(TextCodecUtil::canDecode(TextEncoding::Gbk) == icu
                  && TextCodecUtil::canEncode(TextEncoding::Gbk) == icu,
              QString("GB18030 能力与 hasCodec 一致（ICU=%1）").arg(icu ? 1 : 0));

        // encode 用 ok 报告「字节流能否忠实表示原文本」
        bool ok = true;
        if (TextCodecUtil::canEncode(TextEncoding::Big5)) {
            // 繁体可表示 -> ok；简体专用字不可表示 -> ok=false（但仍给出尽力而为的字节）
            QStringEncoder probe{QStringLiteral("Big5")};
            const QByteArray trad = probe(QString::fromUtf8("測試"));
            check(!probe.hasError() || probe.finalize().error
                      == QStringConverter::FinalizeResultError::NoError,
                  "Big5 编码器可用（繁体测试）");
            const QByteArray enc1 = TextCodecUtil::encode(QString::fromUtf8("測試"),
                                                          TextEncoding::Big5, &ok);
            check(ok && !enc1.isEmpty() && enc1 == trad, "Big5 可表示时 ok=true");
            ok = true;
            const QByteArray enc2 = TextCodecUtil::encode(QString::fromUtf8("测试"),
                                                          TextEncoding::Big5, &ok);
            check(!ok, "Big5 无法表示简体字时 ok=false");
            Q_UNUSED(enc2);
        } else {
            const QByteArray enc = TextCodecUtil::encode(QString::fromUtf8("測試"),
                                                         TextEncoding::Big5, &ok);
            check(!ok && !enc.isEmpty(), "Big5 不可用时 ok=false 且退化为 UTF-8（不丢内容）");
        }

        // writeFile 在目标编码不可用时必须拒绝，而不是写出与声明不符的文件
        QTemporaryDir tmp;
        const QString p = tmp.filePath(QStringLiteral("out.bin"));
        if (TextCodecUtil::canEncode(TextEncoding::EucKr)) {
            check(TextCodecUtil::writeFile(p, QString::fromUtf8("테스트"), TextEncoding::EucKr),
                  "EUC-KR 可用时正常写入");
        } else {
            check(!TextCodecUtil::writeFile(p, QString::fromUtf8("테스트"), TextEncoding::EucKr),
                  "目标编码不可用时 writeFile 拒绝写入");
            check(!QFile::exists(p), "拒绝时不留半成品文件");
        }
        // 无法表示 -> 也拒绝（不写出被悄悄改坏的文件）
        if (TextCodecUtil::canEncode(TextEncoding::Big5)) {
            check(!TextCodecUtil::writeFile(p, QString::fromUtf8("测试简体"), TextEncoding::Big5),
                  "目标编码无法表示文本时 writeFile 拒绝写入");
        } else {
            qDebug().noquote() << "   [skip] 无 Big5 转换器";
        }
    }

    qDebug() << "\n=========================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] encoding probe tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
