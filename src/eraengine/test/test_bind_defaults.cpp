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
// test_bind_defaults.cpp —— 元素形参的**默认值**：两条绑定路径必须同一语义
//
// 元素形参（`@F(A:0)` / `@F(A:0:1)`）的实参绑定在 script_runner.cpp 里有**两份**
// 实现，分别服务两种调用形态：
//
//   · 语句 `CALL F, …`        -> ScriptRunner::bindArguments / 内部 bindOne
//   · 表达式内调用 `F(…)`      -> ScriptRunner::invokeUserFunction / bindDefault
//
// 同一件事两份实现就会有「其中一份缺一半」。这里钉住的契约是**两路一致**：
// 同一个 `@F(SS:0 = "x")`，`CALL F` 与式子里的 `F()` 读到的形参值必须相同；
// 而且都必须是声明里的默认值，既不是空串、也不是整数化的 "0"。
//
// 覆盖（每组的 CALL 路径与式子路径成对出现，核心断言是成对相等）：
//   1. 字符串元素形参 `SS:0 = "x"`   —— 省略实参取默认值；两路结果相同
//   2. 同一形参**给实参**时实参覆盖默认值（两路）；再次省略仍回到默认值
//   3. 整型元素形参 `V:0 = 40` 同款（只修串不丢整）
//   4. 多维字符串元素形参 `A:0:1 = "md"`（下标不得被塌缩成 A:0）
//   5. 无默认值 + 省略实参 -> 空串（不凭空造值、不读到大域同名变量）
//   6. 默认值的文本形态：含中文、含空格、空串 `""`（空串不得整数化成 "0"）
//   7. 写入位置：默认值落在**被调段**的私有元素上（显式段身份），调用者的同名
//      私有元素不受影响 —— 上一刀「作用域永远是参数」的形状。
//
// 观测点：默认值由被调段自己的语句读回（`G_*_SEEN '= SS:0`）写入大域观测变量，
// 另有「按显式段身份直接读存储槽」的写侧证据。两条路径的观测值直接比较。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QLatin1Char>
#include <QStringList>

#include "ast/ast_builder.h"
#include "ast/expression_evaluator.h"
#include "ast/name_interner.h"
#include "ast/print_template.h"
#include "process_state.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "script_runner.h"
#include "variable_storage.h"

static int g_failures = 0;
static int g_assertions = 0;

static void check(bool cond, const QString& what) {
    ++g_assertions;
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static QList<LogicalLine> buildLines(EraParseTable& table, const QStringList& src,
                                     const QString& file) {
    const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
    QList<LogicalLine> out;
    for (int i = 0; i < src.size(); ++i)
        out.append(AstBuilder::build(src.at(i), ScriptPosition(file, i, 0), resolve));
    return out;
}

// 段身份文本 = (脚本名, 段起点行)。测试侧按脚本源码里的 @标签 行号算出来
// —— 生产代码不做这种映射（运行期一律读行上的 scopeKey）。
static QString segmentKeyOf(const QStringList& src, const QString& label,
                            const QString& file) {
    for (int i = 0; i < src.size(); ++i) {
        QString line = src.at(i).trimmed();
        if (!line.startsWith(QLatin1Char('@'))) continue;
        line.remove(0, 1);
        int cut = line.size();
        for (int k = 0; k < line.size(); ++k) {
            const QChar c = line.at(k);
            if (c == QLatin1Char('(') || c == QLatin1Char(',') || c.isSpace()) { cut = k; break; }
        }
        if (line.left(cut).compare(label, Qt::CaseInsensitive) == 0)
            return eravar::segmentKeyText(file, i);
    }
    return QString();
}

// 观测用的**大域**字符串变量声明（每组的 CALL / 式子两路各一个槽）。
// 显式声明而不是靠「未声明赋值」：字符串侧的装载期校验比整数侧严，
// 观测点必须是确定存在的大域槽，否则「读不到」会与「没写」混为一谈。
static const QStringList kObserverHeader = {
    QStringLiteral("#DIMS OBS_G1_CALL_SEEN, 2"),
    QStringLiteral("#DIMS OBS_G1_EXPR_SEEN, 2"),
    QStringLiteral("#DIMS OBS_G2_CALL_SEEN, 2"),
    QStringLiteral("#DIMS OBS_G2_EXPR_SEEN, 2"),
    QStringLiteral("#DIMS OBS_G2_CALL_GIVEN, 2"),
    QStringLiteral("#DIMS OBS_G2_EXPR_GIVEN, 2"),
    QStringLiteral("#DIMS OBS_G2_EXPR_OMIT2, 2"),
    QStringLiteral("#DIMS OBS_G4_CALL_SEEN, 2"),
    QStringLiteral("#DIMS OBS_G4_EXPR_SEEN, 2"),
    QStringLiteral("#DIMS OBS_G4_CALL_GIVEN, 2"),
    QStringLiteral("#DIMS OBS_G4_EXPR_GIVEN, 2"),
    QStringLiteral("#DIMS OBS_G4_EXPR_OMIT2, 2"),
    QStringLiteral("#DIMS OBS_G5_CALL_SEEN, 2"),
    QStringLiteral("#DIMS OBS_G5_EXPR_SEEN, 2"),
    QStringLiteral("#DIMS G6_CALL_CJK, 2"),
    QStringLiteral("#DIMS OBS_G6_EXPR_CJK, 2"),
    QStringLiteral("#DIMS G6_CALL_EMPTY, 2"),
    QStringLiteral("#DIMS OBS_G6_EXPR_EMPTY, 2"),
    QStringLiteral("#DIMS OBS_G7_CALL_SEEN, 2"),
    QStringLiteral("#DIMS OBS_G7_EXPR_SEEN, 2"),
    QStringLiteral("#DIMS OBS_G8_OMIT_MSG, 2"),
    QStringLiteral("#DIMS OBS_G8_GIVEN_MSG, 2"),
    // 组 5 的「大域同名变量」：被调段的私有同名元素不得读到它
    QStringLiteral("#DIMS NSTR, 3")
};

// 一个装载好的脚本 + 引擎全套（与 test_scope_plumbing 的夹具同形）。
struct Fixture {
    VariableStorage storage;
    ProcessState state;
    EraParseTable table;
    ExpressionEvaluator evaluator;
    ExecutionEngine engine;
    ScriptRunner runner;

    Fixture()
        : table(&state), engine(&storage, nullptr),
          runner(&table, &engine, &state, &storage) {
        table.setExpressionEvaluator(&evaluator);
        table.setVariableStorage(&storage);
        engine.setParseTable(&table);
        engine.setExpressionEvaluator(&evaluator);
        runner.setExpressionEvaluator(&evaluator);
        runner.setStepLimit(200000);
    }

    bool loadHeader(const QString& file, const QStringList& src) {
        return table.loadScript(file, buildLines(table, src, file), /*isHeaderFile=*/true);
    }

    bool load(const QString& file, const QStringList& src, const QString& entry) {
        if (!loadHeader(QStringLiteral("bind_defaults.ERH"), kObserverHeader)) return false;
        const bool ok = table.loadScript(file, buildLines(table, src, file));
        if (!ok) return false;
        table.setEntryPoint(entry);
        table.markLoadComplete();
        return true;
    }

    ExecState run() { return runner.runToCompletion(); }

    qint64 globalInt(const QString& name) const {
        return storage.getGlobalInt1DInCurrentFrame(name, 0);
    }
    // 大域字符串观测槽：显式段身份 = kGlobalScope（不读「谁是当前帧」）
    QString globalStr(const QString& name) const {
        return storage.getStringSlot(name, {0}, eravar::kGlobalScope).text;
    }
    // 被调段的私有元素：显式段身份直读存储槽
    QString calleeStr(const QString& var, const QList<int>& indices,
                      const QStringList& src, const QString& label, const QString& file) {
        return storage.getStringSlot(var, indices, scopeOf(src, label, file)).text;
    }
    qint64 calleeInt(const QString& var, const QList<int>& indices,
                     const QStringList& src, const QString& label, const QString& file) {
        return storage.readIntByName(var, indices, scopeOf(src, label, file));
    }
    eravar::ScopeId scopeOf(const QStringList& src, const QString& label,
                            const QString& file) {
        return eravar::internName(segmentKeyOf(src, label, file));
    }
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Element-parameter defaults: statement CALL vs expression call";
    qDebug() << "===========================================================";

    // =====================================================================
    qDebug() << "\n1) 核心：`@F(SS:0 = \"x\")` 两条绑定路径结果相同";
    {
        Fixture f;
        const QString file = QStringLiteral("bind1.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("CALL G1_CALL"),                 // 语句 CALL，省略实参
            QStringLiteral("OBS_G1_LEN = STRLENS(G1_FN())"),    // 表达式内调用，省略实参
            QStringLiteral("RETURN"),
            QStringLiteral("@G1_CALL(SS:0 = \"x\")"),
            QStringLiteral("#DIMS SS, 3"),
            QStringLiteral("OBS_G1_CALL_SEEN '= SS:0"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G1_FN(SS:0 = \"x\")"),
            QStringLiteral("#FUNCTIONS"),
            QStringLiteral("#DIMS SS, 3"),
            QStringLiteral("OBS_G1_EXPR_SEEN '= SS:0"),
            QStringLiteral("RETURNF SS:0")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "1) 装载");
        check(f.run() == ExecState::Halt, "1) 脚本结束");

        const QString callSeen = f.globalStr(QStringLiteral("OBS_G1_CALL_SEEN"));
        const QString exprSeen = f.globalStr(QStringLiteral("OBS_G1_EXPR_SEEN"));
        check(callSeen == QStringLiteral("x"),
              QString("1) 语句 CALL 省略实参 -> SS:0 == 默认值 \"x\"（得到 \"%1\"）").arg(callSeen));
        check(exprSeen == QStringLiteral("x"),
              QString("1) 式子调用省略实参 -> SS:0 == 默认值 \"x\"（得到 \"%1\"）").arg(exprSeen));
        // 本刀的核心断言：同一份声明、同一个省略实参，两路必须给出同一结果。
        check(callSeen == exprSeen,
              QString("1) 【核心】两条绑定路径结果相同（CALL=\"%1\" / 式子=\"%2\"）")
                  .arg(callSeen, exprSeen));
        check(f.globalInt(QStringLiteral("OBS_G1_LEN")) == 1,
              QString("1) 式子调用的默认值同样到了返回路径（STRLENS == %1）")
                  .arg(f.globalInt(QStringLiteral("OBS_G1_LEN"))));
        check(f.calleeStr(QStringLiteral("SS"), {0}, src, QStringLiteral("G1_CALL"), file)
                  == QStringLiteral("x"),
              "1) 写侧：G1_CALL 段私有 SS:0 == \"x\"（显式段身份直读）");
        check(f.calleeStr(QStringLiteral("SS"), {0}, src, QStringLiteral("G1_FN"), file)
                  == QStringLiteral("x"),
              "1) 写侧：G1_FN 段私有 SS:0 == \"x\"（显式段身份直读）");
        check(f.storage.getStringSlotRaw(QStringLiteral("SS"), {0}).text.isEmpty(),
              QString("1) 没有创建同名裸全局槽 SS（得到 \"%1\"）")
                  .arg(f.storage.getStringSlotRaw(QStringLiteral("SS"), {0}).text));
    }

    // =====================================================================
    qDebug() << "\n2) 字符串元素形参：省略取默认值、给实参覆盖默认值（两路）";
    {
        Fixture f;
        const QString file = QStringLiteral("bind2.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("CALL G2_CALL"),                  // 省略 -> "x"
            QStringLiteral("CALL G2_CALL_GIVEN, \"y\""),     // 给实参 -> "y"
            QStringLiteral("OBS_G2_A = STRLENS(G2_FN())"),       // 省略 -> "x"
            QStringLiteral("OBS_G2_B = STRLENS(G2_FN_GIVEN(\"yy\"))"),  // 给实参 -> "yy"
            QStringLiteral("OBS_G2_C = STRLENS(G2_FN_OMIT2())"),         // 再省略 -> "x"
            QStringLiteral("RETURN"),
            QStringLiteral("@G2_CALL(SS:0 = \"x\")"),
            QStringLiteral("#DIMS SS, 3"),
            QStringLiteral("OBS_G2_CALL_SEEN '= SS:0"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G2_CALL_GIVEN(SS:0 = \"x\")"),
            QStringLiteral("#DIMS SS, 3"),
            QStringLiteral("OBS_G2_CALL_GIVEN '= SS:0"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G2_FN(SS:0 = \"x\")"),
            QStringLiteral("#FUNCTIONS"),
            QStringLiteral("#DIMS SS, 3"),
            QStringLiteral("OBS_G2_EXPR_SEEN '= SS:0"),
            QStringLiteral("RETURNF SS:0"),
            QStringLiteral("@G2_FN_GIVEN(SS:0 = \"x\")"),
            QStringLiteral("#FUNCTIONS"),
            QStringLiteral("#DIMS SS, 3"),
            QStringLiteral("OBS_G2_EXPR_GIVEN '= SS:0"),
            QStringLiteral("RETURNF SS:0"),
            QStringLiteral("@G2_FN_OMIT2(SS:0 = \"x\")"),
            QStringLiteral("#FUNCTIONS"),
            QStringLiteral("#DIMS SS, 3"),
            QStringLiteral("OBS_G2_EXPR_OMIT2 '= SS:0"),
            QStringLiteral("RETURNF SS:0")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "2) 装载");
        check(f.run() == ExecState::Halt, "2) 脚本结束");

        const QString callOmit = f.globalStr(QStringLiteral("OBS_G2_CALL_SEEN"));
        const QString exprOmit = f.globalStr(QStringLiteral("OBS_G2_EXPR_SEEN"));
        const QString callGiven = f.globalStr(QStringLiteral("OBS_G2_CALL_GIVEN"));
        const QString exprGiven = f.globalStr(QStringLiteral("OBS_G2_EXPR_GIVEN"));
        check(callOmit == QStringLiteral("x") && exprOmit == QStringLiteral("x"),
              QString("2) 省略实参两路都取默认值 \"x\"（CALL=\"%1\" / 式子=\"%2\"）")
                  .arg(callOmit, exprOmit));
        check(callOmit == exprOmit, "2) 省略实参：两路结果相同");
        check(callGiven == QStringLiteral("y"),
              QString("2) 语句 CALL 给实参覆盖默认值（得到 \"%1\"）").arg(callGiven));
        check(exprGiven == QStringLiteral("yy"),
              QString("2) 式子调用给实参覆盖默认值（得到 \"%1\"）").arg(exprGiven));
        check(f.globalInt(QStringLiteral("OBS_G2_A")) == 1
                  && f.globalInt(QStringLiteral("OBS_G2_B")) == 2
                  && f.globalInt(QStringLiteral("OBS_G2_C")) == 1,
              QString("2) 式子调用返回值长度 省略=%1 / 实参=%2 / 再省略=%3")
                  .arg(f.globalInt(QStringLiteral("OBS_G2_A")))
                  .arg(f.globalInt(QStringLiteral("OBS_G2_B")))
                  .arg(f.globalInt(QStringLiteral("OBS_G2_C"))));
        check(f.calleeStr(QStringLiteral("SS"), {0}, src, QStringLiteral("G2_FN_GIVEN"), file)
                  == QStringLiteral("yy"),
              "2) 写侧：G2_FN_GIVEN 段私有 SS:0 == \"yy\"（实参覆盖，显式段身份）");
        check(f.globalStr(QStringLiteral("OBS_G2_EXPR_OMIT2")) == QStringLiteral("x"),
              QString("2) 再次省略实参仍回到默认值（得到 \"%1\"）")
                  .arg(f.globalStr(QStringLiteral("OBS_G2_EXPR_OMIT2"))));
    }

    // =====================================================================
    qDebug() << "\n3) 整型元素形参 `V:0 = 40` 同款（防止只修串丢整）";
    {
        Fixture f;
        const QString file = QStringLiteral("bind3.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("CALL G3_CALL"),
            QStringLiteral("CALL G3_CALL_GIVEN, 8"),
            QStringLiteral("OBS_G3_A = G3_FN()"),
            QStringLiteral("OBS_G3_B = G3_FN_GIVEN(9)"),
            QStringLiteral("OBS_G3_C = G3_FN_OMIT2()"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G3_CALL(V:0 = 40)"),
            QStringLiteral("#DIM V, 3"),
            QStringLiteral("OBS_G3_CALL_SEEN = V:0"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G3_CALL_GIVEN(V:0 = 40)"),
            QStringLiteral("#DIM V, 3"),
            QStringLiteral("OBS_G3_CALL_GIVEN = V:0"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G3_FN(V:0 = 40)"),
            QStringLiteral("#FUNCTION"),
            QStringLiteral("#DIM V, 3"),
            QStringLiteral("OBS_G3_EXPR_SEEN = V:0"),
            QStringLiteral("RETURNF V:0"),
            QStringLiteral("@G3_FN_GIVEN(V:0 = 40)"),
            QStringLiteral("#FUNCTION"),
            QStringLiteral("#DIM V, 3"),
            QStringLiteral("OBS_G3_EXPR_GIVEN = V:0"),
            QStringLiteral("RETURNF V:0"),
            QStringLiteral("@G3_FN_OMIT2(V:0 = 40)"),
            QStringLiteral("#FUNCTION"),
            QStringLiteral("#DIM V, 3"),
            QStringLiteral("OBS_G3_EXPR_OMIT2 = V:0"),
            QStringLiteral("RETURNF V:0")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "3) 装载");
        check(f.run() == ExecState::Halt, "3) 脚本结束");

        const qint64 callOmit = f.globalInt(QStringLiteral("OBS_G3_CALL_SEEN"));
        const qint64 exprOmit = f.globalInt(QStringLiteral("OBS_G3_EXPR_SEEN"));
        check(callOmit == 40 && exprOmit == 40,
              QString("3) 省略实参两路都取默认值 40（CALL=%1 / 式子=%2）")
                  .arg(callOmit).arg(exprOmit));
        check(callOmit == exprOmit, "3) 省略实参：两路结果相同（整数）");
        check(f.globalInt(QStringLiteral("OBS_G3_CALL_GIVEN")) == 8,
              QString("3) 语句 CALL 给实参覆盖默认值（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("OBS_G3_CALL_GIVEN"))));
        check(f.globalInt(QStringLiteral("OBS_G3_EXPR_GIVEN")) == 9,
              QString("3) 式子调用给实参覆盖默认值（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("OBS_G3_EXPR_GIVEN"))));
        check(f.globalInt(QStringLiteral("OBS_G3_A")) == 40 && f.globalInt(QStringLiteral("OBS_G3_B")) == 9
                  && f.globalInt(QStringLiteral("OBS_G3_C")) == 40,
              QString("3) 式子调用返回值 省略=%1 / 实参=%2 / 再省略=%3")
                  .arg(f.globalInt(QStringLiteral("OBS_G3_A")))
                  .arg(f.globalInt(QStringLiteral("OBS_G3_B")))
                  .arg(f.globalInt(QStringLiteral("OBS_G3_C"))));
        check(f.calleeInt(QStringLiteral("V"), {0}, src, QStringLiteral("G3_FN"), file) == 40,
              "3) 写侧：G3_FN 段私有 V:0 == 40（显式段身份）");
        check(f.globalInt(QStringLiteral("OBS_G3_EXPR_OMIT2")) == 40,
              QString("3) 再次省略实参仍回到默认值 40（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("OBS_G3_EXPR_OMIT2"))));
    }

    // =====================================================================
    qDebug() << "\n4) 多维字符串元素形参 `MX:0:1 = \"md\"`（下标不得被塌缩）";
    {
        Fixture f;
        const QString file = QStringLiteral("bind4.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("CALL G4_CALL"),
            QStringLiteral("CALL G4_CALL_GIVEN, \"zz\""),
            QStringLiteral("OBS_G4_A = STRLENS(G4_FN())"),
            QStringLiteral("OBS_G4_B = STRLENS(G4_FN_GIVEN(\"yy\"))"),
            QStringLiteral("OBS_G4_C = STRLENS(G4_FN_OMIT2())"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G4_CALL(MX:0:1 = \"md\")"),
            QStringLiteral("#DIMS MX, 2, 3"),
            QStringLiteral("OBS_G4_CALL_SEEN '= MX:0:1"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G4_CALL_GIVEN(MX:0:1 = \"md\")"),
            QStringLiteral("#DIMS MX, 2, 3"),
            QStringLiteral("OBS_G4_CALL_GIVEN '= MX:0:1"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G4_FN(MX:0:1 = \"md\")"),
            QStringLiteral("#FUNCTIONS"),
            QStringLiteral("#DIMS MX, 2, 3"),
            QStringLiteral("OBS_G4_EXPR_SEEN '= MX:0:1"),
            QStringLiteral("RETURNF MX:0:1"),
            QStringLiteral("@G4_FN_GIVEN(MX:0:1 = \"md\")"),
            QStringLiteral("#FUNCTIONS"),
            QStringLiteral("#DIMS MX, 2, 3"),
            QStringLiteral("OBS_G4_EXPR_GIVEN '= MX:0:1"),
            QStringLiteral("RETURNF MX:0:1"),
            QStringLiteral("@G4_FN_OMIT2(MX:0:1 = \"md\")"),
            QStringLiteral("#FUNCTIONS"),
            QStringLiteral("#DIMS MX, 2, 3"),
            QStringLiteral("OBS_G4_EXPR_OMIT2 '= MX:0:1"),
            QStringLiteral("RETURNF MX:0:1")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "4) 装载");
        check(f.run() == ExecState::Halt, "4) 脚本结束");

        const QString callOmit = f.globalStr(QStringLiteral("OBS_G4_CALL_SEEN"));
        const QString exprOmit = f.globalStr(QStringLiteral("OBS_G4_EXPR_SEEN"));
        check(callOmit == QStringLiteral("md") && exprOmit == QStringLiteral("md"),
              QString("4) 省略实参两路都取默认值 \"md\"（CALL=\"%1\" / 式子=\"%2\"）")
                  .arg(callOmit, exprOmit));
        check(callOmit == exprOmit, "4) 省略实参：两路结果相同（多维）");
        check(f.globalStr(QStringLiteral("OBS_G4_CALL_GIVEN")) == QStringLiteral("zz"),
              QString("4) 语句 CALL 给实参覆盖（得到 \"%1\"）")
                  .arg(f.globalStr(QStringLiteral("OBS_G4_CALL_GIVEN"))));
        check(f.globalStr(QStringLiteral("OBS_G4_EXPR_GIVEN")) == QStringLiteral("yy"),
              QString("4) 式子调用给实参覆盖（得到 \"%1\"）")
                  .arg(f.globalStr(QStringLiteral("OBS_G4_EXPR_GIVEN"))));
        check(f.globalStr(QStringLiteral("OBS_G4_EXPR_OMIT2")) == QStringLiteral("md"),
              QString("4) 再次省略实参仍回到默认值 \"md\"（得到 \"%1\"）")
                  .arg(f.globalStr(QStringLiteral("OBS_G4_EXPR_OMIT2"))));
        check(f.globalInt(QStringLiteral("OBS_G4_A")) == 2 && f.globalInt(QStringLiteral("OBS_G4_B")) == 2
                  && f.globalInt(QStringLiteral("OBS_G4_C")) == 2,
              QString("4) 返回值长度 省略=%1 / 实参=%2 / 再省略=%3")
                  .arg(f.globalInt(QStringLiteral("OBS_G4_A")))
                  .arg(f.globalInt(QStringLiteral("OBS_G4_B")))
                  .arg(f.globalInt(QStringLiteral("OBS_G4_C"))));
        const eravar::ScopeId fnScope = f.scopeOf(src, QStringLiteral("G4_FN"), file);
        check(f.storage.getStringSlot(QStringLiteral("MX"), {0, 1}, fnScope).text
                  == QStringLiteral("md"),
              "4) 写侧：G4_FN 段私有 MX:0:1 == \"md\"");
        check(f.storage.getStringSlot(QStringLiteral("MX"), {0, 0}, fnScope).text.isEmpty(),
              "4) 下标未被塌缩：G4_FN 段私有 MX:0:0 仍为空");
    }

    // =====================================================================
    qDebug() << "\n5) 无默认值 + 省略实参 -> 空串（不凭空造值、不读同名大域）";
    {
        Fixture f;
        const QString file = QStringLiteral("bind5.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("NSTR:0 '= \"stale-global\""),   // 同名大域变量有残值
            QStringLiteral("CALL G5_CALL"),
            QStringLiteral("OBS_G5_LEN = STRLENS(G5_FN())"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G5_CALL(NSTR:0)"),
            QStringLiteral("#DIMS NSTR, 3"),               // 被调段同名私有元素
            QStringLiteral("OBS_G5_CALL_SEEN '= NSTR:0"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G5_FN(NSTR:0)"),
            QStringLiteral("#FUNCTIONS"),
            QStringLiteral("#DIMS NSTR, 3"),
            QStringLiteral("OBS_G5_EXPR_SEEN '= NSTR:0"),
            QStringLiteral("RETURNF NSTR:0")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "5) 装载");
        check(f.run() == ExecState::Halt, "5) 脚本结束");

        const QString callSeen = f.globalStr(QStringLiteral("OBS_G5_CALL_SEEN"));
        const QString exprSeen = f.globalStr(QStringLiteral("OBS_G5_EXPR_SEEN"));
        check(callSeen.isEmpty(),
              QString("5) 语句 CALL 无默认值 + 省略 -> 空串（得到 \"%1\"）").arg(callSeen));
        check(exprSeen.isEmpty(),
              QString("5) 式子调用无默认值 + 省略 -> 空串（得到 \"%1\"）").arg(exprSeen));
        check(callSeen == exprSeen, "5) 无默认值：两路结果相同");
        check(f.globalInt(QStringLiteral("OBS_G5_LEN")) == 0,
              QString("5) 式子返回值长度 == 0（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("OBS_G5_LEN"))));
        check(f.storage.getStringSlot(QStringLiteral("NSTR"), {0}, eravar::kGlobalScope).text
                  == QStringLiteral("stale-global"),
              "5) 同名大域变量仍是 \"stale-global\"（没有读到/写到大域）");
    }

    // =====================================================================
    qDebug() << "\n6) 默认值文本：含中文 / 含空格 / 空串 \"\"（不得整数化成 \"0\"）";
    {
        Fixture f;
        const QString file = QStringLiteral("bind6.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("CALL G6_CALL_CJK"),
            QStringLiteral("CALL G6_CALL_EMPTY"),
            // STRLENS 按「语言编码的字节数」计（C# LangManager.GetStrlenLang），
            // 含中文时用 STRLENSU 数码元，断言才与语言编码无关。
            QStringLiteral("OBS_G6_A = STRLENSU(G6_FN_CJK())"),
            QStringLiteral("OBS_G6_B = STRLENSU(G6_FN_EMPTY())"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G6_CALL_CJK(A:0 = \"中文 值\")"),
            QStringLiteral("#DIMS A, 3"),
            QStringLiteral("G6_CALL_CJK '= A:0"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G6_CALL_EMPTY(A:0 = \"\")"),
            QStringLiteral("#DIMS A, 3"),
            QStringLiteral("G6_CALL_EMPTY '= A:0"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G6_FN_CJK(A:0 = \"中文 值\")"),
            QStringLiteral("#FUNCTIONS"),
            QStringLiteral("#DIMS A, 3"),
            QStringLiteral("OBS_G6_EXPR_CJK '= A:0"),
            QStringLiteral("RETURNF A:0"),
            QStringLiteral("@G6_FN_EMPTY(A:0 = \"\")"),
            QStringLiteral("#FUNCTIONS"),
            QStringLiteral("#DIMS A, 3"),
            QStringLiteral("OBS_G6_EXPR_EMPTY '= A:0"),
            QStringLiteral("RETURNF A:0")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "6) 装载");
        check(f.run() == ExecState::Halt, "6) 脚本结束");

        const QString callCjk = f.globalStr(QStringLiteral("G6_CALL_CJK"));
        const QString exprCjk = f.globalStr(QStringLiteral("OBS_G6_EXPR_CJK"));
        check(callCjk == QStringLiteral("中文 值") && exprCjk == QStringLiteral("中文 值"),
              QString("6) 含中文+空格的默认值两路一致（CALL=\"%1\" / 式子=\"%2\"）")
                  .arg(callCjk, exprCjk));
        check(f.globalInt(QStringLiteral("OBS_G6_A")) == 4,
              QString("6) 中文默认值长度 4 码元（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("OBS_G6_A"))));

        const QString callEmpty = f.globalStr(QStringLiteral("G6_CALL_EMPTY"));
        const QString exprEmpty = f.globalStr(QStringLiteral("OBS_G6_EXPR_EMPTY"));
        check(callEmpty.isEmpty(),
              QString("6) 空串默认值：语句 CALL 得到空串（得到 \"%1\"，整数化会得到 \"0\"）")
                  .arg(callEmpty));
        check(exprEmpty.isEmpty(),
              QString("6) 空串默认值：式子调用得到空串（得到 \"%1\"）").arg(exprEmpty));
        check(callEmpty == exprEmpty, "6) 空串默认值：两路结果相同");
        check(f.globalInt(QStringLiteral("OBS_G6_B")) == 0,
              QString("6) 空串默认值返回长度 == 0（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("OBS_G6_B"))));
    }

    // =====================================================================
    qDebug() << "\n7) 写入位置：默认值落在被调段私有元素，调用者同名私有不受影响";
    {
        Fixture f;
        const QString file = QStringLiteral("bind7.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("#DIMS SS, 3"),
            QStringLiteral("SS:0 '= \"main-own\""),
            QStringLiteral("CALL G7_CALL"),
            QStringLiteral("OBS_G7_LEN = STRLENS(G7_FN())"),
            QStringLiteral("IF SS:0 == \"main-own\""),
            QStringLiteral("  OBS_G7_MAIN_KEPT = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G7_CALL(SS:0 = \"x\")"),
            QStringLiteral("#DIMS SS, 3"),
            QStringLiteral("OBS_G7_CALL_SEEN '= SS:0"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G7_FN(SS:0 = \"x\")"),
            QStringLiteral("#FUNCTIONS"),
            QStringLiteral("#DIMS SS, 3"),
            QStringLiteral("OBS_G7_EXPR_SEEN '= SS:0"),
            QStringLiteral("RETURNF SS:0")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "7) 装载");
        check(f.run() == ExecState::Halt, "7) 脚本结束");

        const QString callSeen = f.globalStr(QStringLiteral("OBS_G7_CALL_SEEN"));
        const QString exprSeen = f.globalStr(QStringLiteral("OBS_G7_EXPR_SEEN"));
        check(callSeen == QStringLiteral("x") && exprSeen == QStringLiteral("x"),
              QString("7) 两路都读到默认值 \"x\"（CALL=\"%1\" / 式子=\"%2\"）")
                  .arg(callSeen, exprSeen));
        check(callSeen == exprSeen, "7) 两路结果相同");
        check(f.globalInt(QStringLiteral("OBS_G7_MAIN_KEPT")) == 1,
              QString("7) 调用者私有 SS:0 未被默认值改写（得到 OBS_G7_MAIN_KEPT=%1）")
                  .arg(f.globalInt(QStringLiteral("OBS_G7_MAIN_KEPT"))));
        check(f.calleeStr(QStringLiteral("SS"), {0}, src, QStringLiteral("G7_CALL"), file)
                  == QStringLiteral("x")
                  && f.calleeStr(QStringLiteral("SS"), {0}, src, QStringLiteral("G7_FN"), file)
                         == QStringLiteral("x"),
              "7) 写侧：两个被调段的私有 SS:0 都是 \"x\"（显式段身份）");
        check(f.calleeStr(QStringLiteral("SS"), {0}, src, QStringLiteral("MAIN"), file)
                  == QStringLiteral("main-own"),
              "7) 写侧：MAIN 段私有 SS:0 仍是 \"main-own\"");
        check(f.storage.getStringSlotRaw(QStringLiteral("SS"), {0}).text.isEmpty(),
              "7) 没有创建同名裸全局槽 SS");
    }


    // =====================================================================
    qDebug() << "\n8) eraTW 真实形状：末尾 `ARGS:1 = \"\"` 的省略实参";
    {
        // eraTW/ERB/COMMON.ERB:574 `@RECOVER_PERMIL(ARG,回復割合,ARGS,回復速度の影響,ARGS:1="")`
        // 把 ARGS:1 原样转交 @RECOVER，后者用 `ARGS:1 == ""` 判断「要不要打提示」
        //（552 行：小额回复 + 无自定义文案 -> 直接 RETURN）。旧实现把省略的空串
        // 缺省值整数化成 "0"，这个判断恒为假 —— 每次小额回复都会弹出带「（0）」的提示。
        Fixture f;
        const QString file = QStringLiteral("bind8.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("CALL G8_OMIT, 0, 50, \"体力\", 1"),                 // 末尾省略
            QStringLiteral("CALL G8_GIVEN, 0, 50, \"体力\", 1, \"カスタム\""),  // 显式给
            QStringLiteral("RETURN"),
            QStringLiteral("@G8_OMIT(ARG, 回復割合, ARGS, 回復速度の影響, ARGS:1 = \"\")"),
            QStringLiteral("#DIM 回復割合"),
            QStringLiteral("#DIM 回復速度の影響"),
            QStringLiteral("OBS_G8_OMIT_MSG '= ARGS:1"),
            QStringLiteral("OBS_G8_TAKEN = 0"),
            QStringLiteral("IF ARGS:1 == \"\""),
            QStringLiteral("  OBS_G8_TAKEN = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("RETURN"),
            QStringLiteral("@G8_GIVEN(ARG, 回復割合, ARGS, 回復速度の影響, ARGS:1 = \"\")"),
            QStringLiteral("#DIM 回復割合"),
            QStringLiteral("#DIM 回復速度の影響"),
            QStringLiteral("OBS_G8_GIVEN_MSG '= ARGS:1"),
            QStringLiteral("RETURN")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "8) 装载");
        check(f.run() == ExecState::Halt, "8) 脚本结束");

        const QString omitMsg = f.globalStr(QStringLiteral("OBS_G8_OMIT_MSG"));
        check(omitMsg.isEmpty(),
              QString("8) 省略末尾实参 -> ARGS:1 是空串（得到 \"%1\"，整数化会得到 \"0\"）")
                  .arg(omitMsg));
        check(f.globalInt(QStringLiteral("OBS_G8_TAKEN")) == 1,
              QString("8) `ARGS:1 == \"\"` 分支被走到（得到 OBS_G8_TAKEN=%1）")
                  .arg(f.globalInt(QStringLiteral("OBS_G8_TAKEN"))));
        check(f.globalStr(QStringLiteral("OBS_G8_GIVEN_MSG")) == QStringLiteral("カスタム"),
              QString("8) 显式给的第 5 个实参覆盖空串缺省值（得到 \"%1\"）")
                  .arg(f.globalStr(QStringLiteral("OBS_G8_GIVEN_MSG"))));
    }

    // =====================================================================
    qDebug() << "\n================================";
    qDebug().noquote() << "断言数:" << g_assertions;
    if (g_failures == 0) qDebug() << "ALL PASS";
    else qDebug() << g_failures << "FAILURE(S)";
    return g_failures == 0 ? 0 : 1;
}
