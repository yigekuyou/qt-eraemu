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
// fork_datatable.cpp —— DT_* 族（21 条）+ DT_COLUMN_OPTIONS「仿照语义」自研实现
//
// C# 语义：System.Data.DataTable（Creator.Method.cs:1270-1780 + Utils.cs:283-327）。
// 本移植**不用数据库**：自建内存类型化表 ——
//   · 列：名字 / 类型（int8,int16,int32,int64,string）/ AllowDBNull / DefaultValue
//   · 行：QList<QVariant>（无效 QVariant == DBNull）
//   · 第 0 列恒为 `id`（long，非空、唯一、主键；不可改、不可删）
//   · 过滤/排序：**自研 DataView 子集**（= <> > >= < <= AND OR NOT LIKE IN
//     IS [NOT] NULL + 括号；排序 col [ASC|DESC] 逗号分隔）
//   · TOXML/FROMXML：自洽的架构+数据 XML（形状对齐 .NET DataSet，见文件尾说明）
// ---------------------------------------------------------------------------

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <climits>

#include "fork_support.h"
#include "extension_registry.h"
#include "expression_ast.h"
#include "logical_line.h"
#include "variable_storage.h"

namespace forkdt {

namespace {

using namespace forksupport;

// ---------------------------------------------------------------------------
// 类型（对齐 Utils.DataTable：内置类型与数值码 1..5）
// ---------------------------------------------------------------------------
enum class DType { Int8, Int16, Int32, Int64, Str };

int typeToCode(DType t) {
    switch (t) {
    case DType::Int8:  return 1;
    case DType::Int16: return 2;
    case DType::Int32: return 3;
    case DType::Int64: return 4;
    case DType::Str:   return 5;
    }
    return 0;
}

bool codeToType(int code, DType& out) {
    switch (code) {
    case 1: out = DType::Int8;  return true;
    case 2: out = DType::Int16; return true;
    case 3: out = DType::Int32; return true;
    case 4: out = DType::Int64; return true;
    case 5: out = DType::Str;   return true;
    default: return false;
    }
}

bool nameToType(const QString& n, DType& out) {
    const QString s = n.trimmed().toLower();
    if (s == QLatin1String("int8"))  { out = DType::Int8;  return true; }
    if (s == QLatin1String("int16")) { out = DType::Int16; return true; }
    if (s == QLatin1String("int32")) { out = DType::Int32; return true; }
    if (s == QLatin1String("int64")) { out = DType::Int64; return true; }
    if (s == QLatin1String("string")){ out = DType::Str;   return true; }
    return false;
}

// 整数按列类型夹取（对齐 Utils.DataTable.ConvertInt）
qint64 clampInt(qint64 v, DType t) {
    switch (t) {
    case DType::Int8:  return std::max<qint64>(-128, std::min<qint64>(v, 127));
    case DType::Int16: return std::max<qint64>(-32768, std::min<qint64>(v, 32767));
    case DType::Int32: return std::max<qint64>(INT32_MIN, std::min<qint64>(v, INT32_MAX));
    case DType::Str:
    case DType::Int64: return v;
    }
    return v;
}

struct DColumn {
    QString name;
    DType   type = DType::Str;
    bool    nullable = true;
    QVariant defaultValue;          // DT_COLUMN_OPTIONS 的 DEFAULT=（无效 = 无默认值）
};

struct DTable {
    QString name;
    bool    caseSensitive = true;   // DT_NOCASE
    bool    hasDefaultedRows = false;
    QList<DColumn> columns;
    QList<QList<QVariant>> rows;    // 无效 QVariant == DBNull

    [[nodiscard]] int columnIndex(const QString& col) const {
        for (int i = 0; i < columns.size(); ++i) {
            if (caseSensitive ? columns.at(i).name == col
                              : columns.at(i).name.compare(col, Qt::CaseInsensitive) == 0)
                return i;
        }
        return -1;
    }
    [[nodiscard]] int idColumn() const { return columns.isEmpty() ? -1 : 0; }
};

QHash<QString, DTable>& tableStore() {
    static QHash<QString, DTable> store;
    return store;
}

// 新行 id（对齐 C# Utils.TimePoint()：高精度时间基准 + 计数器，唯一且不可预测）
qint64 nextTimePoint() {
    static qint64 counter = 0;
    const qint64 base = QDateTime::currentMSecsSinceEpoch() * 10000;
    return base + (++counter);
}

// ---------------------------------------------------------------------------
// 过滤/排序表达式（DataTable.Select 子集）
// ---------------------------------------------------------------------------
struct DValue {
    enum Kind { Null, Int, Str } kind = Null;
    qint64  i = 0;
    QString s;
    [[nodiscard]] bool isNull() const { return kind == Null; }
    static DValue fromVariant(const QVariant& v) {
        DValue d;
        if (!v.isValid()) return d;
        if (v.userType() == QMetaType::QString) { d.kind = Str; d.s = v.toString(); return d; }
        d.kind = Int;
        d.i = v.toLongLong();
        return d;
    }
};

enum class FT { Name, Number, Str };
struct FToken { FT type = FT::Name; QString text; };

bool tokenizeFilter(const QString& in, QList<FToken>& out) {
    int i = 0;
    const int n = in.size();
    while (i < n) {
        const QChar c = in.at(i);
        if (c.isSpace()) { ++i; continue; }
        if (c == QLatin1Char('\'')) {
            const int end = in.indexOf(QLatin1Char('\''), i + 1);
            if (end < 0) return false;
            out.append({ FT::Str, in.mid(i + 1, end - i - 1) });
            i = end + 1;
            continue;
        }
        if (c == QLatin1Char('"')) {          // 双引号：C# 视为列名（此处按名字处理）
            const int end = in.indexOf(QLatin1Char('"'), i + 1);
            if (end < 0) return false;
            out.append({ FT::Name, in.mid(i + 1, end - i - 1) });
            i = end + 1;
            continue;
        }
        if (c.isDigit() || (c == QLatin1Char('-') && i + 1 < n && in.at(i + 1).isDigit())) {
            const int b = i;
            if (in.at(i) == QLatin1Char('-')) ++i;
            while (i < n && in.at(i).isDigit()) ++i;
            out.append({ FT::Number, in.mid(b, i - b) });
            continue;
        }
        if (c.isLetter() || c == QLatin1Char('_')) {
            const int b = i;
            while (i < n && (in.at(i).isLetterOrNumber() || in.at(i) == QLatin1Char('_')
                             || in.at(i) == QLatin1Char(':'))) ++i;
            out.append({ FT::Name, in.mid(b, i - b) });
            continue;
        }
        // 运算符/括号/逗号
        if (c == QLatin1Char('<') || c == QLatin1Char('>')) {
            QString op(c);
            if (i + 1 < n && in.at(i + 1) == QLatin1Char('=')) { op += QLatin1Char('='); ++i; }
            else if (i + 1 < n && in.at(i + 1) == QLatin1Char('>') && c == QLatin1Char('<')) { op += QLatin1Char('>'); ++i; }
            out.append({ FT::Name, op });
            ++i;
            continue;
        }
        if (c == QLatin1Char('=')) { out.append({ FT::Name, QStringLiteral("=") }); ++i; continue; }
        if (c == QLatin1Char('(') || c == QLatin1Char(')') || c == QLatin1Char(',')) {
            out.append({ FT::Name, QString(c) });
            ++i;
            continue;
        }
        if (c == QLatin1Char('!')) { out.append({ FT::Name, QStringLiteral("<>") }); ++i; continue; }
        return false;
    }
    return true;
}

struct FilterParser {
    const QList<FToken>& t;
    const DTable& table;
    int pos = 0;
    bool ok = true;

    FilterParser(const QList<FToken>& tokens, const DTable& dt) : t(tokens), table(dt) {}

    [[nodiscard]] bool atEnd() const { return pos >= t.size(); }
    [[nodiscard]] const FToken* peek() const { return atEnd() ? nullptr : &t.at(pos); }
    bool acceptKeyword(const char* kw) {
        if (!atEnd() && t.at(pos).type == FT::Name
            && t.at(pos).text.compare(QLatin1String(kw), Qt::CaseInsensitive) == 0) {
            ++pos;
            return true;
        }
        return false;
    }

    DValue operand(const QList<QVariant>& row) {
        const FToken* tk = peek();
        if (tk == nullptr) { ok = false; return {}; }
        if (tk->type == FT::Number) {
            ++pos;
            DValue d; d.kind = DValue::Int; d.i = tk->text.toLongLong();
            return d;
        }
        if (tk->type == FT::Str) {
            ++pos;
            DValue d; d.kind = DValue::Str; d.s = tk->text;
            return d;
        }
        // 列名
        ++pos;
        const int idx = table.columnIndex(tk->text);
        if (idx < 0) return DValue{};                 // 未知列 -> NULL
        return DValue::fromVariant(row.value(idx));
    }

    bool literalList(const QList<QVariant>& row, QList<DValue>& out) {
        if (peek() == nullptr || peek()->text != QLatin1String("(")) { ok = false; return false; }
        ++pos;
        while (!atEnd()) {
            out.append(operand(row));
            if (peek() != nullptr && peek()->text == QLatin1String(",")) { ++pos; continue; }
            break;
        }
        if (peek() == nullptr || peek()->text != QLatin1String(")")) { ok = false; return false; }
        ++pos;
        return true;
    }

    static int compare(const DValue& a, const DValue& b) {
        if (a.kind == DValue::Str || b.kind == DValue::Str) {
            const QString as = a.kind == DValue::Str ? a.s : QString::number(a.i);
            const QString bs = b.kind == DValue::Str ? b.s : QString::number(b.i);
            return as.compare(bs);
        }
        if (a.i < b.i) return -1;
        if (a.i > b.i) return 1;
        return 0;
    }

    static bool like(const QString& value, const QString& pattern) {
        QString re;
        for (const QChar c : pattern) {
            if (c == QLatin1Char('%')) re += QStringLiteral(".*");
            else if (c == QLatin1Char('_')) re += QLatin1Char('.');
            else re += QRegularExpression::escape(QString(c));
        }
        return QRegularExpression(QLatin1Char('^') + re + QLatin1Char('$'),
                                  QRegularExpression::DotMatchesEverythingOption)
               .match(value).hasMatch();
    }

    bool primary(const QList<QVariant>& row) {
        if (peek() != nullptr && peek()->text == QLatin1String("(")) {
            ++pos;
            const bool v = orExpr(row);
            if (peek() != nullptr && peek()->text == QLatin1String(")")) ++pos;
            else ok = false;
            return v;
        }
        const DValue lhs = operand(row);
        const FToken* op = peek();
        if (op == nullptr) { ok = false; return false; }

        if (op->type == FT::Name && op->text.compare(QLatin1String("IS"), Qt::CaseInsensitive) == 0) {
            ++pos;
            const bool neg = acceptKeyword("NOT");
            if (!acceptKeyword("NULL")) { ok = false; return false; }
            return neg ? !lhs.isNull() : lhs.isNull();
        }
        if (op->type == FT::Name
            && (op->text.compare(QLatin1String("LIKE"), Qt::CaseInsensitive) == 0)) {
            ++pos;
            const DValue rhs = operand(row);
            return like(lhs.kind == DValue::Str ? lhs.s : QString::number(lhs.i), rhs.s);
        }
        if (op->type == FT::Name
            && (op->text.compare(QLatin1String("IN"), Qt::CaseInsensitive) == 0)) {
            ++pos;
            QList<DValue> list;
            if (!literalList(row, list)) return false;
            for (const DValue& v : list) if (compare(lhs, v) == 0) return true;
            return false;
        }
        if (op->type == FT::Name && op->text.size() <= 2
            && QStringLiteral("= <> < > <= >=").contains(op->text)) {
            const QString o = op->text;
            ++pos;
            const DValue rhs = operand(row);
            const int c = compare(lhs, rhs);
            if (o == QLatin1String("="))  return c == 0;
            if (o == QLatin1String("<>")) return c != 0;
            if (o == QLatin1String("<"))  return c < 0;
            if (o == QLatin1String(">"))  return c > 0;
            if (o == QLatin1String("<=")) return c <= 0;
            return c >= 0;
        }
        ok = false;
        return false;
    }

    bool notExpr(const QList<QVariant>& row) {
        if (acceptKeyword("NOT")) return !notExpr(row);
        return primary(row);
    }

    bool andExpr(const QList<QVariant>& row) {
        bool v = notExpr(row);
        while (acceptKeyword("AND")) v = notExpr(row) && v;
        return v;
    }

    bool orExpr(const QList<QVariant>& row) {
        bool v = andExpr(row);
        while (acceptKeyword("OR")) v = andExpr(row) || v;
        return v;
    }
};

// 排序规格：col [ASC|DESC]{, …}
struct SortKey { int column = -1; bool desc = false; };

bool parseSort(const QString& spec, const DTable& table, QList<SortKey>& out) {
    for (const QString& partRaw : spec.split(QLatin1Char(','))) {
        const QString part = partRaw.trimmed();
        if (part.isEmpty()) continue;
        const QStringList toks = part.split(QRegularExpression(QStringLiteral("\\s+")),
                                            Qt::SkipEmptyParts);
        if (toks.isEmpty()) continue;
        SortKey k;
        k.column = table.columnIndex(toks.at(0));
        if (k.column < 0) return false;
        if (toks.size() >= 2) {
            const QString dir = toks.at(1).toUpper();
            if (dir == QLatin1String("DESC")) k.desc = true;
            else if (dir != QLatin1String("ASC")) return false;
        }
        out.append(k);
    }
    return true;
}

// 选出命中行（下标）；filter 为空 = 全部
bool selectRows(const DTable& table, const QString& filter, const QString& sort,
                QList<int>& out)
{
    QList<int> idx;
    if (filter.trimmed().isEmpty()) {
        for (int i = 0; i < table.rows.size(); ++i) idx.append(i);
    } else {
        QList<FToken> tokens;
        if (!tokenizeFilter(filter, tokens)) return false;
        FilterParser p(tokens, table);
        for (int i = 0; i < table.rows.size(); ++i) {
            p.pos = 0;
            p.ok = true;
            if (p.orExpr(table.rows.at(i))) idx.append(i);
        }
        if (!p.ok) return false;
    }
    if (!sort.trimmed().isEmpty()) {
        QList<SortKey> keys;
        if (!parseSort(sort, table, keys)) return false;
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
            for (const SortKey& k : keys) {
                const int c = FilterParser::compare(
                    DValue::fromVariant(table.rows.at(a).value(k.column)),
                    DValue::fromVariant(table.rows.at(b).value(k.column)));
                if (c != 0) return k.desc ? c > 0 : c < 0;
            }
            return false;
        });
    }
    out = idx;
    return true;
}

// ---------------------------------------------------------------------------
// 取实参中的「数组」（列名数组 / 值数组 / 输出数组）：由节点名 + storage 读
// ---------------------------------------------------------------------------
QString arrayNameOf(const QList<const ExpressionNode*>& nodes, int i) {
    return nodes.size() > i ? refTargetName(nodes.at(i)) : QString();
}

// 去掉一层引号（DT_COLUMN_OPTIONS 的 raw 文本解析用）
QString unquote(const QString& s) {
    if (s.size() >= 2 && s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"')))
        return s.mid(1, s.size() - 2);
    return s;
}

// ---------------------------------------------------------------------------
// 通用：把「列名 → 值」写进行（成对形态 / 数组形态共用）
//   return: 写入个数；blk 报告错误（列不存在 / id 列 / 类型不符）
// ---------------------------------------------------------------------------
struct SetOutcome { int written = 0; bool idColumn = false; bool unknownColumn = false; bool typeMismatch = false; };

void applyCell(DTable& t, QList<QVariant>& row, int col, const QVariant& v, SetOutcome& r) {
    if (col < 0 || col >= t.columns.size()) { r.unknownColumn = true; return; }
    if (col == 0) { r.idColumn = true; return; }
    const DColumn& c = t.columns.at(col);
    if (!v.isValid()) { row[col] = QVariant(); ++r.written; return; }    // DBNull
    if (c.type == DType::Str) {
        if (v.userType() != QMetaType::QString) { r.typeMismatch = true; return; }
        row[col] = v.toString();
    } else {
        if (v.userType() == QMetaType::QString) { r.typeMismatch = true; return; }
        row[col] = clampInt(v.toLongLong(), c.type);
    }
    ++r.written;
}

QList<QVariant> newRow(DTable& t, bool& ok) {
    ok = true;
    QList<QVariant> row;
    row.reserve(t.columns.size());
    for (const DColumn& c : t.columns) {
        if (c.defaultValue.isValid()) row.append(c.defaultValue);
        else row.append(QVariant());        // DBNull
    }
    return row;
}

// ---------------------------------------------------------------------------
// XML（TOXML/FROMXML）：**形状自定**，只要求「调用成功 + 返回值/往返正确」
//
//   架构 XML：<xs:schema id="表名"> … <xs:element name="列" type="xs:byte|short|int|long|string"/> …
//   数据 XML：<DocumentElement><表名><列>值</列>…</表名>…</DocumentElement>
//
// 不做「与 .NET 逐位一致」：本移植的目标是**命令语义**（DT_TOXML 返回数据 XML、
//   第 2 参得到架构 XML；DT_FROMXML 用这一对把表读回来），而不是与 .NET 的
//   `WriteXmlSchema/WriteXml`（带 msdata:* 的 DataSet 专属形状）互换字节。
//   因此这里选最小自洽形状，由本实现的 DT_FROMXML 原样读回（组 39 已断言往返）。
// ---------------------------------------------------------------------------
QString xmlEscape(const QString& s) {
    QString o;
    for (const QChar c : s) {
        if (c == QLatin1Char('&')) o += QLatin1String("&amp;");
        else if (c == QLatin1Char('<')) o += QLatin1String("&lt;");
        else if (c == QLatin1Char('>')) o += QLatin1String("&gt;");
        else o += c;
    }
    return o;
}

QString typeToXsd(DType t) {
    switch (t) {
    case DType::Int8:  return QStringLiteral("xs:byte");
    case DType::Int16: return QStringLiteral("xs:short");
    case DType::Int32: return QStringLiteral("xs:int");
    case DType::Int64: return QStringLiteral("xs:long");
    case DType::Str:   return QStringLiteral("xs:string");
    }
    return QStringLiteral("xs:string");
}

bool xsdToType(const QString& xsd, DType& out) {
    const QString s = xsd.trimmed();
    if (s.endsWith(QLatin1String("byte")))  { out = DType::Int8;  return true; }
    if (s.endsWith(QLatin1String("short"))) { out = DType::Int16; return true; }
    if (s.endsWith(QLatin1String("int")))   { out = DType::Int32; return true; }
    if (s.endsWith(QLatin1String("long")))  { out = DType::Int64; return true; }
    if (s.endsWith(QLatin1String("string"))){ out = DType::Str;   return true; }
    return false;
}

QString tableSchemaXml(const DTable& t) {
    QString x;
    x += QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    x += QStringLiteral("<xs:schema id=\"") + xmlEscape(t.name)
       + QStringLiteral("\" xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">\n");
    x += QStringLiteral("  <xs:element name=\"") + xmlEscape(t.name) + QStringLiteral("\">\n");
    x += QStringLiteral("    <xs:complexType>\n      <xs:sequence>\n");
    for (const DColumn& c : t.columns) {
        x += QStringLiteral("        <xs:element name=\"") + xmlEscape(c.name)
           + QStringLiteral("\" type=\"") + typeToXsd(c.type) + QStringLiteral("\"");
        if (!c.nullable) x += QStringLiteral(" minOccurs=\"1\"");
        x += QStringLiteral(" />\n");
    }
    x += QStringLiteral("      </xs:sequence>\n    </xs:complexType>\n  </xs:element>\n");
    x += QStringLiteral("</xs:schema>");
    return x;
}

QString tableDataXml(const DTable& t) {
    QString x;
    x += QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    x += QStringLiteral("<DocumentElement>\n");
    for (const QList<QVariant>& row : t.rows) {
        x += QStringLiteral("  <") + xmlEscape(t.name) + QStringLiteral(">\n");
        for (int i = 0; i < t.columns.size(); ++i) {
            const QVariant& v = row.value(i);
            x += QStringLiteral("    <") + xmlEscape(t.columns.at(i).name) + QStringLiteral(">");
            if (v.isValid()) x += xmlEscape(v.toString());
            x += QStringLiteral("</") + xmlEscape(t.columns.at(i).name) + QStringLiteral(">\n");
        }
        x += QStringLiteral("  </") + xmlEscape(t.name) + QStringLiteral(">\n");
    }
    x += QStringLiteral("</DocumentElement>");
    return x;
}

} // namespace

// ---------------------------------------------------------------------------
// 注册
// ---------------------------------------------------------------------------
void registerDataTableExtensions(ExtensionRegistry& ext)
{
    // ---- 生命周期（Creator.Method.cs:1270）----
    ext.regExpr(QStringLiteral("DT_CREATE"), OperandType::Int, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const QString key = a.value(0).toString();
            auto& store = tableStore();
            if (store.contains(key)) { out = QVariant::fromValue<qint64>(0); return true; }
            DTable t;
            t.name = key;
            t.columns.append(DColumn{ QStringLiteral("id"), DType::Int64, false, QVariant() });
            store.insert(key, t);
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    ext.regExpr(QStringLiteral("DT_EXIST"), OperandType::Int, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            out = QVariant::fromValue<qint64>(tableStore().contains(a.value(0).toString()) ? 1 : 0);
            return true;
        });

    ext.regExpr(QStringLiteral("DT_RELEASE"), OperandType::Int, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            tableStore().remove(a.value(0).toString());
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    ext.regExpr(QStringLiteral("DT_CLEAR"), OperandType::Int, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            store[key].rows.clear();
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    ext.regExpr(QStringLiteral("DT_NOCASE"), OperandType::Int, 2, 2,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            store[key].caseSensitive = a.value(1).toLongLong() == 0;   // C#: flag==0 -> CaseSensitive=true
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    // ---- 列（Creator.Method.cs:1325）----
    ext.regExpr(QStringLiteral("DT_COLUMN_ADD"), OperandType::Int, 2, 4,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            DTable& t = store[key];
            const QString name = a.value(1).toString();
            if (t.columnIndex(name) >= 0) { out = QVariant::fromValue<qint64>(0); return true; }

            DType type = DType::Str;                 // 未指定类型：默认字符串（C# Add(name) 语义）
            bool haveType = false;
            if (a.size() >= 3 && !omitted(a.at(2))) {
                if (a.at(2).userType() == QMetaType::QString) haveType = nameToType(a.at(2).toString(), type);
                else haveType = codeToType(static_cast<int>(a.at(2).toLongLong()), type);
                if (!haveType) { out = QVariant::fromValue<qint64>(0); return true; }
            }
            const bool nullable = a.size() < 4 ? true : a.value(3).toLongLong() != 0;
            t.columns.append(DColumn{ name, type, nullable, QVariant() });
            for (QList<QVariant>& row : t.rows) row.append(QVariant());
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    ext.regExpr(QStringLiteral("DT_COLUMN_EXIST"), OperandType::Int, 2, 2,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            const DTable& t = store[key];
            const int idx = t.columnIndex(a.value(1).toString());
            out = QVariant::fromValue<qint64>(idx < 0 ? 0 : typeToCode(t.columns.at(idx).type));
            return true;
        });

    ext.regExpr(QStringLiteral("DT_COLUMN_REMOVE"), OperandType::Int, 2, 2,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            DTable& t = store[key];
            const int idx = t.columnIndex(a.value(1).toString());
            if (idx <= 0) { out = QVariant::fromValue<qint64>(0); return true; }   // id 不可删
            t.columns.removeAt(idx);
            for (QList<QVariant>& row : t.rows) if (row.size() > idx) row.removeAt(idx);
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    ext.regExpr(QStringLiteral("DT_COLUMN_NAMES"), OperandType::Int, 1, 2,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes,
               QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            const DTable& t = store[key];
            const QString target = nodes.size() >= 2 ? refTargetName(nodes.at(1)) : QString();
            const QString outName = target.isEmpty() ? QStringLiteral("RESULTS") : target;
            for (int i = 0; i < t.columns.size(); ++i)
                writeStr(ext.services().storage, outName, i, t.columns.at(i).name);
            out = QVariant::fromValue<qint64>(t.columns.size());
            return true;
        });

    ext.regExpr(QStringLiteral("DT_COLUMN_LENGTH"), OperandType::Int, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            out = QVariant::fromValue<qint64>(store.contains(key) ? store[key].columns.size() : -1);
            return true;
        });

    ext.regExpr(QStringLiteral("DT_ROW_LENGTH"), OperandType::Int, 1, 1,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            out = QVariant::fromValue<qint64>(store.contains(key) ? store[key].rows.size() : -1);
            return true;
        });

    // ---- 行（Creator.Method.cs:1393）----
    ext.regExpr(QStringLiteral("DT_ROW_ADD"), OperandType::Int, 1, 32,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes,
               QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            DTable& t = store[key];
            bool ok = false;
            QList<QVariant> row = newRow(t, ok);
            SetOutcome r;

            if (a.size() == 4) {
                // 数组形态：DT_ROW_ADD(表名, ref 列名数组, ref 值数组, 个数)
                const QString namesVar = arrayNameOf(nodes, 1);
                const QString valsVar = arrayNameOf(nodes, 2);
                const int count = static_cast<int>(a.value(3).toLongLong());
                VariableStorage* st = ext.services().storage;
                const int n = qMin(count, qMin(arrayCapacity(st, namesVar), arrayCapacity(st, valsVar)));
                for (int i = 0; i < n; ++i) {
                    const int col = t.columnIndex(st->getGlobalStr1D(namesVar, i));
                    applyCell(t, row, col, QVariant(st->getGlobalStr1D(valsVar, i)), r);
                }
            } else {
                for (int i = 1; i + 1 < a.size(); i += 2) {
                    const int col = t.columnIndex(a.value(i).toString());
                    applyCell(t, row, col, a.at(i + 1), r);
                }
            }
            if (r.idColumn || r.unknownColumn || r.typeMismatch) {
                out = QVariant::fromValue<qint64>(r.idColumn || r.unknownColumn ? 0 : -2);
                return true;
            }
            const qint64 id = nextTimePoint();
            row[0] = id;
            t.rows.append(row);
            out = QVariant::fromValue<qint64>(id);
            return true;
        });

    ext.regExpr(QStringLiteral("DT_ROW_SET"), OperandType::Int, 2, 34,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes,
               QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            DTable& t = store[key];
            const qint64 id = a.value(1).toLongLong();
            int rowIndex = -1;
            for (int i = 0; i < t.rows.size(); ++i)
                if (t.rows.at(i).value(0).toLongLong() == id) { rowIndex = i; break; }
            if (rowIndex < 0) { out = QVariant::fromValue<qint64>(-2); return true; }
            SetOutcome r;
            if (a.size() == 5) {
                const QString namesVar = arrayNameOf(nodes, 2);
                const QString valsVar = arrayNameOf(nodes, 3);
                const int count = static_cast<int>(a.value(4).toLongLong());
                VariableStorage* st = ext.services().storage;
                const int n = qMin(count, qMin(arrayCapacity(st, namesVar), arrayCapacity(st, valsVar)));
                for (int i = 0; i < n; ++i)
                    applyCell(t, t.rows[rowIndex], t.columnIndex(st->getGlobalStr1D(namesVar, i)),
                              QVariant(st->getGlobalStr1D(valsVar, i)), r);
            } else {
                for (int i = 2; i + 1 < a.size(); i += 2)
                    applyCell(t, t.rows[rowIndex], t.columnIndex(a.value(i).toString()), a.at(i + 1), r);
            }
            out = QVariant::fromValue<qint64>(r.written);
            return true;
        });

    ext.regExpr(QStringLiteral("DT_ROW_REMOVE"), OperandType::Int, 2, 3,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes,
               QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            DTable& t = store[key];
            QList<qint64> ids;
            if (a.size() == 3) {                       // 批量：id 数组 + 个数
                const QString idsVar = arrayNameOf(nodes, 1);
                VariableStorage* st = ext.services().storage;
                const int count = static_cast<int>(a.value(2).toLongLong());
                const int n = qMin(count, arrayCapacity(st, idsVar));
                for (int i = 0; i < n; ++i) ids.append(st->getGlobalInt1D(idsVar, i));
            } else {
                ids.append(a.value(1).toLongLong());
            }
            int removed = 0;
            for (int i = t.rows.size() - 1; i >= 0; --i) {
                if (ids.contains(t.rows.at(i).value(0).toLongLong())) { t.rows.removeAt(i); ++removed; }
            }
            out = QVariant::fromValue<qint64>(removed);
            return true;
        });

    // ---- 单元格（Creator.Method.cs:1569/1637）----
    auto cellIndex = [](const DTable& t, qint64 idx, bool asId) {
        if (!asId) return idx >= 0 && idx < t.rows.size() ? static_cast<int>(idx) : -1;
        for (int i = 0; i < t.rows.size(); ++i)
            if (t.rows.at(i).value(0).toLongLong() == idx) return i;
        return -1;
    };

    ext.regExpr(QStringLiteral("DT_CELL_GET"), OperandType::Int, 3, 4,
        [cellIndex](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(0); return true; }
            const DTable& t = store[key];
            const bool asId = a.size() == 4 && a.value(3).toLongLong() != 0;
            const int row = cellIndex(t, a.value(1).toLongLong(), asId);
            const int col = t.columnIndex(a.value(2).toString());
            if (row < 0 || col < 0) { out = QVariant::fromValue<qint64>(0); return true; }
            const QVariant& v = t.rows.at(row).value(col);
            out = QVariant::fromValue<qint64>(v.isValid() ? v.toLongLong() : 0);
            return true;
        });

    ext.regExpr(QStringLiteral("DT_CELL_ISNULL"), OperandType::Int, 3, 4,
        [cellIndex](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            const DTable& t = store[key];
            const bool asId = a.size() == 4 && a.value(3).toLongLong() != 0;
            const int row = cellIndex(t, a.value(1).toLongLong(), asId);
            const int col = t.columnIndex(a.value(2).toString());
            if (row < 0 || col < 0) { out = QVariant::fromValue<qint64>(-2); return true; }
            out = QVariant::fromValue<qint64>(t.rows.at(row).value(col).isValid() ? 0 : 1);
            return true;
        });

    ext.regExpr(QStringLiteral("DT_CELL_GETS"), OperandType::Str, 3, 4,
        [cellIndex](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QString(); return true; }
            const DTable& t = store[key];
            const bool asId = a.size() == 4 && a.value(3).toLongLong() != 0;
            const int row = cellIndex(t, a.value(1).toLongLong(), asId);
            const int col = t.columnIndex(a.value(2).toString());
            if (row < 0 || col < 0) { out = QString(); return true; }
            const QVariant& v = t.rows.at(row).value(col);
            out = v.isValid() ? v.toString() : QString();
            return true;
        });

    ext.regExpr(QStringLiteral("DT_CELL_SET"), OperandType::Int, 3, 5,
        [cellIndex](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            DTable& t = store[key];
            const bool asId = a.size() == 5 && a.value(4).toLongLong() != 0;
            const int row = cellIndex(t, a.value(1).toLongLong(), asId);
            const QString colName = a.value(2).toString();
            const int col = t.columnIndex(colName);
            if (col == 0) { out = QVariant::fromValue<qint64>(0); return true; }      // id 不可改
            if (row < 0 || col < 0) { out = QVariant::fromValue<qint64>(-3); return true; }
            const QVariant v = a.size() > 3 ? a.at(3) : QVariant();
            SetOutcome r;
            applyCell(t, t.rows[row], col, v, r);
            if (r.typeMismatch) { out = QVariant::fromValue<qint64>(-2); return true; }
            out = QVariant::fromValue<qint64>(r.written > 0 ? 1 : -3);
            return true;
        });

    // ---- DT_SELECT（Creator.Method.cs:1679）----
    ext.regExpr(QStringLiteral("DT_SELECT"), OperandType::Int, 1, 4,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes,
               QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QVariant::fromValue<qint64>(-1); return true; }
            const DTable& t = store[key];
            const QString filter = a.size() > 1 ? a.value(1).toString() : QString();
            const QString sort = a.size() > 2 ? a.value(2).toString() : QString();
            QList<int> ids;
            if (!selectRows(t, filter, sort, ids)) { out = QVariant::fromValue<qint64>(0); return true; }

            const bool toResult = a.size() != 4;
            VariableStorage* st = ext.services().storage;
            if (toResult) {
                writeInt(st, QStringLiteral("RESULT"), 0, ids.size());
                const int cap = arrayCapacity(st, QStringLiteral("RESULT"));
                const int n = cap > 1 ? qMin(ids.size(), cap - 1) : 0;
                for (int i = 0; i < n; ++i)
                    writeInt(st, QStringLiteral("RESULT"), i + 1, t.rows.at(ids.at(i)).value(0).toLongLong());
            } else {
                const QString target = refTargetName(nodes.value(3));
                const int cap = arrayCapacity(st, target);
                const int n = cap > 0 ? qMin(ids.size(), cap) : ids.size();
                for (int i = 0; i < n; ++i)
                    writeInt(st, target, i, t.rows.at(ids.at(i)).value(0).toLongLong());
            }
            out = QVariant::fromValue<qint64>(ids.size());
            return true;
        });

    // ---- TOXML / FROMXML（Creator.Method.cs:1715/1745）----
    ext.regExpr(QStringLiteral("DT_TOXML"), OperandType::Str, 1, 2,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>& nodes,
               QVariant& out) {
            auto& store = tableStore();
            const QString key = a.value(0).toString();
            if (!store.contains(key)) { out = QString(); return true; }
            const DTable& t = store[key];
            const QString schema = tableSchemaXml(t);
            if (nodes.size() >= 2) {
                const QString target = refTargetName(nodes.at(1));
                if (!target.isEmpty()) writeStr(ext.services().storage, target, 0, schema);
            } else {
                writeStr(ext.services().storage, QStringLiteral("RESULTS"), 1, schema);
            }
            out = tableDataXml(t);
            return true;
        });

    ext.regExpr(QStringLiteral("DT_FROMXML"), OperandType::Int, 3, 3,
        [](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const QString key = a.value(0).toString();
            const QString schemaXml = a.value(1).toString();
            const QString dataXml = a.value(2).toString();
            if (!schemaXml.contains(QLatin1String("<xs:schema")) || schemaXml.trimmed().isEmpty()) {
                out = QVariant::fromValue<qint64>(0);
                return true;
            }
            DTable t;
            t.name = key;
            // 架构：取 <xs:element name="..." type="..."/>（第一层）
            QRegularExpression re(QStringLiteral("<xs:element\\s+name=\"([^\"]+)\"\\s+type=\"([^\"]+)\""));
            auto it = re.globalMatch(schemaXml);
            while (it.hasNext()) {
                const auto m = it.next();
                DType type = DType::Str;
                xsdToType(m.captured(2), type);
                t.columns.append(DColumn{ m.captured(1), type,
                                          m.captured(1) == QLatin1String("id") ? false : true,
                                          QVariant() });
            }
            if (t.columns.isEmpty()) { out = QVariant::fromValue<qint64>(0); return true; }

            // 数据：<表元素名><列>值</列>…</表元素名>
            // 表元素名取架构里「无 type 的那个 <xs:element name="X">」（.NET WriteXml 用表名），
            // 没找到才回落到 key。
            QString rowTag = key;
            {
                QRegularExpression rootRe(QStringLiteral("<xs:element\\s+name=\"([^\"]+)\"(?![^>]*type=)"));
                const auto rm = rootRe.match(schemaXml);
                if (rm.hasMatch()) rowTag = rm.captured(1);
            }
            QRegularExpression rowRe(QStringLiteral("<") + QRegularExpression::escape(rowTag)
                                     + QStringLiteral(">(.*?)</") + QRegularExpression::escape(rowTag)
                                     + QStringLiteral(">"),
                                     QRegularExpression::DotMatchesEverythingOption);
            auto rows = rowRe.globalMatch(dataXml);
            while (rows.hasNext()) {
                const QString body = rows.next().captured(1);
                bool ok = false;
                QList<QVariant> row = newRow(t, ok);
                for (int c = 0; c < t.columns.size(); ++c) {
                    QRegularExpression cellRe(QStringLiteral("<") + QRegularExpression::escape(t.columns.at(c).name)
                                              + QStringLiteral(">(.*?)</") + QRegularExpression::escape(t.columns.at(c).name)
                                              + QStringLiteral(">"),
                                              QRegularExpression::DotMatchesEverythingOption);
                    const auto m = cellRe.match(body);
                    if (!m.hasMatch()) continue;
                    const QString raw = m.captured(1);
                    if (raw.isEmpty()) { row[c] = QVariant(); continue; }
                    if (t.columns.at(c).type == DType::Str) row[c] = raw;
                    else row[c] = clampInt(raw.toLongLong(), t.columns.at(c).type);
                }
                t.rows.append(row);
            }
            auto& store = tableStore();
            store[key] = t;
            out = QVariant::fromValue<qint64>(1);
            return true;
        });

    // ---- DT_COLUMN_OPTIONS（命令：`DT_COLUMN_OPTIONS 表, 列, DEFAULT = 值`）----
    // 非常规实参语法（`KEY = 值`），故直接解析 LogicalLine::raw（对齐 C#
    // SP_DT_COLUMN_OPTIONS_ArgumentBuilder）。
    ext.reg(QStringLiteral("DT_COLUMN_OPTIONS"),
        [&ext](const LogicalLine& line, const QList<Operand>&) -> bool {
            // raw 是整行原文（含指令名）；先剥掉指令名再按顶层逗号切分实参
            QString body = line.raw.trimmed();
            const int sp = body.indexOf(QRegularExpression(QStringLiteral("\\s")));
            if (sp > 0) body = body.mid(sp + 1).trimmed();
            const QStringList parts = body.split(QLatin1Char(','));
            if (parts.size() < 3) return true;
            const QString tableName = unquote(parts.at(0).trimmed());
            const QString columnName = unquote(parts.at(1).trimmed());
            QString rest = parts.at(2);
            for (int i = 3; i < parts.size(); ++i) rest += QLatin1Char(',') + parts.at(i);
            const int eq = rest.indexOf(QLatin1Char('='));
            if (eq < 0) return true;
            const QString kw = rest.left(eq).trimmed();
            const QString valText = rest.mid(eq + 1).trimmed();
            if (kw.compare(QLatin1String("DEFAULT"), Qt::CaseInsensitive) != 0) return true;

            auto& store = tableStore();
            if (!store.contains(tableName)) return true;        // C# 会抛错；此处静默
            DTable& t = store[tableName];
            const int col = t.columnIndex(columnName);
            if (col < 0) return true;
            const ExtensionRegistry::Services& sv = ext.services();
            const QVariant v = sv.evaluate ? sv.evaluate(valText) : QVariant(valText);
            DColumn& c = t.columns[col];
            if (c.type == DType::Str) c.defaultValue = v.userType() == QMetaType::QString ? v : QVariant(v.toString());
            else c.defaultValue = clampInt(v.toLongLong(), c.type);
            return true;
        });
}

} // namespace forkdt
