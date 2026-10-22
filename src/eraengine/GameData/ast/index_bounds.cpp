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
#include "index_bounds.h"

#include <QCoreApplication>

#include "expression_ast.h"

namespace indexbounds {

const Policy& issuePolicy(Kind kind) noexcept {
    // ★ 分工表：同一事实只允许一个生产者。3/6 产「运行期必然使该行失效」的
    // 事实（RejectLine），6/6 只补 3/6 看不见的形态（ReportOnly）。
    static const Policy table[] = {
        // TooManyIndices
        {DiagSeverity::Error, DiagEffect::RejectLine, DiagOrigin::Pass3},
        // DimensionMismatch
        {DiagSeverity::Error, DiagEffect::RejectLine, DiagOrigin::Pass3},
        // DeclaredLiteralOutOfRange
        {DiagSeverity::Error, DiagEffect::RejectLine, DiagOrigin::Pass3},
        // NegativeConstantIndex：-1 是 UnaryOp，3/6 的纯字面量检查看不见
        {DiagSeverity::Warning, DiagEffect::ReportOnly, DiagOrigin::Pass6},
        // SystemSizeExceeded：尺寸来自 VariableSize，3/6 的 lengths 被清空
        {DiagSeverity::Warning, DiagEffect::ReportOnly, DiagOrigin::Pass6},
        // UnknownStringIndex：运行期 resolveIndex 必抛
        {DiagSeverity::Error, DiagEffect::ReportOnly, DiagOrigin::Pass6},
        // ConstantDivisionByZero：3/6 的装载期求值已是运行期事实，由它禁行
        {DiagSeverity::Error, DiagEffect::RejectLine, DiagOrigin::Pass3},
        // DynamicDivisionByZero：除数 0 但被除数含变量，3/6 不求值
        {DiagSeverity::Error, DiagEffect::ReportOnly, DiagOrigin::Pass6},
        // ConstantIndexInvalid：3/6 用装载期求值判定，失败即运行期事实
        {DiagSeverity::Error, DiagEffect::RejectLine, DiagOrigin::Pass3},
    };
    static_assert(sizeof(table) / sizeof(table[0]) == static_cast<int>(Kind::ConstantIndexInvalid) + 1,
                  "indexbounds::Kind 新增了类别，issuePolicy 表必须同步");
    return table[static_cast<int>(kind)];
}

const char* kindName(Kind kind) noexcept {
    switch (kind) {
    case Kind::TooManyIndices:            return "too-many-indices";
    case Kind::DimensionMismatch:         return "dimension-mismatch";
    case Kind::DeclaredLiteralOutOfRange: return "declared-literal-out-of-range";
    case Kind::NegativeConstantIndex:     return "negative-constant-index";
    case Kind::SystemSizeExceeded:        return "system-size-exceeded";
    case Kind::UnknownStringIndex:        return "unknown-string-index";
    case Kind::ConstantDivisionByZero:    return "constant-division-by-zero";
    case Kind::DynamicDivisionByZero:     return "dynamic-division-by-zero";
    case Kind::ConstantIndexInvalid:      return "constant-index-invalid";
    }
    return "unknown";
}

int elementDimension(int indexPos, int dimension, bool characterData, int indexCount) noexcept {
    // 角色数据：只有「下标项数 > 声明维数」时第一个下标才是角色号位。
    // 写成一处，3/6 与 6/6 不再各算一遍。
    return indexPos - ((characterData && indexCount > dimension) ? 1 : 0);
}

bool isOutOfRange(qint64 value, int size) noexcept {
    return size > 0 && (value < 0 || value >= size);
}

bool pass3CoversLiteralBounds(int length) noexcept {
    return length > 0;
}

bool isConstantIndexExpression(const ExpressionNode& node, const ConstNameResolver& resolveConst) {
    switch (node.kind()) {
    case NodeKind::Literal:
        return !static_cast<const LiteralNode&>(node).isString();
    case NodeKind::Variable: {
        const auto& v = static_cast<const VariableNode&>(node);
        // 单个常量名（标量）才算；常量数组整体/普通变量/形参都不是常量下标。
        // accessKind 是收尾定型（walkApply）刚按**本作用域**算好的分类，
        // 用它把「绝大多数普通变量」在这里 O(1) 排除，不再查常数表。
        return !v.isArray()
            && v.accessKind == VariableNode::AccessKind::Const
            && resolveConst && resolveConst(v.name()).has_value();
    }
    case NodeKind::UnaryOp: {
        const auto& u = static_cast<const UnaryOpNode&>(node);
        const TokenType op = u.op().type();
        if (u.isPostfix() || op == TokenType::INCREMENT || op == TokenType::DECREMENT)
            return false;
        return u.operand() && isConstantIndexExpression(*u.operand(), resolveConst);
    }
    case NodeKind::BinaryOp: {
        const auto& b = static_cast<const BinaryOpNode&>(node);
        return b.left() && b.right()
            && isConstantIndexExpression(*b.left(), resolveConst)
            && isConstantIndexExpression(*b.right(), resolveConst);
    }
    case NodeKind::Function:
    case NodeKind::If:
    case NodeKind::StrForm:
        // 函数/三目/FORM 不在装载期折叠：它们可能读运行期状态，算「未知」。
        return false;
    }
    return false;
}

namespace {

bool hasDivisionByZero(const ExpressionNode& node) {
    switch (node.kind()) {
    case NodeKind::Literal:
    case NodeKind::Variable:
        return false;
    case NodeKind::BinaryOp: {
        const auto& b = static_cast<const BinaryOpNode&>(node);
        const TokenType t = b.op().type();
        if (t == TokenType::DIVIDE || t == TokenType::MODULO) {
            const auto& right = b.right();
            if (right && right->kind() == NodeKind::Literal) {
                const auto& lit = static_cast<const LiteralNode&>(*right);
                if (lit.valueType() == OperandType::Int && lit.intValue() == 0) return true;
            }
        }
        return (b.left() && hasDivisionByZero(*b.left()))
            || (b.right() && hasDivisionByZero(*b.right()));
    }
    case NodeKind::UnaryOp:
        return static_cast<const UnaryOpNode&>(node).operand()
            && hasDivisionByZero(*static_cast<const UnaryOpNode&>(node).operand());
    case NodeKind::Function: {
        for (const auto& arg : static_cast<const FunctionNode&>(node).arguments())
            if (arg && hasDivisionByZero(*arg)) return true;
        return false;
    }
    case NodeKind::If: {
        const auto& n = static_cast<const IfNode&>(node);
        return (n.condition() && hasDivisionByZero(*n.condition()))
            || (n.thenExpr() && hasDivisionByZero(*n.thenExpr()))
            || (n.elseExpr() && hasDivisionByZero(*n.elseExpr()));
    }
    case NodeKind::StrForm: {
        for (const StrFormPart& p : static_cast<const StrFormNode&>(node).parts()) {
            if (p.expression && hasDivisionByZero(*p.expression)) return true;
            if (p.width && hasDivisionByZero(*p.width)) return true;
        }
        return false;
    }
    }
    return false;
}

}  // namespace

bool hasConstantDivisionByZero(const ExpressionNode& node) {
    return hasDivisionByZero(node);
}

bool isConstantSubtree(const ExpressionNode& node) {
    bool constant = true;
    walkExpression(const_cast<ExpressionNode&>(node), [&](ExpressionNode& n) {
        if (n.kind() == NodeKind::Variable || n.kind() == NodeKind::Function) constant = false;
        if (n.kind() == NodeKind::UnaryOp) {
            const auto op = static_cast<const UnaryOpNode&>(n).op().type();
            if (op == TokenType::INCREMENT || op == TokenType::DECREMENT) constant = false;
        }
    });
    return constant;
}

QString tooManyIndicesMessage() {
    return QCoreApplication::translate("ParseDiagnostics", "变量下标超过三项");
}

QString dimensionMismatchMessage(const QString& name) {
    return QCoreApplication::translate("ParseDiagnostics",
        "变量 %1 缺少参数或下标维数错误").arg(name);
}

QString declaredLiteralOutOfRangeMessage(const QString& name) {
    return QCoreApplication::translate("ParseDiagnostics",
        "变量 %1 常量下标越界").arg(name);
}

QString negativeIndexMessage(const QString& name, qint64 value) {
    return QCoreApplication::translate("ParseDiagnostics",
        "变量 %1 的常量下标 %2 为负（下标下界恒为 0；运行期该访问静默失败）")
        .arg(name).arg(value);
}

QString beyondSizeMessage(const QString& name, qint64 value, int size) {
    return QCoreApplication::translate("ParseDiagnostics",
        "变量 %1 的常量下标 %2 越过其尺寸 %3（运行期读取为 0、写入被忽略）")
        .arg(name).arg(value).arg(size);
}

QString unknownStringIndexMessage(const QString& name, const QString& text) {
    return QCoreApplication::translate("ParseDiagnostics",
        "变量 %1 的字符串下标 \"%2\" 未在常数名表中定义（运行到该行会失败）")
        .arg(name, text);
}

QString divisionByZeroMessage() {
    return QCoreApplication::translate("ParseDiagnostics",
        "0 による除算が行われました（运行到该行会失败）");
}

QString constantEvalFailedMessage() {
    return QCoreApplication::translate("ParseDiagnostics", "装载期常量表达式求值失败");
}

}  // namespace indexbounds
