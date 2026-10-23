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
// test_storage_form.cpp —— 容器的「单一形态」护栏
//
// 契约（与实现无关，只走公开 API）：
//   1. 每个存储键 === 一条**扁平槽位序列**；同一变量 (i,j,k) 的槽位 =
//      Σ idx[d]·strides[d]（行主序），最后一个元素的最后一维也能往返；
//   2. 越界（负数 / 超过该维长度）读 0、写不入形状之外（多维声明）；
//   3. 1D 的懒扩容只改 allocatedSlots（容器事实），shapeOf（形状）不变；
//   4. CHARADATA：每角色一份元素序列（角色 stride = 元素总数），角色之间互不干扰；
//   5. CSV 模板快照与运行时角色数据互不影响，形状口径与声明一致；
//   6. 系统变量（FLAG/DA/TA/CFLAG）的维长与读写一致；
//   7. VARSIZE 语句（RESULT）/ VARSIZE 函数 / shapeOf+arraySize 三者读数一致；
//   8. 槽位连续性：按 strides 反算的扁平序号读到同一格。
//
// 本文件先在旧实现（按维度分家的平行容器）上全绿，阶段 2 收敛成单一形态后
// 仍是同一组断言（改坏取址公式/角色 stride 必然红）。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>
#include <QSharedPointer>

#include "ast/ast_builder.h"
#include "ast/expression_evaluator.h"
#include "ast/name_interner.h"
#include "ast/variable_table.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "process_state.h"
#include "script_runner.h"
#include "variable_storage.h"

static int g_failures = 0;
static int g_assertions = 0;

static void check(bool cond, const QString& what) {
    ++g_assertions;
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static qint64 product(const QList<int>& lengths) {
    qint64 n = 1;
    for (int v : lengths) n *= v;
    return n;
}

// 行主序 strides（最后一维连续）：槽位连续性的期望值由它算，不抄魔法数。
static QList<int> stridesOf(const QList<int>& lengths) {
    QList<int> strides(lengths.size(), 1);
    int acc = 1;
    for (int i = lengths.size() - 1; i >= 0; --i) { strides[i] = acc; acc *= lengths.at(i); }
    return strides;
}

static qint64 flatOf(const QList<int>& indices, const QList<int>& strides) {
    qint64 off = 0;
    for (int i = 0; i < indices.size() && i < strides.size(); ++i) off += qint64(indices.at(i)) * strides.at(i);
    return off;
}

static FlatStringValue flatStr(const QString& text) { return FlatStringValue(text); }

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    ProcessState state;
    EraParseTable pt(&state);
    VariableStorage vs;
    ExpressionEvaluator ev;
    ExecutionEngine ex(&vs, nullptr);
    ScriptRunner runner(&pt, &ex, &state, &vs);
    pt.setVariableStorage(&vs);
    pt.setExpressionEvaluator(&ev);
    ex.setParseTable(&pt);
    ex.setExpressionEvaluator(&ev);
    runner.setExpressionEvaluator(&ev);
    runner.setStepLimit(1000000);

    const int g1 = 5;
    const int g2a = 2, g2b = 3;
    const int g3a = 2, g3b = 2, g3c = 3;
    const int p1 = 3;
    const int p2a = 2, p2b = 2;
    const int p3a = 2, p3b = 2, p3c = 2;
    const int t1 = 4;
    const int t2a = 2, t2b = 3;
    const int t3a = 2, t3b = 2, t3c = 2;
    const int c1 = 4;
    const int c2a = 2, c2b = 3;
    const int s1 = 3;
    const int s2a = 2, s2b = 2;

    QStringList src = {
        QStringLiteral("@MAIN"),
        QStringLiteral("#DIM GLOBAL G1, %1").arg(g1),
        QStringLiteral("#DIM GLOBAL G2, %1, %2").arg(g2a).arg(g2b),
        QStringLiteral("#DIM GLOBAL G3, %1, %2, %3").arg(g3a).arg(g3b).arg(g3c),
        QStringLiteral("#DIMS GLOBAL T1, %1").arg(t1),
        QStringLiteral("#DIMS GLOBAL T2, %1, %2").arg(t2a).arg(t2b),
        QStringLiteral("#DIMS GLOBAL T3, %1, %2, %3").arg(t3a).arg(t3b).arg(t3c),
        QStringLiteral("#DIM P1, %1").arg(p1),
        QStringLiteral("#DIM P2, %1, %2").arg(p2a).arg(p2b),
        QStringLiteral("#DIM P3, %1, %2, %3").arg(p3a).arg(p3b).arg(p3c),
        QStringLiteral("#DIMS Q2, %1, %2").arg(p2a).arg(p2b),
        QStringLiteral("#DIM CHARADATA C1, %1").arg(c1),
        QStringLiteral("#DIM CHARADATA C2, %1, %2").arg(c2a).arg(c2b),
        QStringLiteral("#DIM CHARADATA C0"),
        QStringLiteral("#DIMS CHARADATA S1, %1").arg(s1),
        QStringLiteral("#DIMS CHARADATA S2, %1, %2").arg(s2a).arg(s2b),
        QStringLiteral("#DIM GLOBAL OBS, 40"),
        // VARSIZE 语句：把形状逐维写进 RESULT（Array identity，不是元素值）。
        QStringLiteral("VARSIZE G2"),
        QStringLiteral("OBS:0 = RESULT:0"),
        QStringLiteral("OBS:1 = RESULT:1"),
        // VARSIZE 函数：与语句同一份形状。
        QStringLiteral("OBS:2 = VARSIZE(\"G2\")"),
        QStringLiteral("OBS:3 = VARSIZE(\"G2\", 1)"),
        QStringLiteral("OBS:4 = VARSIZE(\"G3\", 1)"),
        QStringLiteral("OBS:5 = VARSIZE(\"G1\")"),
        QStringLiteral("OBS:6 = VARSIZE(\"C2\")"),
        QStringLiteral("OBS:7 = VARSIZE(\"C2\", 1)"),
        QStringLiteral("OBS:8 = VARSIZE(\"T2\", 1)"),
        QStringLiteral("RETURN"),
    };

    const AstResolver resolve = [&pt](const QString& e) { return pt.expressionAst(e); };
    QList<LogicalLine> lines;
    for (int i = 0; i < src.size(); ++i)
        lines.append(AstBuilder::build(src.at(i), ScriptPosition("storageform.ERB", i, 1), resolve));
    check(pt.loadScript("storageform", lines), QStringLiteral("loadScript(容器形态夹具)"));
    vs.setSaveVariableTable(&pt.variableTable());
    pt.setEntryPoint(QStringLiteral("MAIN"));
    pt.markLoadComplete();
    const int warnings = pt.parseWarnings().size();

    const QString privateScope = pt.lineAt("storageform", 0)->scopeKey;
    const eravar::ScopeId sid = eravar::internName(privateScope);
    check(!privateScope.isEmpty(), QStringLiteral("私有段身份非空（%1）").arg(privateScope));
    check(runner.runToCompletion() == ExecState::Halt, QStringLiteral("夹具运行到 HALT"));

    // =====================================================================
    qDebug() << "\n1) 广域 1D / 2D / 3D：末元素往返 + 越界两侧";
    {
        // 1D：最后一维的最后一个元素
        vs.setGlobalInt1D(QStringLiteral("G1"), g1 - 1, 11);
        check(vs.getGlobalInt1D(QStringLiteral("G1"), g1 - 1) == 11,
              QStringLiteral("G1:%1 往返（末元素）").arg(g1 - 1));
        vs.setGlobalInt1D(QStringLiteral("G1"), 0, 1);
        check(vs.getGlobalInt1D(QStringLiteral("G1"), 0) == 1, QStringLiteral("G1:0 往返"));
        check(vs.getGlobalInt1D(QStringLiteral("G1"), g1) == 0,
              QStringLiteral("G1:%1 未写入 -> 0（右越界只读）").arg(g1));
        vs.setGlobalInt1D(QStringLiteral("G1"), -1, 5);
        check(vs.getGlobalInt1D(QStringLiteral("G1"), -1) == 0,
              QStringLiteral("G1:-1 写忽略 / 读 0（左越界）"));

        // 2D
        vs.setGlobalInt2D(QStringLiteral("G2"), g2a - 1, g2b - 1, 2121);
        check(vs.getGlobalInt2D(QStringLiteral("G2"), g2a - 1, g2b - 1) == 2121,
              QStringLiteral("G2:[%1,%2] 往返（末元素）").arg(g2a - 1).arg(g2b - 1));
        vs.setGlobalInt2D(QStringLiteral("G2"), 0, 1, 21);
        check(vs.getGlobalInt2D(QStringLiteral("G2"), 0, 1) == 21, QStringLiteral("G2:[0,1] 往返"));
        check(vs.getGlobalInt2D(QStringLiteral("G2"), g2a, 0) == 0,
              QStringLiteral("G2:[%1,0] 读 0（右越界）").arg(g2a));
        check(vs.getGlobalInt2D(QStringLiteral("G2"), 0, g2b) == 0,
              QStringLiteral("G2:[0,%1] 读 0（最后一维右越界）").arg(g2b));
        vs.setGlobalInt2D(QStringLiteral("G2"), g2a, 0, 99);
        check(vs.getGlobalInt2D(QStringLiteral("G2"), g2a, 0) == 0,
              QStringLiteral("G2:[%1,0] 写忽略（形状之外不生长）").arg(g2a));
        vs.setGlobalInt2D(QStringLiteral("G2"), -1, 0, 99);
        check(vs.getGlobalInt2D(QStringLiteral("G2"), -1, 0) == 0,
              QStringLiteral("G2:[-1,0] 写忽略 / 读 0（左越界）"));

        // 3D
        vs.setGlobalInt3D(QStringLiteral("G3"), g3a - 1, g3b - 1, g3c - 1, 3232);
        check(vs.getGlobalInt3D(QStringLiteral("G3"), g3a - 1, g3b - 1, g3c - 1) == 3232,
              QStringLiteral("G3:[%1,%2,%3] 往返（末元素）")
                  .arg(g3a - 1).arg(g3b - 1).arg(g3c - 1));
        vs.setGlobalInt3D(QStringLiteral("G3"), 1, 0, 2, 302);
        check(vs.getGlobalInt3D(QStringLiteral("G3"), 1, 0, 2) == 302, QStringLiteral("G3:[1,0,2] 往返"));
        check(vs.getGlobalInt3D(QStringLiteral("G3"), g3a, 0, 0) == 0,
              QStringLiteral("G3:[%1,0,0] 读 0（第 0 维右越界）").arg(g3a));
        check(vs.getGlobalInt3D(QStringLiteral("G3"), 0, g3b, 0) == 0,
              QStringLiteral("G3:[0,%1,0] 读 0（第 1 维右越界）").arg(g3b));
        check(vs.getGlobalInt3D(QStringLiteral("G3"), 0, 0, g3c) == 0,
              QStringLiteral("G3:[0,0,%1] 读 0（最后一维右越界）").arg(g3c));
        vs.setGlobalInt3D(QStringLiteral("G3"), 0, 0, g3c, 99);
        check(vs.getGlobalInt3D(QStringLiteral("G3"), 0, 0, g3c) == 0,
              QStringLiteral("G3:[0,0,%1] 写忽略（形状之外不生长）").arg(g3c));
    }

    // =====================================================================
    qDebug() << "\n2) 广域字符串 1D / 2D / 3D";
    {
        vs.setGlobalStr1D(QStringLiteral("T1"), t1 - 1, QStringLiteral("t1-last"));
        check(vs.getGlobalStr1D(QStringLiteral("T1"), t1 - 1) == QStringLiteral("t1-last"),
              QStringLiteral("T1:%1 往返（末元素）").arg(t1 - 1));
        check(vs.getGlobalStr1D(QStringLiteral("T1"), t1).isEmpty(),
              QStringLiteral("T1:%1 未写入 -> 空串").arg(t1));

        vs.setGlobalStr2D(QStringLiteral("T2"), t2a - 1, t2b - 1, QStringLiteral("t2-last"));
        check(vs.getGlobalStr2D(QStringLiteral("T2"), t2a - 1, t2b - 1) == QStringLiteral("t2-last"),
              QStringLiteral("T2:[%1,%2] 往返（末元素）").arg(t2a - 1).arg(t2b - 1));
        check(vs.getStringSlot(QStringLiteral("T2"), {1, 1}).text.isEmpty(),
              QStringLiteral("T2:[1,1] 未写入 -> 空串"));
        vs.setGlobalStr2D(QStringLiteral("T2"), 1, 2, QStringLiteral("t2-12"));
        check(vs.getStringSlot(QStringLiteral("T2"), {1, 2}).text == QStringLiteral("t2-12"),
              QStringLiteral("T2 经 getStringSlot 读到同一格"));

        vs.setStringSlot(QStringLiteral("T3"), {t3a - 1, t3b - 1, t3c - 1}, flatStr(QStringLiteral("t3-last")));
        check(vs.getStringSlot(QStringLiteral("T3"), {t3a - 1, t3b - 1, t3c - 1}).text
                  == QStringLiteral("t3-last"),
              QStringLiteral("T3 末元素经 set/getStringSlot 往返"));
        check(vs.getGlobalStr3DRaw(QStringLiteral("T3"), 0, 1, 1).isEmpty(),
              QStringLiteral("T3:[0,1,1] 未被末元素写污染（隔离）"));
    }

    // =====================================================================
    qDebug() << "\n3) 私有 1D / 2D / 3D（显式段身份）";
    {
        vs.setPrivateScope(sid, {});
        vs.setGlobalInt1D(QStringLiteral("P1"), p1 - 1, 31);
        check(vs.getGlobalInt1D(QStringLiteral("P1"), p1 - 1) == 31,
              QStringLiteral("P1:%1 往返（私有末元素）").arg(p1 - 1));
        vs.setGlobalInt2D(QStringLiteral("P2"), p2a - 1, p2b - 1, 32);
        check(vs.getGlobalInt2D(QStringLiteral("P2"), p2a - 1, p2b - 1) == 32,
              QStringLiteral("P2:[%1,%2] 往返（私有末元素）").arg(p2a - 1).arg(p2b - 1));
        vs.setGlobalInt3D(QStringLiteral("P3"), p3a - 1, p3b - 1, p3c - 1, 33);
        check(vs.getGlobalInt3D(QStringLiteral("P3"), p3a - 1, p3b - 1, p3c - 1) == 33,
              QStringLiteral("P3 末元素往返（私有 3D）"));
        check(vs.getGlobalInt2D(QStringLiteral("P2"), p2a, 0) == 0,
              QStringLiteral("P2:[%1,0] 私有 2D 越界读 0").arg(p2a));
        vs.setGlobalStr2D(QStringLiteral("Q2"), p2a - 1, p2b - 1, QStringLiteral("q-last"));
        check(vs.getStringSlot(QStringLiteral("Q2"), {p2a - 1, p2b - 1}).text == QStringLiteral("q-last"),
              QStringLiteral("Q2 私有 2D 字符串末元素往返"));
        // 私有名的形状只在它自己的段里存在；广域上下文不冒充。
        check(vs.shapeOf(QStringLiteral("P2"), sid).lengths == QList<int>({p2a, p2b}),
              QStringLiteral("shapeOf(P2, 段) == 声明 [%1,%2]").arg(p2a).arg(p2b));
        check(vs.shapeOf(QStringLiteral("P2"), eravar::kGlobalScope).isEmpty(),
              QStringLiteral("shapeOf(P2, 广域) 为空（私有形状不跨段）"));
        vs.setPrivateScope(eravar::kGlobalScope, {});
    }

    // =====================================================================
    qDebug() << "\n4) 懒扩容：allocatedSlots 变化、shapeOf 不变";
    {
        const VariableStorage::ArrayShape before = vs.shapeOf(QStringLiteral("G1"), eravar::kGlobalScope);
        const int slotsBefore = vs.allocatedSlots(QStringLiteral("G1"));
        vs.setGlobalInt1D(QStringLiteral("G1"), g1 + 5, 555);
        const int slotsAfter = vs.allocatedSlots(QStringLiteral("G1"));
        const VariableStorage::ArrayShape after = vs.shapeOf(QStringLiteral("G1"), eravar::kGlobalScope);
        check(slotsAfter > slotsBefore,
              QStringLiteral("1D 越界写后 allocatedSlots 增长（%1 -> %2）").arg(slotsBefore).arg(slotsAfter));
        check(slotsAfter >= g1 + 6,
              QStringLiteral("allocatedSlots 覆盖写入下标（>= %1）").arg(g1 + 6));
        check(before.lengths == QList<int>({g1}) && after.lengths == QList<int>({g1}),
              QStringLiteral("扩容前后 shapeOf(G1) 恒为 [%1]").arg(g1));
        check(after.elementCount == product(QList<int>{g1}),
              QStringLiteral("形状 elementCount 不随容器增长（仍为 %1）").arg(g1));

        const VariableStorage::ArrayShape sBefore = vs.shapeOf(QStringLiteral("T1"), eravar::kGlobalScope);
        const int sSlotsBefore = vs.allocatedSlots(QStringLiteral("T1"));
        vs.setGlobalStr1D(QStringLiteral("T1"), t1 + 4, QStringLiteral("grow"));
        check(vs.allocatedSlots(QStringLiteral("T1")) > sSlotsBefore,
              QStringLiteral("字符串 1D 越界写后 allocatedSlots 增长（%1 -> %2）")
                  .arg(sSlotsBefore).arg(vs.allocatedSlots(QStringLiteral("T1"))));
        check(vs.shapeOf(QStringLiteral("T1"), eravar::kGlobalScope).lengths == sBefore.lengths,
              QStringLiteral("字符串扩容后 shapeOf(T1) 不变（仍为 [%1]）").arg(t1));

        // 多维：容器分配量与形状是两件事，但声明多维不允许「写时偷偷长」。
        check(vs.allocatedSlots(QStringLiteral("G2")) == g2a,
              QStringLiteral("声明 2D 的 allocatedSlots == 首维 %1").arg(g2a));
    }

    // =====================================================================
    qDebug() << "\n5) CHARADATA：角色隔离（不同角色 / 不同变量 / 不同维度）";
    {
        vs.addChara(100);
        vs.addChara(200);
        check(vs.charaNum() == 2, QStringLiteral("两个运行时角色（模板 100 / 200）"));

        vs.setCharaInt(QStringLiteral("C1"), 0, c1 - 1, 401);
        check(vs.getCharaInt(QStringLiteral("C1"), 0, c1 - 1) == 401,
              QStringLiteral("C1:角色0:%1 往返（末元素）").arg(c1 - 1));
        check(vs.getCharaInt(QStringLiteral("C1"), 1, c1 - 1) == 0,
              QStringLiteral("C1 角色 0 的写入不影响角色 1"));
        vs.setCharaInt(QStringLiteral("C1"), 1, c1 - 1, 402);
        check(vs.getCharaInt(QStringLiteral("C1"), 0, c1 - 1) == 401
                  && vs.getCharaInt(QStringLiteral("C1"), 1, c1 - 1) == 402,
              QStringLiteral("C1 两个角色各自保留自己的值"));
        check(vs.getCharaInt3D(QStringLiteral("C2"), 0, 0, 0) == 0,
              QStringLiteral("C1 的写入不影响 C2（不同变量）"));

        vs.setCharaInt3D(QStringLiteral("C2"), 0, c2a - 1, c2b - 1, 411);
        check(vs.getCharaInt3D(QStringLiteral("C2"), 0, c2a - 1, c2b - 1) == 411,
              QStringLiteral("C2:角色0:[%1,%2] 往返（末元素）").arg(c2a - 1).arg(c2b - 1));
        check(vs.getCharaInt3D(QStringLiteral("C2"), 1, c2a - 1, c2b - 1) == 0,
              QStringLiteral("C2 角色 1 未被角色 0 覆盖"));
        check(vs.getCharaInt3D(QStringLiteral("C2"), 0, c2a, 0) == 0,
              QStringLiteral("C2 越界元素读 0"));
        // 标量角色变量（元素维 0）：只有元素 0
        vs.setCharaInt(QStringLiteral("C0"), 1, 0, 431);
        check(vs.getCharaInt(QStringLiteral("C0"), 1, 0) == 431,
              QStringLiteral("C0 角色 1 标量往返（元素维 0）"));
        check(vs.getCharaInt(QStringLiteral("C0"), 0, 0) == 0,
              QStringLiteral("C0 角色 0 与角色 1 隔离"));

        vs.setCharaStr(QStringLiteral("S1"), 0, s1 - 1, QStringLiteral("s1-0"));
        vs.setCharaStr(QStringLiteral("S1"), 1, s1 - 1, QStringLiteral("s1-1"));
        check(vs.getCharaStr(QStringLiteral("S1"), 0, s1 - 1) == QStringLiteral("s1-0")
                  && vs.getCharaStr(QStringLiteral("S1"), 1, s1 - 1) == QStringLiteral("s1-1"),
              QStringLiteral("S1 两个角色的字符串互不覆盖"));
        vs.setStringSlot(QStringLiteral("S2"), {0, s2a - 1, s2b - 1}, flatStr(QStringLiteral("s2-0")));
        check(vs.getStringSlot(QStringLiteral("S2"), {0, s2a - 1, s2b - 1}).text == QStringLiteral("s2-0"),
              QStringLiteral("S2 角色 0 的 2D 字符串末元素往返"));
        check(vs.getStringSlot(QStringLiteral("S2"), {1, s2a - 1, s2b - 1}).text.isEmpty(),
              QStringLiteral("S2 角色 1 未被角色 0 覆盖"));
        // 角色号 + 元素维的实参规约：C2 是 2D，缺角色号时取 TARGET。
        check(vs.charaDataDimension(QStringLiteral("C2")) == 2,
              QStringLiteral("C2 的元素维数为 2"));
        check(vs.charaDataDimension(QStringLiteral("C1")) == 1,
              QStringLiteral("C1 的元素维数为 1"));
    }

    // =====================================================================
    qDebug() << "\n6) CSV 模板快照：模板与运行时互不影响";
    {
        vs.setCharaInt(QStringLiteral("C1"), 0, 1, 42);
        vs.setCharaStr(QStringLiteral("S1"), 0, 0, QStringLiteral("csv-name"));
        vs.snapshotCharaTemplates();
        vs.setCharaInt(QStringLiteral("C1"), 0, 1, 99);
        vs.setCharaStr(QStringLiteral("S1"), 0, 0, QStringLiteral("runtime-name"));
        check(vs.getCharaInt(QStringLiteral("C1"), 0, 1) == 99,
              QStringLiteral("运行时角色数据已被改写（99）"));
        check(vs.csvCharaInt(QStringLiteral("C1"), 100, 1) == 42,
              QStringLiteral("CSV 模板仍是快照值（42）"));
        check(vs.getCharaStr(QStringLiteral("S1"), 0, 0) == QStringLiteral("runtime-name"),
              QStringLiteral("运行时字符串已被改写"));
        check(vs.csvCharaStr(QStringLiteral("S1"), 100, 0) == QStringLiteral("csv-name"),
              QStringLiteral("CSV 模板字符串仍是快照值"));
        // CSV 变量与声明同维度
        check(vs.shapeOf(QStringLiteral("C1"), eravar::kGlobalScope).lengths == QList<int>({c1}),
              QStringLiteral("CSV 变量 C1 的维长 == 声明 [%1]").arg(c1));
        check(vs.shapeOf(QStringLiteral("C2"), eravar::kGlobalScope).lengths == QList<int>({c2a, c2b}),
              QStringLiteral("CSV 变量 C2 的维长 == 声明 [%1,%2]").arg(c2a).arg(c2b));
    }

    // =====================================================================
    qDebug() << "\n7) 系统变量：维长与读写一致";
    {
        const VariableConfig& cfg = vs.variableConfig();
        const int flagLen = cfg.getSize1D(QStringLiteral("FLAG"));
        check(flagLen > 0, QStringLiteral("VariableSize 给了 FLAG 长度（%1）").arg(flagLen));
        check(vs.shapeOf(QStringLiteral("FLAG"), eravar::kGlobalScope).lengths == QList<int>({flagLen}),
              QStringLiteral("shapeOf(FLAG) == [%1]").arg(flagLen));
        vs.setSystemVariable(QStringLiteral("FLAG"), flagLen - 1, 71);
        check(vs.getSystemVariable(QStringLiteral("FLAG"), flagLen - 1) == 71,
              QStringLiteral("FLAG:%1 往返（末元素）").arg(flagLen - 1));
        vs.setFlag(flagLen - 1, 72);
        check(vs.getFlag(flagLen - 1) == 72,
              QStringLiteral("setFlag/getFlag 与 setSystemVariable 同一槽"));
        check(vs.getSystemVariable(QStringLiteral("FLAG"), flagLen) == 0,
              QStringLiteral("FLAG:%1 越界读 0").arg(flagLen));

        const QPair<int, int> da = cfg.getSize2D(QStringLiteral("DA"));
        check(vs.shapeOf(QStringLiteral("DA"), eravar::kGlobalScope).lengths
                  == QList<int>({da.first, da.second}),
              QStringLiteral("shapeOf(DA) == [%1,%2]").arg(da.first).arg(da.second));
        const QVector<int> ta3 = cfg.getSize3D(QStringLiteral("TA"));
        check(vs.shapeOf(QStringLiteral("TA"), eravar::kGlobalScope).lengths
                  == QList<int>(ta3.begin(), ta3.end()),
              QStringLiteral("shapeOf(TA) == VariableSize 3D 形状"));

        const int cflagLen = cfg.getSize1D(QStringLiteral("CFLAG"));
        check(cflagLen > 0, QStringLiteral("VariableSize 给了 CFLAG 长度（%1）").arg(cflagLen));
        check(vs.shapeOf(QStringLiteral("CFLAG"), eravar::kGlobalScope).lengths
                  == QList<int>({cflagLen}),
              QStringLiteral("shapeOf(CFLAG) == [%1]").arg(cflagLen));
        vs.setCharaInt(QStringLiteral("CFLAG"), 1, cflagLen - 1, 81);
        check(vs.getCharaInt(QStringLiteral("CFLAG"), 1, cflagLen - 1) == 81,
              QStringLiteral("CFLAG 角色 1 末元素往返"));
        check(vs.getCharaInt(QStringLiteral("CFLAG"), 0, cflagLen - 1) == 0,
              QStringLiteral("CFLAG 角色 0 未被角色 1 覆盖"));
    }

    // =====================================================================
    qDebug() << "\n8) VARSIZE 语句 / VARSIZE 函数 / shapeOf+arraySize 三者一致";
    {
        check(vs.getGlobalInt1D(QStringLiteral("OBS"), 0) == g2a,
              QStringLiteral("VARSIZE 语句 RESULT:0 == %1（实得 %2）")
                  .arg(g2a).arg(vs.getGlobalInt1D(QStringLiteral("OBS"), 0)));
        check(vs.getGlobalInt1D(QStringLiteral("OBS"), 1) == g2b,
              QStringLiteral("VARSIZE 语句 RESULT:1 == %1（实得 %2）")
                  .arg(g2b).arg(vs.getGlobalInt1D(QStringLiteral("OBS"), 1)));
        check(vs.getGlobalInt1D(QStringLiteral("OBS"), 2) == g2a,
              QStringLiteral("VARSIZE(\"G2\") == %1").arg(g2a));
        check(vs.getGlobalInt1D(QStringLiteral("OBS"), 3) == g2b,
              QStringLiteral("VARSIZE(\"G2\",1) == %1").arg(g2b));
        check(vs.getGlobalInt1D(QStringLiteral("OBS"), 4) == g3b,
              QStringLiteral("VARSIZE(\"G3\",1) == %1").arg(g3b));
        check(vs.getGlobalInt1D(QStringLiteral("OBS"), 5) == g1,
              QStringLiteral("VARSIZE(\"G1\") == %1").arg(g1));
        check(vs.getGlobalInt1D(QStringLiteral("OBS"), 6) == c2a,
              QStringLiteral("VARSIZE(\"C2\") == %1（CHARADATA 元素维）").arg(c2a));
        check(vs.getGlobalInt1D(QStringLiteral("OBS"), 7) == c2b,
              QStringLiteral("VARSIZE(\"C2\",1) == %1").arg(c2b));
        check(vs.getGlobalInt1D(QStringLiteral("OBS"), 8) == t2b,
              QStringLiteral("VARSIZE(\"T2\",1) == %1").arg(t2b));
        check(vs.arraySize(QStringLiteral("G2")) == g2a,
              QStringLiteral("arraySize(G2) == %1").arg(g2a));
        check(vs.arrayLengths(QStringLiteral("G2")) == QList<int>({g2a, g2b}),
              QStringLiteral("arrayLengths(G2) == [%1,%2]").arg(g2a).arg(g2b));
        check(vs.arraySize(QStringLiteral("G3")) == g3a,
              QStringLiteral("arraySize(G3) == %1").arg(g3a));
        check(vs.arrayLengths(QStringLiteral("T3")) == QList<int>({t3a, t3b, t3c}),
              QStringLiteral("arrayLengths(T3) == [%1,%2,%3]").arg(t3a).arg(t3b).arg(t3c));
        check(warnings == 0, QStringLiteral("夹具装载告警为 0（实得 %1）").arg(warnings));
    }

    // =====================================================================
    qDebug() << "\n9) 槽位连续性：按 strides 反算的扁平序号读到同一格";
    {
        const QList<int> g3l{g3a, g3b, g3c};
        const QList<int> g3s = stridesOf(g3l);
        vs.setGlobalInt3D(QStringLiteral("G3"), 1, 0, 1, 901);
        const qint64 off = flatOf({1, 0, 1}, g3s);
        check(vs.getGlobalInt1D(QStringLiteral("G3"), int(off)) == 901,
              QStringLiteral("G3:[1,0,1] == 扁平槽 %1（stride %2）")
                  .arg(off).arg(g3s.at(0)));
        vs.setGlobalInt3D(QStringLiteral("G3"), 1, 1, 2, 902);
        check(vs.getGlobalInt1D(QStringLiteral("G3"), int(flatOf({1, 1, 2}, g3s))) == 902,
              QStringLiteral("G3:[1,1,2] == 扁平槽 %1").arg(flatOf({1, 1, 2}, g3s)));
        check(vs.getGlobalInt1D(QStringLiteral("G3"), int(flatOf({0, 1, 2}, g3s))) == 0,
              QStringLiteral("不同坐标不落到同一槽（[0,1,2] 仍为 0）"));
        // 2D：末元素 = (d0-1)*d1 + (d1-1)
        const QList<int> g2s = stridesOf({g2a, g2b});
        check(vs.getGlobalInt1D(QStringLiteral("G2"), int(flatOf({g2a - 1, g2b - 1}, g2s))) == 2121,
              QStringLiteral("G2 末元素 == 扁平槽 %1").arg(flatOf({g2a - 1, g2b - 1}, g2s)));
        // 私有 3D 同一公式
        vs.setPrivateScope(sid, {});
        const QList<int> p3s = stridesOf({p3a, p3b, p3c});
        vs.setGlobalInt3D(QStringLiteral("P3"), 1, 1, 0, 903);
        check(vs.getGlobalInt1D(QStringLiteral("P3"), int(flatOf({1, 1, 0}, p3s))) == 903,
              QStringLiteral("P3:[1,1,0] == 扁平槽 %1（私有）").arg(flatOf({1, 1, 0}, p3s)));
        vs.setPrivateScope(eravar::kGlobalScope, {});
        check(vs.isMultiDimArray(QStringLiteral("G3")),
              QStringLiteral("G3 登记为多维（几何来自声明）"));
        check(vs.multiDimLengths(QStringLiteral("G3")) == g3l,
              QStringLiteral("multiDimLengths(G3) == [%1,%2,%3]").arg(g3a).arg(g3b).arg(g3c));
    }

    // =====================================================================
    // 断言数自检：形态 × 维度的线性覆盖（≥40）。
    qDebug() << "\n================================";
    qDebug().noquote() << "断言数:" << g_assertions;
    if (g_failures == 0) qDebug() << "ALL PASS";
    else qDebug() << g_failures << "FAILURE(S)";
    return g_failures == 0 ? 0 : 1;
}
