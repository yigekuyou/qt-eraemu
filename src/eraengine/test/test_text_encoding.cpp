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
// test_text_encoding.cpp
//
// 验证「跨平台文本编解码 + 配置项解析」：
//   1. 编码嗅探：ASCII / UTF-8 / UTF-8-BOM / UTF-16LE / Shift-JIS / 非法字节
//   2. Shift-JIS <-> Unicode 双向映射（含半角片假名、常用汉字）
//   3. decode/encode 往返（UTF-8 默认；Shift-JIS 兼容 Emuera）
//   4. ConfigLoader：`key:value`（Emuera 原格式）与 `key=value`、';' 注释、
//      Shift-JIS 配置、写回 UTF-8+BOM 再读
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "text_encoding.h"
#include "config_loader.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static QString encName(TextEncoding e) { return QString::fromLatin1(TextCodecUtil::name(e)); }

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Text encoding / config test";
    qDebug() << "===========================";

    qDebug() << "\n1) 编码嗅探";
    check(TextCodecUtil::detect("key:value\n") == TextEncoding::Utf8, "纯 ASCII -> UTF-8");
    check(TextCodecUtil::detect(QString::fromUtf8("标题,eraThe World").toUtf8())
              == TextEncoding::Utf8, "UTF-8 含中日文 -> UTF-8");
    {
        QByteArray bom("\xEF\xBB\xBF", 3);
        bom += QString::fromUtf8("标题").toUtf8();
        check(TextCodecUtil::detect(bom) == TextEncoding::Utf8Bom, "UTF-8 BOM -> UTF-8-BOM");
        check(TextCodecUtil::decode(bom) == QString::fromUtf8("标题"), "BOM 被剥离");
    }
    {
        QByteArray u16("\xFF\xFE", 2);
        u16 += QStringEncoder(QStringEncoder::Utf16LE)(QString::fromUtf8("日本語"));
        check(TextCodecUtil::detect(u16) == TextEncoding::Utf16LE, "FF FE -> UTF-16LE");
        check(TextCodecUtil::decode(u16) == QString::fromUtf8("日本語"), "UTF-16LE 解码正确");
    }
    {
        // 「タイトル」的 Shift-JIS 字节
        const QByteArray sjis("\x83\x5E\x83\x43\x83\x67\x83\x8B", 8);
        check(TextCodecUtil::detect(sjis) == TextEncoding::ShiftJis, "Shift-JIS 字节 -> SHIFT-JIS");
        check(TextCodecUtil::decode(sjis) == QString::fromUtf8("タイトル"),
              QString("Shift-JIS 解码 = タイトル（得到 %1）").arg(TextCodecUtil::decode(sjis)));
    }
    {
        // 0xFF 不是合法 UTF-8 也不是合法 SJIS
        const QByteArray junk("\xFF\xFE\xFF\xFF", 4);   // 会被 BOM 规则抢先，换一个
        const QByteArray bad("\x81\x20\x81\x20", 4);    // 0x20 不是合法后继
        check(TextCodecUtil::detect(bad) == TextEncoding::Latin1, "非法 UTF-8/Shift-JIS -> LATIN1");
        check(!TextCodecUtil::decode(bad).isEmpty(), "任意字节都能解出结果（不崩溃）");
    }

    qDebug() << "\n2) Shift-JIS 双向（全部由 Qt/ICU 转换器完成）";
    {
        TextEncoding det = TextEncoding::Auto;
        const QString back = TextCodecUtil::decode(QByteArray("\x83\x5E\x82\xA0", 4),
                                                   TextEncoding::ShiftJis, &det);
        check(back == QString::fromUtf8("タあ"),
              QString("835E 82A0 -> タあ（得到 %1）").arg(back));
        const QByteArray enc = TextCodecUtil::encode(QString::fromUtf8("タあ"),
                                                     TextEncoding::ShiftJis);
        check(enc == QByteArray("\x83\x5E\x82\xA0", 4),
              QString("タあ -> 835E 82A0（得到 %1）").arg(QString::fromLatin1(enc.toHex())));
        // 半角片假名（单字节区）
        check(TextCodecUtil::encode(QString::fromUtf8("ｱ"), TextEncoding::ShiftJis)
                  == QByteArray("\xB1", 1), "ｱ -> B1");
    }

    qDebug() << "\n3) decode/encode 往返";
    {
        // 注意：UTF-8/BOM/UTF-16 能表示任意字符；Shift-JIS 只能表示日文范围内字符，
        // 因此往返测试用「SJIS 可表示」的文本，不可表示的另测（见下）。
        const QString text = QString::fromUtf8("タイトル:eraThe World 日本語 テスト ｱｲｳ");
        for (TextEncoding enc : {TextEncoding::Utf8, TextEncoding::Utf8Bom,
                                 TextEncoding::Utf16LE, TextEncoding::Utf16BE,
                                 TextEncoding::ShiftJis}) {
            const QByteArray bytes = TextCodecUtil::encode(text, enc);
            TextEncoding detected = TextEncoding::Auto;
            const QString back = TextCodecUtil::decode(bytes, TextEncoding::Auto, &detected);
            check(back == text, QString("%1 往返一致（嗅探为 %2）").arg(encName(enc), encName(detected)));
        }
        // SJIS 里无法表示的字符（emoji / 简化字）：ok=false（替换字节由转换器决定），不崩
        bool sjisOk = true;
        const QByteArray sjis = TextCodecUtil::encode(QString::fromUtf8("A😀B标"),
                                                      TextEncoding::ShiftJis, &sjisOk);
        check(!sjisOk && !sjis.isEmpty(),
              QString("不可表示字符 -> ok=false, 替换字节 %1")
                  .arg(QString::fromLatin1(sjis.toHex())));
        bool sjisOk2 = true;
        const QByteArray sjis2 = TextCodecUtil::encode(QString::fromUtf8("A中文B"),
                                                       TextEncoding::ShiftJis, &sjisOk2);
        check(sjisOk2 && sjis2 == QByteArray("A\x92\x86\x95\xB6" "B"),
              QString("常用汉字可表示 -> ok=true（得到 %1）").arg(QString::fromLatin1(sjis2.toHex())));
        // 反斜线必须能往返（ERB 的 \@ / \n 转义依赖它）
        bool bsOk = true;
        const QByteArray bs = TextCodecUtil::encode(QStringLiteral("C:\\bin"), TextEncoding::ShiftJis, &bsOk);
        check(bsOk && bs == QByteArray("C:\\bin"),
              QString("反斜线 0x5C 往返正确（得到 %1）").arg(QString::fromLatin1(bs.toHex())));
        check(TextCodecUtil::decode(QByteArray("\x5C", 1), TextEncoding::ShiftJis) == QStringLiteral("\\"),
              "0x5C 解码为反斜线（不是 ¥）");
    }

    qDebug() << "\n4) ConfigLoader：Emuera 原格式 key:value + Shift-JIS";
    {
        QTemporaryDir tmp;
        const QString path = tmp.filePath(QStringLiteral("emuera.config"));
        // 用 Shift-JIS 写出一个「原版 Emuera 风格」的配置
        const QString body = QString::fromUtf8(
            ";コメント\n"
            "ウィンドウ幅:1400\n"
            "ウィンドウ高さ:750\n"
            "サブディレクトリを検索する:YES\n"
            "表示する最低警告レベル:1\n"
            "テキストエディタ:C:\\bin\\notepad.exe\n"
            "追加情報:\"含引号の値\"\n");
        check(TextCodecUtil::writeFile(path, body, TextEncoding::ShiftJis), "写出 Shift-JIS 配置");

        ConfigLoader loader;
        check(loader.loadConfigFile(path, 1), "装载成功");
        check(loader.encodingOf(path) == TextEncoding::ShiftJis, "嗅探为 Shift-JIS");
        check(loader.getInt(QString::fromUtf8("ウィンドウ幅"), -1) == 1400,
              "ウィンドウ幅 = 1400");
        check(loader.getBool(QString::fromUtf8("サブディレクトリを検索する"), false),
              "サブディレクトリを検索する = YES -> true");
        check(loader.getConfig(QString::fromUtf8("テキストエディタ"))
                  == QString::fromUtf8("C:\\bin\\notepad.exe"),
              QString("值里的 ':' 不被截断（得到 %1）")
                  .arg(loader.getConfig(QString::fromUtf8("テキストエディタ"))));
        check(loader.getConfig(QString::fromUtf8("追加情報")) == QString::fromUtf8("含引号の値"),
              QString("引号被剥离（得到 %1）")
                  .arg(loader.getConfig(QString::fromUtf8("追加情報"))));
        check(!loader.hasConfig(QString::fromUtf8("コメント")), "; 注释行被跳过");

        qDebug() << "\n5) 写回：UTF-8 + BOM（Linux/macOS/Windows 通用）";
        const QString outPath = tmp.filePath(QStringLiteral("emuera.out.config"));
        // 直接以 UTF-8+BOM 写一份（模拟我们保存配置的行为）
        check(TextCodecUtil::writeFile(outPath,
                                       QString::fromUtf8("ウィンドウ幅:1600\n"
                                                         "サブディレクトリを検索する:NO\n"),
                                       TextEncoding::Utf8Bom),
              "写出 UTF-8+BOM 配置");
        ConfigLoader loader2;
        check(loader2.loadConfigFile(outPath, 0), "重新装载成功");
        check(loader2.encodingOf(outPath) == TextEncoding::Utf8Bom, "嗅探为 UTF-8-BOM");
        check(loader2.getInt(QString::fromUtf8("ウィンドウ幅"), -1) == 1600, "值正确（UTF-8 路径）");
        check(!loader2.getBool(QString::fromUtf8("サブディレクトリを検索する"), true),
              "NO -> false");

        qDebug() << "\n6) 兼容 key=value（自写配置）";
        const QString eqPath = tmp.filePath(QStringLiteral("eq.config"));
        TextCodecUtil::writeFile(eqPath, QString::fromUtf8("A=1\nB = two\nC:D\n"), TextEncoding::Utf8);
        ConfigLoader loader3;
        loader3.loadConfigFile(eqPath, 0);
        check(loader3.getInt(QStringLiteral("A"), -1) == 1, "A=1");
        check(loader3.getConfig(QStringLiteral("B")) == QStringLiteral("two"), "B = two（含空格）");
        check(loader3.getConfig(QStringLiteral("C")) == QStringLiteral("D"), "C:D（冒号也认）");

        qDebug() << "\n7) 优先级：_fixed.config > emuera.config > _default.config";
        const QString dPath = tmp.filePath(QStringLiteral("_default.config"));
        const QString fPath = tmp.filePath(QStringLiteral("_fixed.config"));
        TextCodecUtil::writeFile(dPath, QString::fromUtf8("X:1\nY:1\n"), TextEncoding::Utf8);
        TextCodecUtil::writeFile(fPath, QString::fromUtf8("X:3\n"), TextEncoding::Utf8);
        ConfigLoader loader4;
        loader4.loadConfigFile(dPath, 0);
        loader4.loadConfigFile(eqPath, 1);
        loader4.loadConfigFile(fPath, 2);
        loader4.setConfig(QStringLiteral("Z"), QStringLiteral("9"));
        check(loader4.getConfig(QStringLiteral("X")) == QStringLiteral("3"),
              "_fixed 覆盖 _default");
        check(loader4.getConfig(QStringLiteral("Y")) == QStringLiteral("1"), "未覆盖的键保留");
        check(loader4.getConfig(QStringLiteral("Z")) == QStringLiteral("9"), "setConfig 生效");
    }


    qDebug() << "\n8) Qt 6.11 QStringConverter 集成（无自建码表）";
    {
        const QStringList codecs = TextCodecUtil::availableCodecs();
        check(!codecs.isEmpty(), QString("availableCodecs() 非空（%1 项）").arg(codecs.size()));
        check(TextCodecUtil::hasCodec(QStringLiteral("UTF-8")), "内置 UTF-8 可用");
        check(TextCodecUtil::backendFor(TextEncoding::Utf8) == QStringLiteral("Qt"),
              "UTF-8 由 Qt 实现");
        // Shift-JIS 现在也交给 Qt/ICU（优先 ICU 内部名 ibm-943_P130-1999 = CP932 行为）
        qDebug().noquote() << "   [info] SHIFT-JIS backend:" << TextCodecUtil::backendFor(TextEncoding::ShiftJis)
                           << " canDecode:" << TextCodecUtil::canDecode(TextEncoding::ShiftJis)
                           << " canEncode:" << TextCodecUtil::canEncode(TextEncoding::ShiftJis);
        check(TextCodecUtil::canDecode(TextEncoding::Latin1), "Latin-1（兜底）可用");
        qDebug().noquote() << "   [info] ICU 编解码器数量:" << codecs.size()
                           << " GB18030 可用:" << TextCodecUtil::hasCodec(QStringLiteral("GB18030"));
    }

    qDebug() << "\n9) Shift-JIS（CP932）由 Qt/ICU 提供";
    if (TextCodecUtil::canDecode(TextEncoding::ShiftJis)) {
        const QByteArray sjis = TextCodecUtil::encode(QString::fromUtf8("タイトル"),
                                                      TextEncoding::ShiftJis);
        check(sjis == QByteArray("\x83\x5E\x83\x43\x83\x67\x83\x8B", 8),
              QString("编码 タイトル = 835E 8343 8367 838B（得到 %1）")
                  .arg(QString::fromLatin1(sjis.toHex())));
        TextEncoding det = TextEncoding::Auto;
        const QString back = TextCodecUtil::decode(sjis, TextEncoding::Auto, &det);
        check(det == TextEncoding::ShiftJis && back == QString::fromUtf8("タイトル"),
              "解码回 タイトル（嗅探为 SHIFT-JIS）");
        // 半角片假名（0xA1-0xDF 单字节）
        check(TextCodecUtil::encode(QString::fromUtf8("ｱｲ"), TextEncoding::ShiftJis)
                  == QByteArray("\xB1\xB2", 2), "半角片假名 ｱｲ = B1 B2");
        // 波浪线那 7 个字符是 ICU 的 JIS 映射（仅显示层面）：只报告不强断言
        bool waveOk = true;
        const QByteArray wave = TextCodecUtil::encode(QString(QChar(0x301C)), TextEncoding::ShiftJis, &waveOk);
        qDebug().noquote() << QString("   [info] U+301C(〜) -> %1 (ok=%2)；0x8160 解码为 %3")
                                  .arg(QString::fromLatin1(wave.toHex()))
                                  .arg(waveOk ? 1 : 0)
                                  .arg(QString(QChar(TextCodecUtil::decode(QByteArray("\x81\x60", 2),
                                                                           TextEncoding::ShiftJis).at(0).unicode())));
    } else {
        qDebug().noquote() << "   [skip] 本机 Qt 无 Shift-JIS 转换器（无 ICU 且本地代码页为 UTF-8）";
    }

    qDebug() << "\n10) 写 BOM 与严格校验（finalize 而非 hasError）";
    {
        const QByteArray bom = TextCodecUtil::encode(QStringLiteral("A"), TextEncoding::Utf8Bom);
        check(bom == QByteArray("\xEF\xBB\xBF" "A"),
              QString("Utf8Bom 写出 EF BB BF（得到 %1）").arg(QString::fromLatin1(bom.toHex())));
        const QByteArray u16 = TextCodecUtil::encode(QStringLiteral("A"), TextEncoding::Utf16LE);
        check(u16.startsWith(QByteArray("\xFF\xFE", 2)),
              QString("Utf16LE 写出 FF FE（得到 %1）").arg(QString::fromLatin1(u16.toHex())));
        check(!TextCodecUtil::isValidInEncoding(QByteArray("\x83\x5E", 2), TextEncoding::Utf8),
              "非法 UTF-8 判定为不合法");
        const QString sysText = TextCodecUtil::decode(QByteArray("abc"), TextEncoding::System);
        check(sysText == QStringLiteral("abc"), "System(Locale) 编码解码 ASCII 正确");
    }

    qDebug() << "\n===========================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] text encoding tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
