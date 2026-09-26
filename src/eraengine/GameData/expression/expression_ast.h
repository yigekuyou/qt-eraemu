#ifndef EXPRESSION_AST_H
#define EXPRESSION_AST_H

#include <QString>
#include <QList>
#include "expression_lexer.h"

class ExpressionNode {
public:
    virtual ~ExpressionNode() = default;
    virtual QString toString() const = 0;
};

class LiteralNode : public ExpressionNode {
public:
    LiteralNode(const ExpressionToken& token);
    QString toString() const override;
    
    ExpressionToken token() const { return m_token; }
    
private:
    ExpressionToken m_token;
};

class VariableNode : public ExpressionNode {
public:
    VariableNode(const QString& name);
    QString toString() const override;
    
    QString name() const { return m_name; }
    bool isArray() const { return !m_indices.isEmpty(); }
    
    void addIndex(ExpressionNode* index);
    const QList<ExpressionNode*>& indices() const { return m_indices; }
    
private:
    QString m_name;
    QList<ExpressionNode*> m_indices;
};

class BinaryOpNode : public ExpressionNode {
public:
    BinaryOpNode(ExpressionNode* left, 
                   const ExpressionToken& op, 
                   ExpressionNode* right);
    QString toString() const override;
    
    ExpressionNode* left() const { return m_left; }
    ExpressionToken op() const { return m_op; }
    ExpressionNode* right() const { return m_right; }
    
private:
    ExpressionNode* m_left;
    ExpressionToken m_op;
    ExpressionNode* m_right;
};

class UnaryOpNode : public ExpressionNode {
public:
    UnaryOpNode(const ExpressionToken& op, ExpressionNode* operand);
    QString toString() const override;
    
    ExpressionToken op() const { return m_op; }
    ExpressionNode* operand() const { return m_operand; }
    
private:
    ExpressionToken m_op;
    ExpressionNode* m_operand;
};

class FunctionNode : public ExpressionNode {
public:
    FunctionNode(const QString& name, const QList<ExpressionNode*>& args);
    QString toString() const override;
    
    QString name() const { return m_name; }
    const QList<ExpressionNode*>& arguments() const { return m_args; }
    
private:
    QString m_name;
    QList<ExpressionNode*> m_args;
};
class IfNode : public ExpressionNode {
public:
		IfNode(ExpressionNode* condition, ExpressionNode* thenExpr, ExpressionNode* elseExpr = nullptr);
		QString toString() const override;

		ExpressionNode* condition() const { return m_condition; }
		ExpressionNode* thenExpr() const { return m_thenExpr; }
		ExpressionNode* elseExpr() const { return m_elseExpr; }

private:
		ExpressionNode* m_condition;
		ExpressionNode* m_thenExpr;
		ExpressionNode* m_elseExpr;
};
// 格式化字符串的组成片段类型
enum class StrFormPartType {
		Text,       // 普通文本字面量
		Expression  // 嵌入的表达式
};

// 格式化字符串的单个片段
class StrFormPart {
public:
		StrFormPart(const QString& text); // 纯文本
		StrFormPart(ExpressionNode* expr); // 嵌入表达式
		~StrFormPart();

		StrFormPartType type() const { return m_type; }
		QString text() const { return m_text; }
		ExpressionNode* expression() const { return m_expr; }

private:
		StrFormPartType m_type;
		QString m_text;
		ExpressionNode* m_expr = nullptr;
};

class StrFormNode : public ExpressionNode {
public:
		explicit StrFormNode(const QList<StrFormPart*>& parts);
		~StrFormNode() override;

		QString toString() const override;

		// 获取所有组成片段
		const QList<StrFormPart*>& parts() const { return m_parts; }

		// 可选：参考 C# 的 Restructure 机制，用于常量折叠和优化
		// 如果所有嵌入表达式都是常量，可以将其折叠为普通的 LiteralNode
		ExpressionNode* restructure();

private:
		QList<StrFormPart*> m_parts;
};
#endif // EXPRESSION_AST_H
