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
#include "../../GameView/graphics_store.h"
#include "../../GameView/resource_image_provider.h"
#include "game_base_data.h"
#include <QDir>
#include <QThread>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSet>
#include <QTime>
#include <QVector>
#include <cmath>
#include <algorithm>
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
        if (c == QLatin1Char('"') || c == QStringLiteral("'")) { quote = c; cur += c; continue; }
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
        if (c == QLatin1Char('"') || c == QStringLiteral("'")) {
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
    case TokenType::MULTIPLY: {
        // C# MultStrInt：str * n 与 n * str 均为字符串重复（双向）
        const QVariant& strSide  = isRuntimeString(left) ? left : right;
        const QVariant& intSide  = isRuntimeString(left) ? right : left;
        const qint64 count = intSide.toLongLong();
        if (count < 0 || count >= 10000) {
            emit evaluationError(QStringLiteral("Int"),
                                 QStringLiteral("文字列に負の値または10000以上の値(%1)を乗算しようとしました").arg(count));
            return QVariant();
        }
        return QVariant(strSide.toString().repeated(static_cast<int>(count)));
    }
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
            const QString padding(int(qMax<qint64>(0, width - units)), QLatin1Char(' '));
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
    QString upperScratch;
    const QString& upper = eraUpperKey(name, upperScratch);
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
        QString upperScratch;
        const QString& upper = eraUpperKey(varName, upperScratch);
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
            // [qdbug] 修复：RESULTS 是**全局**字符串数组（C# VariableData.cs:202
            //   varTokenDic.Add("RESULTS", new Str1DVariableToken(...))），跨函数
            //   共享；此前放函数局部槽，被调方写的返回值在调用方读不到
            //   （eraTW 的 CALLFORM COLOREDMAP_%RESULTS%_… 展开为空）。
            // 下标：RESULTS:1 此前恒读槽 0（硬编码 0）—— OPTION_SETNAME 的
            // RESULTS:0（名）/RESULTS:1（值）全落同一槽，菜单左右两列显示同一个值
            int idx = 0;
            if (node.isArray() && !node.indices().isEmpty()) {
                idx = static_cast<int>(resolveIndex(node, 0, storage, gameBaseData));
            }
            return QVariant(storage->getGlobalStr1D(QStringLiteral("RESULTS"), idx));
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

    // ---- 系统伪变量（C# VariableToken.PseudoVariableToken 族）----
    if (varName.compare(QLatin1String("__INT_MAX__"), Qt::CaseInsensitive) == 0) {
        return QVariant::fromValue<qint64>(std::numeric_limits<qint64>::max());
    }
    if (varName.compare(QLatin1String("__INT_MIN__"), Qt::CaseInsensitive) == 0) {
        return QVariant::fromValue<qint64>(std::numeric_limits<qint64>::min());
    }
    if (varName.compare(QLatin1String("EMUERA_VERSION"), Qt::CaseInsensitive) == 0) {
        // C# EMUERA_VERSIONToken -> MainWindow.InternalEmueraVer（非空字符串）
        return QVariant(QStringLiteral("1.824"));
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
        QString upperScratch;
        const QString& upper = eraUpperKey(varName, upperScratch);
        if (upper == QLatin1String("RESULTS")) {
            // [qdbug] 修复：RESULTS 全局（同上，C# VariableData.cs:202）
            // 下标：同上 —— RESULTS:1 恒读槽 0 -> OPTION 菜单左右两列同值
            int idx = 0;
            if (node.isArray() && !node.indices().isEmpty()) {
                idx = static_cast<int>(resolveIndex(node, 0, storage, gameBaseData));
            }
            return QVariant(storage->getGlobalStr1D(QStringLiteral("RESULTS"), idx));
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
        else if (u == 0x3000) out.append(QStringLiteral(" "));
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

QString ExpressionEvaluator::createBar(qint64 value, qint64 maxValue, qint64 length) const {
    if (maxValue <= 0 || length <= 0 || length >= 100) {
        return QString();
    }
    qint64 count = value * length / maxValue;
    if (count < 0) count = 0;
    if (count > length) count = length;
    QString bar = QStringLiteral("[");
    bar += QString(int(count), m_barFilled);
    bar += QString(int(length - count), m_barEmpty);
    bar += QLatin1Char(']');
    return bar;
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
    QString upperScratch;
    // REF 形参别名解析：`#DIM REF ターゲット` ← 系统变量 TARGET 时，按解析后的
    // 名字取名（系统变量的长度/元素都登记在 "TARGET" 名下）。否则 size=0 ->
    // 整数组读成空 -> FINDELEMENT(ターゲット,…) 恒 -1（eraTW 画像表示 示例立絵）。
    // 普通变量（含 REF 到用户全局）解析结果与原名字一致，行为不变。
    const QString upper = storage->systemVariableName(eraUpperKey(name, upperScratch));

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
    // 用户 #DIM 声明的数组（ARR 等）不在 variableConfig 里：按实际存储长度取
    // （此前 size 恒 0 -> MAXARRAY/SUMARRAY/FINDELEMENT 对用户数组全返回 0/-1）
    if (size <= 0) size = storage->arraySize(name);
    if (size <= 0) size = storage->arraySize(upper);
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
    QString upperScratch;
    const QString& upper = eraUpperKey(name, upperScratch);

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
        QList<const ExpressionNode*> argNodes;
        args.reserve(node.arguments().size());
        argNodes.reserve(node.arguments().size());
        for (int i = 0; i < node.arguments().size(); ++i) {
            // 省略实参（`F(a, , c)`）记为无效 QVariant —— 对齐 C# 的省略 -> 缺省值
            // 语义（字符串形参得 ""、整数形参得 0）。此前求值成整数 0，绑定到
            // 字符串形参后变成 "0"（TEMPVAR 的 VARMAKE 把空 V_STR 写成 "0"）。
            const ExpressionNode* argNode = node.arguments().at(i).get();
            argNodes.append(argNode);
            if (node.isArgOmitted(i)) {
                args.append(QVariant());
                continue;
            }
            args.append(evaluateNode(*argNode, storage, gameBaseData));
        }
        QVariant out;
        if (m_userInvoker(funcName, args, argNodes, out)) {
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
    case BuiltinOp::ChkData: {
        // CHKDATA <存档名/号>：存档存在判定（C# CheckdataMethod）
        // 走 EraEngine 注入的 provider（存档目录 + save{##}.sav 探测）
        const QString saveName = S(0);
        if (m_saveExistsProvider) { out = QVariant::fromValue<qint64>(m_saveExistsProvider(saveName)); return true; }
        out = QVariant::fromValue<qint64>(0);
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
        // eraTW 私家改造版 readme v6.1：0x0A/0x0D 之外的 C0 控制码 -> 空串
        if (i < 0x20 && i != 0x0A && i != 0x0D) { out = QVariant(QString()); return true; }
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
        // LINEISEMPTY()：当前打印缓冲是否为空（C# GlobalStatic.Console.EmptyLine）
        out = QVariant::fromValue<qint64>(m_lineEmptyProvider ? m_lineEmptyProvider() : 0);
        return true;
    case BuiltinOp::StrJoin: {
        // STRJOIN <一元数组>{, <连接符>{, <開始>{, <个数>}}}（对齐 C# JoinMethod：
        // 连接符缺省 ","；第 3/4 参为 [開始, 開始+个数) 区间（EmueraEE/eraTW v7.2）
        const VariableNode* var = argVar(node, 0);
        if (!var) { out = QVariant(QString()); return true; }
        const QString delim = (node.arguments().size() >= 2 && !node.isArgOmitted(1))
                                  ? S(1) : QStringLiteral(",");
        const bool strArray = (var->valueType() == OperandType::Str)
                              || (storage && storage->isCharaDataString(var->name()));
        const int start = (node.arguments().size() >= 3 && !node.isArgOmitted(2))
                              ? static_cast<int>(I(2)) : 0;
        QStringList parts;
        if (strArray) {
            const QList<QString> values = readStrArray(*var, storage);
            const int count = (node.arguments().size() >= 4 && !node.isArgOmitted(3))
                                  ? static_cast<int>(I(3)) : values.size() - start;
            const int end = start + count;
            for (int i = qMax(0, start); i < qMin(end, values.size()); ++i)
                parts << values.at(i);
        } else {
            const QList<qint64> values = readIntArray(*var, storage, gameBaseData, false);
            const int count = (node.arguments().size() >= 4 && !node.isArgOmitted(3))
                                  ? static_cast<int>(I(3)) : values.size() - start;
            const int end = start + count;
            for (int i = qMax(0, start); i < qMin(end, values.size()); ++i)
                parts << QString::number(values.at(i));
        }
        out = QVariant(parts.join(delim));
        return true;
    }

    // ---------------- 时间 / 常量 ----------------
    // GETTIME() -> YYYYMMDDhhmmssmmm（17 位，C# GettimeMethod）；GETTIMES 同 C# GettimesMethod
    case BuiltinOp::GetTime: {
        const QDateTime now = QDateTime::currentDateTime();
        qint64 date = now.date().year();
        date = (date * 100 + now.date().month()) * 100 + now.date().day();
        date = (date * 100 + now.time().hour()) * 100 + now.time().minute();
        date = (date * 100 + now.time().second()) * 1000 + now.time().msec();
        out = QVariant::fromValue<qint64>(date);
        return true;
    }
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
        // BARSTR(var, max, length) —— 与 BAR/BARL 指令共用 createBar()
        out = QVariant(createBar(I(0), I(1), I(2)));
        return true;
    }
    case BuiltinOp::PrintCPerLine:
    case BuiltinOp::PrintCLength: {
        // PRINTCLENGTH / PRINTCPERLINE（C# Config.PrintCLength / PrintCPerLine）
        const QPair<int,int> pc = m_printCProvider ? m_printCProvider() : qMakePair(25, 3);
        out = QVariant::fromValue<qint64>(op == BuiltinOp::PrintCLength ? pc.first : pc.second);
        return true;
    }

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
        QString text = hasArg(node, 0) ? argStr(node, 0, storage, gameBaseData) : QString();
        // C# 的 STRLENFORM 实参是 FORM_STR（装载期已把 {…}/%…% 建成展开节点）；
        // 这里拿到的是字面量原文，含展开标记时再按格式串求值一次。
        if (text.contains(QLatin1Char('{')) || text.contains(QLatin1Char('%'))) {
            const auto resolve = [](const QString& e) -> QSharedPointer<ExpressionNode> {
                ExpressionLexer lexer;
                ExpressionParser parser;
                return parser.parse(lexer.tokenize(e, 1));
            };
            if (auto form = StrFormParser::parse(text, resolve)) {
                text = evaluate(*form.staticCast<ExpressionNode>(), storage, gameBaseData).toString();
            }
        }
        if (op == BuiltinOp::StrLenFormU) out = QVariant::fromValue<qint64>(text.size());
        else out = QVariant::fromValue<qint64>(langByteCount(text));
        return true;
    }
    case BuiltinOp::VarSize: {
        // VARSIZE("变量名"[, 维])：名字给成**字符串**；
        // VARSIZE 变量名          ：名字给成**标识符**（eraMegaten DUNGEON_POINTER
        //                           `VARSIZE 迷宮00`；C# 的 VariableTerm 形态）。
        // 之前只认字符串 -> 标识符形态先报「第 1 个参数需要字符串表达式」，
        // 即便放过也会去求值变量（拿到元素值）而不是它的名字。
        QString name;
        const QSharedPointer<ExpressionNode>& a0 = node.arguments().value(0);
        if (a0 && a0->kind() == NodeKind::Variable) {
            name = static_cast<const VariableNode&>(*a0).name();
        } else {
            name = S(0);
        }
        QString upperScratch;
        const QString& upper = eraUpperKey(name, upperScratch);
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

    // ---------------- G 图像 / 精灵 ----------------
    // 对齐 C# Graphics*Method / Sprite*Method（Creator.Method.cs）：
    // 失败一律返回 0（不抛错），eraTW 用返回值判断资源是否存在。
    case BuiltinOp::GCreated: {
        out = QVariant::fromValue<qint64>(GraphicsStore::gCreated(I(0)) ? 1 : 0);
        return true;
    }
    case BuiltinOp::GWidth:
    case BuiltinOp::GHeight: {
        const QImage image = GraphicsStore::gImage(I(0));
        out = QVariant::fromValue<qint64>(
            op == BuiltinOp::GWidth ? image.width() : image.height());
        return true;
    }
    case BuiltinOp::GCreate: {
        out = QVariant::fromValue<qint64>(
            GraphicsStore::gCreate(I(0), static_cast<int>(I(1)), static_cast<int>(I(2))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::GCreateFromFile: {
        out = QVariant::fromValue<qint64>(
            GraphicsStore::gCreateFromFile(I(0), S(1)) ? 1 : 0);
        return true;
    }
    case BuiltinOp::GDispose: {
        out = QVariant::fromValue<qint64>(GraphicsStore::gDispose(I(0)) ? 1 : 0);
        return true;
    }
    case BuiltinOp::GClear: {
        // GCLEAR 的两个形态（EM 私家版拡張；对齐 EE GraphicsClearMethod）：
        //   2 参：id, cARGB                    -> 全图清除
        //   6 参：id, cARGB, x, y, w, h        -> 只清除该矩形（SetClip+Clear+ResetClip）
        // 6 参形态由扩展经 regCoreArgRange 放宽实参个数区间后到达这里。
        if (node.arguments().size() >= 6) {
            out = QVariant::fromValue<qint64>(GraphicsStore::gFillRectangle(
                I(0), QColor::fromRgba(static_cast<QRgb>(I(1))),
                QRect(static_cast<int>(I(2)), static_cast<int>(I(3)),
                      static_cast<int>(I(4)), static_cast<int>(I(5)))) ? 1 : 0);
            return true;
        }
        out = QVariant::fromValue<qint64>(
            GraphicsStore::gClear(I(0), QColor::fromRgba(static_cast<QRgb>(I(1)))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::GFillRectangle: {
        // 对齐 C# GraphicsFillRectangleMethod：`GFILLRECTANGLE id, x, y, w, h`
        // （**没有颜色实参**）—— 用 GSETBRUSH 设定的画刷填充。此前误把第 2 个
        // 实参当颜色、并把矩形整体后移一位（对齐 emuera.em `ReadRectangle(argNo=1)`）。
        out = QVariant::fromValue<qint64>(GraphicsStore::gFillRectangle(
            I(0), GraphicsStore::brushColor(I(0)),
            QRect(static_cast<int>(I(1)), static_cast<int>(I(2)),
                  static_cast<int>(I(3)), static_cast<int>(I(4)))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::GSetColor: {
        // GSETCOLOR id, cARGB, x, y：单像素填充
        auto it = GraphicsStore::gImage(I(0));
        if (it.isNull()) { out = QVariant::fromValue<qint64>(0); return true; }
        const int x = static_cast<int>(I(2)), y = static_cast<int>(I(3));
        if (x < 0 || y < 0 || x >= it.width() || y >= it.height()) {
            out = QVariant::fromValue<qint64>(0);
            return true;
        }
        it.setPixel(x, y, QColor::fromRgba(static_cast<QRgb>(I(1))).rgba());
        GraphicsStore::gDrawImage(I(0), it, it.rect());
        out = QVariant::fromValue<qint64>(1);
        return true;
    }
    case BuiltinOp::GGetColor: {
        const QImage image = GraphicsStore::gImage(I(0));
        const int x = static_cast<int>(I(1)), y = static_cast<int>(I(2));
        if (image.isNull() || x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
            out = QVariant::fromValue<qint64>(-1);
            return true;
        }
        const QRgb c = image.pixel(x, y);
        // 对齐 C# GraphicsGetColorMethod：`c.ToArgb() & 0xFFFFFFFF`（ARGB 无符号 32 位）。
        // 此前写成 `(c << 24) | r<<16 | g<<8 | b` —— 高位重复左移，返回值是垃圾。
        out = QVariant::fromValue<qint64>(static_cast<qint64>(c) & 0xFFFFFFFFLL);
        return true;
    }
    case BuiltinOp::GDrawG: {
        // GDRAWG id, srcId[, destX, destY, destW, destH[, srcX, srcY, srcW, srcH[, CM]]]
        const int count = node.arguments().size();
        const QRect dest = count >= 6
            ? QRect(int(I(2)), int(I(3)), int(I(4)), int(I(5)))
            : QRect();
        const QImage src = GraphicsStore::gImage(I(1));
        if (src.isNull()) { out = QVariant::fromValue<qint64>(0); return true; }
        QRect srcRect;
        if (count >= 10) srcRect = QRect(int(I(6)), int(I(7)), int(I(8)), int(I(9)));
        float cm[5][5];
        const bool hasCm = count >= 11 && readColorMatrix(node, 10, storage, gameBaseData, cm);
        out = QVariant::fromValue<qint64>(GraphicsStore::gDrawG(
            I(0), I(1), dest, srcRect, hasCm ? cm : nullptr) ? 1 : 0);
        return true;
    }
    case BuiltinOp::GDrawSprite: {
        // GDRAWSPRITE id, name[, destX, destY, destW, destH[, CM]]
        const int count = node.arguments().size();
        const QString spriteName = S(1);
        const QImage sprite = GraphicsStore::spriteImage(spriteName);
        if (sprite.isNull()) { out = QVariant::fromValue<qint64>(0); return true; }
        QRect dest = count >= 6
            ? QRect(int(I(2)), int(I(3)), int(I(4)), int(I(5)))
            : QRect(0, 0, sprite.width(), sprite.height());
        // C# ASpriteSingle.GraphicsDraw(g, destRect)：把 DestBasePosition 按
        // 「目标尺寸 / 源矩形尺寸」同比缩放后加到目标原点。eraTW 的白蓮(55)/路人立绘
        // 就是「把带 CSV 偏移的部件叠进一张 G」合成的 —— 丢掉偏移，部件全挤到 (0,0)，
        // 立绘不完整、差分图像盖不到该盖的地方。
        int bx = 0, by = 0;
        if (!dest.isEmpty() && GraphicsStore::spriteBasePos(spriteName, bx, by)
            && (bx != 0 || by != 0)) {
            dest.moveLeft(dest.x() + bx * dest.width() / qMax(1, sprite.width()));
            dest.moveTop(dest.y() + by * dest.height() / qMax(1, sprite.height()));
        }
        float cm[5][5];
        const bool hasCm = count >= 7 && readColorMatrix(node, 6, storage, gameBaseData, cm);
        out = QVariant::fromValue<qint64>(GraphicsStore::gDrawImage(
            I(0), sprite, dest, hasCm ? cm : nullptr) ? 1 : 0);
        return true;
    }
    case BuiltinOp::SpriteCreated: {
        // 运行期精灵（SPRITECREATE 产物）或静态资源（C# AppContents 会把
        // resources 下所有 csv 条目注册成精灵，eraTW 用
        // `SPRITECREATED("55_A1")` 检查资源是否安装）任一存在即真。
        out = QVariant::fromValue<qint64>(
            (GraphicsStore::spriteCreated(S(0)) || ResourceImageProvider::hasResource(S(0))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::SpriteWidth:
    case BuiltinOp::SpriteHeight: {
        QImage image = GraphicsStore::spriteImage(S(0));
        int width = image.width(), height = image.height();
        if (image.isNull() && ResourceImageProvider::intrinsicSize(S(0), width, height)) {
            // 静态资源：按图集矩形 / 文件实际尺寸
        }
        out = QVariant::fromValue<qint64>(op == BuiltinOp::SpriteWidth ? width : height);
        return true;
    }
    case BuiltinOp::SpritePosX:
    case BuiltinOp::SpritePosY: {
        // C# SpriteStateMethod：精灵不存在时返回 0（不是 -1）；静态资源也返回
        // 自己的 DestBasePosition（CSV 第 7/8 列）。
        int bx = 0, by = 0;
        const bool found = GraphicsStore::spriteBasePos(S(0), bx, by);
        out = QVariant::fromValue<qint64>(found ? (op == BuiltinOp::SpritePosX ? bx : by) : 0);
        return true;
    }
    case BuiltinOp::SpriteMove:
    case BuiltinOp::SpriteSetPos: {
        // C# SPRITESETPOS 是绝对、SPRITEMOVE 是相对（DestBasePosition.Offset）。
        // 两者对静态资源（CSV 精灵）同样有效 —— eraTW 虽然没用，但保持语义一致。
        const QString name = S(0);
        int bx = 0, by = 0;
        if (!GraphicsStore::spriteBasePos(name, bx, by)) {
            out = QVariant::fromValue<qint64>(0);
            return true;
        }
        if (op == BuiltinOp::SpriteMove) {
            GraphicsStore::spriteSetPos(name, bx + static_cast<int>(I(1)),
                                        by + static_cast<int>(I(2)));
        } else {
            GraphicsStore::spriteSetPos(name, static_cast<int>(I(1)), static_cast<int>(I(2)));
        }
        out = QVariant::fromValue<qint64>(1);
        return true;
    }
    case BuiltinOp::SpriteGetColor: {
        const QImage image = GraphicsStore::spriteImage(S(0));
        const int x = static_cast<int>(I(1)), y = static_cast<int>(I(2));
        if (image.isNull() || x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
            out = QVariant::fromValue<qint64>(-1);
            return true;
        }
        const QRgb c = image.pixel(x, y);
        out = QVariant::fromValue<qint64>((qint64(qAlpha(c)) << 24) | qRed(c) << 16 | qGreen(c) << 8 | qBlue(c));
        return true;
    }
    case BuiltinOp::SpriteCreate: {
        // SPRITECREATE name, gID[, x, y, w, h]
        const QRect rect = node.arguments().size() >= 6
            ? QRect(int(I(2)), int(I(3)), int(I(4)), int(I(5)))
            : QRect();
        out = QVariant::fromValue<qint64>(GraphicsStore::spriteCreate(S(0), I(1), rect) ? 1 : 0);
        return true;
    }
    case BuiltinOp::SpriteDispose: {
        out = QVariant::fromValue<qint64>(GraphicsStore::spriteDispose(S(0)) ? 1 : 0);
        return true;
    }


    // ---------------- 显示状态 / 输入（对齐 C# Console 系 Method）----------------
    case BuiltinOp::CurrentAlign: {
        // CURRENTALIGN() -> "LEFT"/"CENTER"/"RIGHT"（C# CurrentAlignMethod 返回 string）
        const int align = m_alignProvider ? m_alignProvider() : 0;
        out = align == 1 ? QStringLiteral("CENTER") : align == 2 ? QStringLiteral("RIGHT") : QStringLiteral("LEFT");
        return true;
    }
    case BuiltinOp::GetFocusColor: {
        // GETFOCUSCOLOR() -> 选中文字色（C# Config.FocusColor，默认黄色）
        out = QVariant::fromValue<qint64>(m_focusColorProvider ? m_focusColorProvider() : 0xFFFF00);
        return true;
    }
    case BuiltinOp::GetFont: {
        // GETFONT() -> 当前字体名（C# Console.Font.Name）
        out = m_fontProvider ? m_fontProvider() : QString();
        return true;
    }
    case BuiltinOp::ChkFont: {
        // CHKFONT <字体名> -> 系统是否安装该字体（C# Config.IsFontExists）。
        // 注意：QFontDatabase 需要 QGuiApplication；无 GUI 的宿主（test_cli 等
        // 用 QCoreApplication）调用它会**段错误**，因此先探测应用类型。
        const bool hasGui = (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr);
        out = QVariant::fromValue<qint64>(hasGui && QFontDatabase::hasFamily(S(0)) ? 1 : 0);
        return true;
    }
    case BuiltinOp::ClientWidth:
    case BuiltinOp::ClientHeight: {
        // CLIENTWIDTH / CLIENTHEIGHT -> 逻辑客户区列数/行数
        const QPair<int,int> size = m_clientSizeProvider ? m_clientSizeProvider() : qMakePair(0, 0);
        out = QVariant::fromValue<qint64>(op == BuiltinOp::ClientWidth ? size.first : size.second);
        return true;
    }
    case BuiltinOp::Isskip:
    case BuiltinOp::Messkip:
    case BuiltinOp::MouseSkip: {
        // ISSKIP / MESSKIP / MOUSESKIP -> 是否处于对应跳过状态
        const int kind = (op == BuiltinOp::Isskip) ? 0 : (op == BuiltinOp::Messkip) ? 1 : 2;
        out = QVariant::fromValue<qint64>(m_skipProvider ? m_skipProvider(kind) : 0);
        return true;
    }
    case BuiltinOp::GetLineStr: {
        // GETLINESTR <行号> -> 该显示行的文本
        out = m_lineStrProvider ? m_lineStrProvider(int(I(0))) : QString();
        return true;
    }
    case BuiltinOp::Extension: {
        // 扩展式中函数（EE 等）：实参求值后交给注册类注入的回调。
        // 引擎不内联任何扩展名/实现（原生归原生、扩展归扩展）。
        if (!m_extensionFnInvoker) return false;
        QList<QVariant> args;
        QList<const ExpressionNode*> argNodes;
        args.reserve(node.arguments().size());
        argNodes.reserve(node.arguments().size());
        for (int i = 0; i < node.arguments().size(); ++i) {
            const auto& arg = node.arguments().at(i);
            argNodes.append(arg.get());
            args.append(node.isArgOmitted(i) ? QVariant()
                                             : evaluateNode(*arg, storage, gameBaseData));
        }
        return m_extensionFnInvoker(node.name(), args, argNodes, out);
    }
    case BuiltinOp::GetKey:
    case BuiltinOp::GetKeyTriggered: {
        // GETKEY / GETKEYTRIGGERED <键码>：无 GUI 输入源时恒 0
        out = QVariant::fromValue<qint64>(0);
        return true;
    }
    case BuiltinOp::MouseX:
    case BuiltinOp::MouseY: {
        out = QVariant::fromValue<qint64>(0);
        return true;
    }
    case BuiltinOp::IsActive: {
        // ISACTIVE() -> 主窗口是否激活；CLI 下视为激活
        out = QVariant::fromValue<qint64>(1);
        return true;
    }

    // ---------------- 随机数状态（C# DumpRanddata / InitRanddata）----------------
    case BuiltinOp::DumpRand: {
        // DUMPRAND：MT 状态写入 RANDDATA 数组（C# rand.GetRand(RANDDATA)）
        const QList<qint64> state = m_rand.state();
        for (int i = 0; i < state.size(); ++i)
            storage->setGlobalInt1D(QStringLiteral("RANDDATA"), i, state.at(i));
        out = QVariant::fromValue<qint64>(0);
        return true;
    }
    case BuiltinOp::InitRand: {
        // INITRAND：从 RANDDATA 数组恢复 MT 状态（C# rand.SetRand(RANDDATA)）
        QList<qint64> state;
        state.reserve(Mt19937::StateLength);
        for (int i = 0; i < Mt19937::StateLength; ++i)
            state.append(storage->getGlobalInt1D(QStringLiteral("RANDDATA"), i));
        m_rand.setState(state);
        out = QVariant::fromValue<qint64>(0);
        return true;
    }

    // ---------------- 角色操作 / 检索 ----------------
    case BuiltinOp::GetChara:
    case BuiltinOp::GetSpChara: {
        // GETCHARA <番号>{, <SP判定>} / GETSPCHARA <番号>
        // 在角色列表中找 NO == 番号 的角色，返回运行时下标；找不到 -1。
        // 第 2 实参非 0 时只检索 SP 角色（GETSPCHARA 等价于 GETCHARA(no, 1)）。
        const qint64 no = I(0);
        const bool wantSp = (op == BuiltinOp::GetSpChara) || (node.arguments().size() >= 2 && I(1) != 0);
        const int count = charaCount(storage);
        for (int i = 0; i < count; ++i) {
            const bool isSp = storage->isSpChara(i);
            if (wantSp != isSp) continue;
            if (storage->charaCsvNo(i) == static_cast<int>(no)) {
                out = QVariant::fromValue<qint64>(i);
                return true;
            }
        }
        out = QVariant::fromValue<qint64>(-1);
        return true;
    }
    case BuiltinOp::FindChara:
    case BuiltinOp::FindCharaLast:
    case BuiltinOp::FindCharaData:
    case BuiltinOp::FindCharaDataLast: {
        // FINDCHARA <角色变量>, <式>{, <開始>, <終了>}（FINDLASTCHARA 从末尾找；
        // FIND_CHARADATA 系检索用户 CHARADATA 变量，检索方式相同）
        const VariableNode* var = argVar(node, 0);
        if (!var || !storage) { out = QVariant::fromValue<qint64>(-1); return true; }
        const QString varName = var->name();
        const bool isStr = storage->isCharaDataString(varName);
        qint64 element = 0;
        if (!var->indices().isEmpty())
            element = evaluateNode(*var->indices().first(), storage, gameBaseData).toLongLong();
        const bool last = (op == BuiltinOp::FindCharaLast || op == BuiltinOp::FindCharaDataLast);
        const int count = charaCount(storage);
        // C#：開始缺省 0、終了缺省 CHARANUM（FINDCHARA(V, X) 是合法的省略形式）
        const int start = qBound(0,
            (node.arguments().size() > 2 && !node.isArgOmitted(2)) ? int(I(2)) : 0, count);
        const int end = qMin(count,
            (node.arguments().size() > 3 && !node.isArgOmitted(3)) ? int(I(3)) : count);
        for (int step = 0; step <= (end - start); ++step) {
            const int i = last ? (end - 1 - step) : (start + step);
            if (i < start || i >= end) break;
            if (isStr) {
                if (storage->getCharaStr(varName, i, int(element)) == S(1)) {
                    out = QVariant::fromValue<qint64>(i);
                    return true;
                }
            } else {
                if (storage->getCharaInt(varName, i, int(element)) == I(1)) {
                    out = QVariant::fromValue<qint64>(i);
                    return true;
                }
            }
        }
        out = QVariant::fromValue<qint64>(-1);
        return true;
    }
    case BuiltinOp::ChkCharaData: {
        // CHKCHARADATA <变量名> -> 是否为已登记的角色数据变量
        out = QVariant::fromValue<qint64>(storage && storage->isCharaDataVariable(S(0)) ? 1 : 0);
        return true;
    }
    case BuiltinOp::SwapChara: {
        storage->swapChara(int(I(0)), int(I(1)));
        out = QVariant::fromValue<qint64>(0);
        return true;
    }
    case BuiltinOp::CopyChara: {
        storage->copyChara(int(I(0)), int(I(1)));
        out = QVariant::fromValue<qint64>(0);
        return true;
    }
    case BuiltinOp::AddCopyChara: {
        storage->addCopyChara(int(I(0)));
        out = QVariant::fromValue<qint64>(0);
        return true;
    }
    case BuiltinOp::PickupChara: {
        // PICKUPCHARA <角色>(, <角色>…)：角色列表重排为给定序列
        QList<int> indexes;
        for (int i = 0; i < node.arguments().size(); ++i)
            indexes.append(int(I(i)));
        storage->pickupChara(indexes);
        out = QVariant::fromValue<qint64>(0);
        return true;
    }
    case BuiltinOp::SaveChara: {
        // SAVECHARA <文件名>, <摘要>{, <角色番号>…}：角色清单写入 sav/
        const QString path = saveFilePath(S(0));
        if (node.arguments().size() >= 3) {
            QList<int> indexes;
            for (int i = 2; i < node.arguments().size(); ++i)
                indexes.append(int(I(i)));
            storage->pickupChara(indexes);
        }
        QFile file(path);
        out = QVariant::fromValue<qint64>(
            (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)
                 && file.write(storage->dumpCharaList().toUtf8()) >= 0) ? 1 : 0);
        return true;
    }
    case BuiltinOp::LoadChara: {
        // LOADCHARA <文件名>：把角色清单追加进当前列表
        QFile file(saveFilePath(S(0)));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out = QVariant::fromValue<qint64>(0);
            return true;
        }
        storage->appendCharaList(QString::fromUtf8(file.readAll()));
        out = QVariant::fromValue<qint64>(1);
        return true;
    }
    case BuiltinOp::ResetStain: {
        // RESET_STAIN <角色>：STAIN 数组恢复初始值（_REPLACE.CSV 初值未装载时按 0 处理）
        // STAIN 是**角色**变量，arraySize 查不到 —— 尺寸从 VariableSize 配置取
        const int target = int(I(0));
        int n = storage->variableConfig().getSize1D(QStringLiteral("STAIN"));
        if (n <= 0) n = 100;
        for (int i = 0; i < n; ++i)
            storage->setCharaInt(QStringLiteral("STAIN"), target, i, 0);
        out = QVariant::fromValue<qint64>(0);
        return true;
    }

    // ---------------- 存档 / 文本 ----------------
    case BuiltinOp::SaveText: {
        // SAVETEXT <文字列式>, <ファイル番号>{, <force_savdir>, <force_UTF8>}
        // 对齐 C# SaveTextMethod（Creator.Method.cs:4118）：
        //   整段文本覆写 sav/txt{番号:02}.txt（getSaveDataPathText）
        const QString path = saveFilePath(QStringLiteral("txt%1.txt")
                                              .arg(int(I(1)), 2, 10, QLatin1Char('0')));
        QFile file(path);
        out = QVariant::fromValue<qint64>(
            (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)
                 && file.write(S(0).toUtf8()) >= 0) ? 1 : 0);
        return true;
    }
    case BuiltinOp::LoadText: {
        // LOADTEXT <ファイル番号>{, <force_savdir>, <force_UTF8>} -> 整段文本
        // 对齐 C# LoadTextMethod：**不是**行区间，而是读回整个文件内容。
        QFile file(saveFilePath(QStringLiteral("txt%1.txt")
                                    .arg(int(I(0)), 2, 10, QLatin1Char('0'))));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { out = QString(); return true; }
        out = QString::fromUtf8(file.readAll());
        return true;
    }
    case BuiltinOp::PutForm: {
        // PUTFORM <FORM文本>：存档概要**追加**（对齐 C# Process.ScriptProc.cs:291-293：
        // SAVEDATA_TEXT 非空则 +=、否则 =；@SAVEINFO 内可多次调用逐段拼接）
        // 写入走 setSystemStr（与读取点 SAVEDATA_TEXT 分支同一存储 m_systemStrVars；
        // 此前写 setGlobalStr1D -> m_globalStr1D，读取 getSystemStr -> m_systemStrVars，
        // 两张表不一致导致概要永不生效）
        const QString prev = storage->getSystemStr(QStringLiteral("SAVEDATA_TEXT"), 0);
        storage->setSystemStr(QStringLiteral("SAVEDATA_TEXT"), 0, prev + S(0));
        out = QVariant::fromValue<qint64>(0);
        return true;
    }
    case BuiltinOp::SaveNos: {
        // SAVENOS <数值变量>：NOS 数组写入 sav/nos.dat
        QFile file(saveFilePath(QStringLiteral("nos.dat")));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            out = QVariant::fromValue<qint64>(0);
            return true;
        }
        const int n = storage->arraySize(QStringLiteral("NOS"));
        for (int i = 0; i < n; ++i)
            file.write(QString("%1\n").arg(storage->getGlobalInt1D(QStringLiteral("NOS"), i)).toUtf8());
        out = QVariant::fromValue<qint64>(1);
        return true;
    }
    case BuiltinOp::DebugClear: {
        if (m_clearProvider) m_clearProvider();
        out = QVariant::fromValue<qint64>(0);
        return true;
    }

    // ---------------- 数组 / 阈值 ----------------
    case BuiltinOp::ArrayMSort: {
        // ARRAYMSORT <角色变量>, <顺序>{, <開始>, <数>}：按变量值对角色多重排序
        const VariableNode* var = argVar(node, 0);
        if (!var) { out = QVariant::fromValue<qint64>(0); return true; }
        const bool forward = (node.arguments().size() < 2 || I(1) == 0);
        const int count = charaCount(storage);
        QList<int> order;
        order.reserve(count);
        for (int i = 0; i < count; ++i) order.append(i);
        const QString varName = var->name();
        qint64 element = 0;
        if (!var->indices().isEmpty())
            element = evaluateNode(*var->indices().first(), storage, gameBaseData).toLongLong();
        const bool isStr = storage->isCharaDataString(varName);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            if (isStr) {
                const QString va = storage->getCharaStr(varName, a, int(element));
                const QString vb = storage->getCharaStr(varName, b, int(element));
                return forward ? va < vb : va > vb;
            }
            const qint64 va = storage->getCharaInt(varName, a, int(element));
            const qint64 vb = storage->getCharaInt(varName, b, int(element));
            return forward ? va < vb : va > vb;
        });
        storage->pickupChara(order);
        out = QVariant::fromValue<qint64>(0);
        return true;
    }
    case BuiltinOp::GetPalamLv:
    case BuiltinOp::GetExpLv: {
        // GETPALAMLV / GETEXPLV <值>, <上限LV>：与 PALAMLV/EXPLV 阈值表逐级比较
        // （对齐 C# getPalamLv：pl < 阈值[i+1] -> 返回 i，全不满足返回 maxlv）
        const QString table = (op == BuiltinOp::GetPalamLv) ? QStringLiteral("PALAMLV")
                                                            : QStringLiteral("EXPLV");
        const qint64 pl = I(0);
        const qint64 maxlv = I(1);
        // PALAMLV/EXPLV 是**系统**数组（C# varData.DataIntegerArray[PALAMLV]）：
        // 写入走 setSystemVariable，读取必须同样优先系统槽，否则恒读 0
        // -> 所有值都 >= 0 的阈值比较失败 -> 恒返回 maxlv。
        const bool isSys = storage->hasSystemVariable(table);
        for (qint64 i = 0; i < maxlv; ++i) {
            const qint64 threshold = isSys
                ? storage->getSystemVariable(table, int(i + 1))
                : storage->getGlobalInt1D(table, int(i + 1));
            if (pl < threshold) {
                out = QVariant::fromValue<qint64>(i);
                return true;
            }
        }
        out = QVariant::fromValue<qint64>(maxlv);
        return true;
    }

    // ---------------- 颜色 / 其它 ----------------
    case BuiltinOp::ColorFromName: {
        // COLOR_FROMNAME <颜色名>：C_* 常量 / SVG 颜色名 / 0xRRGGBB -> 0xRRGGBB
        QString name = S(0).trimmed();
        if (name.startsWith(QLatin1Char('#'))) name.remove(0, 1);
        bool ok = false;
        const uint v = name.toUInt(&ok, 0);
        if (ok) { out = QVariant::fromValue<qint64>(qint64(v & 0xFFFFFF)); return true; }
        if (name.startsWith(QStringLiteral("C_"), Qt::CaseInsensitive)) name.remove(0, 2);
        const QColor c(name.toLower());
        out = QVariant::fromValue<qint64>(c.isValid() ? qint64(c.rgb() & 0xFFFFFF) : qint64(0xFFFFFF));
        return true;
    }
    case BuiltinOp::ColorFromRgb: {
        // COLOR_FROMRGB <红>, <绿>, <蓝>
        out = QVariant::fromValue<qint64>((I(0) << 16) | ((I(1) & 0xFF) << 8) | (I(2) & 0xFF));
        return true;
    }
    case BuiltinOp::SetAnimeTimer: {
        // SETANIMETIMER <时间>：动画时间基准（渲染层暂未消费，接受并忽略）
        out = QVariant::fromValue<qint64>(0);
        return true;
    }
    case BuiltinOp::TwAit: {
        // TWAIT <毫秒>{, <可否跳过>}：无条件等待指定毫秒
        const qint64 ms = I(0);
        if (ms > 0 && ms < 60'000) QThread::msleep(uint(ms));
        out = QVariant::fromValue<qint64>(0);
        return true;
    }
    case BuiltinOp::ResetBgColor: {
        // RESETBGCOLOR：背景色复位（渲染层当前未消费背景色状态）
        out = QVariant::fromValue<qint64>(0);
        return true;
    }

    // ---------------- HTML ----------------
    case BuiltinOp::HtmlEscape: {
        // HTML_ESCAPE <字符串>：转义 HTML 实体
        QString text = S(0);
        text.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
        text.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
        text.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
        text.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
        text.replace(QLatin1Char('\''), QStringLiteral("&apos;"));
        out = text;
        return true;
    }
    case BuiltinOp::HtmlToPlainText: {
        // HTML_TOPLAINTEXT <字符串>：剥掉标签 + 实体还原
        QString text = S(0);
        text.replace(QRegularExpression(QStringLiteral("<[^>]*>")), QString());
        text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
        text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        text.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
        text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
        text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        out = text;
        return true;
    }
    case BuiltinOp::HtmlGetPrintedStr: {
        // HTML_GETPRINTEDSTR <行号>：该行打印缓冲的 HTML 原文
        out = m_htmlGetProvider ? m_htmlGetProvider(int(I(0))) : QString();
        return true;
    }
    case BuiltinOp::HtmlPopPrintingStr: {
        // HTML_POPPRINTINGSTR()：弹出当前打印缓冲的 HTML 原文
        out = m_htmlPopProvider ? m_htmlPopProvider() : QString();
        return true;
    }

    // ---------------- G 图像补全 / CBG / 精灵动画 ----------------
    case BuiltinOp::GSetBrush: {
        // GSETBRUSH id, cARGB：颜色是 **ARGB**（对齐 C# ReadColor -> Color.FromArgb）。
        // 此前用 QColor(int) 当成 RGB（丢 alpha）。
        out = QVariant::fromValue<qint64>(
            GraphicsStore::gSetBrush(int(I(0)), QColor::fromRgba(static_cast<QRgb>(I(1)))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::GSetPen: {
        out = QVariant::fromValue<qint64>(GraphicsStore::gSetPen(int(I(0)), QColor(int(I(1))), int(I(2))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::GSetFont: {
        out = QVariant::fromValue<qint64>(GraphicsStore::gSetFont(int(I(0)), S(1), int(I(2))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::GSave: {
        // GSAVE <ID>, <文件编号>：G 图像存为 sav/g{no}.png
        out = QVariant::fromValue<qint64>(GraphicsStore::gSave(int(I(0)),
            saveFilePath(QStringLiteral("img%1.png").arg(int(I(1)), 4, 10, QLatin1Char('0')))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::GLoad: {
        out = QVariant::fromValue<qint64>(GraphicsStore::gLoad(int(I(0)),
            saveFilePath(QStringLiteral("img%1.png").arg(int(I(1)), 4, 10, QLatin1Char('0')))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::GDrawGWithMask: {
        out = QVariant::fromValue<qint64>(GraphicsStore::gDrawGWithMask(
            int(I(0)), int(I(1)), int(I(2)), int(I(3)), int(I(4))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::CbgSetG: {
        out = QVariant::fromValue<qint64>(GraphicsStore::cbgSetG(int(I(0)), int(I(1)), int(I(2)), int(I(3))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::CbgSetSprite: {
        out = QVariant::fromValue<qint64>(GraphicsStore::cbgSetSprite(S(0), int(I(1)), int(I(2)), int(I(3))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::CbgSetButtonSprite: {
        // CBGSETBUTTONSPRITE <按钮值>, <精灵名>, <选中精灵名>, <x>, <y>, <z>{, <tooltip>}
        out = QVariant::fromValue<qint64>(GraphicsStore::cbgSetButtonSprite(
            I(0), S(1), S(2), int(I(3)), int(I(4)), int(I(5)),
            node.arguments().size() >= 7 ? S(6) : QString()) ? 1 : 0);
        return true;
    }
    case BuiltinOp::CbgSetBmapG: {
        out = QVariant::fromValue<qint64>(GraphicsStore::cbgSetBmapG(int(I(0))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::CbgClear:      GraphicsStore::cbgClear();           break;
    case BuiltinOp::CbgClearButton: GraphicsStore::cbgClearButton();     break;
    case BuiltinOp::CbgRemoveRange: {
        out = QVariant::fromValue<qint64>(GraphicsStore::cbgRemoveRange(int(I(0)), int(I(1))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::CbgRemoveBmap: GraphicsStore::cbgRemoveBmap();       break;
    case BuiltinOp::SpriteAnimeCreate: {
        out = QVariant::fromValue<qint64>(GraphicsStore::spriteAnimeCreate(S(0), int(I(1)), int(I(2))) ? 1 : 0);
        return true;
    }
    case BuiltinOp::SpriteAnimeAddFrame: {
        out = QVariant::fromValue<qint64>(GraphicsStore::spriteAnimeAddFrame(
            S(0), int(I(1)), int(I(2)), int(I(3)), int(I(4)), int(I(5)),
            int(I(6)), int(I(7)), int(I(8))) ? 1 : 0);
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


QString ExpressionEvaluator::saveFilePath(const QString& fileName) const {
    if (m_saveDirectory.isEmpty()) return fileName;
    QDir().mkpath(m_saveDirectory);   // 首次写盘时按需创建 sav/
    return m_saveDirectory + QLatin1Char('/') + fileName;
}

bool ExpressionEvaluator::readColorMatrix(const FunctionNode &node, int argNo,
                                          VariableStorage *storage, GameBaseData *gameBaseData,
                                          float out[5][5]) {
    // 对齐 C# ReadColormatrix：第 argNo 个实参是 2D/3D 数组变量的一个元素引用
    // （如 `カラーマトリクス:0:0`），以它为左上角读 5x5 个整数并除以 256。
    const VariableNode* var = argVar(node, argNo);
    if (!var || !storage) return false;
    const QList<int> ids = [&]{
        QList<int> ids;
        ids.reserve(var->indices().size());
        for (int i = 0; i < var->indices().size(); ++i)
            ids.append(static_cast<int>(resolveIndex(*var, i, storage, gameBaseData)));
        return ids;
    }();
    const QString name = storage->resolvedStorageName(var->name());
    if (var->indices().size() >= 3) {
        const int e1 = ids.value(0), e2 = ids.value(1), e3 = ids.value(2);
        if (e2 < 0 || e3 < 0) return false;
        for (int x = 0; x < 5; ++x)
            for (int y = 0; y < 5; ++y)
                out[x][y] = static_cast<float>(storage->getGlobalInt3D(name, e1, e2 + x, e3 + y)) / 256.0f;
        return true;
    }
    if (var->indices().size() == 2) {
        const int e1 = ids.value(0), e2 = ids.value(1);
        if (e1 < 0 || e2 < 0) return false;
        for (int x = 0; x < 5; ++x)
            for (int y = 0; y < 5; ++y)
                out[x][y] = static_cast<float>(storage->getGlobalInt2D(name, e1 + x, e2 + y)) / 256.0f;
        return true;
    }
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
