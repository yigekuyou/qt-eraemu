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
    // 三元语义（对齐 C# TermStack；见 test/data/language/运算符.md「三元运算符」）
    void ternaryIsLeftAssociative();
    void ternaryBranchesAreNonTernary();
    void parenthesizedNestedTernaryIsAccepted();
    void ternaryMissingHashFails();
    // 绑定力表（Pratt）：默认全左结合；Provider 注入可表达右结合
    void binaryBindingPowersAreLeftAssociative();
    void rightAssociativeInjectionGivesRightAssoc();
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

void TestAstSpan::ternaryIsLeftAssociative() {
    // A ? B # C ? D # E  ==  ((A ? B # C) ? D # E)
    const auto ast = parseExpr(QStringLiteral("A ? B # C ? D # E"));
    QVERIFY(ast);
    QCOMPARE(ast->kind(), NodeKind::If);
    const auto root = qSharedPointerCast<IfNode>(ast);
    // 外层条件本身是三元 ⇒ 左结合
    QVERIFY(root->condition());
    QCOMPARE(root->condition()->kind(), NodeKind::If);
    // 外层分支是普通变量 C/D/E 侧：then = D，else = E
    QCOMPARE(root->thenExpr()->kind(), NodeKind::Variable);
    QCOMPARE(root->elseExpr()->kind(), NodeKind::Variable);

    const auto inner = qSharedPointerCast<IfNode>(root->condition());
    QCOMPARE(inner->thenExpr()->kind(), NodeKind::Variable);   // B
    QCOMPARE(inner->elseExpr()->kind(), NodeKind::Variable);   // C
}

void TestAstSpan::ternaryBranchesAreNonTernary() {
    // 未加括号的嵌套 `?`：C# 因「式の数が不足しています」报错，本实现同样失败
    QVERIFY2(!parseExpr(QStringLiteral("A ? B ? C # D # E")),
             "未加括号的嵌套三元应解析失败（对齐 C#）");
}

void TestAstSpan::parenthesizedNestedTernaryIsAccepted() {
    const auto ast = parseExpr(QStringLiteral("A ? (B ? C # D) # E"));
    QVERIFY(ast);
    QCOMPARE(ast->kind(), NodeKind::If);
    const auto root = qSharedPointerCast<IfNode>(ast);
    QVERIFY(root->thenExpr());
    QCOMPARE(root->thenExpr()->kind(), NodeKind::If);          // 括号里的三元
    QCOMPARE(root->elseExpr()->kind(), NodeKind::Variable);    // E
}

void TestAstSpan::ternaryMissingHashFails() {
    // 表达式层缺 `#` = 错误（C# ReduceTernaryTerm 之前就因操作数不足抛 CodeEE）。
    // 注意：StrForm（\@ … ? … \@）层缺 `#` 是「警告 + 假值空串」，见 strform_parser。
    QVERIFY2(!parseExpr(QStringLiteral("A ? B")), "缺 # 应解析失败");
}

void TestAstSpan::binaryBindingPowersAreLeftAssociative() {
    // EraBasic 原生运算符全部左结合：{p, p+1}
    struct Case { TokenType op; int p; };
    const Case cases[] = {
        {TokenType::MULTIPLY, 10}, {TokenType::MINUS, 9}, {TokenType::SHIFT_LEFT, 8},
        {TokenType::LESS_THAN, 7}, {TokenType::EQUALS, 6}, {TokenType::BIT_AND, 5},
        {TokenType::AND, 4},
    };
    for (const Case& c : cases) {
        const BindingPower bp = ExpressionParser::binaryBindingPower(c.op);
        QCOMPARE(bp.left, c.p);
        QCOMPARE(bp.right, c.p + 1);          // 左结合 ⇒ right > left
    }
    // 非二元运算符：哨兵 {0,0}
    const BindingPower none = ExpressionParser::binaryBindingPower(TokenType::COMMA);
    QCOMPARE(none.left, 0);
    QCOMPARE(none.right, 0);
}

void TestAstSpan::rightAssociativeInjectionGivesRightAssoc() {
    // 默认左结合：A - B - C == ((A - B) - C)
    {
        const auto ast = parseExpr(QStringLiteral("A - B - C"));
        QVERIFY(ast);
        const auto root = qSharedPointerCast<BinaryOpNode>(ast);
        QCOMPARE(root->left()->kind(), NodeKind::BinaryOp);    // 左边先归约
        QCOMPARE(root->right()->kind(), NodeKind::Variable);
    }
    // 注入：把 MINUS 声明为右结合（{9,9}）⇒ A - B - C == (A - (B - C))
    {
        ExpressionLexer lexer;
        ExpressionParser parser;
        parser.setBindingPowerProvider([](TokenType op) -> BindingPower {
            BindingPower bp = ExpressionParser::binaryBindingPower(op);
            if (op == TokenType::MINUS) bp.right = bp.left;    // 右结合
            return bp;
        });
        const auto ast = parser.parse(lexer.tokenize(QStringLiteral("A - B - C"), 1));
        QVERIFY(ast);
        const auto root = qSharedPointerCast<BinaryOpNode>(ast);
        QCOMPARE(root->left()->kind(), NodeKind::Variable);     // A
        QCOMPARE(root->right()->kind(), NodeKind::BinaryOp);    // (B - C)
    }
}

QTEST_APPLESS_MAIN(TestAstSpan)
#include "test_ast_span.moc"