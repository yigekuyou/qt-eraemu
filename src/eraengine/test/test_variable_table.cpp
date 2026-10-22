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
//
// 2026-10-22 存档 golden 重算：私有作用域身份由函数名改为段身份 (脚本, 段起点行)，
// SMD 的写盘名随之变化；存档容器版本 1808 -> 1809。旧 md5 生成于「条目顺序确定性」
// 之前（QHash 遍历序，本测试未固定种子），不可复现 —— 分类见下方 md5 断言处。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>
#include <QCryptographicHash>

#include "ast/ast_builder.h"
#include "ast/variable_table.h"
#include "ast/name_interner.h"
#include "process_state.h"
#include "era_parse_table.h"
#include "variable_storage.h"
#include "expression_evaluator.h"
#include "execution_engine.h"
#include "script_runner.h"
#include "era_binary_data.h"

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

    // 增量装载协议：定型之后再加脚本必须先显式重新打开 Loading。
    const auto resumeIncremental = [&table]() {
        if (table.loadPhase() == EraParseTable::LoadPhase::Finalized)
            check(table.beginIncrementalLoad(), "beginIncrementalLoad（增量装载）");
    };

    const VariableTable& vt = table.variableTable();
    // 私有作用域 = **段身份**（脚本名, 段起点行）。测试按装载后的行取身份文本，
    // 不再用函数名当键 —— 这正是本刀要固定的语义。
    const QString kMain = table.lineAt("vt", 0)->scopeKey;
    const QString kOther = table.lineAt("vt", 9)->scopeKey;

    qDebug() << "\n1) 声明解析";
    check(vt.count() == 5, QString("变量数 == 5（得到 %1）").arg(vt.count()));
    const VariableDecl* a = vt.find("A", kMain);
    check(a != nullptr && a->type == OperandType::Int, "A(MAIN) -> Int");
    check(a != nullptr && a->dimension == 1 && a->lengths.value(0) == 3, "A(MAIN) 维度 [3]");
    const VariableDecl* s = vt.find("S", kMain);
    check(s != nullptr && s->type == OperandType::Str, "S(MAIN) -> Str");
    const VariableDecl* g = vt.find("G");
    check(g != nullptr && g->scope == VarScope::Global, "G -> Global");
    check(g != nullptr && g->lengths.value(0) == 2, "G 维度 [2]");
    const VariableDecl* cjk = vt.find("開始行", kMain);
    check(cjk != nullptr && cjk->type == OperandType::Int, "開始行(MAIN) -> Int（非 ASCII 名）");

    qDebug() << "\n2) 函数作用域";
    const VariableDecl* aOther = vt.find("A", kOther);
    check(aOther != nullptr && aOther->function == "OTHER", "A(OTHER) 是 OTHER 的局部");
    check(vt.find("A", kMain) != vt.find("A", kOther), "同名变量在不用函数中区分");

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
        resumeIncremental();
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
        resumeIncremental();
        table.loadScript("dims", s2);
        table.markLoadComplete();

        const QString kDimsMain = table.lineAt("dims", 0)->scopeKey;
        qint64 w = 0;
        // 这些 CONST 声明在 @MAIN **函数内** = 该函数的私有常量
        //（C#：函数内的 #DIM 走 LogicalLine.AddPrivateVariable），必须带作用域查。
        check(table.variableTable().constInt("W", w, kDimsMain) && w == 12,
              "#DIM CONST W = 12 -> 常数表（MAIN 作用域）");
        check(!table.variableTable().constInt("W", w),
              "私有常量 W 在无作用域上下文不可见");
        check(!table.variableTable().constInt("W", w, kOther),
              "私有常量 W 在别的函数内不可见");
        const VariableDecl* grid = table.variableTable().find("GRID", kDimsMain);
        check(grid != nullptr && grid->lengths == QList<int>({12, 24}),
              QString("GRID 维数 == [12,24]（得到 %1 个：%2）")
                  .arg(grid ? grid->lengths.size() : -1)
                  .arg(grid && !grid->lengths.isEmpty() ? QString::number(grid->lengths.value(0)) : "?"));
        const VariableDecl* nums = table.variableTable().find("NUMS", kDimsMain);
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
        resumeIncremental();
        table.loadScript("carr", s3);
        table.markLoadComplete();
        const QString kCarrMain = table.lineAt("carr", 0)->scopeKey;
        const VariableDecl* snow = table.variableTable().find("SNOW", kCarrMain);
        check(snow != nullptr && snow->lengths == QList<int>({4}),
              QString("SNOW 长度 == [4]（得到 %1）")
                  .arg(snow && !snow->lengths.isEmpty() ? QString::number(snow->lengths.value(0)) : "?"));
        qint64 snow1 = 0;
        check(table.variableTable().constArrayAt("SNOW", 1, snow1, kCarrMain) && snow1 == 0x222222,
              "SNOW:1 == 0x222222");
        // 单元素常量仍然参与「常量折叠」；多元素常量数组不折叠
        qint64 scalar = 0;
        check(table.variableTable().constInt("SCALAR", scalar, kCarrMain) && scalar == 7,
              "#DIM CONST SCALAR = 7 -> 标量常数");
        qint64 snowScalar = 0;
        check(!table.variableTable().constInt("SNOW", snowScalar, kCarrMain),
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
        resumeIncremental();
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
        resumeIncremental();
        table.loadScript("scope", ss);
        table.markLoadComplete();

        const VariableTable& vt = table.variableTable();
        const QString kHolder = table.lineAt("scope", 0)->scopeKey;
        const QString kUser = table.lineAt("scope", 3)->scopeKey;
        const QString kReader = table.lineAt("scope", 6)->scopeKey;
        qint64 v = 0;
        check(vt.constInt("SHADOW", v, kHolder) && v == 51,
              "私有常量在声明它的函数内可见");
        check(!vt.constInt("SHADOW", v, kReader),
              "私有常量在别的函数内不可见");
        check(!vt.constInt("SHADOW", v),
              "私有常量在无作用域上下文不可见");
        check(!vt.constInt("SHADOW", v, kUser),
              "同名私有可写变量遮蔽别处的私有常量");
        check(vt.constInt("GSHADOW", v, kHolder) && v == 99,
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

    qDebug() << "\n5) 多维变量护栏（记录当前行为）";
    {
        // 夹具只记录**现状**：多维存储尚未真实分配（声明 2,3,4 的 GARR 只有
        // 扁平 1D 槽；VARSIZE 无维读的是那个槽的实际长度，不是声明形状）。
        ProcessState ms;
        EraParseTable mt(&ms);
        VariableStorage vs;
        ExpressionEvaluator ev;
        mt.setVariableStorage(&vs);
        mt.setExpressionEvaluator(&ev);
        ExecutionEngine eng(&vs, nullptr);
        eng.setParseTable(&mt);
        eng.setExpressionEvaluator(&ev);
        ScriptRunner runner(&mt, &eng, &ms, &vs);
        runner.setExpressionEvaluator(&ev);
        runner.setStepLimit(100000);

        const QStringList src = {
            "@MAIN",
            "#DIM GLOBAL GARR, 2, 3, 4",      // 3D，乘积 24
            "#DIM GLOBAL ONED, 4",            // 单维（轻量容器）
            "#DIM GLOBAL R_DIM2",
            "#DIM GLOBAL R_NODIM",
            "#DIM GLOBAL R_SZ0",
            "#DIM GLOBAL R_SZ1",
            "#DIM GLOBAL R_SZ2",
            "#DIM SAVEDATA SMD, 2, 3",        // 存档形状 2x3
            "#DIM MAT, 2, 3",                 // 函数私有 2D
            "GARR:1:2:3 = 42",
            "ONED:2 = 9",
            "MAT:1:2 = 5",
            "SMD:1:2 = 99",
            "R_DIM2 = VARSIZE(\"GARR\", 2)",
            "R_NODIM = VARSIZE(\"GARR\")",
            "VARSIZE GARR",
            "R_SZ0 = RESULT:0",
            "R_SZ1 = RESULT:1",
            "R_SZ2 = RESULT:2",
            "RETURN"
        };
        const AstResolver r = [&mt](const QString& e) { return mt.expressionAst(e); };
        QList<LogicalLine> lines;
        for (int i = 0; i < src.size(); ++i)
            lines.append(AstBuilder::build(src.at(i), ScriptPosition("md.ERB", i, 1), r));
        check(mt.loadScript("md", lines), "loadScript(多维夹具)");
        vs.setSaveVariableTable(&mt.variableTable());
        mt.setEntryPoint("MAIN");
        mt.markLoadComplete();
        const ExecState st = runner.runToCompletion();
        qDebug() << "      runner state =" << int(st) << "warnings:" << mt.parseWarnings();
        check(st == ExecState::Halt, "多维夹具运行到 HALT");

        check(vs.getGlobalInt3D("GARR", 1, 2, 3) == 42, "GARR:1:2:3 落到 3D 元素 42");
        // 脚本层 `GARR:23` 被静态检查拒绝（多维必须给足下标）。扁平写法只能在
        // 存储层出现：它现在是**同一个**多维扁平容器里的元素 23（=1*12+2*4+3）。
        vs.setGlobalInt1D("GARR", 23, 7);
        check(vs.getGlobalInt1D("GARR", 23) == 7, "GARR[23]（存储层扁平写）== 7");
        check(vs.getGlobalInt3D("GARR", 1, 2, 3) == vs.getGlobalInt1D("GARR", 23),
              "X:1:2:3 与 X[扁平] 落到同一槽位");
        // strides 正确性的直接护栏：不同下标元组必须落到不同槽位。dim0 的 stride
        // 若写错（例如用 1 而不是 lengths[1]*lengths[2]），GARR:1:0:0 与 GARR:0:0:1
        // 会别名到同一个扁平槽，下面两条断言就会红。
        vs.setGlobalInt3D("GARR", 1, 0, 0, 43);
        check(vs.getGlobalInt1D("GARR", 12) == 43, "GARR:1:0:0 落在扁平 12（=1*12+0*4+0）");
        check(vs.getGlobalInt3D("GARR", 0, 0, 1) == 0,
              "GARR:1:0:0 与 GARR:0:0:1 不别名（stride 正确）");
        check(vs.arraySize("GARR") == 2, "arraySize(GARR) == 声明首维 2（不再读 1D 占位槽）");
        check(vs.getGlobalInt1D("R_DIM2", 0) == 4, "VARSIZE(GARR,2) == 4（声明维）");
        qDebug() << "      R_NODIM=" << vs.getGlobalInt1D("R_NODIM",0); check(vs.getGlobalInt1D("R_NODIM", 0) == 2, "当前 VARSIZE(GARR) == 2（1D 槽按首维分配）");
        check(vs.getGlobalInt1D("R_SZ0", 0) == 2 && vs.getGlobalInt1D("R_SZ1", 0) == 3
                  && vs.getGlobalInt1D("R_SZ2", 0) == 4,
              "VARSIZE 语句一次写出 2/3/4（声明各维）");
        check(vs.getGlobalInt2D("MAT", 1, 2) == 5, "私有 2D MAT:1:2 == 5");
        check(vs.arraySize("MAT") == 2, "当前私有 MAT 的 1D 槽按首维分配 == 2");
        check(vs.getGlobalInt1D("ONED", 2) == 9 && vs.arraySize("ONED") == 4,
              "单维 ONED 装载期按声明分配 4（行为保持不变）");
        check(vs.getGlobalInt3D("GARR", 100, 0, 0) == 0, "越界读 == 0");
        vs.setGlobalInt3D("GARR", 100, 0, 0, 5);
        check(vs.getGlobalInt3D("GARR", 100, 0, 0) == 0, "多维越界写被忽略（不再懒扩容）");

        // 存档字节：SMD 声明 2x3，只有 [1][2]=99；golden md5 钉在这里。
        //
        // golden 变更理由（2026-10-22 重算）：
        //   1) 私有作用域身份由函数名改为段身份 (脚本, 段起点行)：SMD 的写盘名
        //      从 "MAIN\x1fSMD" 变成 eravar::makePrivateStorageKey(段身份, "SMD")
        //      （= "2:MD:0\x1fSMD"）；
        //   2) 存档容器版本 1808 -> 1809（版本字段 4 字节）。
        // 另外，旧 golden（7c71c4f2…）生成于 36fee1c「条目顺序确定性」之前，当时
        // 顺序来自 QHash 遍历（本测试没有固定种子），字节不可复现；现行顺序是
        // (slot, 大写名) 的数据全序，下方结构断言即该契约。
        auto doc = vs.captureBinarySave(EraBinaryData::FileType::Normal);
        doc.description = "multidim-guard"; doc.uniqueCode = 7; doc.scriptVersion = 1808;
        QByteArray bytes;
        check(EraBinaryData::encode(doc, bytes), "encode(多维存档)");
        const QByteArray md5 = QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex();
        qDebug() << "      multidim save md5 =" << md5 << "bytes =" << bytes.size();
        // 内容护栏（不只比 md5）：SMD 是唯一用户条目，形状 2x3，只有 [1][2]=99；
        // 非 SAVEDATA 的私有 MAT 不入档。
        {
            EraBinaryData::Document reread;
            check(EraBinaryData::decode(bytes, reread) && reread.variables.size() == doc.variables.size(),
                  "多维存档可解码（golden 变更前的结构护栏）");
            int smd = 0, mat = 0;
            for (const auto& v : reread.variables) {
                if (v.name == eravar::makePrivateStorageKey(eravar::segmentKeyText("md", 0), "SMD")) {
                    ++smd;
                    check(v.dimensions == QList<int>({2, 3}) && v.integers == QList<qint64>({0,0,0,0,0,99}),
                          "多维存档：SMD 形状/内容");
                }
                if (v.name.contains(QLatin1Char('\x1f')) && v.name.endsWith(QLatin1String("MAT"))) ++mat;
            }
            check(smd == 1 && mat == 0, "多维存档：只有 SAVEDATA 的私有条目入档");
        }
        check(md5 == QByteArrayLiteral("b84c72e55b6586fe045a9238f0a9c9b0"),
              QString("多维存档字节 md5（实得 %1）").arg(QString::fromLatin1(md5)));
    }

    qDebug() << "\n===================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] variable-table tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
