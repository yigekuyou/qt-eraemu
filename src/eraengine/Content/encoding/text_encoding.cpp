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
#include "text_encoding.h"

#include <QDebug>
#include <QFile>
#include <QStringConverter>
#include <atomic>

namespace {

// 嗅探都不成立时的回退编码（默认 Latin-1，永不失败）。
// 用 atomic 是因为 ERB 解析会跑在多个 worker 线程里。
std::atomic<int>& fallbackSlot() {
    static std::atomic<int> slot{static_cast<int>(TextEncoding::Latin1)};
    return slot;
}

// ---- Qt 编解码器选择 ----
struct QtCodecChoice {
    bool valid = false;
    bool byEnum = false;                        // 用 QStringConverter::Encoding 构造
    QStringConverter::Encoding enumValue = QStringConverter::Encoding::Utf8;
    QString name;                               // 否则用 ICU 名字构造
};

// Shift-JIS(CP932) 的 ICU 候选。
//
// 实测（Qt 6.11 + ICU）：
//   ibm-943_P130-1999 : 0x8160 -> U+301C（CP932 的波浪线）但 **0x5C -> U+00A5(¥)**
//   Shift_JIS / windows-932 / cp932 / MS932… : 0x5C -> U+005C（正确）但 0x8160 -> U+FF5E
//
// 0x5C 必须映射成反斜线：ERB 脚本的 `\@...\@`、`\n` 转义都依赖它，若变成 ¥
// 会直接破坏解析；而 0x8160 那 7 个「波浪线/双竖线」字符只是显示层面的差别
// （Emuera 也不会用它们做字符串比较），因此**取后者**。
const QStringList& sjisCandidateNames() {
    static const QStringList names = {
        QStringLiteral("Shift_JIS"),
        QStringLiteral("Shift-JIS"),
        QStringLiteral("windows-932"),
        QStringLiteral("cp932"),
        QStringLiteral("MS932"),
    };
    return names;
}

// 本地代码页是否是「非 UTF-8 的旧代码页」（Windows 日文系统 = CP932）
bool systemIsLegacyCodePage() {
    static const bool legacy = [] {
        QStringEncoder encoder(QStringConverter::Encoding::System);
        if (!encoder.isValid()) return false;
        // 用日文片假名探测：若字节与 UTF-8 不同，说明 System 是旧代码页
        const QString probe = QStringLiteral("ア");
        const QByteArray bytes = encoder(probe);
        return !bytes.isEmpty() && bytes != probe.toUtf8();
    }();
    return legacy;
}

QtCodecChoice qtCodecFor(TextEncoding e) {
    QtCodecChoice c;
    switch (e) {
    case TextEncoding::Utf8:
    case TextEncoding::Utf8Bom:
        c.valid = true; c.byEnum = true; c.enumValue = QStringConverter::Encoding::Utf8; break;
    case TextEncoding::Utf16LE:
        c.valid = true; c.byEnum = true; c.enumValue = QStringConverter::Encoding::Utf16LE; break;
    case TextEncoding::Utf16BE:
        c.valid = true; c.byEnum = true; c.enumValue = QStringConverter::Encoding::Utf16BE; break;
    case TextEncoding::Utf32LE:
        c.valid = true; c.byEnum = true; c.enumValue = QStringConverter::Encoding::Utf32LE; break;
    case TextEncoding::Utf32BE:
        c.valid = true; c.byEnum = true; c.enumValue = QStringConverter::Encoding::Utf32BE; break;
    case TextEncoding::Latin1:
        c.valid = true; c.byEnum = true; c.enumValue = QStringConverter::Encoding::Latin1; break;
    case TextEncoding::System:
        // 等价于 C# 的 Config.Encode：Windows = ANSI 代码页，Unix = UTF-8
        c.valid = true; c.byEnum = true; c.enumValue = QStringConverter::Encoding::System; break;
    case TextEncoding::ShiftJis: {
        for (const QString& n : sjisCandidateNames()) {
            QStringDecoder probe{QAnyStringView(n)};
            if (probe.isValid()) { c.valid = true; c.name = n; return c; }
        }
        // 无 ICU：退到本地代码页（日文 Windows/日文 locale 下即 CP932）
        if (systemIsLegacyCodePage()) {
            c.valid = true; c.byEnum = true; c.enumValue = QStringConverter::Encoding::System;
        }
        return c;
    }
    case TextEncoding::Gbk:
        c.valid = true; c.name = QStringLiteral("GB18030"); break;
    case TextEncoding::Big5:
        c.valid = true; c.name = QStringLiteral("Big5"); break;
    case TextEncoding::EucKr:
        c.valid = true; c.name = QStringLiteral("EUC-KR"); break;
    case TextEncoding::Auto:
    default:
        break;
    }
    return c;
}

// 严格解码：Flag::Stateless 让「不完整序列」报错；错误必须用 finalize() 取
// （Qt6 的 decoder 有状态：丢弃返回值 => hasError() 恒 false）
bool decodeWithQtStrict(const QByteArray& data, const QtCodecChoice& c, QString& out) {
    if (!c.valid) return false;
    QStringDecoder decoder = c.byEnum
        ? QStringDecoder(c.enumValue, QStringConverter::Flag::Stateless)
        : QStringDecoder(QAnyStringView(c.name), QStringConverter::Flag::Stateless);
    if (!decoder.isValid()) return false;
    out = decoder(data);                       // 必须接住返回值
    return decoder.finalize().error == QStringConverter::FinalizeResultError::NoError;
}

bool encodeWithQt(const QString& text, const QtCodecChoice& c, bool writeBom, QByteArray& out) {
    if (!c.valid) return false;
    const QStringConverter::Flags flags = writeBom ? QStringConverter::Flag::WriteBom
                                                   : QStringConverter::Flag::Default;
    QStringEncoder encoder = c.byEnum ? QStringEncoder(c.enumValue, flags)
                                      : QStringEncoder(QAnyStringView(c.name), flags);
    if (!encoder.isValid()) return false;
    out = encoder(text);
    return encoder.finalize().error == QStringConverter::FinalizeResultError::NoError;
}

} // namespace

namespace TextCodecUtil {

const char* name(TextEncoding enc) {
    switch (enc) {
    case TextEncoding::Auto:    return "AUTO";
    case TextEncoding::Utf8:    return "UTF-8";
    case TextEncoding::Utf8Bom: return "UTF-8-BOM";
    case TextEncoding::Utf16LE: return "UTF-16LE";
    case TextEncoding::Utf16BE: return "UTF-16BE";
    case TextEncoding::Utf32LE: return "UTF-32LE";
    case TextEncoding::Utf32BE: return "UTF-32BE";
    case TextEncoding::ShiftJis:return "SHIFT-JIS";
    case TextEncoding::Gbk:     return "GB18030";
    case TextEncoding::Big5:    return "BIG5";
    case TextEncoding::EucKr:   return "EUC-KR";
    case TextEncoding::System:  return "SYSTEM";
    case TextEncoding::Latin1:  return "LATIN1";
    }
    return "UNKNOWN";
}

TextEncoding fromName(const QString& name) {
    const QString k = name.trimmed().toUpper().remove(QLatin1Char('-')).remove(QLatin1Char('_'));
    if (k.isEmpty() || k == QLatin1String("AUTO")) return TextEncoding::Auto;
    if (k == QLatin1String("UTF8BOM")) return TextEncoding::Utf8Bom;
    if (k == QLatin1String("UTF8")) return TextEncoding::Utf8;
    if (k == QLatin1String("UTF16LE")) return TextEncoding::Utf16LE;
    if (k == QLatin1String("UTF16BE")) return TextEncoding::Utf16BE;
    if (k == QLatin1String("UTF32LE")) return TextEncoding::Utf32LE;
    if (k == QLatin1String("UTF32BE")) return TextEncoding::Utf32BE;
    if (k == QLatin1String("SJIS") || k == QLatin1String("SHIFTJIS")
        || k == QLatin1String("CP932") || k == QLatin1String("MS932")
        || k == QLatin1String("WINDOWS31J") || k == QLatin1String("932")) {
        return TextEncoding::ShiftJis;
    }
    if (k == QLatin1String("GB18030") || k == QLatin1String("GBK") || k == QLatin1String("CP936")
        || k == QLatin1String("GB2312") || k == QLatin1String("936")
        || k == QLatin1String("CHINESEHANS")) {
        return TextEncoding::Gbk;
    }
    if (k == QLatin1String("BIG5") || k == QLatin1String("CP950") || k == QLatin1String("950")
        || k == QLatin1String("CHINESEHANT")) {
        return TextEncoding::Big5;
    }
    if (k == QLatin1String("EUCKR") || k == QLatin1String("CP949") || k == QLatin1String("949")
        || k == QLatin1String("KSC5601") || k == QLatin1String("KOREAN")) {
        return TextEncoding::EucKr;
    }
    if (k == QLatin1String("SYSTEM") || k == QLatin1String("LOCALE")
        || k == QLatin1String("ANSI")) {
        return TextEncoding::System;
    }
    if (k == QLatin1String("LATIN1") || k == QLatin1String("ISO88591")) return TextEncoding::Latin1;
    return TextEncoding::Auto;
}

TextEncoding fromLanguageName(const QString& language) {
    const QString k = language.trimmed().toUpper();
    if (k == QLatin1String("JAPANESE") || k == QLatin1String("JP")) return TextEncoding::ShiftJis;
    if (k == QLatin1String("KOREAN") || k == QLatin1String("KO")) return TextEncoding::EucKr;
    if (k == QLatin1String("CHINESE_HANS") || k == QLatin1String("CHINESE-HANS")
        || k == QLatin1String("ZH_CN") || k == QLatin1String("CHINESE")) {
        return TextEncoding::Gbk;
    }
    if (k == QLatin1String("CHINESE_HANT") || k == QLatin1String("CHINESE-HANT")
        || k == QLatin1String("ZH_TW")) {
        return TextEncoding::Big5;
    }
    return TextEncoding::Auto;
}

QStringList availableCodecs() {
    return QStringConverter::availableCodecs();
}

bool hasCodec(const QString& codecName) {
    if (codecName.isEmpty()) return false;
    if (QStringConverter::encodingForName(QAnyStringView(codecName)).has_value()) return true;
    const QStringList all = QStringConverter::availableCodecs();
    for (const QString& c : all) {
        if (c.compare(codecName, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

bool canDecode(TextEncoding enc) {
    if (enc == TextEncoding::Auto) return false;   // 不是一种具体编码
    const QtCodecChoice c = qtCodecFor(enc);
    if (!c.valid) return false;
    const QStringDecoder probe = c.byEnum ? QStringDecoder(c.enumValue)
                                          : QStringDecoder(QAnyStringView(c.name));
    return probe.isValid();
}

bool canEncode(TextEncoding enc) {
    if (enc == TextEncoding::Auto) return false;
    const QtCodecChoice c = qtCodecFor(enc);
    if (!c.valid) return false;
    const QStringEncoder probe = c.byEnum ? QStringEncoder(c.enumValue)
                                          : QStringEncoder(QAnyStringView(c.name));
    return probe.isValid();
}

QString backendFor(TextEncoding enc) {
    switch (enc) {
    case TextEncoding::Auto:
        return QStringLiteral("n/a");
    case TextEncoding::ShiftJis: {
        const QtCodecChoice c = qtCodecFor(TextEncoding::ShiftJis);
        if (!c.valid) return QStringLiteral("n/a");
        return c.byEnum ? QStringLiteral("Qt/System") : QStringLiteral("Qt/ICU");
    }
    case TextEncoding::Gbk:
    case TextEncoding::Big5:
    case TextEncoding::EucKr:
        return canDecode(enc) ? QStringLiteral("Qt/ICU") : QStringLiteral("n/a");
    default:
        return QStringLiteral("Qt");
    }
}

void setFallbackEncoding(TextEncoding enc) {
    if (enc == TextEncoding::Auto) enc = TextEncoding::Latin1;
    fallbackSlot().store(static_cast<int>(enc), std::memory_order_relaxed);
}

TextEncoding fallbackEncoding() {
    return static_cast<TextEncoding>(fallbackSlot().load(std::memory_order_relaxed));
}

bool isValidInEncoding(const QByteArray& data, TextEncoding enc) {
    if (enc == TextEncoding::Auto) return true;
    if (enc == TextEncoding::Latin1) return true;   // 永不失败
    QString tmp;
    const QtCodecChoice c = qtCodecFor(enc);
    if (!c.valid) return false;
    return decodeWithQtStrict(data, c, tmp);
}

TextEncoding detect(const QByteArray& data) {
    if (data.isEmpty()) return TextEncoding::Utf8;

    // 1) Qt 官方嗅探（对 BOM 类给出确定结论）
    if (const auto e = QStringConverter::encodingForData(data)) {
        switch (*e) {
        case QStringConverter::Encoding::Utf8:    return TextEncoding::Utf8Bom;
        case QStringConverter::Encoding::Utf16LE: return TextEncoding::Utf16LE;
        case QStringConverter::Encoding::Utf16BE: return TextEncoding::Utf16BE;
        case QStringConverter::Encoding::Utf32LE: return TextEncoding::Utf32LE;
        case QStringConverter::Encoding::Utf32BE: return TextEncoding::Utf32BE;
        default: break;
        }
    }
    // 1b) UTF-32 的 BOM（encodingForData 不一定认）
    if (data.size() >= 4 && static_cast<quint8>(data.at(0)) == 0xFF
        && static_cast<quint8>(data.at(1)) == 0xFE && static_cast<quint8>(data.at(2)) == 0x00
        && static_cast<quint8>(data.at(3)) == 0x00) {
        return TextEncoding::Utf32LE;
    }
    if (data.size() >= 4 && static_cast<quint8>(data.at(0)) == 0x00
        && static_cast<quint8>(data.at(1)) == 0x00 && static_cast<quint8>(data.at(2)) == 0xFE
        && static_cast<quint8>(data.at(3)) == 0xFF) {
        return TextEncoding::Utf32BE;
    }

    // 2) 纯 ASCII：UTF-8 与其它编码等价，按 UTF-8 处理
    bool asciiOnly = true;
    for (int i = 0; i < data.size(); ++i) {
        if (static_cast<quint8>(data.at(i)) >= 0x80) { asciiOnly = false; break; }
    }
    if (asciiOnly) return TextEncoding::Utf8;

    // 3) 严格 UTF-8
    if (isValidInEncoding(data, TextEncoding::Utf8)) return TextEncoding::Utf8;

    // 4) 声明的东亚语言（`内部で使用する東アジア言語` / ROM 探测结论）优先：
    //    GBK/Big5/EUC-KR 与 CP932 的字节范围重叠，声明是唯一可靠的区分手段。
    const TextEncoding fb = fallbackEncoding();
    if (fb == TextEncoding::Gbk || fb == TextEncoding::Big5 || fb == TextEncoding::EucKr) {
        if (isValidInEncoding(data, fb)) return fb;
    }

    // 5) Shift-JIS（CP932）
    if (isValidInEncoding(data, TextEncoding::ShiftJis)) return TextEncoding::ShiftJis;

    // 6) 兜底：声明的编码（可能是 System/SJIS）→ Latin-1（永不失败）
    if (fb != TextEncoding::Latin1 && isValidInEncoding(data, fb)) return fb;
    return TextEncoding::Latin1;
}

QString decode(const QByteArray& data, TextEncoding hint, TextEncoding* detected) {
    TextEncoding enc = (hint == TextEncoding::Auto) ? detect(data) : hint;
    if (enc == TextEncoding::Auto) enc = detect(data);

    // 转换器缺失（Qt 未编入 ICU / 非日文 locale 的 System）：明确告警后回退，
    // 不要静默产出空字符串（Qt 的无效 decoder 会那样做）
    if (!canDecode(enc)) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            qWarning() << "[TextCodecUtil] 本机 Qt 不支持编码" << name(enc)
                       << "（无转换器）；将回退到 SHIFT-JIS / LATIN1。可用编解码器数:"
                       << availableCodecs().size() << " backendFor:"
                       << backendFor(TextEncoding::ShiftJis);
        }
        enc = canDecode(TextEncoding::ShiftJis) ? TextEncoding::ShiftJis : TextEncoding::Latin1;
    }
    if (detected) *detected = enc;

    QByteArray body = data;
    if (enc == TextEncoding::Utf8Bom && body.startsWith(QByteArray("\xEF\xBB\xBF", 3))) {
        body.remove(0, 3);
    } else if (enc == TextEncoding::Utf16LE && body.size() >= 2
               && static_cast<quint8>(body.at(0)) == 0xFF
               && static_cast<quint8>(body.at(1)) == 0xFE) {
        body.remove(0, 2);   // 显式去掉 BOM（Qt 也会跳过，这里保持确定）
    } else if (enc == TextEncoding::Utf16BE && body.size() >= 2
               && static_cast<quint8>(body.at(0)) == 0xFE
               && static_cast<quint8>(body.at(1)) == 0xFF) {
        body.remove(0, 2);
    } else if (enc == TextEncoding::Utf32LE || enc == TextEncoding::Utf32BE) {
        if (body.size() >= 4) body.remove(0, 4);
    }

    QString out;
    if (decodeWithQtStrict(body, qtCodecFor(enc), out)) {
        return out;
    }

    // 严格解码失败（真实游戏里存在「声明是 UTF-8 实际是 SJIS」的文件 / 半截文件）：
    // 回退 Shift-JIS → 声明的编码 → Latin-1，保证不丢内容
    if (enc != TextEncoding::ShiftJis && canDecode(TextEncoding::ShiftJis)
        && isValidInEncoding(body, TextEncoding::ShiftJis)
        && decodeWithQtStrict(body, qtCodecFor(TextEncoding::ShiftJis), out)) {
        if (detected) *detected = TextEncoding::ShiftJis;
        return out;
    }
    if (enc != TextEncoding::Latin1) {
        if (detected) *detected = TextEncoding::Latin1;
    }
    QString latin;
    latin.reserve(body.size());
    for (int i = 0; i < body.size(); ++i) {
        latin.append(QChar(static_cast<quint8>(body.at(i))));
    }
    return latin;
}

QByteArray encode(const QString& text, TextEncoding enc, bool* ok) {
    if (ok) *ok = true;
    if (enc == TextEncoding::Auto) enc = TextEncoding::Utf8;

    const bool writeBom = (enc == TextEncoding::Utf8Bom || enc == TextEncoding::Utf16LE
                           || enc == TextEncoding::Utf16BE || enc == TextEncoding::Utf32LE
                           || enc == TextEncoding::Utf32BE);

    if (!canEncode(enc)) {
        if (ok) *ok = false;
        static bool warned = false;
        if (!warned) {
            warned = true;
            qWarning() << "[TextCodecUtil] 本机 Qt 不支持编码" << name(enc) << "（无转换器）；"
                       << "已退化为 UTF-8。可用编解码器数:" << availableCodecs().size();
        }
        return text.toUtf8();
    }

    QByteArray out;
    if (encodeWithQt(text, qtCodecFor(enc), writeBom, out)) {
        return out;   // 完全可表示
    }
    // 有字符无法用目标编码表示（finalize 报 InvalidCharacters）：仍返回尽力而为的字节，
    // 但 ok=false，让调用方决定（写文件时会被拒绝）
    if (ok) *ok = false;
    qWarning() << "[TextCodecUtil] 编码" << name(enc) << "无法表示部分字符（已用替代字符）";
    return out;
}

QString readFile(const QString& filePath, TextEncoding hint, TextEncoding* detected, bool* ok) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok) *ok = false;
        return QString();
    }
    const QByteArray data = file.readAll();
    if (ok) *ok = true;
    return decode(data, hint, detected);
}

bool writeFile(const QString& filePath, const QString& text, TextEncoding enc) {
    if (enc == TextEncoding::Auto) enc = TextEncoding::Utf8;
    if (!canEncode(enc)) {
        qWarning() << "[TextCodecUtil] 拒绝写入" << filePath << "：本机 Qt 无编码"
                   << name(enc) << "的转换器（绝不写出与声明编码不符的内容）";
        return false;
    }
    bool encodeOk = true;
    const QByteArray data = encode(text, enc, &encodeOk);
    if (!encodeOk) {
        qWarning() << "[TextCodecUtil] 拒绝写入" << filePath << "：文本无法用"
                   << name(enc) << "完整表示（避免写出被悄悄改坏的文件）";
        return false;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(data) == data.size();
}

} // namespace TextCodecUtil
