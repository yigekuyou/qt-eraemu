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
// AST 源码区间（ExpressionNode::span）回归。
//
// 依据 test/data/language/表达式.md / 调试与错误.md：解析诊断要能定位。
// 表达式 AST 按文本缓存并跨行共享（EraParseTable::m_astCache），故 span 只能是
// **表达式文本内**的偏移（缓存安全）；绝对位置由调用方按行首列换算。
// 本测试锁住：① 根节点区间 == 整个表达式；② 子表达式区间各自正确；
//           ③ 解析失败时诊断带列号。
// ---------------------------------------------------------------------------
#include <QtTest>
#include <QString>
#include <QSharedPointer>
#include "expression_lexer.h"
#include "expression_parser.h"
#include "expression_ast.h"

namespace {
QSharedPointer<ExpressionNode> parseExpr(const QString& text) {
    ExpressionLexer lexer;
    ExpressionParser parser;
    return parser.parse(lexer.tokenize(text, 1));
}
}  // namespace

class TestAstSpan : public QObject {
    Q_OBJECT

private slots:
    void rootSpanCoversWholeExpression();
    void childSpanIsSubRange();
    void unaryAndParenSpan();
    void parseErrorReportsColumn();
};

void TestAstSpan::rootSpanCoversWholeExpression() {
    const QStringList cases = {
        QStringLiteral("1 + 2"),
        QStringLiteral("(1 + 2) * 3"),
        QStringLiteral("-1 + 2"),
        QStringLiteral("A * B + C"),
    };
    for (const QString& e : cases) {
        const auto ast = parseExpr(e);
        QVERIFY2(ast, qPrintable(QStringLiteral("解析失败: %1").arg(e)));
        const SourceSpan s = ast->span();
        QVERIFY2(s.valid(), qPrintable(QStringLiteral("span 无效: %1").arg(e)));
        // 列从 1 起；无尾随空白时区间长度 == 表达式长度
        QCOMPARE(s.begin, 1);
        QCOMPARE(s.length(), e.size());
    }
}

void TestAstSpan::childSpanIsSubRange() {
    // 1 + 2 * 3  ->  (+ 1 (* 2 3))
    const auto ast = parseExpr(QStringLiteral("1 + 2 * 3"));
    QVERIFY(ast);
    QCOMPARE(ast->kind(), NodeKind::BinaryOp);
    const auto root = qSharedPointerCast<BinaryOpNode>(ast);
    QCOMPARE(root->span().begin, 1);
    QCOMPARE(root->span().length(), 9);

    // 右子节点是 2 * 3（第 5 列起，长度 5）
    const auto right = root->right();
    QVERIFY(right);
    QCOMPARE(right->kind(), NodeKind::BinaryOp);
    QCOMPARE(right->span().begin, 5);
    QCOMPARE(right->span().length(), 5);

    // 左子节点是字面量 1（第 1 列起，长度 1）
    const auto left = root->left();
    QVERIFY(left);
    QCOMPARE(left->kind(), NodeKind::Literal);
    QCOMPARE(left->span().begin, 1);
    QCOMPARE(left->span().length(), 1);
}

void TestAstSpan::unaryAndParenSpan() {
    const auto neg = parseExpr(QStringLiteral("-1"));
    QVERIFY(neg);
    QCOMPARE(neg->kind(), NodeKind::UnaryOp);
    QCOMPARE(neg->span().begin, 1);
    QCOMPARE(neg->span().length(), 2);

    // 括号：根区间含外层括号（1..7）
    const auto paren = parseExpr(QStringLiteral("(1 + 2)"));
    QVERIFY(paren);
    QCOMPARE(paren->span().begin, 1);
    QCOMPARE(paren->span().length(), 7);
}

void TestAstSpan::parseErrorReportsColumn() {
    // 三元缺 `#`：consume 失败点应报出具体列（这里 `3` 在第 7 列）
    static QString captured;
    captured.clear();
    QtMessageHandler old = qInstallMessageHandler(
        [](QtMsgType type, const QMessageLogContext&, const QString& msg) {
            if (type == QtWarningMsg && msg.contains(QStringLiteral("表达式语法错误")))
                captured = msg;
        });
    const auto ast = parseExpr(QStringLiteral("1 ? 2 3"));
    qInstallMessageHandler(old);

    QVERIFY2(!ast, "畸形表达式应解析失败");
    QVERIFY2(!captured.isEmpty(), "应输出「表达式语法错误」诊断");
    QVERIFY2(captured.contains(QStringLiteral("第 7 列")),
             qPrintable(QStringLiteral("诊断应带列号，实际: %1").arg(captured)));
}

QTEST_APPLESS_MAIN(TestAstSpan)
#include "test_ast_span.moc"
