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
#include <atomic>
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

// 这些名字在求值器里有专门分支（伪变量 / 系统数组 / 常数折叠 / LASTLOAD 元数据 /
// RESULTS 语义），分词期不判它们的访问方式 —— 留给运行期完整解析（旧路）。
bool isDynamicallyResolvedName(const QString& upper) {
    static const QSet<QString> kNames = {
        QStringLiteral("RAND"), QStringLiteral("RANDDATA"),
        QStringLiteral("LINECOUNT"), QStringLiteral("EMUERA_VERSION"),
        QStringLiteral("__INT_MAX__"), QStringLiteral("__INT_MIN__"),
        QStringLiteral("GAMEBASE"), QStringLiteral("RESULTS"),
        QStringLiteral("SAVEDATA_TEXT"),
        QStringLiteral("LASTLOAD_NO"), QStringLiteral("LASTLOAD_VERSION"),
        QStringLiteral("LASTLOAD_TEXT"),
    };
    return upper.startsWith(QLatin1String("GAMEBASE_")) || kNames.contains(upper);
}

// ---- 调试统计（默认关闭）----
// EMUERA_KIND_STATS=1 时按 kind 数「变量分词」的判定结果；dump_warnings
// --kind-stats 打印后清零。只用于核对 Dynamic 占比，不在默认路径上。
namespace {
std::atomic<qint64> g_accessKindCount[16];
bool kindStatsEnabled() {
    static const bool on = qEnvironmentVariableIsSet("EMUERA_KIND_STATS");
    return on;
}
const char* accessKindName(VariableNode::AccessKind k) {
    switch (k) {
    case VariableNode::AccessKind::Dynamic:    return "Dynamic";
    case VariableNode::AccessKind::GlobalInt1: return "GlobalInt1";
    case VariableNode::AccessKind::GlobalInt2: return "GlobalInt2";
    case VariableNode::AccessKind::GlobalInt3: return "GlobalInt3";
    case VariableNode::AccessKind::GlobalStr1: return "GlobalStr1";
    case VariableNode::AccessKind::GlobalStr2: return "GlobalStr2";
    case VariableNode::AccessKind::GlobalStr3: return "GlobalStr3";
    case VariableNode::AccessKind::LocalSlot:  return "LocalSlot";
    case VariableNode::AccessKind::ArgSlot:    return "ArgSlot";
    case VariableNode::AccessKind::LocalStr:   return "LocalStr";
    case VariableNode::AccessKind::ArgStr:     return "ArgStr";
    case VariableNode::AccessKind::Param:      return "Param";
    case VariableNode::AccessKind::Pseudo:     return "Pseudo";
    case VariableNode::AccessKind::System:     return "System";
    case VariableNode::AccessKind::Chara:      return "Chara";
    case VariableNode::AccessKind::Const:      return "Const";
    }
    return "?";
}
// 析构即计数：computeVariableAccess 有多处提前 return，用作用域守卫统一收口。
struct KindCounter {
    const VariableNode& var;
    ~KindCounter() {
        if (kindStatsEnabled())
            g_accessKindCount[int(var.accessKind)].fetch_add(1, std::memory_order_relaxed);
    }
};
} // namespace

// 装载期「变量分词」：一次算出运行期的访问方式（kind + 静态存储键）。
// **类别**在装载期定型（只有「取哪个槽/哪个绑定/哪个角色号」留给运行期）；
// 真正判不出类别的才留 Dynamic。判定顺序 = 运行期旧路的早退顺序（优先级）：
//   形参 > 伪变量 > 系统槽 > LOCAL/ARG 槽位 > 角色/常数 > 表项
// 等价性依据：执行到本节点时，stepOnce 按 line.ownerFunction 设私有作用域，
// 与本函数静态判定的作用域恒同；m_references 只可能是**本函数** REF 形参名
//（已被形参过滤）。私有名 -> FUNC\x1fNAME 复合键与 setPrivateScope 的映射一致
//（两边都走 eravar::makePrivateStorageKey / internName，唯一构造点）。
void computeVariableAccess(VariableNode& var, const VariableTable& table,
                           const QString& function, const QSet<QString>* paramNames) {
    var.accessKind = VariableNode::AccessKind::Dynamic;
    var.accessKey = {};
    const KindCounter counter{var};
    const QString upper = nk(var.name());
    // 1) 形参：值绑定 / ARG 别名 / REF 引用绑定都是运行期的 —— 但**类别**已定。
    if (paramNames && paramNames->contains(upper)) {
        var.accessKind = VariableNode::AccessKind::Param; return;
    }
    // 2) 伪变量（求值器有专门分支：RAND/RANDDATA/LINECOUNT/GAMEBASE_*/RESULTS/…）
    if (isDynamicallyResolvedName(upper)) {
        var.accessKind = VariableNode::AccessKind::Pseudo; return;
    }
    // 3) 「有存储通道」的系统变量名：运行期读写一律先走系统槽（readLhs/writeLhs/
    // evaluateVariable 旧路径都是 hasSystemVariable 优先），脚本里的 #DIM/VARI
    // 同名私有声明遮蔽不了系统槽 —— 类别定 System，槽位选择仍是运行期的事
    //（sysvar::systemStorageNames 与运行期 hasSystemVariable 共用同一份名单）。
    if (sysvar::systemStorageNames().contains(upper)) {
        var.accessKind = VariableNode::AccessKind::System; return;
    }
    // 4) LOCAL / ARG / LOCALS / ARGS 槽位（形参已过滤；这些名字不在系统/角色表里）
    if (upper == QLatin1String("LOCAL")) {
        var.accessKind = VariableNode::AccessKind::LocalSlot; return;
    }
    if (upper == QLatin1String("ARG")) {
        var.accessKind = VariableNode::AccessKind::ArgSlot; return;
    }
    if (upper == QLatin1String("LOCALS")) {
        var.accessKind = VariableNode::AccessKind::LocalStr; return;
    }
    if (upper == QLatin1String("ARGS")) {
        var.accessKind = VariableNode::AccessKind::ArgStr; return;
    }
    // 5) 角色数据变量 / 常数：类别已定（chara 槽位翻译、常数折叠仍走运行期链）
    if (var.characterData) {
        var.accessKind = VariableNode::AccessKind::Chara; return;
    }
    if (var.readOnly) {
        var.accessKind = VariableNode::AccessKind::Const; return;
    }
    const auto* d = table.find(var.name(), function);
    if (!d) {
        // 未声明名：无表项时才可能是「裸用户槽」。若带系统元数据（TA/TB/CDFLAG
        // 之类），旧路有系统特判 —— 类别/槽位都判不出来，留 Dynamic（唯一剩下的
        // 一类：它既不是系统槽也不是角色声明，只是有元数据的裸名）。维数视作 1。
        if (sysvar::systemVariableMetadataDyn(var.name().toStdString()).type != OperandType::Unknown)
            return;
        var.accessKind = VariableNode::AccessKind::GlobalInt1;   // 类型未知的裸名按整型 1 维
        var.accessKey.name = eravar::internName(upper);
        return;
    }
    // 6) 表项：常数 / 角色数据 / REF 形参 —— 各自的类别（REF 归 Param，不另立）
    if (d->isConst) {
        var.accessKind = VariableNode::AccessKind::Const; return;
    }
    if (d->isCharaData) {
        var.accessKind = VariableNode::AccessKind::Chara; return;
    }
    if (d->isReference) {
        var.accessKind = VariableNode::AccessKind::Param; return;
    }
    const bool isStr = d->type == OperandType::Str;
    // 复合键的判定必须与运行期作用域映射一致：**只有函数体内声明的局部**
    //（scope == Local）会被 setPrivateScope 重映射成 FUNC\x1fNAME；头文件级
    // #DIM 虽带 isPrivate 标志，但登记在 m_globals、不进任何函数的映射表，
    // 运行期按裸名解析（与 restoreDeclaredDefaults 的键构造同口径）。
    const bool functionLocal = d->scope == VarScope::Local && !d->scopeKey.isEmpty();
    // 身份 = intern id（scope 0 = 裸键）：setPrivateScope 用同一张表登记
    // 作用域名/私有名，两边整数相等 <=> 字符串逐字节相等。
    var.accessKey.name = eravar::internName(upper);
    var.accessKey.scope = functionLocal ? eravar::internName(d->scopeKey) : 0;
    const int dim = qMax(1, d->dimension);
    var.accessKind = isStr
        ? (dim >= 3 ? VariableNode::AccessKind::GlobalStr3
                    : (dim == 2 ? VariableNode::AccessKind::GlobalStr2
                                : VariableNode::AccessKind::GlobalStr1))
        : (dim >= 3 ? VariableNode::AccessKind::GlobalInt3
                    : (dim == 2 ? VariableNode::AccessKind::GlobalInt2
                                : VariableNode::AccessKind::GlobalInt1));
}

// 下标节点的装载期常量值（唯一写入点）。
// 前置条件：*idx 已在本趟定型完毕（accessKind 等就绪）—— 因此只在
// VariableNode 的子下标递归**之后**调用。形态判定走 index_bounds 的唯一一份
// isConstantIndexExpression；求值走装载层注入的 evalConst（与运行期同一
// ExpressionEvaluator），保证算出来的值与运行期逐位一致。
void fillConstantIndex(ExpressionNode& idx,
                       const indexbounds::ConstNameResolver& resolveConst,
                       const VariableTable::ConstantIndexEvaluator& evalConst) {
    // 快速路径 1：字面量直接就是值（语料里绝大多数下标）。这一步**不需要**
    // 装载期求值器 —— 直接用测试/局部路径（只 applyTypes、不注入求值器）也得
    // 到同一个字段，两趟判定因此永远只读字段，不再看 AST 形态。
    if (idx.kind() == NodeKind::Literal) {
        const auto& lit = static_cast<const LiteralNode&>(idx);
        if (lit.isString()) {
            idx.constantIndex = {};
            return;
        }
        idx.constantIndex.value = lit.intValue();
        idx.constantIndex.state = indexbounds::ConstantIndexState::Value;
        return;
    }
    // 快速路径 2：`-字面量`（旧路第二处模式匹配的形态）直接取负，同样不求值。
    if (idx.kind() == NodeKind::UnaryOp) {
        const auto& un = static_cast<const UnaryOpNode&>(idx);
        if (!un.isPostfix() && un.op().type() == TokenType::MINUS && un.operand()
            && un.operand()->kind() == NodeKind::Literal) {
            const auto& lit = static_cast<const LiteralNode&>(*un.operand());
            if (lit.isString()) {
                idx.constantIndex = {};
                return;
            }
            const qint64 v = lit.intValue();
            if (v != std::numeric_limits<qint64>::min()) {
                idx.constantIndex.value = -v;
                idx.constantIndex.state = indexbounds::ConstantIndexState::Value;
                return;
            }
        }
    }
    // 其余形态（常量名 / 算术组合）需要装载期求值：没有求值器时保持 Unknown
    // （清空是必要的：同一节点可能先按一个作用域定型、再按另一个作用域定型）。
    if (!evalConst) {
        idx.constantIndex = {};
        return;
    }
    if (!indexbounds::isConstantIndexExpression(idx, resolveConst)) {
        idx.constantIndex = {};
        return;
    }
    const std::optional<qint64> v = evalConst(idx);
    idx.constantIndex.value = v.value_or(0);
    idx.constantIndex.state = v.has_value() ? indexbounds::ConstantIndexState::Value
                                            : indexbounds::ConstantIndexState::Invalid;
}

// 递归把类型回填到 AST 的变量节点（其余节点仅递归）
void walkApply(ExpressionNode& node, const VariableTable& table, const QString& function,
               const QSet<QString>* paramNames,
               const VariableTable::ConstantIndexEvaluator& evalConst,
               const indexbounds::ConstNameResolver& resolveConst) {
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
        computeVariableAccess(var, table, function, paramNames);
        // 先递归（子下标定型）再回填常量值：isConstantIndexExpression 读的是
        // 子节点**定型后**的 accessKind，值是求值器按同一棵（已定型）树算的。
        for (auto& idx : var.indices()) {
            if (idx) walkApply(*idx, table, function, paramNames, evalConst, resolveConst);
        }
        for (auto& idx : var.indices()) {
            if (idx) fillConstantIndex(*idx, resolveConst, evalConst);
        }
        break;
    }
    case NodeKind::BinaryOp: {
        auto& op = static_cast<BinaryOpNode&>(node);
        if (op.left())  walkApply(*op.left(), table, function, paramNames, evalConst, resolveConst);
        if (op.right()) walkApply(*op.right(), table, function, paramNames, evalConst, resolveConst);
        break;
    }
    case NodeKind::UnaryOp: {
        auto& op = static_cast<UnaryOpNode&>(node);
        if (op.operand()) walkApply(*op.operand(), table, function, paramNames, evalConst, resolveConst);
        break;
    }
    case NodeKind::Function: {
        auto& fn = static_cast<FunctionNode&>(node);
        for (auto& a : fn.arguments()) {
            if (a) walkApply(*a, table, function, paramNames, evalConst, resolveConst);
        }
        break;
    }
    case NodeKind::If: {
        auto& n = static_cast<IfNode&>(node);
        if (n.condition()) walkApply(*n.condition(), table, function, paramNames, evalConst, resolveConst);
        if (n.thenExpr())  walkApply(*n.thenExpr(), table, function, paramNames, evalConst, resolveConst);
        if (n.elseExpr())  walkApply(*n.elseExpr(), table, function, paramNames, evalConst, resolveConst);
        break;
    }
    case NodeKind::StrForm: {
        auto& n = static_cast<StrFormNode&>(node);
        for (auto& p : n.parts()) {
            if (p.type == StrFormPartType::Expression && p.expression) {
                walkApply(*p.expression, table, function, paramNames, evalConst, resolveConst);
                if (p.width) walkApply(*p.width, table, function, paramNames, evalConst, resolveConst);
            }
        }
        break;
    }
    case NodeKind::Literal:
        break;
    }
}

} // namespace

// ---- 调试：访问描述符判定分布（默认关闭，见上方 KindCounter）----
QMap<QString, qint64> VariableTable::accessKindHistogram() {
    QMap<QString, qint64> out;
    for (int i = 0; i < 16; ++i) {
        const qint64 n = g_accessKindCount[i].load(std::memory_order_relaxed);
        if (n > 0) out.insert(QString::fromLatin1(accessKindName(VariableNode::AccessKind(i))), n);
    }
    return out;
}

void VariableTable::resetAccessKindHistogram() {
    for (int i = 0; i < 16; ++i) g_accessKindCount[i].store(0, std::memory_order_relaxed);
}

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
    // 插入即失效缓存（缓存是整表/每函数快照，add 在装载期/单测里都可能发生在
    // 缓存建立之后）：declarationsRef / localsOfRef / localNamesOfRef 一并清。
    // 装载期没有运行期调用方，缓存为空，清空是零成本的。
    const struct CacheInvalidator {
        VariableTable& t;
        ~CacheInvalidator() {
            t.m_declarationsCache.clear();
            t.m_localsRefCache.clear();
            t.m_localNamesRefCache.clear();
        }
    } invalidator{*this};
    const QString nameKey = nk(decl.name);
    if (decl.scope == VarScope::Local && !decl.scopeKey.isEmpty()) {
        // 段身份文本**大小写敏感**（含脚本名）：绝不 toUpper。
        auto& fmap = m_locals[decl.scopeKey];
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
        m_dimensionsResolved = false;   // 新声明还没求值过维数
        return DeclStatus::Added;
    }
    const auto existing = m_globals.constFind(nameKey);
    if (existing != m_globals.constEnd()) {
        return sameDeclaration(existing.value(), decl) ? DeclStatus::DuplicateSame
                                                       : DeclStatus::Conflict;
    }
    m_globals.insert(nameKey, decl);
    m_dimensionsResolved = false;   // 新声明还没求值过维数
    return DeclStatus::Added;
}

bool VariableTable::add(const VariableDecl& decl) {
    return addChecked(decl) == DeclStatus::Added;
}

const VariableDecl* VariableTable::find(const QString& name, const QString& scopeKey) const {
    if (!scopeKey.isEmpty()) {
        const auto fit = m_locals.constFind(scopeKey);
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

OperandType VariableTable::typeOf(const QString& name, const QString& scopeKey) const {
    if (const VariableDecl* d = find(name, scopeKey)) {
        return d->type;
    }
    if (scopeKey.isEmpty()) {
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

QList<VariableDecl> VariableTable::localsOf(const QString& scopeKey) const {
    QList<VariableDecl> out;
    const auto it = m_locals.constFind(scopeKey);
    if (it != m_locals.constEnd()) {
        for (const VariableDecl& d : it.value()) out.append(d);
    }
    return out;
}

const QList<VariableDecl>& VariableTable::localsOfRef(const QString& scopeKey) const {
    static const QList<VariableDecl> kEmpty;
    const QString& key = scopeKey;
    const auto it = m_localsRefCache.constFind(key);
    if (it != m_localsRefCache.constEnd()) return it.value();
    const QList<VariableDecl> decls = localsOf(scopeKey);
    if (decls.isEmpty()) return kEmpty;
    return *m_localsRefCache.insert(key, decls);
}

const QStringList& VariableTable::localNamesOfRef(const QString& scopeKey) const {
    static const QStringList kEmpty;
    const QString& key = scopeKey;
    const auto it = m_localNamesRefCache.constFind(key);
    if (it != m_localNamesRefCache.constEnd()) return it.value();
    QStringList names;
    for (const VariableDecl& decl : localsOfRef(scopeKey))
        if (!decl.isConst) names.append(decl.name);
    return *m_localNamesRefCache.insert(key, names);
}

// 装载期一次性建表：m_locals（声明）是唯一事实来源。
// 键/值的规范化与旧路径**逐字节一致**：旧 setPrivateScope 用 toUpper() 做名字键、
// 用 makePrivateStorageKey(fkey, name) 做值，这里同样走 toUpper()/唯一构造点。
// 与 localNamesOfRef 同一过滤口径（跳过 CONST）：常量由常数表取值，不是存储槽。
VariableTable::PrivateNameTable VariableTable::buildPrivateNameTable() const {
    PrivateNameTable table;
    table.reserve(m_locals.size());
    for (auto it = m_locals.constBegin(); it != m_locals.constEnd(); ++it) {
        const QString& function = it.key();   // m_locals 的键已是规范化（大写）形式
        QHash<QString, QString> names;
        names.reserve(it.value().size());
        for (const VariableDecl& decl : it.value()) {
            if (decl.isConst) continue;
            names.insert(decl.name.toUpper(),
                         eravar::makePrivateStorageKey(function, decl.name));   // function 已是段身份文本
        }
        // 全是 CONST 的函数没有存储槽：不建空表（查询侧 miss 与空表结果相同）。
        if (names.isEmpty()) continue;
        table.insert(eravar::internName(function), names);
    }
    return table;
}

void VariableTable::clear() {
    m_constInt.clear();
    m_constArray.clear();
    m_constStrArray.clear();
    m_constStr.clear();
    m_constExpr.clear();
    m_dimEvaluator = {};
    m_dimensionsResolved = false;
    m_globals.clear();
    m_locals.clear();
    m_localNameCount.clear();
    m_uniqueLocalType.clear();
    m_localsRefCache.clear();
    m_localNamesRefCache.clear();
    m_declarationsCache.clear();
}

void VariableTable::applyTypes(ExpressionNode& node, const VariableTable& table,
                               const QString& function, const QSet<QString>* paramNames,
                               const ConstantIndexEvaluator& evalConst) {
    // 常量名查询器在**一趟定型**里只建一次（captures 是引用，寿命覆盖整趟）。
    // 没有求值器时完全不建（不产生每节点的 std::function 分配）。
    static const indexbounds::ConstNameResolver kNoConstNames;
    if (!evalConst) {
        walkApply(node, table, function, paramNames, evalConst, kNoConstNames);
        return;
    }
    const indexbounds::ConstNameResolver resolveConst =
        [&table, &function](const QString& name) -> std::optional<qint64> {
        qint64 v = 0;
        if (table.constInt(name, v, function)) return v;
        return std::nullopt;
    };
    walkApply(node, table, function, paramNames, evalConst, resolveConst);
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
    // 装载期几何：维数 -> strides 只在这里算一次。运行期（VariableStorage 的
    // 多维容器、越界判定、VARSIZE）只读，不重新推导。
    const auto geometry = [](VariableDecl& d) {
        d.strides.clear();
        d.elementCount = 0;
        if (d.isReference) return;          // REF 没有自己的数组（别名目标才算）
        if (d.lengths.isEmpty()) { d.elementCount = 1; return; }
        qint64 total = 1;
        for (int n : d.lengths) {
            if (n <= 0) return;             // 维数未定稿：不分配、不注册形状
            total *= n;
            if (total > qint64(std::numeric_limits<int>::max())) return;
        }
        d.elementCount = total;
        if (d.lengths.size() >= 2) {
            d.strides.resize(d.lengths.size());
            int acc = 1;                    // 最后一维连续
            for (int i = d.lengths.size() - 1; i >= 0; --i) {
                d.strides[i] = acc;
                acc *= d.lengths.at(i);
            }
        }
    };
    const auto fix = [this, &geometry](VariableDecl& d, const QString& scope) {
        if (!d.lengthExprs.isEmpty()) {
            QList<int> lengths;
            lengths.reserve(d.lengthExprs.size());
            for (const QString& e : d.lengthExprs) {
                int v = 0;
                if (evalDim(e, v, scope) && v > 0) lengths.append(v);
                else lengths.append(0);   // 仍未知
            }
            d.lengths = lengths;
            d.dimension = lengths.isEmpty() ? 1 : lengths.size();
        }
        geometry(d);
    };

    for (auto& d : m_globals) fix(d, QString());
    for (auto it = m_locals.begin(); it != m_locals.end(); ++it) {
        // 局部声明的维数在**本函数**作用域内求值（可引用本函数的私有常量）
        for (auto& d : it.value()) fix(d, it.key());
    }
    m_localsRefCache.clear();   // 维数已重写，丢弃 localsOfRef 缓存
    m_localNamesRefCache.clear();
    m_declarationsCache.clear();
    m_dimensionsResolved = true;
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
