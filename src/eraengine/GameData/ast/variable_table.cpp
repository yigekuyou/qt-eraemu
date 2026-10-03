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
#include <limits>
#include "system_variables.h"
#include <QDebug>
#include <QStringList>

namespace {

// Emuera 的 `大文字小文字の違いを無視する:YES`（ICVariable）语义：
// 标识符（变量名 / 函数名）大小写不敏感，内部统一用大写做键。
QString nk(const QString& name) { return name.toUpper(); }

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
                if (p.width) walkApply(*p.width, table, function);
            }
        }
        break;
    }
    case NodeKind::Literal:
        break;
    }
}

} // namespace

namespace {

// 两份声明是否等价（同名重复声明是否无害）
bool sameDeclaration(const VariableDecl& a, const VariableDecl& b) {
    return a.type == b.type && a.scope == b.scope && a.dimension == b.dimension
           && a.lengths == b.lengths && a.isConst == b.isConst
           && a.isReference == b.isReference && a.isCharaData == b.isCharaData;
}

} // namespace

VariableTable::DeclStatus VariableTable::addChecked(const VariableDecl& decl) {
    if (decl.name.isEmpty()) return DeclStatus::Conflict;
    const QString nameKey = nk(decl.name);
    if (decl.scope == VarScope::Local && !decl.function.isEmpty()) {
        auto& fmap = m_locals[nk(decl.function)];
        const auto existing = fmap.constFind(nameKey);
        if (existing != fmap.constEnd()) {
            // first-wins：保留首次声明（含其维数/初值），后续重复声明不再覆盖，
            // 否则 `#DIM A, 3` 之后再出现一个 `#DIM A` 会把数组尺寸抹掉。
            return sameDeclaration(existing.value(), decl) ? DeclStatus::DuplicateSame
                                                           : DeclStatus::Conflict;
        }
        fmap.insert(nameKey, decl);
        // 维护反向索引（唯一性用于无函数上下文时的类型解析）
        const int c = m_localNameCount.value(nameKey, 0) + 1;
        m_localNameCount.insert(nameKey, c);
        if (c == 1) m_uniqueLocalType.insert(nameKey, decl.type);
        else m_uniqueLocalType.remove(nameKey);
        return DeclStatus::Added;
    }
    const auto existing = m_globals.constFind(nameKey);
    if (existing != m_globals.constEnd()) {
        return sameDeclaration(existing.value(), decl) ? DeclStatus::DuplicateSame
                                                       : DeclStatus::Conflict;
    }
    m_globals.insert(nameKey, decl);
    return DeclStatus::Added;
}

bool VariableTable::add(const VariableDecl& decl) {
    return addChecked(decl) == DeclStatus::Added;
}

const VariableDecl* VariableTable::find(const QString& name, const QString& function) const {
    if (!function.isEmpty()) {
        const auto fit = m_locals.constFind(nk(function));
        if (fit != m_locals.constEnd()) {
            const auto it = fit.value().constFind(nk(name));
            if (it != fit.value().constEnd()) return &it.value();
        }
    }
    const auto it = m_globals.constFind(nk(name));
    return it == m_globals.constEnd() ? nullptr : &it.value();
}

bool VariableTable::contains(const QString& name) const {
    return m_globals.contains(nk(name)) || m_localNameCount.contains(nk(name));
}

OperandType VariableTable::typeOf(const QString& name, const QString& function) const {
    if (const VariableDecl* d = find(name, function)) {
        return d->type;
    }
    if (function.isEmpty()) {
        // 无上下文：仅当该名字在全表局部中唯一出现时才采用（O(1) 反向索引）
        const OperandType local = m_uniqueLocalType.value(nk(name), OperandType::Unknown);
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

QList<VariableDecl> VariableTable::localsOf(const QString& function) const {
    QList<VariableDecl> out;
    const auto it = m_locals.constFind(nk(function));
    if (it != m_locals.constEnd()) {
        for (const VariableDecl& d : it.value()) out.append(d);
    }
    return out;
}

const QList<VariableDecl>& VariableTable::localsOfRef(const QString& function) const {
    static const QList<VariableDecl> kEmpty;
    const QString key = nk(function);
    const auto it = m_localsRefCache.constFind(key);
    if (it != m_localsRefCache.constEnd()) return it.value();
    const QList<VariableDecl> decls = localsOf(function);
    if (decls.isEmpty()) return kEmpty;
    return *m_localsRefCache.insert(key, decls);
}

const QStringList& VariableTable::localNamesOfRef(const QString& function) const {
    static const QStringList kEmpty;
    const QString key = nk(function);
    const auto it = m_localNamesRefCache.constFind(key);
    if (it != m_localNamesRefCache.constEnd()) return it.value();
    QStringList names;
    for (const VariableDecl& decl : localsOfRef(function))
        if (!decl.isConst) names.append(decl.name);
    return *m_localNamesRefCache.insert(key, names);
}

bool VariableTable::constStr(const QString& name, QString& out) const {
    const auto it = m_constStr.constFind(nk(name));
    if (it != m_constStr.constEnd()) { out = it.value(); return true; }
    return false;
}

void VariableTable::clear() {
    m_globals.clear();
    m_locals.clear();
    m_localNameCount.clear();
    m_uniqueLocalType.clear();
    m_localsRefCache.clear();
    m_localNamesRefCache.clear();
}

void VariableTable::applyTypes(ExpressionNode& node, const VariableTable& table,
                               const QString& function) {
    walkApply(node, table, function);
}

// ---------------------------------------------------------------------------
// 常数（#DIM CONST）与维数求值
// ---------------------------------------------------------------------------
void VariableTable::setConstInt(const QString& name, qint64 value) {
    if (!name.isEmpty()) m_constInt.insert(nk(name), value);
}

void VariableTable::setConstStr(const QString& name, const QString& value) {
    if (!name.isEmpty()) m_constStr.insert(nk(name), value);
}

bool VariableTable::constInt(const QString& name, qint64& out) const {
    const auto it = m_constInt.constFind(nk(name));
    if (it != m_constInt.constEnd()) { out = it.value(); return true; }
    return false;
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
    // 兜底：交给求值器做完整的常量折叠（`CLASS_NUM + 1` / `MAXBASE - 1` …）。
    // 由 EraParseTable 在常量表就绪后注入；未注入时保持旧行为（记 0）。
    if (m_dimEvaluator) {
        const qint64 v64 = m_dimEvaluator(expr);
        if (v64 > 0 && v64 <= std::numeric_limits<int>::max()) {
            out = static_cast<int>(v64);
            return true;
        }
    }
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
    m_localsRefCache.clear();   // 维数已重写，丢弃 localsOfRef 缓存
    m_localNamesRefCache.clear();
    qDebug() << "[parse] 变量维数求值：全局" << m_globals.size() << "局部作用域" << m_locals.size()
             << "常量" << m_constInt.size() << "常量数组" << m_constArray.size();
}

void VariableTable::setConstArray(const QString& name, const QList<qint64>& values)
{
    m_constArray.insert(nk(name), values);
    if (!values.isEmpty()) m_constInt.insert(nk(name), values.first());
}

void VariableTable::setConstExprs(const QString& name, const QStringList& exprs)
{
    m_constExpr.insert(nk(name), exprs);
}

QStringList VariableTable::constExprs(const QString& name) const
{
    return m_constExpr.value(nk(name));
}

bool VariableTable::constArrayAt(const QString& name, int index, qint64& out) const
{
    const auto it = m_constArray.constFind(nk(name));
    if (it == m_constArray.constEnd()) return false;
    if (index < 0 || index >= it.value().size()) return false;
    out = it.value().at(index);
    return true;
}

int VariableTable::constArraySize(const QString& name) const
{
    const auto it = m_constArray.constFind(nk(name));
    return (it == m_constArray.constEnd()) ? 0 : it.value().size();
}

void VariableTable::setConstStrArray(const QString& name, const QStringList& values)
{
    if (!name.isEmpty() && !values.isEmpty()) m_constStrArray.insert(nk(name), values);
}

bool VariableTable::constStrArrayAt(const QString& name, int index, QString& out) const
{
    const auto it = m_constStrArray.constFind(nk(name));
    if (it == m_constStrArray.constEnd()) return false;
    if (index < 0 || index >= it.value().size()) return false;
    out = it.value().at(index);
    return true;
}

int VariableTable::constStrArraySize(const QString& name) const
{
    const auto it = m_constStrArray.constFind(nk(name));
    return (it == m_constStrArray.constEnd()) ? 0 : it.value().size();
}
