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
#include "ast_disk_cache.h"

#include "erb_loader.h"   // ParsedErbFile / LogicalLine / AST 节点

#include <algorithm>
#include <QDebug>
#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSharedPointer>
#include <QStandardPaths>

using NodePtr = QSharedPointer<ExpressionNode>;

namespace {

// ---- 解析结果的安全上界（防止损坏/恶意缓存文件导致 OOM）----
constexpr qint32 kMaxFiles   = 2'000'000;
constexpr qint32 kMaxLines   = 20'000'000;
constexpr qint32 kMaxNodes   = 20'000'000;
constexpr qint32 kMaxOperands = 1'000'000;
constexpr qint32 kMaxParts   = 1'000'000;

QString cacheDir() {
    static const QString dir = [] {
        const QByteArray env = qgetenv("EMUERA_AST_CACHE_DIR");
        QString base = env.isEmpty()
                           ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                           : QString::fromLocal8Bit(env);
        if (base.isEmpty()) base = QDir::tempPath() + QStringLiteral("/emuera-cache");
        QDir().mkpath(base);
        return base + QStringLiteral("/erb-ast");
    }();
    QDir().mkpath(dir);
    return dir;
}

QString filePathFor(const QString& key) {
    return cacheDir() + QLatin1Char('/') + key + QStringLiteral(".bin");
}

void collectConfigStamps(const QString& root, int maxDepth, QList<QString>& out) {
    if (maxDepth < 0) return;
    QDir d(root);
    if (!d.exists()) return;
    const QFileInfoList fis =
        d.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : fis) {
        const QString suffix = fi.suffix().toLower();
        if (suffix == QLatin1String("csv") || suffix == QLatin1String("config")) {
            out.append(fi.absoluteFilePath() + QLatin1Char('|')
                       + QString::number(fi.size()) + QLatin1Char('|')
                       + QString::number(fi.lastModified().toMSecsSinceEpoch()));
        }
    }
    if (maxDepth == 0) return;
    const QStringList subs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& s : subs) {
        collectConfigStamps(root + QLatin1Char('/') + s, maxDepth - 1, out);
    }
}

// ---------------------------------------------------------------------------
// 表达式节点表（每文件一份）：后序收集 → 子节点 id 恒小于父节点 id，
// 于是「先读子、后读父」的线性读回即可原样恢复共享（同一节点只写一次）。
// ---------------------------------------------------------------------------
void appendChildren(const ExpressionNode& n, QList<NodePtr>& out) {
    switch (n.kind()) {
    case NodeKind::Literal:
        break;
    case NodeKind::Variable: {
        const auto& v = static_cast<const VariableNode&>(n);
        for (const NodePtr& c : v.indices()) if (c) out.append(c);
        break;
    }
    case NodeKind::BinaryOp: {
        const auto& b = static_cast<const BinaryOpNode&>(n);
        if (b.left()) out.append(b.left());
        if (b.right()) out.append(b.right());
        break;
    }
    case NodeKind::UnaryOp: {
        const auto& u = static_cast<const UnaryOpNode&>(n);
        if (u.operand()) out.append(u.operand());
        break;
    }
    case NodeKind::Function: {
        const auto& f = static_cast<const FunctionNode&>(n);
        for (const NodePtr& a : f.arguments()) if (a) out.append(a);
        break;
    }
    case NodeKind::If: {
        const auto& i = static_cast<const IfNode&>(n);
        if (i.condition()) out.append(i.condition());
        if (i.thenExpr()) out.append(i.thenExpr());
        if (i.elseExpr()) out.append(i.elseExpr());
        break;
    }
    case NodeKind::StrForm: {
        const auto& s = static_cast<const StrFormNode&>(n);
        for (const StrFormPart& p : s.parts()) {
            if (p.expression) out.append(p.expression);
            if (p.width) out.append(p.width);
        }
        break;
    }
    }
}

struct NodeTable {
    QList<NodePtr> nodes;
    QHash<const ExpressionNode*, qint32> ids;
};

void collectPostOrder(const NodePtr& n, NodeTable& t) {
    if (!n || t.ids.contains(n.data())) return;
    QList<NodePtr> children;
    appendChildren(*n, children);
    for (const NodePtr& c : children) collectPostOrder(c, t);
    t.ids.insert(n.data(), qint32(t.nodes.size()));
    t.nodes.append(n);
}

inline qint32 idOf(const NodeTable& t, const NodePtr& n) {
    return n ? t.ids.value(n.data(), -1) : -1;
}

inline NodePtr refOf(const QList<NodePtr>& table, qint32 id) {
    return (id >= 0 && id < table.size()) ? table.at(id) : NodePtr();
}

void writeToken(QDataStream& out, const ExpressionToken& tok) {
    out << qint32(int(tok.type())) << tok.value() << qint32(tok.line()) << qint32(tok.column());
}

ExpressionToken readToken(QDataStream& in) {
    qint32 type = 0, line = 0, column = 0;
    QString value;
    in >> type >> value >> line >> column;
    return ExpressionToken(static_cast<TokenType>(type), value, line, column);
}

void writeNode(QDataStream& out, const ExpressionNode& n, const NodeTable& t) {
    out << quint8(n.kind());
    switch (n.kind()) {
    case NodeKind::Literal: {
        const auto& l = static_cast<const LiteralNode&>(n);
        writeToken(out, l.token());
        out << quint8(l.valueType()) << l.intValue() << l.strValue();
        break;
    }
    case NodeKind::Variable: {
        const auto& v = static_cast<const VariableNode&>(n);
        out << v.name() << quint8(v.valueType()) << qint32(v.indices().size());
        for (const NodePtr& c : v.indices()) out << idOf(t, c);
        break;
    }
    case NodeKind::BinaryOp: {
        const auto& b = static_cast<const BinaryOpNode&>(n);
        out << idOf(t, b.left());
        writeToken(out, b.op());
        out << idOf(t, b.right());
        break;
    }
    case NodeKind::UnaryOp: {
        const auto& u = static_cast<const UnaryOpNode&>(n);
        writeToken(out, u.op());
        out << bool(u.isPostfix()) << idOf(t, u.operand());
        break;
    }
    case NodeKind::Function: {
        const auto& f = static_cast<const FunctionNode&>(n);
        out << f.name() << quint8(f.valueType()) << bool(f.isUserFunction()) << f.arityError();
        const auto& args = f.arguments();
        out << qint32(args.size());
        for (const NodePtr& a : args) out << idOf(t, a);
        out << qint32(args.size());
        for (int i = 0; i < args.size(); ++i) out << bool(f.isArgOmitted(i));
        break;
    }
    case NodeKind::If: {
        const auto& i = static_cast<const IfNode&>(n);
        out << idOf(t, i.condition()) << idOf(t, i.thenExpr()) << idOf(t, i.elseExpr());
        break;
    }
    case NodeKind::StrForm: {
        const auto& s = static_cast<const StrFormNode&>(n);
        const QList<StrFormPart>& parts = s.parts();
        out << qint32(parts.size());
        for (const StrFormPart& p : parts) {
            out << quint8(p.type) << p.text << idOf(t, p.expression) << idOf(t, p.width)
                << bool(p.leftAlign);
        }
        break;
    }
    }
}

NodePtr readNode(QDataStream& in, const QList<NodePtr>& table) {
    quint8 kind = 0;
    in >> kind;
    switch (static_cast<NodeKind>(kind)) {
    case NodeKind::Literal: {
        const ExpressionToken tok = readToken(in);
        quint8 type = 0;
        qint64 iv = 0;
        QString sv;
        in >> type >> iv >> sv;
        if (in.status() != QDataStream::Ok) return NodePtr();
        // 有 token 的字面量按 token 重建（与解析期构造完全一致）；
        // 无 token 的（占位 0 / 常量折叠 / 显式值构造）按值重建。
        if (tok.type() != TokenType::UNKNOWN) return QSharedPointer<LiteralNode>::create(tok);
        if (static_cast<OperandType>(type) == OperandType::Str)
            return QSharedPointer<LiteralNode>::create(sv);
        return QSharedPointer<LiteralNode>::create(iv);
    }
    case NodeKind::Variable: {
        QString name;
        quint8 type = 0;
        qint32 cnt = 0;
        in >> name >> type >> cnt;
        if (in.status() != QDataStream::Ok || cnt < 0 || cnt > kMaxNodes) return NodePtr();
        auto v = QSharedPointer<VariableNode>::create(name, static_cast<OperandType>(type));
        for (qint32 i = 0; i < cnt; ++i) {
            qint32 id = -1;
            in >> id;
            v->addIndex(refOf(table, id));
        }
        return v;
    }
    case NodeKind::BinaryOp: {
        qint32 lid = -1;
        in >> lid;
        const ExpressionToken op = readToken(in);
        qint32 rid = -1;
        in >> rid;
        return QSharedPointer<BinaryOpNode>::create(refOf(table, lid), op, refOf(table, rid));
    }
    case NodeKind::UnaryOp: {
        const ExpressionToken op = readToken(in);
        bool postfix = false;
        qint32 oid = -1;
        in >> postfix >> oid;
        return QSharedPointer<UnaryOpNode>::create(op, refOf(table, oid), postfix);
    }
    case NodeKind::Function: {
        QString name, err;
        quint8 type = 0;
        bool isUser = false;
        qint32 cnt = 0;
        in >> name >> type >> isUser >> err >> cnt;
        if (in.status() != QDataStream::Ok || cnt < 0 || cnt > kMaxNodes) return NodePtr();
        QList<NodePtr> args;
        args.reserve(cnt);
        for (qint32 i = 0; i < cnt; ++i) {
            qint32 id = -1;
            in >> id;
            args.append(refOf(table, id));
        }
        auto f = QSharedPointer<FunctionNode>::create(name, args);
        f->setValueType(static_cast<OperandType>(type));
        f->setUserFunction(isUser);
        f->setArityError(err);
        qint32 omitted = 0;
        in >> omitted;
        if (omitted < 0 || omitted > kMaxNodes) return NodePtr();
        for (qint32 i = 0; i < omitted; ++i) {
            bool b = false;
            in >> b;
            if (b) f->markArgOmitted(i);
        }
        // builtinIndex / extensionSpec 由 finalizeParse::resolveFunctionNodes 重算
        return f;
    }
    case NodeKind::If: {
        qint32 c = -1, t = -1, e = -1;
        in >> c >> t >> e;
        return QSharedPointer<IfNode>::create(refOf(table, c), refOf(table, t), refOf(table, e));
    }
    case NodeKind::StrForm: {
        qint32 cnt = 0;
        in >> cnt;
        if (in.status() != QDataStream::Ok || cnt < 0 || cnt > kMaxParts) return NodePtr();
        QList<StrFormPart> parts;
        parts.reserve(cnt);
        for (qint32 i = 0; i < cnt; ++i) {
            StrFormPart p;
            quint8 ptype = 0;
            qint32 eid = -1, wid = -1;
            in >> ptype >> p.text >> eid >> wid >> p.leftAlign;
            p.type = static_cast<StrFormPartType>(ptype);
            p.expression = refOf(table, eid);
            p.width = refOf(table, wid);
            parts.append(p);
        }
        return QSharedPointer<StrFormNode>::create(parts);
    }
    }
    return NodePtr();
}

bool readNodeTable(QDataStream& in, QList<NodePtr>& table) {
    qint32 n = 0;
    in >> n;
    if (in.status() != QDataStream::Ok || n < 0 || n > kMaxNodes) return false;
    table.reserve(n);
    for (qint32 i = 0; i < n; ++i) {
        NodePtr p = readNode(in, table);
        if (!p || in.status() != QDataStream::Ok) return false;
        table.append(p);
    }
    return true;
}

void writeNodeTable(QDataStream& out, const NodeTable& t) {
    out << qint32(t.nodes.size());
    for (const NodePtr& n : t.nodes) writeNode(out, *n, t);
}

void collectFileNodes(const ParsedErbFile& pf, NodeTable& t) {
    for (const LogicalLine& line : pf.lines) {
        for (const Operand& op : line.arguments) collectPostOrder(op.ast, t);
        collectPostOrder(line.condition, t);
        if (line.printTemplate) {
            for (const PrintTemplatePart& p : line.printTemplate->parts) {
                collectPostOrder(p.expression, t);
                collectPostOrder(p.buttonValueExpression, t);
                for (auto it = p.attributes.constBegin(); it != p.attributes.constEnd(); ++it)
                    collectPostOrder(it.value(), t);
            }
        }
    }
}

// ---- PrintTemplate ----
void writeTemplate(QDataStream& out, const PrintTemplate& tpl, const NodeTable& t) {
    out << qint32(tpl.parts.size()) << qint32(tpl.alignment) << bool(tpl.noWrap);
    for (const PrintTemplatePart& p : tpl.parts) {
        out << quint8(p.kind) << p.text << p.imageAlt << idOf(t, p.expression)
            << p.style.color << p.style.buttonColor << p.style.fontName
            << bool(p.style.bold) << bool(p.style.italic) << bool(p.style.underline)
            << bool(p.style.strike) << p.buttonValue << idOf(t, p.buttonValueExpression);
        out << qint32(p.attributeOrder.size());
        for (const QString& k : p.attributeOrder) out << k << idOf(t, p.attributes.value(k));
        out << p.tooltip << qint32(p.width) << qint32(p.height) << p.shapeType;
        out << qint32(p.shapeParams.size());
        for (int v : p.shapeParams) out << qint32(v);
        out << bool(p.lockedPosition) << qint32(p.x) << qint32(p.y);
    }
}

bool readTemplate(QDataStream& in, const QList<NodePtr>& table, PrintTemplate& tpl) {
    qint32 nparts = 0, alignment = 0;
    bool noWrap = false;
    in >> nparts >> alignment >> noWrap;
    if (in.status() != QDataStream::Ok || nparts < 0 || nparts > kMaxParts) return false;
    tpl.alignment = alignment;
    tpl.noWrap = noWrap;
    tpl.parts.reserve(nparts);
    for (qint32 i = 0; i < nparts; ++i) {
        PrintTemplatePart p;
        quint8 kind = 0;
        qint32 eid = -1, bid = -1, orderCount = 0, w = 0, h = 0, shapeCount = 0, x = 0, y = 0;
        in >> kind >> p.text >> p.imageAlt >> eid
           >> p.style.color >> p.style.buttonColor >> p.style.fontName
           >> p.style.bold >> p.style.italic >> p.style.underline >> p.style.strike
           >> p.buttonValue >> bid >> orderCount;
        if (in.status() != QDataStream::Ok || orderCount < 0 || orderCount > kMaxParts) return false;
        p.kind = static_cast<PrintTemplatePart::Kind>(kind);
        p.expression = refOf(table, eid);
        p.buttonValueExpression = refOf(table, bid);
        for (qint32 j = 0; j < orderCount; ++j) {
            QString k;
            qint32 aid = -1;
            in >> k >> aid;
            p.attributes.insert(k, refOf(table, aid));
            p.attributeOrder.append(k);
        }
        in >> p.tooltip >> w >> h >> p.shapeType >> shapeCount;
        if (in.status() != QDataStream::Ok || shapeCount < 0 || shapeCount > kMaxParts) return false;
        p.width = w;
        p.height = h;
        p.shapeParams.reserve(shapeCount);
        for (qint32 j = 0; j < shapeCount; ++j) {
            qint32 v = 0;
            in >> v;
            p.shapeParams.append(v);
        }
        in >> p.lockedPosition >> x >> y;
        p.x = x;
        p.y = y;
        tpl.parts.append(p);
    }
    return in.status() == QDataStream::Ok;
}

// ---- LogicalLine ----
void writeLine(QDataStream& out, const LogicalLine& line, const NodeTable& t) {
    out << quint8(line.kind) << qint32(line.position.lineNumber) << qint32(line.position.column)
        << line.labelName << line.labelArgs << line.labelDefaults
        << line.functionName << line.assignOperator << bool(line.isFunctionCall)
        << line.raw << bool(line.isError) << line.errMes;
    out << qint32(line.arguments.size());
    for (const Operand& op : line.arguments) {
        out << op.raw << bool(op.isString) << bool(op.isVariable) << idOf(t, op.ast);
    }
    out << idOf(t, line.condition);
    out << bool(line.printTemplate != nullptr);
    if (line.printTemplate) writeTemplate(out, *line.printTemplate, t);
}

bool readLine(QDataStream& in, const QList<NodePtr>& table, const QString& path, int index,
              LogicalLine& line) {
    quint8 kind = 0;
    qint32 ln = 0, col = 1, nargs = 0, cid = -1;
    bool isFunctionCall = false, isError = false, hasTemplate = false;
    in >> kind >> ln >> col >> line.labelName >> line.labelArgs >> line.labelDefaults
       >> line.functionName >> line.assignOperator >> isFunctionCall
       >> line.raw >> isError >> line.errMes >> nargs;
    if (in.status() != QDataStream::Ok || nargs < 0 || nargs > kMaxOperands) return false;
    line.kind = static_cast<LineKind>(kind);
    line.position = ScriptPosition(path, ln, col);
    line.lineIndex = index;
    line.isFunctionCall = isFunctionCall;
    line.isError = isError;
    line.arguments.reserve(nargs);
    for (qint32 i = 0; i < nargs; ++i) {
        Operand op;
        bool isStr = false, isVar = false;
        qint32 aid = -1;
        in >> op.raw >> isStr >> isVar >> aid;
        op.isString = isStr;
        op.isVariable = isVar;
        op.ast = refOf(table, aid);
        line.arguments.append(op);
    }
    in >> cid;
    line.condition = refOf(table, cid);
    in >> hasTemplate;
    if (in.status() != QDataStream::Ok) return false;
    if (hasTemplate) {
        auto tpl = QSharedPointer<PrintTemplate>::create();
        if (!readTemplate(in, table, *tpl)) return false;
        line.printTemplate = tpl;
    }
    return in.status() == QDataStream::Ok;
}

}  // namespace

// ---------------------------------------------------------------------------
// 公开 API
// ---------------------------------------------------------------------------
namespace ErbAstDiskCache {

bool enabled() {
    static const bool on = [] {
        const QByteArray env = qgetenv("EMUERA_AST_DISK_CACHE").trimmed().toLower();
        return env == "1" || env == "true" || env == "on" || env == "yes";
    }();
    return on;
}

void setEnabled(bool) {
    // 环境变量优先；此处保留接口以便调用方显式打开（当前实现以 env 为准）。
}

QString computeKey(const QString& dirPath, const QStringList& files, int readEncoding,
                   bool debugMode) {
    QCryptographicHash h(QCryptographicHash::Sha1);
    const auto addStr = [&h](const QString& s) {
        h.addData(s.toUtf8());
        h.addData(QByteArray(1, '\0'));
    };
    addStr(QStringLiteral("emuera-erb-ast"));
    addStr(QString::number(kFormatVersion));
    addStr(QDir(dirPath).absolutePath());
    addStr(QString::number(readEncoding));
    addStr(debugMode ? QStringLiteral("dbg") : QStringLiteral("-"));
    for (const QString& f : files) {
        const QFileInfo fi(f);
        addStr(fi.absoluteFilePath());
        addStr(QString::number(fi.size()));
        addStr(QString::number(fi.lastModified().toMSecsSinceEpoch()));
    }
    // 影响 AST 的 CSV / config（_Rename.csv、常量表、编码配置）：
    //   · 游戏目录内（递归浅层）—— 覆盖 <game>/CSV 与 <game>/emuera.config；
    //   · 若 dirPath 是 ERB 子目录，则 CSV 通常与它同级 —— 只追加**兄弟 CSV 目录**
    //     与父目录**直接存放**的 config（避免把父目录下无关仓库/构建产物卷进来，
    //     那会让 key 每次构建都变化 → 缓存永不命中）。
    QList<QString> extra;
    collectConfigStamps(QDir(dirPath).absolutePath(), 3, extra);
    const QString parent = QFileInfo(dirPath).absolutePath();
    if (!parent.isEmpty() && QFileInfo(parent) != QFileInfo(dirPath)) {
        collectConfigStamps(parent, 0, extra);   // 父目录直属的 *.csv/*.config
        QDir p(parent);
        const QStringList subs =
            p.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& s : subs) {
            if (s.compare(QLatin1String("CSV"), Qt::CaseInsensitive) == 0) {
                collectConfigStamps(parent + QLatin1Char('/') + s, 2, extra);
            }
        }
    }
    std::sort(extra.begin(), extra.end());
    for (const QString& e : extra) addStr(e);
    return QString::fromLatin1(h.result().toHex());
}

QByteArray serializeParsedFile(const ParsedErbFile& pf) {
    QByteArray blob;
    QDataStream out(&blob, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << pf.scriptName << pf.path;
    NodeTable t;
    collectFileNodes(pf, t);
    writeNodeTable(out, t);
    out << qint32(pf.lines.size());
    for (const LogicalLine& line : pf.lines) writeLine(out, line, t);
    out << pf.warnings;
    if (out.status() != QDataStream::Ok) return QByteArray();
    return blob;
}

bool deserializeParsedFile(const QByteArray& blob, ParsedErbFile& out) {
    QDataStream in(blob);
    in.setVersion(QDataStream::Qt_6_0);
    in >> out.scriptName >> out.path;
    if (in.status() != QDataStream::Ok) {
        qWarning() << "[ErbAstDiskCache] header 读取失败";
        return false;
    }
    QList<NodePtr> table;
    if (!readNodeTable(in, table)) {
        qWarning() << "[ErbAstDiskCache] 节点表读取失败 script=" << out.scriptName;
        return false;
    }
    qint32 nlines = 0;
    in >> nlines;
    if (in.status() != QDataStream::Ok || nlines < 0 || nlines > kMaxLines) {
        qWarning() << "[ErbAstDiskCache] nlines 非法:" << nlines << out.scriptName;
        return false;
    }
    out.lines.reserve(nlines);
    for (qint32 i = 0; i < nlines; ++i) {
        LogicalLine line;
        if (!readLine(in, table, out.path, i, line)) {
            qWarning() << "[ErbAstDiskCache] 行读取失败 line=" << i << out.scriptName;
            return false;
        }
        out.lines.append(line);
    }
    in >> out.warnings;
    if (in.status() != QDataStream::Ok) {
        qWarning() << "[ErbAstDiskCache] warnings 读取失败" << out.scriptName;
        return false;
    }
    return true;
}

Writer::Writer(const QString& key) : m_path(filePathFor(key)) {}

Writer::~Writer() {
    if (m_open) m_file.cancelWriting();
}

bool Writer::begin(int fileCount) {
    m_file.setFileName(m_path);
    if (!m_file.open(QIODevice::WriteOnly)) return false;
    m_out.setDevice(&m_file);
    m_out.setVersion(QDataStream::Qt_6_0);
    m_out << qint32(kFormatVersion) << qint32(fileCount);
    m_open = true;
    return m_out.status() == QDataStream::Ok;
}

bool Writer::writeBlob(const QByteArray& blob) {
    if (!m_open || m_failed || blob.isEmpty()) {
        if (blob.isEmpty()) m_failed = true;
        return false;
    }
    m_out << blob;
    if (m_out.status() != QDataStream::Ok) {
        m_failed = true;
        return false;
    }
    return true;
}

bool Writer::commit() {
    if (!m_open) return false;
    m_open = false;
    if (m_failed || m_out.status() != QDataStream::Ok) {
        m_file.cancelWriting();
        return false;
    }
    const bool ok = m_file.commit();
    if (!ok) {
        qWarning() << "[ErbAstDiskCache] commit 失败:" << m_file.fileName()
                   << m_file.error() << m_file.errorString();
    }
    return ok;
}

void Writer::abort() {
    if (!m_open) return;
    m_open = false;
    m_file.cancelWriting();
}

Reader::Reader(const QString& key) {
    m_file.setFileName(filePathFor(key));
    if (!m_file.open(QIODevice::ReadOnly)) return;
    m_in.setDevice(&m_file);
    m_in.setVersion(QDataStream::Qt_6_0);
    qint32 version = 0;
    m_in >> version >> m_count;
    if (m_in.status() != QDataStream::Ok || version != kFormatVersion || m_count < 0
        || m_count > kMaxFiles) {
        m_file.close();
        return;
    }
    m_open = true;
}

bool Reader::readBlob(QByteArray& out) {
    if (!m_open || m_read >= m_count) return false;
    m_in >> out;
    if (m_in.status() != QDataStream::Ok) {
        qWarning() << "[ErbAstDiskCache] blob 读取失败 idx=" << m_read
                   << " status=" << int(m_in.status()) << " devPos=" << m_file.pos()
                   << " size=" << m_file.size() << " blobBytes=" << out.size();
        close();
        return false;
    }
    ++m_read;
    return true;
}

bool Reader::next(ParsedErbFile& out) {
    if (!m_open || m_read >= m_count) return false;
    QByteArray blob;
    m_in >> blob;
    if (m_in.status() != QDataStream::Ok) {
        qWarning() << "[ErbAstDiskCache] blob 读取失败 idx=" << m_read
                   << " status=" << int(m_in.status()) << " devPos=" << m_file.pos()
                   << " size=" << m_file.size() << " blobBytes=" << blob.size();
        close();
        return false;
    }
    if (!deserializeParsedFile(blob, out)) {
        qWarning() << "[ErbAstDiskCache] 反序列化失败 idx=" << m_read
                   << " blobBytes=" << blob.size();
        close();
        return false;
    }
    ++m_read;
    return true;
}

void Reader::close() {
    m_open = false;
    m_file.close();
}

void remove(const QString& key) {
    QFile::remove(filePathFor(key));
}

}  // namespace ErbAstDiskCache
