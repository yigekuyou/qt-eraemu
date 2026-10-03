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
// test_builtin_functions.cpp
//
// 验证 AST 中的「内部命令（内置函数）表达式」实现：
//   1. 内置函数目录（uiltin_functions）完整性 —— 对齐 C# FunctionMethodCreator
//   2. 解析期函数解析：内置 / 用户自定义 / 未定义 三分
//   3. 参数个数 + 逐位类型校验（复刻 FunctionMethod.CheckArgumentType）
//   4. 求值：数学 / 字符串 / 数组 / 位 / 编码相关 / 配置
//   5. EraParseTable 集成：校验告警汇总（finalizeParse）
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "ast/function_types.h"
#include "ast/expression_ast.h"
#include "ast/expression_lexer.h"
#include "ast/expression_parser.h"
#include "ast/expression_evaluator.h"
#include "ast/ast_builder.h"
#include "ast/variable_table.h"
#include "constant_table.h"
#include "process_state.h"
#include "era_parse_table.h"
#include "variable_storage.h"

// ---- 编译期自检 ----
static_assert(findBuiltinFunction("STRLENS") != nullptr);
static_assert(findBuiltinFunction("MAXARRAY") != nullptr);
static_assert(findBuiltinFunction("GCREATE") != nullptr);
static_assert(findBuiltinFunction("NOSUCHFUNCTION") == nullptr);
static_assert(builtinFunctionReturnType("GETTIME") == OperandType::Int);
static_assert(builtinFunctionReturnType("GETTIMES") == OperandType::Str);
static_assert(builtinFunctionIndex("STRLENS") == builtinFunctionIndex("STRLENS"));

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static QSharedPointer<ExpressionNode> parse(const QString& text, ExpressionParser* outParser = nullptr) {
    static ExpressionLexer lexer;
    static ExpressionParser parser;
    if (outParser) *outParser = parser;
    return parser.parse(lexer.tokenize(text, 1));
}

static QSharedPointer<ExpressionNode> parseWith(ExpressionParser& parser, const QString& text) {
    ExpressionLexer lexer;
    return parser.parse(lexer.tokenize(text, 1));
}

static const BuiltinFunctionSpec& spec(const char* name) {
    const BuiltinFunctionSpec* s = findBuiltinFunction(name);
    Q_ASSERT(s != nullptr);
    return *s;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Built-in function (internal command) expressions test";
    qDebug() << "====================================================";

    // =====================================================================
    qDebug() << "\n1) 内置函数目录完整性";
    check(kBuiltinFunctionCount >= 160, QString("目录条目 %1 >= 160").arg(kBuiltinFunctionCount));
    {
        bool dup = false;
        for (std::size_t i = 0; i < kBuiltinFunctionCount && !dup; ++i) {
            for (std::size_t j = i + 1; j < kBuiltinFunctionCount; ++j) {
                if (kBuiltinFunctions[i].name == kBuiltinFunctions[j].name) { dup = true; break; }
            }
        }
        check(!dup, "无重名条目");
    }
    {
        bool badRange = false;
        bool badPattern = false;
        for (const auto& s : kBuiltinFunctions) {
            if (s.maxArgs >= 0 && s.maxArgs < s.minArgs) badRange = true;
            for (char c : s.argPattern) {
                if (!(c == 'a' || c == 'i' || c == 's' || c == 'v'
                      || c == 'n' || c == 't' || c == 'A' || c == '1' || c == 'c')) badPattern = true;
            }
        }
        check(!badRange, "参数个数范围合法（min <= max）");
        check(!badPattern, "参数形态模式字符合法");
    }
    // C# methodDic 的代表性条目
    const char* kJsNames[] = {"GETCHARA", "CSVNAME", "CSVCSTR", "FINDCHARA", "VARSIZE",
                              "CHKFONT", "GETTIME", "GETTIMES", "ABS", "SIGN", "RAND",
                              "MIN", "MAX", "LIMIT", "SUMARRAY", "MAXARRAY", "INRANGEARRAY",
                              "GETBIT", "GETNUM", "STRLENS", "STRLENSU", "SUBSTRING",
                              "STRFIND", "STRCOUNT", "TOSTR", "TOINT", "TOHALF", "TOFULL",
                              "REPLACE", "UNICODE", "ENCODETOUNI", "CHARATU", "STRFORM",
                              "STRJOIN", "GETCONFIG", "GETCONFIGS", "MONEYSTR", "BARSTR",
                              "HTML_ESCAPE", "GCREATE", "SPRITECREATE", "CBGSETG"};
    int missing = 0;
    for (const char* n : kJsNames) if (!isBuiltinFunction(n)) { ++missing; qDebug() << "    缺:" << n; }
    check(missing == 0, QString("C# methodDic 代表条目齐全（缺 %1）").arg(missing));

    check(builtinFunctionReturnType("GETTIMES") == OperandType::Str, "GETTIMES -> Str");
    check(builtinFunctionReturnType("CSVCSTR") == OperandType::Str, "CSVCSTR -> Str");
    check(builtinFunctionReturnType("MAXARRAY") == OperandType::Int, "MAXARRAY -> Int");
    {
        int mn = 0, mx = 0;
        check(builtinFunctionArgRange("RAND", mn, mx) && mn == 1 && mx == 2, "RAND 参数 1..2");
        check(builtinFunctionArgRange("MIN", mn, mx) && mx == -1, "MIN 参数 1..*");
        check(builtinFunctionArgRange("STRLENS", mn, mx) && mn == 1 && mx == 1, "STRLENS 参数 1..1");
        check(!builtinFunctionArgRange("NOPE", mn, mx), "未登记函数无参数范围");
    }

    // =====================================================================
    qDebug() << "\n2) 解析期函数解析（内置 / 用户 / 未定义）";
    {
        auto f = parse("STRLENS(\"ab\")");
        auto* fn = f ? static_cast<FunctionNode*>(f.get()) : nullptr;
        check(fn && fn->kind() == NodeKind::Function, "STRLENS(...) 解析为函数调用");
        check(fn && fn->isBuiltin(), "STRLENS 识别为内置函数");
        check(fn && fn->arityError().isEmpty(), "STRLENS(\"ab\") 参数校验通过");
        check(f && f->valueType() == OperandType::Int, "STRLENS(...) -> Int");

        auto f2 = parse("GETTIMES()");
        check(f2 && f2->valueType() == OperandType::Str, "GETTIMES() -> Str");

        ExpressionParser userParser;
        userParser.setFunctionTypeProvider([](const QString& name) {
            return name == QLatin1String("MYSTRING") ? OperandType::Str : OperandType::Unknown;
        });
        auto u = parseWith(userParser, "MYSTRING(1)");
        auto* unf = u ? static_cast<FunctionNode*>(u.get()) : nullptr;
        check(unf && unf->isUserFunction(), "MYSTRING 识别为用户自定义函数");
        check(u && u->valueType() == OperandType::Str, "MYSTRING() -> Str（#FUNCTIONS）");

        auto x = parseWith(userParser, "NOSUCHFN(1)");
        auto* xf = x ? static_cast<FunctionNode*>(x.get()) : nullptr;
        check(xf && !xf->isBuiltin() && !xf->isUserFunction(), "NOSUCHFN 既非内置也非用户函数");
        check(xf && !xf->arityError().isEmpty(), "未定义函数带出错消息（对齐 C# ThrowException）");
        check(x && x->valueType() == OperandType::Unknown, "未定义函数类型为 Unknown");
    }

    // =====================================================================
    qDebug() << "\n3) 参数个数 / 类型校验（CheckArgumentType 复刻）";
    {
        const QList<QSharedPointer<ExpressionNode>> none;
        check(!validateBuiltinCall(spec("ABS"), none).isEmpty(), "ABS() -> 参数过少");
        check(!validateBuiltinCall(spec("ABS"), {parse("1"), parse("2")}).isEmpty(), "ABS(1,2) -> 参数过多");
        check(validateBuiltinCall(spec("ABS"), {parse("1")}).isEmpty(), "ABS(1) -> 通过");
        check(!validateBuiltinCall(spec("STRLENS"), {parse("1")}).isEmpty(), "STRLENS(1) -> 需要字符串");
        check(validateBuiltinCall(spec("STRLENS"), {parse("\"s\"")}).isEmpty(), "STRLENS(\"s\") -> 通过");
        check(!validateBuiltinCall(spec("SUMARRAY"), {parse("1")}).isEmpty(), "SUMARRAY(1) -> 需要变量");
        check(validateBuiltinCall(spec("SUMARRAY"), {parse("FLAG")}).isEmpty(), "SUMARRAY(FLAG) -> 通过");
        check(!validateBuiltinCall(spec("MAXARRAY"), {parse("RESULTS")}).isEmpty(),
              "MAXARRAY(RESULTS) -> 需要整型数组变量");
        check(validateBuiltinCall(spec("MIN"), {parse("1"), parse("2"), parse("3")}).isEmpty(),
              "MIN(1,2,3) -> 通过（变长）");
        check(!validateBuiltinCall(spec("RAND"), none).isEmpty(), "RAND() -> 参数过少");
        check(!validateBuiltinCall(spec("RAND"), {parse("1"), parse("2"), parse("3")}).isEmpty(),
              "RAND(1,2,3) -> 参数过多");

        // eraTW CHARA_DIARY.ERB：LOADTEXT (1700+LOCAL)（1 参）/ SAVETEXT SAVESTR:1, (1700+RESULT)（2 参）
        // —— C# LoadTextMethod/SaveTextMethod 的尾参 force_savdir / force_UTF8 可省略。
        // 此前 min 参误写为 3/4，eraTW 的合法写法刷「参数过少」告警。
        check(validateBuiltinCall(spec("LOADTEXT"), {parse("1700")}).isEmpty(),
              "LOADTEXT(1700) -> 通过（1 参合法）");
        check(validateBuiltinCall(spec("LOADTEXT"), {parse("1700"), parse("0"), parse("0")}).isEmpty(),
              "LOADTEXT(1700,0,0) -> 通过（3 参合法）");
        check(!validateBuiltinCall(spec("LOADTEXT"), none).isEmpty(),
              "LOADTEXT() -> 参数过少");
        check(!validateBuiltinCall(spec("LOADTEXT"),
                                   {parse("1"), parse("2"), parse("3"), parse("4")}).isEmpty(),
              "LOADTEXT(1,2,3,4) -> 参数过多");
        check(validateBuiltinCall(spec("SAVETEXT"), {parse("\"s\""), parse("1700")}).isEmpty(),
              "SAVETEXT(\"s\",1700) -> 通过（2 参合法）");
        check(validateBuiltinCall(spec("SAVETEXT"),
                                  {parse("\"s\""), parse("1700"), parse("0"), parse("0")}).isEmpty(),
              "SAVETEXT(\"s\",1700,0,0) -> 通过（4 参合法）");
        check(!validateBuiltinCall(spec("SAVETEXT"), {parse("\"s\"")}).isEmpty(),
              "SAVETEXT(\"s\") -> 参数过少");

        // 解析内嵌调用时的校验（"1 + ABS()"）
        auto nested = parse("1 + ABS()");
        bool found = false;
        if (nested) {
            walkExpression(*nested, [&found](ExpressionNode& n) {
                if (n.kind() == NodeKind::Function) {
                    found = !static_cast<const FunctionNode&>(n).arityError().isEmpty();
                }
            });
        }
        check(found, "1 + ABS() 内的 ABS 报参数过少");
    }

    // =====================================================================
    qDebug() << "\n4) 求值：数学 / 位";
    VariableStorage storage;
    storage.setSystemVariable(QStringLiteral("FLAG"), 0, 1);
    storage.setSystemVariable(QStringLiteral("FLAG"), 1, 2);
    storage.setSystemVariable(QStringLiteral("FLAG"), 2, 3);
    storage.setSystemVariable(QStringLiteral("FLAG"), 3, 0);

    ExpressionEvaluator ev;
    ev.setLanguageEncoding(TextEncoding::ShiftJis);
    ev.setMoneyLabel(QStringLiteral("円"), false);
    ev.setBarChars(QLatin1Char('*'), QLatin1Char('.'));

    const auto num = [&](const QString& text) -> qint64 {
        auto n = parse(text);
        return n ? ev.evaluate(*n, &storage).toLongLong() : -999999;
    };
    const auto str = [&](const QString& text) -> QString {
        auto n = parse(text);
        return n ? ev.evaluate(*n, &storage).toString() : QStringLiteral("<null>");
    };

    storage.setGlobalInt1D("SIDE", 0, 0);
    check(num("0 && SIDE++") == 0 && num("1 || SIDE++") == 1
              && num("0 !& SIDE++") == 1 && num("1 !| SIDE++") == 0
              && storage.getGlobalInt1D("SIDE", 0) == 0,
          "短路逻辑不执行右侧自增");
    check(num("1 && SIDE++") == 0 && num("0 || SIDE++") == 1
              && storage.getGlobalInt1D("SIDE", 0) == 2,
          "需要右值时仅执行一次自增");

    check(num("ABS(-5)") == 5, "ABS(-5) == 5");
    check(num("SIGN(-3)") == -1 && num("SIGN(0)") == 0 && num("SIGN(7)") == 1, "SIGN == -1/0/1");
    check(num("LIMIT(10, 0, 5)") == 5 && num("LIMIT(-3, 0, 5)") == 0 && num("LIMIT(2, 0, 5)") == 2,
          "LIMIT(x,0,5) 夹取");
    check(num("MIN(3, 1, 2)") == 1, "MIN(3,1,2) == 1");
    check(num("MAX(3, 1, 2)") == 3, "MAX(3,1,2) == 3");
    check(num("POWER(2, 10)") == 1024, "POWER(2,10) == 1024");
    check(num("SQRT(16)") == 4, "SQRT(16) == 4");
    check(num("CBRT(27)") == 3, "CBRT(27) == 3");
    check(num("LOG10(1000)") == 3, "LOG10(1000) == 3");
    check(num("EXPONENT(0)") == 1, "EXPONENT(0) == 1");
    check(num("GETBIT(5, 0)") == 1 && num("GETBIT(5, 1)") == 0 && num("GETBIT(5, 2)") == 1,
          "GETBIT(5,0/1/2)");
    check(num("INRANGE(3, 1, 5)") == 1 && num("INRANGE(9, 1, 5)") == 0, "INRANGE");

    // =====================================================================
    qDebug() << "\n5) 求值：字符串（含语言字节长度语义）";
    check(num("STRLENS(\"AB\")") == 2, "STRLENS(\"AB\") == 2（ASCII 1 字节/字符）");
    check(num("STRLENS(\"あ\")") == 2, "STRLENS(\"あ\") == 2（Shift-JIS 2 字节）");
    check(num("STRLENSU(\"あ\")") == 1, "STRLENSU(\"あ\") == 1（UTF-16 码元）");
    check(str("SUBSTRING(\"あいう\", 2, 2)") == QStringLiteral("い"),
          "SUBSTRING(\"あいう\",2,2) == い（按语言字节位置）");
    check(str("SUBSTRINGU(\"abcde\", 1, 3)") == QStringLiteral("bcd"), "SUBSTRINGU(\"abcde\",1,3) == bcd");
    check(num("STRFIND(\"あいう\", \"う\")") == 4, "STRFIND(\"あいう\",\"う\") == 4（语言字节位置）");
    check(num("STRFINDU(\"abcde\", \"cd\")") == 2, "STRFINDU(\"abcde\",\"cd\") == 2");
    check(num("STRCOUNT(\"abcabc\", \"abc\")") == 2, "STRCOUNT == 2");
    check(str("TOSTR(123)") == QStringLiteral("123"), "TOSTR(123) == \"123\"");
    check(num("TOINT(\"123\")") == 123 && num("TOINT(\"abc\")") == 0 && num("TOINT(\"１２３\")") == 0,
          "TOINT：数字/非数字/全角");
    check(num("ISNUMERIC(\"12.5\")") == 1 && num("ISNUMERIC(\"1a\")") == 0, "ISNUMERIC");
    check(str("TOUPPER(\"aB\")") == QStringLiteral("AB"), "TOUPPER");
    check(str("TOLOWER(\"aB\")") == QStringLiteral("ab"), "TOLOWER");
    check(str("TOFULL(\"A1\")") == QString::fromUtf8("Ａ１"), "TOFULL（ASCII -> 全角）");
    check(str("TOHALF(\"Ａ１\")") == QStringLiteral("A1"), "TOHALF（全角 -> ASCII）");
    check(str("REPLACE(\"abcab\", \"ab\", \"X\")") == QStringLiteral("XcX"), "REPLACE");
    check(str("UNICODE(65)") == QStringLiteral("A"), "UNICODE(65) == A");
    check(num("ENCODETOUNI(\"A\")") == 65, "ENCODETOUNI(\"A\") == 65");
    check(str("CONVERT(255, 16)") == QStringLiteral("ff"), "CONVERT(255,16) == ff");
    check(str("CONVERT(5, 2)") == QStringLiteral("101"), "CONVERT(5,2) == 101");
    check(str("CHARATU(\"abc\", 1)") == QStringLiteral("b"), "CHARATU");
    check(!str("ESCAPE(\"a.b\")").isEmpty(), "ESCAPE 非空（正则转义）");
    check(str("MONEYSTR(100)") == QString::fromUtf8("100円"), "MONEYSTR(100) == 100円（单位在后）");
    check(str("BARSTR(5, 10, 10)") == QStringLiteral("[*****.....]"), "BARSTR(5,10,10)");
    check(str("STRFORM(\"HP={FLAG:0}\")") == QStringLiteral("HP=1"), "STRFORM 运行期展开");

    // =====================================================================
    qDebug() << "\n6) 求值：数组 / 变量 / 配置";
    check(num("SUMARRAY(FLAG, 0, 3)") == 6, "SUMARRAY(FLAG,0,3) == 1+2+3");
    check(num("MAXARRAY(FLAG, 0, 4)") == 3, "MAXARRAY(FLAG,0,4) == 3");
    check(num("MINARRAY(FLAG, 0, 3)") == 1, "MINARRAY(FLAG,0,3) == 1");
    check(num("INRANGEARRAY(FLAG, 2, 4, 0, 4)") == 2, "INRANGEARRAY(FLAG,2,4,0,4) == 2（2,3）");
    check(num("MATCH(FLAG, 2, 0, 4)") == 1, "MATCH(FLAG,2,0,4) == 1（出现 1 次）");
    check(num("FINDELEMENT(FLAG, 3, 0, 4)") == 2, "FINDELEMENT(FLAG,3) == 2");
    check(num("FINDLASTELEMENT(FLAG, 3, 0, 4)") == 2, "FINDLASTELEMENT(FLAG,3) == 2");
    check(num("GROUPMATCH(1, 1, 2, 1)") == 2, "GROUPMATCH(1,1,2,1) == 2");
    check(num("NOSAMES(1, 2, 1)") == 0 && num("NOSAMES(1, 2, 3)") == 1, "NOSAMES");
    check(num("ALLSAMES(1, 1, 1)") == 1 && num("ALLSAMES(1, 2, 1)") == 0, "ALLSAMES");
    check(num("VARSIZE(\"FLAG\")") == 10000, "VARSIZE(\"FLAG\") == 10000（默认表）");

    {
        int calls = 0;
        const QString kMoney = QString::fromUtf8("お金の単位");
        const QString kMax   = QString::fromUtf8("最大所持数");
        ev.setConfigProvider([&calls, kMoney, kMax](const QString& key, QString& value) {
            ++calls;
            if (key == kMoney) { value = QString::fromUtf8("円"); return true; }
            if (key == kMax)   { value = QStringLiteral("999"); return true; }
            return false;
        });
        check(str("GETCONFIGS(\"お金の単位\")") == QString::fromUtf8("円"), "GETCONFIGS 命中配置");
        check(num("GETCONFIG(\"最大所持数\")") == 999, "GETCONFIG 解析为整数");
        check(num("GETCONFIG(\"不存在\")") == 0, "GETCONFIG 未命中 -> 0");
        check(calls > 0, "配置回调被调用");
    }

    {
        // GETNUM：CSV 常量名 -> 下标
        QTemporaryDir tmp;
        const QString csvDir = tmp.filePath(QStringLiteral("CSV"));
        QDir().mkpath(csvDir);
        QFile f(QDir(csvDir).absoluteFilePath(QStringLiteral("FLAG.csv")));
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            ts.setEncoding(QStringConverter::Utf8);
            ts << "; c\n時間停止,10\n甲,0\n現在位置,1234\n";
        }
        ConstantTable ct;
        ct.loadCsvDirectory(csvDir, false);
        ev.setConstantTable(&ct);
        check(num("GETNUM(FLAG, \"現在位置\")") == 2, "GETNUM(FLAG,\"現在位置\") == 2");
        check(num("GETNUM(FLAG, \"未知\")") == -1, "GETNUM 未命中 -> -1");
    }

    // =====================================================================
    qDebug() << "\n7) EraParseTable 集成：函数校验告警";
    {
        ProcessState state;
        EraParseTable table(&state);
        VariableStorage st2;
        table.setVariableStorage(&st2);

        const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
        const QStringList source = {
            QStringLiteral("@MAIN"),
            QStringLiteral("A = STRLENS(1)"),      // 类型错误（需要字符串）
            QStringLiteral("B = ABS()"),           // 参数过少
            QStringLiteral("C = NOSUCHFUNC(1)"),   // 未定义函数
            QStringLiteral("D = ABS(-1)"),         // 正常
            QStringLiteral("E = SUMARRAY(3)"),     // 需要变量
        };
        QList<LogicalLine> script;
        for (int i = 0; i < source.size(); ++i) {
            script.append(AstBuilder::build(source.at(i), ScriptPosition(QStringLiteral("t.ERB"), i, 0), resolve));
        }
        table.loadScript(QStringLiteral("t"), script);
        table.finalizeParse();

        const QList<QString>& warns = table.parseWarnings();
        const QString joined = warns.join(QLatin1Char('\n'));
        check(table.parseWarningCount() >= 4,
              QString("校验告警 >= 4（实得 %1）").arg(table.parseWarningCount()));
        check(joined.contains(QStringLiteral("STRLENS")), "包含 STRLENS 类型告警");
        check(joined.contains(QStringLiteral("ABS")), "包含 ABS 参数过少告警");
        check(joined.contains(QStringLiteral("NOSUCHFUNC")), "包含未定义函数告警");
        check(joined.contains(QStringLiteral("SUMARRAY")), "包含 SUMARRAY 变量告警");
        check(!joined.contains(QStringLiteral("ABS(-1)")), "正常调用 ABS(-1) 不产生告警");
    }

    qDebug() << "\n====================================================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] built-in function tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
