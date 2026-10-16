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
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include "ast/ast_builder.h"
#include "ast/parse_diagnostic.h"
#include "era_parse_table.h"
#include "process_state.h"

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

    qDebug() << "\n=============================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] static target tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
