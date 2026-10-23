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
// test_scope_plumbing.cpp —— 「谁的作用域」护栏（运行期端到端）
//
// 「名字 + 下标 -> 存储」的解析已经收敛成单份（resolveIntLocus / resolveStrLocus），
// 但解析需要**一个作用域**。存储层为兼容旧调用方保留了「隐式当前帧」的单参
// 重载（storageName(name) = storageName(name, m_privateScopeId)）。运行期有一批
// 调用点在**已经知道正确作用域**、或**参数已经是解析后的键**的情况下用了它：
//
//   · 隐式当前帧 ≠ 被调段的帧      -> 私有名在调用者名表里查不到 -> 落到裸名
//                                     （写的是全局槽 / 调用者的槽，不是被调段的）；
//   · 参数已经是解析后的键          -> 被当成名字再解析一次，REF 别名会让它改道。
//
// 这一组只走**可观察事实**（脚本自己读回 + 公开存储 API），与实现形态无关：
//
//   1. 元素形参 `@F(S:0, SS:0)`：被调段私有变量被写入，调用者同名私有变量不变，
//      且没有创建同名的大写裸全局槽；
//   2. 同上，整型路径（1D `V:0` + 2D `W:0:1`）；
//   3. 形参默认值 / 省略实参：落到被调段私有变量（语句 CALL 与式中调用两条绑定
//      路径），而不是裸全局；落在私有槽里的值在段内读到、在段外不可见；
//   4. REF 别名：别名的目标是**广域裸名**、而被调段恰好有同名私有变量时，
//      经别名读写的必须是那个广域变量（已解析的键不得被当前帧的私有名表改道）；
//   5. 三层嵌套（caller -> mid -> leaf）各有同名私有变量：每层读到自己那份。
//
// 观测点选择说明：1/2/5 在写侧（形参绑定）与读侧（表达式求值）都要正确，
// 因此用「段内读回」+「段外用显式 scope 读私有槽」两侧夹住；3 覆盖 bindArguments
// 与 invokeUserFunction 两条绑定路径；4 是唯一能把「已解析键被二次解析」变成
// 可观察差异的最小形态（目标裸名 + 被调段同名私有名）。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QLatin1Char>
#include <QStringList>

#include "ast/ast_builder.h"
#include "ast/expression_evaluator.h"
#include "ast/name_interner.h"
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

// 一个装载好的脚本 + 引擎全套（与 test_user_functions 的夹具同形）。
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

    bool load(const QString& file, const QStringList& src, const QString& entry) {
        const bool ok = table.loadScript(file, buildLines(table, src, file));
        if (!ok) return false;
        table.setEntryPoint(entry);
        table.markLoadComplete();
        return true;
    }

    bool loadHeader(const QString& file, const QStringList& src) {
        return table.loadScript(file, buildLines(table, src, file), /*isHeaderFile=*/true);
    }

    ExecState run() { return runner.runToCompletion(); }
    qint64 globalInt(const QString& name, int x = 0) const {
        return storage.getGlobalInt1D(name, x);
    }
    eravar::ScopeId scopeOf(const QStringList& src, const QString& label,
                            const QString& file) {
        return eravar::internName(segmentKeyOf(src, label, file));
    }
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Scope plumbing regression (implicit current frame)";
    qDebug() << "================================================";

    // =====================================================================
    qDebug() << "\n1) 元素形参（#DIMS SS）不得写到裸全局 / 调用者帧";
    {
        Fixture f;
        const QString file = QStringLiteral("scope1.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("#DIM S, 3"),
            QStringLiteral("#DIMS SS, 3"),
            QStringLiteral("S = 100"),
            QStringLiteral("SS '= \"caller\""),
            QStringLiteral("CALL STR_TARGET, 7, \"from-caller\""),
            QStringLiteral("IF S == 100"),
            QStringLiteral("  M_S_KEPT = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("IF SS == \"caller\""),
            QStringLiteral("  M_SS_KEPT = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("RETURN"),
            QStringLiteral("@STR_TARGET(S:0, SS:0)"),
            QStringLiteral("#DIM S, 3"),
            QStringLiteral("#DIMS SS, 3"),
            QStringLiteral("IF S == 7"),
            QStringLiteral("  T_S_SEEN = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("IF SS == \"from-caller\""),
            QStringLiteral("  T_SS_SEEN = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("S = 200"),
            QStringLiteral("SS '= \"callee\""),
            QStringLiteral("RETURN")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "1) 装载");
        check(f.run() == ExecState::Halt, "1) 脚本结束");
        const eravar::ScopeId mainScope = f.scopeOf(src, QStringLiteral("MAIN"), file);
        const eravar::ScopeId calleeScope = f.scopeOf(src, QStringLiteral("STR_TARGET"), file);
        check(mainScope != eravar::kGlobalScope && calleeScope != eravar::kGlobalScope
                  && mainScope != calleeScope,
              "1) MAIN / STR_TARGET 是两个不同的段身份");

        check(f.globalInt(QStringLiteral("T_S_SEEN")) == 1,
              QString("1) 被调段读到元素形参写入的整型 S == 7（得到 T_S_SEEN=%1）")
                  .arg(f.globalInt(QStringLiteral("T_S_SEEN"))));
        check(f.globalInt(QStringLiteral("T_SS_SEEN")) == 1,
              "1) 被调段读到元素形参写入的字符串 SS == \"from-caller\"");
        check(f.globalInt(QStringLiteral("M_S_KEPT")) == 1, "1) 调用者的同名私有 S 未被改写");
        check(f.globalInt(QStringLiteral("M_SS_KEPT")) == 1, "1) 调用者的同名私有 SS 未被改写");

        check(f.storage.readIntByName(QStringLiteral("S"), {0}, calleeScope) == 200,
              QString("1) 被调段私有 S:0 == 200（得到 %1）")
                  .arg(f.storage.readIntByName(QStringLiteral("S"), {0}, calleeScope)));
        check(f.storage.getStringSlot(QStringLiteral("SS"), {0}, calleeScope).text
                  == QStringLiteral("callee"),
              QString("1) 被调段私有 SS:0 == \"callee\"（得到 \"%1\"）")
                  .arg(f.storage.getStringSlot(QStringLiteral("SS"), {0}, calleeScope).text));
        check(f.storage.readIntByName(QStringLiteral("S"), {0}, mainScope) == 100,
              "1) 调用者私有 S:0 仍是 100");
        check(f.storage.getStringSlot(QStringLiteral("SS"), {0}, mainScope).text
                  == QStringLiteral("caller"),
              "1) 调用者私有 SS:0 仍是 \"caller\"");

        // 「写错位置」的直接证据：没有任何裸全局槽被创建。
        check(f.storage.getGlobalInt1DRaw(QStringLiteral("S"), 0) == 0,
              QString("1) 不存在裸全局槽 S（得到 %1）")
                  .arg(f.storage.getGlobalInt1DRaw(QStringLiteral("S"), 0)));
        check(f.storage.getStringSlotRaw(QStringLiteral("SS"), {0}).text.isEmpty(),
              QString("1) 不存在裸全局槽 SS（得到 \"%1\"）")
                  .arg(f.storage.getStringSlotRaw(QStringLiteral("SS"), {0}).text));
    }

    // =====================================================================
    qDebug() << "\n2) 整型元素形参（1D V:0 / 2D W:0:1）";
    {
        Fixture f;
        const QString file = QStringLiteral("scope2.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("#DIM V, 3"),
            QStringLiteral("#DIM W, 2, 3"),
            QStringLiteral("V:0 = 100"),
            QStringLiteral("W:0:1 = 101"),
            QStringLiteral("CALL INT_TARGET, 11, 22"),
            QStringLiteral("IF V:0 == 100"),
            QStringLiteral("  M_V_KEPT = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("IF W:0:1 == 101"),
            QStringLiteral("  M_W_KEPT = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("RETURN"),
            QStringLiteral("@INT_TARGET(V:0, W:0:1)"),
            QStringLiteral("#DIM V, 3"),
            QStringLiteral("#DIM W, 2, 3"),
            QStringLiteral("T_V_SEEN = V:0"),
            QStringLiteral("T_W_SEEN = W:0:1"),
            QStringLiteral("V:0 = 111"),
            QStringLiteral("W:0:1 = 222"),
            QStringLiteral("RETURN")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "2) 装载");
        check(f.run() == ExecState::Halt, "2) 脚本结束");
        const eravar::ScopeId mainScope = f.scopeOf(src, QStringLiteral("MAIN"), file);
        const eravar::ScopeId calleeScope = f.scopeOf(src, QStringLiteral("INT_TARGET"), file);

        check(f.globalInt(QStringLiteral("T_V_SEEN")) == 11,
              QString("2) 被调段 V:0 == 11（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("T_V_SEEN"))));
        check(f.globalInt(QStringLiteral("T_W_SEEN")) == 22,
              QString("2) 被调段 W:0:1 == 22（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("T_W_SEEN"))));
        check(f.globalInt(QStringLiteral("M_V_KEPT")) == 1
                  && f.globalInt(QStringLiteral("M_W_KEPT")) == 1,
              "2) 调用者的同名私有 V:0 / W:0:1 未被改写");
        check(f.storage.readIntByName(QStringLiteral("V"), {0}, calleeScope) == 111,
              "2) 被调段私有 V:0 == 111");
        check(f.storage.readIntByName(QStringLiteral("W"), {0, 1}, calleeScope) == 222,
              QString("2) 被调段私有 W:0:1 == 222（得到 %1）")
                  .arg(f.storage.readIntByName(QStringLiteral("W"), {0, 1}, calleeScope)));
        check(f.storage.readIntByName(QStringLiteral("V"), {0}, mainScope) == 100
                  && f.storage.readIntByName(QStringLiteral("W"), {0, 1}, mainScope) == 101,
              "2) 调用者的私有 V:0 / W:0:1 保持原值");
        check(f.storage.getGlobalInt1DRaw(QStringLiteral("V"), 0) == 0
                  && f.storage.getGlobalInt2DRaw(QStringLiteral("W"), 0, 1) == 0,
              "2) 不存在裸全局槽 V / W");
    }

    // =====================================================================
    qDebug() << "\n3) 形参默认值 / 省略实参：落到被调段私有变量";
    {
        Fixture f;
        const QString file = QStringLiteral("scope3.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("CALL DEF_TARGET"),
            QStringLiteral("CALL DEF_TARGET2, 9"),
            QStringLiteral("R_FN = FN_TARGET()"),
            QStringLiteral("RETURN"),
            QStringLiteral("@DEF_TARGET(V:1 = 40, U = 41)"),
            QStringLiteral("#DIM V, 3"),
            QStringLiteral("#DIM U"),
            QStringLiteral("D_V_SEEN = V:1"),
            QStringLiteral("D_U_SEEN = U"),
            QStringLiteral("V:1 = 99"),
            QStringLiteral("U = 98"),
            QStringLiteral("RETURN"),
            QStringLiteral("@DEF_TARGET2(A, V:1 = 40)"),
            QStringLiteral("#DIM A"),
            QStringLiteral("#DIM V, 3"),
            QStringLiteral("D2_A_SEEN = A"),
            QStringLiteral("D2_V_SEEN = V:1"),
            QStringLiteral("RETURN"),
            QStringLiteral("@FN_TARGET(V:1 = 55)"),
            QStringLiteral("#FUNCTION"),
            QStringLiteral("#DIM V, 3"),
            QStringLiteral("FN_CALLED = 1"),
            QStringLiteral("D3_V_SEEN = V:1"),
            QStringLiteral("RETURN 1")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "3) 装载");
        check(f.run() == ExecState::Halt, "3) 脚本结束");
        const eravar::ScopeId defScope = f.scopeOf(src, QStringLiteral("DEF_TARGET"), file);
        const eravar::ScopeId def2Scope = f.scopeOf(src, QStringLiteral("DEF_TARGET2"), file);
        const eravar::ScopeId fnScope = f.scopeOf(src, QStringLiteral("FN_TARGET"), file);

        check(f.globalInt(QStringLiteral("D_V_SEEN")) == 40,
              QString("3) 省略实参 + 元素形参默认值 V:1 = 40（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("D_V_SEEN"))));
        check(f.globalInt(QStringLiteral("D_U_SEEN")) == 41,
              QString("3) 省略实参 + 标量形参默认值 U = 41（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("D_U_SEEN"))));
        check(f.storage.readIntByName(QStringLiteral("V"), {1}, defScope) == 99,
              QString("3) 默认值落在 DEF_TARGET 的私有 V:1（段内写 99 后读到 %1）")
                  .arg(f.storage.readIntByName(QStringLiteral("V"), {1}, defScope)));
        // 标量形参是**值绑定**（m_parameters），本来就没有自己的槽；这里只钉住
        // 「它没有被错写成同名私有槽」这一条可观察事实。
        check(f.storage.readIntByName(QStringLiteral("U"), {0}, defScope) == 0,
              QString("3) 标量形参 U 没有被错写成私有槽（得到 %1）")
                  .arg(f.storage.readIntByName(QStringLiteral("U"), {0}, defScope)));
        check(f.globalInt(QStringLiteral("FN_CALLED")) == 1,
              "3) 式中调用 FN_TARGET() 确实执行到了函数体");
        check(f.globalInt(QStringLiteral("D2_A_SEEN")) == 9
                  && f.globalInt(QStringLiteral("D2_V_SEEN")) == 40,
              "3) 部分实参 + 其余默认值（A=9, V:1=40）各自到位");
        check(f.storage.readIntByName(QStringLiteral("V"), {1}, def2Scope) == 40,
              "3) DEF_TARGET2 的私有 V:1 == 40");
        check(f.globalInt(QStringLiteral("D3_V_SEEN")) == 55,
              QString("3) 式中调用的默认值 V:1 = 55（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("D3_V_SEEN"))));
        check(f.storage.readIntByName(QStringLiteral("V"), {1}, fnScope) == 55,
              "3) FN_TARGET 的私有 V:1 == 55");
        check(f.storage.getGlobalInt1DRaw(QStringLiteral("U"), 0) == 0
                  && f.storage.getGlobalInt1DRaw(QStringLiteral("V"), 1) == 0,
              "3) 默认值没有创建同名裸全局槽 U / V");
    }

    // =====================================================================
    qDebug() << "\n4) REF 别名：目标裸名 + 被调段同名私有变量，不得改道";
    {
        Fixture f;
        const QString headerFile = QStringLiteral("scope4.ERH");
        const QStringList header = {
            QStringLiteral("#DIM RG, 3"),
            QStringLiteral("#DIMS RGS, 3")
        };
        check(f.loadHeader(headerFile, header), "4) 头文件（广域 RG / RGS）");
        const QString file = QStringLiteral("scope4.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("RG:0 = 5"),
            QStringLiteral("RGS:0 '= \"global\""),
            QStringLiteral("CALL REF_TARGET, RG, RGS"),
            QStringLiteral("IF RG:0 == 77"),
            QStringLiteral("  M_REF_G = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("IF RGS:0 == \"through-ref\""),
            QStringLiteral("  M_REF_GS = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("RETURN"),
            QStringLiteral("@REF_TARGET(P, PS)"),
            QStringLiteral("#DIM REF P"),
            QStringLiteral("#DIMS REF PS"),
            QStringLiteral("#DIM RG, 3"),
            QStringLiteral("#DIMS RGS, 3"),
            QStringLiteral("P:0 = 77"),
            QStringLiteral("PS:0 '= \"through-ref\""),
            QStringLiteral("IF P:0 == 77"),
            QStringLiteral("  T_REF_P = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("IF PS:0 == \"through-ref\""),
            QStringLiteral("  T_REF_PS = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("RG:0 = 500"),
            QStringLiteral("RGS:0 '= \"private\""),
            QStringLiteral("RETURN")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "4) 装载");
        check(f.run() == ExecState::Halt, "4) 脚本结束");
        const eravar::ScopeId calleeScope = f.scopeOf(src, QStringLiteral("REF_TARGET"), file);

        check(f.globalInt(QStringLiteral("M_REF_G")) == 1,
              QString("4) 经 REF 写入的是广域 RG:0 = 77（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("RG"), 0)));
        check(f.globalInt(QStringLiteral("M_REF_GS")) == 1,
              QString("4) 经 REF 写入的是广域 RGS:0 == \"through-ref\"（得到 \"%1\"）")
                  .arg(f.storage.getGlobalStr1DRaw(QStringLiteral("RGS"), 0)));
        check(f.globalInt(QStringLiteral("T_REF_P")) == 1,
              QString("4) 段内经别名读回 P:0 == 77（得到 %1）")
                  .arg(f.storage.readIntByName(QStringLiteral("P"), {0}, calleeScope)));
        check(f.globalInt(QStringLiteral("T_REF_PS")) == 1,
              "4) 段内经别名读回 PS:0 == \"through-ref\"");
        check(f.storage.readIntByName(QStringLiteral("RG"), {0}, calleeScope) == 500,
              QString("4) 被调段同名**私有** RG:0 只受段内语句影响（== 500，得到 %1）")
                  .arg(f.storage.readIntByName(QStringLiteral("RG"), {0}, calleeScope)));
        check(f.storage.getStringSlot(QStringLiteral("RGS"), {0}, calleeScope).text
                  == QStringLiteral("private"),
              "4) 被调段同名**私有** RGS:0 == \"private\"");
        check(f.globalInt(QStringLiteral("RG"), 0) == 77,
              QString("4) 广域 RG:0 仍是 77（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("RG"), 0)));
        check(f.storage.getGlobalStr1DRaw(QStringLiteral("RGS"), 0)
                  == QStringLiteral("through-ref"),
              "4) 广域 RGS:0 仍是 \"through-ref\"");
    }

    // =====================================================================
    qDebug() << "\n5) 三层嵌套：每层读到自己那份同名私有变量";
    {
        Fixture f;
        const QString file = QStringLiteral("scope5.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("#DIM N, 3"),
            QStringLiteral("#DIMS NS, 3"),
            QStringLiteral("N:0 = 1"),
            QStringLiteral("NS:0 '= \"one\""),
            QStringLiteral("CALL MID, 2, \"two\""),
            QStringLiteral("M_AFTER_N = N:0"),
            QStringLiteral("IF NS:0 == \"one\""),
            QStringLiteral("  M_AFTER_NS = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("RETURN"),
            QStringLiteral("@MID(N:0, NS:0)"),
            QStringLiteral("#DIM N, 3"),
            QStringLiteral("#DIMS NS, 3"),
            QStringLiteral("D_MID_N = N:0"),
            QStringLiteral("IF NS:0 == \"two\""),
            QStringLiteral("  D_MID_NS = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("CALL LEAF, 3, \"three\""),
            QStringLiteral("L_MID_N = N:0"),
            QStringLiteral("IF NS:0 == \"two\""),
            QStringLiteral("  L_MID_NS = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("RETURN"),
            QStringLiteral("@LEAF(N:0, NS:0)"),
            QStringLiteral("#DIM N, 3"),
            QStringLiteral("#DIMS NS, 3"),
            QStringLiteral("D_LEAF_N = N:0"),
            QStringLiteral("IF NS:0 == \"three\""),
            QStringLiteral("  D_LEAF_NS = 1"),
            QStringLiteral("ENDIF"),
            QStringLiteral("N:0 = 30"),
            QStringLiteral("NS:0 '= \"leaf\""),
            QStringLiteral("RETURN")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "5) 装载");
        check(f.run() == ExecState::Halt, "5) 脚本结束");
        const eravar::ScopeId leafScope = f.scopeOf(src, QStringLiteral("LEAF"), file);

        check(f.globalInt(QStringLiteral("D_MID_N")) == 2
                  && f.globalInt(QStringLiteral("D_MID_NS")) == 1,
              "5) mid 读到自己那份（N=2, NS=\"two\"）");
        check(f.globalInt(QStringLiteral("D_LEAF_N")) == 3
                  && f.globalInt(QStringLiteral("D_LEAF_NS")) == 1,
              "5) leaf 读到自己那份（N=3, NS=\"three\"）");
        check(f.globalInt(QStringLiteral("L_MID_N")) == 2
                  && f.globalInt(QStringLiteral("L_MID_NS")) == 1,
              QString("5) leaf 返回后 mid 仍读到 2 / \"two\"（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("L_MID_N"))));
        check(f.globalInt(QStringLiteral("M_AFTER_N")) == 1
                  && f.globalInt(QStringLiteral("M_AFTER_NS")) == 1,
              "5) 回到 MAIN 后仍是 1 / \"one\"");
        check(f.storage.readIntByName(QStringLiteral("N"), {0}, leafScope) == 30,
              "5) leaf 的私有 N:0 被自己写成 30（不是 3，也不是 1）");
        check(f.storage.getGlobalInt1DRaw(QStringLiteral("N"), 0) == 0
                  && f.storage.getStringSlotRaw(QStringLiteral("NS"), {0}).text.isEmpty(),
              "5) 三层调用没有创建裸全局槽 N / NS");
    }

    // =====================================================================
    qDebug() << "\n6) REF 写路径：别名写入落到 REF 目标（与读路径对照）";
    {
        Fixture f;
        const QString headerFile = QStringLiteral("scope6.ERH");
        const QStringList header = {QStringLiteral("#DIM GQ, 3")};
        check(f.loadHeader(headerFile, header), "6) 头文件（广域 GQ）");
        const QString file = QStringLiteral("scope6.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("GQ:0 = 1"),
            QStringLiteral("CALL W_TARGET, GQ"),
            QStringLiteral("W_AFTER = GQ:0"),
            QStringLiteral("RETURN"),
            QStringLiteral("@W_TARGET(Q)"),
            QStringLiteral("#DIM REF Q"),
            QStringLiteral("Q:0 = 42"),
            QStringLiteral("RETURN")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "6) 装载");
        check(f.run() == ExecState::Halt, "6) 脚本结束");
        // 写侧证据：形参名 Q 不是槽名 —— 它只是指向广域 GQ 的别名。
        check(f.globalInt(QStringLiteral("GQ"), 0) == 42,
              QString("6) 段内 `Q:0 = 42` 落到 REF 目标广域 GQ:0（得到 %1）")
                  .arg(f.globalInt(QStringLiteral("GQ"), 0)));
        check(f.globalInt(QStringLiteral("W_AFTER")) == 42,
              "6) 回到调用者后读到 REF 写入的值（读写同一目标）");
        check(f.storage.getGlobalInt1DRaw(QStringLiteral("Q"), 0) == 0,
              QString("6) 写路径没有创建形参名的裸全局槽 Q（得到 %1）")
                  .arg(f.storage.getGlobalInt1DRaw(QStringLiteral("Q"), 0)));
    }

    // =====================================================================
    qDebug() << "\n7) 角色字符串经求值器路径（直接 / 经 REF 别名）读得到";
    {
        Fixture f;
        f.storage.addChara(0);
        f.storage.setCharaStr(QStringLiteral("CSTR"), 0, 0, QStringLiteral("chara-text"));
        check(f.storage.getCharaStr(QStringLiteral("CSTR"), 0, 0) == QStringLiteral("chara-text"),
              "7) 前置：CSTR:0:0 已写入（直接读回）");
        const QString headerFile = QStringLiteral("scope7.ERH");
        const QStringList header = {
            QStringLiteral("#DIMS DIRECT, 2"),
            QStringLiteral("#DIMS VIA_REF, 2")
        };
        check(f.loadHeader(headerFile, header), "7) 头文件（广域 DIRECT / VIA_REF）");
        const QString file = QStringLiteral("scope7.ERB");
        const QStringList src = {
            QStringLiteral("@MAIN"),
            QStringLiteral("DIRECT '= CSTR:0:0"),
            QStringLiteral("CALL R_TARGET, CSTR"),
            QStringLiteral("RETURN"),
            QStringLiteral("@R_TARGET(PS)"),
            QStringLiteral("#DIMS REF PS"),
            QStringLiteral("VIA_REF '= PS:0"),
            QStringLiteral("RETURN")
        };
        check(f.load(file, src, QStringLiteral("MAIN")), "7) 装载");
        check(f.run() == ExecState::Halt, "7) 脚本结束");
        // 角色字符串的「角色号 + 元素」定位不在扁平字符串容器里 —— 任何把它
        // 当普通扁平槽（GlobalStr）的读法在这里都会得到空串。
        check(f.storage.getGlobalStr1D(QStringLiteral("DIRECT"), 0) == QStringLiteral("chara-text"),
              QString("7) 直接经求值器读 CSTR:0:0（得到 \"%1\"）")
                  .arg(f.storage.getGlobalStr1D(QStringLiteral("DIRECT"), 0)));
        check(f.storage.getGlobalStr1D(QStringLiteral("VIA_REF"), 0) == QStringLiteral("chara-text"),
              QString("7) 经 REF 别名读同一角色字符串（得到 \"%1\"）")
                  .arg(f.storage.getGlobalStr1D(QStringLiteral("VIA_REF"), 0)));
    }

    // =====================================================================
    qDebug() << "\n================================";
    qDebug().noquote() << "断言数:" << g_assertions;
    if (g_failures == 0) qDebug() << "ALL PASS";
    else qDebug() << g_failures << "FAILURE(S)";
    return g_failures == 0 ? 0 : 1;
}