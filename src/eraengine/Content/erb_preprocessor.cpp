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

namespace {

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
                warnings->append(QStringLiteral("%1:%2: %3").arg(fileName).arg(line).arg(text));
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
            warnings->append(QStringLiteral("%1:%2: 缺少 [%3]").arg(fileName).arg(line).arg(m));
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
        def.body = s.trimmed();
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

QString ErbPreprocessor::expandMacros(const QString& line) const {
    if (m_macroTable.isEmpty()) return line;
    QString text = line;
    for (int pass = 0; pass < 32; ++pass) {
        QString out;
        int i = 0;
        bool changed = false;
        while (i < text.size()) {
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
            out += body;
            i = k + 1;
            changed = true;
        }
        if (!changed) return out;
        text = out;
    }
    return text;
}

QList<ErbSourceLine> ErbPreprocessor::process(const QString& content, QStringList* warnings,
                                             const QString& fileName) const {
    QList<ErbSourceLine> out;

    // 保留全部物理行（含结尾空串），确保 lineNumber 与物理行一致
    QStringList raw = content.split(QLatin1Char('\n'));
    if (!raw.isEmpty() && raw.last().isEmpty()) raw.removeLast();

    const auto applyRename = [this](const QString& line) {
        if (m_rename.isEmpty()) return line;
        if (!line.contains(QLatin1String("[[")) || !line.contains(QLatin1String("]]"))) return line;
        QString s = line;
        for (auto it = m_rename.constBegin(); it != m_rename.constEnd(); ++it) {
            if (s.contains(it.key())) s.replace(it.key(), it.value());
        }
        return s;
    };

    PPState pp;

    for (int idx = 0; idx < raw.size(); ++idx) {
        const int lineNo = idx + 1;
        ErbSourceLine outLine;
        outLine.physicalLine = lineNo;

        QString line = applyRename(raw.at(idx));
        // CRLF 文件：split('\n') 会留下行尾 CR，而 C# 的 EraStreamReader 是连行终止符
        // 一起剥掉的。必须在这里去掉 —— 否则它会混进 PRINT 族的字面文本里。
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);

        // 空行 / 纯空白行：C# 直接跳过（不产生逻辑行）
        if (line.isEmpty() || line.trimmed().isEmpty()) {
            out.append(outLine);   // text 为空 -> Null 行
            continue;
        }

        // 宏替换（C# 在词法级展开；这里在行文本级做，先于一切解析）
        line = expandMacros(line);
        if (line.isEmpty() || line.trimmed().isEmpty()) {
            out.append(outLine);
            continue;
        }

        const QString left = line.trimmed();

        // ---- 行连接 '}'（单个）当作意外，容错跳过 ----
        if (left == QLatin1String("}")) {
            if (warnings) {
                warnings->append(QStringLiteral("%1:%2: 意外的行连接终止符 '}'")
                                     .arg(fileName).arg(lineNo));
            }
            out.append(outLine);
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
                warnings->append(QStringLiteral("%1:%2: 行连接 '{' 缺少对应的 '}'")
                                     .arg(fileName).arg(lineNo));
            }
            if (!pp.disabled) {
                outLine.text = joined.trimmed();
            }
            out.append(outLine);
            // 被吞并的物理行（含 '}' 行）保持为空
            for (int k = idx + 1; k < j; ++k) {
                ErbSourceLine filler;
                filler.physicalLine = k + 1;
                out.append(filler);
            }
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
                    warnings->append(QStringLiteral("%1:%2: [%3] 之后的内容被忽略")
                                         .arg(fileName).arg(lineNo).arg(token));
                }
            } else if (warnings) {
                warnings->append(QStringLiteral("%1:%2: '[' 的用法不正确")
                                     .arg(fileName).arg(lineNo));
            }
            out.append(outLine);
            continue;
        }

        if (pp.disabled) {
            out.append(outLine);
            continue;
        }

        outLine.text = line;
        out.append(outLine);
    }

    pp.fileEnd(fileName, raw.size(), warnings);
    return out;
}
