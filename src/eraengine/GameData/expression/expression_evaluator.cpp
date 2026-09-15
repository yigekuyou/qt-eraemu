#include "expression_evaluator.h"
#include "expression_parser.h"
#include "expression_ast.h"
#include "expression_lexer.h"
#include "variable_storage.h"
#include "variable_token.h"
#include "game_base_data.h"
#include <QRegularExpression>
#include <QDebug>

ExpressionEvaluator::ExpressionEvaluator(QObject *parent)
    : QObject(parent)
{
}

QVariant ExpressionEvaluator::evaluate(const QString &expression, VariableStorage *storage, GameBaseData *gameBaseData)
{
		if (!storage) {
				emit evaluationError(expression, "VariableStorage is null");
				return QVariant();
		}

		ExpressionLexer lexer;
		auto tokens = lexer.tokenize(expression);
		if (tokens.isEmpty()) {
				return QVariant();
		}

		ExpressionParser parser;
		// 使用 std::unique_ptr 包装 AST 根节点，离开作用域时自动释放，杜绝内存泄漏
		std::unique_ptr<ExpressionNode> ast(parser.parse(tokens));

		if (!ast) {
				emit evaluationError(expression, "Failed to parse expression AST");
				return QVariant();
		}

		QVariant result = evaluateNode(*ast, storage, gameBaseData);

		emit evaluationFinished(expression, result);
		return result;
}
QVariant ExpressionEvaluator::slotEvaluate(const QString &expression, VariableStorage *storage, GameBaseData *gameBaseData)
{
		return evaluate(expression, storage, gameBaseData);
}

QVariant ExpressionEvaluator::evaluateNode(const ExpressionNode &node, VariableStorage *storage, GameBaseData *gameBaseData)
{
    // Dispatch to appropriate evaluation method based on node type
    if (const auto *literal = dynamic_cast<const LiteralNode*>(&node)) {
        return evaluateLiteral(*literal);
    } else if (const auto *variable = dynamic_cast<const VariableNode*>(&node)) {
        return evaluateVariable(*variable, storage, gameBaseData);
    } else if (const auto *binary = dynamic_cast<const BinaryOpNode*>(&node)) {
        return evaluateBinaryOp(*binary, storage, gameBaseData);
    } else if (const auto *unary = dynamic_cast<const UnaryOpNode*>(&node)) {
        return evaluateUnaryOp(*unary, storage, gameBaseData);
    } else if (const auto *function = dynamic_cast<const FunctionNode*>(&node)) {
        return evaluateFunction(*function, storage, gameBaseData);
    }
    
    return QVariant();
}

QVariant ExpressionEvaluator::evaluateLiteral(const LiteralNode &node)
{
    // Return the literal value directly
    return QVariant(node.token().value());
}

QVariant ExpressionEvaluator::evaluateVariable(const VariableNode &node, VariableStorage *storage, GameBaseData *gameBaseData)
{
    qDebug() << "evaluateVariable: varName=" << node.name();
    
    if (!storage) {
        return QVariant();
    }
    
    QString varName = node.name();
    
    // Check if this is a GameBase variable
    if (varName.startsWith("GAMEBASE_") && gameBaseData) {
        QString key = varName.mid(9);  // Remove "GAMEBASE_" prefix
        QString value = gameBaseData->get(key);
        qDebug() << "  GameBase value for" << key << ":" << value;
        if (!value.isEmpty()) {
            return QVariant(value);
        }
    }
    
    // Check if this is an array access
    if (node.isArray()) {
        // Evaluate the index expression
        const auto& indices = node.indices();
        if (indices.isEmpty()) {
            return QVariant();
        }
        
        // For now, assume 1D array access
        auto *indexNode = indices.first();
        if (auto *literal = dynamic_cast<LiteralNode*>(indexNode)) {
            int index = evaluateLiteral(*literal).toInt();
            return evaluateIndexedVariable(varName, index, storage);
        }
    }
    
    // Simple variable access - return 0 if not found
    return QVariant(0);
}

QVariant ExpressionEvaluator::evaluateIndexedVariable(const QString &varName, int index, VariableStorage *storage)
{
    if (!storage) {
        return QVariant(0);
    }
    
    // Try to get the variable value
    // This would need to check the variable type and call the appropriate method
    // For now, returning 0 for unknown variables
    return QVariant(0);
}

QVariant ExpressionEvaluator::evaluateBinaryOp(const BinaryOpNode &node, VariableStorage *storage, GameBaseData *gameBaseData)
{
    QVariant left = evaluateNode(*node.left(), storage, gameBaseData);
    QVariant right = evaluateNode(*node.right(), storage, gameBaseData);
    
    if (!left.isValid() || !right.isValid()) {
        return QVariant();
    }
    
    switch (node.op().type()) {
    case TokenType::PLUS:
        return left.toInt() + right.toInt();
    case TokenType::MINUS:
        return left.toInt() - right.toInt();
    case TokenType::MULTIPLY:
        return left.toInt() * right.toInt();
    case TokenType::DIVIDE:
        if (right.toInt() == 0) {
            return QVariant(0); // Avoid division by zero
        }
        return left.toInt() / right.toInt();
    case TokenType::MODULO:
        if (right.toInt() == 0) {
            return QVariant(0); // Avoid modulo by zero
        }
        return left.toInt() % right.toInt();
    case TokenType::LESS_THAN:
        return left.toInt() < right.toInt();
    case TokenType::LESS_EQUAL:
        return left.toInt() <= right.toInt();
    case TokenType::GREATER_THAN:
        return left.toInt() > right.toInt();
    case TokenType::GREATER_EQUAL:
        return left.toInt() >= right.toInt();
    case TokenType::EQUALS:
        return left.toInt() == right.toInt();
    case TokenType::NOT_EQUALS:
        return left.toInt() != right.toInt();
    case TokenType::AND:
        return left.toBool() && right.toBool();
    case TokenType::OR:
        return left.toBool() || right.toBool();
    default:
        return QVariant();
    }
}

QVariant ExpressionEvaluator::evaluateUnaryOp(const UnaryOpNode &node, VariableStorage *storage, GameBaseData *gameBaseData)
{
    QVariant operand = evaluateNode(*node.operand(), storage, gameBaseData);
    
    if (!operand.isValid()) {
        return QVariant();
    }
    
    switch (node.op().type()) {
    case TokenType::NOT:
        return !operand.toBool();
    default:
        return QVariant();
    }
}

QVariant ExpressionEvaluator::evaluateFunction(const FunctionNode &node, VariableStorage *storage, GameBaseData *gameBaseData)
{
    Q_UNUSED(storage);
    Q_UNUSED(gameBaseData);
    
    // Built-in function evaluation
    QString funcName = node.name().toUpper();
    
    if (funcName == "ABS") {
        if (node.arguments().isEmpty()) {
            return QVariant(0);
        }
        QVariant arg = evaluateNode(*node.arguments().first(), storage);
        return QVariant(qAbs(arg.toInt()));
    }
    
    if (funcName == "LENGTH") {
        if (node.arguments().isEmpty()) {
            return QVariant(0);
        }
        QVariant arg = evaluateNode(*node.arguments().first(), storage);
        return QVariant(arg.toString().length());
    }
    
    // Add more built-in functions as needed
    
    return QVariant(0);
}

bool ExpressionEvaluator::evaluateInt(const QString &expression, VariableStorage *storage, qint64 &result)
{
    if (!storage) {
        return false;
    }
    
    QVariant value = evaluate(expression, storage);
    
    if (value.isValid() && value.canConvert<qint64>()) {
        result = value.value<qint64>();
        return true;
    }
    
    return false;
}

bool ExpressionEvaluator::evaluateStr(const QString &expression, VariableStorage *storage, QString &result)
{
    if (!storage) {
        return false;
    }
    
    QVariant value = evaluate(expression, storage);
    
    if (value.isValid() && value.canConvert<QString>()) {
        result = value.toString();
        return true;
    }
    
    return false;
}

// Helper method that would actually perform the evaluation
bool ExpressionEvaluator::parseAndEvaluate(const QString &expression, VariableStorage *storage)
{
    if (!storage) {
        return false;
    }
    
    // Parse the expression into an AST
    ExpressionLexer lexer;
    auto tokens = lexer.tokenize(expression);
    
    if (tokens.isEmpty()) {
        return false;
    }
    
    ExpressionParser parser;
    auto ast = parser.parse(tokens);
    
    if (!ast) {
        return false;
    }
    
    // Evaluate the AST
    evaluateNode(*ast, storage);
    
    return true;
}