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
#include "erb_preprocessor.h"
#include "ast/strform_parser.h"

namespace {

// 「文件:行: 」前缀 —— 预处理警告的位置串。
// **必须用拼接，不能用链式 .arg()**：fileName 在 Android 上是 content:// URI，
// 路径里带 %2F/%3A 百分号转义，QString::arg 会把它们当占位符吃掉
//（真机实测：所有 %2F 被替换成 "<行号>F"、%3A 变 "<列号>A"）；桌面路径里
// 若有字面 %2/%3 同样中招。见 ScriptPosition::toString() / ParseDiagnostic::positionText()
// —— 三处同因，一起改。
inline QString warnPrefix(const QString& fileName, int line) {
    return fileName + QLatin1Char(':') + QString::number(line) + QStringLiteral(": ");
}

// 读取单个标识符（字母/数字/下划线/非 ASCII），与 C# LexicalAnalyzer.ReadSingleIdentifier 对齐
QString readIdentifier(const QString& s, int& i) {
    const int start = i;
    while (i < s.size()) {
        const QChar c = s.at(i);
        if (c.isLetterOrNumber() || c == QLatin1Char('_') || c.unicode() > 127) {
            ++i;
        } else {
            break;
        }
    }
    return s.mid(start, i - start);
}

// 剥离宏替换体里的行尾 ';' 注释（Emuera 注释语义），尊重字符串字面量。
//
// 为什么必须在**收集宏定义时**就剥掉：
//   C# Emuera 的 #DEFINE 替换体是词法 token 流（HeaderFileLoader.
//   analyzeSharpDefine -> LexicalAnalyzer.Analyse），';' 在词法阶段即被当作
//   注释终止，**永远进不了宏体**；而本移植版是文本级替换，若把 `;注释`
//   一并收进 body，展开到某条语句中间时，就会从该 ';' 起把**本行剩余代码
//   整段注释掉**。实测 _SYSTEM_RPG.ERH：
//     #DEFINE DEFRPG_EVENT_DB TRPG_FLAG_EVENT:(FLAG:RPG_進行中イベント) ;イベント
//   `DEFRPG_EVENT_DB:0 = 0001` 展开成
//     `TRPG_FLAG_EVENT:(FLAG:RPG_進行中イベント) ;イベント:0 = 0001`
//   被截断为 `TRPG_FLAG_EVENT:(FLAG:RPG_進行中イベント)`，于是报
//   「未识别的指令: TRPG_FLAG_EVENT」+「表达式无法归约: :(FLAG:…)」，
//   并且 `= 0001` 的赋值被静默丢弃（语义错误，不只是噪音）。
//
// 语义需与 ast_builder.cpp 的 stripLineComment 保持一致：宏体展开到使用处
// 后仍要过那一关，两处判定必须同源（含 `'=` 不是引号的特例）。
QString stripMacroBodyComment(const QString& text) {
    QChar quote;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (!quote.isNull()) {
            // Escaped quotes belong to the literal; they cannot expose an
            // entity semicolon or the remaining HTML to comment stripping.
            if (c == QLatin1Char('\\') && i + 1 < text.size()) { ++i; continue; }
            if (c == quote) quote = QChar();
            continue;
        }
        // FORM contains its own quoted expressions. Skip the whole load-time
        // span so their delimiters never expose entity/comment semicolons.
        if (c == QLatin1Char('@') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
            i = StrFormParser::expressionSpanEnd(text, i) - 1;
            continue;
        }
        if (c == QLatin1Char('"')) { quote = c; continue; }
        if (c == QLatin1Char('\'')) {
            // `'=` 是字符串赋值运算符，不是引号
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('=')) continue;
            quote = c;
            continue;
        }
        if (c == QLatin1Char(';')) return text.left(i);
    }
    return text;
}

// 预处理状态机（对齐 C# ErbLoader.PPState）
struct PPState {
    bool skip = false;
    bool done = false;
    bool disabled = false;
    QStringList match;    // 栈：期望的闭合指令
    QList<bool> disabledStack;
    QList<bool> doneStack;

    void addKeyWord(const QString& token, const QString& token2,
                    const QSet<QString>& macros, bool debugMode,
                    const QString& fileName, int line, QStringList* warnings) {
        const auto warn = [&](const QString& text) {
            if (warnings) {
                warnings->append(warnPrefix(fileName, line) + text);
            }
        };
        const bool hasToken2 = !token2.isEmpty();
        const QString upper = token.toUpper();

        if (upper == QLatin1String("SKIPSTART")) {
            if (hasToken2) { warn(QStringLiteral("[SKIPSTART] 有多余参数")); return; }
            if (skip) { warn(QStringLiteral("[SKIPSTART] 重复使用")); return; }
            match.append(QStringLiteral("SKIPEND"));
            disabledStack.append(disabled);
            doneStack.append(done);
            skip = true;
            disabled = true;
            done = false;
        } else if (upper == QLatin1String("IF_DEBUG")) {
            match.append(QStringLiteral("ELSEIF"));
            disabledStack.append(disabled);
            doneStack.append(done);
            disabled = !debugMode;
            done = !disabled;
        } else if (upper == QLatin1String("IF_NDEBUG")) {
            match.append(QStringLiteral("ELSEIF"));
            disabledStack.append(disabled);
            doneStack.append(done);
            disabled = debugMode;
            done = !disabled;
        } else if (upper == QLatin1String("IF")) {
            if (!hasToken2) { warn(QStringLiteral("[IF] 缺少参数")); return; }
            match.append(QStringLiteral("ELSEIF"));
            disabledStack.append(disabled);
            doneStack.append(done);
            disabled = !macros.contains(token2);
            done = !disabled;
        } else if (upper == QLatin1String("ELSEIF")) {
            if (!hasToken2) { warn(QStringLiteral("[ELSEIF] 缺少参数")); return; }
            if (match.isEmpty() || match.takeLast() != QLatin1String("ELSEIF")) {
                warn(QStringLiteral("不合适的 [ELSEIF]"));
                return;
            }
            match.append(QStringLiteral("ELSEIF"));
            disabled = done || !macros.contains(token2);
            done = done || !disabled;
        } else if (upper == QLatin1String("ELSE")) {
            if (hasToken2) { warn(QStringLiteral("[ELSE] 有多余参数")); return; }
            if (match.isEmpty() || match.takeLast() != QLatin1String("ELSEIF")) {
                warn(QStringLiteral("不合适的 [ELSE]"));
                return;
            }
            match.append(QStringLiteral("ENDIF"));
            disabled = done;
            done = true;
        } else if (upper == QLatin1String("SKIPEND")) {
            if (hasToken2) { warn(QStringLiteral("[SKIPEND] 有多余参数")); return; }
            const QString m = match.isEmpty() ? QString() : match.takeLast();
            if (m != QLatin1String("SKIPEND")) {
                warn(QStringLiteral("[SKIPEND] 与 [SKIPSTART] 不对应"));
                return;
            }
            skip = false;
            disabled = disabledStack.isEmpty() ? false : disabledStack.takeLast();
            done = doneStack.isEmpty() ? false : doneStack.takeLast();
        } else if (upper == QLatin1String("ENDIF")) {
            if (hasToken2) { warn(QStringLiteral("[ENDIF] 有多余参数")); return; }
            const QString m = match.isEmpty() ? QString() : match.takeLast();
            if (m != QLatin1String("ENDIF") && m != QLatin1String("ELSEIF")) {
                warn(QStringLiteral("没有对应 [IF] 的 [ENDIF]"));
                return;
            }
            disabled = disabledStack.isEmpty() ? false : disabledStack.takeLast();
            done = doneStack.isEmpty() ? false : doneStack.takeLast();
        } else if (upper == QLatin1String("DEFINE")) {
            // 宏定义由 collectDefines 处理；此处不参与开关
        } else {
            warn(QStringLiteral("无法识别的预处理器 [%1]").arg(token));
        }
        if (skip) disabled = true;
    }

    void fileEnd(const QString& fileName, int line, QStringList* warnings) {
        if (match.isEmpty()) return;
        QString m = match.takeLast();
        if (m == QLatin1String("ELSEIF")) m = QStringLiteral("ENDIF");
        if (warnings) {
            warnings->append(warnPrefix(fileName, line) + QStringLiteral("缺少 [") + m + QLatin1Char(']'));
        }
    }
};

} // namespace

ErbPreprocessor::RenameMap ErbPreprocessor::parseRenameCsv(const QString& content) {
    RenameMap map;
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';'))) continue;

        // 以 "\," 为分隔（转义逗号），最后一个分片里必须再含一个 ','
        QStringList baseTokens = line.split(QStringLiteral("\\,"));
        if (baseTokens.isEmpty()) continue;
        QString& last = baseTokens.last();
        if (!last.contains(QLatin1Char(','))) continue;
        const QStringList lastParts = last.split(QLatin1Char(','));
        last = lastParts.value(0);
        const QString value = baseTokens.join(QLatin1Char(',')).trimmed();
        const QString key = QStringLiteral("[[%1]]").arg(lastParts.value(1).trimmed());
        map.insert(key, value);   // 右为 ERB 中的表记，左为替换目标
    }
    return map;
}

QSet<QString> ErbPreprocessor::collectDefines(const QString& content) {
    const MacroTable table = collectMacroTable(content);
    QSet<QString> macros;
    for (auto it = table.constBegin(); it != table.constEnd(); ++it)
        macros.insert(it.key());
    return macros;
}

ErbPreprocessor::MacroTable ErbPreprocessor::collectMacroTable(const QString& content) {
    MacroTable table;
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        QString s = raw.trimmed();
        if (s.endsWith(QLatin1Char('\r'))) s.chop(1);
        if (!s.startsWith(QLatin1Char('#'))) continue;
        s = s.mid(1).trimmed();
        if (!s.startsWith(QLatin1String("DEFINE"), Qt::CaseInsensitive)) continue;
        s = s.mid(6);
        int i = 0;
        while (i < s.size() && s.at(i).isSpace()) ++i;
        const QString name = readIdentifier(s, i);
        if (name.isEmpty()) continue;
        // C# analyzeSharpDefine：带参宏的 '(' 必须紧贴宏名（中间不允许空白）
        MacroDef def;
        if (i < s.size() && s.at(i) == QLatin1Char('(')) {
            const int close = s.indexOf(QLatin1Char(')'), i);
            if (close > i) {
                for (const QString& p : s.mid(i + 1, close - i - 1).split(QLatin1Char(','))) {
                    const QString t = p.trimmed();
                    if (!t.isEmpty()) def.params.append(t);
                }
                s = s.mid(close + 1);
            }
        } else {
            // 对象宏：替换体从宏名之后开始（跳过宏名本身）。
            // 此前未剥离宏名，导致 body="DOC_MAC_27 33"，展开成
            // "DOC_MAC_27 33 33 …"（自引用膨胀），完全错误。
            s = s.mid(i);
        }
        // 行尾 `;注释` 不属于替换体（见 stripMacroBodyComment 的说明）。
        def.body = stripMacroBodyComment(s).trimmed();
        table.insert(name, def);
    }
    return table;
}

namespace {

// 标识符边界判定（前后均不得是标识符字符）
bool isIdentChar(QChar c) {
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c.unicode() > 127;
}

} // namespace

QString ErbPreprocessor::expandMacros(const QString& line, MacroAbort* abort) const {
    if (m_macroTable.isEmpty()) return line;
    QString text = line;
    int expansions = 0;   // 本行累计展开次数（对齐 C# MAX_EXPAND_MACRO）
    for (int pass = 0; pass < kMaxMacroPasses; ++pass) {
        QString out;
        int i = 0;
        bool changed = false;
        while (i < text.size()) {
            // ---- 展开体积上限（fuel）----
            // 自引用宏（如头文件里的 `#DEFINE A A A`）每轮翻倍：32 轮上限只限了**轮数**，
            // 不限**体积** → 2^32 级膨胀，装载期卡死（实测 15s 不返回）。
            // 给单行展开结果设上限，超出即停止并保留原行（宁可留未展开的宏名，也不卡死）。
            if (out.size() > kMaxExpandedLineLength) {
                if (abort) *abort = MacroAbort::Size;   // 安全网：体积
                return line;
            }
            const QChar c = text.at(i);
            if (c == QLatin1Char('"')) {
                // 字符串字面量整体拷贝（C# 词法级展开不会进入字符串 token）
                out += c;
                ++i;
                while (i < text.size()) {
                    out += text.at(i);
                    const bool isEnd = text.at(i) == QLatin1Char('"');
                    ++i;
                    if (isEnd) break;
                }
                continue;
            }
            if (!isIdentChar(c)) { out += c; ++i; continue; }
            int j = i;
            while (j < text.size() && isIdentChar(text.at(j))) ++j;
            const QString id = text.mid(i, j - i);
            auto it = m_macroTable.constFind(id);
            if (it == m_macroTable.constEnd()) {
                out += id;
                i = j;
                continue;
            }
            const MacroDef& def = it.value();
            if (++expansions > kMaxExpandMacro) {   // 主防线：单行展开次数
                if (abort) *abort = MacroAbort::Count;
                return line;
            }
            if (def.params.isEmpty()) {
                out += def.body;
                i = j;
                changed = true;
                continue;
            }
            // 带参宏：宏名后必须紧跟 '('（C# hasArg 规则）
            int k = j;
            while (k < text.size() && text.at(k).isSpace()) ++k;
            if (k >= text.size() || text.at(k) != QLatin1Char('(')) {
                out += id;
                i = j;
                continue;
            }
            // 顶层逗号切分实参
            QStringList args;
            int depth = 0;
            int start = ++k;
            while (k < text.size()) {
                const QChar ck = text.at(k);
                if (ck == QLatin1Char('(')) ++depth;
                else if (ck == QLatin1Char(')')) {
                    if (depth == 0) break;
                    --depth;
                } else if (ck == QLatin1Char(',') && depth == 0) {
                    args.append(text.mid(start, k - start));
                    start = k + 1;
                }
                ++k;
            }
            if (k >= text.size()) {   // 括号未闭合：不展开
                out += id;
                i = j;
                continue;
            }
            args.append(text.mid(start, k - start));
            QString body = def.body;
            for (int p = 0; p < def.params.size() && p < args.size(); ++p) {
                const QString& param = def.params.at(p);
                // 仅替换 body 中的完整标识符
                QString replaced;
                int bi = 0;
                while (bi < body.size()) {
                    if (isIdentChar(body.at(bi))) {
                        int bj = bi;
                        while (bj < body.size() && isIdentChar(body.at(bj))) ++bj;
                        const QString bid = body.mid(bi, bj - bi);
                        replaced += (bid == param) ? args.value(p).trimmed() : bid;
                        bi = bj;
                    } else {
                        replaced += body.at(bi);
                        ++bi;
                    }
                }
                body = replaced;
            }
            if (++expansions > kMaxExpandMacro) {
                if (abort) *abort = MacroAbort::Count;
                return line;
            }
            out += body;
            i = k + 1;
            changed = true;
        }
        if (!changed) return out;
        text = out;
    }
    return text;
}

QList<ErbSourceLine> ErbPreprocessed::materialize() const {
    QList<ErbSourceLine> out;
    out.reserve(spans_.size());
    for (int i = 0; i < spans_.size(); ++i) {
        ErbSourceLine line;
        line.physicalLine = spans_.at(i).physicalLine;
        if (spans_.at(i).length > 0) line.text = text_.mid(spans_.at(i).offset, spans_.at(i).length);
        out.append(line);
    }
    return out;
}

void ErbPreprocessed::appendLine(int physicalLine, const QString& text) {
    ErbLineSpan span;
    span.physicalLine = physicalLine;
    span.offset = text_.size();
    span.length = text.size();
    if (span.length > 0) text_ += text;
    spans_.append(span);
}

void ErbPreprocessed::squeeze() {
    text_.squeeze();
}

void ErbPreprocessed::reserve(int lineCount, int charCount) {
    spans_.reserve(lineCount);
    text_.reserve(charCount);
}

ErbPreprocessed ErbPreprocessor::process(const QString& content, QStringList* warnings,
                                        const QString& fileName) const {
    ErbPreprocessed out;

    // 保留全部物理行（含结尾空串），确保 lineNumber 与物理行一致
    QStringList raw = content.split(QLatin1Char('\n'));
    if (!raw.isEmpty() && raw.last().isEmpty()) raw.removeLast();
    // 行数 = 物理行数；字符数按原文预留（预处理后略长于原文：行连接补空格、
    // 宏展开；超出时由 QString 自己再长一次，末尾 squeeze 掉多余容量）。
    out.reserve(raw.size(), content.size());

    // Rename 替换：只按本行出现的 [[…]] 去 QHash 里查，不做全表扫描。
    // 旧实现逐词条 s.contains(key) 是 O(行数 × 词条)：eraH 2030 词条、数万带
    // [[ 的行，在设备单线程预处理阶段 ~10s（真机实测全链路 43.9s → 优化后回到
    // 30s 量级）；语义差异：旧实现按 QHash 序多趟替换，本行替换结果中若再含
    // 别的 [[…]] 键会级联；新实现从替换文本之后继续扫描（不级联）——实际游戏
    // 的 Rename 值是编号/文本（不含 [[），C# 单趟语义与此一致。
    const auto applyRename = [this](const QString& line) {
        if (m_rename.isEmpty()) return line;
        const int first = line.indexOf(QLatin1String("[["));
        if (first < 0 || !line.contains(QLatin1String("]]"))) return line;
        QString s = line;
        int at = first;
        while (at >= 0) {
            const int end = s.indexOf(QLatin1String("]]"), at + 2);
            if (end < 0) break;
            const QString key = s.mid(at, end + 2 - at);   // 含 [[ ]] 的完整键
            const auto it = m_rename.constFind(key);
            if (it != m_rename.cend()) {
                s.replace(at, key.length(), it.value());
                at += it.value().length();   // 跳过替换文本，不级联
            } else {
                at = end + 2;
            }
            at = s.indexOf(QLatin1String("[["), at);
        }
        return s;
    };

    PPState pp;

    for (int idx = 0; idx < raw.size(); ++idx) {
        const int lineNo = idx + 1;

        QString line = applyRename(raw.at(idx));
        // CRLF 文件：split('\n') 会留下行尾 CR，而 C# 的 EraStreamReader 是连行终止符
        // 一起剥掉的。必须在这里去掉 —— 否则它会混进 PRINT 族的字面文本里。
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);

        // 空行 / 纯空白行：C# 直接跳过（不产生逻辑行）
        if (line.isEmpty() || line.trimmed().isEmpty()) {
            out.appendLine(lineNo, QString());   // text 为空 -> Null 行
            continue;
        }

        // 宏替换（C# 在词法级展开；这里在行文本级做，先于一切解析）
        MacroAbort macroAbort = MacroAbort::None;
        line = expandMacros(line, &macroAbort);
        if (macroAbort != MacroAbort::None) {
            const MacroAbort why = macroAbort;
            if (warnings) {
                warnings->append(
                    why == MacroAbort::Count
                        ? warnPrefix(fileName, lineNo)
                              + QStringLiteral("疑似自我引用/循环引用宏（单行展开次数超过 %1），本行按原文保留")
                                    .arg(kMaxExpandMacro)
                        : warnPrefix(fileName, lineNo)
                              + QStringLiteral("宏展开超过 %1 字符上限（疑似自引用膨胀），本行按原文保留")
                                    .arg(kMaxExpandedLineLength));
            }
        }
        if (line.isEmpty() || line.trimmed().isEmpty()) {
            out.appendLine(lineNo, QString());
            continue;
        }

        const QString left = line.trimmed();

        // ---- 行连接 '}'（单个）当作意外，容错跳过 ----
        if (left == QLatin1String("}")) {
            if (warnings) {
                warnings->append(warnPrefix(fileName, lineNo) + QStringLiteral("意外的行连接终止符 '}'"));
            }
            out.appendLine(lineNo, QString());
            continue;
        }

        // ---- 行连接 '{'：吞并到对应的 '}' 行 ----
        if (left == QLatin1String("{")) {
            QString joined;
            int j = idx + 1;
            bool closed = false;
            while (j < raw.size()) {
                const QString l2 = applyRename(raw.at(j));
                const QString t2 = l2.trimmed();
                if (t2 == QLatin1String("}")) { closed = true; ++j; break; }
                if (!t2.isEmpty()) {
                    joined += l2;
                    joined += QLatin1Char(' ');
                }
                ++j;
            }
            if (!closed && warnings) {
                warnings->append(warnPrefix(fileName, lineNo) + QStringLiteral("行连接 '{' 缺少对应的 '}'"));
            }
            out.appendLine(lineNo, pp.disabled ? QString() : joined.trimmed());
            // 被吞并的物理行（含 '}' 行）保持为空
            for (int k = idx + 1; k < j; ++k) out.appendLine(k + 1, QString());
            idx = j - 1;
            continue;
        }

        // ---- 预处理指令 [XXX ...] ----
        if (left.startsWith(QLatin1Char('[')) && !left.startsWith(QLatin1String("[["))) {
            const int close = left.indexOf(QLatin1Char(']'));
            if (close > 0) {
                QString inner = left.mid(1, close - 1).trimmed();
                int i = 0;
                const QString token = readIdentifier(inner, i);
                while (i < inner.size() && inner.at(i).isSpace()) ++i;
                const QString token2 = readIdentifier(inner, i);
                pp.addKeyWord(token, token2, m_macros, m_debugMode, fileName, lineNo, warnings);
                if (i < inner.size() && warnings) {
                    // token 来自用户脚本内容，同样可能有字面 %N —— 继续用拼接
                    warnings->append(warnPrefix(fileName, lineNo) + QLatin1Char('[') + token
                                    + QStringLiteral("] 之后的内容被忽略"));
                }
            } else if (warnings) {
                warnings->append(warnPrefix(fileName, lineNo) + QStringLiteral("'[' 的用法不正确"));
            }
            out.appendLine(lineNo, QString());
            continue;
        }

        if (pp.disabled) {
            out.appendLine(lineNo, QString());
            continue;
        }

        out.appendLine(lineNo, line);
    }

    pp.fileEnd(fileName, raw.size(), warnings);
    // 预留按原文长度给（行连接补空格 / 宏展开会让实际略长），这里把多余容量
    // 交还：这份产物的生命期是整个装载期，容量翻倍就是白占一份内存。
    out.squeeze();
    return out;
}
