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
#ifndef ERA_TEXT_ENCODING_H
#define ERA_TEXT_ENCODING_H

#include <QByteArray>
#include <QString>
#include <QStringList>

// ---------------------------------------------------------------------------
// TextEncoding —— 文本文件编解码（跨平台、与 C# Emuera 对齐）
//
// 背景
// ----
// C# Emuera 读**所有**文本文件（.ERB/.ERH/.CSV/*.config）都用
// `Config.Encode = Encoding.GetEncoding("SHIFT-JIS")`（`内部で使用する東アジア言語`
// 可换成 949/936/950 → EUC-KR / GBK / Big5），也就是**默认非 UTF-8**。
// 而同一份游戏目录里常常混着编码：eraTW 的 emuera.config 是 Shift-JIS，
// eraTW/ERB/*.ERB 却是 UTF-8+BOM。
//
// 设计：**全部交给 Qt 6 QStringConverter / ICU**，本模块不自己维护码表、
// 也不做「像哪国文字」的启发式评分。
//   · 标准编码（UTF-8/16/32、Latin-1、System=Locale）走 QStringConverter::Encoding；
//   · Shift-JIS / GB18030 / Big5 / EUC-KR 走 ICU 名字构造
//     （`QStringDecoder(QAnyStringView("ibm-943_P130-1999"))` 等）；
//   · 嗅探：`QStringConverter::encodingForData()` +「能不能严格解码」；
//   · 严格校验：`Flag::Stateless` + `finalize()`（**不要只看 hasError()**，
//     Qt6 的 decoder 有状态，丢弃返回值的调用会被优化掉，hasError() 会恒为 false）。
//
// 关于 Shift-JIS 的 ICU 名字（重要）
// --------------------------------
// 用 ICU 的 "Shift_JIS"（= windows-932/cp932/MS932 同族）。实测这组转换器：
//   0x5C -> U+005C（反斜线，ERB 的 `\@`/`\n` 转义依赖它）✓
//   0x8160 -> U+FF5E、U+301C **编码会失败**（CP932 是 U+301C ↔ 0x8160）
// 另一组 ICU 名字 ibm-943_P130-1999 虽给出 CP932 的波浪线映射，但 **0x5C -> U+00A5(¥)**，
// 会破坏脚本解析 —— 两害相权取前者的「反斜线正确」。
// 若本机 Qt 没有编入 ICU，则退到 `Encoding::System`
// （Windows 上 = ANSI 代码页，日文系统即 CP932；与 C# 的 Config.Encode 一致），
// 最后仍不可用则明确报错（canDecode/canEncode 返回 false），绝不静默丢内容。
// ---------------------------------------------------------------------------
enum class TextEncoding {
    Auto = 0,     // 仅用于「请求」：交给检测
    Utf8,
    Utf8Bom,
    Utf16LE,
    Utf16BE,
    Utf32LE,
    Utf32BE,
    ShiftJis,     // CP932（Windows-31J）；ICU 名字见文件头
    Gbk,          // GB18030 / CP936
    Big5,         // CP950
    EucKr,        // CP949
    System,       // 操作系统本地编码（Windows=ANSI 代码页；Unix=UTF-8）——对齐 C# Config.Encode
    Latin1,
};

namespace TextCodecUtil {

// 名称（配置项/日志用）："AUTO" "UTF-8" "UTF-8-BOM" … "SHIFT-JIS" "GB18030" "BIG5" "EUC-KR" "SYSTEM" "LATIN1"
[[nodiscard]] const char* name(TextEncoding enc);
// 从名称解析（大小写不敏感；接受 "SJIS"/"CP932"/"GBK"/"CP936"/"GB2312"/"CP950"/"CP949"/"LOCALE" 等别名）
[[nodiscard]] TextEncoding fromName(const QString& name);
// C# `内部で使用する東アジア言語` 的值（JAPANESE/KOREAN/CHINESE_HANS/CHINESE_HANT）-> 编码
[[nodiscard]] TextEncoding fromLanguageName(const QString& language);

// ---- Qt/ICU 能力查询 ----
[[nodiscard]] QStringList availableCodecs();          // QStringConverter::availableCodecs()
[[nodiscard]] bool hasCodec(const QString& codecName);
// 该编码能否用于解码 / 编码（**必须先查**：Qt 在没有对应转换器时会给出「无效」对象，
// 此时 decoder(data) 只会返回空字符串、encoder(text) 只会返回空字节流）
[[nodiscard]] bool canDecode(TextEncoding enc);
[[nodiscard]] bool canEncode(TextEncoding enc);
// 实现来源："Qt"（内置枚举）/ "Qt/ICU"（ICU 名字）/ "Qt/System"（本地代码页）/ "n/a"
[[nodiscard]] QString backendFor(TextEncoding enc);

// ---- 嗅探 ----
// 顺序：QStringConverter::encodingForData（BOM）→ 纯 ASCII → 严格 UTF-8
//       → 声明的回退编码（中文/韩文，若能严格解码）→ Shift-JIS（若能严格解码）→ Latin-1
// 说明：GBK/Big5 与 CP932 的字节范围重叠，没有「像哪国文字」评分时无法自动区分
//       —— 这正是 C# 的做法（它完全靠 `Config.Encode`），请用配置项声明语言/编码。
[[nodiscard]] TextEncoding detect(const QByteArray& data);

// 嗅探都不成立时使用的编码（默认 Latin1）。
// 与 C# 的 `内部で使用する東アジア言語` 对应；由 EraEngine 读配置或按 ROM 探测设置。
void setFallbackEncoding(TextEncoding enc);
[[nodiscard]] TextEncoding fallbackEncoding();

// ---- 解码 / 编码 ----
// hint != Auto 时强制使用该编码；否则先检测。
// detected（可空）输出实际使用的编码（BOM 会被剥离）。
[[nodiscard]] QString decode(const QByteArray& data, TextEncoding hint = TextEncoding::Auto,
                            TextEncoding* detected = nullptr);
// 严格校验：该字节流在指定编码下是否完全合法（Flag::Stateless + finalize）
[[nodiscard]] bool isValidInEncoding(const QByteArray& data, TextEncoding enc);
// 编码为字节流。ok（可空）：字节流能否**忠实**表示原文本
// （false = 要么没有该编码的转换器、要么有字符无法表示；此时返回尽力而为的字节 / UTF-8）
[[nodiscard]] QByteArray encode(const QString& text, TextEncoding enc = TextEncoding::Utf8,
                                bool* ok = nullptr);

// 便捷：读/写文件（自动嗅探 / 默认 UTF-8）
[[nodiscard]] QString readFile(const QString& filePath, TextEncoding hint = TextEncoding::Auto,
                              TextEncoding* detected = nullptr, bool* ok = nullptr);
// 写文件。目标编码不可用或无法忠实表示时**拒绝写入**并返回 false
bool writeFile(const QString& filePath, const QString& text, TextEncoding enc = TextEncoding::Utf8);

} // namespace TextCodecUtil

#endif // ERA_TEXT_ENCODING_H
