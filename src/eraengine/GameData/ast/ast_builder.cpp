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
#include <QCoreApplication>
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
            // `'=` 是字符串赋值运算符，**不是**引号（对齐 C# LexicalAnalyzer：`'` 只在
            // AllowSingleQuotationStr 上下文里才是字符串界符）。此前一律把 `'` 当引号，
            // 于是 `X '= SUBSTRINGU(…); 说明` 的 ';' 被吞进"字符串"，注释没被剥掉，
            // 右值整段解析失败 ->「赋值右值无法解析或包含空项」。
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('=')) continue;
            quote = c;
            continue;
        }
        if (c == QLatin1Char(';')) return text.left(i);
    }
    return text;
}

// 构建完成后做「语句参数类型化 + 解析期校验」
LogicalLine finalized(LogicalLine line) {
    line.runnerInstruction = classifyRunnerInstruction(line.functionName);
    // Every literal string parameter has a load-time LiteralNode as well.
    for (Operand& operand : line.arguments) {
        if (operand.isString && !operand.ast)
            operand.ast = QSharedPointer<LiteralNode>::create(operand.raw);
    }
    ArgumentParser::build(line);
    // 构建期未定型的表达式（解析失败或类型未知）：标记为待收尾补齐。
    // 构建期已定型的行不再进入收尾重解析（"收尾只扫描未定义"）。
    for (const Operand& operand : line.arguments) {
        if (!operand.ast || (operand.ast && operand.ast->valueType() == OperandType::Unknown))
            line.needsCompletion = true;
    }
    if (line.condition && line.condition->valueType() == OperandType::Unknown)
        line.needsCompletion = true;
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

// FORM 字面量/条件 FORM 在装载期必须走 StrFormParser，不能送进普通表达式
// parser。CALL 的字符串参数尤其常见：`CALL F, @"文本/\\@A ? 男 # 女\\@"`。
bool isFormArgument(const QString& text) {
    const QString t = text.trimmed();
    // Only language FORM terms, not percent operators or text inside plain quotes.
    return t.startsWith(QLatin1String("@\"")) || t.startsWith(QLatin1String("\\@"));
}

void parseFormOperand(Operand& operand, const QString& text, const AstResolver& resolve) {
    operand.raw = text;
    // Use the expression frontend to remove FORM delimiters and preserve composite
    // string expressions. Its FORM provider splits content into StrFormPart nodes.
    if (resolve) operand.ast = resolve(text);
}

// Quoted operands keep their unquoted raw value for the legacy string consumers,
// while also carrying the same load-time node required by CALLF and other AST-only
// execution paths.
void parseQuotedOperand(Operand& operand, const QString& text) {
    operand.isString = true;
    operand.raw = text.mid(1, text.length() - 2);
    operand.ast = QSharedPointer<LiteralNode>::create(operand.raw);
}

// FORMS: compile statically known string content; variable/function reads remain
// independent expression operations. Never reinterpret their result as source.
QSharedPointer<ExpressionNode> compileFormsParts(const QSharedPointer<ExpressionNode>& node,
                                               const AstResolver& resolve) {
    if (!node) return {};
    if (node->kind() == NodeKind::Literal && node->isString())
        return StrFormParser::parse(static_cast<const LiteralNode&>(*node).strValue(), resolve);
    if (node->kind() == NodeKind::BinaryOp) {
        const auto& binary = static_cast<const BinaryOpNode&>(*node);
        if (binary.op().type() == TokenType::PLUS && binary.valueType() == OperandType::Str) {
            auto left = compileFormsParts(binary.left(), resolve);
            auto right = compileFormsParts(binary.right(), resolve);
            if (!left || !right) return {};
            return QSharedPointer<StrFormNode>::create(QList<StrFormPart>{
                StrFormPart::makeExpr(left), StrFormPart::makeExpr(right)});
        }
    }
    if (node->kind() == NodeKind::StrForm) return node;
    return QSharedPointer<StrFormNode>::create(QList<StrFormPart>{StrFormPart::makeExpr(node)});
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
    int begin = 0;
    while (begin < text.size()) {
        const int end = StrFormParser::findTopLevel(text, ',', begin, -1);
        if (end < 0) { out << text.mid(begin).trimmed(); break; }
        out << text.mid(begin, end - begin).trimmed();
        begin = end + 1;
    }
    return out;
}

// 顶层 '=' 的位置（-1 = 无）；用于剥离形参默认值
int topLevelEquals(const QString& text) {
    return StrFormParser::findTopLevel(text, '=', 0, -1);
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
//
// name 在**所有生产调用点**都已是大小写折叠的名字：构建期写入点只有
// `line.functionName = first.text.toUpper()`（见 build()），赋值运算符与 ++/-- 都不是
// CALL 族。因此这里不再逐行 `toUpper()`（eraTW 收尾 3/6 约 186 万指令行，
// 每行一次 QString 堆分配）。
// 为保住「大小写不敏感」的对外语义，比较用 QString::compare(..., CaseInsensitive)：
// 逐字符折叠、**不分配**，代价与一次普通 QString 比较同量级。
bool AstBuilder::isCallFamilyInstruction(const QString& name) {
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
    if (name.isEmpty()) return false;
    // 快速排除：全部名字都以这 6 个字母开头（C/T/J/G/B/F）。名字是热路径输入，
    // 绝大多数行首字符不在其中 —— 一次比较就能挡掉。
    const QChar head = name.at(0).toUpper();   // QChar 折叠，无分配
    if (head != QLatin1Char('C') && head != QLatin1Char('T') && head != QLatin1Char('J')
        && head != QLatin1Char('G') && head != QLatin1Char('B') && head != QLatin1Char('F'))
        return false;
    for (const char* n : kNames) {
        if (name.compare(QLatin1String(n), Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

// PRINT 族的参数形态（对齐 C# PRINT_Instruction 扫描后缀选 ArgumentBuilder）：
//   PRINT…(V)     -> SP_PRINTV        逗号分隔的整型表达式
//   PRINT…(S)     -> STR_EXPRESSION   字符串表达式
//   PRINT…(FORMS) -> STR_EXPRESSION   字符串表达式（装载期编译静态片段）
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
        info.single = true;
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
        if (!QLatin1String("VSLWCKDFORMN").contains(c)) return PrintArgInfo{};
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
    // N 后缀（C# PRINT_Instruction：`if (st.CurrentEqualTo("N")) { isLineEnd = false;
    // flag |= PRINT_WAITINPUT; }`）：**不收尾**地打印（后续输出继续接在这一行上）
    // 并等待一次按键。C# 的 IsLineEnd=false 让下一行在 addDisplayLine 里
    // `deleteLine(1) + ChangeStr` 合并进本行；本移植的「未定型行」
    //（m_pendingParts/m_pendingOpen）就是同一语义，因此 N = 不换行 + 等待。
    // 名字：PRINTN / PRINTVN / PRINTSN / PRINTFORMN / PRINTFORMSN。
    if (rest.startsWith(QLatin1Char('N'))) {
        info.waitInput = true;
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

// PRINTDATA 族后缀（C# PRINT_DATA_Instruction 的构造：st.Jump(9/*PRINTDATA*/)
// 之后依次识别 K / D / L|W；没吃干净就是非法名）。
AstBuilder::PrintDataFlags AstBuilder::printDataFlags(const QString& upperName) {
    PrintDataFlags info;
    if (!upperName.startsWith(QLatin1String("PRINTDATA"))) return PrintDataFlags{};
    QString rest = upperName.mid(9);
    if (rest.startsWith(QLatin1Char('K'))) {
        info.kana = true;
        rest = rest.mid(1);
    }
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
    if (!rest.isEmpty()) return PrintDataFlags{};
    return info;
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
            if (prev == '!') continue;                       // !=

            // 位复合赋值 <<= >>= &= |= ^=（C# ReadAssignmentOperator 全集）。
            // 此前只认 += -= *= /= %=：`||` 之类不受影响，但
            //   `STAIN:ARG:3 |= STAIN:PLAYER:0`（eraTW COMF150/CCOMF70/COMF19/修
            //   助手）与 `FLAG:999 &= ~(15 << 5)`（守矢くじ）会被切成
            //   lhs="STAIN:ARG:3 |" op="="，左值解析失败 -> 整行失效。
            // `<<=`/`>>=` 必须先于单字符的 `<=`/`>=` 判断（它们的前两位是 `<<`/`>>`）。
            if ((prev == '<' || prev == '>') && i >= 2 && line.at(i - 2) == prev) {
                lhs = line.left(i - 2).trimmed();
                op = QString(prev) + prev + '=';
                rhs = line.mid(i + 1).trimmed();
            } else if (prev == '<' || prev == '>') {
                continue;                                    // <= >=
            } else if (prev == '&' || prev == '|' || prev == '^') {
                lhs = line.left(i - 1).trimmed();
                op = QString(prev) + '=';
                rhs = line.mid(i + 1).trimmed();
            } else if (prev == '+' || prev == '-' || prev == '*' || prev == '/' || prev == '%') {
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
    const int n = text.length();

    auto flush = [&]() {
        const QString t = current.trimmed();
        if (!t.isEmpty()) tokens.append(t);
        current.clear();
    };

    for (int i = 0; i < n; ++i) {
        const QChar ch = text.at(i);

        const int span = StrFormParser::expressionSpanEnd(text, i);
        if (span > i) {
            current += text.mid(i, span - i);
            i = span - 1;
            continue;
        }

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
                              const AstResolver& resolveInput,
                              const AstResolver& resolveQuiet,
                              ParseDiagnostics* diagnostics,
                              const std::function<OperandType(const QString&)>& declaredType) {
    LogicalLine line;
    line.raw = rawLine;
    line.position = position;
    // 数字字面量去重键 = 本次 build 运行（同一行同一位置只报一次）；
    // 键里带 file/line/column，跨行/跨文件本来就不会撞上。
    QSet<QString> reportedNumbers;
    const AstResolver resolve = resolveInput ? AstResolver([&](const QString& expression) -> QSharedPointer<ExpressionNode> {
        if (diagnostics) diagnoseIntegerLiterals(expression, position.filename,
            position.lineNumber, position.column + qMax(0, int(rawLine.indexOf(expression))),
            *diagnostics, reportedNumbers);
        return resolveInput(expression);
    }) : AstResolver{};


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
        // '@'/'$' 之后的**前导空白**必须跳过：C# ErbLoader 只 ShiftNext() 掉符号，
        // 名字由 LexicalAnalyzer 读（它会 SkipWhiteSpace）—— 所以 `$ GOOUT` 的标签名
        // 就是 GOOUT。eraTW 有 4 行这种写法（NASIKUZUSI.ERB 的 `$ GOOUT`、
        // COMF410 掃除.ERB 的 `$ CANCEL_LOOP` 等）；此前名字被切成空串，标签根本没登记，
        // 运行到 `GOTO GOOUT` 就报 "GOTO label not found"。
        const QString label = trimmed.mid(1).trimmed();
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
                        line.mutableLabelArgs().append(item);
                        line.mutableLabelDefaults().append(defaultValue);
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
            //
            // 后缀自增/自减**语句**同理也是赋值形状（C# 命令判定是**精确查表**
            // `GetFunctionIdentifier`，查不到就走赋值；ReadAssignmentOperator 把
            // `X ++` 归约为 Increment）：`PRINT_SKILL_NUM ++` / `PRINT_CNT ++`
            // 这类「以指令前缀开头的变量名」必须按自增处理 —— FL3.8 的技能数
            // 计数就是这么写的，被当成 PRINT 族指令后整行失效。
            bool wholeLineIncrement = false;
            for (const char* opText : {"++", "--"}) {
                if (!trimmed.endsWith(QLatin1String(opText))) continue;
                const QString body = trimmed.left(trimmed.size() - 2).trimmed();
                // 保守形状：一个裸 token（无空白/逗号），如 `PRINT_CNT++`
                wholeLineIncrement = !body.isEmpty() && !body.contains(QLatin1Char(' '))
                                     && !body.contains(QLatin1Char(','));
                break;
            }
            if (wholeLineIncrement) {
                firstIsInstruction = false;
            } else {
                QString l, o, r;
                firstIsInstruction = !(splitAssignment(trimmed, l, o, r) && isBareVariableLhs(l));
            }
        }
    }
    QString lhs, op, rhs;
    if (!firstIsInstruction && splitAssignment(trimmed, lhs, op, rhs)) {
        line.kind = LineKind::Instruction;
        line.functionName = op;
        line.assignOperator = op;
        Operand dest(lhs);
        if (resolveQuiet) dest.ast = resolveQuiet(lhs);
        else if (resolve) dest.ast = resolve(lhs);
        Operand value(rhs);
        // 右值在此处只是**临时**归约（变量类型尚未定稿）：字符串赋值的右值
        // 会被最终 applyStringAssignments() 用 StrFormParser 替换掉。用静默
        // resolver 解析，避免临时失败刷屏（705 条噪音的来源）。
        if (resolveQuiet) value.ast = resolveQuiet(rhs);
        else if (resolve) value.ast = resolve(rhs);
        // 已知字符串左值（系统变量/已登记声明）立即按原始 FORM 编译。
        // 其余声明由 markLoadComplete 的 applyStringAssignments 完成定型。
        const auto& lookup = resolveQuiet ? resolveQuiet : resolve;
        if (op == QLatin1String("=") && lookup) {
            const auto target = lookup(lhs);
            const OperandType exprType = target ? target->valueType() : OperandType::Unknown;
            const OperandType declType = declaredType ? declaredType(lhs) : OperandType::Unknown;
            if (exprType == OperandType::Str)
                value.ast = StrFormParser::parse(rhs, lookup);   // 构建期即按 FORM 定型
            else if (declType == OperandType::Int)
                ;                                                // 已声明整型：RHS 按表达式
            else
                // 左值「已声明类型」未知：构建期无法判定 RHS 是表达式还是 FORM 文本
                // ——标记为待收尾按 StrForm 定型（收尾只处理这些未定型项）。
                line.strAssignPending = true;
        }
        if (lookup && !(op == "=" && value.ast && value.ast->kind() == NodeKind::StrForm)) {
            for (const auto& text : assignmentValues(rhs)) line.assignmentAsts.append(lookup(text));
        }
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
            Operand target(body);
            if (resolveQuiet) target.ast = resolveQuiet(body);
            else if (resolve) target.ast = resolve(body);
            line.arguments = { target };
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
            Operand target(body);
            if (resolveQuiet) target.ast = resolveQuiet(body);
            else if (resolve) target.ast = resolve(body);
            line.arguments = { target };
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
        && line.functionName != QLatin1String("POWER")
        && line.functionName != QLatin1String("ENCODETOUNI")
        && line.functionName != QLatin1String("VARSIZE")
        // SAVENOS / PRINTCPERLINE 例外：语句形式是 SP_GETINT「<数值变量>」——
        // 把配置值（Config.SaveDataNos / PrintCPerLine）**写进该变量**，与 0 参
        // 式中函数同名不同形。归约成 `SAVENOS()` 会报「参数过多」，并且丢掉赋值。
        // 语句形式的 ArgKind::GetInt 登记在 argument_parser.h，语义在执行链。
        && line.functionName != QLatin1String("SAVENOS")
        && line.functionName != QLatin1String("PRINTCPERLINE")) {
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
        // Function statements use the command spelling `NAME arg1, arg2`.
        // A leading parenthesized operand is still only one operand; it does
        // not turn the remainder into a complete call.  For example,
        // `TWAIT (S_TIME:(COUNT - 1)), 0` must become
        // `TWAIT(S_TIME:(COUNT - 1), 0)`, while `TWAIT(1, 0)` is already a
        // complete call.  Find the matching outer parenthesis before deciding
        // whether the source is call syntax.
        bool completeCall = callText.startsWith(QLatin1Char('('));
        if (completeCall) {
            int depth = 0;
            bool quoted = false;
            bool escaped = false;
            int close = -1;
            for (int i = 0; i < callText.size(); ++i) {
                const QChar c = callText.at(i);
                if (quoted) {
                    if (escaped) escaped = false;
                    else if (c == QLatin1Char('\\')) escaped = true;
                    else if (c == QLatin1Char('"')) quoted = false;
                    continue;
                }
                if (c == QLatin1Char('"')) { quoted = true; continue; }
                if (c == QLatin1Char('(')) ++depth;
                else if (c == QLatin1Char(')') && --depth == 0) { close = i; break; }
            }
            completeCall = close == callText.size() - 1;
        }
        const QString exprText = completeCall
            ? line.functionName + callText
            : line.functionName + QLatin1Char('(') + callText + QLatin1Char(')');
        Operand call(exprText);
        if (resolve) call.ast = resolve(exprText);
        line.arguments.append(call);
        line.isFunctionCall = true;
        return finalized(std::move(line));
    }

    // C# ENCODETOUNI statement uses FORM_STR_NULLABLE, unlike its expression
    // function. Quotes and commas are literal FORM text; an empty argument is valid.
    if (line.functionName == QLatin1String("ENCODETOUNI")) {
        const QString text = trimmed.mid(first.text.length()).trimmed();
        Operand form(text);
        if (resolve) form.ast = StrFormParser::parse(text, resolve);
        line.arguments.append(form);
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
        // 标签名 = 第一个 token（到 空白 / 逗号 / '(' 为止）。跳过标签后的空白，
        // 遇 '(' 时按括号形式取实参 —— 否则逗号形式的实参里若含 '('
        // （如 `CALL F, !(1)`），rest.indexOf('(') 会命中实参里的括号，
        // 把标签名错切成 "F, !" -> "CALL label not found"。
        int nameEnd = 0;
        while (nameEnd < rest.size()) {
            const QChar c = rest.at(nameEnd);
            if (c == '%' || c == '{') {
                const int end = c == '%' ? StrFormParser::findPercentEnd(rest, nameEnd + 1)
                    : StrFormParser::findTopLevel(rest, '}', nameEnd + 1, -1);
                if (end >= 0) { nameEnd = end + 1; continue; }
            }
            const int span = StrFormParser::expressionSpanEnd(rest, nameEnd);
            if (span > nameEnd) { nameEnd = span; continue; }
            if (c.isSpace() || c == ',' || c == '(') break;
            ++nameEnd;
        }
        funcName = rest.left(nameEnd).trimmed();
        int argsBegin = nameEnd;
        while (argsBegin < rest.size() && rest.at(argsBegin).isSpace()) ++argsBegin;
        const QChar afterName = (argsBegin < rest.size()) ? rest.at(argsBegin) : QChar();
        if (afterName == QLatin1Char('(')) {
            const int close = rest.lastIndexOf(')');
            args = (close > argsBegin) ? rest.mid(argsBegin + 1, close - argsBegin - 1)
                                       : rest.mid(argsBegin + 1);
        } else {
            // 逗号形式 `CALL 标签, 实参1, 实参2`（Emuera 与括号形式等价，eraTW 大量使用）。
            // 标签与实参表之间**只有一个**分隔逗号：对齐 C# SP_CALL_ArgumentBuilder ——
            // LexicalAnalyzer.ReadString 停在 ','，随后 wc.ShiftNext() 只跳掉这一个分隔符，
            // 剩下的交给 ReduceArguments（空项记为 null 占位）。所以 `CALL F, , X` 是
            // 「第 1 实参省略、第 2 实参 = X」，多跳逗号会让实参整体左移（eraMegaten /
            // FL3.8 大量 `CALL MSG, , 地址, ...` 会因此串位）。
            int argStart = nameEnd;
            while (argStart < rest.size() && rest.at(argStart).isSpace()) ++argStart;
            if (argStart < rest.size() && rest.at(argStart) == QLatin1Char(',')) ++argStart;
            while (argStart < rest.size() && rest.at(argStart).isSpace()) ++argStart;
            if (argStart < rest.size()) args = rest.mid(argStart);
        }
        if (funcName.startsWith('@')) funcName = funcName.mid(1);

        Operand target(funcName);
        // FUNC uses SP_CALLFORM too: every list target, including a static
        // name, is a FORM AST. The execution side only evaluates this AST.
        if ((line.functionName.contains(QLatin1String("FORM"))
             || line.functionName == QLatin1String("FUNC")) && resolve) {
            target.ast = StrFormParser::parse(funcName, resolve);
        }
        line.arguments.append(target);
        if (!args.trimmed().isEmpty()) {
            // 对齐 C# ExpressionParser.ReduceArguments：只按**顶层**逗号切分
            // （CALL F(GET_STR(a, b, c), "x") 是 2 个实参，不是 5 个）
            const QStringList argList = splitTopLevelComma(args);
            for (const QString& a : argList) {
                const QString t = a.trimmed();
                // CALLF 的逗号参数语法允许省略参数（例如 `F(, "x")`）。
                // 空片段是一个**省略实参位**，必须占位（见 Operand::omitted），
                // 否则 `CALLF OBJNAME_TO_ID(, "RESET", CLASS_NAME)` 会被读成
                // `OBJNAME_TO_ID("RESET", CLASS_NAME)`，实参整体左移。
                // 对齐 C# ReduceArguments：空项作为 null 加入实参表。
                if (t.isEmpty()) {
                    Operand placeholder;
                    placeholder.omitted = true;
                    placeholder.ast = QSharedPointer<LiteralNode>::create(0);
                    line.arguments.append(placeholder);
                    continue;
                }
                Operand operand(t);
                if (isFormArgument(t)) {
                    // FORM 在装载期分词并保存 StrForm AST；运行期只求值该 AST。
                    parseFormOperand(operand, t, resolve);
                } else if (wholeQuoted(t)) {
                    parseQuotedOperand(operand, t);
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
            if (isFormArgument(text)) {
                parseFormOperand(operand, text, resolve);
            } else if (wholeString) {
                parseQuotedOperand(operand, text);
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
    if (line.functionName == QLatin1String("HTML_PRINT")
        || line.functionName == QLatin1String("HTML_PRINT_ISLAND")) {
        // C# SP_HTML_PRINT（ArgumentBuilder.cs:355）：1~2 个实参 ——
        //   第 1 实参 = HTML 字符串式（FORM 展开，必填、必须字符串型）；
        //   第 2 实参 = 可选数值（EM 私家版_HTML_PRINT拡張）：非 0 时输出进
        //     **打印缓冲**（EmueraConsole.PrintHtml(str, toPrintBuffer)），
        //     见 test/data/commands/HTML_PRINT.md 的「本仓库扩展」。
        // 切分必须是**词法级**顶层逗号：HTML 文本里 rect='620px,0,…' 的逗号在
        // 引号内 —— splitTopLevelComma 经 expressionSpanEnd 认得 @"…"/"…"/'…'。
        const QString expr = trimmed.mid(first.text.length()).trimmed();
        const QStringList parts = splitTopLevelComma(expr);
        if (parts.size() > 2) {
            line.isError = true;
            line.mutableErrMes() = QCoreApplication::translate("ParseDiagnostics", "%1 参数过多（最多 2 个，实得 %2）")
                              .arg(line.functionName).arg(parts.size());
        } else if (!parts.isEmpty()) {
            Operand operand(parts.first());
            if (resolve) operand.ast = resolve(parts.first());
            line.arguments.append(operand);
            line.printTemplate = PrintTemplateCompiler::compile(operand.ast);
            if (parts.size() == 2) {
                Operand flag(parts.at(1));
                if (resolve) flag.ast = resolve(parts.at(1));
                line.arguments.append(flag);
            }
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
            line.printTemplate = QSharedPointer<CompiledPrintTemplate>::create();
            CompiledPrintTemplatePart part;
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
                line.printTemplate = QSharedPointer<CompiledPrintTemplate>::create();
                CompiledPrintTemplatePart part;
                part.kind = operand.isString ? PrintTemplatePart::Kind::Text
                                             : (operand.ast ? PrintTemplatePart::Kind::Expression : PrintTemplatePart::Kind::Text);
                part.text = operand.raw;
                part.expression = operand.ast;
                line.printTemplate->parts.append(part);
            }
            return finalized(std::move(line));
        }
        // StrExpression / FORMS：装载期生成独立的文本与求值片段
        if (!expr.isEmpty()) {
            Operand operand(expr);
            if (printInfo.forms) {
                // PRINTFORMS 在装载期拆成文本片段与独立表达式片段。
                // 有 FORM 标记时生成 StrForm AST；裸字符串变量/表达式则作为
                // 一个独立 AST 运算，不把运行期结果再次当作源码解析。
                if (resolve) operand.ast = compileFormsParts(resolve(expr), resolve);
            } else if (isFormArgument(expr)) {
                parseFormOperand(operand, expr, resolve);
            } else if (wholeQuoted(expr)) {
                parseQuotedOperand(operand, expr);
            } else if (resolve) {
                operand.ast = resolve(expr);
            }
            line.arguments.append(operand);
            line.printTemplate = QSharedPointer<CompiledPrintTemplate>::create();
            CompiledPrintTemplatePart part;
            part.kind = operand.isString ? PrintTemplatePart::Kind::Text
                                         : (operand.ast ? PrintTemplatePart::Kind::Expression : PrintTemplatePart::Kind::Text);
            part.text = operand.raw;
            part.expression = operand.ast;
            line.printTemplate->parts.append(part);
        }
        return finalized(std::move(line));
    }

    // C# AnalyzePrintV: a leading apostrophe reads literal text up to the
    // next unescaped comma (or EOL). It is not a paired quote or FORM.
    if (printInfo.mode == PrintArgMode::PrintV) {
        const QString text = trimmed.mid(first.text.length());
        int i = 0;
        while (i < text.size()) {
            while (i < text.size() && text[i].isSpace()) ++i;
            if (i >= text.size()) break;
            Operand operand;
            if (text[i] == QLatin1Char('\'')) {
                ++i;
                operand.isString = true;
                while (i < text.size() && text[i] != QLatin1Char(',')) {
                    QChar ch = text[i++];
                    if (ch == QLatin1Char('\\') && i < text.size()) {
                        const QChar escaped = text[i++];
                        if (escaped == 's') ch = QLatin1Char(' ');
                        else if (escaped == 'S') ch = QChar(0x3000);
                        else if (escaped == 't') ch = QLatin1Char('\t');
                        else if (escaped == 'n') ch = QLatin1Char('\n');
                        else ch = escaped;
                    }
                    operand.raw += ch;
                }
            } else {
                const int end = StrFormParser::findTopLevel(text, ',', i, -1);
                const int stop = end < 0 ? text.size() : end;
                operand.raw = text.mid(i, stop - i).trimmed();
                if (resolve && !operand.raw.isEmpty()) operand.ast = resolve(operand.raw);
                i = stop;
            }
            line.arguments.append(operand);
            if (i < text.size() && text[i] == ',') ++i;
        }
        return finalized(std::move(line));
    }

    // CUSTOMDRAWLINE takes literal text (C# CUSTOMDRAWLINE_Instruction),
    // including quotes, commas and spaces; it must never call the resolver.
    if (line.functionName == QLatin1String("CUSTOMDRAWLINE")) {
        QString text = source.mid(source.indexOf(first.text) + first.text.size());
        int begin = 0;
        while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
        text = text.mid(begin);
        if (!text.isEmpty()) {
            Operand operand(text);
            operand.isString = true;
            operand.ast = QSharedPointer<LiteralNode>::create(text);
            line.arguments.append(operand);
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

    // 声明型扩展语句（VARI/VARS，regOpaque 注入）：C# 在 ParseLine 里对它们
    // 走专用分支（`PopArgumentPrimitive().SubstringROS()` 直接取原文），实参
    // **不做词法**。这里整行剩余当**一个不透明操作数**；真正的「目标变量 +
    // 初值」由 EraParseTable::applyScopedVariableLine 按行文本重建
    //（装载循环里、收尾之前）。
    if (isOpaqueStatement(line.functionName)) {
        const QString rest = trimmed.mid(first.text.length()).trimmed();
        if (!rest.isEmpty()) line.arguments.append(Operand(rest));
        return finalized(std::move(line));
    }

    // 其余指令：按指令规范决定切分方式（对齐 C# ArgumentParser）
    const QString remainder = trimmed.mid(first.text.length());
    int mn = 0, mx = -1;
    ArgKind argKind = classifyInstructionKind(line.functionName.toStdString(), mn, mx);
    // 扩展语句（注册类 reg/regExpr 登记的名字）按 C# METHOD_Instruction 的实参
    // 形态：ReduceArguments = **逗号分隔的表达式项**。未登记进指令规范表的名字
    // 此前回落 Raw（空白也切），`DT_CELL_SET "T", D, "C", V + 1, 1` 的第 4 实参
    // `V + 1` 会被拆成 [V][+][1] 三个 token —— 既刷「表达式无法归约: +」，
    // 运行期 runStatement 拿到的实参也整个错位。
    if (argKind == ArgKind::Raw && s_extensionStatements.contains(line.functionName))
        argKind = ArgKind::Expressions;

    if (line.functionName == "DT_COLUMN_OPTIONS") {
        const auto parts = splitTopLevelComma(remainder);
        const auto fail = [&] { line.isError = true; line.mutableErrMes() = "Invalid DT_COLUMN_OPTIONS arguments"; };
        if (parts.size() < 3) fail();
        for (int i = 0; i < parts.size(); ++i) {
            QString text = parts[i].trimmed();
            if (i >= 2) {
                const int eq = topLevelEquals(text);
                if (eq < 0 || text.left(eq).trimmed().compare("DEFAULT", Qt::CaseInsensitive) != 0) { fail(); continue; }
                text = text.mid(eq + 1).trimmed();
            }
            Operand operand(text);
            if (resolve) operand.ast = resolve(text);
            if (!operand.ast) fail();
            if (i < 2 && operand.ast && operand.ast->valueType() != OperandType::Str) fail();
            if (i >= 2) line.mutableColumnDefaults().append(operand.ast);
            line.arguments.append(operand);
        }
        return finalized(std::move(line));
    }

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
            if (argKind == ArgKind::FormStr) {
                // The command schema determines FORM parsing before inspecting
                // leading syntax. A leading yen-at is only one part of the FORM,
                // and following text/interpolations belong to the same node.
                if (resolve) operand.ast = StrFormParser::parse(rest, resolve);
            } else if (isFormArgument(rest)) {
                parseFormOperand(operand, rest, resolve);
            } else if (wholeQuoted(rest)) {
                operand.isString = true;
                operand.raw = rest.mid(1, rest.length() - 2);
                operand.ast = QSharedPointer<LiteralNode>::create(operand.raw);
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
        if (isFormArgument(token)) {
            parseFormOperand(operand, token, resolve);
        } else if (wholeQuoted(token)) {
            parseQuotedOperand(operand, token);
        } else if (token.startsWith('%') || token.startsWith('$')) {
            operand.isVariable = true;
        } else if (token != QLatin1String(",") && token != QLatin1String(":")) {
            if (resolve && argKind != ArgKind::Case
                && !(argKind == ArgKind::Times && line.arguments.size() >= 2))
                operand.ast = resolve(token);
        }
        line.arguments.append(operand);
    }
    if (argKind == ArgKind::Case) {
        buildCaseClauses(line, resolve);
        // markLoadComplete rebuilds these clauses in their owning function scope.
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
        line.mutableErrMes() = QCoreApplication::translate("ParseDiagnostics", "未识别的指令: %1").arg(line.functionName);
        if (diagnostics) {
            // 结构化定位（LSP/MCP 直接可用）：LSP 要 0 基 (行, 列) + 长度，MCP 要数字。
            // AstBuilder 没有 token 列号（Word 只带类别+文本），但指令名就是本行的
            // **首个 token**，故 列 = 缩进 + 1、跨度 = 指令名长度 —— 足够精确，
            // 且无需为了一个列号去改词法模型。
            const int indent = raw.indexOf(trimmed);
            const int column1 = (indent >= 0 ? indent : 0) + 1;
            diagnostics->add(DiagSeverity::Warning, DiagCode::kUnknownInstruction,
                             position.filename, position.lineNumber, column1,
                             line.functionName.size(),
                             QCoreApplication::translate("ParseDiagnostics", "未识别的指令: %1").arg(line.functionName),
                             trimmed.left(80));
        }
    }

    return finalized(std::move(line));
}

namespace {
// 顶层逗号切分（跳过引号与括号嵌套）—— 供 CASE 的参数列表使用
QStringList splitCaseArgs(const QString& text) {
    return splitTopLevelComma(text);
}
// CASE 臂解析（对齐 C# CASE_ArgumentBuilder）：只认引号/括号外的 IS / TO。
// perf：CASE 行装载后不可变，结果缓存在 LogicalLine::caseCache —— 首次
// caseMatches 时解析一次（含 expressionAst 预解析），之后直接复用。
// 此前每字符 × 每 CASE 臂都要重新 splitCaseArgs + expressionAst，是 eraTW
// 地图逐字符 SELECTCASE 热路径上的最大热点。
} // namespace

void AstBuilder::buildCaseClauses(const LogicalLine& caseLine, const AstResolver& resolve)
{
    caseLine.caseCache.clear();
    QString spec = stripLineComment(caseLine.raw).trimmed();
    // 去掉前导 'CASE'
    if (spec.left(4).compare(QLatin1String("CASE"), Qt::CaseInsensitive) == 0) {
        spec = spec.mid(4);
    }
    spec = spec.trimmed();

    for (const QString& part : splitCaseArgs(spec)) {
        const QString t = part.trimmed();
        if (t.isEmpty()) continue;
        CaseClause clause;
        // `CASE IS >= n` / `CASE IS>=n`：'IS' 之后允许没有空白。
        // eraTW 的 M_KOJO_K17_イベント.ERB 就写 `CASE IS>=K17C_思慕诶嘿嘿`；
        // 旧代码要求 t[2] 是空白，于是整段 `IS>=…` 落到 Equal 臂，
        // 既解析不出表达式（新解析器把裸 IS 判为无效标识符 -> 「表达式无法归约」），
        // 该 CASE 臂也永远不命中。判据改为「IS 后面不是标识符字符」。
        const bool startsWithIs = t.size() > 2
            && t.left(2).compare("IS", Qt::CaseInsensitive) == 0
            && !t.at(2).isLetterOrNumber();
        if (startsWithIs) {
            const QString rest = t.mid(2).trimmed();
            static const QStringList ops = {"<=", ">=", "==", "!=", "<", ">"};
            bool opMatched = false;
            for (const QString& op : ops) {
                if (!rest.startsWith(op)) continue;
                clause.kind = CaseClause::Kind::IsOp;
                clause.op = op;
                clause.textA = rest.mid(op.size()).trimmed();
                opMatched = true;
                break;
            }
            if (!opMatched) continue;   // 与原实现一致：无操作数的 IS 臂被忽略
        } else {
            int to = -1, depth = 0;
            for (int i = 0; i < t.size(); ++i) {
                const QChar c = t[i];
                const int span = StrFormParser::expressionSpanEnd(t, i);
                if (span > i) { i = span - 1; continue; }
                if (c == '(' || c == '[') ++depth;
                if (c == ')' || c == ']') --depth;
                if (depth == 0 && i > 0 && i + 2 < t.size() && t[i-1].isSpace()
                    && t.mid(i, 2).compare("TO", Qt::CaseInsensitive) == 0 && t[i+2].isSpace()) {
                    to = i;
                    break;
                }
            }
            if (to >= 0) {
                clause.kind = CaseClause::Kind::Range;
                clause.textA = t.left(to).trimmed();
                clause.textB = t.mid(to + 2).trimmed();
            } else {
                clause.kind = CaseClause::Kind::Equal;
                clause.textA = t;
            }
        }
        if (resolve) {
            clause.astA = resolve(clause.textA);
            if (clause.kind == CaseClause::Kind::Range)
                clause.astB = resolve(clause.textB);
        }
        // 纯字符串字面量（无转义 / 无 %..% 形式）预存字面值：匹配期直接 QString 比较
        if (clause.kind == CaseClause::Kind::Equal) {
            const QString& s = clause.textA;
            if (s.size() >= 2 && s.startsWith(QLatin1Char('"'))
                && s.endsWith(QLatin1Char('"'))) {
                bool simple = true;
                for (int i = 1; i + 1 < s.size(); ++i) {
                    const QChar c = s.at(i);
                    if (c == QLatin1Char('"') || c == QLatin1Char('\\')
                        || c == QLatin1Char('%')) {
                        simple = false;
                        break;
                    }
                }
                if (simple) {
                    clause.isSimpleStr = true;
                    clause.strLiteral = s.mid(1, s.size() - 2);
                }
            }
        }
        caseLine.caseCache.append(clause);
    }
    caseLine.caseCacheReady = true;
}

QStringList AstBuilder::assignmentValues(const QString& text) {
    QStringList values;
    int start = 0;
    while (true) {
        const int end = StrFormParser::findTopLevel(text, ',', start, -1);
        if (end < 0) {
            const QString value = text.mid(start).trimmed();
            if (!value.isEmpty() || values.isEmpty()) values.append(value);
            return values;
        }
        values.append(text.mid(start, end-start).trimmed());
        start = end + 1;
    }
}
