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
// test_index_bounds.cpp —— 「下标/尺寸越界」两处判定的护栏
//
// 背景：3/6（validateExpression）与 6/6（checkStaticValues）都判越界。
// 本测试把每个形态的**两处产物**逐字钉住，并锁死收敛后的契约：
//   * 每类事实只有一个生产者（indexbounds::issuePolicy 的 producer）；
//   * 6/6 是 3/6 的残余：同一行不允许既有 Pass3 的 RejectLine 又有 Pass6 的
//     ReportOnly（历史矛盾：`10 / 0` 曾同时得到两条）；
//   * 判定函数（下界、维度映射、常量下标、常量子树）只有 index_bounds 一份。
//
// 形态覆盖（数字 = 脚本行号）：
//   4  已声明数组的纯字面量越界        Pass3 RejectLine + 同趟告警；Pass6 让位
//   5  已声明数组的负字面量            Pass6 Warning/ReportOnly（3/6 看不见）
//   6  字面字符串下标（名表未命中）    Pass6 Error/ReportOnly
//   7  系统变量字面量越过尺寸          Pass6 Warning/ReportOnly（lengths 被清空）
//   8  系统变量负字面量                Pass6 Warning/ReportOnly
//   9  常量除零                        Pass3 RejectLine（唯一生产者；Pass6 让位）
//   10 除数字面 0、被除数含变量        Pass6 Error/ReportOnly（3/6 不求值）
//   11 二维数组缺下标                  Pass3 RejectLine
//   12 二维数组多下标                  Pass3 RejectLine
//   13 下标项数 > 3                    表达式解析先拒（3/6 的 n>3 分支用直接构造的 AST 锁）
//   14/15 常量表达式下标（10+0 / -1+0）与 18-25：装载期算出**一个字段**
//        （ExpressionNode::constantIndex），两趟只读它 —— 非负归 3/6、
//        负值归 6/6、求值失败归 3/6（见 indexbounds::issuePolicy）
//   16 VARSIZE 的下标被语言忽略        两趟都不判
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDebug>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <optional>

#include "ast/ast_builder.h"
#include "ast/expression_ast.h"
#include "ast/expression_evaluator.h"
#include "ast/index_bounds.h"
#include "ast/parse_diagnostic.h"
#include "constant_table.h"
#include "era_parse_table.h"
#include "process_state.h"
#include "variable_storage.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

namespace {

const QString kFile = QStringLiteral("bounds.ERB");

QString serialize(const ParseDiagnostic& d) {
    return QStringList{
        QString::fromLatin1(diagOriginName(d.origin)),
        QString::fromLatin1(diagSeverityName(d.severity)),
        QString::fromLatin1(diagEffectName(d.effect)),
        d.code,
        QString::number(d.line),
        d.message,
    }.join(QLatin1Char('\t'));
}

QStringList serializeAll(const ParseDiagnostics& d) {
    QStringList out;
    for (const ParseDiagnostic& x : d.all()) out.append(serialize(x));
    return out;
}

QList<ParseDiagnostic> onLine(const ParseDiagnostics& d, int line) {
    QList<ParseDiagnostic> out;
    for (const ParseDiagnostic& x : d.all())
        if (x.file == kFile && x.line == line) out.append(x);
    return out;
}

QList<ParseDiagnostic> withOriginOnLine(const ParseDiagnostics& d, int line, DiagOrigin origin) {
    QList<ParseDiagnostic> out;
    for (const ParseDiagnostic& x : onLine(d, line))
        if (x.origin == origin) out.append(x);
    return out;
}

QStringList expectRow(DiagOrigin origin, DiagSeverity sev, DiagEffect effect, const char* code,
                      int line, const QString& message) {
    return {QString::fromLatin1(diagOriginName(origin)),
            QString::fromLatin1(diagSeverityName(sev)),
            QString::fromLatin1(diagEffectName(effect)),
            QString::fromLatin1(code),
            QString::number(line),
            message};
}

// 把「该行的产物」序列化后与期望逐条（顺序敏感）比对。
bool lineEquals(const ParseDiagnostics& d, int line, const QList<QStringList>& expected,
                QString* actualText = nullptr) {
    QStringList actual;
    for (const ParseDiagnostic& x : onLine(d, line)) actual.append(serialize(x));
    QStringList wanted;
    for (const QStringList& row : expected) wanted.append(row.join(QLatin1Char('\t')));
    if (actualText) *actualText = actual.join(QStringLiteral(" ⏎ "));
    return actual == wanted;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    qDebug() << "Index/static bounds: one fact, one producer, two views";

    ProcessState state;
    EraParseTable table(&state);
    VariableStorage storage;
    ExpressionEvaluator evaluator;
    ConstantTable constantTable;   // 空表：任何名字都映射不到 -> 字符串下标必报
    table.setVariableStorage(&storage);
    table.setExpressionEvaluator(&evaluator);
    table.setConstantTable(&constantTable);

    const QStringList source = {
        QStringLiteral("@MAIN"),                    // 1
        QStringLiteral("#DIM ARR, 10"),             // 2
        QStringLiteral("#DIM A2, 3, 3"),            // 3
        QStringLiteral("PRINTV ARR:10"),            // 4
        QStringLiteral("PRINTV ARR:(-1)"),          // 5
        QStringLiteral("PRINTV ARR:\"x\""),         // 6
        QStringLiteral("PRINTV ITEM:5000"),         // 7
        QStringLiteral("PRINTV ITEM:(-1)"),         // 8
        QStringLiteral("PRINTV 10 / 0"),            // 9
        QStringLiteral("PRINTV A / 0"),             // 10
        QStringLiteral("PRINTV A2:1"),              // 11
        QStringLiteral("PRINTV A2:1:1:1"),          // 12
        QStringLiteral("PRINTV A2:1:1:1:1"),        // 13
        QStringLiteral("PRINTV ARR:(10+0)"),        // 14
        QStringLiteral("PRINTV ARR:(-1+0)"),        // 15
        QStringLiteral("VARSIZE ARR:500"),          // 16
        QStringLiteral("ARR:10 = 1"),               // 17 左值越界（另一条 3/6 路径）
        // ---- 常量表达式下标（本刀把「是不是常量、值是多少」收成装载期字段）----
        QStringLiteral("PRINTV ARR:LIDX"),          // 18 常量名（consts.ERH 的 LIDX=10）
        QStringLiteral("PRINTV ARR:(2*5)"),         // 19 常量算术（无空格）
        QStringLiteral("PRINTV ARR:(10-10)"),       // 20 常量算术（界内，负对照）
        QStringLiteral("PRINTV ARR:(10+0)"),        // 21 常量算术（越界）
        QStringLiteral("PRINTV ARR:(-1+0)"),        // 22 常量算术（负）
        QStringLiteral("PRINTV ARR:(1/0)"),         // 23 常量表达式求值失败
        QStringLiteral("PRINTV ITEM:(5000+0)"),     // 24 系统变量 + 常量算术越界
        QStringLiteral("PRINTV ITEM:(LIDX+1)"),     // 25 系统变量 + 常量名（界内，负对照）
    };
    const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
    QList<LogicalLine> lines;
    for (int i = 0; i < source.size(); ++i)
        lines.append(AstBuilder::build(source.at(i), ScriptPosition(kFile, i + 1, 1), resolve));
    // 常量放在头文件（文件级 #DIM 才是广域常量；.ERB 的 # 行必须紧跟 @label）。
    const QList<LogicalLine> constLines = {
        AstBuilder::build(QStringLiteral("#DIM CONST LIDX = 10"),
                          ScriptPosition(QStringLiteral("consts.ERH"), 1, 1), resolve),
    };
    check(table.loadScript(QStringLiteral("consts.ERH"), constLines, /*isHeaderFile=*/true),
          "常量头文件装载成功");
    check(table.loadScript(kFile, lines), "夹具装载成功");
    table.markLoadComplete();

    const ParseDiagnostics& diags = table.parseDiagnostics();
    const ScriptData* script = table.script(kFile);
    check(script != nullptr, "能取到夹具脚本");

    // ------------------------------------------------------------------
    // 1) 每行的 Pass3 / Pass6 产物（逐字、逐顺序）
    // ------------------------------------------------------------------
    qDebug() << "\n1) 形态 × 两处判定";

    const QString kArrOob = QStringLiteral("变量 ARR 常量下标越界");
    const QString kDivZero = QStringLiteral("0 による除算が行われました（运行到该行会失败）");
    const QString kNegArr = QStringLiteral("变量 ARR 的常量下标 -1 为负（下标下界恒为 0；运行期该访问静默失败）");
    const QString kNegItem = QStringLiteral("变量 ITEM 的常量下标 -1 为负（下标下界恒为 0；运行期该访问静默失败）");
    const QString kArrStr = QStringLiteral("变量 ARR 的字符串下标 \"x\" 未在常数名表中定义（运行到该行会失败）");
    const QString kItemSize = QStringLiteral("变量 ITEM 的常量下标 5000 越过其尺寸 1000（运行期读取为 0、写入被忽略）");
    const QString kDim = QStringLiteral("变量 A2 缺少参数或下标维数错误");
    const QString kTooMany = QStringLiteral("变量下标超过三项");
    const QString kLvalueOob = QStringLiteral("赋值左值必须为有效变量项（ARR:10：变量 ARR 常量下标越界）");

    // 3：3/6 判定（RejectLine）+ 同趟 validateArguments 的告警（既有行为）；6/6 让位。
    check(lineEquals(diags, 4, {
              expectRow(DiagOrigin::Pass3, DiagSeverity::Error, DiagEffect::RejectLine,
                        DiagCode::kArgCheck, 4, kArrOob),
              expectRow(DiagOrigin::Pass3, DiagSeverity::Warning, DiagEffect::ReportOnly,
                        DiagCode::kArgCheck, 4, kArrOob + QStringLiteral(" (PRINTV ARR:10)")),
          }), "4  声明数组纯字面量越界 -> Pass3 RejectLine + 同趟告警；Pass6 0 条");
    check(withOriginOnLine(diags, 4, DiagOrigin::Pass6).isEmpty(),
          "4  Pass6 对已 RejectLine 的行不再产任何事实");

    // 4：3/6 的纯字面量检查看不见 -1（UnaryOp），6/6 唯一生产者。
    check(lineEquals(diags, 5, {
              expectRow(DiagOrigin::Pass6, DiagSeverity::Warning, DiagEffect::ReportOnly,
                        DiagCode::kStaticValue, 5, kNegArr),
          }), "5  负字面量 -> 仅 Pass6 Warning/ReportOnly（3/6 看不见）");

    // 5：字符串下标只有 6/6 看。
    check(lineEquals(diags, 6, {
              expectRow(DiagOrigin::Pass6, DiagSeverity::Error, DiagEffect::ReportOnly,
                        DiagCode::kStaticValue, 6, kArrStr),
          }), "6  字面字符串下标未命中名表 -> 仅 Pass6 Error/ReportOnly");

    // 6/7：系统变量尺寸与负下标都只有 6/6 看（3/6 的 lengths 被 walkApply 清空）。
    check(lineEquals(diags, 7, {
              expectRow(DiagOrigin::Pass6, DiagSeverity::Warning, DiagEffect::ReportOnly,
                        DiagCode::kStaticValue, 7, kItemSize),
          }), "7  系统变量尺寸越界 -> 仅 Pass6 Warning/ReportOnly");
    check(lineEquals(diags, 8, {
              expectRow(DiagOrigin::Pass6, DiagSeverity::Warning, DiagEffect::ReportOnly,
                        DiagCode::kStaticValue, 8, kNegItem),
          }), "8  系统变量负下标 -> 仅 Pass6 Warning/ReportOnly");

    // 8：历史矛盾点。旧行为 = Pass3 的泛化文案 RejectLine + Pass6 的除零
    //    ReportOnly（同一行两种 effect）。现在 3/6 是唯一生产者，文案也统一。
    check(lineEquals(diags, 9, {
              expectRow(DiagOrigin::Pass3, DiagSeverity::Error, DiagEffect::RejectLine,
                        DiagCode::kArgCheck, 9, kDivZero),
              expectRow(DiagOrigin::Pass3, DiagSeverity::Warning, DiagEffect::ReportOnly,
                        DiagCode::kArgCheck, 9, kDivZero + QStringLiteral(" (PRINTV 10 / 0)")),
          }), "9  常量除零 -> Pass3 RejectLine（唯一生产者）；Pass6 0 条");
    check(withOriginOnLine(diags, 9, DiagOrigin::Pass6).isEmpty(),
          "9  同一行不再出现 Pass3 RejectLine + Pass6 ReportOnly（历史矛盾已收敛）");

    // 9：被除数含变量时 3/6 的装载期求值不参与，6/6 是唯一生产者。
    check(lineEquals(diags, 10, {
              expectRow(DiagOrigin::Pass6, DiagSeverity::Error, DiagEffect::ReportOnly,
                        DiagCode::kStaticValue, 10, kDivZero),
          }), "10 动态除零（A / 0）-> 仅 Pass6 Error/ReportOnly");

    // 11/12/13：维数/项数由 3/6 判；6/6 对 n != full 直接返回。
    check(lineEquals(diags, 11, {
              expectRow(DiagOrigin::Pass3, DiagSeverity::Error, DiagEffect::RejectLine,
                        DiagCode::kArgCheck, 11, kDim),
              expectRow(DiagOrigin::Pass3, DiagSeverity::Warning, DiagEffect::ReportOnly,
                        DiagCode::kArgCheck, 11, kDim + QStringLiteral(" (PRINTV A2:1)")),
          }), "11 二维数组缺下标 -> Pass3 RejectLine；Pass6 让位");
    check(lineEquals(diags, 12, {
              expectRow(DiagOrigin::Pass3, DiagSeverity::Error, DiagEffect::RejectLine,
                        DiagCode::kArgCheck, 12, kDim),
              expectRow(DiagOrigin::Pass3, DiagSeverity::Warning, DiagEffect::ReportOnly,
                        DiagCode::kArgCheck, 12, kDim + QStringLiteral(" (PRINTV A2:1:1:1)")),
          }), "12 二维数组多下标 -> Pass3 RejectLine；Pass6 让位");
    // 13：四项下标在**表达式解析**阶段就被拒（test_expression_types 锁过），
    // 3/6 的 n>3 分支在正常源码路径上不可达 —— 这里记录“两趟都没有事实”的现状；
    // n>3 的判定本身在第 4 节用直接构造的 AST 锁住。
    check(onLine(diags, 13).isEmpty(),
          "13 四项下标被表达式解析先拒 -> 两趟都没有诊断（记录现状）");

    // 14/15 + 18-25：常量表达式的唯一定值在装载期字段里；两趟只读它。
    // 非负常量（含常量名/算术）在 lengths 已知时由 3/6 判上界、RejectLine；
    // 负值（3/6 不看下界）由 6/6 报；求值失败由 3/6 按 ConstantDivisionByZero /
    // ConstantIndexInvalid 处置。
    const auto row3 = [&](int line, const QString& msg) {
        return expectRow(DiagOrigin::Pass3, DiagSeverity::Error, DiagEffect::RejectLine,
                         DiagCode::kArgCheck, line, msg);
    };
    const auto warn3 = [&](int line, const QString& msg) {
        return expectRow(DiagOrigin::Pass3, DiagSeverity::Warning, DiagEffect::ReportOnly,
                         DiagCode::kArgCheck, line, msg);
    };
    const auto row6 = [&](int line, const QString& msg) {
        return expectRow(DiagOrigin::Pass6, DiagSeverity::Warning, DiagEffect::ReportOnly,
                         DiagCode::kStaticValue, line, msg);
    };
    const auto text3 = [](int /*line*/, const QString& arg) {
        return QStringLiteral(" (") + arg + QStringLiteral(")");
    };
    check(lineEquals(diags, 14, {
              row3(14, kArrOob), warn3(14, kArrOob + text3(14, "PRINTV ARR:(10+0)")),
          }), "14 ARR:(10+0) -> 常量值 10 越界：Pass3 RejectLine + 同趟告警");
    check(lineEquals(diags, 15, {row6(15, kNegArr)}),
          "15 ARR:(-1+0) -> 常量值 -1：仅 Pass6 Warning/ReportOnly（下界归 6/6）");
    check(onLine(diags, 16).isEmpty(), "16 VARSIZE ARR:500 -> 两趟都不判（语言忽略其下标）");
    check(lineEquals(diags, 18, {
              row3(18, kArrOob), warn3(18, kArrOob + text3(18, "PRINTV ARR:LIDX")),
          }), "18 ARR:LIDX（#DIM CONST 名 = 10）-> Pass3 RejectLine + 同趟告警");
    check(lineEquals(diags, 19, {
              row3(19, kArrOob), warn3(19, kArrOob + text3(19, "PRINTV ARR:(2*5)")),
          }), "19 ARR:(2*5) -> 常量值 10 越界：Pass3 RejectLine + 同趟告警");
    check(onLine(diags, 20).isEmpty(), "20 ARR:(10-10) -> 常量值 0 界内：0 条");
    check(lineEquals(diags, 21, {
              row3(21, kArrOob), warn3(21, kArrOob + text3(21, "PRINTV ARR:(10+0)")),
          }), "21 ARR:(10+0) -> 常量值 10 越界：Pass3 RejectLine + 同趟告警");
    check(lineEquals(diags, 22, {row6(22, kNegArr)}),
          "22 ARR:(-1+0) -> 常量值 -1：仅 Pass6 Warning/ReportOnly");
    check(lineEquals(diags, 23, {
              expectRow(DiagOrigin::Pass3, DiagSeverity::Error, DiagEffect::RejectLine,
                        DiagCode::kArgCheck, 23, kDivZero),
              expectRow(DiagOrigin::Pass3, DiagSeverity::Warning, DiagEffect::ReportOnly,
                        DiagCode::kArgCheck, 23, kDivZero + QStringLiteral(" (PRINTV ARR:(1/0))")),
          }), "23 ARR:(1/0) -> 装载期求值失败（除零），Pass3 RejectLine；文案不变");
    check(withOriginOnLine(diags, 23, DiagOrigin::Pass6).isEmpty(),
          "23 常量除零行上 Pass6 让位");
    check(lineEquals(diags, 24, {row6(24, kItemSize)}),
          "24 ITEM:(5000+0) -> 常量值 5000 越过系统尺寸：仅 Pass6");
    check(onLine(diags, 25).isEmpty(),
          "25 ITEM:(LIDX+1) -> 常量值 11 界内：0 条");
    check(lineEquals(diags, 17, {
              // 同一条事实在 3/6 内部有两个入口（实参循环 + 赋值左值专用诊断）——
              // 这是既有的同趟重复，记录为现状（见报告「未改的同类隐患」）。
              expectRow(DiagOrigin::Pass3, DiagSeverity::Error, DiagEffect::RejectLine,
                        DiagCode::kArgCheck, 17, kArrOob),
              expectRow(DiagOrigin::Pass3, DiagSeverity::Error, DiagEffect::RejectLine,
                        DiagCode::kArgCheck, 17, kLvalueOob),
              expectRow(DiagOrigin::Pass3, DiagSeverity::Warning, DiagEffect::ReportOnly,
                        DiagCode::kArgCheck, 17, kLvalueOob + QStringLiteral(" (ARR:10 = 1)")),
          }), "17 赋值左值越界 -> Pass3 RejectLine（同一条处置表）；Pass6 让位");

    // ------------------------------------------------------------------
    // 2) 运行期事实：line.isError / argument.typeOk
    // ------------------------------------------------------------------
    qDebug() << "\n2) 运行期事实（line.isError / typeOk）";
    if (script) {
        const auto expectLineState = [script](int line, bool isError, bool typeOk, const char* what) {
            const LogicalLine& l = script->lines.at(line - 1);
            check(l.isError == isError && l.argument.typeOk == typeOk, QString::fromLatin1(what));
        };
        expectLineState(4, true, false, "4  RejectLine -> isError=true / typeOk=false");
        expectLineState(5, false, true, "8  仅 Pass6 -> isError=false / typeOk=true");
        expectLineState(6, false, true, "8  仅 Pass6 -> isError=false / typeOk=true");
        expectLineState(7, false, true, "8  仅 Pass6 -> isError=false / typeOk=true");
        expectLineState(8, false, true, "8  仅 Pass6 -> isError=false / typeOk=true");
        expectLineState(9, true, false, "9  常量除零 -> isError=true / typeOk=false");
        expectLineState(10, false, true, "10 动态除零 -> isError=false / typeOk=true");
        expectLineState(11, true, false, "11 缺下标 -> isError=true / typeOk=false");
        expectLineState(12, true, false, "12 多下标 -> isError=true / typeOk=false");
        expectLineState(13, false, true, "13 四项下标（解析期先拒）-> 不置错");
        expectLineState(14, true, false, "14 常量表达式越界 -> isError=true / typeOk=false");
        expectLineState(15, false, true, "15 常量表达式负下标（仅 6/6）-> 不置错");
        expectLineState(16, false, true, "16 VARSIZE -> 不置错");
        expectLineState(17, true, false, "17 左值越界 -> isError=true / typeOk=false");
        expectLineState(18, true, false, "18 常量名越界 -> isError=true / typeOk=false");
        expectLineState(19, true, false, "19 常量算术越界 -> isError=true / typeOk=false");
        expectLineState(20, false, true, "20 常量算术（界内）-> 不置错");
        expectLineState(21, true, false, "21 常量算术（越界）-> isError=true / typeOk=false");
        expectLineState(22, false, true, "22 常量算术（负，仅 6/6）-> 不置错");
        expectLineState(23, true, false, "23 常量除零 -> isError=true / typeOk=false");
        expectLineState(24, false, true, "24 系统变量常量算术越界（仅 6/6）-> 不置错");
        expectLineState(25, false, true, "25 系统变量常量名（界内）-> 不置错");
    }

    // ------------------------------------------------------------------
    // 3) 合并后的最终列表：条数 + 顺序 + 每条字段
    // ------------------------------------------------------------------
    qDebug() << "\n3) 合并后的最终诊断列表";
    const QStringList all = serializeAll(diags);
    const QStringList expectedAll = {
        row3(4, kArrOob).join(QLatin1Char('\t')),
        warn3(4, kArrOob + QStringLiteral(" (PRINTV ARR:10)")).join(QLatin1Char('\t')),
        row6(5, kNegArr).join(QLatin1Char('\t')),
        expectRow(DiagOrigin::Pass6, DiagSeverity::Error, DiagEffect::ReportOnly,
                  DiagCode::kStaticValue, 6, kArrStr).join(QLatin1Char('\t')),
        row6(7, kItemSize).join(QLatin1Char('\t')),
        row6(8, kNegItem).join(QLatin1Char('\t')),
        row3(9, kDivZero).join(QLatin1Char('\t')),
        warn3(9, kDivZero + QStringLiteral(" (PRINTV 10 / 0)")).join(QLatin1Char('\t')),
        expectRow(DiagOrigin::Pass6, DiagSeverity::Error, DiagEffect::ReportOnly,
                  DiagCode::kStaticValue, 10, kDivZero).join(QLatin1Char('\t')),
        row3(11, kDim).join(QLatin1Char('\t')),
        warn3(11, kDim + QStringLiteral(" (PRINTV A2:1)")).join(QLatin1Char('\t')),
        row3(12, kDim).join(QLatin1Char('\t')),
        warn3(12, kDim + QStringLiteral(" (PRINTV A2:1:1:1)")).join(QLatin1Char('\t')),
        row3(14, kArrOob).join(QLatin1Char('\t')),
        warn3(14, kArrOob + text3(14, "PRINTV ARR:(10+0)")).join(QLatin1Char('\t')),
        row6(15, kNegArr).join(QLatin1Char('\t')),
        row3(17, kArrOob).join(QLatin1Char('\t')),
        row3(17, kLvalueOob).join(QLatin1Char('\t')),
        warn3(17, kLvalueOob + QStringLiteral(" (ARR:10 = 1)")).join(QLatin1Char('\t')),
        row3(18, kArrOob).join(QLatin1Char('\t')),
        warn3(18, kArrOob + text3(18, "PRINTV ARR:LIDX")).join(QLatin1Char('\t')),
        row3(19, kArrOob).join(QLatin1Char('\t')),
        warn3(19, kArrOob + text3(19, "PRINTV ARR:(2*5)")).join(QLatin1Char('\t')),
        row3(21, kArrOob).join(QLatin1Char('\t')),
        warn3(21, kArrOob + text3(21, "PRINTV ARR:(10+0)")).join(QLatin1Char('\t')),
        row6(22, kNegArr).join(QLatin1Char('\t')),
        row3(23, kDivZero).join(QLatin1Char('\t')),
        warn3(23, kDivZero + QStringLiteral(" (PRINTV ARR:(1/0))")).join(QLatin1Char('\t')),
        row6(24, kItemSize).join(QLatin1Char('\t')),
    };
    check(all.size() == 29, QString("最终列表 29 条（得到 %1）").arg(all.size()));
    check(all == expectedAll, "最终列表逐条（origin/severity/effect/code/line/message）且顺序一致");
    if (all != expectedAll) {
        qDebug().noquote() << "    expected:" << expectedAll.join(QStringLiteral(" | "));
        qDebug().noquote() << "    actual  :" << all.join(QStringLiteral(" | "));
    }

    // 核心不变量：任何一行只要 Pass3 判了 RejectLine，Pass6 就必须 0 条。
    bool noCrossPassConflict = true;
    for (int line = 1; line <= source.size(); ++line) {
        bool rejected = false;
        for (const ParseDiagnostic& x : withOriginOnLine(diags, line, DiagOrigin::Pass3))
            if (x.effect == DiagEffect::RejectLine) rejected = true;
        if (rejected && !withOriginOnLine(diags, line, DiagOrigin::Pass6).isEmpty())
            noCrossPassConflict = false;
    }
    check(noCrossPassConflict, "全表：Pass3 RejectLine 的行上 Pass6 一条也没有");

    // ------------------------------------------------------------------
    // 4) 处置表本身是数据：改一个 kind 的 effect，这里立即红
    // ------------------------------------------------------------------
    qDebug() << "\n4) 处置表（issuePolicy）";
    const auto& tooManyP = indexbounds::issuePolicy(indexbounds::Kind::TooManyIndices);
    check(tooManyP.severity == DiagSeverity::Error && tooManyP.effect == DiagEffect::RejectLine
              && tooManyP.producer == DiagOrigin::Pass3,
          "TooManyIndices -> Error/RejectLine/Pass3");
    const auto& declP = indexbounds::issuePolicy(indexbounds::Kind::DeclaredLiteralOutOfRange);
    check(declP.severity == DiagSeverity::Error && declP.effect == DiagEffect::RejectLine
              && declP.producer == DiagOrigin::Pass3,
          "DeclaredLiteralOutOfRange -> Error/RejectLine/Pass3");
    const auto& negP = indexbounds::issuePolicy(indexbounds::Kind::NegativeConstantIndex);
    check(negP.severity == DiagSeverity::Warning && negP.effect == DiagEffect::ReportOnly
              && negP.producer == DiagOrigin::Pass6,
          "NegativeConstantIndex -> Warning/ReportOnly/Pass6");
    const auto& sysP = indexbounds::issuePolicy(indexbounds::Kind::SystemSizeExceeded);
    check(sysP.severity == DiagSeverity::Warning && sysP.effect == DiagEffect::ReportOnly
              && sysP.producer == DiagOrigin::Pass6,
          "SystemSizeExceeded -> Warning/ReportOnly/Pass6");
    const auto& strP = indexbounds::issuePolicy(indexbounds::Kind::UnknownStringIndex);
    check(strP.severity == DiagSeverity::Error && strP.effect == DiagEffect::ReportOnly
              && strP.producer == DiagOrigin::Pass6,
          "UnknownStringIndex -> Error/ReportOnly/Pass6");
    const auto& constDivP = indexbounds::issuePolicy(indexbounds::Kind::ConstantDivisionByZero);
    check(constDivP.severity == DiagSeverity::Error && constDivP.effect == DiagEffect::RejectLine
              && constDivP.producer == DiagOrigin::Pass3,
          "ConstantDivisionByZero -> Error/RejectLine/Pass3（3/6 是唯一生产者）");
    const auto& dynDivP = indexbounds::issuePolicy(indexbounds::Kind::DynamicDivisionByZero);
    check(dynDivP.severity == DiagSeverity::Error && dynDivP.effect == DiagEffect::ReportOnly
              && dynDivP.producer == DiagOrigin::Pass6,
          "DynamicDivisionByZero -> Error/ReportOnly/Pass6");

    // 判定函数本身：唯一一份下界/维度规则。
    check(indexbounds::isOutOfRange(-1, 10) && indexbounds::isOutOfRange(10, 10)
              && !indexbounds::isOutOfRange(9, 10) && !indexbounds::isOutOfRange(5000, 0),
          "isOutOfRange：下界 0 / 上界 size / size<=0 不判");
    check(indexbounds::elementDimension(0, 2, true, 3) == -1
              && indexbounds::elementDimension(1, 2, true, 3) == 0
              && indexbounds::elementDimension(0, 2, false, 2) == 0,
          "elementDimension：角色号位不占元素维");
    const auto* arrOobNode = dynamic_cast<const VariableNode*>(lines.at(3).arguments.first().ast.data());
    const auto* arrNegNode = dynamic_cast<const VariableNode*>(lines.at(4).arguments.first().ast.data());
    const auto litIndex = arrOobNode && !arrOobNode->indices().isEmpty()
        ? arrOobNode->indices().first() : QSharedPointer<ExpressionNode>();
    const auto negIndex = arrNegNode && !arrNegNode->indices().isEmpty()
        ? arrNegNode->indices().first() : QSharedPointer<ExpressionNode>();
    check(litIndex && litIndex->constantIndex.isValid() && litIndex->constantIndex.value == 10,
          "constantIndex 字段：字面量 10（Value）");
    check(negIndex && negIndex->constantIndex.isValid() && negIndex->constantIndex.value == -1,
          "constantIndex 字段：负字面量 -1（Value，含符号）");

    const auto constIndexField = [&lines](int lineNo) -> indexbounds::ConstantIndex {
        const auto* v = dynamic_cast<const VariableNode*>(
            lines.at(lineNo - 1).arguments.first().ast.data());
        if (!v || v->indices().isEmpty() || !v->indices().first()) return {};
        return v->indices().first()->constantIndex;
    };
    check(constIndexField(18).isValid() && constIndexField(18).value == 10,
          "constantIndex 字段：18 常量名 LIDX -> Value 10");
    check(constIndexField(19).isValid() && constIndexField(19).value == 10,
          "constantIndex 字段：19 (2*5) -> Value 10");
    check(constIndexField(20).isValid() && constIndexField(20).value == 0,
          "constantIndex 字段：20 (10-10) -> Value 0");
    check(constIndexField(21).isValid() && constIndexField(21).value == 10,
          "constantIndex 字段：21 (10+0) -> Value 10");
    check(constIndexField(22).isValid() && constIndexField(22).value == -1,
          "constantIndex 字段：22 (-1+0) -> Value -1");
    check(constIndexField(23).isInvalid(),
          "constantIndex 字段：23 (1/0) -> Invalid（是常量但求值失败）");
    check(constIndexField(24).isValid() && constIndexField(24).value == 5000,
          "constantIndex 字段：24 (5000+0) -> Value 5000");
    check(constIndexField(25).isValid() && constIndexField(25).value == 11,
          "constantIndex 字段：25 (LIDX+1) -> Value 11");

    // 运行期行为不受影响：常量表达式下标本来就求值正确；本刀只新增装载期字段，
    // evaluate() 的路径一字不改。这里直接求值下标节点，锁住结果。
    const auto indexNodeOf = [&lines](int lineNo) -> QSharedPointer<ExpressionNode> {
        const auto* v = dynamic_cast<const VariableNode*>(lines.at(lineNo - 1).arguments.first().ast.data());
        return v && !v->indices().isEmpty() ? v->indices().first() : QSharedPointer<ExpressionNode>();
    };
    const auto evalIndex = [&](int lineNo) -> std::optional<qint64> {
        const auto node = indexNodeOf(lineNo);
        if (!node) return std::nullopt;
        const QVariant v = evaluator.evaluate(*node, &storage, nullptr,
                                              eravar::internName(QStringLiteral("MAIN")));
        return v.isValid() && v.canConvert<qint64>()
            ? std::optional<qint64>(v.value<qint64>()) : std::nullopt;
    };
    check(evalIndex(18) == 10, "运行期 18 ARR:LIDX 的下标求值 = 10（#DIM CONST 名）");
    check(evalIndex(19) == 10, "运行期 19 ARR:(2*5) 的下标求值 = 10");
    check(evalIndex(20) == 0,  "运行期 20 ARR:(10-10) 的下标求值 = 0");
    check(evalIndex(21) == 10, "运行期 21 ARR:(10+0) 的下标求值 = 10");
    check(evalIndex(22) == -1, "运行期 22 ARR:(-1+0) 的下标求值 = -1");
    check(evalIndex(24) == 5000, "运行期 24 ITEM:(5000+0) 的下标求值 = 5000");
    check(evalIndex(25) == 11, "运行期 25 ITEM:(LIDX+1) 的下标求值 = 11");

    // n>3 的判定本身（正常源码路径上表达式解析会先拒，见第 1 节 13 行）：
    VariableNode fourIndices(QStringLiteral("A2"));
    for (int i = 0; i < 4; ++i)
        fourIndices.addIndex(QSharedPointer<LiteralNode>::create(qint64(i)));
    check(validateExpression(fourIndices) == kTooMany,
          "validateExpression：下标项数 > 3 -> \"变量下标超过三项\"（3/6 的 RejectLine 事实）");

    // ------------------------------------------------------------------
    // 5) 重复运行幂等：两趟各自的切片不因再跑一次而改变
    // ------------------------------------------------------------------
    qDebug() << "\n5) 幂等";
    const QString before = all.join(QLatin1Char('\n'));
    table.checkStaticValues();
    check(serializeAll(table.parseDiagnostics()).join(QLatin1Char('\n')) == before,
          "checkStaticValues 重跑一次 -> 整份诊断逐字节相同");

    if (g_failures == 0) qDebug() << "\nALL PASSED";
    else qDebug() << "\nFAILURES:" << g_failures;
    return g_failures == 0 ? 0 : 1;
}
