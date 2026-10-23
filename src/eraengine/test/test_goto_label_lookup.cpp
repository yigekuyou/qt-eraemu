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
// test_goto_label_lookup.cpp
//
// 验收物：**GOTO 的 $ 标签解析 = 一次索引查表**。段身份（LabelRef::functionLine）
// 在装载期写进唯一那张标签索引，运行期只做
//     firstGotoLabelPosition(script, 段, 名字)
// 这一次查询，不再从函数入口逐行往下扫。
//
// 两部分：
//   1) 纯函数 position::firstGotoLabel（手搓 ScriptData + 手搓索引，不经装载/
//      执行流水线）—— 段过滤这一条规则可以逐条钉死，报错时一眼看出是哪个
//      过滤条件坏了。
//   2) 真 EraParseTable + AstBuilder + ScriptRunner：同一批脚本真的跑起来，
//      用「哪一行执行了 / 哪个变量被写了 / 错误状态与位置」做断言。
//
// 覆盖 a~e（任务书编号）：
//   a) 同名 $ 标签分处两个函数（同一脚本）-> GOTO 各自命中本函数那一条。
//      旧实现走脚本级 labelPositions（last-wins 一座）会跳进**别的函数**。
//   b) 同一函数内同名 $ 重复声明 -> 固定取该段内行号最小的一条
//      （与 firstLabel 的 first-wins 同一口径）。断言具体行号 3，不是「两者相等」。
//   c) 找不到标签 -> 与旧行为一致：ExecState::Error + errorOccurred，
//      且位置**完全不动**（不发 positionChanged）。
//   d) 装载顺序无关：同一批脚本以相反顺序 loadScript，a/b/e 的断言值逐字相同。
//   e) $ 在第一个 @ 之前（文件头段，functionLine = -1）：从文件头段 GOTO 命中；
//      从函数体内 GOTO 同一个名字不命中（段不同）-> 错误。
//
// 装载协议：loadScript 在 Finalized 后会被拒绝，追加脚本必须先
// beginIncrementalLoad()（见第 2 节末尾的附加断言）。
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>

#include "ast/ast_builder.h"
#include "ast/expression_evaluator.h"
#include "ast/logical_line.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "position_state.h"
#include "process_state.h"
#include "script_runner.h"
#include "variable_storage.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

static void checkInt(int actual, int expected, const QString& what) {
    check(actual == expected, QStringLiteral("%1 (got %2, want %3)")
                                   .arg(what).arg(actual).arg(expected));
}

static void checkStr(const QString& actual, const QString& expected, const QString& what) {
    check(actual == expected, QStringLiteral("%1 (got \"%2\", want \"%3\")")
                                  .arg(what, actual.isEmpty() ? QStringLiteral("<empty>") : actual,
                                       expected.isEmpty() ? QStringLiteral("<empty>") : expected));
}

// 位置命中判据：索引里的指针是否指向 (script, line)。
static void checkRef(const LabelRef* ref, const QString& script, int line, const QString& what) {
    if (!ref) { check(false, what + QStringLiteral(" (got nullptr)")); return; }
    checkInt(ref->line, line, what + QStringLiteral(" [line]"));
    checkStr(ref->script, script, what + QStringLiteral(" [script]"));
}

// ===========================================================================
// 1) 纯函数：position::firstGotoLabel 的段过滤
//
// 手搓两个 ScriptData + 一张与装载收尾同形的索引（同脚本内按行号升序）。
//
// L.ERB                                  索引（名字 -> 声明，全序第一条在前）
//   0  $HEADER      seg = -1              HEADER -> L.ERB:0  seg=-1
//   1  CHECK = 1    seg = -1              LOOP   -> L.ERB:4  seg=2
//   2  @ALPHA       seg = 2                        L.ERB:7  seg=5
//   3  GOTO LOOP    seg = 2                        M.ERB:1  seg=0
//   4  $LOOP        seg = 2   <- ALPHA 的     DUP    -> L.ERB:9  seg=8
//   5  @BETA        seg = 5                        L.ERB:10 seg=8
//   6  GOTO LOOP    seg = 5                        L.ERB:11 seg=8
//   7  $LOOP        seg = 5   <- BETA 的      ALPHA  -> L.ERB:2  seg=2 (@)
//   8  @DUPFN       seg = 8                BETA   -> L.ERB:5  seg=5 (@)
//   9  $DUP         seg = 8                DUPFN  -> L.ERB:8  seg=8 (@)
//  10  $DUP         seg = 8                OTHER  -> M.ERB:0  seg=0 (@)
//  11  $DUP         seg = 8
//
// M.ERB
//   0  @OTHER       seg = 0
//   1  $LOOP        seg = 0   <- 与 L.ERB 同名，但**别的脚本**
// ===========================================================================
static LogicalLine instr(const QString& name, const QString& owner) {
    LogicalLine ll;
    ll.kind = LineKind::Instruction;
    ll.functionName = name;
    ll.ownerFunction = owner;
    return ll;
}

static LogicalLine funcLabel(const QString& label) {
    LogicalLine ll;
    ll.kind = LineKind::FunctionLabel;
    ll.labelName = label;
    ll.ownerFunction = label;
    return ll;
}

static LogicalLine gotoLabel(const QString& label, const QString& owner) {
    LogicalLine ll;
    ll.kind = LineKind::GotoLabel;
    ll.labelName = label;
    ll.ownerFunction = owner;
    return ll;
}

struct GotoFixture {
    QHash<QString, ScriptData> scripts;
    QHash<QString, QList<LabelRef>> labels;

    GotoFixture() {
        ScriptData l;
        l.lines = {gotoLabel("HEADER", QString()),
                   instr("CHECK = 1", QString()),
                   funcLabel("ALPHA"), instr("GOTO LOOP", "ALPHA"),
                   gotoLabel("LOOP", "ALPHA"),
                   funcLabel("BETA"), instr("GOTO LOOP", "BETA"),
                   gotoLabel("LOOP", "BETA"),
                   funcLabel("DUPFN"), gotoLabel("DUP", "DUPFN"),
                   gotoLabel("DUP", "DUPFN"), gotoLabel("DUP", "DUPFN")};
        ScriptData m;
        m.lines = {funcLabel("OTHER"), gotoLabel("LOOP", "OTHER")};
        scripts.insert(QStringLiteral("L.ERB"), l);
        scripts.insert(QStringLiteral("M.ERB"), m);

        // LabelRef{script, line, functionLine, isFunction}
        labels[QStringLiteral("HEADER")] = {LabelRef{QStringLiteral("L.ERB"), 0, -1, false}};
        labels[QStringLiteral("LOOP")] = {LabelRef{QStringLiteral("L.ERB"), 4, 2, false},
                                          LabelRef{QStringLiteral("L.ERB"), 7, 5, false},
                                          LabelRef{QStringLiteral("M.ERB"), 1, 0, false}};
        labels[QStringLiteral("DUP")] = {LabelRef{QStringLiteral("L.ERB"), 9, 8, false},
                                         LabelRef{QStringLiteral("L.ERB"), 10, 8, false},
                                         LabelRef{QStringLiteral("L.ERB"), 11, 8, false}};
        labels[QStringLiteral("ALPHA")] = {LabelRef{QStringLiteral("L.ERB"), 2, 2, true}};
        labels[QStringLiteral("BETA")] = {LabelRef{QStringLiteral("L.ERB"), 5, 5, true}};
        labels[QStringLiteral("DUPFN")] = {LabelRef{QStringLiteral("L.ERB"), 8, 8, true}};
        labels[QStringLiteral("OTHER")] = {LabelRef{QStringLiteral("M.ERB"), 0, 0, true}};
    }

    ScriptView view() const {
        ScriptView v;
        v.scripts = &scripts;
        v.labelIndex = &labels;
        return v;
    }
};

static void testFirstGotoLabelUnit() {
    qDebug() << "\n1) position::firstGotoLabel：段过滤（同脚本 + 同段 + 只认 $）";
    GotoFixture f;
    const ScriptView view = f.view();

    // 同一脚本里两个同名的 $LOOP（段 2 / 段 5）：答案由**传入的段**决定。
    checkRef(position::firstGotoLabel(view, "LOOP", "L.ERB", 2), "L.ERB", 4,
             "段 2（@ALPHA）-> 行 4");
    checkRef(position::firstGotoLabel(view, "LOOP", "L.ERB", 5), "L.ERB", 7,
             "段 5（@BETA）-> 行 7（同一个脚本、同一个名字，只是段不同）");
    // 段过滤：本脚本有 LOOP，但两处都在别的段 -> 不是「跨段找一条」。
    check(position::firstGotoLabel(view, "LOOP", "L.ERB", 0) == nullptr,
          "段 0 里没有 LOOP -> nullptr（不跨段）");
    check(position::firstGotoLabel(view, "LOOP", "L.ERB", 99) == nullptr,
          "不存在的段号 -> nullptr");

    // 脚本过滤：段号相同（0）但脚本不同，不能串。
    checkRef(position::firstGotoLabel(view, "LOOP", "M.ERB", 0), "M.ERB", 1,
             "M.ERB 段 0 -> 行 1（段号相同也不跨脚本）");
    check(position::firstGotoLabel(view, "LOOP", "NO.ERB", 0) == nullptr,
          "不存在的脚本 -> nullptr");
    check(position::firstGotoLabel(view, "NOPE", "L.ERB", 2) == nullptr,
          "不存在的名字 -> nullptr");

    // 只认 $：@ 标签同名也不能被 GOTO 命中。
    check(position::firstGotoLabel(view, "ALPHA", "L.ERB", 2) == nullptr,
          "@ALPHA 不是 GOTO 目标（ref.isFunction -> 跳过）");

    // 名字大小写折叠（索引键的唯一形式）。
    checkRef(position::firstGotoLabel(view, "loop", "L.ERB", 5), "L.ERB", 7,
             "小写 \"loop\" 与 \"LOOP\" 同解");

    // 同段重复声明：取行号最小的一条（first-wins，与 firstLabel 同口径）。
    checkRef(position::firstGotoLabel(view, "DUP", "L.ERB", 8), "L.ERB", 9,
             "同段三条 DUP -> 行 9（行号最小）");
    checkRef(position::firstGotoLabel(view, "dup", "L.ERB", 8), "L.ERB", 9,
             "同段三条 dup（小写）-> 行 9");

    // 文件头段：functionLine = -1。
    checkRef(position::firstGotoLabel(view, "HEADER", "L.ERB", -1), "L.ERB", 0,
             "文件头段（-1）-> 行 0");
    check(position::firstGotoLabel(view, "HEADER", "L.ERB", 2) == nullptr,
          "函数段 2 里没有 HEADER（段不同）-> nullptr");

    // 空索引：不崩、返回 nullptr。
    ScriptView empty;
    check(position::firstGotoLabel(empty, "LOOP", "L.ERB", 2) == nullptr,
          "labelIndex == nullptr -> nullptr");

    // 同一张索引的两个查询：firstLabel 只筛「名字 + 脚本」，firstGotoLabel 多筛「段」。
    const LabelRef* plain = position::firstLabel(view, "LOOP", /*callableOnly=*/false, "L.ERB");
    checkRef(plain, "L.ERB", 4, "firstLabel(LOOP, L.ERB) -> 全序第一条 = 行 4");
    const LabelRef* seg2 = position::firstGotoLabel(view, "LOOP", "L.ERB", 2);
    check(seg2 && plain && seg2->script == plain->script && seg2->line == plain->line,
          "段 2 时两个查询给出**同一条**（段过滤只影响不匹配的段）");
    const LabelRef* seg5 = position::firstGotoLabel(view, "LOOP", "L.ERB", 5);
    check(seg5 && seg5->line == 7 && plain->line == 4,
          "段 5 时 firstGotoLabel(7) != firstLabel(4)：差异只能来自段过滤");

    // 索引里的段字段本身（数据，不是消费者）：GOTO 的目标必须带着声明处的段。
    const QList<LabelRef> loops = f.labels.value(QStringLiteral("LOOP"));
    checkInt(int(loops.size()), 3, "索引收了 3 份 LOOP 声明");
    if (loops.size() == 3) {
        checkInt(loops.at(0).functionLine, 2, "L.ERB:4 的 functionLine = 2");
        checkInt(loops.at(1).functionLine, 5, "L.ERB:7 的 functionLine = 5");
        checkInt(loops.at(2).functionLine, 0, "M.ERB:1 的 functionLine = 0");
        check(!loops.at(0).isFunction && !loops.at(1).isFunction && !loops.at(2).isFunction,
              "$ 标签全部 isFunction = false");
        check(f.labels.value(QStringLiteral("HEADER")).first().functionLine == -1,
              "$HEADER（第一个 @ 之前）的 functionLine = -1");
    }
}

// ===========================================================================
// 2) 真 EraParseTable：装载期把段写进索引 + 运行期一次查表
// ===========================================================================

// ---- 参照脚本（行号写死在下方的断言里）------------------------------------
// a.ERB：两个函数各有一条 $LOOP
//   0  @ALPHA        3  $LOOP（ALPHA 的）      6  @BETA        9  $LOOP（BETA 的）
// b.ERB：同一函数内 $DUP 重复；外加一个必然失败的 @MISSINGFN
//   0  @DUPFN        3  $DUP（第一条）         6  $DUP（第二条）  8  $DUPEND
//  10  @MISSINGFN   11  GOTO NOPE
// h.ERB：$HEADER 在第一个 @ 之前（文件头段）
//   0  GOTO HEADER   1  $HEADER                4  @HEADERFN      5  GOTO HEADER
static QStringList srcA() {
    return {QStringLiteral("@ALPHA"),        // 0
            QStringLiteral("GOTO LOOP"),     // 1
            QStringLiteral("A_AFTER = 99"),  // 2（跳过）
            QStringLiteral("$LOOP"),         // 3  <- ALPHA 的 $LOOP
            QStringLiteral("A_OUT = 10"),    // 4
            QStringLiteral("RETURN"),        // 5
            QStringLiteral("@BETA"),         // 6
            QStringLiteral("GOTO LOOP"),     // 7
            QStringLiteral("B_AFTER = 99"),  // 8（跳过）
            QStringLiteral("$LOOP"),         // 9  <- BETA 的 $LOOP
            QStringLiteral("B_OUT = 20"),    // 10
            QStringLiteral("RETURN")};       // 11
}

static QStringList srcB() {
    return {QStringLiteral("@DUPFN"),        // 0
            QStringLiteral("GOTO DUP"),      // 1
            QStringLiteral("D_AFTER = 99"),  // 2（跳过）
            QStringLiteral("$DUP"),          // 3  <- 第一条声明
            QStringLiteral("D_OUT = 1"),     // 4
            QStringLiteral("GOTO DUPEND"),   // 5
            QStringLiteral("$DUP"),          // 6  <- 第二条声明（永远不该命中）
            QStringLiteral("D_OUT = 2"),     // 7
            QStringLiteral("$DUPEND"),       // 8
            QStringLiteral("RETURN"),        // 9
            QStringLiteral("@MISSINGFN"),    // 10
            QStringLiteral("GOTO NOPE"),     // 11 <- 找不到 -> 错误，位置停在这里
            QStringLiteral("M_AFTER = 99"),  // 12（不执行）
            QStringLiteral("RETURN")};       // 13
}

static QStringList srcH() {
    return {QStringLiteral("GOTO HEADER"),   // 0  <- 文件头段（functionLine = -1）
            QStringLiteral("$HEADER"),       // 1
            QStringLiteral("H_OUT = 7"),     // 2
            QStringLiteral("RETURN"),        // 3  <- 空栈 -> Halt
            QStringLiteral("@HEADERFN"),     // 4
            QStringLiteral("GOTO HEADER"),   // 5  <- 段 4 != -1 -> 必须失败
            QStringLiteral("RETURN")};       // 6
}

// ---- 真装载 + 真执行的夹具 ------------------------------------------------
struct Harness {
    VariableStorage storage;
    ProcessState state;
    EraParseTable table;
    ExpressionEvaluator evaluator;
    ExecutionEngine engine;
    ScriptRunner runner;
    QStringList errors;     // ScriptRunner::errorOccurred
    QStringList positions;  // positionChanged -> "脚本:行"

    Harness()
        : table(&state), engine(&storage, nullptr), runner(&table, &engine, &state, &storage) {
        table.setExpressionEvaluator(&evaluator);
        table.setVariableStorage(&storage);
        engine.setParseTable(&table);
        engine.setExpressionEvaluator(&evaluator);
        runner.setExpressionEvaluator(&evaluator);
        runner.setStepLimit(5000);
        QObject::connect(&runner, &ScriptRunner::errorOccurred,
                         [this](const QString& m) { errors << m; });
        QObject::connect(&table, &EraParseTable::positionChanged,
                         [this](const QString& s, int l) {
                             positions << QStringLiteral("%1:%2").arg(s).arg(l);
                         });
    }

    void load(const QString& name, const QStringList& src) {
        QList<LogicalLine> lines;
        for (int i = 0; i < src.size(); ++i) {
            lines.append(AstBuilder::build(src.at(i), ScriptPosition(name, i, 0),
                                           [this](const QString& text) {
                                               return table.expressionAst(text);
                                           }));
        }
        table.loadScript(name, lines);
    }

    // 按给定顺序装载全部三个脚本（装载序是 d 的自变量）。
    void loadAll(const QStringList& order) {
        for (const QString& name : order) {
            if (name == QLatin1String("a.ERB")) load(name, srcA());
            else if (name == QLatin1String("b.ERB")) load(name, srcB());
            else if (name == QLatin1String("h.ERB")) load(name, srcH());
        }
    }
};

// labels() 只投影 @ 声明（事件导航用），所以 $ 标签的声明列表直接看索引本体。
static QList<LabelRef> decls(const EraParseTable& table, const QString& name) {
    return table.labelIndex().value(name.toUpper());
}

static void testTableAndExecution() {
    // ---- 装载期：索引里带着段 ------------------------------------------------
    qDebug() << "\n2) 装载期：LabelRef::functionLine 由 loadScript 唯一登记点写入";
    Harness h;
    h.loadAll({QStringLiteral("a.ERB"), QStringLiteral("b.ERB"), QStringLiteral("h.ERB")});
    h.table.markLoadComplete();

    // labels() 只投影 @ 声明（事件导航要的是函数），$ 标签直接看索引本体。
    const QList<LabelRef> loops = decls(h.table, QStringLiteral("LOOP"));
    checkInt(int(loops.size()), 2, "a.ERB 的两条 $LOOP 都进索引");
    if (loops.size() == 2) {
        checkInt(loops.at(0).line, 3, "第一条声明 = 行 3");
        checkInt(loops.at(0).functionLine, 0, "行 3 属于 @ALPHA（段 = 0）");
        checkInt(loops.at(1).line, 9, "第二条声明 = 行 9");
        checkInt(loops.at(1).functionLine, 6, "行 9 属于 @BETA（段 = 6）");
    }
    const QList<LabelRef> dups = decls(h.table, QStringLiteral("DUP"));
    checkInt(int(dups.size()), 2, "b.ERB 的两条 $DUP 都进索引（事件导航需要全部）");
    if (dups.size() == 2) {
        checkInt(dups.at(0).line, 3, "索引第一条 = 行 3（first-wins）");
        checkInt(dups.at(0).functionLine, 0, "两条都在 @DUPFN 的段 0");
        checkInt(dups.at(1).functionLine, 0, "第二条也在段 0");
    }
    {
        const QList<LabelRef> headers = decls(h.table, QStringLiteral("HEADER"));
        checkInt(int(headers.size()), 1, "索引收了 1 份 $HEADER 声明");
        if (!headers.isEmpty()) {
            checkInt(headers.first().functionLine, -1,
                     "$HEADER 的 functionLine = -1（第一个 @ 之前）");
        }
    }

    // 一次查表：段 + 名字 -> 行号；脚本不存在 / 段不存在 / 名字不存在 -> -1。
    checkInt(h.table.firstGotoLabelPosition(QStringLiteral("a.ERB"), 0, QStringLiteral("LOOP")), 3,
             "firstGotoLabelPosition(a.ERB, 段0, LOOP) = 3");
    checkInt(h.table.firstGotoLabelPosition(QStringLiteral("a.ERB"), 6, QStringLiteral("LOOP")), 9,
             "firstGotoLabelPosition(a.ERB, 段6, LOOP) = 9");
    checkInt(h.table.firstGotoLabelPosition(QStringLiteral("a.ERB"), 3, QStringLiteral("LOOP")), -1,
             "段 3 不是任何段的起点 -> -1（段身份是 @ 的行号，不是任意行号）");
    checkInt(h.table.firstGotoLabelPosition(QStringLiteral("a.ERB"), 0, QStringLiteral("NOPE")), -1,
             "名字不存在 -> -1");
    checkInt(h.table.firstGotoLabelPosition(QStringLiteral("no.ERB"), 0, QStringLiteral("LOOP")), -1,
             "脚本不存在 -> -1");
    checkInt(h.table.firstGotoLabelPosition(QStringLiteral("h.ERB"), -1, QStringLiteral("HEADER")), 1,
             "文件头段（-1）$HEADER -> 行 1");
    checkInt(h.table.firstGotoLabelPosition(QStringLiteral("h.ERB"), 4, QStringLiteral("HEADER")), -1,
             "@HEADERFN 的段 4 里没有 HEADER -> -1");

    // currentFunctionSegment：无帧时按执行行的 ownerFunction 回查段起点。
    h.table.setPosition(QStringLiteral("a.ERB"), 1, /*jumped=*/false);
    checkInt(h.table.currentFunctionSegment(), 0, "无帧 + 在 @ALPHA 体内 -> 段 0");
    h.table.setPosition(QStringLiteral("a.ERB"), 7, /*jumped=*/false);
    checkInt(h.table.currentFunctionSegment(), 6, "无帧 + 在 @BETA 体内 -> 段 6");
    h.table.setPosition(QStringLiteral("h.ERB"), 0, /*jumped=*/false);
    checkInt(h.table.currentFunctionSegment(), -1, "无帧 + 文件头段 -> -1");

    // ---- 附加：Finalized 之后追加脚本必须先 beginIncrementalLoad ------------
    check(!h.table.loadScript(QStringLiteral("late.ERB"),
                              {funcLabel(QStringLiteral("LATE")),
                               gotoLabel(QStringLiteral("LOOP"), QStringLiteral("LATE"))}),
          "Finalized 后 loadScript 被拒绝（装载协议）");
    check(h.table.beginIncrementalLoad(), "beginIncrementalLoad 打开增量装载");
    check(h.table.loadScript(QStringLiteral("i.ERB"),
                             {funcLabel(QStringLiteral("IFN")),
                              gotoLabel(QStringLiteral("ILOOP"), QStringLiteral("IFN")),
                              instr(QStringLiteral("RETURN"), QStringLiteral("IFN"))}),
          "增量装载 i.ERB");
    checkInt(h.table.firstGotoLabelPosition(QStringLiteral("i.ERB"), 0, QStringLiteral("ILOOP")), 1,
             "增量装载的脚本同样在**同一处**登记段（functionLine = 0）");
    checkInt(h.table.firstGotoLabelPosition(QStringLiteral("i.ERB"), 1, QStringLiteral("ILOOP")), -1,
             "增量装载：段不匹配仍然不命中");
}

// ---- a) 同名 $ 分处两个函数 ------------------------------------------------
static void testA() {
    qDebug() << "\n3) a) 同名 $LOOP 分处 @ALPHA / @BETA：各自命中本函数那一条";
    Harness h;
    h.loadAll({QStringLiteral("a.ERB"), QStringLiteral("b.ERB"), QStringLiteral("h.ERB")});
    h.table.markLoadComplete();

    h.table.setEntryPoint(QStringLiteral("ALPHA"));
    h.positions.clear();
    check(h.runner.runToCompletion() == ExecState::Halt, "ALPHA 正常结束（无跳转错误）");
    checkInt(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("A_OUT"), 0), 10,
             "ALPHA 的 GOTO LOOP 落在自己的 $LOOP（行 3）");
    checkInt(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("A_AFTER"), 0), 0,
             "GOTO 之后的兜底行没有执行");
    check(h.positions.contains(QStringLiteral("a.ERB:3")), "跳转落点行 = 3");
    check(!h.positions.contains(QStringLiteral("a.ERB:9")),
          "**没有**跳到 @BETA 的 $LOOP（行 9）");

    h.table.setEntryPoint(QStringLiteral("BETA"));
    h.positions.clear();
    check(h.runner.runToCompletion() == ExecState::Halt, "BETA 正常结束");
    checkInt(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("B_OUT"), 0), 20,
             "BETA 的 GOTO LOOP 落在自己的 $LOOP（行 9）");
    checkInt(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("B_AFTER"), 0), 0,
             "GOTO 之后的兜底行没有执行");
    check(h.positions.contains(QStringLiteral("a.ERB:9")), "跳转落点行 = 9");
    check(!h.positions.contains(QStringLiteral("a.ERB:3")),
          "**没有**跳到 @ALPHA 的 $LOOP（行 3）");
    check(h.errors.isEmpty(), "a 场景没有 errorOccurred");

    // 运行时算出来的段就是索引里的那个（不是「行 0 一律」）。
    checkInt(h.table.firstGotoLabelPosition(QStringLiteral("a.ERB"), 0, QStringLiteral("LOOP")), 3,
             "ALPHA 段 -> 3");
    checkInt(h.table.firstGotoLabelPosition(QStringLiteral("a.ERB"), 6, QStringLiteral("LOOP")), 9,
             "BETA 段 -> 9");
}

// ---- b) 同一函数内重复声明 -------------------------------------------------
static void testB() {
    qDebug() << "\n4) b) 同一函数内 $DUP 重复声明：取段内行号最小的一条（= 3，不是 6）";
    Harness h;
    h.loadAll({QStringLiteral("a.ERB"), QStringLiteral("b.ERB"), QStringLiteral("h.ERB")});
    h.table.markLoadComplete();

    // 直接断言具体行号（与 firstLabel 的 first-wins 同一口径）。
    checkInt(h.table.firstGotoLabelPosition(QStringLiteral("b.ERB"), 0, QStringLiteral("DUP")), 3,
             "firstGotoLabelPosition(b.ERB, 段0, DUP) = 3（第一条声明）");

    h.table.setEntryPoint(QStringLiteral("DUPFN"));
    h.positions.clear();
    check(h.runner.runToCompletion() == ExecState::Halt, "DUPFN 正常结束");
    checkInt(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("D_OUT"), 0), 1,
             "GOTO DUP 执行的是行 3 那一段（D_OUT = 1）");
    checkInt(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("D_AFTER"), 0), 0,
             "GOTO 之后的兜底行没有执行");
    check(h.positions.contains(QStringLiteral("b.ERB:3")), "落点 = 行 3");
    check(!h.positions.contains(QStringLiteral("b.ERB:6")),
          "**没有**落到第二条声明（行 6）—— 旧实现 last-wins 会答 6");
    check(h.errors.isEmpty(), "b 场景没有 errorOccurred");
}

// ---- A) 同名标签分处两个脚本：答案只能来自唯一索引的全序第一条 ------------
// 被删掉的 ErbLoader 裸名表是「名字 -> (script, line)」的最后一笔写入：
// 键大小写敏感、重复声明 last-wins、没有脚本维度。a.ERB 先装、z.ERB 后装时
// 它会答 z.ERB —— 与唯一索引的口径（脚本名 QString::compare 最小的 a.ERB）
// 相反。这里用真 EraParseTable 装载，钉住索引口径。
static void testSameNameAcrossScripts() {
    qDebug() << "\n2b) 同名标签分处两个脚本：唯一索引口径（脚本名 -> 行号 全序第一条）";
    const auto probe = [](const QStringList& order, const QString& name) -> QString {
        Harness h;
        for (const QString& script : order) {
            if (script == QLatin1String("a.ERB")) {
                h.load(script, {QStringLiteral("@A"), QStringLiteral("$SHARED"),
                                QStringLiteral("RETURN")});
            } else {
                h.load(script, {QStringLiteral("@Z"), QStringLiteral("$SHARED"),
                                QStringLiteral("RETURN")});
            }
        }
        h.table.markLoadComplete();
        ScriptView view;
        view.labelIndex = &h.table.labelIndex();
        const LabelRef* ref = position::firstLabel(view, name, /*callableOnly=*/false);
        return ref ? QStringLiteral("%1:%2").arg(ref->script).arg(ref->line)
                   : QStringLiteral("<none>");
    };
    checkStr(probe({QStringLiteral("a.ERB"), QStringLiteral("z.ERB")}, QStringLiteral("SHARED")),
             QStringLiteral("a.ERB:1"),
             "a 先装 / z 后装 -> a.ERB:1（不是最后写入的 z.ERB）");
    checkStr(probe({QStringLiteral("z.ERB"), QStringLiteral("a.ERB")}, QStringLiteral("SHARED")),
             QStringLiteral("a.ERB:1"), "装载序反过来 -> 仍是 a.ERB:1（全序与装载序无关）");
    checkStr(probe({QStringLiteral("a.ERB"), QStringLiteral("z.ERB")}, QStringLiteral("shArEd")),
             QStringLiteral("a.ERB:1"), "混合大小写查询同解（索引键大写折叠）");
}

// ---- c) 找不到标签 ---------------------------------------------------------
static void testC() {
    qDebug() << "\n5) c) 找不到 $ 标签：进入错误状态且位置不变";
    Harness h;
    h.loadAll({QStringLiteral("a.ERB"), QStringLiteral("b.ERB"), QStringLiteral("h.ERB")});
    h.table.markLoadComplete();

    // 位置显式钉在 GOTO 那一行（b.ERB:11），jumped=true 表示这是执行落点。
    h.table.setPosition(QStringLiteral("b.ERB"), 11, /*jumped=*/true);
    h.positions.clear();
    h.errors.clear();
    const ExecState st = h.runner.runToCompletion();

    check(st == ExecState::Error, "GOTO NOPE -> ExecState::Error");
    check(h.state.getExecState() == ExecState::Error, "ProcessState 进入错误状态");
    // 一次失败只留一条有用的消息：GOTO 的具体消息之后，stepOnce 不再补发
    // 泛化的 "execution error"（旧实现会发两条，宿主显示最后一条时玩家只看到
    // "execution error"）。
    checkInt(int(h.errors.size()), 1, "恰好一条 errorOccurred（不补发泛化 execution error）");
    if (!h.errors.isEmpty()) {
        check(h.errors.first().contains(QStringLiteral("GOTO label not found: NOPE")),
              "错误消息含 \"GOTO label not found: NOPE\"");
        check(!h.errors.contains(QStringLiteral("execution error")),
              "没有泛化的 \"execution error\"");
    }
    // 位置不变 = 一条 positionChanged 都没发（位置只能经由 setPosition/advance 改变）。
    check(h.positions.isEmpty(), "错误路径不发 positionChanged（位置不变）");
    checkInt(h.table.currentLine(), 11, "PC 仍停在 GOTO 那一行");
    checkStr(h.table.currentScript(), "b.ERB", "脚本不变");
    checkInt(h.table.depth(), 0, "没有压帧（GOTO 不产生返回地址）");
    checkInt(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("M_AFTER"), 0), 0,
             "GOTO 的下一行没有执行");
}

// ---- e) 文件头段 ----------------------------------------------------------
static void testE() {
    qDebug() << "\n6) e) $ 在第一个 @ 之前（functionLine = -1）";
    Harness h;
    h.loadAll({QStringLiteral("a.ERB"), QStringLiteral("b.ERB"), QStringLiteral("h.ERB")});
    h.table.markLoadComplete();

    // 从文件头段 GOTO：段身份 -1，$HEADER 的 functionLine 也是 -1 -> 命中。
    h.table.setPosition(QStringLiteral("h.ERB"), 0, /*jumped=*/false);
    checkInt(h.table.currentFunctionSegment(), -1, "文件头段的段身份 = -1");
    h.positions.clear();
    check(h.runner.runToCompletion() == ExecState::Halt, "文件头段脚本正常结束");
    checkInt(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("H_OUT"), 0), 7,
             "文件头段的 GOTO HEADER 命中了 $HEADER（行 1）");
    check(h.positions.contains(QStringLiteral("h.ERB:1")), "落点 = 行 1");
    check(h.errors.isEmpty(), "文件头段 GOTO 没有报错");

    // 从函数体内 GOTO 同一个名字：段 4 != -1 -> 不命中 -> 错误。
    Harness h2;
    h2.loadAll({QStringLiteral("a.ERB"), QStringLiteral("b.ERB"), QStringLiteral("h.ERB")});
    h2.table.markLoadComplete();
    h2.table.setEntryPoint(QStringLiteral("HEADERFN"));
    checkInt(h2.table.currentFunctionSegment(), 4, "进入 @HEADERFN 后段身份 = 4");
    h2.positions.clear();
    h2.errors.clear();
    check(h2.runner.runToCompletion() == ExecState::Error,
          "@HEADERFN 里 GOTO HEADER -> 错误（文件头段的 $ 不属于本段）");
    checkInt(int(h2.errors.size()), 1, "恰好一条 errorOccurred（不补发泛化 execution error）");
    check(!h2.errors.isEmpty()
              && h2.errors.first().contains(QStringLiteral("GOTO label not found: HEADER")),
          "错误消息含 \"GOTO label not found: HEADER\"");
    // 入口 @ 标签行会先 advance 到 GOTO 行（唯一一次位置变化），之后不再动。
    checkStr(h2.positions.join(QLatin1Char(',')), QStringLiteral("h.ERB:5"),
             "错误路径的位置轨迹只有「入口标签 -> GOTO 行」这一跳");
    checkInt(h2.table.currentLine(), 5, "PC 停在 GOTO 那一行（h.ERB:5）");
    checkInt(h2.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("H_OUT"), 0), 0,
             "$HEADER 之后的 H_OUT 没有执行（没有跨段跳进文件头段）");
}

// ---- d) 装载顺序无关 ------------------------------------------------------
// 每一轮都把 a/b/h 三个脚本按给定顺序装载，跑同一批程序，把**全部**可观察值
// 拼成一个字符串。正序与逆序的结果必须逐字相同 —— 钉住「与 QHash / 装载序无关」。
static QString stateName(ExecState s) {
    switch (s) {
    case ExecState::Continue: return QStringLiteral("Continue");
    case ExecState::WaitInput: return QStringLiteral("WaitInput");
    case ExecState::WaitSystemInput: return QStringLiteral("WaitSystemInput");
    case ExecState::WaitEvent: return QStringLiteral("WaitEvent");
    case ExecState::Halt: return QStringLiteral("Halt");
    case ExecState::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("?");
}

static QString roundSummary(const QStringList& order) {
    QStringList out;

    // a：两个函数各自的 GOTO 落点（用输出变量 + 位置轨迹双重观察）
    {
        Harness h;
        h.loadAll(order);
        h.table.markLoadComplete();
        h.table.setEntryPoint(QStringLiteral("ALPHA"));
        h.positions.clear();
        const ExecState s1 = h.runner.runToCompletion();
        const QString posA = h.positions.join(QLatin1Char(','));
        h.table.setEntryPoint(QStringLiteral("BETA"));
        h.positions.clear();
        const ExecState s2 = h.runner.runToCompletion();
        const QString posB = h.positions.join(QLatin1Char(','));
        out << QStringLiteral("a[%1|%2|%3|%4|%5|%6|%7|%8]")
                   .arg(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("A_OUT"), 0))
                   .arg(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("A_AFTER"), 0))
                   .arg(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("B_OUT"), 0))
                   .arg(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("B_AFTER"), 0))
                   .arg(stateName(s1), stateName(s2), posA, posB);
        out << QStringLiteral("aApi[%1|%2]")
                   .arg(h.table.firstGotoLabelPosition(QStringLiteral("a.ERB"), 0,
                                                       QStringLiteral("LOOP")))
                   .arg(h.table.firstGotoLabelPosition(QStringLiteral("a.ERB"), 6,
                                                       QStringLiteral("LOOP")));
    }

    // b：同函数重复声明 -> 具体行号 + 执行结果
    {
        Harness h;
        h.loadAll(order);
        h.table.markLoadComplete();
        h.table.setEntryPoint(QStringLiteral("DUPFN"));
        h.positions.clear();
        const ExecState s = h.runner.runToCompletion();
        out << QStringLiteral("b[%1|%2|%3|%4|%5|%6]")
                   .arg(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("D_OUT"), 0))
                   .arg(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("D_AFTER"), 0))
                   .arg(stateName(s))
                   .arg(h.table.firstGotoLabelPosition(QStringLiteral("b.ERB"), 0,
                                                       QStringLiteral("DUP")))
                   .arg(h.positions.join(QLatin1Char(',')))
                   .arg(decls(h.table, QStringLiteral("DUP")).size());
    }

    // e：文件头段命中 + 函数体内不命中
    {
        Harness h;
        h.loadAll(order);
        h.table.markLoadComplete();
        h.table.setPosition(QStringLiteral("h.ERB"), 0, /*jumped=*/false);
        h.positions.clear();
        const ExecState s = h.runner.runToCompletion();
        out << QStringLiteral("eHit[%1|%2|%3|%4|%5]")
                   .arg(h.storage.getGlobalInt1DInCurrentFrame(QStringLiteral("H_OUT"), 0))
                   .arg(stateName(s))
                   .arg(h.positions.join(QLatin1Char(',')))
                   .arg(h.table.firstGotoLabelPosition(QStringLiteral("h.ERB"), -1,
                                                       QStringLiteral("HEADER")))
                   .arg(h.table.firstGotoLabelPosition(QStringLiteral("h.ERB"), 4,
                                                       QStringLiteral("HEADER")));
    }
    {
        Harness h;
        h.loadAll(order);
        h.table.markLoadComplete();
        h.table.setEntryPoint(QStringLiteral("HEADERFN"));
        h.positions.clear();
        const ExecState s = h.runner.runToCompletion();
        out << QStringLiteral("eMiss[%1|%2|%3]")
                   .arg(stateName(s))
                   .arg(h.table.currentLine())
                   .arg(h.errors.isEmpty() ? QStringLiteral("<none>") : h.errors.first());
    }

    return out.join(QLatin1Char(' '));
}

static void testD() {
    qDebug() << "\n7) d) 装载顺序无关：正序 / 逆序 loadScript 的可观察值逐字相同";

    const QStringList forwardOrder = {QStringLiteral("a.ERB"), QStringLiteral("b.ERB"),
                                      QStringLiteral("h.ERB")};
    QStringList reversedOrder = forwardOrder;
    std::reverse(reversedOrder.begin(), reversedOrder.end());

    const QString forward = roundSummary(forwardOrder);
    const QString backward = roundSummary(reversedOrder);

    // 先钉住正序那一轮的具体值，避免「两轮一致地错」也算通过。
    checkStr(forward,
             QStringLiteral("a[10|0|20|0|Halt|Halt|a.ERB:1,a.ERB:3,a.ERB:4,a.ERB:5|"
                            "a.ERB:7,a.ERB:9,a.ERB:10,a.ERB:11] aApi[3|9] "
                            "b[1|0|Halt|3|b.ERB:1,b.ERB:3,b.ERB:4,b.ERB:5,b.ERB:8,b.ERB:9|2] "
                            "eHit[7|Halt|h.ERB:1,h.ERB:2,h.ERB:3|1|-1] "
                            "eMiss[Error|5|GOTO label not found: HEADER]"),
             "正序装载：a/b/e 的全部断言值");
    checkStr(backward, forward, "逆序装载 -> 完全相同（与 QHash / 装载序无关）");
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    qDebug() << "GOTO label lookup (one index query)";
    qDebug() << "====================================";

    testFirstGotoLabelUnit();
    testTableAndExecution();
    testSameNameAcrossScripts();
    testA();
    testB();
    testC();
    testE();
    testD();

    if (g_failures == 0) qDebug() << "\nALL PASSED";
    else qDebug() << "\nFAILURES:" << g_failures;
    return g_failures == 0 ? 0 : 1;
}