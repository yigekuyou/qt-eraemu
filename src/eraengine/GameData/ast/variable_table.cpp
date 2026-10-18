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
// perf：find/localsOf/typeOf 每次查表都经过这里 —— 只有**确实含小写字母**时
// 才真正 toUpper()（纯 CJK 名字 toUpper() 恒等，此前每次都要分配 + 逐字符转换；
// 判定口径与 eraUpperKey 一致：ASCII 小写直判，非 ASCII 走 QChar::isLower）。
QString nk(const QString& name) {
    for (const QChar c : name) {
        const ushort u = c.unicode();
        if ((u >= 'a' && u <= 'z') || (u >= 0x80 && c.isLower())) return name.toUpper();
    }
    return name;
}

// 递归把类型回填到 AST 的变量节点（其余节点仅递归）
void walkApply(ExpressionNode& node, const VariableTable& table, const QString& function) {
    switch (node.kind()) {
    case NodeKind::Variable: {
        auto& var = static_cast<VariableNode&>(node);
        const OperandType t = table.typeOf(var.name(), function);
        if (isKnown(t)) var.setValueType(t);
        if (const auto* d = table.find(var.name(), function)) {
            var.dimension = d->dimension; var.characterData = d->isCharaData;
            var.readOnly = d->isConst; var.lengths = d->isReference ? QList<int>() : d->lengths;
        } else {
            const auto meta = sysvar::systemVariableMetadataDyn(var.name().toStdString());
            if (meta.type != OperandType::Unknown) {
                var.dimension = meta.dimension;
                var.characterData = meta.characterData;
                var.readOnly = meta.readOnly;
                var.lengths.clear();
            }
        }
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
           && a.isReference == b.isReference && a.isCharaData == b.isCharaData
           && a.isSavedata == b.isSavedata && a.isGlobal == b.isGlobal;
}

} // namespace

VariableTable::DeclStatus VariableTable::addChecked(const VariableDecl& decl) {
    if (decl.name.isEmpty()) return DeclStatus::Conflict;
    // 插入即失效 declarationsRef 缓存（缓存是整表快照，add 在装载期/单测里
    // 都可能发生在缓存建立之后）。
    const struct CacheInvalidator {
        VariableTable& t;
        ~CacheInvalidator() { t.m_declarationsCache.clear(); }
    } invalidator{*this};
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

bool VariableTable::isPrivateOnlyName(const QString& name) const {
    const QString n = nk(name);
    return !m_globals.contains(n) && m_localNameCount.contains(n);
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
    return sysvar::systemVariableTypeDyn(name.toStdString());
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

// declarations() 的缓存版：重建一次是 18515 项（每项带 QString/QList 成员），
// saveShapes() 在一次存/读档里要调 2~4 次、resetForNewGame/applyBinarySave
// 各一次 —— 全部按引用共享。缓存失效点与 m_localsRefCache 相同
// （clear / 维数定稿），只允许装载完成后使用。
const QList<VariableDecl>& VariableTable::declarationsRef() const {
    if (m_declarationsCache.isEmpty()) m_declarationsCache = declarations();
    return m_declarationsCache;
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

void VariableTable::clear() {
    m_constInt.clear();
    m_constArray.clear();
    m_constStrArray.clear();
    m_constStr.clear();
    m_constExpr.clear();
    m_dimEvaluator = {};
    m_globals.clear();
    m_locals.clear();
    m_localNameCount.clear();
    m_uniqueLocalType.clear();
    m_localsRefCache.clear();
    m_localNamesRefCache.clear();
    m_declarationsCache.clear();
}

void VariableTable::applyTypes(ExpressionNode& node, const VariableTable& table,
                               const QString& function) {
    walkApply(node, table, function);
}

// ---------------------------------------------------------------------------
// 常数（#DIM CONST）与维数求值
//
// 常数与变量一样分广域/局部（见 variable_table.h 的说明）：函数内的
// `#DIM CONST X` 是该函数的私有常量，只有在本函数内可见。
// ---------------------------------------------------------------------------
bool VariableTable::shadowedByVariable(const QString& name, const QString& scope) const {
    // 本作用域声明的同名**可写变量**遮蔽同名常量。
    // C# IdentifierDictionary.GetVariableToken() 正是先查
    // `line.ParentLabelLine.GetPrivateVariable(key)`（当前函数的私有变量表），
    // 命中就直接返回——私有变量优先于任何同名常量。
    const VariableDecl* d = find(name, scope);
    return d && !d->isConst;
}

template <typename T>
const T* VariableTable::findScopedConst(const QHash<QString, QHash<QString, T>>& store,
                                        const QString& name, const QString& scope) const {
    if (shadowedByVariable(name, scope)) return nullptr;
    const QString n = nk(name);
    // 1) 当前作用域的私有常量
    if (!scope.isEmpty()) {
        const auto sit = store.constFind(nk(scope));
        if (sit != store.constEnd()) {
            const auto it = sit.value().constFind(n);
            if (it != sit.value().constEnd()) return &it.value();
        }
    }
    // 2) 广域常量（处处可见）
    const auto git = store.constFind(QString());
    if (git != store.constEnd()) {
        const auto it = git.value().constFind(n);
        if (it != git.value().constEnd()) return &it.value();
    }
    return nullptr;
}

void VariableTable::setConstInt(const QString& name, qint64 value, const QString& owner) {
    if (!name.isEmpty()) m_constInt[nk(owner)][nk(name)] = value;
}

void VariableTable::setConstStr(const QString& name, const QString& value, const QString& owner) {
    if (!name.isEmpty()) m_constStr[nk(owner)][nk(name)] = value;
}

bool VariableTable::constInt(const QString& name, qint64& out, const QString& scope) const {
    if (const qint64* v = findScopedConst(m_constInt, name, scope)) { out = *v; return true; }
    return false;
}

bool VariableTable::constStr(const QString& name, QString& out, const QString& scope) const {
    if (const QString* v = findScopedConst(m_constStr, name, scope)) { out = *v; return true; }
    return false;
}

bool VariableTable::evalDim(const QString& expr, int& out, const QString& scope) const {
    bool ok = false;
    const int v = expr.toInt(&ok);
    if (ok) {
        out = v;
        return true;
    }
    // 常数名（#DIM CONST name = value）—— 与维数声明同作用域才可见
    qint64 cv = 0;
    if (constInt(expr, cv, scope)) {
        out = static_cast<int>(cv);
        return true;
    }
    // 兜底：交给求值器做完整的常量折叠（`CLASS_NUM + 1` / `MAXBASE - 1` …）。
    // 由 EraParseTable 在常量表就绪后注入；未注入时保持旧行为（记 0）。
    if (m_dimEvaluator) {
        const qint64 v64 = m_dimEvaluator(expr, scope);
        if (v64 > 0 && v64 <= std::numeric_limits<int>::max()) {
            out = static_cast<int>(v64);
            return true;
        }
    }
    return false;
}

void VariableTable::resolveDimensions() {
    const auto fix = [this](VariableDecl& d, const QString& scope) {
        if (d.lengthExprs.isEmpty()) return;
        QList<int> lengths;
        lengths.reserve(d.lengthExprs.size());
        for (const QString& e : d.lengthExprs) {
            int v = 0;
            if (evalDim(e, v, scope) && v > 0) lengths.append(v);
            else lengths.append(0);   // 仍未知
        }
        d.lengths = lengths;
        d.dimension = lengths.isEmpty() ? 1 : lengths.size();
    };

    for (auto& d : m_globals) fix(d, QString());
    for (auto it = m_locals.begin(); it != m_locals.end(); ++it) {
        // 局部声明的维数在**本函数**作用域内求值（可引用本函数的私有常量）
        for (auto& d : it.value()) fix(d, it.key());
    }
    m_localsRefCache.clear();   // 维数已重写，丢弃 localsOfRef 缓存
    m_localNamesRefCache.clear();
    m_declarationsCache.clear();
    qDebug() << "[parse] 变量维数求值：全局" << m_globals.size() << "局部作用域" << m_locals.size()
             << "常量" << constCount() << "常量数组" << m_constArray.size();
}

void VariableTable::setConstArray(const QString& name, const QList<qint64>& values, const QString& owner)
{
    if (name.isEmpty()) return;
    m_constArray[nk(owner)][nk(name)] = values;
    // 只有**单元素**常量才当作标量常数参与表达式折叠（`#DIM CONST W = 12`）。
    // 多元素常量数组（`#DIM CONST K17C_RTF = 0,1p0,…`）**不**折叠：把整数组名
    // 折成首个元素会让 `FINDELEMENT(K17C_RTF, v)` 之类「把数组本身当变量用」的
    // 调用变成「实得表达式」，也会掩盖真正的下标缺失。数组访问走 constArrayAt()。
    if (values.size() == 1) m_constInt[nk(owner)][nk(name)] = values.first();
}

void VariableTable::setConstExprs(const QString& name, const QStringList& exprs, const QString& owner)
{
    m_constExpr[nk(owner)][nk(name)] = exprs;
}

QStringList VariableTable::constExprs(const QString& name, const QString& scope) const
{
    if (const QStringList* v = findScopedConst(m_constExpr, name, scope)) return *v;
    return {};
}

bool VariableTable::constArrayAt(const QString& name, int index, qint64& out, const QString& scope) const
{
    const QList<qint64>* vals = findScopedConst(m_constArray, name, scope);
    if (!vals || index < 0 || index >= vals->size()) return false;
    out = vals->at(index);
    return true;
}

int VariableTable::constArraySize(const QString& name, const QString& scope) const
{
    const QList<qint64>* vals = findScopedConst(m_constArray, name, scope);
    return vals ? vals->size() : 0;
}

void VariableTable::setConstStrArray(const QString& name, const QStringList& values, const QString& owner)
{
    if (!name.isEmpty() && !values.isEmpty()) m_constStrArray[nk(owner)][nk(name)] = values;
}

bool VariableTable::constStrArrayAt(const QString& name, int index, QString& out, const QString& scope) const
{
    const QStringList* vals = findScopedConst(m_constStrArray, name, scope);
    if (!vals || index < 0 || index >= vals->size()) return false;
    out = vals->at(index);
    return true;
}

int VariableTable::constStrArraySize(const QString& name, const QString& scope) const
{
    const QStringList* vals = findScopedConst(m_constStrArray, name, scope);
    return vals ? vals->size() : 0;
}

int VariableTable::constCount() const {
    int n = 0;
    for (auto it = m_constInt.constBegin(); it != m_constInt.constEnd(); ++it) n += it.value().size();
    for (auto it = m_constStr.constBegin(); it != m_constStr.constEnd(); ++it) n += it.value().size();
    return n;
}
