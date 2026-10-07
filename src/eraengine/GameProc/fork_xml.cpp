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
// ---------------------------------------------------------------------------
// fork_xml.cpp —— XML_* 族（18 条）「仿照语义」自研实现
//
// C# 语义：System.Xml.XmlDocument + **XPath SelectNodes**（Creator.Method.cs:54-1270）。
// 本移植不引入第三方 XML 库，也**不链接 Qt6::Xml**：
//   · 解析/序列化：Qt6::Core 的 QXmlStreamReader（已链接，零新依赖）
//   · 文档模型  ：自建小 DOM（元素/文本/属性/注释/PI + 父指针，够增删改查）
//   · 路径选择  ：**自研 XPath 子集**（Qt 6 已移除 XML Patterns，没有现成的）
//      支持：`/a/b`、`./k`、`//t`、`.`、`..`、`@attr`、`*`、`[n]`（1 起算）
//      —— 覆盖 era 游戏与本文档用例里出现的全部写法（见 test/data/functions/XML_*.md）。
//
// 文档字典 = C# VariableData.DataXmlDocument（键 = 下标字符串）。
// 取值样式（XML_GET/SET）：0=Value / 1=InnerText / 2=InnerXml / 3=OuterXml / 4=Name。
// ---------------------------------------------------------------------------

#include <QList>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>
#include <memory>

#include "fork_support.h"
#include "extension_registry.h"
#include "expression_ast.h"
#include "variable_storage.h"

namespace forkxml {

namespace {

using namespace forksupport;

// ---------------------------------------------------------------------------
// 小 DOM
// ---------------------------------------------------------------------------
struct XNode;
using XNodePtr = std::shared_ptr<XNode>;

struct XNode : public std::enable_shared_from_this<XNode> {
    enum Kind { Document, Element, Text, Attribute, Comment, PI };
    Kind kind = Element;
    QString name;                                        // 元素/属性/PI 名
    QString text;                                        // 文本内容 / 属性值
    QList<QPair<QString, QString>> attrs;                // 元素属性（有序）
    QList<XNodePtr> children;                            // 元素子节点
    XNode* parent = nullptr;                             // 便于 InsertBefore/After/Remove

    [[nodiscard]] bool isElement() const { return kind == Element; }
};

[[nodiscard]] QString escapeText(const QString& s) {
    QString o;
    o.reserve(s.size());
    for (const QChar c : s) {
        if (c == QLatin1Char('&')) o += QLatin1String("&amp;");
        else if (c == QLatin1Char('<')) o += QLatin1String("&lt;");
        else if (c == QLatin1Char('>')) o += QLatin1String("&gt;");
        else o += c;
    }
    return o;
}

[[nodiscard]] QString escapeAttr(const QString& s) {
    QString o;
    o.reserve(s.size());
    for (const QChar c : s) {
        if (c == QLatin1Char('&')) o += QLatin1String("&amp;");
        else if (c == QLatin1Char('<')) o += QLatin1String("&lt;");
        else if (c == QLatin1Char('"')) o += QLatin1String("&quot;");
        else if (c == QLatin1Char('\n')) o += QLatin1String("&#xA;");
        else if (c == QLatin1Char('\r')) o += QLatin1String("&#xD;");
        else if (c == QLatin1Char('\t')) o += QLatin1String("&#x9;");
        else o += c;
    }
    return o;
}

[[nodiscard]] QString serialize(const XNodePtr& n);

[[nodiscard]] QString serializeChildren(const XNodePtr& n) {
    QString o;
    for (const XNodePtr& c : n->children) o += serialize(c);
    return o;
}

[[nodiscard]] QString serialize(const XNodePtr& n) {
    if (!n) return QString();
    switch (n->kind) {
    case XNode::Document: return serializeChildren(n);
    case XNode::Text:     return escapeText(n->text);
    case XNode::Comment:  return QStringLiteral("<!--") + n->text + QStringLiteral("-->");
    case XNode::PI:       return QStringLiteral("<?") + n->name + QLatin1Char(' ')
                               + n->text + QStringLiteral("?>");
    case XNode::Attribute:
    case XNode::Element: {
        QString o = QLatin1Char('<') + n->name;
        for (const auto& a : n->attrs)
            o += QLatin1Char(' ') + a.first + QStringLiteral("=\"") + escapeAttr(a.second) + QLatin1Char('"');
        if (n->children.isEmpty()) { o += QStringLiteral(" />"); return o; }
        o += QLatin1Char('>');
        o += serializeChildren(n);
        o += QStringLiteral("</") + n->name + QLatin1Char('>');
        return o;
    }
    }
    return QString();
}

// InnerText：子树内全部文本拼接（对齐 XmlNode.InnerText）
void collectInnerText(const XNodePtr& n, QString& out) {
    if (!n) return;
    if (n->kind == XNode::Text || n->kind == XNode::Comment || n->kind == XNode::PI) out += n->text;
    for (const XNodePtr& c : n->children) collectInnerText(c, out);
}

[[nodiscard]] QString nodeName(const XNodePtr& n) {
    switch (n->kind) {
    case XNode::Text:    return QStringLiteral("#text");
    case XNode::Document: return QStringLiteral("#document");
    case XNode::Comment: return QStringLiteral("#comment");
    default: return n->name;
    }
}

[[nodiscard]] QString nodeValue(const XNodePtr& n) {
    switch (n->kind) {
    case XNode::Attribute:
    case XNode::Text:
    case XNode::Comment:
    case XNode::PI: return n->text;
    default: return QString();          // 元素/文档：XmlNode.Value == null -> ""
    }
}


// ---------------------------------------------------------------------------
// 解析（QXmlStreamReader -> 小 DOM）
// ---------------------------------------------------------------------------
XNodePtr parseXml(const QString& xml, bool& ok) {
    ok = false;
    QXmlStreamReader r(xml);
    XNodePtr doc = std::make_shared<XNode>();
    doc->kind = XNode::Document;
    XNode* cur = doc.get();
    while (!r.atEnd()) {
        const QXmlStreamReader::TokenType t = r.readNext();
        switch (t) {
        case QXmlStreamReader::StartElement: {
            XNodePtr e = std::make_shared<XNode>();
            e->kind = XNode::Element;
            e->name = r.name().toString();
            for (const QXmlStreamAttribute& a : r.attributes())
                e->attrs.append({ a.name().toString(), a.value().toString() });
            e->parent = cur;
            cur->children.append(e);
            cur = e.get();
            break;
        }
        case QXmlStreamReader::EndElement:
            if (cur->parent != nullptr) cur = cur->parent;
            break;
        case QXmlStreamReader::Characters:
            if (!r.isWhitespace()) {
                XNodePtr txt = std::make_shared<XNode>();
                txt->kind = XNode::Text;
                txt->text = r.text().toString();
                txt->parent = cur;
                cur->children.append(txt);
            }
            break;
        case QXmlStreamReader::Comment: {
            XNodePtr c = std::make_shared<XNode>();
            c->kind = XNode::Comment;
            c->text = r.text().toString();
            c->parent = cur;
            cur->children.append(c);
            break;
        }
        case QXmlStreamReader::ProcessingInstruction: {
            XNodePtr p = std::make_shared<XNode>();
            p->kind = XNode::PI;
            p->name = r.processingInstructionTarget().toString();
            p->text = r.processingInstructionData().toString();
            p->parent = cur;
            cur->children.append(p);
            break;
        }
        default: break;
        }
    }
    if (r.hasError() || doc->children.isEmpty()) return nullptr;
    ok = true;
    return doc;
}

// ---------------------------------------------------------------------------
// XPath 子集
//   path := ('/' | '//')? step ('/' | '//') step*
//   step := '.' | '..' | '@' name | '*' | name   （可带 [n] 位置谓词）
// ---------------------------------------------------------------------------
struct XStep {
    enum Type { Name, Wildcard, Self, Parent, Attr } type = Name;
    QString name;
    int index = -1;          // [n]，1 起算；-1 = 无谓词
    bool descendant = false; // 该步前面是 '//'
};

bool parseXPath(const QString& path, QList<XStep>& steps) {
    int i = 0;
    const int n = path.size();
    // 跳过开头空白
    while (i < n && path.at(i).isSpace()) ++i;
    bool pendingDescendant = false;
    if (i < n && path.at(i) == QLatin1Char('/')) { ++i; pendingDescendant = (i < n && path.at(i) == QLatin1Char('/')); if (pendingDescendant) ++i; }
    while (i < n) {
        XStep s;
        s.descendant = pendingDescendant;
        pendingDescendant = false;
        if (path.at(i) == QLatin1Char('.')) {
            if (i + 1 < n && path.at(i + 1) == QLatin1Char('.')) { s.type = XStep::Parent; i += 2; }
            else { s.type = XStep::Self; i += 1; }
        } else if (path.at(i) == QLatin1Char('@')) {
            ++i;
            const int b = i;
            while (i < n && (path.at(i).isLetterOrNumber() || path.at(i) == QLatin1Char('_')
                             || path.at(i) == QLatin1Char('-') || path.at(i) == QLatin1Char('.'))) ++i;
            s.type = XStep::Attr;
            s.name = path.mid(b, i - b);
            if (s.name.isEmpty()) return false;
        } else if (path.at(i) == QLatin1Char('*')) {
            s.type = XStep::Wildcard;
            ++i;
        } else if (path.at(i).isLetterOrNumber() || path.at(i) == QLatin1Char('_')) {
            const int b = i;
            while (i < n && (path.at(i).isLetterOrNumber() || path.at(i) == QLatin1Char('_')
                             || path.at(i) == QLatin1Char('-') || path.at(i) == QLatin1Char('.')
                             || path.at(i) == QLatin1Char(':'))) ++i;
            s.type = XStep::Name;
            s.name = path.mid(b, i - b);
        } else {
            return false;
        }
        // 谓词 [n]
        if (i < n && path.at(i) == QLatin1Char('[')) {
            const int close = path.indexOf(QLatin1Char(']'), i);
            if (close < 0) return false;
            bool okNum = false;
            const int idx = path.mid(i + 1, close - i - 1).trimmed().toInt(&okNum);
            if (!okNum || idx < 1) return false;   // 只支持 [数字]
            s.index = idx;
            i = close + 1;
        }
        steps.append(s);
        if (i < n && path.at(i) == QLatin1Char('/')) {
            ++i;
            pendingDescendant = (i < n && path.at(i) == QLatin1Char('/'));
            if (pendingDescendant) ++i;
        } else if (i < n) {
            return false;                          // 多余字符
        }
    }
    return true;
}

void collectDescendants(const XNodePtr& n, QList<XNodePtr>& out) {
    for (const XNodePtr& c : n->children) {
        out.append(c);
        collectDescendants(c, out);
    }
}

// 单步选择（不含谓词）
void selectStep(const QList<XNodePtr>& cur, const XStep& s, QList<XNodePtr>& out) {
    for (const XNodePtr& n : cur) {
        if (s.type == XStep::Self) { out.append(n); continue; }
        if (s.type == XStep::Parent) { if (n->parent) out.append(n->parent->shared_from_this()); continue; }

        // 属性步：属性不在 children 里，直接在元素自身的 attrs 中找
        // （`//a/@id`、`/root/b/@k` 这类路径）
        if (s.type == XStep::Attr) {
            QList<XNodePtr> elems = s.descendant ? QList<XNodePtr>{ n } : QList<XNodePtr>{ n };
            if (s.descendant) { elems.clear(); elems.append(n); collectDescendants(n, elems); }
            for (const XNodePtr& e : elems) {
                if (e->kind != XNode::Element) continue;
                for (const auto& a : e->attrs) {
                    if (a.first != s.name) continue;
                    XNodePtr an = std::make_shared<XNode>();
                    an->kind = XNode::Attribute;
                    an->name = a.first;
                    an->text = a.second;
                    an->parent = e.get();
                    out.append(an);
                }
            }
            continue;
        }

        QList<XNodePtr> cand;
        if (s.descendant) {
            collectDescendants(n, cand);
        } else {
            cand = n->children;
        }
        for (const XNodePtr& c : cand) {
            if (s.type == XStep::Name) {
                if (c->kind == XNode::Element && c->name == s.name) out.append(c);
            } else if (s.type == XStep::Wildcard) {
                if (c->kind == XNode::Element) out.append(c);
            }
        }
    }
}

QList<XNodePtr> selectNodes(const XNodePtr& doc, const QString& path, bool& ok) {
    QList<XNodePtr> cur{ doc };
    QList<XStep> steps;
    ok = parseXPath(path, steps);
    if (!ok) return {};
    for (const XStep& s : steps) {
        QList<XNodePtr> next;
        selectStep(cur, s, next);
        if (s.index > 0) {
            if (next.size() < s.index) next.clear();
            else next = { next.at(s.index - 1) };
        }
        cur = next;
        if (cur.isEmpty()) break;
    }
    return cur;
}

// ---------------------------------------------------------------------------
// 文档字典与「参数 0」解析
// ---------------------------------------------------------------------------
QHash<QString, XNodePtr>& docStore() {
    static QHash<QString, XNodePtr> store;
    return store;
}

// 参数 0 的重载形态：
//   返回 true  -> 直接操作字典里的文档 id（docId 有效）
//   返回 false -> 参数 0 是 XML 文本（临时文档；若为变量则 saveToVar 记名字）
bool resolveDocArg(const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes,
                   bool byName, QString& docId, QString& saveToVar, XNodePtr& tempDoc, bool& ok)
{
    ok = true;
    const ExpressionNode* node0 = nodes.isEmpty() ? nullptr : nodes.at(0);
    const bool argIsInt = node0 != nullptr && node0->valueType() == OperandType::Int;
    const bool argIsStr = node0 != nullptr && node0->valueType() == OperandType::Str;
    if (argIsInt || (byName && argIsStr)) {
        docId = a.value(0).toString();
        return true;
    }
    // 临时文档：解析 XML 文本
    bool parsed = false;
    tempDoc = parseXml(a.value(0).toString(), parsed);
    if (!parsed) { ok = false; return false; }
    saveToVar = nodes.isEmpty() ? QString() : refTargetName(node0);
    return false;
}

// XML_GET 的样式取值
QString styleValue(const XNodePtr& n, qint64 style) {
    switch (style) {
    case 1: { QString t; collectInnerText(n, t); return t; }
    case 2: return serializeChildren(n);
    case 3: return serialize(n);
    case 4: return nodeName(n);
    default: return nodeValue(n);
    }
}

// 属性写入（文档节点上的属性集）
bool setStyleValue(const XNodePtr& n, const QString& val, qint64 style) {
    switch (style) {
    case 1: {
        // InnerText = val：清空子节点，放一个文本节点
        n->children.clear();
        XNodePtr t = std::make_shared<XNode>();
        t->kind = XNode::Text;
        t->text = val;
        t->parent = n.get();
        n->children.append(t);
        return true;
    }
    case 2: {
        // InnerXml = val：解析后替换子节点
        bool ok = false;
        XNodePtr frag = parseXml(QStringLiteral("<_>") + val + QStringLiteral("</_>"), ok);
        if (!ok) return false;
        n->children.clear();
        if (frag->children.isEmpty()) return true;
        for (const XNodePtr& c : frag->children.at(0)->children) {
            c->parent = n.get();
            n->children.append(c);
        }
        return true;
    }
    default:
        if (n->kind == XNode::Attribute) { n->text = val; return true; }
        if (n->kind == XNode::Text) { n->text = val; return true; }
        return false;      // 元素无 Value（C# 会抛异常；容错实现按失败处理）
    }
}

// 增删改（父指针维护）
bool insertRelative(const XNodePtr& target, const XNodePtr& child, int method, bool isAttr) {
    if (method == 0) {
        if (isAttr) {
            if (!target->isElement()) return false;
            target->attrs.append({ child->name, child->text });
            return true;
        }
        child->parent = target.get();
        target->children.append(child);
        return true;
    }
    if (target->parent == nullptr) return false;
    XNode* p = target->parent;
    if (isAttr) {
        if (!p->isElement()) return false;
        if (target->kind != XNode::Attribute) return false;
        int at = -1;
        for (int i = 0; i < p->attrs.size(); ++i)
            if (p->attrs.at(i).first == target->name) { at = i; break; }
        if (at < 0) return false;
        if (method == 1) p->attrs.insert(at, { child->name, child->text });
        else p->attrs.insert(at + 1, { child->name, child->text });
        return true;
    }
    int at = -1;
    for (int i = 0; i < p->children.size(); ++i)
        if (p->children.at(i).get() == target.get()) { at = i; break; }
    if (at < 0) return false;
    child->parent = p;
    if (method == 1) p->children.insert(at, child);
    else p->children.insert(at + 1, child);
    return true;
}

bool removeNode(const XNodePtr& n) {
    if (n->parent == nullptr) return false;
    XNode* p = n->parent;
    if (n->kind == XNode::Attribute) {
        for (int i = 0; i < p->attrs.size(); ++i) {
            if (p->attrs.at(i).first == n->name) { p->attrs.removeAt(i); return true; }
        }
        return false;
    }
    for (int i = 0; i < p->children.size(); ++i) {
        if (p->children.at(i).get() == n.get()) { p->children.removeAt(i); return true; }
    }
    return false;
}

bool replaceNode(const XNodePtr& oldNode, const XNodePtr& newNode) {
    if (oldNode->parent == nullptr) return false;
    XNode* p = oldNode->parent;
    for (int i = 0; i < p->children.size(); ++i) {
        if (p->children.at(i).get() == oldNode.get()) {
            newNode->parent = p;
            p->children.replace(i, newNode);
            return true;
        }
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// 注册
// ---------------------------------------------------------------------------
void registerXmlExtensions(ExtensionRegistry& ext)
{
    // XML_DOCUMENT / XML_EXIST / XML_RELEASE（Creator.Method.cs:739）
    ext.regExpr(QStringLiteral("XML_DOCUMENT"), OperandType::Int, 2, 2,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const QString id = a.value(0).toString();
            auto& store = docStore();
            if (store.contains(id)) { out = QVariant::fromValue<qint64>(0); return true; }
            bool ok = false;
            XNodePtr doc = parseXml(a.value(1).toString(), ok);
            if (!ok) { out = QVariant::fromValue<qint64>(0); return true; }   // C# 抛 CodeEE；容错记 0
            store.insert(id, doc);
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    ext.regExpr(QStringLiteral("XML_EXIST"), OperandType::Int, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            out = QVariant::fromValue<qint64>(docStore().contains(a.value(0).toString()) ? 1 : 0);
            return true;
        });

    ext.regExpr(QStringLiteral("XML_RELEASE"), OperandType::Int, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            out = QVariant::fromValue<qint64>(docStore().remove(a.value(0).toString()) > 0 ? 1 : 0);
            return true;
        });

    ext.regExpr(QStringLiteral("XML_TOSTR"), OperandType::Str, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = docStore();
            const QString id = a.value(0).toString();
            out = store.contains(id) ? serialize(store.value(id)) : QString();
            return true;
        });

    // XML_GET（Creator.Method.cs:54）：返回命中节点数；第 3 参 = 输出数组（或 int 非 0 -> RESULTS）
    ext.regExpr(QStringLiteral("XML_GET"), OperandType::Int, 2, 4,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes,
               QVariant& out) {
            QString docId, saveToVar;
            XNodePtr temp;
            bool ok = true;
            const bool byId = resolveDocArg(a, nodes, false, docId, saveToVar, temp, ok);
            if (!ok) { out = QVariant::fromValue<qint64>(0); return true; }
            XNodePtr doc = byId ? docStore().value(docId) : temp;
            if (!doc) { out = QVariant::fromValue<qint64>(-1); return true; }

            bool pathOk = false;
            const QList<XNodePtr> hits = selectNodes(doc, a.value(1).toString(), pathOk);
            if (!pathOk) { out = QVariant::fromValue<qint64>(0); return true; }

            const qint64 style = a.size() >= 4 ? a.value(3).toLongLong() : 0;
            if (a.size() >= 3) {
                const bool toResults = nodes.at(2) != nullptr
                    && nodes.at(2)->valueType() == OperandType::Int;
                const QString target = toResults ? QStringLiteral("RESULTS")
                                                 : refTargetName(nodes.at(2));
                const int cap = arrayCapacity(ext.services().storage, target);
                const int n = cap > 0 ? qMin(hits.size(), cap) : hits.size();
                for (int i = 0; i < n; ++i)
                    writeStr(ext.services().storage, target, i, styleValue(hits.at(i), style));
            }
            out = QVariant::fromValue<qint64>(hits.size());
            return true;
        });

    ext.regExpr(QStringLiteral("XML_GET_BYNAME"), OperandType::Int, 2, 4,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes,
               QVariant& out) {
            QString docId, saveToVar;
            XNodePtr temp;
            bool ok = true;
            const bool byId = resolveDocArg(a, nodes, true, docId, saveToVar, temp, ok);
            if (!ok) { out = QVariant::fromValue<qint64>(0); return true; }
            XNodePtr doc = byId ? docStore().value(docId) : temp;
            if (!doc) { out = QVariant::fromValue<qint64>(-1); return true; }

            bool pathOk = false;
            const QList<XNodePtr> hits = selectNodes(doc, a.value(1).toString(), pathOk);
            if (!pathOk) { out = QVariant::fromValue<qint64>(0); return true; }

            const qint64 style = a.size() >= 4 ? a.value(3).toLongLong() : 0;
            if (a.size() >= 3) {
                const bool toResults = nodes.at(2) != nullptr
                    && nodes.at(2)->valueType() == OperandType::Int;
                const QString target = toResults ? QStringLiteral("RESULTS")
                                                 : refTargetName(nodes.at(2));
                const int cap = arrayCapacity(ext.services().storage, target);
                const int n = cap > 0 ? qMin(hits.size(), cap) : hits.size();
                for (int i = 0; i < n; ++i)
                    writeStr(ext.services().storage, target, i, styleValue(hits.at(i), style));
            }
            out = QVariant::fromValue<qint64>(hits.size());
            return true;
        });

    // XML_SET / XML_SET_BYNAME（Creator.Method.cs:791）
    auto xmlSet = [](bool byName) {
        return [byName](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes,
                        QVariant& out, ExtensionRegistry* ext) {
            QString docId, saveToVar;
            XNodePtr temp;
            bool ok = true;
            const bool byId = resolveDocArg(a, nodes, byName, docId, saveToVar, temp, ok);
            if (!ok) { out = QVariant::fromValue<qint64>(0); return; }
            XNodePtr doc = byId ? docStore().value(docId) : temp;
            if (!doc) { out = QVariant::fromValue<qint64>(-1); return; }
            bool pathOk = false;
            const QList<XNodePtr> hits = selectNodes(doc, a.value(1).toString(), pathOk);
            if (!pathOk) { out = QVariant::fromValue<qint64>(0); return; }
            const QString val = a.value(2).toString();
            const bool setAll = a.size() >= 4 && a.value(3).toLongLong() != 0;
            qint64 style = a.size() >= 5 ? a.value(4).toLongLong() : 0;
            if (style < 0 || style > 2) style = 0;
            if (!hits.isEmpty()) {
                if (hits.size() == 1) setStyleValue(hits.at(0), val, style);
                else if (setAll) for (const XNodePtr& n : hits) setStyleValue(n, val, style);
                if (!saveToVar.isEmpty())
                    writeStr(ext->services().storage, saveToVar, 0, serialize(doc));
            }
            out = QVariant::fromValue<qint64>(hits.size());
        };
    };
    {
        auto f = xmlSet(false);
        ext.regExpr(QStringLiteral("XML_SET"), OperandType::Int, 3, 5,
            [f, &ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes, QVariant& out) {
                f(a, nodes, out, &ext);
                return true;
            });
        auto g = xmlSet(true);
        ext.regExpr(QStringLiteral("XML_SET_BYNAME"), OperandType::Int, 3, 5,
            [g, &ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes, QVariant& out) {
                g(a, nodes, out, &ext);
                return true;
            });
    }

    // XML_ADDNODE / XML_ADDNODE_BYNAME（Creator.Method.cs:893）
    auto xmlAddNode = [](bool byName, bool attribute) {
        return [byName, attribute](const QList<QVariant>& a,
                                   const QList<const ExpressionNode*>& nodes,
                                   QVariant& out, ExtensionRegistry* ext) {
            QString docId, saveToVar;
            XNodePtr temp;
            bool ok = true;
            const bool byId = resolveDocArg(a, nodes, byName, docId, saveToVar, temp, ok);
            if (!ok) { out = QVariant::fromValue<qint64>(0); return; }
            XNodePtr doc = byId ? docStore().value(docId) : temp;
            if (!doc) { out = QVariant::fromValue<qint64>(-1); return; }
            bool pathOk = false;
            const QList<XNodePtr> hits = selectNodes(doc, a.value(1).toString(), pathOk);
            if (!pathOk) { out = QVariant::fromValue<qint64>(0); return; }

            const int methodPos = attribute ? 5 : 4;      // C#: methodPos
            int method = a.size() >= methodPos ? static_cast<int>(a.value(methodPos - 1).toLongLong()) : 0;
            if (method < 0 || method > 2) method = 0;

            XNodePtr child = std::make_shared<XNode>();
            if (attribute) {
                child->kind = XNode::Attribute;
                child->name = a.value(2).toString();
                if (a.size() >= 4) child->text = a.value(3).toString();
            } else {
                bool parsed = false;
                XNodePtr frag = parseXml(a.value(2).toString(), parsed);
                if (!parsed || frag->children.isEmpty()) { out = QVariant::fromValue<qint64>(0); return; }
                const XNodePtr rootEl = frag->children.at(0);
                child->kind = rootEl->kind;
                child->name = rootEl->name;
                child->attrs = rootEl->attrs;
                child->text = rootEl->text;
                for (const XNodePtr& c : rootEl->children) { c->parent = child.get(); child->children.append(c); }
            }
            if (!hits.isEmpty()) {
                const int setAllPos = attribute ? 6 : 5;
                const bool setAll = a.size() == setAllPos && a.value(setAllPos - 1).toLongLong() != 0;
                bool failedOne = false;
                if (hits.size() == 1) {
                    if (!insertRelative(hits.at(0), child, method, attribute) && method > 0)
                        failedOne = true;
                } else if (setAll) {
                    for (const XNodePtr& n : hits) insertRelative(n, child, method, attribute);
                }
                if (failedOne) { out = QVariant::fromValue<qint64>(0); return; }
                if (!saveToVar.isEmpty())
                    writeStr(ext->services().storage, saveToVar, 0, serialize(doc));
            }
            out = QVariant::fromValue<qint64>(hits.size());
        };
    };
    {
        auto f = xmlAddNode(false, false);
        ext.regExpr(QStringLiteral("XML_ADDNODE"), OperandType::Int, 3, 5,
            [f, &ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes, QVariant& out) {
                f(a, nodes, out, &ext); return true; });
        auto g = xmlAddNode(true, false);
        ext.regExpr(QStringLiteral("XML_ADDNODE_BYNAME"), OperandType::Int, 3, 5,
            [g, &ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes, QVariant& out) {
                g(a, nodes, out, &ext); return true; });
        auto h = xmlAddNode(false, true);
        ext.regExpr(QStringLiteral("XML_ADDATTRIBUTE"), OperandType::Int, 3, 6,
            [h, &ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes, QVariant& out) {
                h(a, nodes, out, &ext); return true; });
        auto i = xmlAddNode(true, true);
        ext.regExpr(QStringLiteral("XML_ADDATTRIBUTE_BYNAME"), OperandType::Int, 3, 6,
            [i, &ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes, QVariant& out) {
                i(a, nodes, out, &ext); return true; });
    }

    // XML_REMOVENODE / XML_REMOVEATTRIBUTE（Creator.Method.cs:1048）
    auto xmlRemoveNode = [](bool byName, bool attribute) {
        return [byName, attribute](const QList<QVariant>& a,
                                   const QList<const ExpressionNode*>& nodes,
                                   QVariant& out, ExtensionRegistry* ext) {
            QString docId, saveToVar;
            XNodePtr temp;
            bool ok = true;
            const bool byId = resolveDocArg(a, nodes, byName, docId, saveToVar, temp, ok);
            if (!ok) { out = QVariant::fromValue<qint64>(0); return; }
            XNodePtr doc = byId ? docStore().value(docId) : temp;
            if (!doc) { out = QVariant::fromValue<qint64>(-1); return; }
            bool pathOk = false;
            const QList<XNodePtr> hits = selectNodes(doc, a.value(1).toString(), pathOk);
            if (!pathOk) { out = QVariant::fromValue<qint64>(0); return; }
            if (!hits.isEmpty()) {
                const bool setAll = a.size() == 3 && a.value(2).toLongLong() != 0;
                bool failed = false;
                if (hits.size() == 1) { if (!removeNode(hits.at(0))) failed = true; }
                else if (setAll) for (const XNodePtr& n : hits) removeNode(n);
                if (failed) { out = QVariant::fromValue<qint64>(0); return; }
                if (!saveToVar.isEmpty())
                    writeStr(ext->services().storage, saveToVar, 0, serialize(doc));
            }
            Q_UNUSED(attribute);
            out = QVariant::fromValue<qint64>(hits.size());
        };
    };
    {
        auto f = xmlRemoveNode(false, false);
        ext.regExpr(QStringLiteral("XML_REMOVENODE"), OperandType::Int, 2, 4,
            [f, &ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes, QVariant& out) {
                f(a, nodes, out, &ext); return true; });
        auto g = xmlRemoveNode(true, false);
        ext.regExpr(QStringLiteral("XML_REMOVENODE_BYNAME"), OperandType::Int, 2, 4,
            [g, &ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes, QVariant& out) {
                g(a, nodes, out, &ext); return true; });
        auto h = xmlRemoveNode(false, true);
        ext.regExpr(QStringLiteral("XML_REMOVEATTRIBUTE"), OperandType::Int, 2, 4,
            [h, &ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes, QVariant& out) {
                h(a, nodes, out, &ext); return true; });
        auto i = xmlRemoveNode(true, true);
        ext.regExpr(QStringLiteral("XML_REMOVEATTRIBUTE_BYNAME"), OperandType::Int, 2, 4,
            [i, &ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes, QVariant& out) {
                i(a, nodes, out, &ext); return true; });
    }

    // XML_REPLACE / XML_REPLACE_BYNAME（Creator.Method.cs:1146）
    auto xmlReplace = [](bool byName) {
        return [byName](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes,
                        QVariant& out, ExtensionRegistry* ext) {
            // 2 参形态：XML_REPLACE(id, xml) —— 整份文档替换
            if (a.size() == 2 && (byName || (nodes.value(0) && nodes.at(0)->valueType() != OperandType::Int))) {
                auto& store = docStore();
                const QString id = a.value(0).toString();
                if (!store.contains(id)) { out = QVariant::fromValue<qint64>(-1); return; }
                bool ok = false;
                XNodePtr doc = parseXml(a.value(1).toString(), ok);
                if (!ok) { out = QVariant::fromValue<qint64>(0); return; }
                store[id] = doc;
                out = QVariant::fromValue<qint64>(1);
                return;
            }
            const QString xml = a.value(a.size() > 2 ? 2 : 1).toString();
            bool parsed = false;
            XNodePtr frag = parseXml(xml, parsed);
            if (!parsed || frag->children.isEmpty()) { out = QVariant::fromValue<qint64>(0); return; }

            QString docId, saveToVar;
            XNodePtr temp;
            bool ok = true;
            const bool byId = resolveDocArg(a, nodes, byName, docId, saveToVar, temp, ok);
            if (!ok) { out = QVariant::fromValue<qint64>(0); return; }
            XNodePtr doc = byId ? docStore().value(docId) : temp;
            if (!doc) { out = QVariant::fromValue<qint64>(-1); return; }
            bool pathOk = false;
            const QList<XNodePtr> hits = selectNodes(doc, a.value(1).toString(), pathOk);
            if (!pathOk) { out = QVariant::fromValue<qint64>(0); return; }
            if (!hits.isEmpty()) {
                const XNodePtr src = frag->children.at(0);
                XNodePtr child = std::make_shared<XNode>();
                child->kind = src->kind;
                child->name = src->name;
                child->attrs = src->attrs;
                child->text = src->text;
                for (const XNodePtr& c : src->children) { c->parent = child.get(); child->children.append(c); }
                const bool setAll = a.size() >= 4 && a.value(3).toLongLong() != 0;
                bool failed = false;
                if (hits.size() == 1) { if (!replaceNode(hits.at(0), child)) failed = true; }
                else if (setAll) for (const XNodePtr& n : hits) replaceNode(n, child);
                if (failed) { out = QVariant::fromValue<qint64>(0); return; }
                if (!saveToVar.isEmpty())
                    writeStr(ext->services().storage, saveToVar, 0, serialize(doc));
            }
            out = QVariant::fromValue<qint64>(hits.size());
        };
    };
    {
        auto f = xmlReplace(false);
        ext.regExpr(QStringLiteral("XML_REPLACE"), OperandType::Int, 2, 4,
            [f, &ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes, QVariant& out) {
                f(a, nodes, out, &ext); return true; });
        auto g = xmlReplace(true);
        ext.regExpr(QStringLiteral("XML_REPLACE_BYNAME"), OperandType::Int, 2, 4,
            [g, &ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes, QVariant& out) {
                g(a, nodes, out, &ext); return true; });
    }
}

} // namespace forkxml
