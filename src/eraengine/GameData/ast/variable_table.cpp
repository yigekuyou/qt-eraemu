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
#include "variable_table.h"
#include "system_variables.h"
#include <QStringList>

namespace {

// 递归把类型回填到 AST 的变量节点（其余节点仅递归）
void walkApply(ExpressionNode& node, const VariableTable& table, const QString& function) {
    switch (node.kind()) {
    case NodeKind::Variable: {
        auto& var = static_cast<VariableNode&>(node);
        const OperandType t = table.typeOf(var.name(), function);
        if (isKnown(t)) var.setValueType(t);
        for (auto& idx : var.indices()) {
            if (idx) walkApply(*idx, table, function);
        }
        break;
    }
    case NodeKind::BinaryOp: {
        auto& op = static_cast<BinaryOpNode&>(node);
        if (op.left())  walkApply(*op.left(), table, function);
        if (op.right()) walkApply(*op.right(), table, function);
        break;
    }
    case NodeKind::UnaryOp: {
        auto& op = static_cast<UnaryOpNode&>(node);
        if (op.operand()) walkApply(*op.operand(), table, function);
        break;
    }
    case NodeKind::Function: {
        auto& fn = static_cast<FunctionNode&>(node);
        for (auto& a : fn.arguments()) {
            if (a) walkApply(*a, table, function);
        }
        break;
    }
    case NodeKind::If: {
        auto& n = static_cast<IfNode&>(node);
        if (n.condition()) walkApply(*n.condition(), table, function);
        if (n.thenExpr())  walkApply(*n.thenExpr(), table, function);
        if (n.elseExpr())  walkApply(*n.elseExpr(), table, function);
        break;
    }
    case NodeKind::StrForm: {
        auto& n = static_cast<StrFormNode&>(node);
        for (auto& p : n.parts()) {
            if (p.type == StrFormPartType::Expression && p.expression) {
                walkApply(*p.expression, table, function);
            }
        }
        break;
    }
    case NodeKind::Literal:
        break;
    }
}

} // namespace

bool VariableTable::add(const VariableDecl& decl) {
    if (decl.name.isEmpty()) return false;
    if (decl.scope == VarScope::Local && !decl.function.isEmpty()) {
        auto& fmap = m_locals[decl.function];
        if (fmap.contains(decl.name)) return false;
        fmap.insert(decl.name, decl);
        // 维护反向索引（唯一性用于无函数上下文时的类型解析）
        const int c = m_localNameCount.value(decl.name, 0) + 1;
        m_localNameCount.insert(decl.name, c);
        if (c == 1) m_uniqueLocalType.insert(decl.name, decl.type);
        else m_uniqueLocalType.remove(decl.name);
        return true;
    }
    if (m_globals.contains(decl.name)) return false;
    m_globals.insert(decl.name, decl);
    return true;
}

const VariableDecl* VariableTable::find(const QString& name, const QString& function) const {
    if (!function.isEmpty()) {
        const auto fit = m_locals.constFind(function);
        if (fit != m_locals.constEnd()) {
            const auto it = fit.value().constFind(name);
            if (it != fit.value().constEnd()) return &it.value();
        }
    }
    const auto it = m_globals.constFind(name);
    return it == m_globals.constEnd() ? nullptr : &it.value();
}

bool VariableTable::contains(const QString& name) const {
    return m_globals.contains(name) || m_localNameCount.contains(name);
}

OperandType VariableTable::typeOf(const QString& name, const QString& function) const {
    if (const VariableDecl* d = find(name, function)) {
        return d->type;
    }
    if (function.isEmpty()) {
        // 无上下文：仅当该名字在全表局部中唯一出现时才采用（O(1) 反向索引）
        const OperandType local = m_uniqueLocalType.value(name, OperandType::Unknown);
        if (isKnown(local)) return local;
    }
    // 用户未声明 -> 回退到系统变量表（FLAG/CFLAG/RESULTS/GLOBALS/…）
    return sysvar::systemVariableType(name.toStdString());
}

int VariableTable::count() const {
    int n = m_globals.size();
    for (const auto& f : m_locals) n += f.size();
    return n;
}

QList<VariableDecl> VariableTable::declarations() const {
    QList<VariableDecl> out;
    out.reserve(count());
    for (const auto& d : m_globals) out.append(d);
    for (const auto& f : m_locals) {
        for (const auto& d : f) out.append(d);
    }
    return out;
}

void VariableTable::clear() {
    m_globals.clear();
    m_locals.clear();
    m_localNameCount.clear();
    m_uniqueLocalType.clear();
}

void VariableTable::applyTypes(ExpressionNode& node, const VariableTable& table,
                               const QString& function) {
    walkApply(node, table, function);
}

// ---------------------------------------------------------------------------
// 常数（#DIM CONST）与维数求值
// ---------------------------------------------------------------------------
void VariableTable::setConstInt(const QString& name, qint64 value) {
    if (!name.isEmpty()) m_constInt.insert(name, value);
}

void VariableTable::setConstStr(const QString& name, const QString& value) {
    if (!name.isEmpty()) m_constStr.insert(name, value);
}

bool VariableTable::constInt(const QString& name, qint64& out) const {
    const auto it = m_constInt.constFind(name);
    if (it == m_constInt.constEnd()) return false;
    out = it.value();
    return true;
}

bool VariableTable::evalDim(const QString& expr, int& out) const {
    bool ok = false;
    const int v = expr.toInt(&ok);
    if (ok) {
        out = v;
        return true;
    }
    // 常数名（#DIM CONST name = value）
    qint64 cv = 0;
    if (constInt(expr, cv)) {
        out = static_cast<int>(cv);
        return true;
    }
    // 形如 数字 +/- 数字 的简单算式（常数表不足以做完整常量折叠）
    return false;
}

void VariableTable::resolveDimensions() {
    const auto fix = [this](VariableDecl& d) {
        if (d.lengthExprs.isEmpty()) return;
        QList<int> lengths;
        lengths.reserve(d.lengthExprs.size());
        for (const QString& e : d.lengthExprs) {
            int v = 0;
            if (evalDim(e, v) && v > 0) lengths.append(v);
            else lengths.append(0);   // 仍未知
        }
        d.lengths = lengths;
        d.dimension = lengths.isEmpty() ? 1 : lengths.size();
    };

    for (auto& d : m_globals) fix(d);
    for (auto& f : m_locals) {
        for (auto& d : f) fix(d);
    }
}
