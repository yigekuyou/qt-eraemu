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
// test_variable_table.cpp
//
// 验证变量表（#DIM/#DIMS/#GLOBAL + 函数作用域）与强类型回填：
//   1. 声明解析：类型 / 作用域 / 维度 / 非 ASCII 变量名
//   2. 函数作用域：同名变量在不同函数互不冲突
//   3. 重复声明 -> 结构化告警
//   4. 类型回填：表达式中的变量获得声明类型，父节点类型随之更新（按需计算）
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include "ast/ast_builder.h"
#include "ast/variable_table.h"
#include "process_state.h"
#include "era_parse_table.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Variable table test";
    qDebug() << "===================";

    ProcessState state;
    EraParseTable table(&state);

    const QStringList src = {
        "@MAIN",          // 0
        "#DIM A, 3",      // 1  MAIN 局部整型数组 [3]
        "#DIMS S",        // 2  MAIN 局部字符串
        "#GLOBAL G, 2",   // 3  全局整型数组 [2]
        "#DIM 開始行",     // 4  MAIN 局部（非 ASCII 名）
        "#DIM A",         // 5  与上面 A 重名 -> 告警
        "B = A + 1",      // 6
        "C = S + 1",      // 7  S 是字符串，+1 类型不匹配
        "PRINTS S",       // 8  持有已回填的字符串表达式
        "@OTHER",         // 9
        "#DIM A"          // 9  另一个函数的 A（局部，允许）
    };

    const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
    QList<LogicalLine> script;
    for (int i = 0; i < src.size(); ++i) {
        script.append(AstBuilder::build(src.at(i), ScriptPosition("t.ERB", i, 1), resolve));
    }
    check(table.loadScript("vt", script), "loadScript(vt)");
    table.markLoadComplete();   // 装载完成后统一回填（引擎在 loadScripts 之后调用）

    const VariableTable& vt = table.variableTable();

    qDebug() << "\n1) 声明解析";
    check(vt.count() == 5, QString("变量数 == 5（得到 %1）").arg(vt.count()));
    const VariableDecl* a = vt.find("A", "MAIN");
    check(a != nullptr && a->type == OperandType::Int, "A(MAIN) -> Int");
    check(a != nullptr && a->dimension == 1 && a->lengths.value(0) == 3, "A(MAIN) 维度 [3]");
    const VariableDecl* s = vt.find("S", "MAIN");
    check(s != nullptr && s->type == OperandType::Str, "S(MAIN) -> Str");
    const VariableDecl* g = vt.find("G");
    check(g != nullptr && g->scope == VarScope::Global, "G -> Global");
    check(g != nullptr && g->lengths.value(0) == 2, "G 维度 [2]");
    const VariableDecl* cjk = vt.find("開始行", "MAIN");
    check(cjk != nullptr && cjk->type == OperandType::Int, "開始行(MAIN) -> Int（非 ASCII 名）");

    qDebug() << "\n2) 函数作用域";
    const VariableDecl* aOther = vt.find("A", "OTHER");
    check(aOther != nullptr && aOther->function == "OTHER", "A(OTHER) 是 OTHER 的局部");
    check(vt.find("A", "MAIN") != vt.find("A", "OTHER"), "同名变量在不用函数中区分");

    qDebug() << "\n3) 重复声明：局部静默 / 全局告警";
    {
        // 局部重名（同名 @label 重复定义时常见）：静默 first-wins（对齐真实游戏兼容性）
        bool localDupWarn = false;
        for (const QString& w : table.parseWarnings()) {
            if (w.contains("重复定义") && w.contains("变量 A")) localDupWarn = true;
        }
        check(!localDupWarn, "局部重复声明不告警（first-wins）");

        // 全局重复：C# 经 IdentifierDictionary.CheckUserVarName（跨文件）→ 告警
        const QStringList gsrc = {"@MAIN", "#DIM GLOBAL GD", "#DIM GLOBAL GD"};
        EraParseTable compiler(nullptr);
        const AstResolver gr = [&compiler](const QString& e) { return compiler.expressionAst(e); };
        QList<LogicalLine> gs;
        for (int i = 0; i < gsrc.size(); ++i) {
            gs.append(AstBuilder::build(gsrc.at(i), ScriptPosition("g.ERB", i, 1), gr));
        }
        table.loadScript("gdup", gs);
        bool globalDupWarn = false;
        for (const QString& w : table.parseWarnings()) {
            if (w.contains("全局变量 GD 重复定义")) globalDupWarn = true;
        }
        check(globalDupWarn, "#DIM GLOBAL 重复声明 -> 告警（跨文件检查）");
    }

    qDebug() << "\n3.5) 维数常数求值（#DIM CONST）";
    {
        // 注意：GRID 的维数引用的常数在其“之后”才声明，验证顺序无关（装载结束时重算）
        const QStringList dimSrc = {
            "@MAIN",
            "#DIM GRID, W, H",      // 1 引用尚未声明的常数
            "#DIM CONST W = 12",    // 2
            "#DIM CONST H = 24",    // 3
            "#DIM NUMS, 3, 4"       // 4 纯字面量
        };
        EraParseTable compiler(nullptr);
        const AstResolver r2 = [&compiler](const QString& e) { return compiler.expressionAst(e); };
        QList<LogicalLine> s2;
        for (int i = 0; i < dimSrc.size(); ++i) {
            s2.append(AstBuilder::build(dimSrc.at(i), ScriptPosition("d.ERB", i, 1), r2));
        }
        table.loadScript("dims", s2);
        table.markLoadComplete();

        qint64 w = 0;
        // 这些 CONST 声明在 @MAIN **函数内** = 该函数的私有常量
        //（C#：函数内的 #DIM 走 LogicalLine.AddPrivateVariable），必须带作用域查。
        check(table.variableTable().constInt("W", w, "MAIN") && w == 12,
              "#DIM CONST W = 12 -> 常数表（MAIN 作用域）");
        check(!table.variableTable().constInt("W", w),
              "私有常量 W 在无作用域上下文不可见");
        check(!table.variableTable().constInt("W", w, "OTHERFN"),
              "私有常量 W 在别的函数内不可见");
        const VariableDecl* grid = table.variableTable().find("GRID", "MAIN");
        check(grid != nullptr && grid->lengths == QList<int>({12, 24}),
              QString("GRID 维数 == [12,24]（得到 %1 个：%2）")
                  .arg(grid ? grid->lengths.size() : -1)
                  .arg(grid && !grid->lengths.isEmpty() ? QString::number(grid->lengths.value(0)) : "?"));
        const VariableDecl* nums = table.variableTable().find("NUMS", "MAIN");
        check(nums != nullptr && nums->lengths == QList<int>({3, 4}), "NUMS 维数 == [3,4]（字面量）");

        // 未显式给尺寸时，长度 = 初值个数（C# sizeNum.Add(terms.Count)）。
        // `#DIM CONST SNOW = a,b,c,d` 是长度 4 的常量数组：曾按长度 1 登记，
        // 使 SNOW:1..3 被误判「常量下标越界」（eraTW DRAW_COLOREDMAP.ERB）。
        const QStringList constArrSrc = {
            "@MAIN",
            "#DIM CONST SNOW = 0x111111,0x222222,0x333333,0x444444",
            "#DIM CONST SCALAR = 7"
        };
        EraParseTable compiler3(nullptr);
        const AstResolver r3 = [&compiler3](const QString& e) { return compiler3.expressionAst(e); };
        QList<LogicalLine> s3;
        for (int i = 0; i < constArrSrc.size(); ++i) {
            s3.append(AstBuilder::build(constArrSrc.at(i), ScriptPosition("ca.ERB", i, 1), r3));
        }
        table.loadScript("carr", s3);
        table.markLoadComplete();
        const VariableDecl* snow = table.variableTable().find("SNOW", "MAIN");
        check(snow != nullptr && snow->lengths == QList<int>({4}),
              QString("SNOW 长度 == [4]（得到 %1）")
                  .arg(snow && !snow->lengths.isEmpty() ? QString::number(snow->lengths.value(0)) : "?"));
        qint64 snow1 = 0;
        check(table.variableTable().constArrayAt("SNOW", 1, snow1, "MAIN") && snow1 == 0x222222,
              "SNOW:1 == 0x222222");
        // 单元素常量仍然参与「常量折叠」；多元素常量数组不折叠
        qint64 scalar = 0;
        check(table.variableTable().constInt("SCALAR", scalar, "MAIN") && scalar == 7,
              "#DIM CONST SCALAR = 7 -> 标量常数");
        qint64 snowScalar = 0;
        check(!table.variableTable().constInt("SNOW", snowScalar, "MAIN"),
              "常量数组 SNOW 不当作标量常数折叠");
    }

    qDebug() << "\n3.6) 常量作用域分离：广域 / 私有常量 / 同名私有变量遮蔽";
    {
        // 常量按**归属作用域**存储：函数内的 `#DIM CONST` 只在该函数内可见，
        // 本函数的同名可写私有变量（`#DIM X`）遮蔽别处的同名常量。
        // 此前只有一张扁平的 name→value 表，运行期求值按名字命中 —— 一个函数里的
        // `#DIM CONST SHADOW = 51` 会盖住别处同名的私有变量（eraTW 的
        // `SETCOLOR 色_RED7` / `CLEARLINE 削除行数 + 1` 都读成了常量的值）。
        const QStringList constSrc = {
            "#DIM CONST GSHADOW = 99",   // 函数外（文件级）-> 广域常量
        };
        EraParseTable gc(nullptr);
        const AstResolver gr = [&gc](const QString& e) { return gc.expressionAst(e); };
        QList<LogicalLine> gs;
        for (int i = 0; i < constSrc.size(); ++i)
            gs.append(AstBuilder::build(constSrc.at(i), ScriptPosition("g.ERB", i, 1), gr));
        // .ERH 文件级声明 = 广域常量（.ERB 里函数外的 # 行会被忽略）
        table.loadScript("gconst", gs, /*isHeaderFile=*/true);

        const QStringList scopeSrc = {
            "@HOLDER",
            "#DIM CONST SHADOW = 51",   // HOLDER 的私有常量
            "RETURN",
            "@USER",
            "#DIM SHADOW",              // USER 的私有**可写变量**（与上面同名）
            "SHADOW = 7",
            "RETURN",
            "@READER",                  // 谁都没声明 SHADOW
            "RETURN"
        };
        EraParseTable sc(nullptr);
        const AstResolver sr = [&sc](const QString& e) { return sc.expressionAst(e); };
        QList<LogicalLine> ss;
        for (int i = 0; i < scopeSrc.size(); ++i)
            ss.append(AstBuilder::build(scopeSrc.at(i), ScriptPosition("s.ERB", i, 1), sr));
        table.loadScript("scope", ss);
        table.markLoadComplete();

        const VariableTable& vt = table.variableTable();
        qint64 v = 0;
        check(vt.constInt("SHADOW", v, "HOLDER") && v == 51,
              "私有常量在声明它的函数内可见");
        check(!vt.constInt("SHADOW", v, "READER"),
              "私有常量在别的函数内不可见");
        check(!vt.constInt("SHADOW", v),
              "私有常量在无作用域上下文不可见");
        check(!vt.constInt("SHADOW", v, "USER"),
              "同名私有可写变量遮蔽别处的私有常量");
        check(vt.constInt("GSHADOW", v, "HOLDER") && v == 99,
              "广域常量在任意函数内可见");
        check(vt.constInt("GSHADOW", v) && v == 99,
              "广域常量在无作用域上下文可见");
    }

    qDebug() << "\n4) 类型回填（强类型）";
    // A 是 Int -> A + 1 合法
    auto add = table.lineAt("vt", 6)->arguments[1].ast;
    check(add && add->valueType() == OperandType::Int, "A + 1 -> Int");
    check(add && static_cast<BinaryOpNode*>(add.get())->typesValid(), "A + 1 类型合法");
    // S 是 Str -> S + 1 类型不匹配
    auto bad = table.lineAt("vt", 7)->arguments[1].ast;
    check(bad && !static_cast<BinaryOpNode*>(bad.get())->typesValid(), "S + 1 类型不匹配（S 为字符串）");
    // 变量节点本身也被定型
    auto sOnly = table.lineAt("vt", 8)->arguments[0].ast;
    check(sOnly && sOnly->valueType() == OperandType::Str, "S -> Str（变量表回填）");
    check(vt.typeOf("G") == OperandType::Int, "typeOf(G) == Int");

    qDebug() << "\n===================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] variable-table tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
