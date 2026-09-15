#include "expression_ast.h"
#include <QDebug>

LiteralNode::LiteralNode(const ExpressionToken& token) : m_token(token) {}

QString LiteralNode::toString() const {
    return "Literal(" + m_token.value() + ")";
}

VariableNode::VariableNode(const QString& name) : m_name(name) {}

QString VariableNode::toString() const {
    if (isArray()) {
        QString indicesStr = "";
        for (const auto& index : m_indices) {
            if (!indicesStr.isEmpty()) indicesStr += ", ";
            indicesStr += index->toString();
        }
        return "Variable(" + m_name + ", [" + indicesStr + "])";
    }
    return "Variable(" + m_name + ")";
}

void VariableNode::addIndex(ExpressionNode* index) {
    m_indices.append(index);
}

BinaryOpNode::BinaryOpNode(ExpressionNode* left, 
                              const ExpressionToken& op, 
                              ExpressionNode* right)
    : m_left(left), m_op(op), m_right(right) {}

QString BinaryOpNode::toString() const {
    return "BinaryOp(" + m_left->toString() + ", " + m_op.value() + ", " + m_right->toString() + ")";
}

UnaryOpNode::UnaryOpNode(const ExpressionToken& op, ExpressionNode* operand)
    : m_op(op), m_operand(operand) {}

QString UnaryOpNode::toString() const {
    return "UnaryOp(" + m_op.value() + ", " + m_operand->toString() + ")";
}

FunctionNode::FunctionNode(const QString& name, const QList<ExpressionNode*>& args)
    : m_name(name), m_args(args) {}

QString FunctionNode::toString() const {
    QString argsStr = "";
    for (const auto& arg : m_args) {
        if (!argsStr.isEmpty()) argsStr += ", ";
        argsStr += arg->toString();
    }
    return "Function(" + m_name + ", [" + argsStr + "])";
}
IfNode::IfNode(ExpressionNode* condition, ExpressionNode* thenExpr, ExpressionNode* elseExpr)
		: m_condition(condition), m_thenExpr(thenExpr), m_elseExpr(elseExpr) {}

QString IfNode::toString() const {
		QString str = "If(" + m_condition->toString() + ", " + m_thenExpr->toString();
		if (m_elseExpr) {
				str += ", " + m_elseExpr->toString();
		}
		str += ")";
		return str;
}
StrFormPart::StrFormPart(const QString& text)
		: m_type(StrFormPartType::Text), m_text(text) {}

StrFormPart::StrFormPart(ExpressionNode* expr)
		: m_type(StrFormPartType::Expression), m_expr(expr) {}

StrFormPart::~StrFormPart() {
		delete m_expr;
}

StrFormNode::StrFormNode(const QList<StrFormPart*>& parts)
		: m_parts(parts) {}

StrFormNode::~StrFormNode() {
		qDeleteAll(m_parts);
}

QString StrFormNode::toString() const {
		QString result = "StrForm([";
		for (int i = 0; i < m_parts.size(); ++i) {
				if (i > 0) result += ", ";
				if (m_parts[i]->type() == StrFormPartType::Text) {
						result += "Text(\"" + m_parts[i]->text() + "\")";
				} else {
						result += "Expr(" + m_parts[i]->expression()->toString() + ")";
				}
		}
		result += "])";
		return result;
}