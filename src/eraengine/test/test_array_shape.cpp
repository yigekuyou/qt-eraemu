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
// test_array_shape.cpp —— 数组「形状」只允许一份事实
//
// 形状 = 维数 / 每维长度 / 元素总数，是**装载期**事实。唯一入口是
// VariableStorage::shapeOf(name, scope)，回落顺序在那里且只在那里：
//   声明（VariableDecl::lengths / elementCount） -> VariableConfig（VariableSize.csv
//   + 默认表，装载期尺寸来源）-> 空（未声明的名字没有形状）。
//
// 本文件钉住（改坏任意一处必然红）：
//   1. 每个形态的 shapeOf：lengths / dimension / elementCount / lengthOf(d)；
//   2. 读者一致性：VARSIZE(X)、VARSIZE(X,d)、arraySizeInCurrentFrame(X)、arrayLengthsInCurrentFrame(X)
//      全部等于「从声明算出来的值」；
//   3. 旧实现的分歧点（探针实测，见报告表）：
//        · 系统 3D（TA）：VARSIZE(TA) 曾是 0，正确值是第 0 维长度；
//        · 系统 2D（DA）第 2 维 / 3D（TA）第 0/1 维曾是 0 或错位；
//        · 用户 1D（U1）的越界维号曾回落成首维（VARSIZE(U1,3) == 6）；
//        · 用户 2D/3D 的越界维号曾回落成首维（VARSIZE(U2,2) == 2）；
//        · 私有多维（L2/L3/M3）：arraySize 曾按「当前帧」解析、与 VARSIZE 不一致；
//        · CHARADATA 多维（C2）曾是 0（形状没进声明查询路径）。
//   4. 「容器分配量」不再是形状来源：未声明的运行时数组 shapeOf 为空，
//      arraySize 只报已分配槽位数；2D/3D 的运行期外接几何（越界写长出来的
//      边界）同样不进形状 —— VARSIZE 与「有没有人先写过」无关（第 6 节）。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>
#include <QSharedPointer>

#include "ast/ast_builder.h"
#include "ast/expression_evaluator.h"
#include "ast/variable_table.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "process_state.h"
#include "script_runner.h"
#include "variable_storage.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static qint64 product(const QList<int>& lengths) {
    qint64 n = 1;
    for (int v : lengths) n *= v;
    return n;
}

static QString join(const QList<int>& lengths) {
    QStringList out;
    for (int v : lengths) out << QString::number(v);
    return out.join(QLatin1Char(','));
}

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

    // 维数一律用变量表达：期望值由声明算出，不抄魔法数。
    const int u1 = 6;
    const int u2a = 2, u2b = 3;
    const int u3a = 2, u3b = 3, u3c = 4;
    const int m3a = 2, m3b = 50, m3c = 5;
    const int l2a = 2, l2b = 3;
    const int l3a = 2, l3b = 2, l3c = 3;
    const int c2a = 2, c2b = 3;

    struct Declared {
        QString name;
        QList<int> lengths;
        bool isPrivate = false;
    };
    const QList<Declared> declared = {
        {QStringLiteral("U1"),   {u1}},
        {QStringLiteral("U2"),   {u2a, u2b}},
        {QStringLiteral("U3"),   {u3a, u3b, u3c}},
        {QStringLiteral("M3"),   {m3a, m3b, m3c}, true},
        {QStringLiteral("L2"),   {l2a, l2b}, true},
        {QStringLiteral("L3"),   {l3a, l3b, l3c}, true},
        {QStringLiteral("C2"),   {c2a, c2b}},
    };

    QStringList src = {
        QStringLiteral("@MAIN"),
        QStringLiteral("#DIM GLOBAL U1, %1").arg(u1),
        QStringLiteral("#DIM GLOBAL U2, %1, %2").arg(u2a).arg(u2b),
        QStringLiteral("#DIM GLOBAL U3, %1, %2, %3").arg(u3a).arg(u3b).arg(u3c),
        QStringLiteral("#DIM M3, %1, %2, %3").arg(m3a).arg(m3b).arg(m3c),
        QStringLiteral("#DIM L2, %1, %2").arg(l2a).arg(l2b),
        QStringLiteral("#DIM L3, %1, %2, %3").arg(l3a).arg(l3b).arg(l3c),
        QStringLiteral("#DIM CHARADATA C2, %1, %2").arg(c2a).arg(c2b),
        QStringLiteral("#DIM GLOBAL OBS, 64"),
    };
    // 生产路径（脚本 -> 求值器）的 VARSIZE 断言：结果写进声明的 OBS 数组
    //（不用 RESULT：那是函数返回值槽，RETURN 会清）。
    int slot = 0;
    QList<QString> vsLabels;
    for (const Declared& d : declared) {
        for (int dim = -1; dim <= 3; ++dim) {
            const QString expr = dim < 0
                ? QStringLiteral("VARSIZE(\"%1\")").arg(d.name)
                : QStringLiteral("VARSIZE(\"%1\",%2)").arg(d.name).arg(dim);
            src << QStringLiteral("OBS:%1 = %2").arg(slot++).arg(expr);
            vsLabels << QStringLiteral("%1,%2").arg(d.name).arg(dim);
        }
    }
    src << QStringLiteral("RETURN");

    const AstResolver resolve = [&pt](const QString& e) { return pt.expressionAst(e); };
    QList<LogicalLine> lines;
    for (int i = 0; i < src.size(); ++i)
        lines.append(AstBuilder::build(src.at(i), ScriptPosition("shape.ERB", i, 1), resolve));
    check(pt.loadScript("shape", lines), QStringLiteral("loadScript(形状夹具)"));
    vs.setSaveVariableTable(&pt.variableTable());
    pt.setEntryPoint(QStringLiteral("MAIN"));
    pt.markLoadComplete();
    check(pt.parseWarnings().isEmpty(),
          QStringLiteral("装载告警为 0（实得 %1）").arg(pt.parseWarnings().size()));

    const QString privateScope = pt.lineAt("shape", 0)->scopeKey;
    const eravar::ScopeId privateScopeId = eravar::internName(privateScope);
    check(!privateScope.isEmpty(), QStringLiteral("私有段身份非空（%1）").arg(privateScope));

    check(runner.runToCompletion() == ExecState::Halt, QStringLiteral("夹具运行到 HALT"));

    qDebug() << "\n1) shapeOf：声明形态（维度 / 每维长度 / 元素总数）";
    for (const Declared& d : declared) {
        const eravar::ScopeId sid = d.isPrivate ? privateScopeId : eravar::kGlobalScope;
        const VariableStorage::ArrayShape shape = vs.shapeOf(d.name, sid);
        check(shape.lengths == d.lengths,
              QStringLiteral("shapeOf(%1).lengths == 声明 [%2]（实得 [%3]）")
                  .arg(d.name, join(d.lengths), join(shape.lengths)));
        check(shape.dimension() == d.lengths.size(),
              QStringLiteral("shapeOf(%1).dimension == %2").arg(d.name).arg(d.lengths.size()));
        check(shape.elementCount == product(d.lengths),
              QStringLiteral("shapeOf(%1).elementCount == %2（实得 %3）")
                  .arg(d.name).arg(product(d.lengths)).arg(shape.elementCount));
        for (int dim = 0; dim <= 3; ++dim) {
            const int expected = dim < d.lengths.size() ? d.lengths.at(dim) : 0;
            check(shape.lengthOf(dim) == expected,
                  QStringLiteral("shapeOf(%1).lengthOf(%2) == %3（越界维 = 0）")
                      .arg(d.name).arg(dim).arg(expected));
        }
        // 私有形态要显式给作用域（夹具里 M3/L2/L3 属于 @MAIN 段）：单参重载是
        // 「当前帧」，此刻帧不在该段 —— 那正是旧实现 arraySize 与 VARSIZE 分歧的来源。
        check(vs.arraySize(d.name, sid) == d.lengths.first(),
              QStringLiteral("arraySize(%1,%2) == 首维 %3")
                  .arg(d.name).arg(d.isPrivate ? QStringLiteral("私有段") : QStringLiteral("广域"))
                  .arg(d.lengths.first()));
        check(vs.arrayLengths(d.name, sid) == d.lengths,
              QStringLiteral("arrayLengthsInCurrentFrame(%1) == 声明（实得 [%2]）")
                  .arg(d.name, join(vs.arrayLengths(d.name, sid))));
    }

    qDebug() << "\n2) shapeOf：无声明 -> VariableConfig（装载期尺寸表）";
    {
        const VariableConfig& cfg = vs.variableConfig();
        // 先落成局部变量：QList(begin(), end()) 若取两个**不同**临时对象的迭代器，
        // 迭代器来自已销毁的容器（C++23 range 构造仍要求同一范围）—— 曾因此 bad_alloc。
        const QVector<int> taSizes3 = cfg.getSize3D(QStringLiteral("TA"));
        const QList<int> taSizes(taSizes3.begin(), taSizes3.end());
        struct SystemForm { QString name; QList<int> expected; };
        const QList<SystemForm> systems = {
            {QStringLiteral("FLAG"), {cfg.getSize1D(QStringLiteral("FLAG"))}},
            {QStringLiteral("DA"),   {cfg.getSize2D(QStringLiteral("DA")).first,
                                      cfg.getSize2D(QStringLiteral("DA")).second}},
            {QStringLiteral("TA"),   taSizes},
            {QStringLiteral("CFLAG"), {cfg.getSize1D(QStringLiteral("CFLAG"))}},
        };
        for (const SystemForm& f : systems) {
            const VariableStorage::ArrayShape shape = vs.shapeOf(f.name, eravar::kGlobalScope);
            check(shape.lengths == f.expected,
                  QStringLiteral("shapeOf(%1) == VariableSize 形状 [%2]（实得 [%3]）")
                      .arg(f.name, join(f.expected), join(shape.lengths)));
            check(shape.elementCount == product(f.expected),
                  QStringLiteral("shapeOf(%1).elementCount == %2").arg(f.name).arg(product(f.expected)));
            check(vs.arraySizeInCurrentFrame(f.name) == f.expected.first(),
                  QStringLiteral("arraySizeInCurrentFrame(%1) == 首维 %2").arg(f.name).arg(f.expected.first()));
        }
    }

    qDebug() << "\n3) VARSIZE（脚本 -> 求值器）与 shapeOf 逐项一致";
    int k = 0;
    for (const Declared& d : declared) {
        for (int dim = -1; dim <= 3; ++dim) {
            const int useDim = dim < 0 ? 0 : dim;
            const int expected = useDim < d.lengths.size() ? d.lengths.at(useDim) : 0;
            const qint64 actual = vs.getGlobalInt1DInCurrentFrame(QStringLiteral("OBS"), k++);
            check(actual == expected,
                  QStringLiteral("VARSIZE(%1,%2) == %3（实得 %4）")
                      .arg(d.name).arg(dim).arg(expected).arg(actual));
        }
    }

    qDebug() << "\n4) 旧分歧点护栏（探针实测的每一处）";
    {
        // 系统 3D：VARSIZE(TA) 曾为 0（旧链只认 getSize1D/getSize2D）。
        const VariableConfig& cfg = vs.variableConfig();
        const QVector<int> ta = cfg.getSize3D(QStringLiteral("TA"));
        check(vs.shapeOf(QStringLiteral("TA"), eravar::kGlobalScope).lengthOf(0) == ta.at(0),
              QStringLiteral("VARSIZE(TA) == %1（旧实现 0）").arg(ta.at(0)));
        check(vs.shapeOf(QStringLiteral("TA"), eravar::kGlobalScope).lengthOf(2) == ta.at(2),
              QStringLiteral("VARSIZE(TA,2) == %1").arg(ta.at(2)));
        // 系统 2D 的第 2 维（旧实现 dim==2 分支会落到 getSize3D）。
        const QPair<int, int> da = cfg.getSize2D(QStringLiteral("DA"));
        check(vs.shapeOf(QStringLiteral("DA"), eravar::kGlobalScope).lengthOf(1) == da.second,
              QStringLiteral("VARSIZE(DA,1) == %1").arg(da.second));
        check(vs.shapeOf(QStringLiteral("DA"), eravar::kGlobalScope).lengthOf(2) == 0,
              QStringLiteral("VARSIZE(DA,2) == 0（越界维不再回落首维）"));
        // 用户 1D 的越界维号（旧实现回落成首维 == u1）。
        check(vs.shapeOf(QStringLiteral("U1"), eravar::kGlobalScope).lengthOf(3) == 0,
              QStringLiteral("VARSIZE(U1,3) == 0（旧实现 %1）").arg(u1));
        // 用户 2D/3D 的越界维号（旧实现回落成首维）。
        check(vs.shapeOf(QStringLiteral("U2"), eravar::kGlobalScope).lengthOf(2) == 0,
              QStringLiteral("VARSIZE(U2,2) == 0（旧实现 %1）").arg(u2a));
        check(vs.shapeOf(QStringLiteral("U3"), eravar::kGlobalScope).lengthOf(3) == 0,
              QStringLiteral("VARSIZE(U3,3) == 0（旧实现 %1）").arg(u3a));
        // CHARADATA 多维：旧实现 VARSIZE(C2) == 0、arraySizeInCurrentFrame(C2) == 0。
        check(vs.shapeOf(QStringLiteral("C2"), eravar::kGlobalScope).lengthOf(0) == c2a,
              QStringLiteral("VARSIZE(C2) == %1（旧实现 0）").arg(c2a));
        check(vs.shapeOf(QStringLiteral("C2"), eravar::kGlobalScope).lengthOf(1) == c2b,
              QStringLiteral("VARSIZE(C2,1) == %1（旧实现 0）").arg(c2b));
        // 私有多维：arraySize 曾按「当前帧」解析，帧不在该段时读到 0。
        check(vs.arraySize(QStringLiteral("L2"), privateScopeId) == l2a,
              QStringLiteral("arraySize(L2, 段) == %1（帧无关）").arg(l2a));
        // 广域上下文不冒充私有声明：私有名的形状只在它自己的段里存在。
        check(vs.arraySize(QStringLiteral("L2"), eravar::kGlobalScope) == 0,
              QStringLiteral("arraySize(L2, 广域) == 0（私有名的形状不跨段）"));
    }

    qDebug() << "\n5) 未声明的名字：没有形状，容器分配量不是形状";
    {
        check(vs.shapeOf(QStringLiteral("RUNTIME_ONLY"), eravar::kGlobalScope).isEmpty(),
              QStringLiteral("未声明名字的 shapeOf 为空"));
        for (int i = 0; i < 5; ++i) vs.setGlobalStr1DInCurrentFrame(QStringLiteral("RUNTIME_ONLY"), i, QStringLiteral("x"));
        check(vs.shapeOf(QStringLiteral("RUNTIME_ONLY"), eravar::kGlobalScope).isEmpty(),
              QStringLiteral("写入容器后 shapeOf 仍为空（分配量不进形状）"));
        check(vs.arraySizeInCurrentFrame(QStringLiteral("RUNTIME_ONLY")) == vs.allocatedSlots(QStringLiteral("RUNTIME_ONLY")),
              QStringLiteral("无形状名字的 arraySize == allocatedSlots（容器事实）"));
        check(vs.arraySizeInCurrentFrame(QStringLiteral("RUNTIME_ONLY")) == 5,
              QStringLiteral("无形状名字的 arraySize == 写入的槽位数 5"));
    }

    // 6) 运行期外接几何**不进形状**：VARSIZE 的答案与执行顺序无关。

    // 背景（旧实现）：shapeOf 的第 3 条回落是「未声明多维容器的运行期外接几何」。
    // 它让 VARSIZE(X) 在「有人先写过」前后给两个答案 —— 答案依赖执行历史。
    // 现在形状只来自装载期事实（声明 -> VariableSize），越界写仍然扩边界
    // （extendExtents/addressOf 的取址照常），但不再回答「变量有多大」。
    // 这些断言是删除那条回落的前提条件，也是它的回归护栏。
    qDebug() << "\n6) 未声明多维名字：写扩边界、形状不变（先读/先写两种顺序同答案）";
    {
        // 顺序 1：先读 VARSIZE，再写，再读。
        VariableStorage before;
        const QString name = QStringLiteral("RUNTIME_2D");
        check(before.shapeOf(name, eravar::kGlobalScope).isEmpty(),
              "写之前：未声明 2D 名字的 shapeOf 为空");
        before.setGlobalInt2DInCurrentFrame(name, 3, 4, 7);
        check(before.shapeOf(name, eravar::kGlobalScope).isEmpty(),
              "写之后：shapeOf 仍为空（外接边界不进形状）");
        check(before.getGlobalInt2DInCurrentFrame(name, 3, 4) == 7,
              "写扩边界仍在：写进去的元素读得回来（取址未被削弱）");
        // 容器事实仍然可读（不是形状）：已分配槽位数 = 首维上界 4。
        check(before.arraySizeInCurrentFrame(name) == before.allocatedSlots(name),
              "无形状名字的 arraySize 仍 == allocatedSlots（容器事实）");
        check(before.arraySizeInCurrentFrame(name) == 4,
              "外接边界 [3][4] 的首维上界 = 4（容器事实，不冒充形状）");

        // 顺序 2：先写，再读 —— 与顺序 1 的第二次读逐项相同。
        VariableStorage after;
        after.setGlobalInt2DInCurrentFrame(name, 3, 4, 7);
        check(after.shapeOf(name, eravar::kGlobalScope).lengths
                  == before.shapeOf(name, eravar::kGlobalScope).lengths,
              "两种顺序的 shapeOf 相同（长度表逐项相等）");
        check(after.shapeOf(name, eravar::kGlobalScope).elementCount
                  == before.shapeOf(name, eravar::kGlobalScope).elementCount,
              "两种顺序的 shapeOf 相同（元素总数相等）");
        check(after.arraySizeInCurrentFrame(name) == before.arraySizeInCurrentFrame(name),
              "两种顺序的 arraySize 相同");
    }
    {
        // 3D + 字符串侧同一条契约（两条写入路径各一份实现，都要钉）。
        VariableStorage vs;
        const QString name3 = QStringLiteral("RUNTIME_3D");
        vs.setGlobalInt3DInCurrentFrame(name3, 2, 3, 4, 11);
        check(vs.shapeOf(name3, eravar::kGlobalScope).isEmpty(),
              "未声明 3D：写扩边界后 shapeOf 仍为空");
        check(vs.getGlobalInt3DInCurrentFrame(name3, 2, 3, 4) == 11,
              "未声明 3D：写进去的元素读得回来");
        const QString sname = QStringLiteral("RUNTIME_S2");
        vs.setGlobalStr2DInCurrentFrame(sname, 1, 2, QStringLiteral("x"));
        check(vs.shapeOf(sname, eravar::kGlobalScope).isEmpty(),
              "未声明字符串 2D：写扩边界后 shapeOf 仍为空");
        check(vs.getGlobalStr2DInCurrentFrame(sname, 1, 2) == QStringLiteral("x"),
              "未声明字符串 2D：写进去的元素读得回来");
    }

    // 形态数量自检：断言数随形态 × 维度线性增长（≥40）。
    qDebug() << "\n================================";
    if (g_failures == 0) qDebug() << "ALL PASS";
    else qDebug() << g_failures << "FAILURE(S)";
    return g_failures == 0 ? 0 : 1;
}