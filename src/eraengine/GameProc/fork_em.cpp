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
// fork_em.cpp —— EM 私家版的「变量名字符串化」与「追加関数」的实现。
//
// 覆盖（C# 注册见 Runtime/Script/Statements/Function/Creator.cs:218-244）：
//   · int GETVAR(str 变量名)                       —— 按名字读整数/常量变量
//   · str GETVARS(str 变量名)                      —— 按名字读字符串变量
//   · int SETVAR(str 变量名, <值>)                 —— 按名字写单个变量
//   · int EXISTVAR(str 名字)                       —— 变量存在性位掩码
//   · int EXISTFILE(str 路径)                      —— 文件存在性（GetValidPath 语义）
//   · int ISDEFINED(str 宏名)                      —— `#DEFINE` 宏是否存在
//   · int REGEXPMATCH(str, str{, int}) / (str, str, ref int, ref str[])
//                                                   —— 正则匹配（返回匹配数）
//   · int HTML_STRINGLEN(str, int)                 —— HTML 串的显示宽度/行宽
//   · str HTML_SUBSTRING(str, int)                 —— 按行宽切一行（写 RESULTS）
//   · int HTML_STRINGLINES(str, int)               —— 按行宽折行后的行数
//
// 这一族**不在核心表**（GameData/ast/function_types.h 只放 C# 原版方法）——
// 它们是 fork 扩展，实参形态/求值全部经 ExtensionRegistry::regExpr 登记，
// 需要的能力（解析变量名、变量存在性、写回变量、游戏目录、字号）由注册类的
// Services 提供（见 extension_registry.h 的 Services 说明）。
//
// 语义依据：emuera.em 的 Creator.Method.cs（实现）与 UI/Game/HtmlManager.cs
// （HtmlSubString/HtmlLength/Unescape），逐条对照本仓库
// test/data/functions/{EXISTFILE,ISDEFINED,REGEXPMATCH,HTML_STRINGLEN,
// HTML_SUBSTRING,HTML_STRINGLINES}.md。路径规范化（`Utils.GetValidPath`）
// 与 LOADTEXT/SAVETEXT 共用一处实现：GameData/ast/text_path.h。
//
// 与 C# 的已知差异（都在**度量**这一层，语义/接口一致）：
//   · C# 用真实字体度量（`GlobalStatic.Console.StrMeasure`）量像素；本移植没有
//     在这条路径上持有字体度量器，改用**字符宽模型**：半角 1、全角 2
//     （「半角字符宽」单位）。这正是 C# 把行宽参数 `length * FontSize / 2`
//     换算成像素时隐含的模型（默认字体下半角 = FontSize/2 像素、全角 = FontSize 像素），
//     对等宽字体的 era 游戏一致；比例字体会偏。
//   · `<img>` / `<shape>` 的宽度在 C# 里来自图像本身；离线求值拿不到，按**零宽**
//     处理（只影响这两个标签参与的折行位置，不影响文本行数）。
// ---------------------------------------------------------------------------

#include "print_template.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

#include "fork_extension.h"
#include "fork_support.h"
#include "extension_registry.h"
#include "expression_ast.h"
#include "text_path.h"
#include "variable_storage.h"

namespace forkem {

namespace {

using namespace forksupport;

// ---------------------------------------------------------------------------
// HTML 实体还原（对齐 C# HtmlManager.Unescape:685）
//   · 具名：nbsp/amp/gt/lt/quot/apos；其余非 `#` 开头一律非法（C# 抛 CodeEE）
//   · 数字：`&#nn;`（10 进制）/ `&#xnn;`（16 进制），范围 0..0xFFFF
// C# 对非法实体抛 CodeEE（`&` 没有 ';'、`&;`、非法字符引用……）；这里返回原串并
// 留痕，让「HTML 串里的裸 &」不至于把脚本打断（同一容错口径见 fork_xml 的说明）。
// ---------------------------------------------------------------------------
QString htmlUnescape(const QString& input) {
    if (!input.contains(QLatin1Char('&'))) return input;
    QString out;
    out.reserve(input.size());
    int index = 0;
    while (index < input.size()) {
        const int found = input.indexOf(QLatin1Char('&'), index);
        if (found < 0) { out += input.mid(index); break; }
        if (found > index) out += input.mid(index, found - index);
        const int semi = input.indexOf(QLatin1Char(';'), found);
        if (semi <= found + 1) {           // `&;` / 未闭合：原样保留，跳过这个 '&'
            out += QLatin1Char('&');
            index = found + 1;
            continue;
        }
        const QString word = input.mid(found + 1, semi - found - 1);
        const QString lower = word.toLower();
        index = semi + 1;
        if (lower == QLatin1String("nbsp")) { out += QLatin1Char(' '); continue; }
        if (lower == QLatin1String("amp"))  { out += QLatin1Char('&'); continue; }
        if (lower == QLatin1String("gt"))   { out += QLatin1Char('>'); continue; }
        if (lower == QLatin1String("lt"))   { out += QLatin1Char('<'); continue; }
        if (lower == QLatin1String("quot")) { out += QLatin1Char('"'); continue; }
        if (lower == QLatin1String("apos")) { out += QLatin1Char('\''); continue; }
        if (!lower.startsWith(QLatin1Char('#'))) { out += QLatin1Char('&') + word + QLatin1Char(';'); continue; }
        QString digits = lower.mid(1);
        int base = 10;
        if (digits.startsWith(QLatin1Char('x'))) { base = 16; digits = digits.mid(1); }
        bool ok = false;
        const uint code = digits.toUInt(&ok, base);
        if (!ok || code > 0xFFFF) { out += QLatin1Char('&') + word + QLatin1Char(';'); continue; }
        out += QChar(static_cast<ushort>(code));
    }
    return out;
}

// 字符的显示宽度（「半角字符宽」单位）：半角 1 / 全角 2。
// 与 C# `StrMeasure`（半角 = FontSize/2 px、全角 = FontSize px）同一模型。
int charWidth(QChar c) {
    const ushort u = c.unicode();
    // ASCII / 半角片假名 / 半角标点：1；其余（含 CJK、全角标点、汉字）按 2。
    if (u < 0x0080) return 1;
    if (u >= 0xFF61 && u <= 0xFF9F) return 1;    // 半角片假名
    if (u >= 0xFF00 && u <= 0xFF60) return 2;    // 全角标点/全角形
    if (u < 0x02B0) return 1;                    // 拉丁扩展（é 等）按半角
    return 2;
}

// ---------------------------------------------------------------------------
// HtmlManager.HtmlSubString 的最小实现（HTML_STRINGLEN/SUBSTRING/STRINGLINES 共用）
//
// 对齐要点（UI/Game/HtmlManager.cs:83）：
//   · 行宽单位是**半角字符宽**；正文逐字符累计，放不下就在该字符前断行；
//   · `<br>` 强制断行（该标签本身不留在本行、也不留给下一行 —— C# 用 `delbr` 跳过它）；
//   · `<b>/<i>/<s>` 等样式标签只在行尾补闭合、在下一行开头补开始
//     （C# 用 beginStack/endStack 保存，其余标签只入栈不入样式栈）；
//   · 本行一个字符都放不下时返回 ["", 原串]（调用方据此停止，不会死循环）。
// ---------------------------------------------------------------------------
struct HtmlLine {
    QString line;    // 本行（已补上闭合标签）
    QString rest;    // 剩余（已补上开始标签）
};

HtmlLine htmlSubString(const QString& input, int length) {
    HtmlLine result;
    result.rest = input;
    if (input.isEmpty() || length <= 0) return result;

    const QString str = htmlUnescape(input);
    QStringList openTags;      // 仍未闭合的标签（原样保存，含属性）
    QStringList openNames;     // 对应的标签名（判断 </x> 配对只需名字）
    int remaining = length;
    int pos = 0;
    int consumedEnd = -1;      // 本行在 str 中的结束位置（不含）
    bool broke = false;
    bool brokeByChar = false;

    while (pos < str.size()) {
        if (str.at(pos) == QLatin1Char('<')) {
            const int close = str.indexOf(QLatin1Char('>'), pos);
            if (close < 0) { pos = str.size(); break; }          // 残缺标签：按文本吃到底
            if (close == pos + 1 || str.at(pos + 1) == QLatin1Char('/')) {
                // 闭合标签（或 `<>`）：弹出最近的同名开始标签（栈式配对）
                const int nameBegin = str.at(pos + 1) == QLatin1Char('/') ? pos + 2 : pos + 1;
                const QString name = str.mid(nameBegin, close - nameBegin).trimmed();
                for (int i = openNames.size() - 1; i >= 0; --i) {
                    if (openNames.at(i).compare(name, Qt::CaseInsensitive) == 0) {
                        openNames.removeAt(i);
                        openTags.removeAt(i);
                        break;
                    }
                }
                pos = close + 1;
                continue;
            }
            int space = str.indexOf(QLatin1Char(' '), pos);
            if (space < 0 || space > close) space = close;
            const QString tag = str.mid(pos + 1, space - pos - 1).toLower();
            if (tag == QLatin1String("br")) {                    // 强制断行：<br> 不留在任何一侧
                consumedEnd = pos;
                broke = true;
                pos = close + 1;
                break;
            }
            if (tag == QLatin1String("img") || tag == QLatin1String("shape")) {
                // C#：整块按图像宽度计入；离线取不到图像宽度 -> 零宽（见文件头说明）
                pos = close + 1;
                continue;
            }
            openTags.append(str.mid(pos, close - pos + 1));      // 含 < > 的标签原文
            openNames.append(tag);
            pos = close + 1;
            continue;
        }
        const int w = charWidth(str.at(pos));
        if (w > remaining) {                                     // 放不下：本行在此结束
            consumedEnd = pos;
            broke = true;
            brokeByChar = true;
            break;
        }
        remaining -= w;
        ++pos;
    }
    if (!broke) consumedEnd = str.size();                        // 全部装进本行

    if (brokeByChar && consumedEnd == 0) {
        // 一个字符都放不下：C# 返回 ["", 原串]（调用方停止计数，避免死循环）
        return result;
    }
    result.line = str.left(consumedEnd);
    for (int i = openNames.size() - 1; i >= 0; --i)
        result.line += QStringLiteral("</") + openNames.at(i) + QLatin1Char('>');
    QString prefix;
    for (const QString& tag : openTags) prefix += tag;
    result.rest = prefix + str.mid(broke ? (brokeByChar ? consumedEnd : pos) : str.size());
    return result;
}

// HTML 串**第一行**的显示宽度（半角字符宽单位）。
// C# `HtmlLength` 量的是 `Html2DisplayLine(s)[0]`（第一显示行）的像素宽；
// 本移植按字符宽模型给出一致的结果（见文件头）。
int htmlFirstLineWidth(const QString& input) {
    const QString str = htmlUnescape(input);
    int width = 0;
    for (int i = 0; i < str.size(); ++i) {
        if (str.at(i) == QLatin1Char('<')) {
            const int close = str.indexOf(QLatin1Char('>'), i);
            if (close < 0) break;
            const QString tag = str.mid(i + 1, qMax(0, close - i - 1)).trimmed().toLower();
            if (tag == QLatin1String("br")) break;               // 第一行到此为止
            i = close;
            continue;
        }
        width += charWidth(str.at(i));
    }
    return width;
}

// ---------------------------------------------------------------------------
// 正则缓存（对齐 C# RegexFactory.GetRegex：同一 pattern 不重复编译）
// ---------------------------------------------------------------------------
QRegularExpression cachedRegex(const QString& pattern, bool& ok) {
    static QHash<QString, QRegularExpression> cache;
    const auto it = cache.constFind(pattern);
    if (it != cache.constEnd()) { ok = it.value().isValid(); return it.value(); }
    QRegularExpression re(pattern);
    // C#：RegexOptions 默认（大小写敏感、. 不匹配换行）；不做 Qt 的额外优化裁剪。
    if (cache.size() < 256) cache.insert(pattern, re);
    ok = re.isValid();
    return re;
}

} // namespace

// ---------------------------------------------------------------------------
// 注册入口（只由 registerForkExtensions 调用）
// ---------------------------------------------------------------------------
namespace {

// GETVAR / GETVARS / SETVAR 的公共前置：名字文本 -> 变量节点。
// 对齐 C#（Creator.Method.cs:265/294/503）的三行 —— 运行期重新词法分析 +
// 归约，结果必须是**单个变量项**，否则 CodeEE（`"FOO + 1"` / `"FOO:1+1"` 都不算）。
// 解析服务由解析表提供（作用域 = 当前执行函数、不折叠 `#DIM CONST`）。
//
// **必须返回共享指针**：运行期解析出来的节点不属于任何已装载的行，调用方是它
// 唯一的所有者 —— 返回裸指针会在函数返回时把节点释放掉，随后求值就是对已析构
// 对象做虚调用（实测 "pure virtual method called" 崩溃，见 FL3.8 的 GETVAR）。
QSharedPointer<ExpressionNode> resolveVariableReference(const ExtensionRegistry::Services& sv,
                                                        const QString& name) {
    if (!sv.parseVariableReference) {
        throw std::runtime_error(QStringLiteral("GETVAR/SETVAR 缺少变量名解析服务").toStdString());
    }
    const QSharedPointer<ExpressionNode> node = sv.parseVariableReference(name);
    if (!node || node->kind() != NodeKind::Variable) {
        // C# CodeEE(trerror.IsNotVar)：`"{0}"が変数ではありません`
        throw std::runtime_error(QStringLiteral("\"%1\"不是变量").arg(name).toStdString());
    }
    return node;
}

// 该变量的属性位掩码；provider 缺失时返回 -1（「未知」—— 跳过存在性/类型检查，
// 只服务缺省装配的单元测试）。
//
// **必须传裸变量名**（`node->name()`，已剥掉下标）：C# 的 GetVariableToken 在
// 归约下标时是按 `key`/`subKey` 分开查的（`"COUNT:1"` -> key="COUNT"），
// 而字典键是裸名 —— 传整串会一律查不到（EXISTVAR 是**唯一**例外：
// 它把名字整串当键用，`EXISTVAR("COUNT:1")` 在 C# 里就是 0）。
int variableFlagsOf(const ExtensionRegistry::Services& sv, const QString& name) {
    return sv.variableFlags ? sv.variableFlags(name) : -1;
}

} // namespace

void registerEmExtensions(ExtensionRegistry& ext)
{
    // ---- GETVAR / GETVARS（Creator.cs:237/238 / Creator.Method.cs:265/294）----
    //   int  GETVAR(str 变量名)  —— 必须解析成**变量项**、且是整数/常量变量
    //   str  GETVARS(str 变量名) —— 同上，字符串侧
    // 读值走 evaluateNode（求值器的变量读取路径已覆盖常数表、系统变量、
    // 角色变量、私有变量、形参别名）——「名字文本 -> 值」只剩解析与类型检查两件事。
    ext.regExpr(QStringLiteral("GETVAR"), OperandType::Int, 1, 1,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const ExtensionRegistry::Services& sv = ext.services();
            const QString name = stringText(a.value(0));
            const QSharedPointer<ExpressionNode> ref = resolveVariableReference(sv, name);
            const int flags = variableFlagsOf(sv, static_cast<const VariableNode*>(ref.data())->name());
            if (flags >= 0 && (flags & 1) == 0) {
                throw std::runtime_error(QStringLiteral("\"%1\"不是整数型变量").arg(name).toStdString());
            }
            if (!sv.evaluateNode) { out = QVariant::fromValue<qint64>(0); return true; }
            out = QVariant::fromValue<qint64>(sv.evaluateNode(ref.data()).toLongLong());
            return true;
        });

    ext.regExpr(QStringLiteral("GETVARS"), OperandType::Str, 1, 1,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const ExtensionRegistry::Services& sv = ext.services();
            const QString name = stringText(a.value(0));
            const QSharedPointer<ExpressionNode> ref = resolveVariableReference(sv, name);
            const int flags = variableFlagsOf(sv, static_cast<const VariableNode*>(ref.data())->name());
            if (flags >= 0 && (flags & 2) == 0) {
                throw std::runtime_error(QStringLiteral("\"%1\"不是字符串型变量").arg(name).toStdString());
            }
            out = sv.evaluateNode ? sv.evaluateNode(ref.data()) : QVariant(QString());
            return true;
        });

    // ---- SETVAR（Creator.cs:239 / Creator.Method.cs:503）----
    //   写**单个**变量；常量与「不是变量」都报 IsNotVar。
    //   C# 的两条类型报错**与直觉相反**（Lang.cs:1144/1145 + Creator.Method.cs:526/532）：
    //     字符串变量被赋非字符串 -> IsNotInt（「…が整数型ではありません」）
    //     整数变量被赋字符串   -> IsNotStr（「…が文字列型ではありません」）
    //   这里如实照搬（见 test/data/functions/SETVAR.md 的备注）。
    ext.regExpr(QStringLiteral("SETVAR"), OperandType::Int, 2, 2,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const ExtensionRegistry::Services& sv = ext.services();
            const QString name = stringText(a.value(0));
            const QSharedPointer<ExpressionNode> ref = resolveVariableReference(sv, name);
            const int flags = variableFlagsOf(sv, static_cast<const VariableNode*>(ref.data())->name());
            if (flags >= 0 && (flags == 0 || (flags & 4) != 0)) {
                // 不存在 / 常量：C# 对两者都走 IsNotVar（`Identifier == null || IsConst`）
                throw std::runtime_error(QStringLiteral("\"%1\"不是变量").arg(name).toStdString());
            }
            const bool valueIsStr = isStringValue(a.value(1));
            const bool targetIsStr = flags >= 0 ? (flags & 2) != 0
                                                : (ref->valueType() == OperandType::Str);
            if (targetIsStr != valueIsStr) {
                throw std::runtime_error(
                    (targetIsStr ? QStringLiteral("\"%1\"不是整数型变量")
                                 : QStringLiteral("\"%1\"不是字符串型变量")).arg(name).toStdString());
            }
            if (!sv.assignVariable || !sv.assignVariable(*ref, a.value(1))) {
                throw std::runtime_error(
                    QStringLiteral("SETVAR 无法写入 \"%1\"").arg(name).toStdString());
            }
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    // ---- EXISTVAR（Creator.cs:223 / Creator.Method.cs:322）----
    //   位掩码 1 整型 / 2 字符串 / 4 常量 / 8 二维 / 16 三维（0 = 不存在）。
    ext.regExpr(QStringLiteral("EXISTVAR"), OperandType::Int, 1, 1,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const int flags = variableFlagsOf(ext.services(), stringText(a.value(0)));
            out = QVariant::fromValue<qint64>(flags < 0 ? 0 : flags);
            return true;
        });

    // ---- EXISTFILE（Creator.cs:222 / Creator.Method.cs:1255）----
    // GetValidPath 路径（相对游戏目录、'/'→'\'、删 "..\["、绝对路径拒绝）+
    // File.Exists（**目录不算存在**：C# File.Exists 对目录返回 false）。
    ext.regExpr(QStringLiteral("EXISTFILE"), OperandType::Int, 1, 1,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const QString base = ext.services().exeDir ? ext.services().exeDir() : QString();
            out = QVariant::fromValue<qint64>(
                erapath::existsUnderBase(base, stringText(a.value(0))) ? 1 : 0);
            return true;
        });

    // ---- ISDEFINED（Creator.cs:224 / Creator.Method.cs:137）----
    // 只查宏（`#DEFINE`），不查变量/函数。C# 按 Config.IgnoreCase 决定比较方式；
    // 本移植的变量/函数名一律大小写不敏感（见 variable_storage.h 的说明），
    // 宏这里同样按大小写不敏感的子集匹配（配置为「区分大小写」时可能多命中，
    // 属已知偏差，错误方向安全）。
    ext.regExpr(QStringLiteral("ISDEFINED"), OperandType::Int, 1, 1,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const QString name = stringText(a.value(0));
            qint64 found = 0;
            const ExtensionRegistry::Services& sv = ext.services();
            if (sv.enumMacroNames) {
                for (const QString& macro : sv.enumMacroNames()) {
                    if (macro.compare(name, Qt::CaseInsensitive) == 0) { found = 1; break; }
                }
            }
            out = QVariant::fromValue<qint64>(found);
            return true;
        });

    // ---- REGEXPMATCH（Creator.cs:244 / Creator.Method.cs:689）----
    // 形式 1：REGEXPMATCH(str, pattern{, int 输出到 RESULTS})      —— 2~3 参
    // 形式 2：REGEXPMATCH(str, pattern, int 分组数变量, str 数组)  —— 恰好 4 参
    //（扩展函数的实参形态只声明返回类型 + 个数区间，逐位 ref 校验在下面按
    //  「实参节点是不是变量」做 —— 与 XML_GET/ENUM* 同一口径）
    ext.regExpr(QStringLiteral("REGEXPMATCH"), OperandType::Int, 2, 4,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes, QVariant& out) {
            const QString base = stringText(a.value(0));
            const QString pattern = stringText(a.value(1));
            bool ok = false;
            const QRegularExpression re = cachedRegex(pattern, ok);
            if (!ok) {
                // 对齐 C# CodeEE(InvalidRegexArg)：模式非法
                throw std::runtime_error(
                    QStringLiteral("REGEXPMATCH 函数: 第 2 个实参不是合法的正则表达式: %1")
                        .arg(re.errorString()).toStdString());
            }
            const QRegularExpressionMatchIterator it = re.globalMatch(base);
            // 先数个数（C# 返回 MatchCollection.Count）
            int count = 0;
            {
                auto probe = it;
                while (probe.hasNext()) { probe.next(); ++count; }
            }
            const int groupCount = re.captureCount() + 1;        // 含第 0 组
            VariableStorage* st = ext.services().storage;
            if (a.size() == 3) {
                if (a.value(2).toLongLong() != 0 && st) {
                    st->setSystemVariable(QStringLiteral("RESULT"), 1, groupCount);   // RESULT:1
                    if (count > 0) {
                        // 顺序 = 「每个匹配 × 每个分组」（含第 0 组 = 整个匹配），
                        // 数组放不下即截断（C# Output 的 `idx >= values.Length` 返回）
                        const int cap = arrayCapacity(st, QStringLiteral("RESULTS"));
                        int idx = 0;
                        auto matches = re.globalMatch(base);
                        while (matches.hasNext()) {
                            const QRegularExpressionMatch m = matches.next();
                            for (int g = 0; g < groupCount; ++g) {
                                if (cap > 0 && idx >= cap) break;
                                writeStr(st, QStringLiteral("RESULTS"), idx++, m.captured(g));
                            }
                        }
                    }
                }
            } else if (a.size() >= 4) {
                // 形式 2：第 3 实参 = 整数变量（分组数），第 4 实参 = 字符串数组变量
                const QString countName = nodes.size() >= 3 ? refTargetName(nodes.at(2)) : QString();
                const QString arrayName = nodes.size() >= 4 ? refTargetName(nodes.at(3)) : QString();
                if (countName.isEmpty() || arrayName.isEmpty()) {
                    throw std::runtime_error(
                        "REGEXPMATCH 函数: 第 3、第 4 个实参必须是变量（分组数变量、结果数组变量）");
                }
                if (st) {
                    writeInt(st, countName, 0, groupCount);
                    if (count > 0) {
                        // C# 忽略数组变量上写的下标，从头整片写（Identifier.GetArray()）
                        const int cap = arrayCapacity(st, arrayName);
                        int idx = 0;
                        auto matches = re.globalMatch(base);
                        while (matches.hasNext()) {
                            const QRegularExpressionMatch m = matches.next();
                            for (int g = 0; g < groupCount; ++g) {
                                if (cap > 0 && idx >= cap) break;
                                writeStr(st, arrayName, idx++, m.captured(g));
                            }
                        }
                    }
                }
            }
            out = QVariant::fromValue<qint64>(count);
            return true;
        });

    // ---- HTML_STRINGLEN（Creator.cs:218 / Creator.Method.cs:31）----
    //   第 2 实参 == 0 -> 「半角字符宽」单位（C#：`2*len/FontSize`，进位）
    //   第 2 实参 != 0 -> **像素**（C#：`HtmlLength` 的返回值）
    ext.regExpr(QStringLiteral("HTML_STRINGLEN"), OperandType::Int, 2, 2,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const int units = htmlFirstLineWidth(stringText(a.value(0)));
            const qint64 mode = a.value(1).toLongLong();
            if (mode == 0) {
                out = QVariant::fromValue<qint64>(units);
                return true;
            }
            const int font = (ext.services().fontSize ? ext.services().fontSize() : 14);
            // 半角 = FontSize/2 像素、全角 = FontSize 像素（字符宽模型的一半就是字号）
            out = QVariant::fromValue<qint64>(static_cast<qint64>(units) * (font / 2));
            return true;
        });

    // ---- HTML_SUBSTRING（Creator.cs:219 / Creator.Method.cs:647）----
    //   按行宽切出**第一行**：RESULTS:0 = 本行、RESULTS:1 = 剩余，返回本行。
    ext.regExpr(QStringLiteral("HTML_SUBSTRING"), OperandType::Str, 2, 2,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const HtmlLine parts = htmlSubString(stringText(a.value(0)),
                                                static_cast<int>(a.value(1).toLongLong()));
            VariableStorage* st = ext.services().storage;
            if (st) {
                const int cap = arrayCapacity(st, QStringLiteral("RESULTS"));
                if (cap <= 0 || cap >= 1) writeStr(st, QStringLiteral("RESULTS"), 0, parts.line);
                if (cap <= 0 || cap >= 2) writeStr(st, QStringLiteral("RESULTS"), 1, parts.rest);
            }
            out = parts.line;
            return true;
        });

    // ---- HTML_STRINGLINES（Creator.cs:220 / Creator.Method.cs:666）----
    //   反复切分直到剩余为空，统计行数（`<br>` 也计一次）。
    ext.regExpr(QStringLiteral("HTML_STRINGLINES"), OperandType::Int, 2, 2,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            QString str = stringText(a.value(0));
            if (str.isEmpty()) { out = QVariant::fromValue<qint64>(0); return true; }
            const int length = static_cast<int>(a.value(1).toLongLong());
            qint64 lines = 0;
            while (!str.isEmpty()) {
                const HtmlLine parts = htmlSubString(str, length);
                if (parts.rest == str) break;      // 放不下任何字符：C# 会死循环，这里停下
                str = parts.rest;
                ++lines;
                if (lines > 1000000) break;        // 安全网
            }
            out = QVariant::fromValue<qint64>(lines);
            return true;
        });
}

} // namespace forkem
