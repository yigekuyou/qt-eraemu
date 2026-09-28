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
#include "expression_ast.h"
#include "operator_table.h"
#include <cmath>
#include <QJsonArray>
#include <QJsonValue>

// 整数/进制字面量求值（对齐 C# LexicalAnalyzer.ReadInt64）：
//   0x.. 十六进制、0b.. 二进制；p/P 为 2 的幂指数、e/E 为 10 的幂指数（如 "1p0"）。
bool parseIntegerLiteral(const QString& text, qint64& out) {
    const QString s = text.trimmed();
    if (s.isEmpty()) return false;

    std::size_t i = 0;
    int base = 10;
    if (s.size() > 2 && s.at(0) == QLatin1Char('0')
        && (s.at(1) == QLatin1Char('x') || s.at(1) == QLatin1Char('X'))) {
        base = 16;
        i = 2;
    } else if (s.size() > 2 && s.at(0) == QLatin1Char('0')
               && (s.at(1) == QLatin1Char('b') || s.at(1) == QLatin1Char('B'))) {
        base = 2;
        i = 2;
    }

    bool ok = false;
    qint64 significand = 0;
    {
        const auto digitOk = [base](QChar c) {
            if (c.isDigit()) return base == 2 ? (c == QLatin1Char('0') || c == QLatin1Char('1')) : true;
            if (base == 16) {
                const QChar lower = c.toLower();
                return c.isLetter() && lower >= QLatin1Char('a') && lower <= QLatin1Char('f');
            }
            return false;
        };
        std::size_t j = i;
        while (j < static_cast<std::size_t>(s.size()) && digitOk(s.at(j))) ++j;
        const QString digits = s.mid(static_cast<int>(i), static_cast<int>(j - i));
        if (digits.isEmpty()) return false;
        significand = digits.toLongLong(&ok, base);
        if (!ok) return false;
        i = j;
    }

    // 指数
    int expBase = 0;
    if (i < static_cast<std::size_t>(s.size())) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('p') || c == QLatin1Char('P')) { expBase = 2; ++i; }
        else if (c == QLatin1Char('e') || c == QLatin1Char('E')) { expBase = 10; ++i; }
    }
    if (expBase == 0) {
        out = significand;
        return i == static_cast<std::size_t>(s.size());   // 尾部必须干净（如 "1.5" 不是整数字面量）
    }

    qint64 exponent = 0;
    {
        std::size_t j = i;
        if (j < static_cast<std::size_t>(s.size())
            && (s.at(j) == QLatin1Char('+') || s.at(j) == QLatin1Char('-'))) ++j;
        while (j < static_cast<std::size_t>(s.size()) && s.at(j).isDigit()) ++j;
        if (j == i) return false;
        exponent = s.mid(static_cast<int>(i), static_cast<int>(j - i)).toLongLong(&ok, 10);
        if (!ok) return false;
        i = j;
    }
    const double d = static_cast<double>(significand) * std::pow(static_cast<double>(expBase),
                                                                 static_cast<double>(exponent));
    out = static_cast<qint64>(d);
    return true;
}

namespace {
QString nodeKindName(NodeKind kind) {
    switch (kind) {
    case NodeKind::Literal: return QStringLiteral("Literal");
    case NodeKind::Variable: return QStringLiteral("Variable");
    case NodeKind::BinaryOp: return QStringLiteral("BinaryOp");
    case NodeKind::UnaryOp: return QStringLiteral("UnaryOp");
    case NodeKind::Function: return QStringLiteral("Function");
    case NodeKind::If: return QStringLiteral("If");
    case NodeKind::StrForm: return QStringLiteral("StrForm");
    }
    return QStringLiteral("Unknown");
}
QString typeName(OperandType type) {
    switch (type) {
    case OperandType::Int: return QStringLiteral("Int");
    case OperandType::Str: return QStringLiteral("Str");
    case OperandType::Void: return QStringLiteral("Void");
    default: return QStringLiteral("Unknown");
    }
}
QJsonObject astJson(const ExpressionNode& node) {
    QJsonObject out;
    out.insert(QStringLiteral("kind"), nodeKindName(node.kind()));
    out.insert(QStringLiteral("type"), typeName(node.valueType()));
    out.insert(QStringLiteral("staticType"), node.isStaticallyTyped());
    out.insert(QStringLiteral("text"), node.toString());
    QJsonArray children;
    if (node.kind() == NodeKind::Literal) {
        const auto& n = static_cast<const LiteralNode&>(node);
        if (n.isString()) out.insert(QStringLiteral("value"), n.strValue());
        else out.insert(QStringLiteral("value"), n.intValue());
    } else if (node.kind() == NodeKind::Variable) {
        const auto& n = static_cast<const VariableNode&>(node);
        out.insert(QStringLiteral("name"), n.name());
        out.insert(QStringLiteral("array"), n.isArray());
        for (const auto& child : n.indices()) if (child) children.append(astJson(*child));
    } else if (node.kind() == NodeKind::BinaryOp) {
        const auto& n = static_cast<const BinaryOpNode&>(node);
        out.insert(QStringLiteral("operator"), n.op().value());
        if (n.left()) children.append(astJson(*n.left()));
        if (n.right()) children.append(astJson(*n.right()));
    } else if (node.kind() == NodeKind::UnaryOp) {
        const auto& n = static_cast<const UnaryOpNode&>(node);
        out.insert(QStringLiteral("operator"), n.op().value());
        out.insert(QStringLiteral("postfix"), n.isPostfix());
        if (n.operand()) children.append(astJson(*n.operand()));
    } else if (node.kind() == NodeKind::Function) {
        const auto& n = static_cast<const FunctionNode&>(node);
        out.insert(QStringLiteral("name"), n.name());
        out.insert(QStringLiteral("builtin"), n.isBuiltin());
        out.insert(QStringLiteral("userFunction"), n.isUserFunction());
        out.insert(QStringLiteral("arityError"), n.arityError());
        for (const auto& child : n.arguments()) if (child) children.append(astJson(*child));
    } else if (node.kind() == NodeKind::If) {
        const auto& n = static_cast<const IfNode&>(node);
        if (n.condition()) children.append(astJson(*n.condition()));
        if (n.thenExpr()) children.append(astJson(*n.thenExpr()));
        if (n.elseExpr()) children.append(astJson(*n.elseExpr()));
    } else if (node.kind() == NodeKind::StrForm) {
        const auto& n = static_cast<const StrFormNode&>(node);
        for (const auto& part : n.parts()) {
            QJsonObject p;
            p.insert(QStringLiteral("part"), part.type == StrFormPartType::Text ? QStringLiteral("text") : QStringLiteral("expression"));
            if (part.type == StrFormPartType::Text) p.insert(QStringLiteral("text"), part.text);
            else if (part.expression) p.insert(QStringLiteral("ast"), astJson(*part.expression));
            children.append(p);
        }
    }
    if (!children.isEmpty()) out.insert(QStringLiteral("children"), children);
    return out;
}
}

QJsonObject expressionAstJson(const ExpressionNode& node) { return astJson(node); }

QString expressionAstDump(const ExpressionNode& node, int indent) {
    const QByteArray json = QJsonDocument(astJson(node)).toJson(QJsonDocument::Indented);
    Q_UNUSED(indent);
    return QString::fromUtf8(json);
}

// ---------------------------------------------------------------------------
// LiteralNode
// ---------------------------------------------------------------------------
LiteralNode::LiteralNode(const ExpressionToken& token)
    : m_token(token)
{
    if (token.type() == TokenType::STRING) {
        m_type = OperandType::Str;
        m_str = token.value();
    } else {
        m_type = OperandType::Int;
        qint64 v = 0;
        m_int = parseIntegerLiteral(token.value(), v) ? v : 0;
    }
}

LiteralNode::LiteralNode(qint64 value) : m_type(OperandType::Int), m_int(value) {}

LiteralNode::LiteralNode(const QString& value) : m_type(OperandType::Str), m_str(value) {}

QString LiteralNode::toString() const {
    if (m_type == OperandType::Str) return QStringLiteral("Literal(\"%1\")").arg(m_str);
    return QStringLiteral("Literal(%1)").arg(m_int);
}

// ---------------------------------------------------------------------------
// VariableNode
// ---------------------------------------------------------------------------
VariableNode::VariableNode(const QString& name, OperandType type)
    : m_name(name), m_type(type) {}

QString VariableNode::toString() const {
    QString s = QStringLiteral("Variable(%1").arg(m_name);
    for (const auto& idx : m_indices) {
        s += QStringLiteral(":") + (idx ? idx->toString() : QStringLiteral("?"));
    }
    s += QStringLiteral(")");
    return s;
}

void VariableNode::addIndex(QSharedPointer<ExpressionNode> index) {
    m_indices.append(std::move(index));
}

// ---------------------------------------------------------------------------
// BinaryOpNode（按运算符表推断类型）
// ---------------------------------------------------------------------------
BinaryOpNode::BinaryOpNode(QSharedPointer<ExpressionNode> left,
                           const ExpressionToken& op,
                           QSharedPointer<ExpressionNode> right)
    : m_left(std::move(left)), m_op(op), m_right(std::move(right))
{
}

QString BinaryOpNode::toString() const {
    return QStringLiteral("BinaryOp(%1, %2, %3)")
        .arg(m_left ? m_left->toString() : QStringLiteral("?"))
        .arg(m_op.value())
        .arg(m_right ? m_right->toString() : QStringLiteral("?"));
}

// ---------------------------------------------------------------------------
// UnaryOpNode
// ---------------------------------------------------------------------------
UnaryOpNode::UnaryOpNode(const ExpressionToken& op, QSharedPointer<ExpressionNode> operand,
                             bool postfix)
    : m_op(op), m_operand(std::move(operand)), m_postfix(postfix)
{
}

QString UnaryOpNode::toString() const {
    return QStringLiteral("UnaryOp(%1, %2)")
        .arg(m_op.value())
        .arg(m_operand ? m_operand->toString() : QStringLiteral("?"));
}

// ---------------------------------------------------------------------------
// FunctionNode
// ---------------------------------------------------------------------------
FunctionNode::FunctionNode(const QString& name, const QList<QSharedPointer<ExpressionNode>>& args)
    : m_name(name), m_args(args) {}

QString FunctionNode::toString() const {
    QString s = QStringLiteral("Function(%1").arg(m_name);
    for (const auto& a : m_args) {
        s += QStringLiteral(", ") + (a ? a->toString() : QStringLiteral("?"));
    }
    return s + QStringLiteral(")");
}

// ---------------------------------------------------------------------------
// 内置函数调用：解析 + 校验（对齐 C# IdentifierDictionary.GetFunctionMethod）
// ---------------------------------------------------------------------------
FunctionResolution resolveFunctionCall(const QString& name,
                                       const std::function<OperandType(const QString&)>& userTypeProvider) {
    FunctionResolution out;
    const QString upper = name.toUpper();

    // 1) 用户自定义函数优先（C#：labelDic.GetNonEventLabel(codeStr)，且 IsMethod）
    if (userTypeProvider) {
        const OperandType t = userTypeProvider(upper);
        if (isKnown(t)) {
            out.isUserFunction = true;
            out.returnType = t;
            return out;
        }
    }
    // 2) 内置函数（methodDic）
    const int index = builtinFunctionIndex(upper.toStdString());
    if (index >= 0) {
        out.isBuiltin = true;
        out.builtinIndex = index;
        out.returnType = kBuiltinFunctions[index].ret;
        return out;
    }
    // 3) 未定义
    return out;
}

namespace {

// 单个实参是否满足位置形态约束；ok=false 时写出期望/实得的说明。
bool argMatches(BuiltinArg want, const QSharedPointer<ExpressionNode>& arg, QString& why) {
    if (!arg) { why = QStringLiteral("实参缺失"); return false; }
    const OperandType t = arg->valueType();
    const bool isVar = (arg->kind() == NodeKind::Variable);
    const auto* var = isVar ? static_cast<const VariableNode*>(arg.get()) : nullptr;

    switch (want) {
    case BuiltinArg::Any:
        return true;
    case BuiltinArg::Int:
        if (isKnown(t) && t != OperandType::Int) {
            why = QStringLiteral("需要整型表达式，实得 %1").arg(QString::fromLatin1(operandTypeName(t)));
            return false;
        }
        return true;
    case BuiltinArg::Str:
        if (isKnown(t) && t != OperandType::Str) {
            why = QStringLiteral("需要字符串表达式，实得 %1").arg(QString::fromLatin1(operandTypeName(t)));
            return false;
        }
        return true;
    case BuiltinArg::Var:
    case BuiltinArg::VarInt:
    case BuiltinArg::VarStr:
    case BuiltinArg::VarArray:
    case BuiltinArg::VarIntArray:
    case BuiltinArg::VarChara:
        // C# 在这里还会检查 IsArray1D / IsCharacterData 等数组元数据；
        // 本移植的 AST 上只有「是不是变量 + 已知的 Int/Str 类型」，
        // 无法可靠判断数组维数/角色属性 —— 因此只校验「必须是变量」+ 类型，
        // 避免把 SUMCARRAY(初期貞操) 之类合法调用误报为错误。
        if (!isVar) { why = QStringLiteral("需要变量，实得表达式"); return false; }
        if ((want == BuiltinArg::VarInt || want == BuiltinArg::VarIntArray
             || want == BuiltinArg::VarChara)
            && isKnown(t) && t != OperandType::Int) {
            why = QStringLiteral("需要整型变量，实得 %1 变量").arg(QString::fromLatin1(operandTypeName(t)));
            return false;
        }
        if (want == BuiltinArg::VarStr && isKnown(t) && t != OperandType::Str) {
            why = QStringLiteral("需要字符串变量，实得 %1 变量").arg(QString::fromLatin1(operandTypeName(t)));
            return false;
        }
        Q_UNUSED(var);
        return true;
    }
    return true;
}

} // namespace

QString validateBuiltinCall(const BuiltinFunctionSpec& spec,
                            const QList<QSharedPointer<ExpressionNode>>& args) {
    const QString funcName = QString::fromUtf8(spec.name.data(), static_cast<int>(spec.name.size()));
    const int n = args.size();

    if (n < spec.minArgs) {
        return QStringLiteral("%1 参数过少（需要至少 %2 个，实得 %3）")
            .arg(funcName).arg(spec.minArgs).arg(n);
    }
    if (spec.maxArgs >= 0 && n > spec.maxArgs) {
        return QStringLiteral("%1 参数过多（最多 %2 个，实得 %3）")
            .arg(funcName).arg(spec.maxArgs).arg(n);
    }

    const int patternLength = static_cast<int>(spec.argPattern.size());
    const int checked = (patternLength < n) ? patternLength : n;
    for (int i = 0; i < checked; ++i) {
        const BuiltinArg want = builtinArgFromCode(spec.argPattern[static_cast<std::size_t>(i)]);
        if (want == BuiltinArg::Any) continue;
        QString why;
        if (!argMatches(want, args.at(i), why)) {
            return QStringLiteral("%1 第 %2 个参数%3").arg(funcName).arg(i + 1).arg(why);
        }
    }
    return QString();
}

// ---------------------------------------------------------------------------
// AST 遍历
// ---------------------------------------------------------------------------
void walkExpression(ExpressionNode& node, const std::function<void(ExpressionNode&)>& visit) {
    visit(node);
    switch (node.kind()) {
    case NodeKind::Literal:
        return;
    case NodeKind::Variable: {
        auto& v = static_cast<VariableNode&>(node);
        for (const auto& idx : v.indices()) {
            if (idx) walkExpression(*idx, visit);
        }
        return;
    }
    case NodeKind::BinaryOp: {
        auto& b = static_cast<BinaryOpNode&>(node);
        if (b.left())  walkExpression(*b.left(), visit);
        if (b.right()) walkExpression(*b.right(), visit);
        return;
    }
    case NodeKind::UnaryOp: {
        auto& u = static_cast<UnaryOpNode&>(node);
        if (u.operand()) walkExpression(*u.operand(), visit);
        return;
    }
    case NodeKind::Function: {
        auto& f = static_cast<FunctionNode&>(node);
        for (const auto& a : f.arguments()) {
            if (a) walkExpression(*a, visit);
        }
        return;
    }
    case NodeKind::If: {
        auto& i = static_cast<IfNode&>(node);
        if (i.condition()) walkExpression(*i.condition(), visit);
        if (i.thenExpr())  walkExpression(*i.thenExpr(), visit);
        if (i.elseExpr())  walkExpression(*i.elseExpr(), visit);
        return;
    }
    case NodeKind::StrForm: {
        auto& s = static_cast<StrFormNode&>(node);
        for (const StrFormPart& p : s.parts()) {
            if (p.type == StrFormPartType::Expression && p.expression) {
                walkExpression(*p.expression, visit);
                if (p.width) walkExpression(*p.width, visit);
            }
        }
        return;
    }
    }
}

// ---------------------------------------------------------------------------
// IfNode（三目：分支类型一致则取之）
// ---------------------------------------------------------------------------
IfNode::IfNode(QSharedPointer<ExpressionNode> condition,
               QSharedPointer<ExpressionNode> thenExpr,
               QSharedPointer<ExpressionNode> elseExpr)
    : m_condition(std::move(condition))
    , m_thenExpr(std::move(thenExpr))
    , m_elseExpr(std::move(elseExpr))
{
}

QString IfNode::toString() const {
    return QStringLiteral("If(%1, %2, %3)")
        .arg(m_condition ? m_condition->toString() : QStringLiteral("?"))
        .arg(m_thenExpr ? m_thenExpr->toString() : QStringLiteral("?"))
        .arg(m_elseExpr ? m_elseExpr->toString() : QStringLiteral("?"));
}

// ---------------------------------------------------------------------------
// StrFormNode
// ---------------------------------------------------------------------------
StrFormNode::StrFormNode(QList<StrFormPart> parts) : m_parts(std::move(parts)) {}

QString StrFormNode::toString() const {
    QString s = QStringLiteral("StrForm(");
    bool first = true;
    for (const StrFormPart& p : m_parts) {
        if (!first) s += QStringLiteral(" + ");
        first = false;
        if (p.type == StrFormPartType::Text) {
            s += QStringLiteral("\"%1\"").arg(p.text);
        } else {
            s += p.expression ? p.expression->toString() : QStringLiteral("?");
        }
    }
    return s + QStringLiteral(")");
}

bool StrFormNode::isConst() const {
    for (const StrFormPart& p : m_parts) {
        if (p.type == StrFormPartType::Expression && p.expression
            && dynamic_cast<const LiteralNode*>(p.expression.get()) == nullptr) {
            return false;
        }
    }
    return true;
}

QSharedPointer<ExpressionNode> cloneExpression(const QSharedPointer<ExpressionNode>& node) {
    if (!node) return {};
    switch (node->kind()) {
    case NodeKind::Literal: return QSharedPointer<LiteralNode>::create(static_cast<const LiteralNode&>(*node));
    case NodeKind::Variable: {
        const auto& n = static_cast<const VariableNode&>(*node);
        auto copy = QSharedPointer<VariableNode>::create(n.name(), n.valueType());
        for (const auto& i : n.indices()) copy->addIndex(cloneExpression(i));
        return copy;
    }
    case NodeKind::BinaryOp: {
        const auto& n = static_cast<const BinaryOpNode&>(*node);
        return QSharedPointer<BinaryOpNode>::create(cloneExpression(n.left()), n.op(), cloneExpression(n.right()));
    }
    case NodeKind::UnaryOp: {
        const auto& n = static_cast<const UnaryOpNode&>(*node);
        return QSharedPointer<UnaryOpNode>::create(n.op(), cloneExpression(n.operand()), n.isPostfix());
    }
    case NodeKind::Function: {
        const auto& n = static_cast<const FunctionNode&>(*node);
        QList<QSharedPointer<ExpressionNode>> args;
        for (const auto& a : n.arguments()) args.append(cloneExpression(a));
        auto copy = QSharedPointer<FunctionNode>::create(n.name(), args);
        copy->setValueType(n.valueType()); copy->setBuiltinIndex(n.builtinIndex());
        copy->setUserFunction(n.isUserFunction()); copy->setArityError(n.arityError()); return copy;
    }
    case NodeKind::If: {
        const auto& n = static_cast<const IfNode&>(*node);
        return QSharedPointer<IfNode>::create(cloneExpression(n.condition()), cloneExpression(n.thenExpr()), cloneExpression(n.elseExpr()));
    }
    case NodeKind::StrForm: {
        QList<StrFormPart> parts = static_cast<const StrFormNode&>(*node).parts();
        for (auto& p : parts) { p.expression = cloneExpression(p.expression); p.width = cloneExpression(p.width); }
        return QSharedPointer<StrFormNode>::create(parts);
    }
    }
    return {};
}
