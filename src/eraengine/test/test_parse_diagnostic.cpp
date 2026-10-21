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
// 装载期结构化诊断（R3）回归。
//
// 依据 test/data/language/调试与错误.md「警告等级与加载期警告」：诊断要分级、
// 可定位、可按类统计。本测试锁住：
//   ① 既有文本告警 "文件:行: 文本" 能拆出 position；
//   ② summarize() 给出「按级别 + 按 code」的汇总；
//   ③ AstBuilder 对未登记的指令产出 Warning/unknown-instruction 诊断（带位置）。
// ---------------------------------------------------------------------------
#include <QtTest>
#include "ast/parse_diagnostic.h"
#include "ast/ast_builder.h"
#include "ast/logical_line.h"

class TestParseDiagnostic : public QObject {
    Q_OBJECT

private slots:
    void textWarningSplitsPosition();
    void summarizeGroupsBySeverityAndCode();
    void astBuilderEmitsUnknownInstruction();
    void positionSurvivesPercentEscapes();
};

void TestParseDiagnostic::textWarningSplitsPosition() {
    ParseDiagnostics d;
    d.addText(DiagSeverity::Warning, DiagCode::kPreprocess,
              QStringLiteral("MAIN.ERB:12: 行连接 '{' 缺少对应的 '}'"));
    QCOMPARE(d.size(), 1);
    const ParseDiagnostic& x = d.all().first();
    QCOMPARE(x.severity, DiagSeverity::Warning);
    QCOMPARE(x.position, QStringLiteral("MAIN.ERB:12"));
    QCOMPARE(x.message, QStringLiteral("行连接 '{' 缺少对应的 '}'"));
    // 文本视图保持既有形态（消费者兼容）
    QCOMPARE(d.texts().first(),
             QStringLiteral("MAIN.ERB:12: 行连接 '{' 缺少对应的 '}'"));
}

void TestParseDiagnostic::summarizeGroupsBySeverityAndCode() {
    ParseDiagnostics d;
    d.add(DiagSeverity::Warning, DiagCode::kUnknownInstruction, QStringLiteral("a.ERB:1:1"),
          QStringLiteral("未识别的指令: FOO"));
    d.add(DiagSeverity::Warning, DiagCode::kUnknownInstruction, QStringLiteral("a.ERB:2:1"),
          QStringLiteral("未识别的指令: BAR"));
    d.add(DiagSeverity::Warning, DiagCode::kExprParse, QString(), QStringLiteral("表达式无法归约: X"));
    d.add(DiagSeverity::Error, DiagCode::kDeclError, QStringLiteral("a.ERB:3:1"),
          QStringLiteral("#DIM 声明错误"));

    QCOMPARE(d.count(DiagSeverity::Warning), 3);
    QCOMPARE(d.count(DiagSeverity::Error), 1);
    const QMap<QString, int> byCode = d.countByCode();
    QCOMPARE(byCode.value(QStringLiteral("unknown-instruction")), 2);
    QCOMPARE(byCode.value(QStringLiteral("expr-parse")), 1);
    QCOMPARE(byCode.value(QStringLiteral("decl-error")), 1);

    const QString s = d.summarize();
    QVERIFY2(s.contains(QStringLiteral("错误 1")), qPrintable(s));
    QVERIFY2(s.contains(QStringLiteral("警告 3")), qPrintable(s));
    QVERIFY2(s.contains(QStringLiteral("unknown-instruction 2")), qPrintable(s));
}

void TestParseDiagnostic::astBuilderEmitsUnknownInstruction() {
    ParseDiagnostics diags;
    ScriptPosition pos(QStringLiteral("t.ERB"), 3, 1);
    const LogicalLine line = AstBuilder::build(QStringLiteral("NOTACOMMAND 1"), pos,
                                               nullptr, {}, &diags);

    QCOMPARE(line.kind, LineKind::Instruction);
    QCOMPARE(diags.size(), 1);
    const ParseDiagnostic& x = diags.all().first();
    QCOMPARE(x.severity, DiagSeverity::Warning);
    QCOMPARE(x.code, QString::fromLatin1(DiagCode::kUnknownInstruction));
    QCOMPARE(x.position, QStringLiteral("t.ERB:3:1"));
    QVERIFY2(x.message.contains(QStringLiteral("NOTACOMMAND")), qPrintable(x.message));
    QVERIFY2(x.snippet.contains(QStringLiteral("NOTACOMMAND")), qPrintable(x.snippet));
    // 显式错误标记（行自带，供缓存/诊断复查）
    QCOMPARE(line.errMes(), QStringLiteral("未识别的指令: NOTACOMMAND"));

    // 已知指令不产生诊断
    ParseDiagnostics ok;
    AstBuilder::build(QStringLiteral("PRINTL hello"), pos, nullptr, {}, &ok);
    QCOMPARE(ok.size(), 0);
}

void TestParseDiagnostic::positionSurvivesPercentEscapes() {
    // Android 上 file/scriptName 是 content:// URI，路径里必有 %2F/%3A 百分号转义。
    // 文本位置串曾用链式 .arg() 拼接，%2/%3 被当占位符吃掉（真机 eraH 实测：
    // 所有 %2F→"<行号>F"、%3A→"<列号>A"）—— 这里锁住三处同因的修复：
    //   ① ParseDiagnostic::positionText() ② ScriptPosition::toString()
    //   ③ ErbPreprocessor 的警告前缀（同文件名）
    const QString uri = QStringLiteral(
        "content://com.android.externalstorage.documents/tree/primary%3Aeraemu"
        "/document/primary%3Aeraemu%2FeraH%2FERB%2FKOJO.ERB");

    ParseDiagnostics d;
    d.add(DiagSeverity::Warning, DiagCode::kArgCheck, uri, 1904, 1, 0,
          QStringLiteral("函数 C_FIRSTTIME_KOJO 第 1 个实参需要Int，实得字符串"));
    const ParseDiagnostic& x = d.all().first();
    QCOMPARE(x.file, uri);                  // 结构化字段原样保留
    QCOMPARE(x.line, 1904);
    QCOMPARE(x.column, 1);
    QCOMPARE(x.position, uri + QStringLiteral(":1904:1"));   // 文本视图不再被吃
    QVERIFY2(!x.position.contains(QStringLiteral("1904F")), qPrintable(x.position));
    QVERIFY2(!x.position.contains(QStringLiteral("1Aeraemu")), qPrintable(x.position));

    ScriptPosition sp;
    sp.filename = uri;
    sp.lineNumber = 1904;
    sp.column = 1;
    QCOMPARE(sp.toString(), uri + QStringLiteral(":1904:1"));
    QVERIFY2(!sp.toString().contains(QStringLiteral("1904F")), qPrintable(sp.toString()));
}

QTEST_APPLESS_MAIN(TestParseDiagnostic)
#include "test_parse_diagnostic.moc"
