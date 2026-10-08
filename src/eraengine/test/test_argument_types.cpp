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
// test_argument_types.cpp
//
// 验证「语句参数类型化」（对齐 C# ArgumentParser/Argument*.cs）：
//   1. 指令规范表 -> ArgKind 分类（constexpr，编译期自检）
//   2. TypedArgument：操作数/参数/表达式/分支归约
//   3. 解析期「个数 + 类型」校验（typeError）
//   4. 通过 EraParseTable 汇总为结构化解析告警
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include "ast/ast_builder.h"
#include "ast/argument_parser.h"
#include "process_state.h"
#include "era_parse_table.h"

// 编译期：规范表查询
static_assert(findInstructionSpec("FOR")->kind == ArgKind::ForNext);
static_assert(findInstructionSpec("IF")->kind == ArgKind::IntExpression);
static_assert(findInstructionSpec("CALL")->kind == ArgKind::Call);
static_assert(findInstructionSpec("ENDIF")->kind == ArgKind::Void);
static_assert(findInstructionSpec("NOSUCH") == nullptr);
// `END` **不是**指令：C# Emuera 的函数表里没有 END（只有 ENDIF/ENDSELECT/…）。
// eraTW MOVEMENT_キャラ移動処理.ERB 用 `#DIM END` + `END = 0`，把它当指令会让
// 赋值被吞、`IF END` 恒假、函数早退失效（角色移动死循环）。
static_assert(findInstructionSpec("END") == nullptr);
// 原版指令补全（2026-10）：此前这些名字回落 Raw（无个数/类型校验）
static_assert(findInstructionSpec("ASSERT")->kind == ArgKind::IntExpression);
static_assert(findInstructionSpec("ADDCHARA")->kind == ArgKind::Expressions);
static_assert(findInstructionSpec("TOOLTIP_SETCOLOR")->minArgs == 2);
static_assert(findInstructionSpec("FONTBOLD")->kind == ArgKind::Void);
// 同时是内置函数的名字**不得**入规范表（行会转函数语句，加规范会误报「参数过多」）
static_assert(findInstructionSpec("PUTFORM") == nullptr);
static_assert(findInstructionSpec("STRLEN") == nullptr);

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static LogicalLine build(EraParseTable& table, const QString& text, int lineNo = 1) {
    const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
    return AstBuilder::build(text, ScriptPosition("t.ERB", lineNo, 1), resolve);
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Typed statement-argument test";
    qDebug() << "=============================";

    ProcessState state;
    EraParseTable table(&state);

    // These command arguments are not ordinary expressions. Record every failed
    // reduction, as ErbLoader does, to catch false-positive load diagnostics.
    QStringList failed;
    const AstResolver strict = [&table, &failed](const QString& text) {
        auto ast = table.expressionAst(text);
        if (!ast) failed.append(text);
        return ast;
    };
    for (const QString& text : QStringList{"CUSTOMDRAWLINE =", "CASE 1101 TO 1106",
            "CASE 4510 TO 4512", "CASE 4000 TO 4839", "CASE IS >= 8, 1 TO 3"})
        AstBuilder::build(text, {}, strict);
    check(failed.isEmpty(), "literal and CASE grammar produce no expression diagnostics");
    const auto literal = AstBuilder::build("customdrawline a, {LOCAL} ", {}, strict);
    check(literal.arguments.size() == 1 && literal.arguments.first().raw == "a, {LOCAL} ",
          "CUSTOMDRAWLINE preserves comma, form text and trailing space");
    AstBuilder::build("CASE 1 TO (", {}, strict);
    check(!failed.isEmpty(), "invalid CASE endpoint still reaches diagnostic resolver");

    failed.clear();
    for (const QString& factor : QStringList{"1.50", "0.80", "-2.5", "1e-2", "+.5"}) {
        const auto line = AstBuilder::build("TIMES LOCAL, " + factor, {}, strict);
        check(line.argument.typeOk && !line.arguments.last().ast,
              "TIMES real constant is not an integer AST: " + factor);
    }
    check(failed.isEmpty(), "TIMES valid multipliers never reach expression resolver");
    for (const QString& text : QStringList{"TIMES LOCAL, 1+2", "TIMES LOCAL, nan",
            "TIMES LOCAL, \"1.5\"", "TIMES LOCALS, 2", "TIMES LOCAL", "CUSTOMDRAWLINE"})
        check(!build(table, text).argument.typeOk, "invalid special argument diagnosed: " + text);
    const QString form = QString::fromUtf8(R"ERB(@"\@ARGS == "a,b TO c" ? 是 # 否\@")ERB");
    for (const QString& prefix : QStringList{"PRINTBUTTON ", "CALL F, ", "RETURN "}) {
        const auto line = AstBuilder::build(prefix + form + ",1", {}, strict);
        check(line.argument.operands.size() == (prefix.startsWith("CALL") ? 3 : 2),
              "FORM internal quotes and commas stay inside argument: " + prefix);
    }
    auto caseLine = AstBuilder::build("CASE " + form + ", IS\t>= 2, 3 TO 5", {}, strict);
    AstBuilder::buildCaseClauses(caseLine, strict);
    check(caseLine.caseCache.size() == 3 && caseLine.caseCache[0].kind == CaseClause::Kind::Equal
          && caseLine.caseCache[1].kind == CaseClause::Kind::IsOp
          && caseLine.caseCache[2].kind == CaseClause::Kind::Range,
          "CASE skips FORM spans for commas/TO and accepts tab after IS");
    for (const QString& target : QStringList{"COMTYPE_{ARG % 10000}",
            "CGEX_{LOCAL:(LOCAL:10)}", QString::fromUtf8(R"ERB(\@!LOCAL ? FGO # EX\@_SUMMON_{LOCAL})ERB")}) {
        const auto line = AstBuilder::build("TRYCCALLFORM " + target + ", 1", {}, strict);
        check(line.arguments.size() == 2 && line.arguments.first().raw == target,
              "CALLFORM target retains complete interpolation: " + target);
    }
    check(failed.isEmpty(), "shared FORM spans produce no false expression failures");

    failed.clear();
    for (const QString& text : QStringList{"CALL INPUTINT (0,99)", "CALL INPUTINT\t(1,2,3,4)",
            "CALL ADDS_ABNORMAL_EXP (\"拡張初体験\", LOCAL)", "CALL F, !(1), (2 + 3)"}) {
        const auto line = AstBuilder::build(text, {}, strict);
        check(line.arguments.size() == (text.contains("1,2,3,4") ? 5 : 3),
              "CALL whitespace before parentheses preserves arguments: " + text);
    }
    for (const QString& text : QStringList{"%RESULTS%", "%NAME:CHARA%", "%NAME:LOCAL%", "a,b,", "\"A\"", ""}) {
        const auto line = AstBuilder::build("ENCODETOUNI " + text, {}, strict);
        check(!line.isFunctionCall && line.argument.kind == ArgKind::FormStr
              && line.argument.typeOk && line.arguments.first().raw == text,
              "ENCODETOUNI statement keeps FORM text: " + text);
    }
    check(failed.isEmpty(), "CALL and ENCODETOUNI FORM produce no false diagnostics");
    check(!table.expressionAst("ENCODETOUNI(%RESULTS%)"), "ordinary expression rejects bare FORM");
    check(bool(table.expressionAst("ENCODETOUNI(\"A\")")), "expression function still accepts string");

    qDebug() << "\n1) 分类（ArgKind）";
    check(build(table, "FOR II, 0, 2").argument.kind == ArgKind::ForNext, "FOR -> ForNext");
    check(build(table, "IF 1 == 1").argument.kind == ArgKind::IntExpression, "IF -> IntExpression");
    check(build(table, "CALL SUB(5)").argument.kind == ArgKind::Call, "CALL -> Call");
    check(build(table, "ENDIF").argument.kind == ArgKind::Void, "ENDIF -> Void");
    check(build(table, "VARSET A, 0, 10").argument.kind == ArgKind::VarSet, "VARSET -> VarSet（SP_SET 族）");
    check(build(table, "SOMETHING 1 2").argument.kind == ArgKind::Raw, "未知指令 -> Raw");
    // `END = 0`：ERA 的 END 是变量（eraTW `#DIM END`），不是指令。
    // 回归：曾经 functionName == "END"（指令），右侧 "= 0" 被丢弃。
    check(build(table, "END = 0").functionName == QLatin1String("="),
          "END = 0 -> 赋值（END 是变量名，不是指令）");
    check(build(table, "END = 1").functionName == QLatin1String("="),
          "END = 1 -> 赋值");
    check(build(table, "ENDIF").functionName == QLatin1String("ENDIF"),
          "ENDIF 邻近关键字不受影响，仍是指令");

    qDebug() << "\n2) 归约后的参数与表达式";
    {
        const LogicalLine forLine = build(table, "FOR II, 0, 2");
        check(forLine.argument.params.size() == 3, "FOR params == [II,0,2]");
        check(forLine.argument.exprs.size() == 2, "FOR exprs == 2（0,2）");
        check(forLine.argument.params.value(0).raw == "II", "FOR 变量名 = II");
    }
    {
        const LogicalLine callLine = build(table, "CALL SUB(5)");
        check(callLine.argument.params.value(0).raw == "SUB", "CALL 函数名 = SUB");
        check(callLine.argument.exprs.size() == 1, "CALL 实参表达式 == 1");
    }
    {
        const LogicalLine ifLine = build(table, "IF 1 == 1");
        check(ifLine.argument.exprs.size() == 1, "IF exprs == 1（整行条件）");
    }

    qDebug() << "\n3) 解析期个数/类型校验";
    check(build(table, "IF 1 == 1").argument.typeOk, "IF 1==1 类型正确");
    check(!build(table, "IF \"a\"").argument.typeOk, "IF \"a\" -> 类型错误（需要整型）");
    // RETURN 对齐 C# INT_ANY：接受逗号分隔的整型表达式序列（RESULT:0..n）
    check(build(table, "RETURN 1, 2").argument.typeOk, "RETURN 1,2 -> 多值返回合法");
    check(build(table, "RETURN").argument.typeOk, "RETURN 无参合法");
    check(!build(table, "ENDIF 1").argument.typeOk, "ENDIF 1 -> 不接受参数");
    check(build(table, "SOMETHING 1 2").argument.typeOk, "未知指令宽松处理（不误报）");
    check(build(table, "FOR II, 0, 2").argument.typeOk, "FOR II,0,2 合法");
    check(!build(table, "SETFONT 1").argument.typeOk, "SETFONT 1 -> 需要字符串");
    check(build(table, "SETFONT \"msgothic\"").argument.typeOk, "SETFONT \"..\" 合法");

    // ---- 原版指令补全（2026-10）：此前回落 Raw（无个数/类型校验）----
    // ASSERT <整型表达式>（整行一个表达式：`1 == 1` 不能被空白切散）
    check(build(table, "ASSERT 1 == 1").argument.typeOk, "ASSERT 1 == 1 合法（整行归约）");
    check(!build(table, "ASSERT").argument.typeOk, "ASSERT 无参 -> 参数过少");
    // 注：引号字面量按既有规则豁免（isString 跳过类型检查，同 IF 的实现）；
    // 用静态字符串变量 RESULTS 验证整型检查确实生效。
    check(!build(table, "ASSERT RESULTS").argument.typeOk, "ASSERT RESULTS（字符串变量）-> 需要整型");
    // 多参族必须走 Expressions（逗号族逐参归约），不能用 IntExpression（整行归约）
    check(build(table, "ADDCHARA 3").argument.typeOk, "ADDCHARA 3 合法");
    check(build(table, "ADDCHARA 3, 5").argument.typeOk,
          "ADDCHARA 3, 5 合法（多参，按顶层逗号切分）");
    check(!build(table, "ADDCHARA").argument.typeOk, "ADDCHARA 无参 -> 参数过少");
    check(build(table, "TOOLTIP_SETCOLOR 0, 0").argument.typeOk, "TOOLTIP_SETCOLOR 0,0 合法");
    check(!build(table, "TOOLTIP_SETCOLOR 0").argument.typeOk, "TOOLTIP_SETCOLOR 0 -> 参数过少");
    // VOID 族
    check(build(table, "FONTBOLD").argument.typeOk, "FONTBOLD 无参合法");
    check(!build(table, "FONTBOLD 1").argument.typeOk, "FONTBOLD 1 -> 参数过多");
    check(build(table, "FONTSTYLE").argument.typeOk, "FONTSTYLE 无参合法（可省略）");
    check(build(table, "FONTSTYLE 1 + 2").argument.typeOk, "FONTSTYLE 1 + 2 合法");
    // 裸记号型（CALLEVENT <事件名>：eraTW 不加引号）——用 Expressions 只校验个数
    check(build(table, "CALLEVENT EVENTTURNEND").argument.typeOk, "CALLEVENT 裸名合法");

    qDebug() << "\n4) 结构化解析告警（EraParseTable）";
    AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
    QList<LogicalLine> script;
    script << AstBuilder::build("@MAIN", ScriptPosition("t.ERB", 0, 1), resolve);
    script << AstBuilder::build("IF \"bad\"", ScriptPosition("t.ERB", 1, 1), resolve);
    script << AstBuilder::build("RETURN 1, 2", ScriptPosition("t.ERB", 2, 1), resolve);
    script << AstBuilder::build("ENDIF", ScriptPosition("t.ERB", 3, 1), resolve);
    table.loadScript("warn", script);
    table.finalizeParse();   // 参数/类型校验在类型回填之后进行
    check(table.parseWarningCount() == 1, QString("告警数 == 1（得到 %1）").arg(table.parseWarningCount()));
    if (table.parseWarningCount() > 0) {
        check(table.parseWarnings().first().contains("t.ERB:1"), "首条告警含位置 t.ERB:1");
    }

    qDebug() << "\n=============================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] typed-argument tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
