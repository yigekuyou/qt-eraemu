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
// test_extension_registry.cpp —— 扩展注册类（ExtensionRegistry）单测
//
// 对应扩展机制重构（复杂度由注册类承担，扩展只调注册类的简单函数）：
//   1. fail-fast：核心名（kBuiltinFunctions）拒绝注册 —— 扩展不得覆盖核心
//   2. first-wins：同名重复注册拒绝（后注册者不生效）
//   3. 桩：只登记名字 -> 「留痕一次 + 跳过」（对齐 EE 的容错语义）
//   4. C++ 重载：reg(name) / reg(name, 实现) / regForm(name) 同名不同参数
//   5. 注册类插入 AST：regForm 声明 -> AstBuilder 按格式串解析实参
//      （PUTFORM 声明由注册类构造时登记；ast_builder 零函数名）
//   6. 引擎集成：EE 扩展默认全启用（注册类构造时装入，EE 头只在注册类里
//      被实现 —— 本测试不直接放置 ee_extension.h），扩展语句（普通语句形态）
//      在 executeInstruction 尾部真正分发
//   7. PUTFORM 执行语义：StrForm 实参 -> SAVEDATA_TEXT 累加
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QString>
#include <QStringList>

#include "ast/logical_line.h"
#include "ast/ast_builder.h"
#include "process_state.h"
#include "era_parse_table.h"
#include "variable_storage.h"
#include "execution_engine.h"
#include "extension_registry.h"

static int g_failed = 0;

static void check(bool ok, const QString& what) {
    if (ok) qDebug() << "  [PASS]" << what;
    else { ++g_failed; qDebug() << "  [FAIL]" << what; }
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "ExtensionRegistry test";
    qDebug() << "======================";

    // ---- 1) fail-fast：核心名拒绝注册（Xorg: 核心段保留）----
    // 注：SPLIT 的实参形态特殊（第3引数是裸数组变量），分发已放回注册类
    // （构造函数 reg 带实现）；kBuiltinFunctions 表里的名字由 fail-fast
    // 保护 —— 扩展不得覆盖。
    qDebug() << "\n1) fail-fast 拒绝核心名";
    {
        ExtensionRegistry ext;
        ext.reg(QStringLiteral("PUTFORM"));
        check(!ext.hasStatement("PUTFORM"), "核心名 PUTFORM 被拒绝（isBuiltinFunction）");
        ext.reg(QStringLiteral("FIND_CHARADATA"),
                [](const LogicalLine&, const QList<Operand>&) { return true; });
        check(!ext.hasStatement("FIND_CHARADATA"), "核心名 FIND_CHARADATA 被拒绝（带实现同样拒）");
    }

    // ---- 2) first-wins：同名重复注册拒绝 ----
    qDebug() << "\n2) first-wins";
    {
        ExtensionRegistry ext;
        int calls = 0;
        ext.reg(QStringLiteral("MYEXT"),
                [&calls](const LogicalLine&, const QList<Operand>&) { ++calls; return true; });
        ext.reg(QStringLiteral("MYEXT"));   // 重复 -> 拒绝（first-wins）
        check(ext.hasStatement("MYEXT"), "首个注册生效");
        LogicalLine line;
        line.functionName = "MYEXT";
        check(ext.runStatement("MYEXT", line), "runStatement 命中实现");
        check(calls == 1, "重复注册不覆盖（calls == 1）");
        check(!ext.runStatement("UNKNOWN", line), "未命中返回 false（调用方落通用路径）");
    }

    // ---- 3) 桩：只登记名字 -> 留痕一次 + 跳过 ----
    qDebug() << "\n3) 桩（reg 只登记名字）";
    {
        ExtensionRegistry ext;
        ext.reg(QStringLiteral("MYSTUB"));
        LogicalLine line;
        line.functionName = "MYSTUB";
        line.position = ScriptPosition("t.ERB", 1, 1);
        check(ext.runStatement("MYSTUB", line), "桩：跳过并 return true");
        check(ext.runStatement("MYSTUB", line), "桩：第二次同样跳过（留痕去重）");
    }

    // ---- 4) C++ 重载：reg(name) / reg(name, fn) / regForm(name) ----
    qDebug() << "\n4) 函数重载（同名不同参数）";
    {
        ExtensionRegistry ext;
        ext.reg(QStringLiteral("OVL1"));
        ext.reg(QStringLiteral("OVL2"),
                [](const LogicalLine&, const QList<Operand>&) { return true; });
        check(ext.hasStatement("OVL1") && ext.hasStatement("OVL2"),
              "reg(name) 与 reg(name, fn) 两个重载都能登记");
        ext.regForm(QStringLiteral("OVLFORM"));
        check(AstBuilder::isFormArgFunction("OVLFORM"), "regForm 插入 AST（isFormArgFunction）");
    }

    // ---- 5) 注册类插入 AST：PUTFORM 声明 + AstBuilder 按格式串解析 ----
    qDebug() << "\n5) 注册类插入 AST（regForm 声明）";
    {
        check(AstBuilder::isFormArgFunction("PUTFORM"),
              "PUTFORM 的 StrForm 实参形态由注册类登记（构造时 regForm）");
        ProcessState state;
        EraParseTable table(&state);
        VariableStorage storage;
        table.setVariableStorage(&storage);
        const AstResolver resolve = [&table](const QString& e) {
            return table.expressionAst(e);
        };
        const LogicalLine line = AstBuilder::build(QStringLiteral("PUTFORM {1 + 2}日目"),
                                                   ScriptPosition("t.ERB", 1, 1), resolve);
        check(line.isFunctionCall, "PUTFORM 被定型为函数调用（形态声明来自注册类）");
        check(!line.arguments.isEmpty() && !line.arguments.first().ast.isNull(),
              "PUTFORM 实参按 StrForm 解析成 AST");
    }

    // ---- 6) 引擎集成：EE 扩展默认全启用 + 扩展语句真分发 ----
    qDebug() << "\n6) 引擎集成（默认全启用，无清单文件）";
    {
        // EE 头只在注册类里被实现：构造 ExtensionRegistry 即装入 EE 扩展
        // （本测试不 include ee_extension.h —— 验证的正是这个性质）
        ExtensionRegistry fresh;
        check(fresh.hasStatement("CHKVARDATA")
                  && fresh.hasStatement("CHKGLOBALDATA")
                  && fresh.hasStatement("FIND_VARDATA"),
              "注册类构造即装入 EE 扩展（EE 头只在注册类里被实现）");

        VariableStorage storage;
        ExecutionEngine engine(&storage, nullptr);   // 构造 -> 注册类装入 EE 扩展
        LogicalLine line;
        line.functionName = "CHKVARDATA";
        check(engine.executeInstruction(line), "引擎：CHKVARDATA 扩展语句分发（桩命中）");
        line.functionName = "FIND_VARDATA";
        check(engine.executeInstruction(line), "引擎：FIND_VARDATA 扩展语句分发（桩命中）");

        // SPLIT（核心实现，引擎专用分支分发）：第3引数是裸数组变量，
        // 指令分发处命中核心分支 -> handleSplit
        // （实参用空 resolver：PARTS 无 AST，handleSplit 按 raw 取裸变量名）
        const AstResolver noResolve = nullptr;
        const LogicalLine splitLine = AstBuilder::build(
            QStringLiteral("SPLIT \"a,b,c\", \",\", PARTS"),
            ScriptPosition("t.ERB", 3, 1), noResolve);
        check(engine.executeInstruction(splitLine), "引擎：SPLIT 语句分发（核心分支命中）");
        check(storage.getGlobalStr1D(QStringLiteral("PARTS"), 0) == QStringLiteral("a"),
              "SPLIT 第 1 段写入 PARTS:0");
        check(storage.getSystemVariable(QStringLiteral("RESULT"), 0) == 3,
              "SPLIT 段数写 RESULT == 3");
    }

    // ---- 7) PUTFORM 执行语义：StrForm 实参 -> SAVEDATA_TEXT 累加 ----
    qDebug() << "\n7) PUTFORM 累加（StrForm 展开）";
    {
        VariableStorage storage;
        ExecutionEngine engine(&storage, nullptr);
        ProcessState state;
        EraParseTable table(&state);
        table.setVariableStorage(&storage);
        const AstResolver resolve = [&table](const QString& e) {
            return table.expressionAst(e);
        };
        const LogicalLine line1 = AstBuilder::build(QStringLiteral("PUTFORM {1 + 2}日目"),
                                                    ScriptPosition("t.ERB", 1, 1), resolve);
        const LogicalLine line2 = AstBuilder::build(QStringLiteral("PUTFORM 终"),
                                                    ScriptPosition("t.ERB", 2, 1), resolve);
        check(engine.executeInstruction(line1), "PUTFORM 第 1 次执行");
        check(engine.executeInstruction(line2), "PUTFORM 第 2 次执行");
        check(storage.getSystemStr(QStringLiteral("SAVEDATA_TEXT"), 0)
                  == QStringLiteral("3日目终"),
              "SAVEDATA_TEXT 累加 == 3日目终");
    }

    qDebug();
    if (g_failed == 0) { qDebug() << "ALL PASS"; return 0; }
    qDebug() << g_failed << "check(s) failed";
    return 1;
}
