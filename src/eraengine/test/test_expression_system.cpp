#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include "expression_lexer.h"
#include "expression_parser.h"
#include "expression_ast.h"
#include "expression_evaluator.h"
#include "variable_storage.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    qDebug() << "Expression System Test";
    qDebug() << "======================";
    
    // Test 1: Tokenizer
    qDebug() << "\n1. Testing Expression Lexer:";
    ExpressionLexer lexer;
    
    QStringList testExpressions = {
        "10 + 20",
        "DAY + MONEY",
        "A * B + C",
        "(1 + 2) * 3",
        "DAY > 5",
        "A + B * C",
        "A > 10 && B < 20",
        "A == 10 || B == 20"
    };
    
    for (const QString& expr : testExpressions) {
        qDebug() << "\n   Testing Expression:" << expr;
        QList<ExpressionToken> tokens = lexer.tokenize(expr);
        
        QString tokenStr;
        for (const auto& token : tokens) {
            if (token.type() != TokenType::END_OF_FILE) {
                tokenStr += token.value() + " ";
            }
        }
        qDebug() << "   Tokens:" << tokenStr.trimmed();
        qDebug() << "   Token count:" << tokens.size();
    }
    
    // Test 2: Parser
    qDebug() << "\n\n2. Testing Expression Parser:";
    ExpressionParser parser;
    
    QString parseTestExpr = "10 + 20 * 3";
    QList<ExpressionToken> parseTokens = lexer.tokenize(parseTestExpr);
    ExpressionNode* ast = parser.parse(parseTokens);
    
    if (ast) {
        qDebug() << "   AST created for:" << parseTestExpr;
        qDebug() << "   AST representation:" << ast->toString();
    } else {
        qDebug() << "   Failed to parse:" << parseTestExpr;
    }
    
    // Test 3: Evaluator with VariableStorage
    qDebug() << "\n\n3. Testing Expression Evaluator:";
    VariableStorage storage;
    ExpressionEvaluator evaluator;
    
    // Set up some test variables using storage
    qDebug() << "\n   Setting up test variables:";
    storage.setGlobalInt1D("DAY", 0, 5);
    qDebug() << "   storage.setGlobalInt1D(\"DAY\", 0, 5)";
    
    storage.setGlobalInt1D("MONEY", 0, 100);
    qDebug() << "   storage.setGlobalInt1D(\"MONEY\", 0, 100)";
    
    storage.setGlobalInt1D("A", 0, 10);
    qDebug() << "   storage.setGlobalInt1D(\"A\", 0, 10)";
    
    storage.setGlobalInt1D("B", 0, 20);
    qDebug() << "   storage.setGlobalInt1D(\"B\", 0, 20)";
    
    storage.setGlobalInt1D("C", 0, 30);
    qDebug() << "   storage.setGlobalInt1D(\"C\", 0, 30)";
    
    // Test simple arithmetic
    QString evalTestExpr = "A + B";
    qDebug() << "\n   Testing evaluation:" << evalTestExpr;
    QList<ExpressionToken> evalTokens = lexer.tokenize(evalTestExpr);
    ast = parser.parse(evalTokens);
    
    if (ast) {
        qDebug() << "   AST:" << ast->toString();
        QVariant result = evaluator.evaluate(ast->toString(), &storage);
        qDebug() << "   Result:" << result.toString();
    }
    
    // Test with constants
    evalTestExpr = "10 + 20 * 3";
    qDebug() << "\n   Testing evaluation:" << evalTestExpr;
    evalTokens = lexer.tokenize(evalTestExpr);
    ast = parser.parse(evalTokens);
    
    if (ast) {
        qDebug() << "   AST:" << ast->toString();
        QVariant result = evaluator.evaluate(ast->toString(), &storage);
        qDebug() << "   Result:" << result.toString();
        qDebug() << "   Expected: 70 (10 + 60)";
    }
    
    // Test 4: Comparison expressions
    qDebug() << "\n\n4. Testing Comparison Expressions:";
    QString compareExpr = "DAY > 3";
    qDebug() << "   Testing:" << compareExpr;
    QList<ExpressionToken> compareTokens = lexer.tokenize(compareExpr);
    ast = parser.parse(compareTokens);
    
    if (ast) {
        qDebug() << "   AST:" << ast->toString();
        QVariant result = evaluator.evaluate(ast->toString(), &storage);
        qDebug() << "   Result:" << result.toString();
        qDebug() << "   Expected: true (5 > 3)";
    }
    
    // Test 5: Complex expressions
    qDebug() << "\n\n5. Testing Complex Expressions:";
    QStringList complexExprs = {
        "A + B * C",
        "(A + B) * C",
        "DAY > 3 && A < 20",
        "MONEY >= 100 || DAY < 1",
        "A == 10"
    };
    
    for (const QString& expr : complexExprs) {
        qDebug() << "\n   Testing:" << expr;
        QList<ExpressionToken> tokens = lexer.tokenize(expr);
        ast = parser.parse(tokens);
        
        if (ast) {
            qDebug() << "   AST:" << ast->toString();
            QVariant result = evaluator.evaluate(ast->toString(), &storage);
            qDebug() << "   Result:" << result.toString();
        } else {
            qDebug() << "   Failed to parse";
        }
    }
    
    // Test 6: Test with local variables
    qDebug() << "\n\n6. Testing with Local Variables:";
    storage.setLocalInt(0, 100);
    qDebug() << "   storage.setLocalInt(0, 100)";
    storage.setLocalStr(0, "test");
    qDebug() << "   storage.setLocalStr(0, \"test\")";
    
    QString localExpr = "A + 50";
    qDebug() << "\n   Testing:" << localExpr;
    evalTokens = lexer.tokenize(localExpr);
    ast = parser.parse(evalTokens);
    
    if (ast) {
        QVariant result = evaluator.evaluate(ast->toString(), &storage);
        qDebug() << "   Result:" << result.toString();
        qDebug() << "   Expected: 60 (10 + 50)";
    }
    
    qDebug() << "\nExpression system test complete!";
    
    return 0;
}
