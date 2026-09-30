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
#include "expression_evaluator.h"
#include "system_variables.h"
#include "constant_table.h"
#include "expression_parser.h"
#include "expression_ast.h"
#include "expression_lexer.h"
#include "operator_table.h"
#include "strform_parser.h"
#include "variable_storage.h"
#include "variable_config.h"
#include "game_base_data.h"
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSet>
#include <QTime>
#include <QVector>
#include <cmath>
#include <cstdio>
#include <utility>

namespace {
// 运行期类型探测（仅当 AST 类型未知时使用）
inline bool isRuntimeString(const QVariant& v) noexcept {
    const int id = v.typeId();
    return id == QMetaType::QString || id == QMetaType::QChar;
}

// ---------------------------------------------------------------------------
// TOSTR 的第 2 参数：.NET 数字格式串（C# `i.ToString(format)`）
//
// Erb 里的 TOSTR 直接转发给 .NET，所以语义必须对齐。这里实现 ERB 实际会用到的
// 子集：
//   标准说明符  D/d 十进制补零、X/x 十六进制、N/n 千分位、F/f 定点、G/g、
//               P/p 百分比、E/e 科学计数
//   自定义      #（可选位）0（必需位）. ,（分组 / 末尾逗号 = 除 1000）
//               % / ‰（放大的字面量）\ 转义 "…"/'…' 字面量 ; 分正/负/零段
// 例：`TOSTR(1234, "0000000000")` -> "0000001234"
//     `TOSTR(1234567, "#,###")`   -> "1,234,567"
// ---------------------------------------------------------------------------

// 千分位分组
QString groupThousands(const QString& digits) {
    QString g;
    for (int i = digits.size(); i > 0; i -= 3) {
        const int from = qMax(0, i - 3);
        g.prepend(digits.mid(from, i - from));
        if (from > 0) g.prepend(QLatin1Char(','));
    }
    return g;
}

QString fixedNumber(qint64 value, int decimals, bool grouping) {
    const bool neg = value < 0;
    const quint64 absV = neg ? (quint64(0) - quint64(value)) : quint64(value);
    QString digits = QString::number(absV);
    if (grouping) digits = groupThousands(digits);
    if (decimals > 0) digits += QLatin1Char('.') + QString(decimals, QLatin1Char('0'));
    if (neg) digits.prepend(QLatin1Char('-'));
    return digits;
}

// 单字母标准格式说明符？
bool stdSpecifier(const QString& format, QChar& spec, int& precision) {
    if (format.isEmpty()) return false;
    const QChar c = format.at(0).toUpper();
    if (!QStringLiteral("DXNFGPE").contains(c)) return false;
    if (format.size() == 1) { spec = c; precision = -1; return true; }
    bool ok = false;
    const int p = format.mid(1).toInt(&ok);
    if (!ok || p < 0 || p > 99) return false;
    spec = c; precision = p;
    return true;
}

QString formatStandard(qint64 value, QChar spec, int precision) {
    const bool neg = value < 0;
    switch (spec.unicode()) {
    case 'D': {
        QString d = QString::number(neg ? (quint64(0) - quint64(value)) : quint64(value));
        const int n = precision < 0 ? 1 : precision;
        while (d.size() < n) d.prepend(QLatin1Char('0'));
        return neg ? (QLatin1Char('-') + d) : d;
    }
    case 'X': {
        // .NET 的 X 按二进制补码输出（与 quint64 转换一致）
        QString h = QString::number(static_cast<quint64>(value), 16).toUpper();
        if (precision > 0) while (h.size() < precision) h.prepend(QLatin1Char('0'));
        return h;
    }
    case 'N': return fixedNumber(value, precision < 0 ? 2 : precision, true);
    case 'F': return fixedNumber(value, precision < 0 ? 2 : precision, false);
    case 'P': {
        const int n = precision < 0 ? 2 : precision;
        return fixedNumber(value * 100, n, true) + QLatin1String(" %");
    }
    case 'E': {
        const int n = precision < 0 ? 6 : precision;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*E", n, static_cast<double>(value));
        return QString::fromLatin1(buf);
    }
    default:  // G
        return QString::number(value);
    }
}

// 按未转义的 ';' 切成 1~3 段（对齐 .NET 自定义格式的正/负/零段）
QStringList splitFormatSections(const QString& format) {
    QStringList out;
    QString cur;
    QChar quote;
    for (int i = 0; i < format.size(); ++i) {
        const QChar c = format.at(i);
        if (!quote.isNull()) {
            cur += c;
            if (c == quote) quote = QChar();
            continue;
        }
        if (c == QLatin1Char('\\')) {
            cur += c;
            if (i + 1 < format.size()) cur += format.at(++i);
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; cur += c; continue; }
        if (c == QLatin1Char(';')) { out.append(cur); cur.clear(); continue; }
        cur += c;
    }
    out.append(cur);
    return out;
}

inline bool isDigitPlaceholder(QChar c) {
    return c == QLatin1Char('#') || c == QLatin1Char('0');
}

// 展开段里的字面量：\x 转义；"…"/'…' 去掉引号保留内容
QString expandLiterals(const QString& text) {
    QString out;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\\')) {
            if (i + 1 < text.size()) out += text.at(++i);
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const QChar q = c;
            for (++i; i < text.size() && text.at(i) != q; ++i) out += text.at(i);
            continue;
        }
        out += c;
    }
    return out;
}

QString formatCustom(qint64 value, const QString& format) {
    const QStringList sections = splitFormatSections(format);
    QString section;
    bool negative = value < 0;
    if (sections.size() <= 1) {
        section = sections.value(0);
    } else if (sections.size() == 2) {
        // 两段 = 非负;负（负段用绝对值格式化，符号由该段自己写）
        section = negative ? sections.at(1) : sections.at(0);
        negative = false;
    } else {
        if (value == 0) { section = sections.at(2); negative = false; }
        else { section = negative ? sections.at(1) : sections.at(0); negative = false; }
    }

    // 占位符区间之外是前后缀字面量
    int firstPh = -1, lastPh = -1;
    for (int i = 0; i < section.size(); ++i) {
        if (isDigitPlaceholder(section.at(i))) {
            if (firstPh < 0) firstPh = i;
            lastPh = i;
        }
    }
    if (firstPh < 0) return expandLiterals(section);       // 纯字面量

    const QString prefix = expandLiterals(section.left(firstPh));
    const QString suffix = expandLiterals(section.mid(lastPh + 1));
    const QString core = section.mid(firstPh, lastPh - firstPh + 1);

    const int dot = core.indexOf(QLatin1Char('.'));
    QString intPart = dot < 0 ? core : core.left(dot);
    QString decPart = dot < 0 ? QString() : core.mid(dot + 1);

    // 末尾逗号 = 每 1000 缩放一次；其余逗号 = 千分位分组
    int scaling = 0;
    while (intPart.endsWith(QLatin1Char(','))) { intPart.chop(1); ++scaling; }
    const bool grouping = intPart.contains(QLatin1Char(','));

    quint64 absV = negative ? (quint64(0) - quint64(value)) : quint64(value);
    for (int i = 0; i < scaling; ++i) absV /= 1000;
    for (const QChar c : intPart) {                        // % / ‰ 放大
        if (c == QLatin1Char('%')) absV *= 100;
        else if (c == QChar(0x2030)) absV *= 1000;
    }
    for (const QChar c : decPart) {                        // 小数段的 % / ‰ 同样放大
        if (c == QLatin1Char('%')) absV *= 100;
        else if (c == QChar(0x2030)) absV *= 1000;
    }

    int minInt = 0;
    for (const QChar c : intPart) if (c == QLatin1Char('0')) ++minInt;
    int decimals = 0;
    for (const QChar c : decPart) if (isDigitPlaceholder(c)) ++decimals;

    QString digits = QString::number(absV);
    const bool allOptional = (minInt == 0);
    if (absV == 0 && allOptional) digits.clear();          // 全可选位 + 0 -> 整部留空
    if (!digits.isEmpty() && grouping) digits = groupThousands(digits);
    if (digits.size() < minInt) digits.prepend(QString(minInt - digits.size(), QLatin1Char('0')));
    if (decimals > 0) {
        if (digits.isEmpty() && !allOptional) digits = QStringLiteral("0");
        digits += QLatin1Char('.') + QString(decimals, QLatin1Char('0'));
    }
    if (negative) digits.prepend(QLatin1Char('-'));
    return prefix + digits + suffix;
}

QString formatDotNetNumber(qint64 value, const QString& format) {
    if (format.isEmpty()) return QString::number(value);
    QChar spec;
    int precision = -1;
    if (stdSpecifier(format, spec, precision)) return formatStandard(value, spec, precision);
    return formatCustom(value, format);
}
} // namespace

ExpressionEvaluator::ExpressionEvaluator(QObject *parent)
    : QObject(parent)
{
}

QVariant ExpressionEvaluator::evaluate(const QString &expression, VariableStorage *storage, GameBaseData *gameBaseData)
{
    if (!storage) {
        emit evaluationError(expression, "VariableStorage is null");
        return QVariant();
    }

    ExpressionLexer lexer;
    auto tokens = lexer.tokenize(expression);
    if (tokens.isEmpty()) {
        return QVariant();
    }

    ExpressionParser parser;
    auto ast = parser.parse(tokens);

    if (!ast) {
        emit evaluationError(expression, "Failed to parse expression AST");
        return QVariant();
    }

    QVariant result = evaluateNode(*ast, storage, gameBaseData);

    emit evaluationFinished(expression, result);
    return result;
}

// NEW: Direct AST evaluation entry points (for cached AST)
QVariant ExpressionEvaluator::evaluate(const ExpressionNode &node, VariableStorage *storage, GameBaseData *gameBaseData)
{
    if (!storage) {
        return QVariant();
    }

    QVariant result = evaluateNode(node, storage, gameBaseData);
    return result;
}

bool ExpressionEvaluator::evaluateInt(const ExpressionNode &node, VariableStorage *storage, qint64 &result)
{
    if (!storage) {
        return false;
    }

    QVariant value = evaluate(node, storage);
    
    if (value.isValid() && value.canConvert<qint64>()) {
        result = value.value<qint64>();
        return true;
    }
    
    return false;
}

bool ExpressionEvaluator::evaluateStr(const ExpressionNode &node, VariableStorage *storage, QString &result)
{
    if (!storage) {
        return false;
    }

    QVariant value = evaluate(node, storage);
    
    if (value.isValid() && value.canConvert<QString>()) {
        result = value.toString();
        return true;
    }
    
    return false;
}

QVariant ExpressionEvaluator::slotEvaluate(const QString &expression, VariableStorage *storage, GameBaseData *gameBaseData)
{
    return evaluate(expression, storage, gameBaseData);
}

QVariant ExpressionEvaluator::evaluateNode(const ExpressionNode &node, VariableStorage *storage, GameBaseData *gameBaseData)
{
    // tag 分派：switch 生成跳转表，分支可预测；避免 dynamic_cast(RTTI) 热路径开销。
    switch (node.kind()) {
    case NodeKind::Literal:
        return evaluateLiteral(static_cast<const LiteralNode&>(node));
    case NodeKind::Variable:
        return evaluateVariable(static_cast<const VariableNode&>(node), storage, gameBaseData);
    case NodeKind::BinaryOp:
        return evaluateBinaryOp(static_cast<const BinaryOpNode&>(node), storage, gameBaseData);
    case NodeKind::UnaryOp:
        return evaluateUnaryOp(static_cast<const UnaryOpNode&>(node), storage, gameBaseData);
    case NodeKind::Function:
        return evaluateFunction(static_cast<const FunctionNode&>(node), storage, gameBaseData);
    case NodeKind::StrForm:
        return evaluateStrForm(static_cast<const StrFormNode&>(node), storage, gameBaseData);
    case NodeKind::If: {
        const auto& ifNode = static_cast<const IfNode&>(node);
        const QVariant cond = evaluateNode(*ifNode.condition(), storage, gameBaseData);
        if (cond.toLongLong() != 0) [[likely]] {
            return evaluateNode(*ifNode.thenExpr(), storage, gameBaseData);
        }
        if (ifNode.elseExpr()) [[likely]] {
            return evaluateNode(*ifNode.elseExpr(), storage, gameBaseData);
        }
        return QVariant::fromValue<qint64>(0);
    }
    }
    std::unreachable();
}

QVariant ExpressionEvaluator::evaluateLiteral(const LiteralNode &node)
{
    // 强类型字面量：整数返回 qint64，字符串返回 QString
    if (node.valueType() == OperandType::Str) {
        return QVariant(node.strValue());
    }
    return QVariant::fromValue<qint64>(node.intValue());
}

// StrForm：文本片段 + 内嵌表达式片段（内嵌表达式按类型转字符串）

// ---------------------------------------------------------------------------
// 二元求值的两个快/慢路径（供 evaluateBinaryOp 调用）
// ---------------------------------------------------------------------------
QVariant ExpressionEvaluator::evaluateIntBinary(TokenType op, qint64 l, qint64 r)
{
    using QT = QVariant;
    switch (op) {
    case TokenType::PLUS:          return QT::fromValue<qint64>(l + r);
    case TokenType::MINUS:         return QT::fromValue<qint64>(l - r);
    case TokenType::MULTIPLY:      return QT::fromValue<qint64>(l * r);
    case TokenType::DIVIDE:        return QT::fromValue<qint64>(r == 0 ? 0 : l / r);
    case TokenType::MODULO:        return QT::fromValue<qint64>(r == 0 ? 0 : l % r);
    case TokenType::LESS_THAN:     return QT::fromValue<qint64>(l <  r ? 1 : 0);
    case TokenType::LESS_EQUAL:    return QT::fromValue<qint64>(l <= r ? 1 : 0);
    case TokenType::GREATER_THAN:  return QT::fromValue<qint64>(l >  r ? 1 : 0);
    case TokenType::GREATER_EQUAL: return QT::fromValue<qint64>(l >= r ? 1 : 0);
    case TokenType::EQUALS:        return QT::fromValue<qint64>(l == r ? 1 : 0);
    case TokenType::NOT_EQUALS:    return QT::fromValue<qint64>(l != r ? 1 : 0);
    case TokenType::AND:           return QT::fromValue<qint64>((l != 0 && r != 0) ? 1 : 0);
    case TokenType::OR:            return QT::fromValue<qint64>((l != 0 || r != 0) ? 1 : 0);
    case TokenType::LOGICAL_XOR:   return QT::fromValue<qint64>((l != 0) != (r != 0) ? 1 : 0);
    case TokenType::LOGICAL_NAND:  return QT::fromValue<qint64>(!((l != 0) && (r != 0)) ? 1 : 0);
    case TokenType::LOGICAL_NOR:   return QT::fromValue<qint64>(!((l != 0) || (r != 0)) ? 1 : 0);
    case TokenType::BIT_AND:       return QT::fromValue<qint64>(l & r);
    case TokenType::BIT_OR:        return QT::fromValue<qint64>(l | r);
    case TokenType::BIT_XOR:       return QT::fromValue<qint64>(l ^ r);
    case TokenType::SHIFT_LEFT:    return QT::fromValue<qint64>(l << r);
    case TokenType::SHIFT_RIGHT:   return QT::fromValue<qint64>(l >> r);
    default: break;
    }
    return QVariant();
}

QVariant ExpressionEvaluator::evaluateStrBinary(TokenType op, const QString& ls, const QString& rs,
                                                const QVariant& left, const QVariant& right)
{
    switch (op) {
    case TokenType::PLUS:
        return QVariant(ls + rs);
    case TokenType::MULTIPLY:   // 'str' * n
        return QVariant(ls.repeated(static_cast<int>(right.toLongLong())));
    case TokenType::EQUALS:        return QVariant::fromValue<qint64>(ls == rs ? 1 : 0);
    case TokenType::NOT_EQUALS:    return QVariant::fromValue<qint64>(ls != rs ? 1 : 0);
    case TokenType::LESS_THAN:     return QVariant::fromValue<qint64>(ls <  rs ? 1 : 0);
    case TokenType::LESS_EQUAL:    return QVariant::fromValue<qint64>(ls <= rs ? 1 : 0);
    case TokenType::GREATER_THAN:  return QVariant::fromValue<qint64>(ls >  rs ? 1 : 0);
    case TokenType::GREATER_EQUAL: return QVariant::fromValue<qint64>(ls >= rs ? 1 : 0);
    default: break;
    }
    Q_UNUSED(left);
    return QVariant();
}

QVariant ExpressionEvaluator::evaluateStrForm(const StrFormNode &node, VariableStorage *storage, GameBaseData *gameBaseData)
{
    QString out;
    for (const StrFormPart &part : node.parts()) {
        if (part.type == StrFormPartType::Text) {
            out += part.text;
            continue;
        }
        if (!part.expression) continue;
        const QVariant v = evaluateNode(*part.expression, storage, gameBaseData);
        QString value = v.toString();
        if (part.width) {
            const qint64 width = qBound<qint64>(0LL, evaluateNode(*part.width, storage, gameBaseData).toLongLong(), 1000000LL);
            int units = 0;
            for (const QChar c : value) units += c.unicode() < 0x80 || (c.unicode() >= 0xff61 && c.unicode() <= 0xff9f) ? 1 : 2;
            const QString padding(qMax<qint64>(0, width - units), QLatin1Char(' '));
            value = part.leftAlign ? value + padding : padding + value;
        }
        out += value;
    }
    return QVariant(out);
}

// 变量下标的求值：整数直接使用；字符串（如 CSV 常量名 / @"..." 格式化串）
// 经 ConstantTable 映射为整数下标（对齐 C# VariableStrArgTerm.GetIntValue）。
qint64 ExpressionEvaluator::resolveIndex(const VariableNode& node, int index,
                                         VariableStorage* storage, GameBaseData* gameBaseData)
{
    if (index < 0 || index >= node.indices().size()) return 0;
    const ExpressionNode& idx = *node.indices().at(index);
    QVariant value = evaluateNode(idx, storage, gameBaseData);
    // 字符串下标判定：解析期已定型（强类型）或运行期确实是字符串
    const bool isStr = (idx.valueType() == OperandType::Str)
                       || value.userType() == QMetaType::QString;
    if (isStr) {
        const QString name = value.toString();
        if (m_constantTable) {
            const int mapped = m_constantTable->indexForVariable(node.name(), name);
            if (mapped >= 0) return mapped;
        }
        return name.toLongLong();   // 退化为数值字面量（如 "3"）
    }
    // 裸标识符下标的运行期兜底：内部解析路径（evaluate(QString) / evalExpressionCached
    // 的回退分支）没有挂常量名提供器，`TALENT:ARG:性別` 的 `性別` 会解析成未知变量
    // 求值成 0。若该标识符恰好是本变量的 CSV 常量名（C# isDefined 优先于变量引用），
    // 必须按常量名映射。
    if (const auto* varIdx = dynamic_cast<const VariableNode*>(&idx)) {
        if (varIdx->indices().isEmpty() && m_constantTable) {
            const int mapped = m_constantTable->indexForVariable(node.name(), varIdx->name());
            if (mapped >= 0) return mapped;
        }
    }
    return value.toLongLong();
}

// 变量声明维度（1/2/3）：由 EraParseTable 的 VariableTable 注入；
// 未注入或未知 -> 1（此时多余下标按 C# Int1DVariableToken 只取第一个）
int ExpressionEvaluator::variableDimension(const QString& name) const
{
    if (m_variableDimProvider) {
        const int d = m_variableDimProvider(name);
        if (d >= 1 && d <= 3) return d;
    }
    return 1;
}

// 解析全部下标（数组变量的 2D/3D 访问）
QList<int> ExpressionEvaluator::resolveIndices(const VariableNode& node, VariableStorage* storage,
                                              GameBaseData* gameBaseData)
{
    QList<int> out;
    const auto& indices = node.indices();
    out.reserve(indices.size());
    for (int i = 0; i < indices.size(); ++i) {
        out.append(static_cast<int>(resolveIndex(node, i, storage, gameBaseData)));
    }
    return out;
}

// 变量写入（整型）：与 evaluateVariable 的读取路径对称，供 ++/-- 的副作用使用
bool ExpressionEvaluator::assignVariable(const VariableNode& node, VariableStorage* storage,
                                         GameBaseData* gameBaseData, qint64 value)
{
    if (!storage) {
        return false;
    }
    const QString name = node.name();
    const QString upper = name.toUpper();
    if (storage->hasParameter(name)) {
        storage->setParameter(name, value);
        return true;
    }
    // 角色数据变量（++/-- 的写回）与读取路径对称
    if (storage->isCharaDataVariable(name)) {
        if (storage->isCharaDataString(name)) return false;   // 字符串不能自增
        const QList<int> cids = resolveIndices(node, storage, gameBaseData);
        int charaId = 0;
        QList<int> elems;
        storage->reduceCharaArgs(name, cids, charaId, elems);
        if (storage->charaDataDimension(name) >= 2)
            storage->setCharaInt3D(name, charaId, elems.value(0), elems.value(1), value);
        else
            storage->setCharaInt(name, charaId, elems.value(0), value);
        return true;
    }
    const QList<int> ids = resolveIndices(node, storage, gameBaseData);
    const int idx = ids.isEmpty() ? 0 : ids.first();
    const int dim = variableDimension(name);
    // `NAME:i:j` / `NAME:i:j:k`（用户 2D/3D 数组）
    if (dim >= 2 && ids.size() >= 2 && !storage->hasSystemVariable(name)) {
        if (dim >= 3 && ids.size() >= 3) {
            storage->setGlobalInt3D(name, ids.at(0), ids.at(1), ids.at(2), value);
        } else {
            storage->setGlobalInt2D(name, ids.at(0), ids.at(1), value);
        }
        return true;
    }
    if (upper == QLatin1String("ARG")) { storage->setArgInt(idx, value); return true; }
    if (upper == QLatin1String("LOCAL")) {
        storage->setLocalInt(idx, value);
        return true;
    }
    if (upper == QLatin1String("ARGS") || upper == QLatin1String("LOCALS")
        || upper == QLatin1String("RESULTS")) {
        return false;   // 字符串变量不能自增
    }
    if (storage->hasSystemVariable(name)) {
        storage->setSystemVariable(name, idx, value);
        return true;
    }
    storage->setGlobalInt1D(name, idx, value);
    return true;
}

QVariant ExpressionEvaluator::evaluateVariable(const VariableNode &node, VariableStorage *storage, GameBaseData *gameBaseData)
{
    if (!storage) {
        return QVariant();
    }
    
    QString varName = node.name();
    if (storage->hasParameter(varName)) return storage->parameter(varName);

    // ---- 角色数据变量（CFLAG/TALENT/… 内建 + 用户 #DIM(S) CHARADATA）----
    // 必须按 (角色号, 元素下标) 存取。此前走通用 1D 全局路径，把角色号之后的
    // 元素下标整个丢掉，导致同一角色的所有元素挤在一个槽位互相覆盖。
    if (storage->isCharaDataVariable(varName)) {
        const QList<int> ids = resolveIndices(node, storage, gameBaseData);
        int charaId = 0;
        QList<int> elems;
        storage->reduceCharaArgs(varName, ids, charaId, elems);
        if (storage->isCharaDataString(varName)) {
            return QVariant(storage->getCharaStr(varName, charaId, elems.value(0)));
        }
        if (storage->charaDataDimension(varName) >= 2) {
            return QVariant::fromValue<qint64>(
                storage->getCharaInt3D(varName, charaId, elems.value(0), elems.value(1)));
        }
        return QVariant::fromValue<qint64>(storage->getCharaInt(varName, charaId, elems.value(0)));
    }
    
    // Check if this is a GameBase variable
    if (varName.startsWith("GAMEBASE_") && gameBaseData) {
        QString key = varName.mid(9);  // Remove "GAMEBASE_" prefix
        QString value = gameBaseData->get(key);
        if (!value.isEmpty()) {
            return QVariant(value);
        }
    }
    
    // 用户函数参数 / 局部变量：ARG / LOCAL（两套独立数组）
    {
        const QString upper = varName.toUpper();
        if (upper == QLatin1String("ARG")) {
            int idx = 0;
            if (node.isArray() && !node.indices().isEmpty()) {
                idx = static_cast<int>(resolveIndex(node, 0, storage, gameBaseData));
            }
            return QVariant(storage->getArgInt(idx));
        }
        if (upper == QLatin1String("LOCAL")) {
            int idx = 0;
            if (node.isArray() && !node.indices().isEmpty()) {
                idx = static_cast<int>(resolveIndex(node, 0, storage, gameBaseData));
            }
            return QVariant(storage->getLocalInt(idx));
        }
        if (upper == QLatin1String("ARGS")) {
            int idx = 0;
            if (node.isArray() && !node.indices().isEmpty()) {
                idx = static_cast<int>(resolveIndex(node, 0, storage, gameBaseData));
            }
            return QVariant(storage->getArgStr(idx));
        }
        if (upper == QLatin1String("LOCALS")) {
            // LOCALS = 字符串局部槽
            int idx = 0;
            if (node.isArray() && !node.indices().isEmpty()) {
                idx = static_cast<int>(resolveIndex(node, 0, storage, gameBaseData));
            }
            return QVariant(storage->getLocalStr(idx));
        }
        if (upper == QLatin1String("RESULTS")) {
            return QVariant(storage->getLocalStr(0));
        }
        // 用户函数形参别名（@F(A,B) 内的 A/B -> LOCAL 槽位）
        const int aliasIdx = storage->localAliasIndex(varName);
        if (aliasIdx >= 0) {
            return QVariant(storage->getLocalInt(aliasIdx));
        }
    }

    // ---- LINECOUNT：控制台当前行数（C# console.LineCount）----
    if (m_lineCountProvider && varName.compare(QLatin1String("LINECOUNT"), Qt::CaseInsensitive) == 0) {
        return QVariant::fromValue<qint64>(m_lineCountProvider());
    }

    // ---- RAND：伪随机变量（C# RandToken）----
    //   `RAND:n` -> [0, n) 的随机数；n<=0 视为 0（C# 会报错，这里容错）
    if (varName.compare(QLatin1String("RAND"), Qt::CaseInsensitive) == 0) {
        qint64 n = 0;
        if (node.isArray() && !node.indices().isEmpty()) {
            n = resolveIndex(node, 0, storage, gameBaseData);
        }
        if (n <= 0) return QVariant::fromValue<qint64>(0);
        return QVariant::fromValue<qint64>(m_rand.nextInt(n));
    }

    // ---- RANDDATA：随机数状态（C# RANDDATA 系统变量，长度 625）----
    if (varName.compare(QLatin1String("RANDDATA"), Qt::CaseInsensitive) == 0) {
        const QList<qint64> st = m_rand.state();
        int idx = 0;
        if (node.isArray() && !node.indices().isEmpty()) {
            idx = static_cast<int>(resolveIndex(node, 0, storage, gameBaseData));
        }
        if (idx < 0 || idx >= st.size()) return QVariant::fromValue<qint64>(0);
        return QVariant::fromValue<qint64>(st.at(idx));
    }

    // ---- #DIM CONST 常量（求值期查表；对齐 C# 常数）----
    //   标量常数：`#DIM CONST X = 5` -> X
    //   常数数组：`#DIM CONST X, 3 = 1, 2, 3` -> X:i（必须按下标取，不能一律返回首元素）
    if (node.isArray() && !node.indices().isEmpty() && m_constArrayProvider
        && (!m_constArrayChecker || m_constArrayChecker(varName))) {
        // 注意：只有确认是常数数组才求值下标 —— 否则下标表达式里的
        // 副作用（如 `BAG:(I++)`）会被算两遍
        const int idx = static_cast<int>(resolveIndex(node, 0, storage, gameBaseData));
        QVariant cv;
        if (m_constArrayProvider(varName, idx, cv) && cv.isValid()) {
            return cv;
        }
    }
    if (!node.isArray() && m_constProvider) {
        QVariant cv;
        if (m_constProvider(varName, cv) && cv.isValid()) {
            return cv;
        }
    }

    // ---- 字符串变量（用户全局字符串 / 系统字符串）----
    // 对齐 Emuera：#DIMS/#GLOBALS 声明的字符串变量；此前只会按整数读取。
    if (node.valueType() == OperandType::Str) {
        const QString upper = varName.toUpper();
        if (upper == QLatin1String("RESULTS")) {
            return QVariant(storage->getLocalStr(0));
        }
        if (upper == QLatin1String("SAVEDATA_TEXT")) {
            return QVariant(storage->getSystemStr(upper, 0));
        }
        int idx = 0;
        if (node.isArray() && !node.indices().isEmpty()) {
            if (variableDimension(varName) >= 2 && node.indices().size() >= 2) {
                const QList<int> ids = resolveIndices(node, storage, gameBaseData);
                if (ids.size() >= 2) {
                    return QVariant(storage->getGlobalStr2D(varName, ids.at(0), ids.at(1)));
                }
            }
            idx = static_cast<int>(resolveIndex(node, 0, storage, gameBaseData));
        }
        return QVariant(storage->getGlobalStr1D(varName, idx));
    }

    // Check if this is an array access
    if (node.isArray()) {
        // Evaluate the index expression
        const auto& indices = node.indices();
        if (indices.isEmpty()) {
            return QVariant();
        }

        // ---- 多维访问（用户 #DIM 的 2D/3D 数组）----
        // 对齐 C# Int2DVariableToken/Int3DVariableToken：直接用两个（三个）下标。
        // 维度由变量声明决定；系统变量与 1D 变量只取第一个下标。
        const int dim = variableDimension(varName);
        if (dim >= 2 && indices.size() >= 2 && !storage->hasSystemVariable(varName)) {
            const QList<int> ids = resolveIndices(node, storage, gameBaseData);
            if (dim >= 3 && ids.size() >= 3) {
                return QVariant::fromValue<qint64>(
                    storage->getGlobalInt3D(varName, ids.at(0), ids.at(1), ids.at(2)));
            }
            if (ids.size() >= 2) {
                return QVariant::fromValue<qint64>(
                    storage->getGlobalInt2D(varName, ids.at(0), ids.at(1)));
            }
        }

        // 1D 访问：下标可以是整数或字符串（CSV 常量名 / 格式化串）
        const int index = static_cast<int>(resolveIndex(node, 0, storage, gameBaseData));

        // Try to get system variable first
        // 判定必须与写入路径（ExecutionEngine::writeLhs）一致，且大小写不敏感：
        // 之前用「硬编码名单 + isVariableGlobal」且漏了 TIME，导致 TIME:0/2/5 写入
        // 落到用户全局槽、读取却走系统通道恒为 0 —— 表现为「变量似乎不可变」。
        if (storage->hasSystemVariable(varName)) {
            return QVariant(storage->getSystemVariable(varName, index));
        }

        // Fall back to global variable
        return QVariant(storage->getGlobalInt1D(varName, index));
    }
    
    // Simple variable access (no array index)
    // 系统变量（RESULT / DAY / MONEY / FLAG / A / B / C / …）-> 取下标 0 的值
    // 只有**真正有存储槽**的系统变量才走系统变量通道（A–Z/DA–DE 等无槽 → 用户全局）
    if (storage->hasSystemVariable(varName)) {
        return QVariant(storage->getSystemVariable(varName, 0));
    }

    // Regular global variable
    return QVariant(storage->getGlobalInt1D(varName, 0));
}

QVariant ExpressionEvaluator::evaluateIndexedVariable(const QString &varName, int index, VariableStorage *storage)
{
    if (!storage) {
        return QVariant(0);
    }
    
    // Try to get the variable value
    // Check if it's a system variable first
    if (varName == "DAY" || varName == "MONEY" || varName == "FLAG" ||
        varName == "ITEM" || varName == "COUNT" || varName == "A" ||
        varName == "B" || varName == "C") {
        return QVariant(storage->getSystemVariable(varName, index));
    }
    
    // Fall back to global variable
    return QVariant(storage->getGlobalInt1D(varName, index));
}

QVariant ExpressionEvaluator::evaluateBinaryOp(const BinaryOpNode &node, VariableStorage *storage, GameBaseData *gameBaseData)
{
    QVariant left = evaluateNode(*node.left(), storage, gameBaseData);
    if (!left.isValid()) return QVariant();
    // Match OperatorMethod.cs: logical operators short-circuit before evaluating
    // the RHS, including its function calls and increment/decrement side effects.
    const TokenType logicalOp = node.op().type();
    if (node.left()->valueType() == OperandType::Int) {
        const bool truth = left.toLongLong() != 0;
        if ((!truth && logicalOp == TokenType::AND)
            || (truth && logicalOp == TokenType::LOGICAL_NOR))
            return QVariant::fromValue<qint64>(0);
        if ((truth && logicalOp == TokenType::OR)
            || (!truth && logicalOp == TokenType::LOGICAL_NAND))
            return QVariant::fromValue<qint64>(1);
    }
    QVariant right = evaluateNode(*node.right(), storage, gameBaseData);
    if (!left.isValid() || !right.isValid()) [[unlikely]] {
        return QVariant();
    }

    const TokenType op = node.op().type();

    // 强类型 AST：优先使用解析期已确定的类型；未知时才做运行期探测。
    OperandType lt = node.left()->valueType();
    OperandType rt = node.right()->valueType();
    if (!isKnown(lt)) [[unlikely]] { lt = isRuntimeString(left) ? OperandType::Str : OperandType::Int; }
    if (!isKnown(rt)) [[unlikely]] { rt = isRuntimeString(right) ? OperandType::Str : OperandType::Int; }

    // 分支预测：整数运算（绝对主流）走快路径
    if (lt == OperandType::Int && rt == OperandType::Int) [[likely]] {
        return evaluateIntBinary(op, left.toLongLong(), right.toLongLong());
    }

    // 类型校验（运算符表）——慢路径
    if (!inferBinaryType(op, lt, rt)) [[unlikely]] {
        emit evaluationError(QString::fromLatin1(operandTypeName(lt)),
                             QStringLiteral("operator %1 不支持 %2,%3")
                                 .arg(node.op().value(),
                                      QString::fromLatin1(operandTypeName(lt)),
                                      QString::fromLatin1(operandTypeName(rt))));
        return QVariant();
    }
    return evaluateStrBinary(op, left.toString(), right.toString(), left, right);
}

QVariant ExpressionEvaluator::evaluateUnaryOp(const UnaryOpNode &node, VariableStorage *storage, GameBaseData *gameBaseData)
{
    QVariant operand = evaluateNode(*node.operand().get(), storage, gameBaseData);
    if (!operand.isValid()) {
        return QVariant();
    }
    const qint64 v = operand.toLongLong();
    switch (node.op().type()) {
    case TokenType::NOT:       return QVariant::fromValue<qint64>(v == 0 ? 1 : 0);
    case TokenType::MINUS:     return QVariant::fromValue<qint64>(-v);
    case TokenType::PLUS:      return QVariant::fromValue<qint64>(v);
    case TokenType::BIT_NOT:   return QVariant::fromValue<qint64>(~v);
    case TokenType::INCREMENT:
    case TokenType::DECREMENT: {
        const qint64 delta = (node.op().type() == TokenType::INCREMENT) ? 1 : -1;
        const QSharedPointer<ExpressionNode> target = node.operand();
        // C# OperatorMethod：++x -> PlusValue(1)（返回新值）；x++ -> PlusValue(1)-1（返回旧值）
        // 两者**都会写回变量**（CanRestructure = false）。
        if (target && target->kind() == NodeKind::Variable) {
            const VariableNode& var = static_cast<const VariableNode&>(*target);
            assignVariable(var, storage, gameBaseData, v + delta);
            return QVariant::fromValue<qint64>(node.isPostfix() ? v : v + delta);
        }
        return QVariant::fromValue<qint64>(v + delta);
    }
    default: break;
    }
    return QVariant();
}

// ===========================================================================
// 内置函数（内部命令）求值
//
//   C# 对应 `GameData/Function/Creator.Method.cs` 的各个 FunctionMethod.Get*/()
//   实现（语义逐条对齐；错误条件放宽为返回 0/默认值，不抛异常）。
// ===========================================================================
namespace {

// 半角 <-> 全角（对齐 C# Strings.StrConv(VbStrConv.Narrow/Wide) 的 ASCII 部分；
// 假名半角/全角映射需要大表，此处保持原样）
QString toHalfWidth(const QString& s) {
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        const ushort u = c.unicode();
        if (u >= 0xFF01 && u <= 0xFF5E) out.append(QChar(u - 0xFEE0));
        else if (u == 0x3000) out.append(QLatin1Char(' '));
        else out.append(c);
    }
    return out;
}

QString toFullWidth(const QString& s) {
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        const ushort u = c.unicode();
        if (u >= 0x21 && u <= 0x7E) out.append(QChar(u + 0xFEE0));
        else if (u == 0x20) out.append(QChar(0x3000));
        else out.append(c);
    }
    return out;
}

} // namespace

qint64 ExpressionEvaluator::parseIntLikeEmuera(const QString &str) const {
    if (str.isEmpty()) return 0;
    // 含全角/多字节字符 → 0（C#：str.Length < GetStrlenLang(str)）
    if (langByteCount(str) > str.size()) return 0;
    const QChar c0 = str.at(0);
    if (!c0.isDigit() && c0 != QLatin1Char('+') && c0 != QLatin1Char('-')) return 0;
    if ((c0 == QLatin1Char('+') || c0 == QLatin1Char('-'))
        && (str.size() < 2 || !str.at(1).isDigit())) return 0;

    qint64 v = 0;
    if (parseIntegerLiteral(str, v)) return v;
    const int dot = str.indexOf(QLatin1Char('.'));
    if (dot > 0 && parseIntegerLiteral(str.left(dot), v)) return v;
    return 0;
}

bool ExpressionEvaluator::isNumericLikeEmuera(const QString &str) const {
    if (str.isEmpty()) return false;
    if (langByteCount(str) > str.size()) return false;
    const QChar c0 = str.at(0);
    if (!c0.isDigit() && c0 != QLatin1Char('+') && c0 != QLatin1Char('-')) return false;
    if ((c0 == QLatin1Char('+') || c0 == QLatin1Char('-'))
        && (str.size() < 2 || !str.at(1).isDigit())) return false;

    qint64 v = 0;
    if (parseIntegerLiteral(str, v)) return true;
    const int dot = str.indexOf(QLatin1Char('.'));
    if (dot <= 0) return false;
    if (!parseIntegerLiteral(str.left(dot), v)) return false;
    for (int i = dot + 1; i < str.size(); ++i) {
        if (!str.at(i).isDigit()) return false;
    }
    return true;
}

void ExpressionEvaluator::setMoneyLabel(const QString &label, bool moneyFirst) {
    m_moneyLabel = label;
    m_moneyFirst = moneyFirst;
}

void ExpressionEvaluator::setBarChars(QChar filled, QChar empty) {
    m_barFilled = filled;
    m_barEmpty = empty;
}

// ---- 实参取值 ----
bool ExpressionEvaluator::hasArg(const FunctionNode &node, int i) {
    return i >= 0 && i < node.arguments().size() && node.arguments().at(i);
}

qint64 ExpressionEvaluator::argInt(const FunctionNode &node, int i, VariableStorage *storage, GameBaseData *g) {
    if (!hasArg(node, i)) return 0;
    return evaluateNode(*node.arguments().at(i), storage, g).toLongLong();
}

QString ExpressionEvaluator::argStr(const FunctionNode &node, int i, VariableStorage *storage, GameBaseData *g) {
    if (!hasArg(node, i)) return QString();
    return evaluateNode(*node.arguments().at(i), storage, g).toString();
}

const VariableNode* ExpressionEvaluator::argVar(const FunctionNode &node, int i) const {
    if (!hasArg(node, i)) return nullptr;
    const ExpressionNode* a = node.arguments().at(i).get();
    return a->kind() == NodeKind::Variable ? static_cast<const VariableNode*>(a) : nullptr;
}

// ---- 数组读取 ----
QList<qint64> ExpressionEvaluator::readIntArray(const VariableNode &var, VariableStorage *storage,
                                                GameBaseData *gameBaseData, bool charaRange) const {
    QList<qint64> out;
    if (!storage) return out;
    const QString name = var.name();
    const QString upper = name.toUpper();

    if (charaRange) {
        // 角色数组：SUMCARRAY(CFLAG:列) 的列号即变量第一个下标，沿角色维求和
        const int column = var.indices().isEmpty()
            ? 0
            : static_cast<int>(const_cast<ExpressionEvaluator*>(this)->resolveIndex(var, 0, storage, gameBaseData));
        const int charaNum = const_cast<ExpressionEvaluator*>(this)->charaCount(storage);
        out.reserve(charaNum);
        for (int i = 0; i < charaNum; ++i) out.append(storage->getCharaInt(name, i, column));
        return out;
    }

    int size = storage->variableConfig().getSize1D(name);
    if (size <= 0) size = storage->variableConfig().getSize1D(upper);
    if (size <= 0) {
        const QPair<int, int> p2 = storage->variableConfig().getSize2D(name);
        size = p2.second;
    }
    if (size <= 0) {
        const QPair<int, int> p2 = storage->variableConfig().getSize2D(upper);
        size = p2.second;
    }
    out.reserve(size);
    for (int i = 0; i < size; ++i) {
        out.append(storage->hasSystemVariable(upper) ? storage->getSystemVariable(upper, i)
                                                     : storage->getGlobalInt1D(name, i));
    }
    return out;
}

// 字符串一维数组内容。对齐 readIntArray，但走字符串容器：
//   * CSV 常数名数组（ABLNAME/BASENAME/TALENTNAME/…）存在全局字符串槽
//   * 用户 #DIMS 全局串、SAVESTR/STR 等系统串
// (BASENAME, ABLNAME …) 以前被 readIntArray 当整数读 -> 全 0，
// 于是 `FINDELEMENT(BASENAME,"気力")` 恒为 0：eraTW 的 BASE_BAR 里
// 体力/気力 都用 BASE_ID=0 的同一个槽，两根条才会「一起变」。
QList<QString> ExpressionEvaluator::readStrArray(const VariableNode &var, VariableStorage *storage) const {
    QList<QString> out;
    if (!storage) return out;
    const QString name = var.name();
    const QString upper = name.toUpper();

    // 角色字符串数组（CSTR 之类）：沿角色维取同一列
    if (storage->isCharaDataVariable(name)) {
        if (!storage->isCharaDataString(name)) return out;
        const int charaNum = charaCount(storage);
        const int column = var.indices().isEmpty()
            ? 0
            : static_cast<int>(const_cast<ExpressionEvaluator*>(this)->resolveIndex(var, 0, storage, nullptr));
        out.reserve(charaNum);
        for (int i = 0; i < charaNum; ++i) out.append(storage->getCharaStr(name, i, column));
        return out;
    }

    int size = storage->variableConfig().getSize1D(name);
    if (size <= 0) size = storage->variableConfig().getSize1D(upper);
    if (size <= 0) size = storage->arraySize(name);
    if (size <= 0) size = storage->variableConfig().getSize2D(name).second;
    out.reserve(size);
    for (int i = 0; i < size; ++i) out.append(storage->getGlobalStr1D(name, i));
    return out;
}

int ExpressionEvaluator::charaCount(VariableStorage *storage) const {
    if (m_charaNumProvider) return m_charaNumProvider();
    if (!storage) return 0;
    return static_cast<int>(storage->getSystemVariable(QStringLiteral("CHARANUM"), 0));
}

// ---- 语言相关字节长度（对齐 C# LangManager）----
int ExpressionEvaluator::langByteCountOfChar(QChar c) const {
    if (c.unicode() < 0x80) return 1;   // ASCII 在所有目标编码下都是 1 字节
    if (m_langEncoding == TextEncoding::Utf8 || m_langEncoding == TextEncoding::Auto) {
        return QString(c).toUtf8().size();
    }
    static QHash<quint32, int> cache;
    const quint32 key = static_cast<quint32>(c.unicode());
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();

    bool ok = true;
    const QByteArray bytes = TextCodecUtil::encode(QString(c), m_langEncoding, &ok);
    const int width = (ok && !bytes.isEmpty()) ? bytes.size() : 1;
    cache.insert(key, width);
    return width;
}

int ExpressionEvaluator::langByteCount(const QString &s) const {
    if (s.isEmpty()) return 0;
    int total = 0;
    for (int i = 0; i < s.size();) {
        // 代理对：一次处理两个 UTF-16 码元，避免孤立代理字符
        if (s.at(i).isHighSurrogate() && i + 1 < s.size() && s.at(i + 1).isLowSurrogate()) {
            const QString pair = s.mid(i, 2);
            bool ok = true;
            const QByteArray bytes = TextCodecUtil::encode(pair, m_langEncoding, &ok);
            total += (ok && !bytes.isEmpty()) ? bytes.size() : 1;
            i += 2;
        } else {
            total += langByteCountOfChar(s.at(i));
            ++i;
        }
    }
    return total;
}

// 对齐 C# LangManager.GetSubStringLang：start/length 都是「语言编码的字节位置」
QString ExpressionEvaluator::langSubstring(const QString &s, int startIndex, int length) const {
    const int totalByte = langByteCount(s);
    if (startIndex >= totalByte || length == 0 || s.isEmpty()) return QString();
    if (length < 0 || length > totalByte) length = totalByte;

    int utf = 0;
    int bytes = 0;
    if (startIndex <= 0) {
        if (length == totalByte) return s;
    } else {
        while (utf < s.size()) {
            const int width = langByteCountOfChar(s.at(utf));
            bytes += width;
            ++utf;
            if (bytes >= startIndex) break;
        }
        if (utf >= s.size()) return QString();
    }

    QString ret;
    bytes = 0;
    while (true) {
        if (utf >= s.size()) break;
        ret.append(s.at(utf));
        bytes += langByteCountOfChar(s.at(utf));
        ++utf;
        if (bytes >= length) break;
    }
    return ret;
}

// 对齐 C# LangManager.GetUFTIndex + string.IndexOf：返回「语言字节位置」
int ExpressionEvaluator::langIndexOf(const QString &target, const QString &word, int langStart) const {
    int utfStart;
    if (langStart <= 0) {
        utfStart = 0;
    } else {
        const int total = langByteCount(target);
        if (langStart >= total) return -1;
        utfStart = 0;
        int bytes = 0;
        while (utfStart < target.size()) {
            bytes += langByteCountOfChar(target.at(utfStart));
            ++utfStart;
            if (bytes >= langStart) break;
        }
    }
    if (utfStart < 0 || utfStart >= target.size()) return -1;
    const int index = target.indexOf(word, utfStart);
    if (index < 0) return -1;
    return langByteCount(target.left(index));   // 换回语言字节位置
}

// ---------------------------------------------------------------------------
// 主入口：内置函数 / 用户函数分派
// ---------------------------------------------------------------------------
QVariant ExpressionEvaluator::evaluateFunction(const FunctionNode &node, VariableStorage *storage, GameBaseData *gameBaseData)
{
    // 1) 内置函数（内部命令）
    if (node.isBuiltin()) {
        QVariant out;
        if (evaluateBuiltin(node, storage, gameBaseData, out)) return out;
    }

    // 2) 用户自定义函数：交给执行链回调（ScriptRunner::invokeUserFunction）
    if (m_userInvoker) {
        const QString funcName = node.name().toUpper();
        QList<QVariant> args;
        args.reserve(node.arguments().size());
        for (const auto& a : node.arguments()) {
            args.append(evaluateNode(*a.get(), storage, gameBaseData));
        }
        QVariant out;
        if (m_userInvoker(funcName, args, out)) {
            return out;
        }
    }
    return QVariant(0);
}

bool ExpressionEvaluator::evaluateBuiltin(const FunctionNode &node, VariableStorage *storage,
                                          GameBaseData *gameBaseData, QVariant &out)
{
    const BuiltinOp op = node.builtinOp();
    const auto I = [&](int i) { return argInt(node, i, storage, gameBaseData); };
    const auto S = [&](int i) { return argStr(node, i, storage, gameBaseData); };
    const auto V = [&]() -> const BuiltinFunctionSpec* { return node.builtinSpec(); };

    switch (op) {
    // ---------------- 数学 ----------------
    case BuiltinOp::Abs:      out = QVariant::fromValue<qint64>(qAbs(I(0))); return true;
    case BuiltinOp::Sign: {
        const qint64 v = I(0);
        out = QVariant::fromValue<qint64>(v > 0 ? 1 : (v < 0 ? -1 : 0));
        return true;
    }
    case BuiltinOp::Rand: {
        // 对齐 C# RandMethod：RAND(max) -> [0,max)，RAND(min,max) -> [min,max)
        // 内部走 VEvaluator.GetNextRand(max - min) == NextUInt64() % (max - min)
        qint64 minV = 0, maxV = 0;
        if (node.arguments().size() == 1) { maxV = I(0); }
        else { minV = I(0); maxV = I(1); }
        if (maxV <= minV) { out = QVariant::fromValue<qint64>(minV); return true; }
        out = QVariant::fromValue<qint64>(minV + m_rand.nextInt(maxV - minV));
        return true;
    }
    case BuiltinOp::Min:
    case BuiltinOp::Max: {
        if (node.arguments().isEmpty()) { out = QVariant::fromValue<qint64>(0); return true; }
        qint64 r = I(0);
        const bool isMax = (op == BuiltinOp::Max);
        for (int i = 1; i < node.arguments().size(); ++i) {
            const qint64 v = I(i);
            if (isMax ? (r < v) : (r > v)) r = v;
        }
        out = QVariant::fromValue<qint64>(r);
        return true;
    }
    case BuiltinOp::Limit: {
        const qint64 v = I(0), lo = I(1), hi = I(2);
        qint64 r = v;
        if (v < lo) r = lo; else if (v > hi) r = hi;
        out = QVariant::fromValue<qint64>(r);
        return true;
    }
    case BuiltinOp::Power: {
        const double r = std::pow(double(I(0)), double(I(1)));
        out = QVariant::fromValue<qint64>(std::isfinite(r) ? static_cast<qint64>(r) : 0);
        return true;
    }
    case BuiltinOp::Sqrt:  { const qint64 v = I(0); out = QVariant::fromValue<qint64>(v < 0 ? 0 : static_cast<qint64>(std::sqrt(double(v)))); return true; }
    case BuiltinOp::Cbrt:  { out = QVariant::fromValue<qint64>(static_cast<qint64>(std::cbrt(double(I(0))))); return true; }
    case BuiltinOp::Log:   { const qint64 v = I(0); out = QVariant::fromValue<qint64>(v <= 0 ? 0 : static_cast<qint64>(std::log(double(v)))); return true; }
    case BuiltinOp::Log10: { const qint64 v = I(0); out = QVariant::fromValue<qint64>(v <= 0 ? 0 : static_cast<qint64>(std::log10(double(v)))); return true; }
    case BuiltinOp::Exponent: { const double r = std::exp(double(I(0))); out = QVariant::fromValue<qint64>(std::isfinite(r) ? static_cast<qint64>(r) : 0); return true; }

    // ---------------- 位 / 范围 ----------------
    case BuiltinOp::GetBit: {
        const qint64 n = I(0), m = I(1);
        out = QVariant::fromValue<qint64>((m < 0 || m > 63) ? 0 : ((n >> static_cast<int>(m)) & 1));
        return true;
    }
    case BuiltinOp::InRange: {
        const qint64 v = I(0), lo = I(1), hi = I(2);
        out = QVariant::fromValue<qint64>((v >= lo && v <= hi) ? 1 : 0);
        return true;
    }

    // ---------------- 字符串 ----------------
    case BuiltinOp::StrLen:  out = QVariant::fromValue<qint64>(langByteCount(S(0))); return true;
    case BuiltinOp::StrLenU: out = QVariant::fromValue<qint64>(S(0).size()); return true;
    case BuiltinOp::Substring: {
        const QString s = S(0);
        const int start = node.arguments().size() >= 2 ? static_cast<int>(I(1)) : 0;
        const int length = node.arguments().size() >= 3 ? static_cast<int>(I(2)) : -1;
        out = QVariant(langSubstring(s, start, length));
        return true;
    }
    case BuiltinOp::SubstringU: {
        const QString s = S(0);
        int start = node.arguments().size() >= 2 ? static_cast<int>(I(1)) : 0;
        int length = node.arguments().size() >= 3 ? static_cast<int>(I(2)) : -1;
        if (start >= s.size() || length == 0) { out = QVariant(QString()); return true; }
        if (length < 0 || length > s.size()) length = s.size();
        if (start <= 0) { if (length == s.size()) { out = QVariant(s); return true; } start = 0; }
        out = QVariant(s.mid(start, length));
        return true;
    }
    case BuiltinOp::StrFind:
    case BuiltinOp::StrFindU: {
        const QString target = S(0), word = S(1);
        const int start = node.arguments().size() >= 3 ? static_cast<int>(I(2)) : 0;
        if (op == BuiltinOp::StrFindU) {
            if (start < 0 || start >= target.size()) { out = QVariant::fromValue<qint64>(-1); return true; }
            out = QVariant::fromValue<qint64>(target.indexOf(word, start));
            return true;
        }
        out = QVariant::fromValue<qint64>(langIndexOf(target, word, start));
        return true;
    }
    case BuiltinOp::StrCount: {
        const QString target = S(0);
        const QRegularExpression re(S(1));
        int count = 0;
        if (re.isValid()) {
            auto it = re.globalMatch(target);
            while (it.hasNext()) { it.next(); ++count; }
        }
        out = QVariant::fromValue<qint64>(count);
        return true;
    }
    case BuiltinOp::ToStr:
        // 第 2 参数是 .NET 数字格式串（`i.ToString(format)`）
        out = QVariant(node.arguments().size() >= 2
                           ? formatDotNetNumber(I(0), S(1))
                           : QString::number(I(0)));
        return true;
    case BuiltinOp::ToInt:   out = QVariant::fromValue<qint64>(parseIntLikeEmuera(S(0))); return true;
    case BuiltinOp::IsNumeric:{ out = QVariant::fromValue<qint64>(isNumericLikeEmuera(S(0)) ? 1 : 0); return true; }
    case BuiltinOp::ToUpper: out = QVariant(S(0).toUpper()); return true;
    case BuiltinOp::ToLower: out = QVariant(S(0).toLower()); return true;
    case BuiltinOp::ToHalf:  out = QVariant(toHalfWidth(S(0))); return true;
    case BuiltinOp::ToFull:  out = QVariant(toFullWidth(S(0))); return true;
    case BuiltinOp::Replace: {
        // REPLACE(<文字列>, <正規表現>, <置換文字列>) —— 对齐 C# ReplaceMethod：
        // 第 2 引数是**正则表达式**（`new Regex(pattern)` + `reg.Replace(base, repl)`）。
        // 以前按「字面串替换」实现，eraTW 的
        //   REPLACE LOCALS, "(^ +| +$)", ""      ; 去首尾空格
        //   ARGS '= REPLACE(ARGS, "/+", "/")     ; 斜杠压缩
        // 全部无效（看起来像没执行）。
        // 注意：ERB 字符串先做转义（`\s`=空格、`\+`=`+`…），到这里已经是
        // 干净的 .NET 风格正则；QRegularExpression 与其语法基本一致。
        QString s = S(0);
        const QString pattern = S(1);
        const QString to = S(2);
        if (pattern.isEmpty()) {
            out = QVariant(s);   // 空模式：保持原样（C# 会抛，这里容错）
            return true;
        }
        const QRegularExpression re(pattern);
        if (!re.isValid()) {
            // 对齐 C# 的 CodeEE（"第２引数が正規表現として不正です"）：留痕并保持原样
            qWarning() << "[exec] REPLACE 的第 2 引数不是合法正则：" << pattern
                       << re.errorString();
            out = QVariant(s);
            return true;
        }
        // 替换串的占位符：.NET 用 $1、Qt 用 \1 —— eraTW 只用空串，这里顺带
        // 把 `$N` 转成 `\N` 以兼容 .NET 写法。
        QString replacement = to;
        {
            static const QRegularExpression groupRef(QStringLiteral("\\$(\\d+)"));
            replacement.replace(groupRef, QStringLiteral("\\\\\\1"));
        }
        s.replace(re, replacement);
        out = QVariant(s);
        return true;
    }
    case BuiltinOp::Unicode: {
        const qint64 i = I(0);
        out = QVariant((i < 0 || i > 0xFFFF) ? QString() : QString(QChar(static_cast<ushort>(i))));
        return true;
    }
    case BuiltinOp::UnicodeByte: {
        // C#: 取 UTF-32 编码的第 1 个字节组的 Int32（小端）
        const QString s = S(0);
        if (s.isEmpty()) { out = QVariant::fromValue<qint64>(0); return true; }
        const uint cp = s.toUcs4().value(0, 0);
        out = QVariant::fromValue<qint64>(static_cast<qint32>(cp));
        return true;
    }
    case BuiltinOp::Convert: {
        const qint64 v = I(0);
        const int base = static_cast<int>(I(1));
        if (base != 2 && base != 8 && base != 10 && base != 16) { out = QVariant(QString()); return true; }
        out = QVariant(QString::number(v, base));
        return true;
    }
    case BuiltinOp::Escape: out = QVariant(QRegularExpression::escape(S(0))); return true;
    case BuiltinOp::EncodeToUni: {
        const QString s = S(0);
        if (s.isEmpty()) { out = QVariant::fromValue<qint64>(-1); return true; }
        const int pos = node.arguments().size() > 1 ? static_cast<int>(I(1)) : 0;
        if (pos < 0 || pos >= s.size()) { out = QVariant::fromValue<qint64>(-1); return true; }
        out = QVariant::fromValue<qint64>(static_cast<qint64>(s.toUcs4().value(pos, 0)));
        return true;
    }
    case BuiltinOp::CharAtU: {
        const QString s = S(0);
        const int pos = static_cast<int>(I(1));
        out = QVariant((pos < 0 || pos >= s.size()) ? QString() : QString(s.at(pos)));
        return true;
    }
    case BuiltinOp::StrForm: {
        // STRFORM("...") —— 运行期展开格式串（{expr} / %expr%）
        // 对齐 C# StrFormMethod：把字符串当格式化串重新解析后求值。
        const QString text = S(0);
        ExpressionLexer lexer;
        ExpressionParser parser;
        const StrFormParser::ExprResolver resolve = [&parser, &lexer](const QString& e) {
            return parser.parse(lexer.tokenize(e, 1));
        };
        const QSharedPointer<ExpressionNode> form = StrFormParser::parse(text, resolve);
        if (!form) { out = QVariant(text); return true; }
        out = evaluateNode(*form, storage, gameBaseData);
        return true;
    }
    case BuiltinOp::LineIsEmpty:
        // 需要控制台状态（C# GlobalStatic.Console.EmptyLine），显示层未接线 → 视为非空
        out = QVariant::fromValue<qint64>(0);
        return true;
    case BuiltinOp::StrJoin: {
        const VariableNode* var = argVar(node, 0);
        if (!var) { out = QVariant(QString()); return true; }
        const QString delim = node.arguments().size() >= 2 ? S(1) : QStringLiteral(",");
        const QList<qint64> values = readIntArray(*var, storage, gameBaseData, false);
        const int start = node.arguments().size() >= 3 ? static_cast<int>(I(2)) : 0;
        const int end = node.arguments().size() >= 4 ? static_cast<int>(I(3)) : values.size();
        QStringList parts;
        for (int i = qMax(0, start); i < qMin(end, values.size()); ++i) {
            parts << QString::number(values.at(i));
        }
        out = QVariant(parts.join(delim));
        return true;
    }

    // ---------------- 时间 / 常量 ----------------
    case BuiltinOp::GetTime:  out = QVariant::fromValue<qint64>(QDateTime::currentSecsSinceEpoch()); return true;
    case BuiltinOp::GetTimes: out = QVariant(QDateTime::currentDateTime().toString(QStringLiteral("yyyy/MM/dd HH:mm:ss"))); return true;
    case BuiltinOp::GetMillisecond: out = QVariant::fromValue<qint64>(QDateTime::currentMSecsSinceEpoch()); return true;
    case BuiltinOp::GetSecond: out = QVariant::fromValue<qint64>(QTime::currentTime().msecsSinceStartOfDay() / 1000); return true;
    case BuiltinOp::MoneyStr: {
        const qint64 money = I(0);
        const QString amount = QString::number(money);
        out = QVariant(m_moneyFirst ? m_moneyLabel + amount : amount + m_moneyLabel);
        return true;
    }
    case BuiltinOp::BarStr: {
        const qint64 var = I(0), maxV = I(1), length = I(2);
        if (maxV <= 0 || length <= 0 || length >= 100) { out = QVariant(QString()); return true; }
        qint64 count = var * length / maxV;
        if (count < 0) count = 0;
        if (count > length) count = length;
        QString bar = QStringLiteral("[");
        bar += QString(int(count), m_barFilled);
        bar += QString(int(length - count), m_barEmpty);
        bar += QLatin1Char(']');
        out = QVariant(bar);
        return true;
    }
    case BuiltinOp::PrintCPerLine:
    case BuiltinOp::PrintCLength:
        out = QVariant::fromValue<qint64>(0);   // 显示层参数未接线
        return true;

    // ---------------- 数组 ----------------
    case BuiltinOp::SumArray:
    case BuiltinOp::SumCharaArray: {
        const VariableNode* var = argVar(node, 0);
        if (!var) { out = QVariant::fromValue<qint64>(0); return true; }
        const bool chara = (op == BuiltinOp::SumCharaArray);
        const QList<qint64> values = readIntArray(*var, storage, gameBaseData, chara);
        const int start = node.arguments().size() >= 2 ? static_cast<int>(I(1)) : 0;
        const int end = node.arguments().size() >= 3 ? static_cast<int>(I(2)) : values.size();
        qint64 sum = 0;
        for (int i = qMax(0, start); i < qMin(end, values.size()); ++i) sum += values.at(i);
        out = QVariant::fromValue<qint64>(sum);
        return true;
    }
    case BuiltinOp::MaxArray:
    case BuiltinOp::MinArray:
    case BuiltinOp::MaxCharaArray:
    case BuiltinOp::MinCharaArray: {
        const VariableNode* var = argVar(node, 0);
        if (!var) { out = QVariant::fromValue<qint64>(0); return true; }
        const bool chara = (op == BuiltinOp::MaxCharaArray || op == BuiltinOp::MinCharaArray);
        const bool wantMax = (op == BuiltinOp::MaxArray || op == BuiltinOp::MaxCharaArray);
        const QList<qint64> values = readIntArray(*var, storage, gameBaseData, chara);
        const int start = node.arguments().size() >= 2 ? static_cast<int>(I(1)) : 0;
        const int end = node.arguments().size() >= 3 ? static_cast<int>(I(2)) : values.size();
        if (values.isEmpty()) { out = QVariant::fromValue<qint64>(0); return true; }
        qint64 r = values.at(qBound(0, start, values.size() - 1));
        for (int i = qMax(0, start); i < qMin(end, values.size()); ++i) {
            const qint64 v = values.at(i);
            if (wantMax ? (v > r) : (v < r)) r = v;
        }
        out = QVariant::fromValue<qint64>(r);
        return true;
    }
    case BuiltinOp::InRangeArray:
    case BuiltinOp::InRangeCharaArray: {
        const VariableNode* var = argVar(node, 0);
        if (!var) { out = QVariant::fromValue<qint64>(0); return true; }
        const bool chara = (op == BuiltinOp::InRangeCharaArray);
        const qint64 lo = I(1), hi = I(2);
        const QList<qint64> values = readIntArray(*var, storage, gameBaseData, chara);
        const int start = node.arguments().size() >= 4 ? static_cast<int>(I(3)) : 0;
        const int end = node.arguments().size() >= 5 ? static_cast<int>(I(4)) : values.size();
        qint64 count = 0;
        for (int i = qMax(0, start); i < qMin(end, values.size()); ++i) {
            const qint64 v = values.at(i);
            if (v >= lo && v < hi) ++count;
        }
        out = QVariant::fromValue<qint64>(count);
        return true;
    }
    case BuiltinOp::Match:
    case BuiltinOp::CharaMatch: {
        const VariableNode* var = argVar(node, 0);
        if (!var) { out = QVariant::fromValue<qint64>(0); return true; }
        const bool chara = (op == BuiltinOp::CharaMatch);
        const bool argIsStr = hasArg(node, 1) && node.arguments().at(1)->valueType() == OperandType::Str;
        const QList<qint64> values = readIntArray(*var, storage, gameBaseData, chara);
        const int start = node.arguments().size() > 2 ? static_cast<int>(I(2)) : 0;
        const int end = node.arguments().size() > 3 ? static_cast<int>(I(3)) : values.size();
        qint64 count = 0;
        if (argIsStr) {
            const QString target = S(1);
            for (int i = qMax(0, start); i < qMin(end, values.size()); ++i) {
                const QString v = QString::number(values.at(i));
                if (v == target || (target.isEmpty() && v.isEmpty())) ++count;
            }
        } else {
            const qint64 target = I(1);
            for (int i = qMax(0, start); i < qMin(end, values.size()); ++i) {
                if (values.at(i) == target) ++count;
            }
        }
        out = QVariant::fromValue<qint64>(count);
        return true;
    }
    case BuiltinOp::GroupMatch:
    case BuiltinOp::Nosames:
    case BuiltinOp::Allsames: {
        const int n = node.arguments().size();
        const bool asStr = hasArg(node, 0) && node.arguments().at(0)->valueType() == OperandType::Str;
        int same = 0;
        if (asStr) {
            const QString base = S(0);
            for (int i = 1; i < n; ++i) if (S(i) == base) ++same;
        } else {
            const qint64 base = I(0);
            for (int i = 1; i < n; ++i) if (I(i) == base) ++same;
        }
        qint64 r = 0;
        if (op == BuiltinOp::GroupMatch) r = same;
        else if (op == BuiltinOp::Nosames) r = (same == 0) ? 1 : 0;
        else r = (same == n - 1) ? 1 : 0;
        out = QVariant::fromValue<qint64>(r);
        return true;
    }
    case BuiltinOp::FindElement:
    case BuiltinOp::FindLastElement: {
        const VariableNode* var = argVar(node, 0);
        if (!var) { out = QVariant::fromValue<qint64>(-1); return true; }
        // 字符串数组（BASENAME/ABLNAME/…）：按字符串比较（对齐 C# FINDELEMENT 的字符串分支）。
        // 否则会被 readIntArray 当整数读成全 0，恒返回 0。
        const bool strArray = (var->valueType() == OperandType::Str)
                              || (storage && storage->isCharaDataString(var->name()));
        if (strArray) {
            const QList<QString> values = readStrArray(*var, storage);
            // 第 3/4 实参可为空（`FINDELEMENT(A, X, 1, , 1)`）：省略时分别取
            // 「0」与「数组末尾」（对齐 C# arguments[i] == null 的默认值）
            const int start = (node.arguments().size() > 2 && !node.isArgOmitted(2))
                                  ? static_cast<int>(I(2)) : 0;
            const int end = (node.arguments().size() > 3 && !node.isArgOmitted(3))
                                ? static_cast<int>(I(3)) : values.size();
            const QString target = argStr(node, 1, storage, gameBaseData);
            // 第 5 实参 isExact：正则必须**整串匹配**（对齐 C# FindElementMethod）
            const bool isExact = (node.arguments().size() > 4 && !node.isArgOmitted(4))
                                     ? (I(4) != 0) : false;
            // 第 2 实参是**正则**（eraTW 惯用 `FINDELEMENT(CLASS_NAME, ESCAPE(name), 1, , 1)`）。
            // 正则非法时退化为字面比较（C# 会抛 CodeEE；这里容错并留痕）。
            const QRegularExpression re(target);
            const bool useRegex = re.isValid();
            if (!useRegex) qWarning() << "[exec] FINDELEMENT 的模式不是合法正则，按字面比较：" << target;
            const auto matches = [&](const QString& s) -> bool {
                if (!useRegex) return s == target;
                const QRegularExpressionMatch m = re.match(s);
                if (!m.hasMatch()) return false;
                return !isExact || m.capturedLength() == s.size();
            };
            qint64 found = -1;
            if (op == BuiltinOp::FindElement) {
                for (int i = qMax(0, start); i < qMin(end, values.size()); ++i) {
                    if (matches(values.at(i))) { found = i; break; }
                }
            } else {
                for (int i = qMin(end, values.size()) - 1; i >= qMax(0, start); --i) {
                    if (matches(values.at(i))) { found = i; break; }
                }
            }
            out = QVariant::fromValue<qint64>(found);
            return true;
        }
        const QList<qint64> values = readIntArray(*var, storage, gameBaseData, false);
        const int start = (node.arguments().size() > 2 && !node.isArgOmitted(2))
                              ? static_cast<int>(I(2)) : 0;
        const int end = (node.arguments().size() > 3 && !node.isArgOmitted(3))
                            ? static_cast<int>(I(3)) : values.size();
        const qint64 target = I(1);
        qint64 found = -1;
        if (op == BuiltinOp::FindElement) {
            for (int i = qMax(0, start); i < qMin(end, values.size()); ++i) {
                if (values.at(i) == target) { found = i; break; }
            }
        } else {
            for (int i = qMin(end, values.size()) - 1; i >= qMax(0, start); --i) {
                if (values.at(i) == target) { found = i; break; }
            }
        }
        out = QVariant::fromValue<qint64>(found);
        return true;
    }
    case BuiltinOp::GetNum: {
        // GETNUM(变量, "常量名") -> CSV 常量名对应的整数下标（对齐 C# ConstantData.TryKeywordToInteger）
        const VariableNode* var = argVar(node, 0);
        if (!var || !m_constantTable) { out = QVariant::fromValue<qint64>(-1); return true; }
        const int idx = m_constantTable->indexForVariable(var->name(), S(1));
        out = QVariant::fromValue<qint64>(idx);
        return true;
    }
    case BuiltinOp::ExistCsv: {
        // EXISTCSV(n) -> 番号 n 的角色 CSV 模板是否存在（对齐 C# CharacterTemplate 检查）
        out = QVariant::fromValue<qint64>(storage && storage->existCsv(static_cast<int>(I(0))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::CsvChara: {
        // CSV 系函数：读取角色 **CSV 模板**的原始值（与运行时变量区分）
        //   CSVNAME/CSVCALLNAME/CSVNICKNAME/CSVMASTERNAME(chara[, idx]) -> 字符串
        //   CSVCSTR(chara, 下标)                                        -> 字符串
        //   CSVBASE/CSVABL/CSVEXP/CSVMARK/CSVTALENT/CSVCFLAG/…(chara, 下标) -> 整数
        // eraTW 用它们取角色的初始/模板数据（NEWGAME、状态显示、地图管理）。
        if (!storage) { out = QVariant(QString()); return true; }
        static const QHash<QString, QString> kStrVarOf = {
            {QStringLiteral("CSVNAME"),       QStringLiteral("NAME")},
            {QStringLiteral("CSVCALLNAME"),   QStringLiteral("CALLNAME")},
            {QStringLiteral("CSVNICKNAME"),   QStringLiteral("NICKNAME")},
            {QStringLiteral("CSVMASTERNAME"), QStringLiteral("MASTERNAME")},
            {QStringLiteral("CSVCSTR"),       QStringLiteral("CSTR")},
        };
        const QString fn = node.name().toUpper();
        const int chara = static_cast<int>(I(0));

        if (kStrVarOf.contains(fn)) {
            const QString var = kStrVarOf.value(fn);
            // 字符串型：单参数形式（CSVNAME(chara)）取元素 0
            int index = node.arguments().size() >= 2 ? static_cast<int>(I(1)) : 0;
            const QString nameArg = node.arguments().size() >= 2 ? S(1) : QString();
            if (!nameArg.isEmpty() && m_constantTable) {
                const int mapped = m_constantTable->indexForVariable(var, nameArg);
                if (mapped >= 0) index = mapped;
            }
            out = QVariant(storage->csvCharaStr(var, chara, index));
            return true;
        }

        // 数值型：CSVBASE -> BASE、CSVMAXBASE -> MAXBASE、CSVABL -> ABL …
        const QString var = fn.mid(3);   // 去掉 "CSV" 前缀
        int index = static_cast<int>(I(1));
        const QString nameArg = S(1);
        if (!nameArg.isEmpty() && m_constantTable) {
            const int mapped = m_constantTable->indexForVariable(var, nameArg);
            if (mapped >= 0) index = mapped;
        }
        out = QVariant::fromValue<qint64>(storage->csvCharaInt(var, chara, index));
        return true;
    }
    case BuiltinOp::GetColor: {
        // GETCOLOR() -> 当前文字颜色（0xRRGGBB）。由执行引擎维护并注入。
        out = QVariant::fromValue<qint64>(m_colorProvider ? m_colorProvider() : 0);
        return true;
    }
    case BuiltinOp::GetStyle: {
        // GETSTYLE() -> 当前字体样式位掩码（1=粗体 2=斜体 4=删除线 8=下划线）。
        out = QVariant::fromValue<qint64>(m_styleProvider ? m_styleProvider() : 0);
        return true;
    }
    case BuiltinOp::GetDefColor: {
        // GETDEFCOLOR() -> 默认文字颜色（C# Config.ForeColor；RESETCOLOR 还原到此值）。
        out = QVariant::fromValue<qint64>(m_defaultColorProvider ? m_defaultColorProvider() : 0xFFFFFF);
        return true;
    }
    case BuiltinOp::GetBgColor: {
        // GETBGCOLOR() -> 当前背景色（C# Console.BackColor）。
        out = QVariant::fromValue<qint64>(m_bgColorProvider ? m_bgColorProvider() : 0);
        return true;
    }
    case BuiltinOp::GetDefBgColor: {
        // GETDEFBGCOLOR() -> 默认背景色
        out = QVariant::fromValue<qint64>(m_defaultBgColorProvider ? m_defaultBgColorProvider() : 0);
        return true;
    }
    case BuiltinOp::CurrentRedraw: {
        // CURRENTREDRAW() -> 当前是否处于「允许重绘」状态（1/0）。
        // eraTW：`PREV_REDRAW = CURRENTREDRAW()` … `REDRAW 0` … `REDRAW PREV_REDRAW`。
        out = QVariant::fromValue<qint64>(m_redrawProvider ? m_redrawProvider() : 1);
        return true;
    }
    case BuiltinOp::StrLenForm:
    case BuiltinOp::StrLenFormU: {
        // STRLENFORM / STRLENFORMU <格式化串>：先把 {…}/%…% 展开，再取长度。
        // 对齐 C# STRLEN_Instruction(argisform=true)：FORM_STR 求值后
        //   STRLENFORM  -> LangManager.GetStrlenLang(str)（按语言编码的字节数）
        //   STRLENFORMU -> str.Length（UTF-16 码元数）
        const QString text = hasArg(node, 0) ? argStr(node, 0, storage, gameBaseData) : QString();
        if (op == BuiltinOp::StrLenFormU) out = QVariant::fromValue<qint64>(text.size());
        else out = QVariant::fromValue<qint64>(langByteCount(text));
        return true;
    }
    case BuiltinOp::VarSize: {
        // VARSIZE("变量名"[, 维])
        const QString name = S(0);
        const QString upper = name.toUpper();
        const int dim = node.arguments().size() >= 2 ? static_cast<int>(I(1)) : 0;
        qint64 size = 0;
        if (storage) {
            size = storage->arraySize(name);
            const VariableConfig& cfg = storage->variableConfig();
            if (size <= 0 && dim == 0) {
                size = cfg.getSize1D(name);
                if (size <= 0) size = cfg.getSize1D(upper);
                if (size <= 0) size = cfg.getSize2D(name).second;
            } else if (size <= 0 && dim == 1) {
                size = cfg.getSize2D(name).first;
            } else if (size <= 0 && dim == 2) {
                const QVector<int> s3 = cfg.getSize3D(name);
                size = s3.isEmpty() ? 0 : s3.at(0);
            }
        }
        out = QVariant::fromValue<qint64>(size);
        return true;
    }

    // ---------------- 配置 ----------------
    case BuiltinOp::GetConfig:
    case BuiltinOp::GetConfigs: {
        QString value;
        if (!m_configProvider || !m_configProvider(S(0), value)) {
            out = (op == BuiltinOp::GetConfig) ? QVariant::fromValue<qint64>(0) : QVariant(QString());
            return true;
        }
        if (op == BuiltinOp::GetConfig) out = QVariant::fromValue<qint64>(value.toLongLong());
        else out = QVariant(value);
        return true;
    }

    // ---------------- 尚未实现求值 ----------------
    case BuiltinOp::None:
        // 未实现的内置函数此前会静默产出 0/空串，表现为「函数恒为 0」而不报错
        // （CSVCSTR / EXISTCSV 就曾如此）。这里明确留痕，便于定位缺口。
        // 同一个名字只报一次（eraTW 里有在内层循环里调用的用例，避免刷屏）。
        if (!m_reportedUnfinished.contains(node.name())) {
            m_reportedUnfinished.insert(node.name());
            qWarning() << "[未完成] 内置函数尚未实现求值（返回 0）:" << node.name();
        }
        break;
    }
    Q_UNUSED(storage);
    Q_UNUSED(gameBaseData);
    Q_UNUSED(V);
    return false;
}

bool ExpressionEvaluator::evaluateInt(const QString &expression, VariableStorage *storage, qint64 &result)
{
    if (!storage) {
        return false;
    }
    
    QVariant value = evaluate(expression, storage);
    
    if (value.isValid() && value.canConvert<qint64>()) {
        result = value.value<qint64>();
        return true;
    }
    
    return false;
}

bool ExpressionEvaluator::evaluateStr(const QString &expression, VariableStorage *storage, QString &result)
{
    if (!storage) {
        return false;
    }
    
    QVariant value = evaluate(expression, storage);
    
    if (value.isValid() && value.canConvert<QString>()) {
        result = value.toString();
        return true;
    }
    
    return false;
}

// Helper method that would actually perform the evaluation
bool ExpressionEvaluator::parseAndEvaluate(const QString &expression, VariableStorage *storage)
{
    if (!storage) {
        return false;
    }
    
    // Parse the expression into an AST
    ExpressionLexer lexer;
    auto tokens = lexer.tokenize(expression);
    
    if (tokens.isEmpty()) {
        return false;
    }
    
    ExpressionParser parser;
    auto ast = parser.parse(tokens);
    
    if (!ast) {
        return false;
    }
    
    // Evaluate the AST
    evaluateNode(*ast, storage);
    
    return true;
}
