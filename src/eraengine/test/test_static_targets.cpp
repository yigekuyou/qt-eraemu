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
// 装载期「运行期跳转/调用目标」静态解析（对齐 C# ErbLoader.setJumpTo）。
//
// 目的：把「运行到那行才炸」的错误提前到装载期。口径与 C# 一致：
//   * 只对**常量目标**下结论（FORM 名 / %…% / [[Rename]] 一律跳过）
//   * TRY* 系（TRYCALL/TRYJUMP/…）语义就是「找不到就跳过」，不报
//   * CALLEVENT 目标缺失静默（引擎事件调用），不报
//   * CALL 走全库标签表，GOTO 走**本函数内**的 $标签
// 以及标签解析的空白容忍：`$ GOOUT` / `@ FOO` 的名字不含前导空白
// （C# 只 ShiftNext 掉符号，名字由 LexicalAnalyzer 读，它会跳空白）。
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "ast/ast_builder.h"
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

// 装载一组脚本并收尾，返回结构化诊断。
static ParseDiagnostics loadScripts(const QList<QPair<QString, QString>>& scripts) {
    ProcessState state;
    EraParseTable table(&state);
    for (const auto& script : scripts) {
        const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
        const QStringList source = script.second.split(QLatin1Char('\n'));
        QList<LogicalLine> lines;
        for (int i = 0; i < source.size(); ++i)
            lines.append(AstBuilder::build(source.at(i), ScriptPosition(script.first, i + 1, 1), resolve));
        table.loadScript(script.first, lines);
    }
    table.markLoadComplete();
    return table.parseDiagnostics();
}

// 同 loadScripts，但接上 VariableStorage —— 数组边界检查要查 VariableSize
// 配置（默认表随 storage 构造就绪）。
static ParseDiagnostics loadScriptsWithStorage(
        const QList<QPair<QString, QString>>& scripts, VariableStorage* storage) {
    ProcessState state;
    EraParseTable table(&state);
    table.setVariableStorage(storage);
    for (const auto& script : scripts) {
        const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
        const QStringList source = script.second.split(QLatin1Char('\n'));
        QList<LogicalLine> lines;
        for (int i = 0; i < source.size(); ++i)
            lines.append(AstBuilder::build(source.at(i), ScriptPosition(script.first, i + 1, 1), resolve));
        table.loadScript(script.first, lines);
    }
    table.markLoadComplete();
    return table.parseDiagnostics();
}

// 同上，但在收尾前先对表做配置（例如开启未被调用函数告警）。
static ParseDiagnostics loadScriptsWith(const QList<QPair<QString, QString>>& scripts,
                                       const std::function<void(EraParseTable&)>& configure) {
    ProcessState state;
    EraParseTable table(&state);
    for (const auto& script : scripts) {
        const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
        const QStringList source = script.second.split(QLatin1Char('\n'));
        QList<LogicalLine> lines;
        for (int i = 0; i < source.size(); ++i)
            lines.append(AstBuilder::build(source.at(i), ScriptPosition(script.first, i + 1, 1), resolve));
        table.loadScript(script.first, lines);
    }
    configure(table);
    table.markLoadComplete();
    return table.parseDiagnostics();
}

static QList<ParseDiagnostic> withCode(const ParseDiagnostics& diagnostics, const QString& code) {
    QList<ParseDiagnostic> out;
    for (const ParseDiagnostic& d : diagnostics.all())
        if (d.code == code) out.append(d);
    return out;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    qDebug() << "Static jump/call target resolution test";
    qDebug() << "======================================";

    qDebug() << "\n1) 标签名允许 '@'/'$' 后的前导空白（eraTW: `$ GOOUT`）";
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nGOTO GOOUT\n$ GOOUT\nRETURN\n")}});
        check(withCode(d, DiagCode::kLabelNotFound).isEmpty(),
              "`$ GOOUT` 登记为 GOOUT，`GOTO GOOUT` 可解析");
        const ParseDiagnostics f = loadScripts({
            {QStringLiteral("b.ERB"), QStringLiteral("@ FOO\nGOTO BAR\n$ BAR\nRETURN\n")}});
        check(withCode(f, DiagCode::kLabelNotFound).isEmpty(), "`@ FOO` 的名字是 FOO（无前导空白）");
    }

    qDebug() << "\n2) CALL：常量目标必须存在（C# IsError）";
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"), QStringLiteral("@MAIN\nCALL MISSING_FN\nRETURN\n")}});
        const auto hits = withCode(d, DiagCode::kLabelNotFound);
        check(hits.size() == 1, QString("CALL 目标缺失 -> 1 条诊断（得到 %1）").arg(hits.size()));
        if (!hits.isEmpty()) {
            check(hits.first().severity == DiagSeverity::Error, "严重度为 error");
            check(hits.first().line == 2, "定位到第 2 行");
            check(hits.first().message.contains(QStringLiteral("@MISSING_FN")), "消息含目标名");
        }
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"), QStringLiteral("@MAIN\nCALL LATER\nRETURN\n")},
            {QStringLiteral("b.ERB"), QStringLiteral("@LATER\nRETURN\n")}});
        check(withCode(d, DiagCode::kLabelNotFound).isEmpty(), "跨文件定义的 CALL 目标不报");
    }

    qDebug() << "\n3) TRY* 系：目标缺失静默（运行期跳过）";
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nTRYCALL MISSING_FN\nTRYJUMP MISSING_FN\n"
                            "TRYCALLFORM MISSING_%X%\nRETURN\n")}});
        check(withCode(d, DiagCode::kLabelNotFound).isEmpty(), "TRYCALL/TRYJUMP/TRYCALLFORM 均不报");
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nCALLFORM NAME_%ABL%_MENU\nJUMPFORM TAG_%X%\nRETURN\n")}});
        check(withCode(d, DiagCode::kLabelNotFound).isEmpty(), "FORM 动态名（含 %）不报");
    }

    qDebug() << "\n4) GOTO：常量 $标签必须在**本函数内**";
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"), QStringLiteral("@MAIN\nGOTO NOWHERE\nRETURN\n")}});
        const auto hits = withCode(d, DiagCode::kLabelNotFound);
        check(hits.size() == 1, "GOTO 目标缺失 -> 报（当前函数内不存在）");
        if (!hits.isEmpty())
            check(hits.first().message.contains(QStringLiteral("$NOWHERE")), "消息含 $标签名");
    }
    {
        // 另一个函数里有同名 $标签：本函数内没有 -> 仍要报（C# GetLabelDollar 语义）
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nGOTO LOOP\nRETURN\n@OTHER\n$ LOOP\nRETURN\n")}});
        check(withCode(d, DiagCode::kLabelNotFound).size() == 1,
              "别的函数里的同名 $标签不算数");
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nGOTO LOOP\n$ LOOP\nRETURN\n@OTHER\nGOTO LOOP\n$ LOOP\nRETURN\n")}});
        check(withCode(d, DiagCode::kLabelNotFound).isEmpty(),
              "两个函数各自的同名 $标签各自解析");
    }

    qDebug() << "\n5) CALLEVENT / BEGIN：C# 不做目标检查（缺失静默）";
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nCALLEVENT EVENTNOBODY\nRETURN\n")}});
        check(withCode(d, DiagCode::kLabelNotFound).isEmpty(), "CALLEVENT 目标缺失不报");
    }

    qDebug() << "\n6) CALL 语句实参校验（对齐 C# ConvertArg 在 setJumpTo 里做的那次）";
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nCALL T(1, 2)\nRETURN\n@T(ARG)\nRETURN\n")}});
        const auto hits = withCode(d, DiagCode::kArgCheck);
        check(hits.size() == 1 && hits.first().message.contains(QStringLiteral("实参过多")),
              "CALL 实参多于形参 -> 报（运行到该行会失败）");
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nCALL T(\"s\")\nRETURN\n@T(ARG)\nRETURN\n")}});
        const auto hits = withCode(d, DiagCode::kArgCheck);
        check(hits.size() == 1 && hits.first().message.contains(QStringLiteral("实得字符串")),
              "字符串实参 -> 整型形参 -> 报");
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nCALL T(1)\nRETURN\n@T(ARG)\nRETURN\n")}});
        check(withCode(d, DiagCode::kArgCheck).isEmpty(), "个数/类型都对 -> 不报");
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nCALL T\nRETURN\n@T\nRETURN\n")}});
        check(withCode(d, DiagCode::kArgCheck).isEmpty(), "无参调用无参函数 -> 不报");
    }
    {
        // REF 形参：省略 / 传字面量都要报（编译期就能确定运行必炸）
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nCALL R()\nCALL R(1)\nRETURN\n@R(A)\n#DIM REF A, 3\nRETURN\n")}});
        const auto hits = withCode(d, DiagCode::kArgCheck);
        check(hits.size() == 2, "REF 形参被省略/传字面量 -> 各报一条");
    }
    {
        // 目标带 %…%（运行期才知道名字）：C# 不判定，这里也不报
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nCALL T_%X%(1)\nRETURN\n@T_A(ARG)\nRETURN\n")}});
        check(withCode(d, DiagCode::kArgCheck).isEmpty(), "FORM 目标不判定实参");
    }

    qDebug() << "\n7) 结构/配对检查（对齐 C# nestCheck 的诊断部分）";
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"), QStringLiteral("@MAIN\nIF 1\nPRINTL a\nRETURN\n")}});
        const auto hits = withCode(d, DiagCode::kStructure);
        check(hits.size() == 1 && hits.first().message.contains(QStringLiteral("ENDIF"))
              && hits.first().message.contains(QStringLiteral("見つかりません")),
              "IF 没有 ENDIF -> 报");
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"), QStringLiteral("@MAIN\nENDIF\nRETURN\n")}});
        check(withCode(d, DiagCode::kStructure).size() == 1, "孤立 ENDIF -> 报");
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nIF 1\nELSE\nENDIF\nRETURN\n")}});
        check(withCode(d, DiagCode::kStructure).isEmpty(), "配平的 IF/ELSE/ENDIF -> 不报");
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"), QStringLiteral("@MAIN\nBREAK\nRETURN\n")}});
        check(withCode(d, DiagCode::kStructure).size() == 1, "循环外 BREAK -> 报");
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nREPEAT 3\nBREAK\nREND\nRETURN\n")}});
        check(withCode(d, DiagCode::kStructure).isEmpty(), "循环内 BREAK -> 不报");
    }
    {
        // PRINTDATA 里可以连续放多个 DATALIST…ENDLIST（eraTW 的 CASINO.ERB）
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nPRINTDATAL\nDATALIST\nDATAFORM x\nENDLIST\n"
                            "DATALIST\nDATAFORM y\nENDLIST\nENDDATA\nRETURN\n")}});
        check(withCode(d, DiagCode::kStructure).isEmpty(), "PRINTDATA 内多个 DATALIST -> 不报");
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"), QStringLiteral("@MAIN\nSIF 1\nSIF 1\nPRINTL a\nRETURN\n")}});
        check(withCode(d, DiagCode::kStructure).size() == 1, "SIF 的次行又是 SIF -> 报");
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"), QStringLiteral("@MAIN\nRETURNF 1\nRETURN\n")}});
        check(withCode(d, DiagCode::kStructure).size() == 1, "#FUNCTION 之外 RETURNF -> 报");
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@F\n#FUNCTIONS\nRETURNF 1\n@MAIN\nPRINTV F()\nRETURN\n")}});
        const auto hits = withCode(d, DiagCode::kStructure);
        check(hits.size() == 1 && hits.first().message.contains(QStringLiteral("数値型")),
              "#FUNCTIONS 返回整型 -> 报类型不符");
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nPRINTDATAL\n$ POINT\nDATAFORM x\nENDDATA\nRETURN\n")}});
        check(withCode(d, DiagCode::kStructure).size() == 1, "PRINTDATA 内定义 $标签 -> 报");
    }

    qDebug() << "\n8) 可静态判定的运行期错误（字面量除零）";
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nPRINTV 10 / 0\nPRINTV 10 % 0\nRETURN\n")}});
        check(withCode(d, DiagCode::kStaticValue).size() == 2, "字面量除零 / 取模零 -> 各报一条");
    }
    {
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\n#DIM Y\nY = 2\nPRINTV 10 / Y\nRETURN\n")}});
        check(withCode(d, DiagCode::kStaticValue).isEmpty(), "除数是变量 -> 不判定");
    }

    qDebug() << "\n9) 未被调用的函数（默认关闭；开启后按 C# 判据）";
    {
        // 默认关闭：什么都不报
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@MAIN\nRETURN\n@LONELY\nRETURN\n")}});
        check(withCode(d, DiagCode::kNotCalled).isEmpty(), "默认（对齐 C# IGNORE）不报");
    }
    {
        const ParseDiagnostics d = loadScriptsWith({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@SYSTEM_TITLE\nCALL USED\nRETURN\n@USED\nRETURN\n@LONELY\nRETURN\n")}},
            [](EraParseTable& t) { t.setFunctionNotCalledWarning(true); });
        const auto hits = withCode(d, DiagCode::kNotCalled);
        check(hits.size() == 1 && hits.first().message.contains(QStringLiteral("LONELY")),
              "开启后：没人调用的 @LONELY 报一条");
        check(!hits.first().message.contains(QStringLiteral("@USED")), "被 CALL 的函数不报");
    }
    {
        // CALLFORM 出现 -> 名字运行期才知道 -> 整体放弃（与 C# useCallForm 一致）
        const ParseDiagnostics d = loadScriptsWith({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@SYSTEM_TITLE\nCALLFORM USED\nRETURN\n@USED\nRETURN\n@LONELY\nRETURN\n")}},
            [](EraParseTable& t) { t.setFunctionNotCalledWarning(true); });
        check(withCode(d, DiagCode::kNotCalled).isEmpty(), "有 CALLFORM -> 整体放弃该判定");
    }
    {
        // 作用域：只作**别的函数**私有声明的名字，在本函数里解析不到。
        // 对齐 C# IdentifierDictionary.ThrowException（privateDimList ->
        // IdentifierNotFoundCodeEE「変数"X"はこの関数中では定義されていません」）。
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"),
             QStringLiteral("@HOLDER\n"          // 1
                            "#DIM CONST SHADOW = 51\n"   // 2 HOLDER 的私有常量
                            "RETURN\n"          // 3
                            "@USER\n"           // 4
                            "CHECK = SHADOW\n"  // 5 别的函数 -> 报
                            "RETURN\n"          // 6
                            "@OWNER\n"          // 7
                            "#DIM CONST LOCAL_C = 7\n"   // 8
                            "CHECK = LOCAL_C\n"  // 9 自己的 -> 不报
                            "RETURN\n"          // 10
                            "@SKIPPER\n"        // 11
                            "GOTO DONE\n"       // 12 $标签不是变量 -> 不报
                            "$DONE\n"           // 13
                            "RETURN\n"          // 14
                            // 15-：原始文本实参的语句（ArgKind::Raw）里裸标识符是
                            // **字面字符串**（C# STR_ArgumentBuilder），不做变量解析
                            // —— 即使同名私有声明存在也不该报（eraMegaten 的
                            // `ALIGNMENT LEFT`/`SETCOLORBYNAME Red` 共 362 处）。
                            "@PIC\n"            // 15
                            "#DIMS LEFT\n"      // 16 让 LEFT/Red 成为「别的函数的私有名」
                            "#DIMS Red\n"       // 17
                            "RETURN\n"          // 18
                            "@DRAWER\n"         // 19
                            "ALIGNMENT LEFT\n"  // 20 关键字，不是变量 -> 不报
                            "SETCOLORBYNAME Red\n"  // 21 颜色名 -> 不报
                            "RETURN\n")},       // 22
        });
        const auto hits = withCode(d, DiagCode::kIdentifierScope);
        check(hits.size() == 1,
              QString("跨函数引用别的函数的私有常量 -> 报一条（得到 %1）").arg(hits.size()));
        check(!hits.isEmpty() && hits.first().message.contains(QStringLiteral("SHADOW")),
              "诊断指向变量 SHADOW");
        check(!hits.isEmpty() && hits.first().line == 5,
              QString("诊断落在使用行（得到 %1，期望 5）")
                  .arg(hits.isEmpty() ? -1 : hits.first().line));
    }

    // ----------------------------------------------------------------------
    // 数组边界条件静态检查（第七轮：把「运行到才知道」的静默失败前移到装载期）。
    // 运行期对这些形态一律宽容（读 0 / 写忽略），不跑游戏完全发现不了；
    // C# 侧 ConstantData.cs 对常量下标是 ParserMediator.Warn + 运行期 CodeEE。
    // ----------------------------------------------------------------------
    qDebug() << "\n11) 数组边界：负常量下标（-1 是 UnaryOp(MINUS,Literal)，非折叠字面量）";
    {
        VariableStorage storage;
        const ParseDiagnostics d = loadScriptsWithStorage({
            {QStringLiteral("a.ERB"), QStringLiteral(
                "@MAIN\n"                     // 1
                "PRINTV UNDECL_BARE:(-1)\n"   // 2 -> 警告（未声明名；X:-1 整体解析失败另报）
                "PRINTV ITEM:(-1)\n"          // 3 -> 警告（系统 1D）
                "PRINTV CFLAG:0:(-1)\n"       // 4 -> 警告（角色系统变量元素位）
                "PRINTV UNDECL_OK:0\n"        // 5 界内不报
                "PRINTV ITEM:100\n")}},        // 6 界内不报
            &storage);
        const auto hits = withCode(d, DiagCode::kStaticValue);
        check(hits.size() == 3, QString("负下标 -> 3 条警告（得到 %1）").arg(hits.size()));
        check(hits.size() == 3 && hits.at(0).line == 2 && hits.at(1).line == 3
                  && hits.at(2).line == 4,
              "定位到使用行（2/3/4）");
        bool allWarn = true;
        for (const auto& h : hits) allWarn = allWarn && h.severity == DiagSeverity::Warning;
        check(allWarn, "严重度全为 warning（只诊断，不禁行）");
    }

    qDebug() << "\n12) 数组边界：系统变量尺寸（VariableSize 默认表；*NAME 名表除外）";
    {
        VariableStorage storage;
        const ParseDiagnostics d = loadScriptsWithStorage({
            {QStringLiteral("a.ERB"), QStringLiteral(
                "@MAIN\n"                    // 1
                "PRINTV ITEM:5000\n"         // 2 -> 警告（ITEM 尺寸 1000）
                "PRINTV FLAG:20000\n"        // 3 -> 警告（FLAG 尺寸 10000）
                "PRINTV CFLAG:0:20000\n"     // 4 -> 警告（CFLAG 元素 10000）
                "PRINTFORM %STR:30000%\n"    // 5 -> 警告（STR 尺寸 20000）
                "PRINTFORM %ABLNAME:500%\n"  // 6 名表变量：真实尺寸=CSV 行数，不判上界
                "PRINTV ITEM:999\n"          // 7 界内不报
                "PRINTV A:0\n")}},           // 8 界内不报
            &storage);
        const auto hits = withCode(d, DiagCode::kStaticValue);
        check(hits.size() == 4, QString("系统尺寸越界 -> 4 条警告（得到 %1）").arg(hits.size()));
        check(hits.size() == 4 && hits.at(0).line == 2 && hits.at(1).line == 3
                  && hits.at(2).line == 4 && hits.at(3).line == 5,
              "定位到使用行（2/3/4/5）");
    }

    qDebug() << "\n13) DT_COLUMN_OPTIONS 的 DEFAULT 表达式纳入装载期校验";
    {
        AstBuilder::registerExtensionStatement(QStringLiteral("DT_COLUMN_OPTIONS"));
        const ParseDiagnostics d = loadScripts({
            {QStringLiteral("a.ERB"), QStringLiteral(
                "@MAIN\n"                                            // 1
                "#DIM A2, 3, 3\n"                                    // 2
                "DT_COLUMN_OPTIONS \"T\", \"C\", DEFAULT A2:1\n")},  // 3
        });
        // DEFAULT 表达式是运行期逐行求值的普通表达式：维数错误此前完全无诊断
        const auto hits = withCode(d, DiagCode::kArgCheck);
        check(hits.size() == 1,
              QString("DEFAULT 表达式维数错误 -> 1 条（得到 %1）").arg(hits.size()));
        check(!hits.isEmpty() && hits.first().line == 3, "定位到 DT_COLUMN_OPTIONS 行");
    }

    qDebug() << "\n14) FOR/REPEAT 计数器：循环值是「值」不是「下标」（FL3.8 标量计数器回归）";
    {
        // writeLoopCounter 语义：resolveIndices(计数器自身的固定下标) + 写循环值 ——
        // `FOR L_CNT, 0, 10` 写的是 L_CNT:0 = 0..9，不存在「越过尺寸 1」。
        // 第一版把循环值当成了下标，FL3.8 六处标量计数器全部误报 —— 此节锁死。
        // 计数器**自身的**常量下标越界仍要报（走上面的常量下标检查，计数器
        // 操作数也在 line.arguments 里）。
        VariableStorage storage;
        const ParseDiagnostics d = loadScriptsWithStorage({
            {QStringLiteral("a.ERB"), QStringLiteral(
                "@MAIN\n"                // 1
                "#DIM L_CNT\n"           // 2 标量计数器（第一版误报形态）
                "FOR L_CNT, 0, 10\n"      // 3 -> 不报
                "NEXT\n"                  // 4
                "FOR A, 0, 1500\n"        // 5 系统计数器大循环值 -> 不报
                "NEXT\n"                  // 6
                "REPEAT 1500\n"           // 7 COUNT 写的是 COUNT:0 -> 不报
                "REND\n"                  // 8
                "FOR A:1500, 0, 10\n"     // 9 -> 报：固定下标 1500 越过 A 的尺寸 1000
                "NEXT\n")}},              // 10
            &storage);
        const auto hits = withCode(d, DiagCode::kStaticValue);
        check(hits.size() == 1,
              QString("仅计数器自身固定下标越界报 1 条（得到 %1）").arg(hits.size()));
        check(hits.size() == 1 && hits.first().line == 9, "定位到第 9 行");
    }

    qDebug() << "\n15) 字面字符串下标：必须在常数名表中可解析（resolveIndex 否则必抛）";
    {
        QTemporaryDir csvDir;
        {
            QFile f(csvDir.filePath(QStringLiteral("TALENT.CSV")));
            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                f.write("ATTACK\nDEFENCE\n");   // ASCII 名字：绕开 CSV 编码猜测
        }
        ConstantTable ct;
        ct.loadCsvDirectory(csvDir.path(), false);
        check(ct.count(QStringLiteral("TALENT")) == 2,
              QString("常数名表装载（得到 %1 行）").arg(ct.count(QStringLiteral("TALENT"))));

        const auto loadWithNames = [&ct](const QString& source) {
            ProcessState state;
            EraParseTable table(&state);
            VariableStorage storage;
            table.setVariableStorage(&storage);
            table.setConstantTable(&ct);
            const AstResolver resolve = [&table](const QString& e) { return table.expressionAst(e); };
            const QStringList src = source.split(QLatin1Char('\n'));
            QList<LogicalLine> lines;
            for (int i = 0; i < src.size(); ++i)
                lines.append(AstBuilder::build(src.at(i), ScriptPosition(QStringLiteral("a.ERB"), i + 1, 1), resolve));
            table.loadScript(QStringLiteral("a.ERB"), lines);
            table.markLoadComplete();
            return table.parseDiagnostics();
        };

        const ParseDiagnostics ok = loadWithNames(QStringLiteral("@MAIN\n"
                           "PRINTV TALENT:0:\"ATTACK\"\n"));   // 攻撃 ✓ 表内
        check(withCode(ok, DiagCode::kStaticValue).isEmpty(),
              "表内名字下标 -> 不报（运行期映射到 0）");

        const ParseDiagnostics bad = loadWithNames(QStringLiteral("@MAIN\n"
                           "PRINTV TALENT:0:\"NOPE\"\n"));  // 未定義
        const auto badHits = withCode(bad, DiagCode::kStaticValue);
        check(badHits.size() == 1,
              QString("表外名字下标 -> 1 条（得到 %1）").arg(badHits.size()));
        check(!badHits.isEmpty() && badHits.first().severity == DiagSeverity::Error,
              "严重度为 error（运行到该行必抛，与除零同级）");
        check(!badHits.isEmpty() && badHits.first().message.contains(QStringLiteral("常数名表")),
              "消息说明原因（未在常数名表中定义）");

        // FORM 插值形态：%VAR:…% 的表达式里字符串下标同样要判（walkExpression
        // 会递归 StrForm parts）。用字符串变量（CSTR）—— %..% 是字符串插值，
        // 整型表达式会先命中既有的「FORM插值类型错误」，那是另一条检查。
        const ParseDiagnostics form = loadWithNames(QStringLiteral("@MAIN\n"
                           "PRINTFORM %CSTR:0:\"NOPE\"%\n"));
        check(withCode(form, DiagCode::kStaticValue).size() == 1,
              "FORM 插值内的表外名字下标 -> 1 条");

        const ParseDiagnostics plain = loadWithNames(QStringLiteral("@MAIN\n"
                           "#DIM ARR, 3\n"
                           "PRINTV ARR:\"x\"\n"));
        check(withCode(plain, DiagCode::kStaticValue).size() == 1,
              "无名字表的用户数组 + 字符串下标 -> 1 条（运行期同样必抛）");

        const ParseDiagnostics varsize = loadWithNames(QStringLiteral("@MAIN\n"
                           "#DIM ARR, 3\n"
                           "VARSIZE ARR:\"x\"\n"));
        check(withCode(varsize, DiagCode::kStaticValue).isEmpty(),
              "VARSIZE 只消费数组身份（下标被忽略）-> 不报");
    }

    qDebug() << "\n=============================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] static target tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
