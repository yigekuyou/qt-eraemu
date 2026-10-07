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
#include "ast_builder.h"
#include <QDebug>
#include "strform_parser.h"
#include "print_template.h"
#include "argument_parser.h"
#include "function_types.h"   // isBuiltinFunction（函数语句的定性）

namespace {

// 剥离行尾 ';' 注释（Emuera 注释语义），尊重引号
QString stripLineComment(const QString& text) {
    QChar quote;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (!quote.isNull()) {
            if (c == quote) quote = QChar();
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; continue; }
        if (c == QLatin1Char(';')) return text.left(i);
    }
    return text;
}

// 构建完成后做「语句参数类型化 + 解析期校验」
LogicalLine finalized(LogicalLine line) {
    ArgumentParser::build(line);
    return line;
}

// 整段文本是否为**单个**字符串字面量（首引号与其配对闭引号恰为行尾）。
// 仅查首尾字符会把表达式 `"AB"+"CD"` 误判成整段字符串 —— 必须扫描配对。
bool wholeQuoted(const QString& t) {
    if (t.size() < 2 || !t.startsWith(QLatin1Char('"'))) return false;
    bool escaped = false;
    for (int j = 1; j < t.size(); ++j) {
        const QChar c = t.at(j);
        if (escaped) { escaped = false; continue; }
        if (c == QLatin1Char('\\')) { escaped = true; continue; }
        if (c == QLatin1Char('"')) return j == t.size() - 1;
    }
    return false;
}

bool isOperatorStart(QChar c) {
    static const QString ops = QStringLiteral("+-*/%=!<>&|^~?#");
    return ops.contains(c);
}

bool isSymbolChar(QChar c) {
    static const QString syms = QStringLiteral("()[],:;");
    return syms.contains(c);
}

// 顶层逗号切分（跳过引号 / 括号嵌套）——用于形参列表
QStringList splitTopLevelComma(const QString& text) {
    QStringList out;
    QString current;
    int depth = 0;
    QChar quote;
    for (const QChar ch : text) {
        if (!quote.isNull()) { current += ch; if (ch == quote) quote = QChar(); continue; }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) { quote = ch; current += ch; continue; }
        if (ch == QLatin1Char('(') || ch == QLatin1Char('[')) { ++depth; current += ch; continue; }
        if (ch == QLatin1Char(')') || ch == QLatin1Char(']')) { if (depth > 0) --depth; current += ch; continue; }
        if (depth == 0 && ch == QLatin1Char(',')) { out << current.trimmed(); current.clear(); continue; }
        current += ch;
    }
    if (!current.trimmed().isEmpty()) out << current.trimmed();
    return out;
}

// 顶层 '=' 的位置（-1 = 无）；用于剥离形参默认值
int topLevelEquals(const QString& text) {
    int depth = 0;
    QChar quote;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (!quote.isNull()) { if (ch == quote) quote = QChar(); continue; }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) { quote = ch; continue; }
        if (ch == QLatin1Char('(') || ch == QLatin1Char('[')) { ++depth; continue; }
        if (ch == QLatin1Char(')') || ch == QLatin1Char(']')) { if (depth > 0) --depth; continue; }
        if (depth == 0 && ch == QLatin1Char('=')) return i;
    }
    return -1;
}

} // namespace

bool AstBuilder::isConditionInstruction(const QString& upperName) {
    return upperName == QLatin1String("IF")
        || upperName == QLatin1String("SIF")
        || upperName == QLatin1String("ELSEIF")
        || upperName == QLatin1String("WHILE")
        || upperName == QLatin1String("REPEAT")
        || upperName == QLatin1String("LOOP");
}

// 首操作数是标签名的指令族（CALL/CALLFORM/CALLF/TRYCALL*/JUMP*/BEGIN）
bool AstBuilder::isCallFamilyInstruction(const QString& upperName) {
    static const char* kNames[] = {
        "CALL", "CALLFORM", "CALLF", "CALLFORMF",
        "TRYCALL", "TRYCALLFORM", "TRYCALLF", "TRYCALLFORMF",
        // TRYC* 系（C# CALL_Instruction(isTry=true, isTryCatch=true)）：
        // eraTW 的 口上 大量使用 TRYCCALLFORM UNIQUE_FA_{CHARA}(…)。
        "TRYCCALL", "TRYCCALLFORM", "TRYCJUMP", "TRYCJUMPFORM",
        "JUMP", "JUMPFORM", "TRYJUMP", "TRYJUMPFORM",
        "GOTOFORM", "TRYGOTOFORM", "TRYCGOTO", "TRYCGOTOFORM",
        "CALLEVENT", "TRYCALLEVENT",
        "BEGIN",
        // FUNC：TRYCALLLIST/TRYJUMPLIST/TRYGOTOLIST 体内的单行条目
        // `FUNC 関数名式(実引数…)` —— 参数形态对齐 C# SP_CALLFORM
        // （ErbLoader 只允许 FUNC 出现在 TRY*LIST 体内）。
        "FUNC",
    };
    for (const char* n : kNames) {
        if (upperName == QLatin1String(n)) return true;
    }
    return false;
}

// PRINT 族的参数形态（对齐 C# PRINT_Instruction 扫描后缀选 ArgumentBuilder）：
//   PRINT…(V)     -> SP_PRINTV        逗号分隔的整型表达式
//   PRINT…(S)     -> STR_EXPRESSION   字符串表达式
//   PRINT…(FORMS) -> STR_EXPRESSION   字符串表达式（结果再按格式串展开）
//   PRINT…(FORM)  -> FORM_STR_NULLABLE 格式化串（文本 + {…}/%…%）
//   PRINT…(其它)  -> STR_NULLABLE     整行**字面文本**（不是表达式！）
// 最后一条很关键：eramaker 风格的 `PRINTL [0] 结缘(\1000)` 整行是文本。
AstBuilder::PrintArgInfo AstBuilder::printInfo(const QString& upperName) {
    PrintArgInfo info;

    // PRINTPLAIN / PRINTPLAINFORM：C# 里是独立注册的函数（STR_NULLABLE /
    // FORM_STR_NULLABLE），参数形态与 PRINT / PRINTFORM 相同
    if (upperName.startsWith(QLatin1String("PRINTPLAINFORM"))) {
        info.mode = PrintArgMode::FormStr;
        return info;
    }
    if (upperName.startsWith(QLatin1String("PRINTPLAIN"))) {
        info.mode = PrintArgMode::Literal;
        return info;
    }

    // C#: StringStream st(name); st.Jump(5/*PRINT*/);
    QString rest;
    if (upperName.startsWith(QLatin1String("PRINTSINGLE"))) {
        info.mode = PrintArgMode::Literal;      // PRINT_SINGLE + EXTENDED
        rest = upperName.mid(11);
    } else if (upperName.startsWith(QLatin1String("PRINT"))) {
        rest = upperName.mid(5);
    } else if (upperName.startsWith(QLatin1String("DEBUGPRINTSINGLE"))) {
        info.mode = PrintArgMode::Literal;      // DEBUGPRINT_SINGLE + EXTENDED
        rest = upperName.mid(16);
    } else if (upperName.startsWith(QLatin1String("DEBUGPRINT"))) {
        // DEBUGPRINT 族与 PRINT 族同一套后缀规则（DEBUGPRINT[V|S|FORM|FORMS][L|W|C|K|D]）。
        // 不识别的话实参会被当**表达式**归约：`DEBUGPRINTL 米吉多拉翁モードon(フラグ)`
        // 这种纯调试文本会被判成「未定义的函数」，且 DEBUGPRINTFORML {…} 的
        // 格式化串语义也拿不到。
        rest = upperName.mid(10);
    } else {
        return info;                            // NotPrint
    }

    // 排除 PRINTBUTTON / PRINTDATA / PRINTCPERLINE 等同前缀指令
    // （C# 是「补后缀后必须恰好到 EOS」，非 PRINT 族后缀会抛 PRINT異常）
    for (const QChar c : rest) {
        if (!QLatin1String("VSLWCKDFORM").contains(c)) return PrintArgInfo{};
    }

    // ---- 参数形态（顺序同 C#：V / S / FORMS / FORM / 其余）----
    if (rest.startsWith(QLatin1String("FORMS"))) {
        info.mode = PrintArgMode::StrExpression;
        info.forms = true;
        rest = rest.mid(5);
    } else if (rest.startsWith(QLatin1String("FORM"))) {
        info.mode = PrintArgMode::FormStr;
        rest = rest.mid(4);
    } else if (rest.startsWith(QLatin1Char('V'))) {
        info.mode = PrintArgMode::PrintV;
        rest = rest.mid(1);
    } else if (rest.startsWith(QLatin1Char('S'))) {
        info.mode = PrintArgMode::StrExpression;
        rest = rest.mid(1);
    } else {
        info.mode = PrintArgMode::Literal;
    }

    // ---- 尾部开关（C# 顺序：LC / C，然后 K，D，L / W）----
    if (rest.startsWith(QLatin1String("LC"))) {
        info.clearPad = true;
        info.padLeft = false;
        rest = rest.mid(2);
    } else if (rest.startsWith(QLatin1Char('C'))) {
        info.clearPad = true;
        info.padLeft = true;
        rest = rest.mid(1);
    }
    if (rest.startsWith(QLatin1Char('K'))) rest = rest.mid(1);
    if (rest.startsWith(QLatin1Char('D'))) {
        info.debug = true;
        rest = rest.mid(1);
    }
    if (rest.startsWith(QLatin1Char('L'))) {
        info.newline = true;
        rest = rest.mid(1);
    } else if (rest.startsWith(QLatin1Char('W'))) {
        info.newline = true;
        info.waitInput = true;
        rest = rest.mid(1);
    }
    if (!rest.isEmpty()) return PrintArgInfo{};   // 后缀没吃干净 -> 不是 PRINT 族
    return info;
}

AstBuilder::PrintArgMode AstBuilder::classifyPrintArg(const QString& upperName) {
    return printInfo(upperName).mode;
}

// 是否是「**精确**登记的指令名」（规范表 + PRINT 族 + CALL 族 + 少数控制流）
//
// 与 isKnownInstructionName 的区别：这里**不含前缀启发式**。
// 前缀匹配（CALL…/JUMP…/GOTO…/PRINT…）会误命中同前缀的**变量名**
// （eraTW 就有 `CALLNAME:MASTER = …`、`GOTJUEL:ARG:0 = …`），
// 所以那些位置必须先确认「这一行不是赋值」再用。
bool AstBuilder::isExactInstructionName(const QString& upperName) {
    if (upperName.isEmpty()) return false;
    // 注册类登记的扩展语句（registerExtensionStatement）：它们也是「指令」。
    // 必须在这里认，否则含 `=` 的行（如 `DT_COLUMN_OPTIONS "t", "c", DEFAULT = 1`）
    // 会先被 splitAssignment 判成赋值语句，永远到不了扩展语句分派。
    if (s_extensionStatements.contains(upperName)) return true;
    if (findInstructionSpec(upperName.toStdString())) return true;      // 指令规范表
    if (printInfo(upperName).mode != PrintArgMode::NotPrint) return true; // PRINT 族
    if (isCallFamilyInstruction(upperName)) return true;                // CALL/JUMP/BEGIN
    static const char* kExtra[] = {
        "FORM", "CHKFONT", "SETCOLOR", "RESETCOLOR", "ALIGNMENT", "REDRAW",
        "NEWLINE", "PRINTDATA", "PRINTDATAL", "PRINTDATAW", "PRINTBUTTONLC",
        "DOUBLEPRINT", "DEBUGPRINT", "HTML_PRINT", "HTML_TAGSPLIT",
        "PRINT_IMG", "PRINTBUTTONC",
        // 控制流 / 结构化指令（由执行链与状态机处理，不在指令规范表里）
        "RESTART", "RESETDATA", "LOADGLOBAL", "SAVEGLOBAL", "DATALIST", "ENDLIST",
        "DATAFORM", "DATA", "ENDDATA", "FORCEWAIT", "SKIPDISP", "REUSELASTLINE",
        "FONTREGULAR", "FONTSTYLE", "FONTBOLD", "FONTITALIC", "FONTSTRIKE",
        "CATCH", "ENDCATCH", "TRYCALLLIST", "TRYJUMPLIST", "GOTOLIST",
        "TRYGOTOLIST", "ENDFUNC",
        "SWAP", "SWAPVAR", "TIMES", "BAR", "BARL", "POWER", "SORTCHARA", "VARSET",
        "CVARSET", "ADDCHARA", "DELCHARA", "ADDCHARAALL", "SPLIT",
        // 由执行链（ScriptRunner）处理的控制流型指令：不在指令规范表里，
        // 但必须算「已知」，否则装载期会刷「未识别的指令」。
        "DOTRAIN", "CALLTRAIN", "STOPCALLTRAIN", "FORCEWAIT", "SKIPDISP",
        "REUSELASTLINE", "NOSKIP", "ENDNOSKIP", "CLEARTEXTBOX", "OUTPUTLOG",
        "RESETGLOBAL", "DELALLCHARA", "ADDSPCHARA", "ADDDEFCHARA",
        "ADDVOIDCHARA", "UPCHECK", "CUPCHECK", "FORCEKANA", "TRYCGOTO",
        "TRYCGOTOFORM", "TRYCCALL", "TRYCCALLFORM", "TRYCJUMP", "TRYCJUMPFORM",
        "SAVEGAME", "LOADGAME", "SAVEDATA", "LOADDATA", "DELDATA",
        // 运行期断言（ExecutionEngine 处理；为假报错终止）—— 不算未识别
        "ASSERT",
        // 控制流关键字（执行链单独处理，不进指令规范表）
        "DO", "REND", "LOOP", "WHILE", "WEND", "REPEAT", "FOR", "NEXT",
        "BREAK", "CONTINUE", "RETURN", "RETURNF", "GOTO", "CALL", "BEGIN",
        "IF", "ELSEIF", "ELSE", "ENDIF", "SIF", "SELECTCASE", "CASE",
        "CASEELSE", "ENDSELECT", "THROW", "QUIT",
        // 注意：此处**不能**再列 "END"。C# Emuera 的函数表里没有名为 END 的
        // 指令（只有 ENDIF/ENDSELECT/ENDDATA/ENDLIST/ENDCATCH/ENDFUNC/
        // ENDNOSKIP 等），所以 `END = 0` 在 C# 里是**赋值**。eraTW 的
        // MOVEMENT_キャラ移動処理.ERB 用 `#DIM END` 声明私有变量再 `END = 0`
        // ——把 END 当指令会让赋值被吞、`IF END` 恒假、该函数的多处早退失效
        //（角色移动进入死循环，见 script_runner 的循环超限告警）。
        // ecd/docs 命令表里、由执行链/引擎单独处理但不在指令规范表里的名字
        // （缺了会被当成「未识别的指令」静默跳过）：
        "RETURNFORM",      // RETURN 的格式化串版本（StrForm 实参）
        "SETSTYLE",        // 字体样式位掩码（与 FONTSTYLE 同类）
        "DRAWLINEFORM",    // DRAWLINE 的格式化串版本
        "SETBGCOLORBYNAME", "TOOLTIP_SETCOLOR", "TOOLTIP_SETDELAY",
        "TOOLTIP_SETDURATION",
        "TRYLIST",         // STRDATA 的 TRY 版本（数据块标记）
    };
    for (const char* n : kExtra) {
        if (upperName == QLatin1String(n)) return true;
    }
    return false;
}

// 前缀启发式：以 PRINT / DEBUGPRINT / HTML_PRINT / CALL / JUMP / TRY / GOTO 开头。
// 目的只是兜住规范表没登记的变体（PRINTFORMD 之类）；命中不等于「这行就是指令」。
bool AstBuilder::hasInstructionPrefix(const QString& upperName) {
    static const char* kPrefixes[] = {"PRINT", "DEBUGPRINT", "HTML_PRINT",
                                      "CALL", "JUMP", "TRYCALL", "TRYJUMP",
                                      "TRYC",   // TRYCCALL / TRYCCALLFORM / TRYCJUMP…
                                      "GOTO", "TRYGOTO", "CALLEVENT", "TRYCALLEVENT"};
    for (const char* p : kPrefixes) {
        if (upperName.startsWith(QLatin1String(p))) return true;
    }
    return false;
}

bool AstBuilder::isKnownInstructionName(const QString& upperName) {
    return isExactInstructionName(upperName) || hasInstructionPrefix(upperName);
}

// 赋值左值必须是「一个变量引用」：以标识符起头，且顶层没有空白
// （下标里的空白在括号内，如 `ステージ:(ステージ幅 - 1):i`）。
bool AstBuilder::isBareVariableLhs(const QString& lhs) {
    if (lhs.isEmpty()) return false;
    const QChar head = lhs.at(0);
    if (!(head.isLetter() || head == QLatin1Char('_') || head.unicode() > 127)) return false;
    int depth = 0;
    QChar quote;
    for (const QChar c : lhs) {
        if (!quote.isNull()) { if (c == quote) quote = QChar(); continue; }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; continue; }
        if (c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{')) { ++depth; continue; }
        if (c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}')) { if (depth > 0) --depth; continue; }
        // 顶层空白/逗号 -> 不是单个变量引用（`CALL FOO, 1` 这类不能被当成赋值）
        if (depth == 0 && (c.isSpace() || c == QLatin1Char(','))) return false;
    }
    return true;
}

bool AstBuilder::isStrFormInstruction(const QString& upperName) {
    // 对齐 C#：这些指令的操作数是 StrForm（文本 + {expr}/%expr%）
    if (classifyPrintArg(upperName) == PrintArgMode::FormStr) return true;
    return upperName == QLatin1String("DRAWLINEFORM")
        || upperName == QLatin1String("PRINTPLAINFORM")
        || upperName == QLatin1String("STRFORM");
}

// ---------------------------------------------------------------------------
// tokenize —— 对齐 C# LexicalAnalyzer.Analyse 的简化版本
// ---------------------------------------------------------------------------
WordCollection AstBuilder::tokenize(const QString& line) {
    WordCollection wc;
    const int n = line.length();
    int i = 0;

    while (i < n) {
        const QChar ch = line.at(i);

        if (ch.isSpace()) { ++i; continue; }

        // 字符串字面量（"..." 或 '...'）
        if (ch == '"' || ch == '\'') {
            const QChar quote = ch;
            const int start = i;
            ++i;
            QString value;
            while (i < n && line.at(i) != quote) {
                if (line.at(i) == '\\' && i + 1 < n) {
                    const QChar esc = line.at(i + 1);
                    switch (esc.toLatin1()) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 's': value += ' ';  break;
                    case '\\': value += '\\'; break;
                    default: value += esc; break;
                    }
                    i += 2;
                    continue;
                }
                value += line.at(i);
                ++i;
            }
            if (i < n) ++i;   // closing quote
            wc.addWord(WordKind::String, value, line.mid(start, i - start), true);
            continue;
        }

        // %VAR% / $VAR 变量引用
        if (ch == '%' || ch == '$') {
            const int start = i;
            ++i;
            while (i < n && !line.at(i).isSpace()
                   && !isSymbolChar(line.at(i)) && line.at(i) != '%') {
                ++i;
            }
            if (i < n && line.at(i) == '%') ++i;   // closing %
            const QString raw = line.mid(start, i - start);
            wc.addWord(WordKind::Identifier, raw, raw);
            continue;
        }

        // 数字字面量
        if (ch.isDigit()) {
            const int start = i;
            if (ch == '0' && i + 1 < n && (line.at(i + 1) == 'x' || line.at(i + 1) == 'X'
                                           || line.at(i + 1) == 'b' || line.at(i + 1) == 'B')) {
                i += 2;
                while (i < n && line.at(i).isLetterOrNumber()) ++i;
            } else {
                while (i < n && line.at(i).isDigit()) ++i;
                // p/P（2 的幂）或 e/E（10 的幂）指数：如 1p0
                if (i < n && (line.at(i) == 'p' || line.at(i) == 'P'
                              || line.at(i) == 'e' || line.at(i) == 'E')) {
                    ++i;
                    if (i < n && (line.at(i) == '+' || line.at(i) == '-')) ++i;
                    while (i < n && line.at(i).isDigit()) ++i;
                }
            }
            wc.addWord(WordKind::Number, line.mid(start, i - start));
            continue;
        }

        // 标识符 / 关键词（含非 ASCII 文本）
        if (ch.isLetter() || ch == '_' || ch.unicode() > 127) {
            const int start = i;
            while (i < n) {
                const QChar c = line.at(i);
                // 续读**排除空白**：全角空格 U+3000 的 unicode() > 127，此前会被
                // 吞进标识符（`IF　絶頂変動値` 变成一个词 → 整行变「未识别的指令」）。
                // C# LexicalAnalyzer 以 char.IsWhiteSpace 断词，这里对齐。
                if ((c.isLetterOrNumber() || c == '_' || c.unicode() > 127) && !c.isSpace()) {
                    ++i;
                } else {
                    break;
                }
            }
            wc.addWord(WordKind::Identifier, line.mid(start, i - start));
            continue;
        }

        // 运算符（贪婪取最长）
        if (isOperatorStart(ch)) {
            const int start = i;
            ++i;
            if (i < n) {
                const QChar nxt = line.at(i);
                const QString two = line.mid(start, 2);
                static const QStringList twoCharOps = {
                    "==", "!=", "<=", ">=", "&&", "||", "^^", "!&", "!|",
                    "<<", ">>", "++", "--", "?=", "'="
                };
                if (twoCharOps.contains(two)) {
                    ++i;
                } else if (nxt == '=' || nxt == '&' || nxt == '|' || nxt == '^'
                           || nxt == '<' || nxt == '>' || nxt == '+' || nxt == '-') {
                    // 非标准组合，按单字符处理
                }
            }
            wc.addWord(WordKind::Operator, line.mid(start, i - start));
            continue;
        }

        // 符号
        if (isSymbolChar(ch)) {
            wc.addWord(WordKind::Symbol, QString(ch));
            ++i;
            continue;
        }

        // 其它：原样保留（如 { } 等）
        wc.addWord(WordKind::Term, QString(ch));
        ++i;
    }

    return wc;
}

// ---------------------------------------------------------------------------
// splitAssignment —— 顶层赋值切分（原 ErbLoader::extractInstruction 的赋值分支）
// ---------------------------------------------------------------------------
bool AstBuilder::splitAssignment(const QString& line, QString& lhs, QString& op, QString& rhs) {
    int depth = 0;
    QChar quote;
    const int n = line.length();

    for (int i = 0; i < n; ++i) {
        const QChar ch = line.at(i);

        if (!quote.isNull()) {
            if (ch == quote) quote = QChar();
            continue;
        }
        if (ch == '"') { quote = ch; continue; }

        // 字符串赋值运算符 '= ：LHS **允许含空白**（与下面的 = 分支对齐）。
        // `LOCALS:(LOCAL + 1) '= …` / `CSTR:ARG:(29 + RESULT) '= RESULTS` 这类
        // 带表达式下标的字符串赋值此前因「LHS 不许有空格」整行被丢弃，
        // 导致 eraMegaten 深层界面的菜单/详情文本整行缺失。
        if (ch == '\'') {
            if (i + 1 < n && line.at(i + 1) == '=') {
                lhs = line.left(i).trimmed();
                op = QStringLiteral("'=");
                rhs = line.mid(i + 2).trimmed();
                if (lhs.isEmpty()) return false;
                const QChar head = lhs.at(0);
                if (!(head.isLetter() || head == QLatin1Char('_') || head.unicode() > 127)) {
                    return false;            // 必须以标识符开头
                }
                return true;
            }
            quote = ch;
            continue;
        }

        if (ch == '(' || ch == '[' || ch == '{') { ++depth; continue; }
        if (ch == ')' || ch == ']' || ch == '}') { if (depth > 0) --depth; continue; }
        if (depth != 0 || ch != '=') continue;

        const QChar next = (i + 1 < n) ? line.at(i + 1) : QChar();
        if (next == '=') { ++i; continue; }   // ==
        if (i > 0) {
            const QChar prev = line.at(i - 1);
            if (prev == '!' || prev == '<' || prev == '>') continue;

            if (prev == '+' || prev == '-' || prev == '*' || prev == '/' || prev == '%') {
                lhs = line.left(i - 1).trimmed();
                op = QString(prev) + '=';
                rhs = line.mid(i + 1).trimmed();
            } else {
                lhs = line.left(i).trimmed();
                op = QStringLiteral("=");
                rhs = line.mid(i + 1).trimmed();
            }
        } else {
            continue;
        }

        // LHS 必须非空；**允许含空白**（Emuera 的赋值 LHS 可以是
        // `ステージ:(ステージ幅 - 1):(LOCAL:0)` 这种带空格的表达式下标）。
        // 「= 出现在指令行里」的情况由 build() 的「首 token 是否指令名」拦掉。
        if (lhs.isEmpty()) return false;
        const QChar head = lhs.at(0);
        if (!(head.isLetter() || head == QLatin1Char('_') || head.unicode() > 127)) {
            return false;                        // 必须以标识符开头
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// splitOperands —— 顶层空白/逗号切分（原 ErbLoader 的 splitTopLevel）
// ---------------------------------------------------------------------------
QStringList AstBuilder::splitOperands(const QString& text, bool splitWhitespace) {
    QStringList tokens;
    QString current;
    int depth = 0;
    QChar quote;
    const int n = text.length();

    auto flush = [&]() {
        const QString t = current.trimmed();
        if (!t.isEmpty()) tokens.append(t);
        current.clear();
    };

    for (int i = 0; i < n; ++i) {
        const QChar ch = text.at(i);

        if (!quote.isNull()) {
            current += ch;
            if (ch == quote) quote = QChar();
            continue;
        }
        if (ch == '"' || ch == '\'') { quote = ch; current += ch; continue; }

        if (ch == '(' || ch == '[' || ch == '{') { ++depth; current += ch; continue; }
        if (ch == ')' || ch == ']' || ch == '}') { if (depth > 0) --depth; current += ch; continue; }

        if (depth == 0 && ((splitWhitespace && ch.isSpace()) || ch == ',')) {
            flush();
            if (ch == ',') tokens.append(QStringLiteral(","));
            continue;
        }
        current += ch;
    }
    flush();
    return tokens;
}

// ---------------------------------------------------------------------------
// build —— 行 -> LogicalLine
// ---------------------------------------------------------------------------
LogicalLine AstBuilder::build(const QString& rawLine,
                              const ScriptPosition& position,
                              const AstResolver& resolve,
                              const AstResolver& resolveQuiet,
                              ParseDiagnostics* diagnostics) {
    LogicalLine line;
    line.raw = rawLine;
    line.position = position;

    // 防御：调用方可能传入带行终止符的行（CRLF）。C# 的行永远不会带 CR。
    QString raw = rawLine;
    if (raw.endsWith(QLatin1Char('\r'))) raw.chop(1);

    // Emuera 专用行 ";!;"：前缀被跳过后按**正常代码行**解析（LexicalAnalyzer.cs
    // 的 st.CurrentEqualTo(";!;") -> st.Jump(3)）；Eramaker 中才是注释。
    // 必须在 stripLineComment 之前剥掉，否则整行会被当成注释丢弃。
    {
        int lead = 0;
        while (lead < raw.size() && (raw.at(lead) == QLatin1Char(' ') || raw.at(lead) == QLatin1Char('\t'))) ++lead;
        if (raw.mid(lead).startsWith(QLatin1String(";!;")))
            raw = raw.left(lead) + raw.mid(lead + 3);
    }

    // ';' 之后是注释（字符串内除外）
    // 注意：行尾空白**不能**在这里就丢掉 —— PRINT 族的字面文本参数是
    // 「命令名之后原样到行尾」（C# STR_ArgumentBuilder -> StringStream.Substring），
    // eraTetris/eraTW 用 `PRINT 　　　　SCORE 　　　　　　　　` 这类尾随全角空格做列对齐。
    const QString source = stripLineComment(raw);
    const QString trimmed = source.trimmed();

    // 空行 / 注释（C# NullLine）
    if (trimmed.isEmpty() || trimmed.startsWith(';')) {
        line.kind = LineKind::Null;
        return finalized(std::move(line));
    }

    // 预处理指令（C# ParseSharpLine 的输入；此处仅保留）
    if (trimmed.startsWith('#')) {
        line.kind = LineKind::Preprocessor;
        return finalized(std::move(line));
    }

    // 标签行
    if (trimmed.startsWith('@') || trimmed.startsWith('$')) {
        line.kind = trimmed.startsWith('@') ? LineKind::FunctionLabel : LineKind::GotoLabel;
        const QString label = trimmed.mid(1);
        // 标签名 = 首个标识符 token（对齐 C# LexicalAnalyzer：名字不能含
        // 空格 / 全角空格 / 制表符 / ( , [ : = 等符号）
        int cut = label.size();
        for (int i = 0; i < label.size(); ++i) {
            const QChar c = label.at(i);
            if (c.isSpace() || c == QLatin1Char('(') || c == QLatin1Char(',')
                || c == QLatin1Char('[') || c == QLatin1Char(':') || c == QLatin1Char('=')) {
                cut = i;
                break;
            }
        }
        line.labelName = label.left(cut).trimmed();

        // 形参列表：Emuera 支持两种写法（ErbLoader.parseLabel 的 symbol.Type）
        //   @F(A, B)               -> '(' 形式
        //   @F, ARG, ARGS:1, X = 1 -> ',' 形式（eramaker 遗产，eraTW 里大量存在）
        // 每项取顶层 '=' 之前的名字（'=' 之后是默认值）。
        if (line.kind == LineKind::FunctionLabel) {
            QString rest = label.mid(cut).trimmed();
            if (rest.startsWith(QLatin1Char(','))) rest = rest.mid(1).trimmed();
            else if (rest.startsWith(QLatin1Char('('))) {
                const int rp = rest.lastIndexOf(QLatin1Char(')'));
                rest = (rp > 0) ? rest.mid(1, rp - 1) : rest.mid(1);
                rest = rest.trimmed();
            } else {
                rest.clear();
            }
            if (!rest.isEmpty()) {
                for (const QString& part : splitTopLevelComma(rest)) {
                    QString item = part.trimmed();
                    const int eq = topLevelEquals(item);
                    const QString defaultValue = eq >= 0 ? item.mid(eq + 1).trimmed() : QString();
                    if (eq >= 0) item = item.left(eq).trimmed();
                    if (!item.isEmpty()) {
                        line.labelArgs.append(item);
                        line.labelDefaults.append(defaultValue);
                    }
                }
            }
        }
        line.arguments = { Operand(line.labelName) };
        return finalized(std::move(line));
    }

    // 赋值语句（函数名 = 特例：AssignOperator）
    // 先看首个 token 是不是「已知指令名」：是则走命令文（对齐 C# LogicalLineParser
    // 先查函数名表）；否则按赋值解 —— 这样 `ステージ:(ステージ幅 - 1):i = v`
    // 这类 **LHS 含空白** 的赋值才能被识别，而 `PRINTFORML a = b` 仍是指令。
    const WordCollection wcHead = tokenize(trimmed);
    bool firstIsInstruction = false;
    if (!wcHead.isEmpty() && wcHead.words().first().kind == WordKind::Identifier) {
        const QString head = wcHead.words().first().text.toUpper();
        firstIsInstruction = isExactInstructionName(head);
        if (!firstIsInstruction && hasInstructionPrefix(head)) {
            // 前缀启发式的**假阳性**：`CALLNAME:MASTER = …`（CALL 前缀）、
            // `GOTJUEL:ARG:0 = 1`（GOTO 前缀）这类**变量**赋值。
            // 只有当这一行不是合法赋值形状时才当成指令。
            QString l, o, r;
            firstIsInstruction = !(splitAssignment(trimmed, l, o, r) && isBareVariableLhs(l));
        }
    }
    QString lhs, op, rhs;
    if (!firstIsInstruction && splitAssignment(trimmed, lhs, op, rhs)) {
        line.kind = LineKind::Instruction;
        line.functionName = op;
        line.assignOperator = op;
        Operand dest(lhs);
        Operand value(rhs);
        // 右值在此处只是**临时**归约（变量类型尚未定稿）：字符串赋值的右值
        // 会被最终 applyStringAssignments() 用 StrFormParser 替换掉。用静默
        // resolver 解析，避免临时失败刷屏（705 条噪音的来源）。
        if (resolveQuiet) value.ast = resolveQuiet(rhs);
        else if (resolve) value.ast = resolve(rhs);
        line.arguments = { dest, value };
        return finalized(std::move(line));
    }

    // 后缀自增/自减**语句**：`I++` / `BAG:COUNT--` / `LOCAL:(… == 0)++`
    // （对齐 C# LogicalLineParser 的 SETFunction + OperatorCode.Increment/Decrement）
    if (!firstIsInstruction) {
        for (const char* opText : {"++", "--"}) {
            if (!trimmed.endsWith(QLatin1String(opText))) continue;
            const QString body = trimmed.left(trimmed.size() - 2).trimmed();
            if (body.isEmpty()) continue;
            // 只拒绝**顶层**赋值运算符：`IF A == B++` 不是自增语句，而
            // `LOCAL:(CFLAG:… == 0)++` 里的 == 在括号内，仍是合法自增。
            // 复用 splitAssignment（它按括号/引号深度扫描），语义一致。
            QString bl, bo, br;
            if (splitAssignment(body, bl, bo, br)) continue;
            const QChar head = body.at(0);
            if (!(head.isLetter() || head == QLatin1Char('_') || head.unicode() > 127)) continue;
            line.kind = LineKind::Instruction;
            line.functionName = QString::fromLatin1(opText);
            line.assignOperator = line.functionName;
            line.arguments = { Operand(body) };
            return finalized(std::move(line));
        }
    }

    // 前缀自增/自减**语句**：`++TFLAG:X` / `--BAG:COUNT`
    // （对齐 C# LogicalLineParser 的前缀 Increment/Decrement：与后缀同义，
    //  都是「左值 = 左值 ∓ 1」，运行期 execution_engine 用同一个 "++"/"--" 分支。）
    // eraMegaten 的 SKILL_KE_クロスマジック.ERB 就写 `++TFLAG:技能用1`。
    if (!firstIsInstruction) {
        for (const char* opText : {"++", "--"}) {
            if (!trimmed.startsWith(QLatin1String(opText))) continue;
            const QString body = trimmed.mid(2).trimmed();
            if (body.isEmpty()) continue;
            QString bl, bo, br;
            if (splitAssignment(body, bl, bo, br)) continue;
            const QChar head = body.at(0);
            if (!(head.isLetter() || head == QLatin1Char('_') || head.unicode() > 127)) continue;
            line.kind = LineKind::Instruction;
            line.functionName = QString::fromLatin1(opText);
            line.assignOperator = line.functionName;
            line.arguments = { Operand(body) };
            return finalized(std::move(line));
        }
    }

    // 命令文
    // 构建期临时使用词法 token；不再存入 LogicalLine（省内存：2M 行级别差异巨大）
    const WordCollection wc = tokenize(trimmed);
    if (wc.isEmpty()) {
        line.kind = LineKind::Null;
        return finalized(std::move(line));
    }

    line.kind = LineKind::Instruction;
    const Word& first = wc.words().first();
    line.functionName = first.text.toUpper();

    // ---- 函数语句（对齐 C# LogicalLineType.Function）----
    // 一行以**内置函数名**开头（且不是已知指令/赋值）时，整行是一次函数调用：
    //   GETMILLISECOND                 -> RESULT = GETMILLISECOND()
    //   GETTIME
    //   CURRENTREDRAW
    //   REPLACE LOCALS, "(^\s+|\s+$)", ""   -> RESULTS:0 = REPLACE(…)
    //   TWAIT 2500, 0
    //   SUBSTRING RESULTS:0, 0, RESULT
    //   SPRITECREATE @"%素体:TEMP%%TEMP_NAME%", G_ID
    // 返回值由执行链写入 RESULT（整型）/ RESULTS:0（字符串）。
    // 这里把「函数名 + 剩余原文」重新组装成一个调用表达式交给表达式解析器归约，
    // 好处是实参里的空格/逗号/运算符（`TWAIT 550 + COUNT * 75, 0`）原样保留。
    //
    // 定性放在**赋值判定之后**：`RAND:3 = 5` 的 `RAND` 也是函数名，
    // 但整行是合法赋值，必须仍按赋值走（eraTW 大量使用 `RAND:n`）。
    if (isBuiltinFunction(line.functionName.toStdString())
        // POWER 例外：语句形式是 SP_POWER 指令「<变量>, X, Y -> 变量 = X^Y」
        // （C# BuiltInFunctionCode.POWER，注释「引数が違うのでMETHOD化できない」），
        // 与式中函数 POWER(X, Y)（2 参）不同形 —— 语句不能按函数调用归约，
        // 否则 eraTW TRACHECK_ORGASM.ERB:92 `POWER Multiplier, 2, MultipleEc`
        // 既触发参数数目双重告警、又丢掉对变量的赋值。
        && line.functionName != QLatin1String("POWER")) {
        const QString callText = trimmed.mid(first.text.length()).trimmed();
        // 实参形态声明外置（注册类可以插入 AST）：ExtensionRegistry::regForm()
        // 声明的函数（如 PUTFORM），实参是**格式化串**（文本 + {…}/%…%），
        // 不是表达式 —— 按表达式归约会失败（eraTW @SAVEINFO 的
        // `PUTFORM {DAY,3,RIGHT}日目 %GET_MAPNAME(…)%` 被静默跳过、概要丢失）。
        // 对齐 C# FormArgument：整行按 StrForm 解析，语义在执行期由专用分支处理。
        // 本文件零函数名 —— 声明由注册类注入（registerFormArgFunction）。
        if (!callText.isEmpty() && isFormArgFunction(line.functionName)) {
            Operand form(callText);
            if (resolve) form.ast = StrFormParser::parse(callText, resolve);
            line.arguments.append(form);
            line.isFunctionCall = true;
            return finalized(std::move(line));
        }
        const QString exprText =
            line.functionName + QLatin1Char('(') + callText + QLatin1Char(')');
        Operand call(exprText);
        if (resolve) call.ast = resolve(exprText);
        line.arguments.append(call);
        line.isFunctionCall = true;
        return finalized(std::move(line));
    }

    // CALL 族特例：CALL / CALLFORM / CALLF / TRYCALL* / JUMP* / BEGIN
    // 第一个操作数是**标签名**（可含 %...% 格式串），不是表达式 ——
    // 对齐 C# 的 SP_CALL / SP_CALLFORM / CALLF_Instruction（目标按标签解析）。
    // 若按表达式解析，TRYCALLFORM NAME_%X%_K30(...) 会被误当成函数调用。
    if (isCallFamilyInstruction(line.functionName)) {
        QString rest = trimmed.mid(first.text.length()).trimmed();
        QString funcName;
        QString args;
        // 标签名 = 第一个 token（到 空白 / 逗号 / '(' 为止）。只有当 '(' **紧跟**
        // 标签名时才按括号形式取实参 —— 否则逗号形式的实参里若含 '('
        // （如 `CALL F, !(1)`），rest.indexOf('(') 会命中实参里的括号，
        // 把标签名错切成 "F, !" -> "CALL label not found"。
        int nameEnd = 0;
        bool inPercentForm = false;   // %…% 是格式串区，内部的 ( , 空格不属于名字边界
        while (nameEnd < rest.size()) {
            const QChar c = rest.at(nameEnd);
            if (c == QLatin1Char('%')) {
                inPercentForm = !inPercentForm;
            } else if (!inPercentForm
                       && (c.isSpace() || c == QLatin1Char(',')
                           || c == QLatin1Char('('))) {
                break;
            }
            ++nameEnd;
        }
        funcName = rest.left(nameEnd).trimmed();
        const QChar afterName = (nameEnd < rest.size()) ? rest.at(nameEnd) : QChar();
        if (afterName == QLatin1Char('(')) {
            const int close = rest.lastIndexOf(')');
            args = (close > nameEnd) ? rest.mid(nameEnd + 1, close - nameEnd - 1)
                                     : rest.mid(nameEnd + 1);
        } else {
            // 逗号形式 `CALL 标签, 实参1, 实参2`（Emuera 与括号形式等价，eraTW 大量使用）。
            int argStart = nameEnd;
            while (argStart < rest.size() && (rest.at(argStart).isSpace()
                   || rest.at(argStart) == QLatin1Char(','))) ++argStart;
            if (argStart < rest.size()) args = rest.mid(argStart);
        }
        if (funcName.startsWith('@')) funcName = funcName.mid(1);

        line.arguments.append(Operand(funcName));
        if (!args.trimmed().isEmpty()) {
            // 对齐 C# ExpressionParser.ReduceArguments：只按**顶层**逗号切分
            // （CALL F(GET_STR(a, b, c), "x") 是 2 个实参，不是 5 个）
            const QStringList argList = splitTopLevelComma(args);
            for (const QString& a : argList) {
                const QString t = a.trimmed();
                Operand operand(t);
                if (wholeQuoted(t)) {
                    operand.isString = true;
                    operand.raw = t.mid(1, t.length() - 2);
                } else if (resolve) {
                    operand.ast = resolve(t);
                }
                line.arguments.append(operand);
            }
        }
        return finalized(std::move(line));
    }

    // PRINTBUTTON 族（PRINTBUTTON / PRINTBUTTONC / PRINTBUTTONLC）的每个参数
    // 是一个完整表达式；只按顶层逗号切分。空格属于字符串表达式的一部分，
    // 不能走 Raw 的空白切分。
    if (line.functionName == QLatin1String("PRINTBUTTON")
        || line.functionName == QLatin1String("PRINTBUTTONC")
        || line.functionName == QLatin1String("PRINTBUTTONLC")) {
        const QString remainder = trimmed.mid(first.text.length()).trimmed();
        for (const QString& item : splitTopLevelComma(remainder)) {
            const QString text = item.trimmed();
            if (text.isEmpty()) continue;
            Operand operand(text);
            bool wholeString = false;
            if (text.size() >= 2 && text.startsWith(QLatin1Char('"'))) {
                bool escaped = false;
                for (int j = 1; j < text.size(); ++j) {
                    const QChar c = text.at(j);
                    if (escaped) { escaped = false; continue; }
                    if (c == QLatin1Char('\\')) { escaped = true; continue; }
                    if (c == QLatin1Char('"')) {
                        wholeString = (j == text.size() - 1);
                        break;
                    }
                }
            }
            if (wholeString) {
                operand.isString = true;
                operand.raw = text.mid(1, text.size() - 2);
            } else if (resolve) {
                operand.ast = resolve(text);
            }
            line.arguments.append(operand);
        }
        return finalized(std::move(line));
    }

    // HTML_PRINT 的参数是一个完整字符串表达式，不按空白或逗号拆分。
    if (line.functionName == QLatin1String("RETURNF")) {
        const QString expr = trimmed.mid(first.text.length()).trimmed();
        if (!expr.isEmpty()) {
            Operand operand(expr);
            if (resolve) operand.ast = resolve(expr);
            line.arguments.append(operand);
        }
        return finalized(std::move(line));
    }
    if (line.functionName == QLatin1String("HTML_PRINT")) {
        const QString expr = trimmed.mid(first.text.length()).trimmed();
        if (!expr.isEmpty()) {
            Operand operand(expr);
            if (resolve) operand.ast = resolve(expr);
            line.arguments.append(operand);
            line.printTemplate = PrintTemplateCompiler::compile(operand.ast);
        }
        return finalized(std::move(line));
    }

    // ---- PRINT 族（对齐 C# PRINT_Instruction：参数形态由**指令名后缀**决定）----
    //
    //   PRINT / PRINTL / PRINTW / PRINTC / PRINTSINGLE / PRINTPLAIN …
    //        整行剩余部分就是**字面文本**（C# STR_ArgumentBuilder），不做表达式归约；
    //   PRINTS / PRINTSL / PRINTFORMS …
    //        整行剩余部分是**一个**字符串表达式（C# STR_EXPRESSION，只按顶层逗号分项）；
    //   PRINTFORM* / PRINTPLAINFORM
    //        整行剩余部分是**一个**格式化串（C# FORM_STR，文本 + {…}/%…%）；
    //   PRINTV*
    //        落到下面的通用路径（C# SP_PRINTV：按顶层逗号/空白分项）。
    const PrintArgInfo printInfo = AstBuilder::printInfo(line.functionName);
    if (printInfo.mode != PrintArgMode::NotPrint
        && printInfo.mode != PrintArgMode::PrintV) {
        // 「命令名之后到行尾」的原文。行尾空白必须保留：eraTetris 用
        // `PRINT 　　　　SCORE 　　　　　　　　` 的尾随全角空格做列对齐。
        int ls = 0;
        while (ls < source.size() && source.at(ls).isSpace()) ++ls;
        QString rest = source.mid(ls).mid(first.text.length());
        if (!rest.isEmpty() && rest.at(0).isSpace()) rest = rest.mid(1);

        if (printInfo.mode == PrintArgMode::Literal) {
            Operand operand(rest);
            operand.isString = true;      // 字面文本（含引号，C# 不剥引号）
            line.arguments.append(operand);
            line.printTemplate = QSharedPointer<PrintTemplate>::create();
            PrintTemplatePart part;
            part.kind = PrintTemplatePart::Kind::Text;
            part.text = rest;
            line.printTemplate->parts.append(part);
            return finalized(std::move(line));
        }
        // 表达式 / 格式化串：只去掉行尾多余的逗号，保留空白（用于列对齐）
        // C# STR_EXPRESSION / FORM_STR 也是从命令名之后到行尾，不修剪空白。
        QString expr = rest;
        while (expr.endsWith(QLatin1Char(','))) {
            expr.chop(1);
        }
        if (printInfo.mode == PrintArgMode::FormStr) {
            if (!expr.isEmpty()) {
                Operand operand(expr);
                if (resolve) operand.ast = StrFormParser::parse(expr, resolve);
                line.arguments.append(operand);
                line.printTemplate = QSharedPointer<PrintTemplate>::create();
                PrintTemplatePart part;
                part.kind = operand.ast ? PrintTemplatePart::Kind::Expression : PrintTemplatePart::Kind::Text;
                part.text = operand.raw;
                part.expression = operand.ast;
                line.printTemplate->parts.append(part);
            }
            return finalized(std::move(line));
        }
        // StrExpression（含 FORMS：结果在运行期再当格式串展开）
        if (!expr.isEmpty()) {
            Operand operand(expr);
            if (wholeQuoted(expr)) {
                operand.isString = true;
                operand.raw = expr.mid(1, expr.length() - 2);
            } else if (resolve) {
                operand.ast = resolve(expr);
            }
            line.arguments.append(operand);
            line.printTemplate = QSharedPointer<PrintTemplate>::create();
            PrintTemplatePart part;
            part.kind = operand.ast ? PrintTemplatePart::Kind::Expression : PrintTemplatePart::Kind::Text;
            part.text = operand.raw;
            part.expression = operand.ast;
            line.printTemplate->parts.append(part);
        }
        return finalized(std::move(line));
    }

    // 格式化串指令：整行操作数按 StrForm 解析（文本 + {expr}/%expr%）
    if (isStrFormInstruction(line.functionName)) {
        const QString rest = trimmed.mid(first.text.length()).trimmed();
        if (!rest.isEmpty()) {
            Operand operand(rest);
            if (resolve) {
                operand.ast = StrFormParser::parse(rest, resolve);
            }
            line.arguments.append(operand);
        }
        return finalized(std::move(line));
    }

    // 其余指令：按指令规范决定切分方式（对齐 C# ArgumentParser）
    const QString remainder = trimmed.mid(first.text.length());
    int mn = 0, mx = -1;
    const ArgKind argKind = classifyInstructionKind(line.functionName.toStdString(), mn, mx);

    // 单表达式参数（INT/STR/EXPRESSION/FORM_STR）：整行作为一个表达式
    // （C# 也是「取剩余全部词」），避免把 "LINECOUNT + 1" 拆成 3 个 token。
    if (argKind == ArgKind::IntExpression || argKind == ArgKind::StrExpression
        || argKind == ArgKind::Expression || argKind == ArgKind::FormStr) {
        QString rest = remainder.trimmed();
        // 对齐 C# ReduceArguments：逗号是「项」的终结符，行尾多余的逗号被忽略
        // （eraTW 中确有 `SIF <cond>,` 这种写法）
        while (rest.endsWith(QLatin1Char(','))) {
            rest.chop(1);
            rest = rest.trimmed();
        }
        if (!rest.isEmpty()) {
            Operand operand(rest);
            if (wholeQuoted(rest)) {
                operand.isString = true;
                operand.raw = rest.mid(1, rest.length() - 2);
            } else if (argKind == ArgKind::FormStr) {
                // FORM_STR：整行是格式化串（文本 + {…}/%…%），不是表达式。
                // 否则 DATAFORM 開玩笑也要有个限度啊！(在心里) 会被当成函数调用。
                Operand form(rest);
                if (resolve) form.ast = StrFormParser::parse(rest, resolve);
                line.arguments.append(form);
                return finalized(std::move(line));
            } else if (rest.startsWith('%') || rest.startsWith('$')) {
                operand.isVariable = true;
            } else if (resolve) {
                operand.ast = resolve(rest);
            }
            line.arguments.append(operand);
            // 条件指令：整行归约为条件表达式（强类型校验用）
            if (isConditionInstruction(line.functionName) && resolve) {
                line.condition = resolve(rest);
            }
        }
        return finalized(std::move(line));
    }

    // 逗号族（CALL/ForNext/Case/Expressions）：仅按顶层逗号切分；Raw：空白+逗号
    const QStringList tokens = splitOperands(remainder, argKind == ArgKind::Raw);
    for (const QString& token : tokens) {
        Operand operand(token);
        if (wholeQuoted(token)) {
            operand.isString = true;
            operand.raw = token.mid(1, token.length() - 2);
        } else if (token.startsWith('%') || token.startsWith('$')) {
            operand.isVariable = true;
        } else if (token != QLatin1String(",") && token != QLatin1String(":")) {
            if (resolve) operand.ast = resolve(token);
        }
        line.arguments.append(operand);
    }

    // 条件指令：用「原始整行文本」作为条件表达式归约（保留引号等字面量）
    if (isConditionInstruction(line.functionName)) {
        const QString cond = remainder.trimmed();
        if (resolve && !cond.isEmpty()) line.condition = resolve(cond);
    }

    // 未知指令名：Emuera 对未登记的命令字会报错；这里留痕（含原行文本）。
    // 扩展语句（注册类 reg() 注入，registerExtensionStatement）不算未识别 ——
    // 扩展默认全启用，登记过的名字是已认识的扩展。
    if (!isKnownInstructionName(line.functionName)
        && !s_extensionStatements.contains(line.functionName)) {
        qWarning() << "[parse] 未识别的指令:" << line.functionName
                   << "原文:" << trimmed.left(80);
        // 结构化诊断：Warning 级（按宽容语义继续执行 = no-op），带位置与原文，
        // 供装载期按类汇总（见 parse_diagnostic.h / 调试与错误.md 的警告等级）。
        line.errMes = QStringLiteral("未识别的指令: %1").arg(line.functionName);
        if (diagnostics) {
            diagnostics->add(DiagSeverity::Warning, DiagCode::kUnknownInstruction,
                             line.position.toString(),
                             QStringLiteral("未识别的指令: %1").arg(line.functionName),
                             trimmed.left(80));
        }
    }

    return finalized(std::move(line));
}
