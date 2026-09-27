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
// test_expression_types.cpp
//
// 验证强类型 AST：
//   1. 字面量/变量/运算符/函数/三目 的类型标注（OperandType）
//   2. 运算符表（constexpr）：优先级 + 类型签名（含编译期 static_assert）
//   3. 类型不匹配检测（BinaryOpNode::typesValid）
//   4. StrForm 解析（文本 + {expr}/%expr% + 引号）与求值
//   5. 用户自定义函数返回类型（FunctionTypeProvider）
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "operand_type.h"
#include "operator_table.h"
#include "function_types.h"
#include "expression_lexer.h"
#include "expression_parser.h"
#include "expression_ast.h"
#include "expression_evaluator.h"
#include "strform_parser.h"
#include "variable_storage.h"
#include "constant_table.h"

// ---- 编译期（C++23 constexpr）自检 ----
static_assert(operatorPrecedence(TokenType::MULTIPLY) > operatorPrecedence(TokenType::PLUS),
              "'*' 优先级高于 '+'");
static_assert(operatorPrecedence(TokenType::PLUS) > operatorPrecedence(TokenType::EQUALS),
              "'+' 优先级高于 '=='");
static_assert(*inferBinaryType(TokenType::PLUS, OperandType::Int, OperandType::Int) == OperandType::Int);
static_assert(*inferBinaryType(TokenType::PLUS, OperandType::Str, OperandType::Str) == OperandType::Str);
static_assert(*inferBinaryType(TokenType::MULTIPLY, OperandType::Str, OperandType::Int) == OperandType::Str);
static_assert(*inferBinaryType(TokenType::EQUALS, OperandType::Str, OperandType::Str) == OperandType::Int,
              "字符串比较结果仍是整数");
static_assert(!inferBinaryType(TokenType::AND, OperandType::Str, OperandType::Int).has_value(),
              "逻辑运算不接受字符串");
static_assert(!inferBinaryType(TokenType::BIT_AND, OperandType::Str, OperandType::Str).has_value(),
              "位运算不接受字符串");
static_assert(inferUnaryType(TokenType::MINUS, OperandType::Int) == OperandType::Int);
static_assert(!inferUnaryType(TokenType::MINUS, OperandType::Str).has_value());
static_assert(builtinFunctionReturnType("STRLENS") == OperandType::Int);
static_assert(builtinFunctionReturnType("TOSTR") == OperandType::Str);
static_assert(builtinFunctionReturnType("GETTIMES") == OperandType::Str);
static_assert(builtinFunctionReturnType("NOSUCHFN") == OperandType::Unknown);
static_assert(isBuiltinFunction("SUBSTRING") && !isBuiltinFunction("STRSUB"));
static_assert(kBuiltinFunctionCount >= 160);

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static QSharedPointer<ExpressionNode> parse(ExpressionParser& parser, const QString& text) {
    ExpressionLexer lexer;
    return parser.parse(lexer.tokenize(text, 1));
}

static QString typeName(OperandType t) { return QString::fromLatin1(operandTypeName(t)); }

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Strong-typed AST test";
    qDebug() << "=====================";

    ExpressionParser parser;

    qDebug() << "\n1) 字面量与变量类型";
    auto litInt = parse(parser, "42");
    auto litStr = parse(parser, "\"hi\"");
    auto varInt = parse(parser, "VA");
    auto varStr = parse(parser, "RESULTS");
    check(litInt && litInt->valueType() == OperandType::Int, "42 -> Int");
    check(litStr && litStr->valueType() == OperandType::Str, "\"hi\" -> Str");
    check(varInt && varInt->valueType() == OperandType::Int, "VA -> Int");
    check(varStr && varStr->valueType() == OperandType::Str, "RESULTS -> Str（已知字符串变量）");

    qDebug() << "\n2) 运算符类型推断";
    auto addII = parse(parser, "1 + 2");
    auto addSS = parse(parser, "\"a\" + \"b\"");
    auto cmpSS = parse(parser, "\"a\" < \"b\"");
    auto mulSI = parse(parser, "\"ab\" * 3");
    auto cmpII = parse(parser, "1 == 1");
    check(addII && addII->valueType() == OperandType::Int, "1 + 2 -> Int");
    check(addSS && addSS->valueType() == OperandType::Str, "\"a\" + \"b\" -> Str");
    check(cmpSS && cmpSS->valueType() == OperandType::Int, "\"a\" < \"b\" -> Int");
    check(mulSI && mulSI->valueType() == OperandType::Str, "\"ab\" * 3 -> Str");
    check(cmpII && cmpII->valueType() == OperandType::Int, "1 == 1 -> Int");

    qDebug() << "\n3) 类型不匹配检测";
    auto badAdd = parse(parser, "1 + \"x\"");
    auto badAnd = parse(parser, "\"a\" && 1");
    check(badAdd && !static_cast<BinaryOpNode*>(badAdd.get())->typesValid(), "1 + \"x\" 标记为类型不匹配");
    check(badAnd && !static_cast<BinaryOpNode*>(badAnd.get())->typesValid(), "\"a\" && 1 标记为类型不匹配");

    qDebug() << "\n4) 一元与三目";
    auto neg = parse(parser, "-5");
    auto tern = parse(parser, "1 ? \"a\" # \"b\"");
    check(neg && neg->valueType() == OperandType::Int, "-5 -> Int");
    check(tern && tern->valueType() == OperandType::Str, "1 ? \"a\" # \"b\" -> Str");

    qDebug() << "\n5) 函数返回类型（内置表 + 用户函数提供者）";
    auto strlen = parse(parser, "STRLENS(\"abcd\")");
    auto tostr = parse(parser, "TOSTR(1)");
    check(strlen && strlen->valueType() == OperandType::Int, "STRLENS(...) -> Int");
    check(tostr && tostr->valueType() == OperandType::Str, "TOSTR(...) -> Str");

    ExpressionParser userParser;
    userParser.setFunctionTypeProvider([](const QString& name) {
        return name == QLatin1String("MYNAME") ? OperandType::Str : OperandType::Unknown;
    });
    auto myname = parse(userParser, "MYNAME(1)");
    check(myname && myname->valueType() == OperandType::Str, "用户函数 MYNAME() -> Str（#FUNCTIONS）");

    qDebug() << "\n6) StrForm 解析";
    ExpressionParser sfParser;
    auto sf = StrFormParser::parse("HP={VA}/{VB}",
                                   [&sfParser](const QString& e) { return parse(sfParser, e); });
    check(sf && sf->valueType() == OperandType::Str, "StrForm -> Str");
    check(sf && sf->parts().size() == 4, "HP={VA}/{VB} -> 4 片段");
    if (sf) {
        check(sf->parts()[0].type == StrFormPartType::Text && sf->parts()[0].text == "HP=", "片段0 = 文本 HP=");
        check(sf->parts()[1].type == StrFormPartType::Expression, "片段1 = 表达式");
        check(sf->parts()[2].text == "/", "片段2 = 文本 /");
        check(sf->parts()[3].type == StrFormPartType::Expression, "片段3 = 表达式");
    }
    auto sfPct = StrFormParser::parse("%VA%円", nullptr);
    check(sfPct && sfPct->parts().size() == 2
          && sfPct->parts()[0].type == StrFormPartType::Expression
          && sfPct->parts()[1].text == "円", "%VA%円 -> [Expr, Text(円)]");
    auto sfQuote = StrFormParser::parse("\"hello\" {VA}", nullptr);
    check(sfQuote && sfQuote->parts()[0].type == StrFormPartType::Text
          && sfQuote->parts()[0].text == "hello ", "引号剥除为文本");

    qDebug() << "\n7) StrForm 求值";
    VariableStorage storage;
    storage.setGlobalInt1D("VA", 0, 30);
    storage.setGlobalInt1D("VB", 0, 7);
    ExpressionEvaluator evaluator;
    ExpressionParser evalParser;
    const auto resolve = [&evalParser](const QString& e) { return parse(evalParser, e); };
    auto sfEval = StrFormParser::parse("HP={VA}/{VB}", resolve);
    const QString rendered = evaluator.evaluate(*sfEval, &storage).toString();
    check(rendered == "HP=30/7", QString("StrForm 求值 == HP=30/7（得到 %1）").arg(rendered));

    qDebug() << "\n8) 系统变量类型表（system_variables.h）";
    check(parse(parser, "RESULTS")->valueType() == OperandType::Str, "RESULTS -> Str");
    check(parse(parser, "GLOBALS:3")->valueType() == OperandType::Str, "GLOBALS:3 -> Str");
    check(parse(parser, "STR:5")->valueType() == OperandType::Str, "STR:5 -> Str");
    check(parse(parser, "ARGS:0")->valueType() == OperandType::Str, "ARGS:0 -> Str");
    check(parse(parser, "CFLAG:1:2")->valueType() == OperandType::Int, "CFLAG:1:2 -> Int");
    check(parse(parser, "CSTR:1:2")->valueType() == OperandType::Str, "CSTR:1:2 -> Str");
    check(parse(parser, "FLAG:7")->valueType() == OperandType::Int, "FLAG:7 -> Int");

    qDebug() << "\n9) 下标只取单项（对齐 C# ReduceVariableArgument）";
    auto indexed = parse(parser, "LOCALS:LOCAL == \"0\" || TOINT(LOCALS:LOCAL)");
    check(indexed && indexed->valueType() == OperandType::Int,
          "LOCALS:LOCAL == \"0\" || ... -> Int（下标不会吞掉比较运算）");
    auto idxPlus = parse(parser, "A:1+2");
    check(idxPlus && idxPlus->valueType() == OperandType::Int && idxPlus->toString().contains("+"),
          "A:1+2 解析为 (A:1)+2");

    qDebug() << "\n10) 数字字面量（0x/0b/p/e）";
    check(parse(parser, "0b11111")->toString().contains("31"), "0b11111 -> 31");
    check(parse(parser, "1p3")->toString().contains("8"), "1p3 -> 8");
    check(parse(parser, "0x1FG") == nullptr || parse(parser, "0x1F")->toString().contains("31"),
          "0x1F -> 31");
    // 全角数字开头的是标识符，不是数字
    auto wideDigit = parse(parser, "１moreフラグ");
    check(wideDigit && wideDigit->kind() == NodeKind::Variable, "１moreフラグ 解析为标识符");

    qDebug() << "\n11) 格式化串 \\@...#...\\@ 与 @\"...\"";
    ExpressionLexer formLexer;
    ExpressionParser formParser;
    const auto formResolve = [&formParser, &formLexer](const QString& e) {
        return formParser.parse(formLexer.tokenize(e, 1));
    };
    formParser.setFormProvider([&formResolve](const QString& text, bool yenAt) {
        if (yenAt) return StrFormParser::parseYenAt(text, formResolve);
        return QSharedPointer<ExpressionNode>(StrFormParser::parse(text, formResolve));
    });
    storage.setSystemVariable(QStringLiteral("FLAG"), 0, 1);
    auto yenAt = formParser.parse(formLexer.tokenize("\\@ FLAG:0 ? %VA% # 零 \\@", 1));
    check(yenAt && yenAt->valueType() == OperandType::Str, "\\@..\\@ -> Str");
    check(yenAt && evaluator.evaluate(*yenAt, &storage).toString() == "30",
          "\\@ 1 ? %VA% # 零 \\@ -> 30");
    storage.setSystemVariable(QStringLiteral("FLAG"), 0, 0);
    check(yenAt && evaluator.evaluate(*yenAt, &storage).toString() == "零",
          "\\@ 0 ? %VA% # 零 \\@ -> 零");
    auto atStr = formParser.parse(formLexer.tokenize("@\"HP=%VA%\"", 1));
    check(atStr && evaluator.evaluate(*atStr, &storage).toString() == "HP=30",
          "@\"HP=%VA%\" -> HP=30");

    qDebug() << "\n12) 字符串下标（CSV 常量名）经 ConstantTable 解析";
    {
        QTemporaryDir tmp;
        const QString csvDir = tmp.filePath(QStringLiteral("CSV"));
        QDir().mkpath(csvDir);
        auto writeCsv = [&](const QString& name, const QString& body) {
            QFile f(QDir(csvDir).absoluteFilePath(name));
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream ts(&f);
                ts.setEncoding(QStringConverter::Utf8);
                ts << body;
            }
        };
        // 行号即下标：現在位置 在第 3 行（下标 2）
        writeCsv(QStringLiteral("FLAG.csv"),
                 QStringLiteral(";comment\n時間停止,10\n甲,0\n現在位置,1234\n"));
        writeCsv(QStringLiteral("TFLAG.csv"), QStringLiteral("一時フラグ,1\n"));

        ConstantTable ct;
        check(ct.loadCsvDirectory(csvDir, false) == 2, "装载 2 个常量表");
        check(ct.indexOf(QStringLiteral("FLAG.CSV"), QStringLiteral("現在位置")) == 2,
              "FLAG.CSV 中 現在位置 -> 2（跳过注释行，行号即下标）");
        check(ConstantTable::csvForVariable(QStringLiteral("flag")) == QStringLiteral("FLAG.CSV"),
              "变量名大小写不敏感 FLAG -> FLAG.CSV");

        ExpressionEvaluator idxEval;
        idxEval.setConstantTable(&ct);
        storage.setSystemVariable(QStringLiteral("FLAG"), 2, 1234);
        ExpressionLexer idxLexer;
        ExpressionParser idxParser;
        idxParser.setConstantNameProvider([&ct](const QString& var, const QString& name) {
            return ct.indexForVariable(var, name) >= 0;
        });
        const auto parse1 = [&](const QString& e) { return idxParser.parse(idxLexer.tokenize(e, 1)); };
        auto strIdx = parse1("FLAG:現在位置");
        check(strIdx && idxEval.evaluate(*strIdx, &storage).toLongLong() == 1234,
              "FLAG:現在位置 -> FLAG[2]（字符串下标映射为行号 2）");
        auto numIdx = parse1("FLAG:3");
        storage.setSystemVariable(QStringLiteral("FLAG"), 3, 99);
        check(numIdx && idxEval.evaluate(*numIdx, &storage).toLongLong() == 99,
              "FLAG:3 -> FLAG[3]（整数下标不受影响）");
    }

    qDebug() << "\n=====================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] strong-typed AST tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
