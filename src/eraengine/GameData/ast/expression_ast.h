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
#ifndef EXPRESSION_AST_H
#define EXPRESSION_AST_H

#include <QString>
#include <QList>
#include <QSharedPointer>
#include <functional>
#include "expression_lexer.h"
#include "operand_type.h"
#include "operator_table.h"
#include "function_types.h"

// ---------------------------------------------------------------------------
// 强类型表达式 AST（对齐 C# GameData/Expression）
//
//   C#                       C++（本文件）
//   IOperandTerm          -> ExpressionNode        （带 valueType()）
//   SingleTerm            -> LiteralNode
//   VariableTerm          -> VariableNode
//   StrFormTerm           -> StrFormNode
//   OperatorTerm/Method   -> BinaryOpNode/UnaryOpNode
//   FunctionMethodTerm    -> FunctionNode（带解析期返回类型）
//   Ternary*              -> IfNode
//
// 每个节点都标注 OperandType（Int/Str/Void），供解析期类型检查与求值期严格分派。
// 节点不可变、可共享（QSharedPointer）。
// ---------------------------------------------------------------------------

// 节点类别（tag）：求值器用 switch(tag) 分派（跳转表 + 分支预测），
// 取代 dynamic_cast 链（去掉 RTTI 热路径开销）。
enum class NodeKind : quint8 {
    Literal, Variable, BinaryOp, UnaryOp, Function, If, StrForm
};

// 整数/进制字面量求值（对齐 C# LexicalAnalyzer.ReadInt64）：
//   0x.. 十六进制、0b.. 二进制、p/P 2 的幂、e/E 10 的幂（如 "1p0"）。
//   成功返回 true 并写出结果。
[[nodiscard]] bool parseIntegerLiteral(const QString& text, qint64& out);

class ExpressionNode {
public:
    virtual ~ExpressionNode() = default;
    [[nodiscard]] virtual NodeKind kind() const noexcept = 0;
    [[nodiscard]] virtual QString toString() const = 0;
    [[nodiscard]] virtual OperandType valueType() const = 0;

    [[nodiscard]] bool isInteger() const { return valueType() == OperandType::Int; }
    [[nodiscard]] bool isString() const { return valueType() == OperandType::Str; }
    // 是否在解析期就已确定类型（未知类型走执行期宽松/分支预测路径）
    [[nodiscard]] bool isStaticallyTyped() const { return isKnown(valueType()); }
};

// SingleTerm：字面量（整数或字符串）
class LiteralNode : public ExpressionNode {
public:
    explicit LiteralNode(const ExpressionToken& token);
    LiteralNode(qint64 value);
    LiteralNode(const QString& value);

    [[nodiscard]] NodeKind kind() const noexcept override { return NodeKind::Literal; }
    [[nodiscard]] QString toString() const override;
    [[nodiscard]] OperandType valueType() const override { return m_type; }

    [[nodiscard]] qint64 intValue() const { return m_int; }
    [[nodiscard]] const QString& strValue() const { return m_str; }
    [[nodiscard]] ExpressionToken token() const { return m_token; }

private:
    ExpressionToken m_token;
    OperandType m_type = OperandType::Int;
    qint64  m_int = 0;
    QString m_str;
};

// VariableTerm：变量（可带下标）。类型：$前缀/已知字符串变量为 Str，否则 Int。
class VariableNode : public ExpressionNode {
public:
    explicit VariableNode(const QString& name, OperandType type = OperandType::Int);

    [[nodiscard]] NodeKind kind() const noexcept override { return NodeKind::Variable; }
    [[nodiscard]] QString toString() const override;
    [[nodiscard]] OperandType valueType() const override { return m_type; }
    void setValueType(OperandType t) { m_type = t; }

    [[nodiscard]] const QString& name() const { return m_name; }
    [[nodiscard]] bool isArray() const { return !m_indices.isEmpty(); }

    void addIndex(QSharedPointer<ExpressionNode> index);
    [[nodiscard]] const QList<QSharedPointer<ExpressionNode>>& indices() const { return m_indices; }

private:
    QString m_name;
    OperandType m_type;
    QList<QSharedPointer<ExpressionNode>> m_indices;
};

// OperatorTerm（二元）
class BinaryOpNode : public ExpressionNode {
public:
    BinaryOpNode(QSharedPointer<ExpressionNode> left,
                 const ExpressionToken& op,
                 QSharedPointer<ExpressionNode> right);

    [[nodiscard]] NodeKind kind() const noexcept override { return NodeKind::BinaryOp; }
    [[nodiscard]] QString toString() const override;
    // 类型按需计算：子节点类型（如变量表回填）变化后仍保持一致
    [[nodiscard]] OperandType valueType() const override {
        const OperandType lt = m_left ? m_left->valueType() : OperandType::Unknown;
        const OperandType rt = m_right ? m_right->valueType() : OperandType::Unknown;
        return inferBinaryType(m_op.type(), lt, rt).value_or(OperandType::Int);
    }
    [[nodiscard]] bool typesValid() const {
        const OperandType lt = m_left ? m_left->valueType() : OperandType::Unknown;
        const OperandType rt = m_right ? m_right->valueType() : OperandType::Unknown;
        return inferBinaryType(m_op.type(), lt, rt).has_value();
    }

    [[nodiscard]] QSharedPointer<ExpressionNode> left() const { return m_left; }
    [[nodiscard]] ExpressionToken op() const { return m_op; }
    [[nodiscard]] QSharedPointer<ExpressionNode> right() const { return m_right; }

private:
    QSharedPointer<ExpressionNode> m_left;
    ExpressionToken m_op;
    QSharedPointer<ExpressionNode> m_right;
};

// 一元运算符
class UnaryOpNode : public ExpressionNode {
public:
    UnaryOpNode(const ExpressionToken& op, QSharedPointer<ExpressionNode> operand);

    [[nodiscard]] NodeKind kind() const noexcept override { return NodeKind::UnaryOp; }
    [[nodiscard]] QString toString() const override;
    [[nodiscard]] OperandType valueType() const override {
        const OperandType ot = m_operand ? m_operand->valueType() : OperandType::Unknown;
        return inferUnaryType(m_op.type(), ot).value_or(OperandType::Int);
    }
    [[nodiscard]] bool typesValid() const {
        const OperandType ot = m_operand ? m_operand->valueType() : OperandType::Unknown;
        return inferUnaryType(m_op.type(), ot).has_value();
    }

    [[nodiscard]] ExpressionToken op() const { return m_op; }
    [[nodiscard]] QSharedPointer<ExpressionNode> operand() const { return m_operand; }

private:
    ExpressionToken m_op;
    QSharedPointer<ExpressionNode> m_operand;
};

// FunctionMethodTerm：函数调用
//
// 解析期把调用解析成三类（对齐 C# IdentifierDictionary.GetFunctionMethod）：
//   * 内置函数（methodDic 命中）        -> isBuiltin()，返回类型来自 kBuiltinFunctions，
//                                          并复刻 FunctionMethod.CheckArgumentType() 校验；
//   * 用户自定义函数（#FUNCTION(S)）    -> isUserFunction()，返回类型由 Provider 提供；
//   * 未定义函数                        -> 两者皆否，arityError() = 「未定义的函数」。
class FunctionNode : public ExpressionNode {
public:
    FunctionNode(const QString& name, const QList<QSharedPointer<ExpressionNode>>& args);

    [[nodiscard]] NodeKind kind() const noexcept override { return NodeKind::Function; }
    [[nodiscard]] QString toString() const override;
    [[nodiscard]] OperandType valueType() const override { return m_type; }
    void setValueType(OperandType t) { m_type = t; }
    [[nodiscard]] bool isUserFunction() const { return m_isUserFunction; }
    void setUserFunction(bool v) { m_isUserFunction = v; }

    // ---- 内置函数（内部命令）----
    [[nodiscard]] bool isBuiltin() const noexcept { return m_builtinIndex >= 0; }
    [[nodiscard]] int  builtinIndex() const noexcept { return m_builtinIndex; }
    void setBuiltinIndex(int index) noexcept { m_builtinIndex = index; }
    [[nodiscard]] const BuiltinFunctionSpec* builtinSpec() const noexcept {
        return m_builtinIndex >= 0 ? &kBuiltinFunctions[m_builtinIndex] : nullptr;
    }
    [[nodiscard]] BuiltinOp builtinOp() const noexcept {
        const BuiltinFunctionSpec* s = builtinSpec();
        return s ? s->op : BuiltinOp::None;
    }

    // 解析期参数校验错误（对齐 C# CheckArgumentType 的返回消息）；空串 = 通过
    [[nodiscard]] const QString& arityError() const noexcept { return m_arityError; }
    void setArityError(const QString& error) { m_arityError = error; }

    // 名字（原始写法；调用前统一 toUpper() 比较，对齐 Config.ICFunction）
    [[nodiscard]] const QString& name() const { return m_name; }
    [[nodiscard]] const QList<QSharedPointer<ExpressionNode>>& arguments() const { return m_args; }

private:
    QString m_name;
    QList<QSharedPointer<ExpressionNode>> m_args;
    OperandType m_type = OperandType::Int;   // 默认整数（C# 未标注时的宽松处理）
    bool m_isUserFunction = false;
    int  m_builtinIndex = -1;                // kBuiltinFunctions 下标；-1 = 非内置
    QString m_arityError;                    // 空串 = 参数校验通过
};

// ---------------------------------------------------------------------------
// 内置函数调用校验（对齐 C# FunctionMethod.CheckArgumentType）
//
//   返回空串表示通过；否则返回可直接并入解析告警的中文消息。
//   `args` 已由解析器归约为强类型 AST（变量类型由 VariableTable 回填）。
// ---------------------------------------------------------------------------
[[nodiscard]] QString validateBuiltinCall(const BuiltinFunctionSpec& spec,
                                          const QList<QSharedPointer<ExpressionNode>>& args);

// 解析某个函数调用名：内置 / 用户 / 未定义。
//   userTypeProvider 为 nullptr 或返回 Unknown 视为非用户函数。
struct FunctionResolution {
    bool     isBuiltin = false;
    bool     isUserFunction = false;
    int      builtinIndex = -1;
    OperandType returnType = OperandType::Unknown;
};
[[nodiscard]] FunctionResolution resolveFunctionCall(
    const QString& name,
    const std::function<OperandType(const QString&)>& userTypeProvider);

// ---------------------------------------------------------------------------
// AST 遍历（前序深度优先）——用于解析期校验/类型回填等全局扫描。
// ---------------------------------------------------------------------------
void walkExpression(ExpressionNode& node,
                    const std::function<void(ExpressionNode&)>& visit);

// 三目：cond ? then # else
class IfNode : public ExpressionNode {
public:
    IfNode(QSharedPointer<ExpressionNode> condition,
           QSharedPointer<ExpressionNode> thenExpr,
           QSharedPointer<ExpressionNode> elseExpr = nullptr);

    [[nodiscard]] NodeKind kind() const noexcept override { return NodeKind::If; }
    [[nodiscard]] QString toString() const override;
    [[nodiscard]] OperandType valueType() const override {
        const OperandType tt = m_thenExpr ? m_thenExpr->valueType() : OperandType::Unknown;
        const OperandType et = m_elseExpr ? m_elseExpr->valueType() : OperandType::Unknown;
        if (isKnown(tt) && isKnown(et)) return tt == et ? tt : OperandType::Int;
        if (isKnown(tt)) return tt;
        if (isKnown(et)) return et;
        return OperandType::Int;
    }

    [[nodiscard]] QSharedPointer<ExpressionNode> condition() const { return m_condition; }
    [[nodiscard]] QSharedPointer<ExpressionNode> thenExpr() const { return m_thenExpr; }
    [[nodiscard]] QSharedPointer<ExpressionNode> elseExpr() const { return m_elseExpr; }

private:
    QSharedPointer<ExpressionNode> m_condition;
    QSharedPointer<ExpressionNode> m_thenExpr;
    QSharedPointer<ExpressionNode> m_elseExpr;
};

// ---------------------------------------------------------------------------
// StrForm（格式化串）：对齐 C# StrForm/StrFormPart + FormattedStringMethod
//   文本片段 + 内嵌表达式片段（{expr} / %expr%）
// ---------------------------------------------------------------------------
enum class StrFormPartType { Text, Expression };

struct StrFormPart {
    StrFormPartType type = StrFormPartType::Text;
    QString text;                                   // Text
    QSharedPointer<ExpressionNode> expression;      // Expression

    static StrFormPart makeText(const QString& t) { return StrFormPart{StrFormPartType::Text, t, nullptr}; }
    static StrFormPart makeExpr(QSharedPointer<ExpressionNode> e) {
        return StrFormPart{StrFormPartType::Expression, QString(), std::move(e)};
    }
};

class StrFormNode : public ExpressionNode {
public:
    explicit StrFormNode(QList<StrFormPart> parts);

    [[nodiscard]] NodeKind kind() const noexcept override { return NodeKind::StrForm; }
    [[nodiscard]] QString toString() const override;
    [[nodiscard]] OperandType valueType() const override { return OperandType::Str; }

    [[nodiscard]] const QList<StrFormPart>& parts() const { return m_parts; }
    [[nodiscard]] bool isConst() const;   // 所有内嵌表达式均为字面量（供常量折叠）

private:
    QList<StrFormPart> m_parts;
};

// C# 命名别名（便于对照阅读）
using OperandTerm = ExpressionNode;   // IOperandTerm
using SingleTerm  = LiteralNode;      // SingleTerm
using VariableTerm = VariableNode;    // VariableTerm
using StrFormTerm = StrFormNode;      // StrFormTerm

#endif // EXPRESSION_AST_H
