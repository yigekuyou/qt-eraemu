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
    QSet<QString> macros;
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        QString s = raw.trimmed();
        if (!s.startsWith(QLatin1Char('#'))) continue;
        s = s.mid(1).trimmed();
        if (!s.startsWith(QLatin1String("DEFINE"), Qt::CaseInsensitive)) continue;
        s = s.mid(6);
        int i = 0;
        while (i < s.size() && s.at(i).isSpace()) ++i;
        const QString name = readIdentifier(s, i);
        if (!name.isEmpty()) macros.insert(name);
    }
    return macros;
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

        // 空行 / 纯空白行：C# 直接跳过（不产生逻辑行）
        if (line.isEmpty() || line.trimmed().isEmpty()) {
            out.append(outLine);   // text 为空 -> Null 行
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
