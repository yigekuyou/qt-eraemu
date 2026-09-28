/*
 * emuera —— 测试 PRINT 族尾随空白保留
 */
#include <QtTest/QtTest>
#include <QString>
#include "GameData/ast/ast_builder.h"
#include "GameData/ast/logical_line.h"

class TestPrintWhitespace : public QObject {
    Q_OBJECT

private slots:
    void testPrintLiteralPreservesTrailingSpaces();
    void testPrintFormPreservesTrailingSpaces();
    void testPrintsPreservesTrailingSpacesInQuotedString();
    void testPrintcPreservesTrailingSpaces();
};

void TestPrintWhitespace::testPrintLiteralPreservesTrailingSpaces() {
    // PRINT  text    (尾随3个空格) -> 应保留
    const QString source = QStringLiteral("PRINT text   ");
    ScriptPosition pos;
    pos.lineNumber = 1;

    const LogicalLine line = AstBuilder::build(source, pos, nullptr);

    QCOMPARE(line.kind, LineKind::Instruction);
    QCOMPARE(line.functionName, QStringLiteral("PRINT"));
    QCOMPARE(line.arguments.size(), 1);
    // 参数应是 "text   "（保留尾随空格）
    QCOMPARE(line.arguments.first().raw, QStringLiteral("text   "));
}

void TestPrintWhitespace::testPrintFormPreservesTrailingSpaces() {
    // PRINTFORM  分数{SCORE}    (尾随4个空格) -> 应保留
    const QString source = QStringLiteral("PRINTFORM 分数{SCORE}    ");
    ScriptPosition pos;
    pos.lineNumber = 1;

    const LogicalLine line = AstBuilder::build(source, pos, nullptr);

    QCOMPARE(line.kind, LineKind::Instruction);
    QCOMPARE(line.functionName, QStringLiteral("PRINTFORM"));
    QCOMPARE(line.arguments.size(), 1);
    // 参数应是 "分数{SCORE}    "（保留尾随空格）
    QCOMPARE(line.arguments.first().raw, QStringLiteral("分数{SCORE}    "));
}

void TestPrintWhitespace::testPrintsPreservesTrailingSpacesInQuotedString() {
    // PRINTS "hello   " (引号内尾随3个空格) -> 应保留
    const QString source = QStringLiteral("PRINTS \"hello   \"");
    ScriptPosition pos;
    pos.lineNumber = 1;

    const LogicalLine line = AstBuilder::build(source, pos, nullptr);

    QCOMPARE(line.kind, LineKind::Instruction);
    QCOMPARE(line.functionName, QStringLiteral("PRINTS"));
    QCOMPARE(line.arguments.size(), 1);
    // 参数应是 "\"hello   \""（引号保留），raw 应是 "hello   "（引号内保留空格）
    const Operand& arg = line.arguments.first();
    QCOMPARE(arg.raw, QStringLiteral("hello   "));  // 引号被剥离后的内容
    QVERIFY(line.printTemplate);
    QCOMPARE(line.printTemplate->parts.size(), 1);
    QCOMPARE(line.printTemplate->parts.first().kind, PrintTemplatePart::Kind::Text);
    QCOMPARE(line.printTemplate->parts.first().text, QStringLiteral("hello   "));
}

void TestPrintWhitespace::testPrintcPreservesTrailingSpaces() {
    // PRINTC  col1    (尾随4个空格) -> 应保留，用于列宽计算
    const QString source = QStringLiteral("PRINTC col1    ");
    ScriptPosition pos;
    pos.lineNumber = 1;

    const LogicalLine line = AstBuilder::build(source, pos, nullptr);

    QCOMPARE(line.kind, LineKind::Instruction);
    QCOMPARE(line.functionName, QStringLiteral("PRINTC"));
    QCOMPARE(line.arguments.size(), 1);
    // 参数应是 "col1    "（保留尾随空格）
    QCOMPARE(line.arguments.first().raw, QStringLiteral("col1    "));
}

QTEST_APPLESS_MAIN(TestPrintWhitespace)
#include "test_print_whitespace.moc"