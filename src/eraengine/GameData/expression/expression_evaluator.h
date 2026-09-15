#ifndef EXPRESSION_EVALUATOR_H
#define EXPRESSION_EVALUATOR_H

#include <QObject>
#include <QVariant>
#include <QString>
#include <QList>

class VariableStorage; // Forward declaration
class GameBaseData; // Forward declaration
class ExpressionNode;
class LiteralNode;
class VariableNode;
class BinaryOpNode;
class UnaryOpNode;
class FunctionNode;

class ExpressionEvaluator : public QObject
{
    Q_OBJECT

public:
		explicit ExpressionEvaluator(QObject *parent = nullptr);

		Q_INVOKABLE QVariant evaluate(const QString &expression, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
		Q_INVOKABLE bool evaluateInt(const QString &expression, VariableStorage *storage, qint64 &result);
		Q_INVOKABLE bool evaluateStr(const QString &expression, VariableStorage *storage, QString &result);

signals:
		void evaluationFinished(const QString &expression, const QVariant &result);
		void evaluationError(const QString &expression, const QString &errorString);

public slots:
		QVariant slotEvaluate(const QString &expression, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
private:
    // AST evaluation methods
    QVariant evaluateNode(const ExpressionNode &node, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
    QVariant evaluateLiteral(const LiteralNode &node);
    QVariant evaluateVariable(const VariableNode &node, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
    QVariant evaluateIndexedVariable(const QString &varName, int index, VariableStorage *storage);
    QVariant evaluateBinaryOp(const BinaryOpNode &node, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
    QVariant evaluateUnaryOp(const UnaryOpNode &node, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
    QVariant evaluateFunction(const FunctionNode &node, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
    
    // Helper methods for expression evaluation
    bool parseAndEvaluate(const QString &expression, VariableStorage *storage);
};

#endif // EXPRESSION_EVALUATOR_H