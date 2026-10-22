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
// test_segment_scope.cpp —— 私有变量作用域身份 = **段身份 (脚本, 段起点行)**
//
// 之前：私有域的唯一键是**函数名**。同名 @F 的两份声明（口上补丁覆盖、同一
// 脚本里重复定义、两个脚本各定义一次）共用一份私有存储 —— 第二份的
// `#DIM X` 与第一份是同一个变量，第二份读得到第一份写进去的值。
//
// 现在：唯一键是段身份 = (脚本名, 段起点行)，段起点行 = 该段 @ 标签在预处理后
// 行序里的下标（文件头段 = -1）。同名 @F 的两份是两个不同的段 id、两个不同的
// 存储槽、两个不同的写盘名。
//
// 本文件锁定（每条夹具的注释里都写明「旧实现读到什么」）：
//   1. 同名 @F 各声明 `#DIM X`：各段各读各的（同脚本两段 + 两个脚本各一段）；
//   2. 只有第二段声明 `#DIM Y`：第一段读到的是**全局同名** Y，不是第二段的私有 Y；
//   3. 私有 `#DIM CONST` 5 / 7：常数表按段身份分桶，各段读自己的；
//   4. 私有 `#DIMS` 与私有 `#DIM A, 3`：字符串/数组也互不串值；
//   5. 同段内递归共享同一个 X，跳到另一个同名段读不到；
//   6. 存档往返：save -> load -> save 字节逐字节相同，写盘名可还原成
//      (脚本, 段起点行)（eravar::splitPrivateStorageKey + parseSegmentKey）；
//   7. 装载期自检：所有 Local 声明的 scopeKey 非空、可还原、同名两段不同、
//      不同名函数互不相同；与 EraParseTable::segmentKeyOfLabel 交叉核对；
//   8. 文件头段（段起点行 = -1）的段身份独立存在，不会退化成空串（= 广域）；
//   9. （发现真缺陷后补的红断言）系统入口调用（SystemStateMachine::
//      callLabelRef）必须按**段身份**应用私有初值 `#DIM X = 7`。
//
// 测试不做任何名字 -> 段身份的映射（生产代码也没有）：期望值一律由
// eravar::segmentKeyText(脚本, 标签行) 现算 —— 那是段身份文本的唯一构造点。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QByteArray>
#include <QDebug>
#include <QHash>
#include <QSet>
#include <QStringList>

#include "ast/ast_builder.h"
#include "ast/expression_evaluator.h"
#include "ast/name_interner.h"
#include "ast/variable_table.h"
#include "era_binary_data.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "process_state.h"
#include "save_variable_map.h"
#include "script_runner.h"
#include "system_state_machine.h"
#include "variable_storage.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

namespace {

// 一台最小运行时：存储 / 状态 / 解析表 / 执行引擎 / 求值器 / 执行链。
// 接线方式与 test_private_scope_table.cpp 相同（生产代码的接线在 Application）。
struct Session {
    VariableStorage storage;
    ProcessState state;
    EraParseTable table{&state};
    ExecutionEngine engine{&storage, nullptr};
    ExpressionEvaluator evaluator;
    ScriptRunner runner{&table, &engine, &state, &storage};

    Session() {
        engine.setParseTable(&table);          // 顺带把 VariableTable 接到存储层（存档映射用）
        engine.setExpressionEvaluator(&evaluator);
        table.setExpressionEvaluator(&evaluator);
        table.setVariableStorage(&storage);
        runner.setExpressionEvaluator(&evaluator);
        runner.setStepLimit(200000);
    }

    bool load(const QString& scriptName, const QStringList& source, bool isHeaderFile = false) {
        QList<LogicalLine> lines;
        lines.reserve(source.size());
        for (int i = 0; i < source.size(); ++i) {
            lines.append(AstBuilder::build(source.at(i), ScriptPosition(scriptName, i, 0),
                [this](const QString& text) { return table.expressionAst(text); }));
        }
        return table.loadScript(scriptName, lines, isHeaderFile);
    }

    // 从某段的 @ 标签行直接执行（系统入口形态：没有调用帧，作用域取自行）。
    ExecState runSegment(const QString& scriptName, int labelLine) {
        table.setPosition(scriptName, labelLine, /*jumped=*/true);
        return runner.runToCompletion();
    }

    // 私有变量的存储键 = 唯一构造点；读用 Raw（不套用「当前作用域」，不会串到别的段）。
    QString privateKey(const QString& scriptName, int labelLine, const QString& var) const {
        return eravar::makePrivateStorageKey(eravar::segmentKeyText(scriptName, labelLine), var);
    }
    qint64 privateInt(const QString& scriptName, int labelLine, const QString& var, int index = 0) const {
        return storage.getGlobalInt1DRaw(privateKey(scriptName, labelLine, var), index);
    }
    QString privateStr(const QString& scriptName, int labelLine, const QString& var, int index = 0) const {
        return storage.getGlobalStr1DRaw(privateKey(scriptName, labelLine, var), index);
    }
    // 脚本写出来的结果（广域整型/字符串，名字大写 -> 裸键）。
    qint64 captured(const QString& name) const {
        return storage.getGlobalInt1DRaw(name.toUpper(), 0);
    }
    QString capturedStr(const QString& name) const {
        return storage.getGlobalStr1DRaw(name.toUpper(), 0);
    }
};

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    qDebug() << "私有作用域身份 = 段身份 (脚本, 段起点行)";

    // =====================================================================
    // 夹具 1~5、7、8、9 共用一台运行时：多脚本共存本身就要求「同一名字在
    // 不同段各有一份」，同一张表里能同时观察所有不变量。
    // =====================================================================
    Session s;
    qDebug() << "\n0) 装载";
    {
        // 文件头段（段起点行 = -1）的 .ERH；HV 在头文件里是广域（ERH 语义）。
        check(s.load(QStringLiteral("DEFS.ERH"),
                     {QStringLiteral("#DIM Y"),      // 夹具 2 的广域同名 Y
                      QStringLiteral("#DIMS AR_S"),   // 夹具 4 的结果槽（广域字符串）
                      QStringLiteral("#DIMS AR_S2")}, // 夹具 4 段 B 的结果槽
                     /*isHeaderFile=*/true),
              "loadScript DEFS.ERH (#DIM Y / #DIMS AR_S 广域)");
        check(s.load(QStringLiteral("HDR.ERH"), {
                  QStringLiteral("#DIM HV"),      // 0  文件头段（-1）
                  QStringLiteral("@HF"),          // 1  段起点 = 1
                  QStringLiteral("#DIM PV"),      // 2
                  QStringLiteral("RETURN")        // 3
              }, /*isHeaderFile=*/true), "loadScript HDR.ERH");

        // 夹具 1a：同一个脚本里两个同名 @DUPF，各自 #DIM X。
        check(s.load(QStringLiteral("DUP.ERB"), {
                  QStringLiteral("@DUPF"),        // 0  段 A（段起点 0）
                  QStringLiteral("#DIM X"),       // 1
                  QStringLiteral("X = 11"),       // 2
                  QStringLiteral("DUP_A = X"),    // 3
                  QStringLiteral("RETURN"),       // 4
                  QStringLiteral("@DUPF"),        // 5  段 B（段起点 5）
                  QStringLiteral("#DIM X"),       // 6
                  QStringLiteral("DUP_B = X"),    // 7  未赋值 -> 应是 0（旧实现 11）
                  QStringLiteral("RETURN")        // 8
              }), "loadScript DUP.ERB (同名 @DUPF 两份)");

        // 夹具 1b：两个脚本各一份同名 @D2。
        check(s.load(QStringLiteral("D2A.ERB"), {
                  QStringLiteral("@D2"),          // 0
                  QStringLiteral("#DIM X"),       // 1
                  QStringLiteral("X = 11"),       // 2
                  QStringLiteral("DUP_C = X"),    // 3
                  QStringLiteral("RETURN")        // 4
              }), "loadScript D2A.ERB (@D2)");
        check(s.load(QStringLiteral("D2B.ERB"), {
                  QStringLiteral("@D2"),          // 0
                  QStringLiteral("#DIM X"),       // 1
                  QStringLiteral("DUP_D = X"),    // 2  旧实现（作用域=函数名 D2）读到 11
                  QStringLiteral("RETURN")        // 3
              }), "loadScript D2B.ERB (@D2)");

        // 夹具 2：只有第二段声明 #DIM Y（Y 另有广域同名声明）。
        check(s.load(QStringLiteral("VIS.ERB"), {
                  QStringLiteral("@V1"),          // 0  段 A（无 Y 的私有声明）
                  QStringLiteral("VIS_FIRST = Y"),// 1  旧实现读到 22，新实现读广域 Y = 0
                  QStringLiteral("RETURN"),       // 2
                  QStringLiteral("@V1"),          // 3  段 B
                  QStringLiteral("#DIM Y"),       // 4
                  QStringLiteral("Y = 22"),       // 5
                  QStringLiteral("RETURN")        // 6
              }), "loadScript VIS.ERB (只有第二段声明 Y)");

        // 夹具 3：同名私有常数 5 / 7。
        check(s.load(QStringLiteral("CONST.ERB"), {
                  QStringLiteral("@C1"),              // 0
                  QStringLiteral("#DIM CONST IC = 5"),// 1
                  QStringLiteral("CON_A = IC"),       // 2
                  QStringLiteral("RETURN"),           // 3
                  QStringLiteral("@C1"),              // 4
                  QStringLiteral("#DIM CONST IC = 7"),// 5
                  QStringLiteral("CON_B = IC"),       // 6
                  QStringLiteral("RETURN")            // 7
              }), "loadScript CONST.ERB (私有 CONST 5 / 7)");

        // 夹具 4：私有字符串 + 私有整型数组。
        check(s.load(QStringLiteral("ARR.ERB"), {
                  QStringLiteral("@A1"),          // 0
                  QStringLiteral("#DIMS S"),      // 1
                  QStringLiteral("#DIM AR, 3"),   // 2
                  QStringLiteral("S '= \"one\""), // 3
                  QStringLiteral("AR:1 = 41"),    // 4
                  QStringLiteral("AR_S '= S"),    // 5  广域字符串 <= 私有字符串
                  QStringLiteral("AR_I = AR:1"),  // 6
                  QStringLiteral("RETURN"),       // 7
                  QStringLiteral("@A1"),          // 8
                  QStringLiteral("#DIMS S"),      // 9
                  QStringLiteral("#DIM AR, 3"),   // 10
                  QStringLiteral("AR_S2 '= S"),   // 11 旧实现读到 "one"
                  QStringLiteral("AR_I2 = AR:1"), // 12 旧实现读到 41
                  QStringLiteral("RETURN")        // 13
              }), "loadScript ARR.ERB (私有 #DIMS / #DIM A,3)");

        // 夹具 5：同段递归 + 跨段不可见。
        check(s.load(QStringLiteral("REC.ERB"), {
                  QStringLiteral("@REC"),         // 0  段 A
                  QStringLiteral("#DIM X"),       // 1
                  QStringLiteral("IF X == 0"),    // 2
                  QStringLiteral("X = 1"),        // 3
                  QStringLiteral("CALL REC"),     // 4  同段递归（第一份 @REC = 段 A）
                  QStringLiteral("REC_AFTER = X"),// 5  同段共享 -> 1
                  QStringLiteral("ENDIF"),        // 6
                  QStringLiteral("RETURN"),       // 7
                  QStringLiteral("@REC"),         // 8  段 B
                  QStringLiteral("#DIM X"),       // 9
                  QStringLiteral("REC_OTHER = X"),// 10 旧实现读到 1
                  QStringLiteral("RETURN")        // 11
              }), "loadScript REC.ERB (同名 @REC 两份)");

        // 夹具 9：系统入口调用的私有初值。
        check(s.load(QStringLiteral("SYS.ERB"), {
                  QStringLiteral("@EVENTFIRST"),     // 0
                  QStringLiteral("#DIM X = 7"),      // 1  每次进入都必须回到 7
                  QStringLiteral("X = X + 1"),       // 2
                  QStringLiteral("SYS_SEEN = X"),    // 3  期望 8（旧身份实现/缺陷实现 9）
                  QStringLiteral("RETURN")           // 4
              }), "loadScript SYS.ERB (#DIM X = 7)");

        s.table.setEntryPoint(QStringLiteral("DUPF"));
        s.table.markLoadComplete();
        check(s.table.isFinalized(), "markLoadComplete -> Finalized");
        check(s.storage.privateScopeTableInstalled(),
              "装载收尾把「段身份 -> 私有名表」装进了存储层");
    }

    // ---------------------------------------------------------------------
    qDebug() << "\n1) 同名 @F 的两段：#DIM X 各读各的";
    {
        // 段 A：X = 11；段 B 只读不写。旧实现（作用域=函数名 DUPF）两段共用
        // 一份 X -> DUP_B 读到 11。新实现两段两个槽 -> DUP_B = 0。
        check(s.runSegment(QStringLiteral("DUP.ERB"), 0) == ExecState::Halt,
              "1a 段 A 执行到 Halt");
        check(s.captured(QStringLiteral("DUP_A")) == 11, "1a 段 A 内读 X = 11");
        check(s.privateInt(QStringLiteral("DUP.ERB"), 0, QStringLiteral("X")) == 11,
              "1a 段 A 的 X 落在 (DUP.ERB,0) 的私有槽");
        check(s.captured(QStringLiteral("X")) == 0,
              "1a 私有 X 没有漏进广域裸名 X（广域 X 仍为 0）");

        check(s.runSegment(QStringLiteral("DUP.ERB"), 5) == ExecState::Halt,
              "1a 段 B 执行到 Halt");
        check(s.captured(QStringLiteral("DUP_B")) == 0,
              "1a 段 B 读 X = 0（旧实现 11：两段共用函数名的私有槽）");
        check(s.privateInt(QStringLiteral("DUP.ERB"), 5, QStringLiteral("X")) == 0,
              "1a 段 B 的 X 在 (DUP.ERB,5) 的私有槽，与段 A 的槽不同");
        check(s.privateKey(QStringLiteral("DUP.ERB"), 0, QStringLiteral("X"))
                  != s.privateKey(QStringLiteral("DUP.ERB"), 5, QStringLiteral("X")),
              "1a 两段的私有存储键不同");

        // 1b：两个脚本各一份 @D2。旧实现按函数名 intern -> 两边同一个作用域名 D2。
        check(s.runSegment(QStringLiteral("D2A.ERB"), 0) == ExecState::Halt,
              "1b D2A 执行到 Halt");
        check(s.captured(QStringLiteral("DUP_C")) == 11, "1b D2A 内读 X = 11");
        check(s.runSegment(QStringLiteral("D2B.ERB"), 0) == ExecState::Halt,
              "1b D2B 执行到 Halt");
        check(s.captured(QStringLiteral("DUP_D")) == 0,
              "1b 另一脚本的同名 @D2 读 X = 0（旧实现 11：跨脚本共用函数名作用域）");
        check(s.privateInt(QStringLiteral("D2A.ERB"), 0, QStringLiteral("X")) == 11
                  && s.privateInt(QStringLiteral("D2B.ERB"), 0, QStringLiteral("X")) == 0,
              "1b 两个脚本的 @D2 各有自己的 X 槽");
    }

    // ---------------------------------------------------------------------
    qDebug() << "\n2) 只有第二段声明 #DIM Y：第一段看到的是广域 Y";
    {
        // 声明可见性是**词法/按段**的：第一段的源码里没有 Y 的声明，段内就
        // 解析不到 Y 的私有域 -> 落到广域同名声明（DEFS.ERH 的 `#DIM Y`，值 0）。
        // 旧实现（作用域=函数名 V1）把两段的声明合并成一张名字表，于是第一段
        // 的 Y 被重映射成 V1 的私有槽 —— 读到第二段写进去的 22。
        const QString keyA = eravar::segmentKeyText(QStringLiteral("VIS.ERB"), 0);
        const QString keyB = eravar::segmentKeyText(QStringLiteral("VIS.ERB"), 3);
        const VariableTable& vt = s.table.variableTable();
        const VariableDecl* inA = vt.find(QStringLiteral("Y"), keyA);
        const VariableDecl* inB = vt.find(QStringLiteral("Y"), keyB);
        check(inA != nullptr && inA->scope == VarScope::Global && inA->scopeKey.isEmpty(),
              "2 段 A 里 Y 解析到广域声明（不是第二段的私有声明）");
        check(inB != nullptr && inB->scope == VarScope::Local && inB->scopeKey == keyB,
              "2 段 B 里 Y 解析到本段的私有声明");

        check(s.runSegment(QStringLiteral("VIS.ERB"), 0) == ExecState::Halt,
              "2 段 A 执行到 Halt");
        check(s.captured(QStringLiteral("VIS_FIRST")) == 0,
              "2 段 A 读 Y = 0（旧实现 22：第二段的私有值）");
        check(s.runSegment(QStringLiteral("VIS.ERB"), 3) == ExecState::Halt,
              "2 段 B 执行到 Halt");
        check(s.privateInt(QStringLiteral("VIS.ERB"), 3, QStringLiteral("Y")) == 22,
              "2 段 B 的 Y = 22 落在 (VIS.ERB,3) 的私有槽");
        check(s.captured(QStringLiteral("VIS_FIRST")) == 0 && s.captured(QStringLiteral("Y")) == 0,
              "2 第二段写私有 Y 不影响第一段与广域 Y");
    }

    // ---------------------------------------------------------------------
    qDebug() << "\n3) 私有 #DIM CONST 5 / 7：常数表按段身份分桶";
    {
        check(s.runSegment(QStringLiteral("CONST.ERB"), 0) == ExecState::Halt,
              "3 段 A 执行到 Halt");
        check(s.captured(QStringLiteral("CON_A")) == 5, "3 段 A 读到 IC = 5");
        check(s.runSegment(QStringLiteral("CONST.ERB"), 4) == ExecState::Halt,
              "3 段 B 执行到 Halt");
        check(s.captured(QStringLiteral("CON_B")) == 7,
              "3 段 B 读到 IC = 7（旧实现：两段同名 -> 同一个常数桶，两段都读到 5 或都读到 7）");
    }

    // ---------------------------------------------------------------------
    qDebug() << "\n4) 私有 #DIMS 与私有 #DIM A, 3：字符串与数组也互不串值";
    {
        check(s.runSegment(QStringLiteral("ARR.ERB"), 0) == ExecState::Halt,
              "4 段 A 执行到 Halt");
        check(s.capturedStr(QStringLiteral("AR_S")) == QStringLiteral("one"),
              "4 段 A 的私有字符串 S = \"one\"");
        check(s.captured(QStringLiteral("AR_I")) == 41, "4 段 A 的私有数组 AR:1 = 41");
        check(s.privateStr(QStringLiteral("ARR.ERB"), 0, QStringLiteral("S"))
                  == QStringLiteral("one"),
              "4 段 A 的 S 落在自己的私有槽");
        check(s.privateInt(QStringLiteral("ARR.ERB"), 0, QStringLiteral("AR"), 1) == 41,
              "4 段 A 的 AR:1 落在自己的私有槽");

        check(s.runSegment(QStringLiteral("ARR.ERB"), 8) == ExecState::Halt,
              "4 段 B 执行到 Halt");
        check(s.capturedStr(QStringLiteral("AR_S2")).isEmpty(),
              "4 段 B 的私有字符串 S = \"\"（旧实现 \"one\"）");
        check(s.captured(QStringLiteral("AR_I2")) == 0,
              "4 段 B 的私有数组 AR:1 = 0（旧实现 41）");
        check(s.privateStr(QStringLiteral("ARR.ERB"), 8, QStringLiteral("S")).isEmpty()
                  && s.privateInt(QStringLiteral("ARR.ERB"), 8, QStringLiteral("AR"), 1) == 0,
              "4 段 B 的 S / AR 与自己段的槽一致（不是段 A 的值）");
        check(s.privateStr(QStringLiteral("ARR.ERB"), 0, QStringLiteral("S"))
                  == QStringLiteral("one"),
              "4 段 B 的读/写没有覆盖段 A 的私有字符串");
    }

    // ---------------------------------------------------------------------
    qDebug() << "\n5) 同段递归共享，跨段互不可见";
    {
        // Emuera 的函数私有变量是「按函数（这里：按段）静态」的：同段递归
        // 看见的是同一份 X；换到另一段（另一个段身份）则是另一份。
        check(s.runSegment(QStringLiteral("REC.ERB"), 0) == ExecState::Halt,
              "5 段 A 执行到 Halt（递归没有失控）");
        check(s.captured(QStringLiteral("REC_AFTER")) == 1,
              "5 递归返回后同段读 X = 1（同段共享）");
        check(s.privateInt(QStringLiteral("REC.ERB"), 0, QStringLiteral("X")) == 1,
              "5 段 A 的 X 槽 = 1");
        check(s.runSegment(QStringLiteral("REC.ERB"), 8) == ExecState::Halt,
              "5 段 B 执行到 Halt");
        check(s.captured(QStringLiteral("REC_OTHER")) == 0,
              "5 同名另一段读 X = 0（旧实现 1）");
        check(s.privateInt(QStringLiteral("REC.ERB"), 8, QStringLiteral("X")) == 0,
              "5 段 B 的 X 槽与段 A 不同");
    }

    // ---------------------------------------------------------------------
    qDebug() << "\n6) 存档往返：写盘名可还原 + 字节确定性";
    {
        Session save;
        check(save.load(QStringLiteral("SAVE.ERB"), {
                  QStringLiteral("@SAVF"),                // 0
                  QStringLiteral("#DIM SAVEDATA SV, 2"),  // 1
                  QStringLiteral("SV:0 = 5"),             // 2
                  QStringLiteral("SV:1 = 6"),             // 3
                  QStringLiteral("RETURN"),               // 4
                  QStringLiteral("@SAVF"),                // 5  第二份同名段
                  QStringLiteral("#DIM SAVEDATA SV, 2"),  // 6
                  QStringLiteral("SV:0 = 7"),             // 7
                  QStringLiteral("SV:1 = 8"),             // 8
                  QStringLiteral("RETURN")                // 9
              }), "6 loadScript SAVE.ERB");
        save.table.setEntryPoint(QStringLiteral("SAVF"));
        save.table.markLoadComplete();
        check(save.runSegment(QStringLiteral("SAVE.ERB"), 0) == ExecState::Halt,
              "6 段 A 执行到 Halt");
        check(save.runSegment(QStringLiteral("SAVE.ERB"), 5) == ExecState::Halt,
              "6 段 B 执行到 Halt");
        check(save.privateInt(QStringLiteral("SAVE.ERB"), 0, QStringLiteral("SV"), 1) == 6
                  && save.privateInt(QStringLiteral("SAVE.ERB"), 5, QStringLiteral("SV"), 1) == 8,
              "6 两段的 SV:1 各是 6 / 8");

        const QList<LabelRef> decls = save.table.labels(QStringLiteral("SAVF"));
        check(decls.size() == 2 && decls.at(0).line == 0 && decls.at(1).line == 5,
              "6 同名 @SAVF 在标签索引里是 0 / 5 两份声明");

        const erasave::Map map = erasave::Map::build(save.storage.variableConfig(),
                                                     &save.table.variableTable(),
                                                     EraBinaryData::FileType::Normal);
        QString nameA, nameB;
        for (const erasave::Entry& e : map.entries()) {
            if (!eravar::isPrivateStorageKey(e.name)) continue;
            if (!e.name.endsWith(QLatin1String("\x1fSV"))) continue;
            const QString prefix = eravar::makePrivateStorageKey(
                eravar::segmentKeyText(QStringLiteral("SAVE.ERB"), 0), QStringLiteral("SV"));
            if (e.name == prefix) nameA = e.name;
            else nameB = e.name;
        }
        check(!nameA.isEmpty() && !nameB.isEmpty(), "6 两个段各有一条私有 SAVEDATA 写盘条目");
        check(nameA != nameB, "6 同名 @SAVF 两段的写盘名不同（旧实现同一个名字）");

        // 写盘名 = 段身份文本 + 0x1F + 名字；拆开后必须还原回 (脚本, 段起点行)，
        // 且段起点行与标签索引里的行号逐一对上（不新造任何字符串解析）。
        for (const QString& name : {nameA, nameB}) {
            QString scopeText, varName, script;
            int line = -2;
            eravar::splitPrivateStorageKey(name, scopeText, varName);
            const bool parsed = eravar::parseSegmentKey(scopeText, script, line);
            bool matchesLabel = false;
            for (const LabelRef& r : decls) matchesLabel = matchesLabel || r.line == line;
            check(varName == QStringLiteral("SV") && parsed
                      && script == QStringLiteral("SAVE.ERB") && matchesLabel,
                  QStringLiteral("6 写盘名 %1 还原为 (SAVE.ERB, %2) 且与标签行号一致")
                      .arg(scopeText.section(QLatin1Char(':'), 1, 1)).arg(line));
        }

        QByteArray bytes;
        const EraBinaryData::Document doc =
            save.storage.captureBinarySave(EraBinaryData::FileType::Normal);
        check(EraBinaryData::encode(doc, bytes), "6 存档编码成功");
        EraBinaryData::Document decoded;
        check(EraBinaryData::decode(bytes, decoded), "6 存档解码成功");
        bool sawA = false, sawB = false;
        for (const EraBinaryData::Variable& v : decoded.variables) {
            sawA = sawA || (v.name == nameA);
            sawB = sawB || (v.name == nameB);
        }
        check(sawA && sawB, "6 两条私有条目都以段身份写进了存档（不是裸名 SV）");

        VariableStorage restored;
        restored.initialize(10, 10);
        restored.setSaveVariableTable(&save.table.variableTable());
        restored.applyBinarySave(decoded);
        check(restored.getGlobalInt1DRaw(
                  eravar::makePrivateStorageKey(eravar::segmentKeyText(QStringLiteral("SAVE.ERB"), 0),
                                                QStringLiteral("SV")), 1) == 6,
              "6 读回后段 A 的 SV:1 = 6");
        check(restored.getGlobalInt1DRaw(
                  eravar::makePrivateStorageKey(eravar::segmentKeyText(QStringLiteral("SAVE.ERB"), 5),
                                                QStringLiteral("SV")), 1) == 8,
              "6 读回后段 B 的 SV:1 = 8（没有互相覆盖）");

        QByteArray again;
        const EraBinaryData::Document reread =
            restored.captureBinarySave(EraBinaryData::FileType::Normal);
        check(EraBinaryData::encode(reread, again), "6 回读后再存成功");
        check(again == bytes, "6 save -> load -> save 逐字节相同");
    }

    // ---------------------------------------------------------------------
    qDebug() << "\n7) 装载期自检：Local 声明的段身份";
    {
        const VariableTable& vt = s.table.variableTable();
        int locals = 0;
        int emptyKeys = 0;
        int unparsable = 0;
        QHash<QString, QSet<QString>> keysByFunction;
        for (const VariableDecl& d : vt.declarations()) {
            if (d.scope != VarScope::Local) continue;
            ++locals;
            if (d.scopeKey.isEmpty()) { ++emptyKeys; continue; }
            QString script;
            int line = -2;
            if (!eravar::parseSegmentKey(d.scopeKey, script, line)
                || s.table.script(script) == nullptr || line < 0) {
                ++unparsable;
            }
            keysByFunction[d.function.toUpper()].insert(d.scopeKey);
        }
        check(locals > 0, QStringLiteral("7 表里有 %1 条 Local 声明").arg(locals));
        check(emptyKeys == 0, "7 所有 Local 声明的 scopeKey 非空（空串 = 广域，会退化成裸名）");
        check(unparsable == 0, "7 所有 scopeKey 都能还原成 (存在的脚本, 非负段起点行)");

        // 同名 @F 的两份 = 两个段身份。
        const QStringList duplicated = {QStringLiteral("DUPF"), QStringLiteral("D2"),
                                        QStringLiteral("C1"), QStringLiteral("A1"),
                                        QStringLiteral("REC")};
        // V1 的分支不对称（第一段没有 Y 的声明），单独核对：只有一个段身份。
        check(keysByFunction.value(QStringLiteral("V1")).size() == 1
                  && keysByFunction.value(QStringLiteral("V1")).contains(
                         eravar::segmentKeyText(QStringLiteral("VIS.ERB"), 3)),
              "7 @V1 的私有声明只挂在声明了 Y 的那一段 (VIS.ERB,3) 上");
        for (const QString& fn : duplicated) {
            check(keysByFunction.value(fn).size() == 2,
                  QStringLiteral("7 同名 @%1 的声明分属 %2 个段身份（应为 2）")
                      .arg(fn).arg(keysByFunction.value(fn).size()));
        }
        // 不同名函数的段身份互不相同（不存在「相邻两段撞同一个键」）。
        QSet<QString> seen;
        int crossDuplicates = 0;
        for (auto it = keysByFunction.cbegin(); it != keysByFunction.cend(); ++it) {
            for (const QString& key : it.value()) {
                if (seen.contains(key)) ++crossDuplicates;
                else seen.insert(key);
            }
        }
        check(crossDuplicates == 0, "7 不同函数之间没有任何共享的段身份");

        // 只用测试/诊断的 segmentKeyOfLabel 交叉核对：名字 -> 第一份声明的段身份。
        const int dupFirst = s.table.getLabelPosition(QStringLiteral("DUP.ERB"), QStringLiteral("DUPF"));
        check(dupFirst == 0, "7 getLabelPosition(DUP.ERB, DUPF) = 第一份 @DUPF 的行 0");
        check(s.table.segmentKeyOfLabel(QStringLiteral("DUP.ERB"), QStringLiteral("DUPF"))
                  == eravar::segmentKeyText(QStringLiteral("DUP.ERB"), dupFirst),
              "7 segmentKeyOfLabel 与 (脚本, 标签行) 现算的段身份一致");
        check(keysByFunction.value(QStringLiteral("DUPF")).contains(
                  eravar::segmentKeyText(QStringLiteral("DUP.ERB"), 5)),
              "7 第二份 @DUPF 的声明挂在 (DUP.ERB,5) 的段身份上");
        // 解析线程两侧写同一字段：装载期自检必须为 0。
        check(s.table.segmentIdentityMismatches() == 0,
              "7 装载期「解析线程 vs loadScript」段身份自检 0 条不一致");
    }

    // ---------------------------------------------------------------------
    qDebug() << "\n8) 文件头段（段起点行 = -1）的身份独立";
    {
        const LogicalLine* headerLine = s.table.lineAt(QStringLiteral("HDR.ERH"), 0);
        const LogicalLine* labelLine  = s.table.lineAt(QStringLiteral("HDR.ERH"), 1);
        const QString expectedHeader = eravar::segmentKeyText(QStringLiteral("HDR.ERH"), -1);
        check(headerLine != nullptr && headerLine->scopeKey == expectedHeader,
              "8 文件头段的行带 (HDR.ERH,-1) 的段身份");
        check(headerLine != nullptr && !headerLine->scopeKey.isEmpty(),
              "8 文件头段的段身份非空（空串 = 无私有域 = 广域，那才是退化）");
        QString script;
        int line = -2;
        check(eravar::parseSegmentKey(expectedHeader, script, line)
                  && script == QStringLiteral("HDR.ERH") && line == -1,
              "8 (HDR.ERH,-1) 的段身份能还原回段起点行 -1");
        check(eravar::internName(expectedHeader) != eravar::kGlobalScope,
              "8 文件头段的 ScopeId 不是顶层（kGlobalScope）");
        check(eravar::internName(QString()) == eravar::kGlobalScope,
              "8 对照：只有空文本才是 kGlobalScope —— 文件头段不是它");
        check(labelLine != nullptr
                  && labelLine->scopeKey == eravar::segmentKeyText(QStringLiteral("HDR.ERH"), 1)
                  && labelLine->scopeKey != expectedHeader,
              "8 @HF 行属于它自己开启的新段，与文件头段是两个身份");
        check(s.table.segmentKeyOfLabel(QStringLiteral("HDR.ERH"), QStringLiteral("HF"))
                  == eravar::segmentKeyText(QStringLiteral("HDR.ERH"), 1),
              "8 segmentKeyOfLabel(HDR.ERH, HF) = (HDR.ERH,1)");

        // 文件头段没有私有域（.ERH 的 #DIM 按头文件语义是广域）——
        // 这是**声明归属**的规则，不是身份退化：身份仍然存在（上面 6 条）。
        check(s.table.variableTable().localsOf(expectedHeader).isEmpty(),
              "8 文件头段没有任何私有声明（.ERH 的 #DIM 是广域）");
        const VariableDecl* hv = s.table.variableTable().find(QStringLiteral("HV"), QString());
        check(hv != nullptr && hv->scope == VarScope::Global && hv->scopeKey.isEmpty(),
              "8 #DIM HV 按 .ERH 语义登记为广域（scopeKey 为空）");
        const VariableDecl* pv = s.table.variableTable().find(QStringLiteral("PV"), QString());
        check(pv != nullptr && pv->scope == VarScope::Global,
              "8 .ERH 里 @HF 下的 #DIM PV 同样是广域（头文件规则），不是段私有");

        // 逆函数必须真的校验格式，否则上面「能还原」等于没断言。
        QString badScript;
        int badLine = 0;
        check(!eravar::parseSegmentKey(QStringLiteral("HDR.ERH"), badScript, badLine),
              "8 非段身份格式的文本被 parseSegmentKey 拒绝");
    }

    // ---------------------------------------------------------------------
    qDebug() << "\n9) 系统入口调用按段身份应用私有初值";
    {
        // 缺陷护栏：SystemStateMachine::callLabelRef 曾把**函数名**传给
        // applyPrivateVariableDefaults(scopeKey) —— 段身份改造之后那个参数
        // 已经是段身份文本，函数名查不到任何私有声明 -> 私有初值静默不生效。
        // 症状：每次进入函数都该把 `#DIM X = 7` 写回，第二次进来却读到上次的 8。
        Session sys;
        check(sys.load(QStringLiteral("EVENTS.ERB"), {
                  QStringLiteral("@EVENTFIRST"),  // 0
                  QStringLiteral("#DIM X = 7"),   // 1
                  QStringLiteral("X = X + 1"),    // 2
                  QStringLiteral("SYS_SEEN = X"), // 3  期望 8，缺陷实现第二次是 9
                  QStringLiteral("BEGIN SHOP"),   // 4
                  QStringLiteral("RETURN"),       // 5
                  QStringLiteral("@SHOW_SHOP"),   // 6
                  QStringLiteral("ONEINPUT")      // 7  停在这里
              }), "9 loadScript EVENTS.ERB");
        sys.table.setEntryPoint(QStringLiteral("EVENTFIRST"));
        sys.table.markLoadComplete();

        SystemStateMachine machine(&sys.state, &sys.table);
        machine.setVariableStorage(&sys.storage);
        machine.setScriptRunner(&sys.runner);
        sys.runner.setSystemStateMachine(&machine);

        sys.state.setSystemState(SystemStateCode::First_Begin);
        check(machine.run() == ExecState::WaitInput,
              "9 第一次 BEGIN FIRST 跑到 @SHOW_SHOP 的 ONEINPUT");
        check(sys.captured(QStringLiteral("SYS_SEEN")) == 8,
              QStringLiteral("9 第一次进入：私有初值 7 + 1 = 8（读到 %1；缺陷实现 1：初值从未生效）")
                  .arg(sys.captured(QStringLiteral("SYS_SEEN"))));

        // 第二次系统入口调用：状态机再次从 First_Begin 起（run() 会复位位置）。
        sys.state.setSystemState(SystemStateCode::First_Begin);
        sys.state.setExecState(ExecState::Continue);
        check(machine.run() == ExecState::WaitInput,
              "9 第二次 BEGIN FIRST 又跑到 @SHOW_SHOP 的 ONEINPUT");
        check(sys.captured(QStringLiteral("SYS_SEEN")) == 8,
              QStringLiteral("9 第二次进入：私有初值重新生效（读到 %1；期望 8）")
                  .arg(sys.captured(QStringLiteral("SYS_SEEN"))));
    }

    qDebug() << "\n================================";
    if (g_failures == 0) qDebug() << "ALL PASS";
    else qDebug() << g_failures << "FAILURE(S)";
    return g_failures == 0 ? 0 : 1;
}
