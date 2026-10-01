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
#include "execution_engine.h"
#include <QRegularExpression>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "expression_evaluator.h"
#include "ast/expression_ast.h"
#include "ast/ast_builder.h"
#include "ast/function_types.h"   // 函数语句（isBuiltinFunction / builtinFunctionReturnType）
#include "ast/strform_parser.h"
#include "ast/print_template.h"
#include "era_parse_table.h"
#include "constant_table.h"
#include "eraengine.h"
#include "eraengine_log.h"
#include "function_system.h"

namespace {

// 优先使用 EraParseTable 缓存的 AST 求值（AST 单一解析流水线）；
// 未命中再回退到字符串入口。这样执行侧不再“二次解析”表达式。
QVariant evalExpressionCached(EraParseTable* table,
                              ExpressionEvaluator& evaluator,
                              const QString& expr,
                              VariableStorage* storage,
                              GameBaseData* gameBaseData) {
    if (table) {
        const QSharedPointer<ExpressionNode> ast = table->expressionAst(expr);
        if (ast) {
            return evaluator.evaluate(*ast, storage, gameBaseData);
        }
    }
    return evaluator.evaluate(expr, storage, gameBaseData);
}

} // namespace

ExecutionEngine::ExecutionEngine(VariableStorage* storage, GameBaseData* gameBaseData, QObject* parent)
    : QObject(parent), m_functionSystem(nullptr), m_storage(storage), m_gameBaseData(gameBaseData), m_running(false), m_currentLine(0), m_executionPosition(0), m_totalInstructionsExecuted(0) {
    connect(&m_erbLoader, &ErbLoader::objectNameChanged, this, &ExecutionEngine::objectNameChanged);

    // ParseTable reference (installed later via setParseTable)
    m_parseTable = nullptr;

    // Initialize function system
    m_functionSystem = new FunctionSystem(this);
}

ExecutionEngine::~ExecutionEngine() {
    if (m_functionSystem) {
        delete m_functionSystem;
        m_functionSystem = nullptr;
    }
}

void ExecutionEngine::setParseTable(EraParseTable* parseTable) {
    m_parseTable = parseTable;
}

bool ExecutionEngine::executeScript(const QString& scriptName) {
    // This function is kept for backward compatibility
    // The new signal/slot architecture handles execution
    // This function now just validates and returns true
    
    // Resolve script name (case-insensitive) to stored key
    const QString resolved = m_erbLoader.resolveScriptName(scriptName);
    if (resolved.isEmpty()) {
        setError("Script not loaded: " + scriptName);
        return false;
    }
    
    m_currentScript = resolved;
    m_running = true;
    m_executionPosition = 0;
    
    // Emit signal when execution starts (connects parsing system to state management)
    emit executionStarted(scriptName);
    
    qDebug() << "Script loaded (signal/slot architecture handles execution):" << scriptName;
    
    // Get cached logical lines from ErbLoader
    QList<LogicalLine> logicalLines = m_erbLoader.getLogicalLinesCI(scriptName);
    
    if (logicalLines.isEmpty()) {
        setError("No logical lines found for script: " + scriptName);
        m_running = false;
        emit executionFinished();
        return false;
    }
    
    qDebug() << "Execution will be driven by signal/slot chain for script:" << scriptName;
    
    // Note: Actual execution is now driven by ParseTable's executeInstruction signals
    // ExecutionEngine just receives and executes via receiveInstruction()
    
    return true;
}

void ExecutionEngine::executeLogicalLine(const LogicalLine& line) {
    // 完整 AST：一行即一个 LogicalLine。
    if (line.kind == LineKind::FunctionLabel || line.kind == LineKind::GotoLabel) {
        // 标签只是跳转目标，不执行
        return;
    }
    if (line.kind != LineKind::Instruction) {
        return;   // 空行 / 注释 / 预处理指令
    }

    qDebug() << "[executeLogicalLine]   Executing Instruction:" << line.functionName;
    if (!executeInstruction(line)) {
        qDebug() << "[executeLogicalLine]   executeInstruction returned false";
    }
}

// FOR loop control - handles FOR LOCAL, start, end
// NEXT loop control - handles NEXT variable
// LOOP control - handles LOOP [condition]
// 顶层 ':' 切分（尊重括号/引号嵌套）——`CFLAG:MAIN:10` / `BAG:(COUNT+1)` 两用
namespace {
QStringList splitTopLevelColon(const QString& text) {
    QStringList out;
    QString cur;
    int depth = 0;
    QChar quote;
    for (const QChar c : text) {
        if (!quote.isNull()) {
            cur += c;
            if (c == quote) quote = QChar();
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; cur += c; continue; }
        if (c == QLatin1Char('(') || c == QLatin1Char('[')) { ++depth; cur += c; continue; }
        if (c == QLatin1Char(')') || c == QLatin1Char(']')) { --depth; cur += c; continue; }
        if (c == QLatin1Char(':') && depth == 0) { out.append(cur); cur.clear(); continue; }
        cur += c;
    }
    out.append(cur);
    return out;
}
} // namespace

int ExecutionEngine::lhsDimension(const QString& name) const
{
    return m_expressionEvaluator ? m_expressionEvaluator->variableDimension(name) : 1;
}

// 解析 LHS：NAME / NAME:i / NAME:i:j / NAME:i:j:k
//   名字后面的每一段都可以是数值、表达式，或字符串常量名（CFLAG:MAIN:10）
ExecutionEngine::LhsRef ExecutionEngine::parseLhsRef(const QString& lhs)
{
    LhsRef ref;
    const QString trimmed = lhs.trimmed();
    if (trimmed.isEmpty()) {
        return ref;
    }
    const QStringList parts = splitTopLevelColon(trimmed);
    ref.name = parts.first().trimmed();
    if (ref.name.isEmpty()) {
        return ref;
    }
    ref.valid = true;
    if (parts.size() == 1) {
        return ref;                       // 无下标
    }
    for (int i = 1; i < parts.size(); ++i) {
        const QString idxText = parts.at(i).trimmed();
        bool ok = false;
        const int direct = idxText.toInt(&ok);
        if (ok) {
            ref.indices.append(direct);
            continue;
        }
        // CSV 常量名下标（MAXBASE:ARG:体力 / TALENT:MASTER:性別 之类）：
        // 必须先按「常量名」查 ConstantTable（与 ExpressionParser::parseIndexTerm
        // 的判定顺序一致），否则会被当普通表达式求值成 0，写错槽位。
        if (m_parseTable) {
            if (const ConstantTable* ct = m_parseTable->constantTable()) {
                const int mapped = ct->indexForVariable(ref.name, idxText);
                if (mapped >= 0) {
                    ref.indices.append(mapped);
                    continue;
                }
            } else {
                qWarning() << "[var] 常量名表为空：CSV 尚未装载，下标按表达式求值";
            }
        }
        // 变量下标 / 表达式（BAG:COUNT、BAG:(COUNT + 1)）
        qint64 value = 0;
        bool resolved = false;
        if (m_parseTable) {
            const QSharedPointer<ExpressionNode> ast = m_parseTable->expressionAst(idxText);
            if (ast) {
                ExpressionEvaluator localEvaluator;
                ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
                value = ev.evaluate(*ast, m_storage, m_gameBaseData).toLongLong();
                qCDebug(eraTrace) << "[var] 下标(表达式)" << ref.name << idxText << "->" << value;
                resolved = true;
            }
        }
        if (!resolved) {
            ref.valid = false;            // 无法解析：当作整串名字（宽松回退）
            ref.name = trimmed;
            ref.indices.clear();
            return ref;
        }
        ref.indices.append(static_cast<int>(value));
    }
    return ref;
}

QPair<QString, int> ExecutionEngine::parseLHS(const QString& lhs) {
    const LhsRef ref = parseLhsRef(lhs);
    if (!ref.hasIndex()) {
        return qMakePair(ref.name, -1);
    }
    return qMakePair(ref.name, ref.first());
}

// 读/写：路由与求值器一致（LOCAL/ARG -> 局部槽；系统变量 -> 系统槽；
// 否则按声明维度走 1D / 2D / 3D）
qint64 ExecutionEngine::readLhs(const LhsRef& ref) {
    if (!m_storage) return 0;
    if (m_storage->hasParameter(ref.name)) return m_storage->parameter(ref.name).toLongLong();
    // 角色数据变量：按 (角色号, 元素下标) 读取（与求值器一致）
    if (m_storage->isCharaDataVariable(ref.name)) {
        if (m_storage->isCharaDataString(ref.name)) return 0;   // 字符串：走 handleStringAssignment
        int charaId = 0;
        QList<int> elems;
        m_storage->reduceCharaArgs(ref.name, ref.indices, charaId, elems);
        if (m_storage->charaDataDimension(ref.name) >= 2)
            return m_storage->getCharaInt3D(ref.name, charaId, elems.value(0), elems.value(1));
        return m_storage->getCharaInt(ref.name, charaId, elems.value(0));
    }
    const QString upper = ref.name.toUpper();
    // ARG / LOCAL 是两套数组（见 variable_storage.h）
    if (upper == QLatin1String("ARG")) return m_storage->getArgInt(ref.first());
    if (upper == QLatin1String("LOCAL")) {
        return m_storage->getLocalInt(ref.first());
    }
    if (upper == QLatin1String("ARGS") || upper == QLatin1String("LOCALS")
        || upper == QLatin1String("RESULTS")) {
        return 0;                                   // 字符串变量：本函数只处理整数
    }
    if (m_storage->hasSystemVariable(ref.name)) {
        return m_storage->getSystemVariable(ref.name, ref.first());
    }
    const int dim = lhsDimension(ref.name);
    if (ref.indices.size() >= 2 && dim >= 2) {
        if (ref.indices.size() >= 3 && dim >= 3) {
            return m_storage->getGlobalInt3D(ref.name, ref.indices.at(0), ref.indices.at(1),
                                             ref.indices.at(2));
        }
        return m_storage->getGlobalInt2D(ref.name, ref.indices.at(0), ref.indices.at(1));
    }
    return m_storage->getGlobalInt1D(ref.name, ref.first());
}

void ExecutionEngine::writeLhs(const LhsRef& ref, qint64 value) {
    if (!m_storage) return;
    qCDebug(eraTrace) << "[var] write" << ref.name << ref.indices << "=" << value;
    if (m_storage->hasParameter(ref.name)) { m_storage->setParameter(ref.name, value); return; }
    // 角色数据变量：按 (角色号, 元素下标) 写入（否则同一角色的元素互相覆盖）
    if (m_storage->isCharaDataVariable(ref.name)) {
        if (m_storage->isCharaDataString(ref.name)) return;   // 字符串：走 handleStringAssignment
        int charaId = 0;
        QList<int> elems;
        m_storage->reduceCharaArgs(ref.name, ref.indices, charaId, elems);
        if (m_storage->charaDataDimension(ref.name) >= 2)
            m_storage->setCharaInt3D(ref.name, charaId, elems.value(0), elems.value(1), value);
        else
            m_storage->setCharaInt(ref.name, charaId, elems.value(0), value);
        return;
    }
    const QString upper = ref.name.toUpper();
    if (upper == QLatin1String("ARG")) {
        m_storage->setArgInt(ref.first(), value);
        return;
    }
    if (upper == QLatin1String("LOCAL")) {
        m_storage->setLocalInt(ref.first(), value);   // 函数局部槽（C# LOCAL）
        return;
    }
    if (upper == QLatin1String("ARGS") || upper == QLatin1String("LOCALS")
        || upper == QLatin1String("RESULTS")) {
        return;                                       // 字符串变量走 handleStringAssignment
    }
    if (m_storage->hasSystemVariable(ref.name)) {
        m_storage->setSystemVariable(ref.name, ref.first(), value);
        return;
    }
    const int dim = lhsDimension(ref.name);
    if (ref.indices.size() >= 2 && dim >= 2) {
        if (ref.indices.size() >= 3 && dim >= 3) {
            m_storage->setGlobalInt3D(ref.name, ref.indices.at(0), ref.indices.at(1),
                                      ref.indices.at(2), value);
            return;
        }
        m_storage->setGlobalInt2D(ref.name, ref.indices.at(0), ref.indices.at(1), value);
        return;
    }
    m_storage->setGlobalInt1D(ref.name, ref.first(), value);
}

bool ExecutionEngine::handleCompoundAssignment(const QString& lhs, const QString& op, const QString& rhs, const QSharedPointer<ExpressionNode>& ast) {
    // Parse the LHS to get variable name and index（支持 2D/3D 下标）
    const LhsRef ref = parseLhsRef(lhs);
    qint64 currentValue = readLhs(ref);
    
    // Evaluate the RHS expression (prefer cached AST)
    ExpressionEvaluator localEvaluator;
    ExpressionEvaluator& evaluator = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
    const QVariant rhsVar = ast ? evaluator.evaluate(*ast, m_storage, m_gameBaseData)
        : evalExpressionCached(m_parseTable, evaluator, rhs, m_storage, m_gameBaseData);
    qint64 rhsValue = rhsVar.isValid() ? rhsVar.toLongLong() : rhs.toLongLong();
    
    // Apply the operation
    if (op == "+=") {
        currentValue += rhsValue;
    } else if (op == "-=") {
        currentValue -= rhsValue;
    } else if (op == "*=") {
        currentValue *= rhsValue;
    } else if (op == "/=") {
        if (rhsValue != 0) {
            currentValue /= rhsValue;
        }
    }
    
    // Set the updated value
    writeLhs(ref, currentValue);
    return true;
}

// ===========================================================================
// VARSET 族
//
//   SET / VARSET / SETS / VAR_SET ：<可変変数>, <式>[, <範囲初値>, <範囲終値>]
//   CVARSET                       ：<角色変数>, <要素>[, <式>[, <範囲初値>, <範囲終値>]]
//
// 语义对齐 C# GameProc/Function/Instraction.Child.cs 的 VARSET_Instruction /
// CVARSET_Instruction 与 GameData/Variable/VariableEvaluator.cs 的
// SetValueAll / SetValueAllEachChara，以及各 VariableToken.SetValueAll：
//
//   * end 省略且目标是 **1 次元**数组 -> end = 该变量的长度（var.GetLength()）
//   * start > end 时**交换**（需求约定；C# 原码 `int t=start; start=end; end=start;`
//     实际会把区间收成空，这里按「交换」的可预期语义实现）
//   * 2 次元 / 3 次元数组的 SetValueAll **忽略范围**，整体赋值
//     （Int2D/3DVariableToken.SetValueAll 就是无脑双重/三重循环）
//   * 角色变量按 (角色号, 元素下标) 路由；**标量**角色变量只写一个槽
//   * 字符串 / 整数由**目标变量类型**决定右值的求值方式
// ===========================================================================
bool ExecutionEngine::isStringVariable(const QString& name, const QString& function) const {
    if (m_storage) {
        // 角色变量：NAME/CALLNAME/… 与用户 #DIMS CHARADATA
        if (m_storage->isCharaDataVariable(name)) return m_storage->isCharaDataString(name);
        const QString upper = name.toUpper();
        // 内建字符串变量/数组（与 handleStringAssignment 保持一致）
        if (upper == QLatin1String("RESULTS") || upper == QLatin1String("ARGS")
            || upper == QLatin1String("LOCALS") || upper == QLatin1String("SAVEDATA_TEXT")) {
            return true;
        }
    }
    if (m_parseTable) {
        const OperandType t = m_parseTable->variableTable().typeOf(name, function);
        if (t == OperandType::Str) return true;
        if (t == OperandType::Int) return false;
    }
    return false;
}

QList<int> ExecutionEngine::declaredLengths(const QString& name, const QString& function) const {
    if (m_parseTable) {
        if (const VariableDecl* d = m_parseTable->variableTable().find(name, function)) {
            return d->lengths;
        }
    }
    return {};
}

int ExecutionEngine::variableLength1D(const QString& name, const QString& function) const {
    const QString upper = name.toUpper();
    // 角色变量：每个角色的元素数（内建走 VariableSize.csv，用户走 #DIM CHARADATA 的维数）
    if (m_storage && m_storage->isCharaDataVariable(name)) {
        const int cfg = m_storage->variableConfig().getSize1D(upper);
        if (cfg > 0) return cfg;
        const QList<int> lens = declaredLengths(name, function);
        return lens.isEmpty() ? 1 : lens.first();
    }
    // LOCAL / ARG / LOCALS / ARGS：VariableSize.csv 指定（LOCAL=500 / ARG=200 / …）
    if (upper == QLatin1String("LOCAL") || upper == QLatin1String("ARG")
        || upper == QLatin1String("LOCALS") || upper == QLatin1String("ARGS")) {
        const int cfg = m_storage ? m_storage->variableConfig().getSize1D(upper) : 0;
        return cfg > 0 ? cfg : 0;
    }
    // 系统变量（RESULT / COUNT / …）：VariableSize.csv，其次当前容器尺寸
    if (m_storage && m_storage->hasSystemVariable(name)) {
        const int cfg = m_storage->variableConfig().getSize1D(upper);
        if (cfg > 0) return cfg;
        return m_storage->arraySize(name);
    }
    // 用户变量：#DIM 声明的长度与当前存储长度取较大者
    const QList<int> lens = declaredLengths(name, function);
    const int declared = lens.isEmpty() ? 1 : lens.first();
    const int stored = m_storage ? m_storage->arraySize(name) : 0;
    return qMax(declared, stored);
}

bool ExecutionEngine::handleVarSet(const LogicalLine& line, bool eachChara) {
    if (!m_storage) return true;

    // 参数（剔除分隔符）。VarSet 走 argument_parser 的 params；兜底用 line.arguments。
    QList<const Operand*> a;
    if (!line.argument.params.isEmpty()) {
        for (const Operand& o : line.argument.params) a.append(&o);
    } else {
        for (const Operand& o : line.arguments) {
            if (o.raw != QLatin1String(",") && o.raw != QLatin1String(":")) a.append(&o);
        }
    }
    if (a.isEmpty()) {
        qWarning() << "[varset] 参数为空，忽略:" << line.functionName;
        return true;
    }

    ExpressionEvaluator localEvaluator;
    ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
    const auto evalInt = [&](const Operand& o) -> qint64 {
        if (o.isString) return o.raw.toLongLong();
        if (o.ast) return ev.evaluate(*o.ast, m_storage, m_gameBaseData).toLongLong();
        return evalExpressionCached(m_parseTable, ev, o.raw, m_storage, m_gameBaseData).toLongLong();
    };
    const auto evalStr = [&](const Operand& o) -> QString {
        if (o.isString) return o.raw;
        if (o.ast) return ev.evaluate(*o.ast, m_storage, m_gameBaseData).toString();
        return evalExpressionCached(m_parseTable, ev, o.raw, m_storage, m_gameBaseData).toString();
    };

    const LhsRef ref = parseLhsRef(a[0]->raw.trimmed());
    if (ref.name.isEmpty()) {
        qWarning() << "[varset] 左值无法解析，忽略:" << a[0]->raw;
        return true;
    }
    const QString name = ref.name;
    const QString upper = name.toUpper();
    const QString fn = line.ownerFunction;
    const bool isString = isStringVariable(name, fn);
    const bool isChara = m_storage->isCharaDataVariable(name);

    // ---------------- CVARSET：对 [start,end) 内每个角色设置同一元素 ----------------
    if (eachChara) {
        // C# 要求一维角色变量（二维被 ArgumentBuilder 明确拒绝）
        if (!isChara || m_storage->charaDataDimension(name) != 1) {
            qWarning() << "[varset] CVARSET 需要一维角色变量，忽略:" << name;
            return true;
        }
        const int index = a.size() >= 2 ? static_cast<int>(evalInt(*a[1])) : 0;
        const qint64 value = a.size() >= 3 ? evalInt(*a[2]) : 0;
        const QString svalue = a.size() >= 3 ? evalStr(*a[2]) : QString();
        const int charaNum = m_storage->charaNum();
        int start = a.size() >= 4 ? static_cast<int>(evalInt(*a[3])) : 0;
        int end = a.size() >= 5 ? static_cast<int>(evalInt(*a[4])) : charaNum;
        if (start < 0 || start > charaNum) {
            qWarning() << "[varset] 命令CVARSET的第４引数(" << start << ")がキャラクタの範囲外です";
            return true;
        }
        if (end < 0 || end > charaNum) {
            qWarning() << "[varset] 命令CVARSET的第５引数(" << end << ")がキャラクタの範囲外です";
            return true;
        }
        if (start > end) std::swap(start, end);
        qDebug() << "[varset] CVARSET" << name << "元素" << index << "值"
                 << (isString ? svalue : QString::number(value))
                 << "角色区间[" << start << "," << end << ")";
        for (int cid = start; cid < end; ++cid) {
            if (isString) m_storage->setCharaStr(name, cid, index, svalue);
            else m_storage->setCharaInt(name, cid, index, value);
        }
        return true;
    }

    // ---------------- VARSET：一个变量的区间赋值 ----------------
    const qint64 value = a.size() >= 2 ? evalInt(*a[1]) : 0;
    const QString svalue = a.size() >= 2 ? evalStr(*a[1]) : QString();
    const bool hasStart = a.size() >= 3;
    const bool hasEnd = a.size() >= 4;
    int start = hasStart ? static_cast<int>(evalInt(*a[2])) : 0;
    int end = hasEnd ? static_cast<int>(evalInt(*a[3])) : -1;   // -1 = 未指定

    // ---- 角色变量 ----
    if (isChara) {
        const int cdim = m_storage->charaDataDimension(name);
        int charaId = 0;
        QList<int> elems;
        m_storage->reduceCharaArgs(name, ref.indices, charaId, elems);
        if (cdim == 0) {
            // 标量角色变量：忽略范围，只写一个槽
            qDebug() << "[varset] VARSET 标量角色变量" << name << "角色" << charaId;
            if (isString) m_storage->setCharaStr(name, charaId, 0, svalue);
            else m_storage->setCharaInt(name, charaId, 0, value);
            return true;
        }
        if (cdim >= 2) {
            // 二维角色数组：整体赋值（忽略范围）
            const QList<int> lens = declaredLengths(name, fn);
            const int n0 = lens.size() >= 1 && lens.at(0) > 0 ? lens.at(0) : 1;
            const int n1 = lens.size() >= 2 && lens.at(1) > 0 ? lens.at(1) : 1;
            qDebug() << "[varset] VARSET 二维角色数组" << name << "角色" << charaId
                     << "尺寸" << n0 << "x" << n1;
            for (int x = 0; x < n0; ++x)
                for (int y = 0; y < n1; ++y)
                    m_storage->setCharaInt3D(name, charaId, x, y, value);
            return true;
        }
        if (end < 0) end = variableLength1D(name, fn);
        if (start > end) std::swap(start, end);
        qDebug() << "[varset] VARSET 角色数组" << name << "角色" << charaId
                 << "区间[" << start << "," << end << ") 值"
                 << (isString ? svalue : QString::number(value));
        for (int i = start; i < end; ++i) {
            if (isString) m_storage->setCharaStr(name, charaId, i, svalue);
            else m_storage->setCharaInt(name, charaId, i, value);
        }
        return true;
    }

    // ---- 引用变量（参数别名）----
    if (m_storage->hasParameter(name)) {
        m_storage->setParameter(name, isString ? QVariant(svalue) : QVariant(value));
        return true;
    }

    // ---- LOCAL / LOCALS / ARG / ARGS ----
    if (upper == QLatin1String("LOCAL") || upper == QLatin1String("LOCALS")
        || upper == QLatin1String("ARG") || upper == QLatin1String("ARGS")) {
        const bool strSlot = (upper == QLatin1String("LOCALS") || upper == QLatin1String("ARGS"));
        if (end < 0) end = variableLength1D(name, fn);
        if (start > end) std::swap(start, end);
        if (end <= 0) return true;
        qDebug() << "[varset] VARSET" << upper << "区间[" << start << "," << end << ")";
        for (int i = start; i < end; ++i) {
            if (strSlot) {
                if (upper == QLatin1String("ARGS")) m_storage->setArgStr(i, svalue);
                else m_storage->setLocalStr(i, svalue);
            } else {
                if (upper == QLatin1String("ARG")) m_storage->setArgInt(i, value);
                else m_storage->setLocalInt(i, value);
            }
        }
        return true;
    }

    // ---- RESULTS：本移植放在 LOCAL 字符串槽 0（与字符串赋值路径一致）----
    if (upper == QLatin1String("RESULTS")) {
        m_storage->setLocalStr(0, svalue);
        return true;
    }

    // ---- 系统变量 ----
    if (m_storage->hasSystemVariable(name)) {
        if (end < 0) end = variableLength1D(name, fn);
        if (start > end) std::swap(start, end);
        qDebug() << "[varset] VARSET 系统变量" << name << "区间[" << start << "," << end << ")";
        for (int i = start; i < end; ++i) {
            if (isString) m_storage->setSystemStr(name, i, svalue);
            else m_storage->setSystemVariable(name, i, value);
        }
        return true;
    }

    // ---- 用户变量：按声明维数分派 ----
    const VariableDecl* decl = m_parseTable ? m_parseTable->variableTable().find(name, fn) : nullptr;
    const int dim = decl ? decl->dimension : 1;
    if (dim >= 2) {
        // 2/3 次元数组：整体赋值（忽略范围）
        const QList<int> lens = declaredLengths(name, fn);
        if (lens.size() < 2) {
            qWarning() << "[varset] 多维变量缺少维数声明，忽略:" << name;
            return true;
        }
        const int n0 = qMax(1, lens.value(0, 1));
        const int n1 = qMax(1, lens.value(1, 1));
        qDebug() << "[varset] VARSET 全局多维" << (isString ? "字符串" : "整数") << name
                 << "维数" << lens;
        if (dim >= 3) {
            const int n2 = qMax(1, lens.value(2, 1));
            for (int x = 0; x < n0; ++x)
                for (int y = 0; y < n1; ++y)
                    for (int z = 0; z < n2; ++z)
                        m_storage->setGlobalInt3D(name, x, y, z, value);
        } else {
            for (int x = 0; x < n0; ++x)
                for (int y = 0; y < n1; ++y) {
                    if (isString) m_storage->setGlobalStr2D(name, x, y, svalue);
                    else m_storage->setGlobalInt2D(name, x, y, value);
                }
        }
        return true;
    }

    // ---- 用户一维变量 ----
    if (end < 0) end = variableLength1D(name, fn);
    if (start > end) std::swap(start, end);
    qDebug() << "[varset] VARSET 全局" << (isString ? "字符串" : "整数") << name
             << "区间[" << start << "," << end << ") 值"
             << (isString ? svalue : QString::number(value));
    for (int i = start; i < end; ++i) {
        if (isString) m_storage->setGlobalStr1D(name, i, svalue);
        else m_storage->setGlobalInt1D(name, i, value);
    }
    return true;
}

bool ExecutionEngine::isRunning() const {
    return m_running;
}

int ExecutionEngine::getCurrentLine() const {
    return m_currentLine;
}

QString ExecutionEngine::getCurrentScript() const {
    return m_currentScript;
}

int ExecutionEngine::getTotalInstructionsExecuted() const {
    return m_totalInstructionsExecuted;
}

bool ExecutionEngine::loadScripts(const QString& scriptDir) {
    return m_erbLoader.loadDirectory(scriptDir, 0);
}

QHash<QString, QList<LogicalLine>> ExecutionEngine::getLoadedScripts() const {
    return m_erbLoader.getLoadedScripts();
}



bool ExecutionEngine::executeInstruction(const LogicalLine& line) {
    const QString& name = line.functionName;
    const QList<Operand>& args = line.arguments;
    m_totalInstructionsExecuted++;
    m_executionPosition++;

    // 只处理「非控制流」指令：控制流（IF/SIF/REPEAT/LOOP/WHILE/WEND/FOR/NEXT/
    // GOTO/CALL/RETURN/BEGIN/INPUT/…）已由 ScriptRunner 依据拍平 AST + 标记区执行。
    if (name == "RESETDATA") {
        return handleResetData();
    }
    if (name == "LOADGLOBAL") {
        return handleLoadGlobal();
    }
    if (name == "SAVEGLOBAL") {
        return handleSaveGlobal();
    }

    // ---- 角色列表：ADDCHARA / DELCHARA（对齐 C# ADDCHARA_Instruction）----
    // 形如 `ADDCHARA 0` / `ADDCHARA LOCAL` / `DELCHARA CHARANUM - 1`：
    // 参数是**表达式**，必须求值而不是取字面量。
    if (name == "ADDCHARA" || name == "DELCHARA") {
        if (!m_storage) return true;
        qint64 value = 0;
        if (!args.isEmpty() && !args.first().raw.trimmed().isEmpty()) {
            ExpressionEvaluator localEvaluator;
            ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
            value = evalExpressionCached(m_parseTable, ev, args.first().raw,
                                         m_storage, m_gameBaseData).toLongLong();
        }
        if (name == "ADDCHARA") {
            if (value < 0) {
                emit errorOccurred(QStringLiteral("ADDCHARA 的角色番号无效: %1").arg(value));
                return true;
            }
            m_storage->addChara(static_cast<int>(value));
        } else if (!m_storage->delChara(static_cast<int>(value))) {
            emit errorOccurred(QStringLiteral("DELCHARA 的番号超出角色范围: %1").arg(value));
        }
        return true;
    }

    // ---- SETBIT / CLEARBIT / INVERTBIT（对齐 C# SETBIT_Instruction）----
    //   SETBIT  <变量>[, <位0-63>]…   置位
    //   CLEARBIT<变量>[, <位0-63>]…   清位
    //   INVERTBIT<变量>[, <位0-63>]…  取反
    // eraTW 用 `SETBIT CFLAG:C_ID:口上実装状況, 口上カウント` 记录口上实现状况，
    // 以前没有实现 -> 那些标志位永远是 0。
    if (name == QLatin1String("SETBIT") || name == QLatin1String("CLEARBIT")
        || name == QLatin1String("INVERTBIT")) {
        if (!m_storage) return true;
        QList<const Operand*> ops;
        if (line.argument.kind == ArgKind::Bit && !line.argument.params.isEmpty()) {
            for (const Operand& a : line.argument.params) ops.append(&a);
        } else {
            for (const Operand& a : line.arguments) {
                if (a.raw != QLatin1String(",")) ops.append(&a);
            }
        }
        if (ops.isEmpty()) return true;
        const LhsRef ref = parseLhsRef(ops.first()->raw);
        if (!ref.valid) return true;
        ExpressionEvaluator localEvaluator;
        ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
        qint64 bits = readLhs(ref);
        for (int i = 1; i < ops.size(); ++i) {
            const qint64 x = ops.at(i)->ast
                ? ev.evaluate(*ops.at(i)->ast, m_storage, m_gameBaseData).toLongLong()
                : ops.at(i)->raw.toLongLong();
            if (x < 0 || x > 63) {
                emit errorOccurred(QStringLiteral("SETBIT 的第 %1 引数超出位范围(0..63)：%2")
                                       .arg(i + 1).arg(x));
                continue;
            }
            const qint64 shift = static_cast<qint64>(1) << static_cast<int>(x);
            if (name == QLatin1String("SETBIT")) bits |= shift;
            else if (name == QLatin1String("CLEARBIT")) bits &= ~shift;
            else bits ^= shift;
        }
        writeLhs(ref, bits);
        return true;
    }

    // ---- 命令式字符串内置函数 ----
    // ERB 既支持 STRLENS(VERSION) 表达式，也支持
    //   STRLENS VERSION
    //   SUBSTRING VERSION, RESULT - 3, 3
    // 后一种形式由 AST 保留为普通指令，必须在执行阶段把结果写回
    // RESULT / RESULTS；否则 TITLE.ERB 的版本号会退化成 0.0。
    if (name == "STRLENS" || name == "STRLENSU" || name == "SUBSTRING"
        || name == "SUBSTRINGU") {
        ExpressionEvaluator localEvaluator;
        ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
        QString callText = line.raw;
        const int commandEnd = callText.indexOf(QRegularExpression(QStringLiteral("\\s")));
        if (commandEnd >= 0) callText = callText.mid(commandEnd).trimmed();
        if (callText.isEmpty()) {
            QStringList callArgs;
            for (const Operand& arg : args) {
                if (arg.raw != QLatin1String(",")) callArgs << arg.raw;
            }
            callText = callArgs.join(QLatin1Char(','));
        }
        // line.raw 保留命令后的空白、逗号和运算符：
        // SUBSTRING VERSION, RESULT - 3, 3 -> SUBSTRING(VERSION, RESULT - 3, 3)
        const QString expr = name + QLatin1Char('(') + callText + QLatin1Char(')');
        QVariant value;
        if (name == QLatin1String("SUBSTRING") || name == QLatin1String("SUBSTRINGU")) {
            const QStringList pieces = callText.split(QLatin1Char(','), Qt::KeepEmptyParts);
            if (pieces.size() >= 3) {
                const QString sourceName = pieces.at(0).trimmed();
                QString source;
                if (sourceName.compare(QLatin1String("RESULTS"), Qt::CaseInsensitive) == 0) {
                    source = m_storage ? m_storage->getLocalStr(0) : QString();
                } else {
                    const QPair<QString, int> sourceRef = parseLHS(sourceName);
                    if (m_storage && sourceRef.first == sourceName && sourceRef.first.size() > 0) {
                        source = m_storage->getGlobalStr1D(sourceRef.first,
                                                           sourceRef.second >= 0 ? sourceRef.second : 0);
                    } else {
                        source = ev.evaluate(sourceName, m_storage, m_gameBaseData).toString();
                    }
                }
                const qint64 start = ev.evaluate(pieces.at(1).trimmed(), m_storage,
                                                 m_gameBaseData).toLongLong();
                const qint64 length = ev.evaluate(pieces.at(2).trimmed(), m_storage,
                                                  m_gameBaseData).toLongLong();
                QString escaped = source;
                escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
                escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
                if (start < 0) {
                    // Emuera 的版本字符串按四位小数部分处理：2 -> 0002。
                    // 负位置从该四位字符串末尾计算。
                    source = QStringLiteral("0000").right(4 - qMin(4, source.size())) + source;
                }
                const int safeStart = start < 0
                                           ? qMax(0, source.size() + static_cast<int>(start))
                                           : static_cast<int>(start);
                value = source.mid(safeStart, length < 0 ? -1 : static_cast<int>(length));
            }
        } else {
            value = ev.evaluate(expr, m_storage, m_gameBaseData);
        }
        if (name == QLatin1String("STRLENS") || name == QLatin1String("STRLENSU")) {
            if (m_storage) m_storage->setSystemVariable(QStringLiteral("RESULT"), 0,
                                                          value.toLongLong());
        } else if (m_storage) {
            m_storage->setLocalStr(0, value.toString());
        }
        return true;
    }

    if (name == QLatin1String("HTML_PRINT")) {
        ExpressionEvaluator fallback;
        ExpressionEvaluator& evaluator = m_expressionEvaluator ? *m_expressionEvaluator : fallback;
        const auto eval = [&](const ExpressionNode& node) {
            return evaluator.evaluate(node, m_storage, m_gameBaseData).toString();
        };
        auto compiled = line.printTemplate;
        if (!compiled && !args.isEmpty()) {
            const auto& arg = args.first();
            compiled = PrintTemplateCompiler::compile(arg.ast ? eval(*arg.ast) : arg.raw);
        }
        if (compiled) emit consolePrintTemplate(PrintTemplateCompiler::evaluate(*compiled, eval));
        return true;
    }

    // ---- 输出（PRINT 族，对齐 C# PRINT_Instruction.DoInstruction）----
    // 形态由后缀决定：PRINT / PRINTL / PRINTS / PRINTFORM / PRINTFORML /
    // PRINTFORMS / PRINTV / PRINTC / PRINTLC / PRINTPLAIN* …
    if (AstBuilder::isPrintFamily(name)) {
        return handlePrintInstruction(line);
    }
    if (name == QLatin1String("PRINT_IMG")) {
        if (args.isEmpty()) return true;
        ExpressionEvaluator localEvaluator;
        ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
        const Operand& operand = args.first();
        const QVariant value = operand.isString
            ? QVariant(operand.raw)
            : (operand.ast ? ev.evaluate(*operand.ast, m_storage, m_gameBaseData)
                           : evalExpressionCached(m_parseTable, ev, operand.raw,
                                                   m_storage, m_gameBaseData));
        emit consolePrintImage(value.toString(), 0, 0, 0);
        return true;
    }
    if (name == "PRINTBUTTON") {
        // PRINTBUTTON <文本>, <整数|字符串>：打印文本并把它变成按钮（对齐 C# PRINTBUTTON）
        // 注意：line.arguments 里逗号也是一个操作数，需要过滤掉
        QList<const Operand*> ops;
        if (line.argument.kind == ArgKind::Button && line.argument.params.size() >= 2) {
            for (const Operand& a : line.argument.params) ops.append(&a);
        } else {
            for (const Operand& a : args) {
                if (a.raw != QLatin1String(",")) ops.append(&a);
            }
        }
        if (ops.size() >= 2) {
            ExpressionEvaluator localEvaluator;
            ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
            // AstBuilder 已把带引号的字面量标记为 isString 并去掉外层引号；
            // 再按 raw 解析会把 "123" 错当整数、把 "A" 错当变量。
            const auto evaluateOperand = [&](const Operand& operand) -> QVariant {
                if (operand.isString) return QVariant(operand.raw);
                if (operand.ast) return ev.evaluate(*operand.ast, m_storage, m_gameBaseData);
                return evalExpressionCached(m_parseTable, ev, operand.raw,
                                            m_storage, m_gameBaseData);
            };
            const QVariant textValue = evaluateOperand(*ops[0]);
            QString text = textValue.toString();
            if (text.isEmpty() && !ops[0]->isString) text = ops[0]->raw.trimmed();
            const QVariant value = evaluateOperand(*ops[1]);
            const bool valueIsString = ops[1]->isString
                                       || (ops[1]->ast && ops[1]->ast->valueType() == OperandType::Str);
            emit consolePrintButton(text, value.toLongLong(), value.toString(), valueIsString);
        }
        return true;
    }
    if (name == "CLEARLINE") {
        // 参数是**表达式**（如 `CLEARLINE LINECOUNT - FIRSTLINE`），不能按字面量解析
        int n = 1;
        if (!args.isEmpty() && !args.first().raw.trimmed().isEmpty()) {
            ExpressionEvaluator localEvaluator;
            ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
            n = static_cast<int>(evalExpressionCached(m_parseTable, ev, args.first().raw,
                                                      m_storage, m_gameBaseData).toLongLong());
        }
        if (n > 0) {
            emit consoleClearLines(n);
        }
        return true;
    }
    if (name == "REDRAW") {
        emit consoleRedraw(args.size() >= 1 ? args[0].raw : QStringLiteral("1"));
        return true;
    }
    // 后缀自增/自减语句：`I++` / `BAG:COUNT--`
    if (name == "++" || name == "--") {
        if (args.isEmpty()) return true;
        const LhsRef ref = parseLhsRef(args.first().raw);
        if (!ref.valid) return true;
        const qint64 v = readLhs(ref);
        writeLhs(ref, v + (name == "++" ? 1 : -1));
        return true;
    }

    if (name == "RANDOMIZE") {
        // 对齐 C# RANDOMIZE_Instruction：Randomize(seed) —— 之后随机序列可复现
        qint64 seed = 0;
        if (!args.isEmpty() && m_expressionEvaluator) {
            seed = evalExpressionCached(m_parseTable, *m_expressionEvaluator, args.first().raw,
                                        m_storage, m_gameBaseData).toLongLong();
        }
        if (m_expressionEvaluator) m_expressionEvaluator->setRandomSeed(static_cast<quint32>(seed));
        qDebug() << "[var] RANDOMIZE 固定种子" << seed;
        return true;
    }

    if (name == "RESETCOLOR") {
        m_colorValue = kDefaultColor;
        emit consoleResetColor();
        return true;
    }
    if (name == "SETCOLOR") {
        // 参数是**表达式**：`SETCOLOR 0x70C070` / `SETCOLOR C_YELLOW` /
        // `SETCOLOR 現在指定の色`（eraTW 的 COLORMESSAGE 就靠后者还原颜色）。
        // 之前直接透传 raw 文本，变量与函数形式都会失效。
        if (!args.isEmpty() && !args.first().raw.trimmed().isEmpty()) {
            const QString raw = args.first().raw.trimmed();
            ExpressionEvaluator localEvaluator;
            ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
            const QVariant value = evalExpressionCached(m_parseTable, ev, raw,
                                                        m_storage, m_gameBaseData);
            if (value.typeId() == QMetaType::QString) {
                // 颜色名（含 C_* 等常量与 @"..." 形式）
                const QString name = value.toString().trimmed();
                m_colorValue = colorValueOf(name);
                emit consoleColor(name);
            } else {
                // 整数：0xRRGGBB（GETCOLOR 的返回值走这条路）
                const qint64 v = value.toLongLong();
                m_colorValue = v;
                emit consoleColor(QStringLiteral("0x%1").arg(v & 0xFFFFFF, 6, 16, QLatin1Char('0')));
            }
        }
        return true;
    }

    // ---- 字体样式（FONTBOLD/FONTITALIC/FONTUNDERLINE/FONTSTRIKE/FONTREGULAR/FONTSTYLE）----
    // GETSTYLE/FONTSTYLE 成对使用（eraTW 的 COLORMESSAGE 保存并还原样式）。
    if (name.startsWith(QLatin1String("FONT"))) {
        if (name == QLatin1String("FONTREGULAR")) {
            m_styleBits = 0;
        } else if (name == QLatin1String("FONTSTYLE")) {
            // FONTSTYLE <位掩码>：整体替换
            if (!args.isEmpty() && !args.first().raw.trimmed().isEmpty()) {
                ExpressionEvaluator localEvaluator;
                ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
                m_styleBits = evalExpressionCached(m_parseTable, ev, args.first().raw,
                                                   m_storage, m_gameBaseData).toLongLong();
            }
        } else {
            const qint64 bit = (name == QLatin1String("FONTBOLD")) ? 1
                             : (name == QLatin1String("FONTITALIC")) ? 2
                             : (name == QLatin1String("FONTSTRIKE")) ? 4
                             : (name == QLatin1String("FONTUNDERLINE")) ? 8 : 0;
            if (bit == 0) return true;          // FONTNAME/FONTSIZE 等：暂不处理字体族/字号
            // 无参数 = 置位；有参数且求值为 0 = 清除
            bool on = true;
            if (!args.isEmpty() && !args.first().raw.trimmed().isEmpty()) {
                ExpressionEvaluator localEvaluator;
                ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
                on = evalExpressionCached(m_parseTable, ev, args.first().raw,
                                          m_storage, m_gameBaseData).toLongLong() != 0;
            }
            m_styleBits = on ? (m_styleBits | bit) : (m_styleBits & ~bit);
        }
        emit consoleFontStyle(m_styleBits & 1, m_styleBits & 2, m_styleBits & 8, m_styleBits & 4);
        return true;
    }
    if (name == "ALIGNMENT") {
        emit consoleAlign(args.size() >= 1 ? args[0].raw : QStringLiteral("LEFT"));
        return true;
    }
    if (name == "DRAWLINE" || name == "CUSTOMDRAWLINE" || name == "DRAWLINEFORM") {
        // 对齐 C# Process.ScriptProc：DRAWLINE = PrintBar()（用 Config.DrawLineString
        // 循环拼到 DrawableWidth 再裁回）+ NewLine()；
        //   CUSTOMDRAWLINE = printCustomBar(<字面文字>) —— 参数是**字面**文字而非表达式
        //     （eraTW 写 `CUSTOMDRAWLINE ━`；按表达式求值会得到 0，
        //      表现为整条分隔线变成 "0000…"，正是标题画面的那串 0）
        //   DRAWLINEFORM   = 格式串（文本 + %…%/{…}）
        QString barStr = m_drawLineString;               // 默认 "-"（半角）
        if (name != QLatin1String("DRAWLINE")) {
            if (args.isEmpty()) return true;
            if (name == QLatin1String("CUSTOMDRAWLINE")) {
                barStr = args.first().raw;
            } else {
                ExpressionEvaluator localEvaluator;
                ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
                barStr = evalExpressionCached(m_parseTable, ev, args.first().raw,
                                              m_storage, m_gameBaseData).toString();
            }
            if (barStr.isEmpty()) {
                emit errorOccurred(QStringLiteral("空文字列によるDRAWLINEが行われました"));
                return true;
            }
        }
        QString bar;
        int units = 0;
        while (units < m_maxLineUnits) {                 // 越过边界再逐字回裁
            bar += barStr;
            units += unitWidth(barStr);
        }
        while (units > m_maxLineUnits && !bar.isEmpty()) {
            bar.chop(1);
            units = unitWidth(bar);
        }
        emit consolePrint(bar, true);
        return true;
    }

    // ---- 赋值 ----
    if (name == "=") {
        if (args.size() >= 2) {
            // 目的变量是字符串 -> 字符串赋值（此前只有整数路径）。
            // 目的类型从**变量表**取（LHS 操作数在 AST 里不带类型节点）。
            // Type lookup must not evaluate indexed LHS expressions (e.g. A:I++).
            const QString lhsName = splitTopLevelColon(args[0].raw).first().trimmed();
            const OperandType destType =
                m_parseTable ? m_parseTable->variableTable().typeOf(lhsName, line.ownerFunction)
                             : OperandType::Unknown;
            if (destType == OperandType::Str) {
                return handleStringAssignment(args[0].raw, args[1].raw, args[1].ast);
            }
            return handleAssignment(args[0].raw, args[1].raw, args[1].ast);
        }
        return true;
    }
    // ---- VARSET 族（对齐 C# FunctionCode.VARSET / CVARSET）----
    //   VARSET 名[, 值[, 初値[, 終値]]]：把变量的元素区间整体赋值
    //   CVARSET 角色变量, 下标[, 值[, 初値[, 終値]]]：逐角色设置同一元素
    // 以前 ArgKind::VarSet 只在 argument_parser 里登记、执行期没有任何分支，
    // 于是 `PRINT_STATE.ERB:336 VARSET TLNT_CNT` 之类的清空被**静默跳过**，
    // 计数器不清零 -> 素質/性的特徴 列表越叠越长。
    if (name == QLatin1String("VARSET") || name == QLatin1String("SETS")
        || name == QLatin1String("VAR_SET") || name == QLatin1String("SET")) {
        return handleVarSet(line, false);
    }
    if (name == QLatin1String("CVARSET")) {
        return handleVarSet(line, true);
    }

    // `'=` —— **字符串专用**赋值运算符（C# OperatorCode.AssignmentStr「単一代入」）。
    // 左值必须是字符串变量；右值按**普通表达式**求值（不做 %..%/{..} 的格式化展开，
    // 与 `=` 的字符串分支不同，见 EraParseTable::applyStringAssignments）。
    // 以前这里没有分支：`X '= Y` 会落到「未知指令静默跳过」，于是
    //   * eraTW 的改名（NAME:ARG '= RESULTS / CALLNAME:ARG '= RESULTS）点了不生效；
    //   * 函数内静态字符串（#DIMS html）不再被重置，重绘时越接越长。
    if (name == QLatin1String("'=")) {
        if (args.size() >= 2) {
            const QString lhsName = splitTopLevelColon(args[0].raw).first().trimmed();
            const OperandType destType =
                m_parseTable ? m_parseTable->variableTable().typeOf(lhsName, line.ownerFunction)
                             : OperandType::Unknown;
            if (destType == OperandType::Int) {
                // C# 同样拒绝：整数型变量没有 '= 语义
                qWarning() << "[exec] 整数型变量不能使用 '= 赋值，已忽略：" << lhsName;
                return true;
            }
            return handleStringAssignment(args[0].raw, args[1].raw, args[1].ast);
        }
        return true;
    }
    if (name == "+=") {
        // 字符串累加（`A += B` -> A = A + B）
        if (args.size() >= 2) {
            // Type lookup must not evaluate indexed LHS expressions (e.g. A:I++).
            const QString lhsName = splitTopLevelColon(args[0].raw).first().trimmed();
            const OperandType destType =
                m_parseTable ? m_parseTable->variableTable().typeOf(lhsName, line.ownerFunction)
                             : OperandType::Unknown;
            if (destType == OperandType::Str) {
                return handleStringAssignment(args[0].raw,
                                              args[0].raw + QLatin1String(" + ") + args[1].raw);
            }
        }
    }
    if (name == "+=" || name == "-=" || name == "*=" || name == "/=") {
        if (args.size() >= 2) {
            return handleCompoundAssignment(args[0].raw, name, args[1].raw, args[1].ast);
        }
        return true;
    }
    if (name == "+" || name == "-" || name == "*" || name == "/" || name == "%") {
        // A + 1 等价 A = A + 1
        if (args.size() >= 2) {
            const QString lhs = args[0].raw;
            return handleAssignment(lhs, lhs + " " + name + " " + args[1].raw);
        }
        return true;
    }

    // ---- 语句形式的内部函数（对齐 C# METHOD_Instruction）----
    //   GETMILLISECOND / GETTIME / GETCOLOR / CURRENTREDRAW / GETBIT …
    //   REPLACE LOCALS, "a", "b" / TWAIT 2500, 0 / SUBSTRING RESULTS:0, 0, 3 …
    // 整行是一次函数调用，返回值为整型时写 RESULT、为字符串时写 RESULTS:0。
    if (line.isFunctionCall) {
        return executeFunctionCall(line);
    }

    // 其它指令：由显示/子系统各自处理。
    // 「未完成」——尚未实现的指令在这里明确留痕（同一名字只报一次），
    // 这样跑 eraTW 时从 stderr 就能看出还有哪些接口没接线。
    // DEBUGPRINT 族：仅在调试模式输出（C# DEBUGPRINT_Instruction 检查
    // debugMode），非调试运行期静默忽略，不算未实现。
    if (name.startsWith(QLatin1String("DEBUGPRINT"))) {
        return true;
    }
    reportUnfinished(QStringLiteral("指令"), name, line);
    return true;
}

// ---------------------------------------------------------------------------
// 语句形式的内部函数（对齐 C# METHOD_Instruction.DoInstruction）
//   if (term.GetOperandType() == typeof(Int64)) RESULT = term.GetIntValue();
//   else                                         RESULTS = term.GetStrValue();
// ---------------------------------------------------------------------------
bool ExecutionEngine::executeFunctionCall(const LogicalLine& line)
{
    if (!m_storage) return true;
    const QString& name = line.functionName;
    const QString upper = name.toUpper();

    // 先在实例里去重登记：即便求值器自己也报过，这里给出**行号 + 原文**，
    // 便于直接从运行日志定位是哪个脚本的哪一行没实现。
    const bool unfinished = !line.arguments.isEmpty() && !line.arguments.first().ast;

    QVariant value;
    if (unfinished) {
        // 表达式没归约出来（语法不认识 / 参数形态特殊）：明确留痕并跳过。
        reportUnfinished(QStringLiteral("函数语句（实参无法归约）"), name, line);
        return true;
    }
    ExpressionEvaluator fallback;
    ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : fallback;
    value = ev.evaluate(*line.arguments.first().ast, m_storage, m_gameBaseData);

    // 返回类型决定写哪个寄存器（对齐 C#：Int64 -> RESULT，string -> RESULTS）
    const OperandType ret = builtinFunctionReturnType(upper.toStdString());
    const bool returnsStr = (ret == OperandType::Str)
                            || (ret == OperandType::Unknown && value.typeId() == QMetaType::QString);
    if (returnsStr) {
        m_storage->setLocalStr(0, value.toString());          // RESULTS:0
    } else {
        m_storage->setSystemVariable(QStringLiteral("RESULT"), 0, value.toLongLong());
    }
    qDebug() << "[funcstmt]" << upper << "->" << (returnsStr ? "RESULTS" : "RESULT")
             << value << "行" << line.position.toString();
    return true;
}

// 未实现接口的运行期留痕（同名只报一次，避免刷屏）
void ExecutionEngine::reportUnfinished(const QString& what, const QString& name,
                                       const LogicalLine& line)
{
    if (m_reportedUnfinished.contains(name)) return;
    m_reportedUnfinished.insert(name);
    qWarning() << "[未完成]" << what << name
               << "在运行期被忽略。行:" << line.position.toString()
               << "原文:" << line.raw.left(100);
}

bool ExecutionEngine::handleStringAssignment(const QString& lhs, const QString& rhs, const QSharedPointer<ExpressionNode>& ast) {
    const LhsRef ref = parseLhsRef(lhs);
    const QString varName = ref.name;
    const int index = ref.hasIndex() ? ref.first() : -1;
    if (varName.isEmpty()) {
        return false;
    }
    ExpressionEvaluator localEvaluator;
    ExpressionEvaluator& evaluator = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;

    // 装载期为「字符串赋值的右值」统一建了格式化串节点（对齐 C# AnalyseFormattedString），
    // 但右值是**裸变量名**（如 `NAME = RESULTS`）时它会被当成字面量文本。
    // 这类情况按普通表达式求值（变量引用）。
    QString value;
    const QString trimmed = rhs.trimmed();
    bool bareIdent = !trimmed.isEmpty()
                     && (trimmed.at(0).isLetter() || trimmed.at(0) == QLatin1Char('_'));
    for (int i = 0; bareIdent && i < trimmed.size(); ++i) {
        const QChar c = trimmed.at(i);
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char(':')
              || c == QLatin1Char('.'))) {
            bareIdent = false;
        }
    }
    bool evaluated = false;
    if (!ast && bareIdent && m_parseTable) {
        const QSharedPointer<ExpressionNode> ast = m_parseTable->expressionAst(trimmed);
        if (ast) {
            value = evaluator.evaluate(*ast, m_storage, m_gameBaseData).toString();
            evaluated = true;
        }
    }
    if (!evaluated) {
        const QVariant rhsValue = ast ? evaluator.evaluate(*ast, m_storage, m_gameBaseData)
        : evalExpressionCached(m_parseTable, evaluator, rhs, m_storage, m_gameBaseData);
        value = rhsValue.toString();
    }

    if (m_storage->hasParameter(varName)) {
        m_storage->setParameter(varName, value);
        return true;
    }
    const QString upper = varName.toUpper();
    if (upper == QLatin1String("RESULTS")) {
        m_storage->setLocalStr(0, value);
        return true;
    }
    // ARGS（实参字符串数组）与 LOCALS（局部字符串数组）分离，同 ARG/LOCAL
    if (upper == QLatin1String("ARGS")) {
        m_storage->setArgStr(index >= 0 ? index : 0, value);
        qCDebug(eraTrace) << "[var] str-write ARGS" << (index >= 0 ? index : 0) << "=" << value;
        return true;
    }
    if (upper == QLatin1String("LOCALS")) {
        m_storage->setLocalStr(index >= 0 ? index : 0, value);
        qCDebug(eraTrace) << "[var] str-write LOCALS" << (index >= 0 ? index : 0) << "=" << value;
        return true;
    }
    if (upper == QLatin1String("SAVEDATA_TEXT")) {
        m_storage->setSystemStr(upper, 0, value);
        return true;
    }
    // 角色字符串变量（NAME/CSTR/用户 #DIMS CHARADATA）：按 (角色号, 下标) 写入
    if (m_storage->isCharaDataString(varName)) {
        int charaId = 0;
        QList<int> elems;
        m_storage->reduceCharaArgs(varName, ref.indices, charaId, elems);
        m_storage->setCharaStr(varName, charaId, elems.value(0), value);
        return true;
    }
    m_storage->setGlobalStr1D(varName, index >= 0 ? index : 0, value);
    return true;
}

bool ExecutionEngine::handleAssignment(const QString& lhs, const QString& rhs, const QSharedPointer<ExpressionNode>& ast) {

    // Parse the LHS to get variable name and index（支持 2D/3D 下标）
    const LhsRef ref = parseLhsRef(lhs);

    // Evaluate the RHS expression using the parse table's cached AST.
    // Pass m_gameBaseData if available so GameBase variables can be resolved.
    ExpressionEvaluator localEvaluator;
    ExpressionEvaluator& evaluator = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
    QVariant rhsValue = ast ? evaluator.evaluate(*ast, m_storage, m_gameBaseData)
        : evalExpressionCached(m_parseTable, evaluator, rhs, m_storage, m_gameBaseData);
    
    
    // If the evaluated result is not valid, try direct conversion
    if (!rhsValue.isValid() || rhsValue.toString().isEmpty()) {
        bool ok = false;
        int intValue = rhs.toInt(&ok);
        if (ok) {
            rhsValue = QVariant(intValue);
        }
    }
    
    
    writeLhs(ref, rhsValue.toLongLong());
    return true;
}

// PRINT 族统一实现（对齐 C# GameProc/Function/Instraction.Child.cs PRINT_Instruction）
//
//   mode == Literal        ：操作数就是「命令名之后到行尾的原文」（含尾随空白！）
//   mode == PrintV         ：多个值直接拼接（不补分隔符）
//   mode == StrExpression  ：求一个字符串表达式
//   mode == FormStr        ：求一个 StrForm（文本 + {…}/%…%），装载期已建好节点
//   forms  （FORMS 后缀）  ：求值结果再当格式串展开一次（C# isForms）
//   clearPad（C/LC 后缀）  ：按 PRINTC 的定宽列布局补齐后打印
//   newline L/W            ：打印后换行（W 还要等一次按键）
bool ExecutionEngine::handlePrintInstruction(const LogicalLine& line) {
    const QString& name = line.functionName;
    const QList<Operand>& args = line.arguments;
    const AstBuilder::PrintArgInfo info = AstBuilder::printInfo(name);
    if (info.mode == AstBuilder::PrintArgMode::NotPrint) {
        return false;
    }

    ExpressionEvaluator localEvaluator;
    ExpressionEvaluator& evaluator = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;

    const auto valueOf = [&](const Operand& a) -> QString {
        if (a.ast) {
            return evaluator.evaluate(*a.ast, m_storage, m_gameBaseData).toString();
        }
        return a.raw;
    };

    QString text;
    // 普通打印变量直接来自 Operand.ast；模板片段若已生成则优先按片段求值。
    if (line.printTemplate && !line.printTemplate->parts.isEmpty()) {
        for (const PrintTemplatePart& part : line.printTemplate->parts) {
            if (part.kind == PrintTemplatePart::Kind::Text) text += part.text;
            else if (part.kind == PrintTemplatePart::Kind::Expression && part.expression)
                text += evaluator.evaluate(*part.expression, m_storage, m_gameBaseData).toString();
        }
    } else switch (info.mode) {
    case AstBuilder::PrintArgMode::PrintV:
        for (const Operand& a : args) text += valueOf(a);
        break;
    case AstBuilder::PrintArgMode::Literal:
        if (!args.isEmpty()) text = args.first().raw;
        break;
    default:   // StrExpression / FormStr
        if (!args.isEmpty()) text = valueOf(args.first());
        break;
    }

    // FORMS：先把字符串求出来，再把它当格式串展开（C# isForms 分支）
    if (info.forms && m_parseTable && !text.isEmpty()) {
        const StrFormParser::ExprResolver resolve =
            [this](const QString& e) { return m_parseTable->expressionAst(e); };
        if (const QSharedPointer<StrFormNode> node = StrFormParser::parse(text, resolve)) {
            text = evaluator.evaluate(*node, m_storage, m_gameBaseData).toString();
        }
    }

    if (info.clearPad) {
        emit consolePrint(padPrintC(text, info.padLeft), false);
    } else {
        emit consolePrint(text, info.newline);
    }
    // W 后缀（PRINTW / PRINTFORMW / …）：换行后再等一次任意键（C# PRINT_WAITINPUT）
    if (info.waitInput) {
        emit requestAnyKey();
    }
    return true;
}

// PRINTC / PRINTLC：按 PRINTCLENGTH 定宽补齐（对齐 C# CreateTypeCString）
//
// C# 用「当前字体下 ' ' 的显示宽度」做上界裁剪，这里用等宽近似：
// 全角 = 2 列、半角 = 1 列。补空格后若超出定宽则从补出来的那一侧裁掉。
QString ExecutionEngine::padPrintC(const QString& text, bool padLeft) const {
    const int width = printCWidth(text);       // 当前文本占的列数
    const int target = padLeft ? m_printCLength : m_printCLength + 1;
    if (width >= target) {
        return text;
    }
    const QString pad(int(target - width), QLatin1Char(' '));
    return padLeft ? (pad + text) : (text + pad);
}

// 文本占几个半角单位（全角 2 / 半角 1）——与 ConsoleLayout / ConsolePlane 同一口径
int ExecutionEngine::unitWidth(const QString& text) {
    int units = 0;
    for (const QChar c : text) {
        const ushort u = c.unicode();
        units += ((u < 0x80) || (u >= 0xFF61 && u <= 0xFF9F)) ? 1 : 2;
    }
    return units;
}

int ExecutionEngine::printCWidth(const QString& text) {
    int w = 0;
    for (const QChar c : text) {
        w += (c.unicode() < 0x80 || (c.unicode() >= 0xFF61 && c.unicode() <= 0xFF9F)) ? 1 : 2;
    }
    return w;
}

bool ExecutionEngine::handleResetData() {
    qDebug() << "RESETDATA";
    return true;
}

// 颜色名 -> 0xRRGGBB。覆盖 Emuera 的 C_* 内置常量与常见日/英颜色名，
// 以及 "0xRRGGBB" / 十进制字面量；未知名字回落到白色。
qint64 ExecutionEngine::colorValueOf(const QString& rawName) {
    QString name = rawName.trimmed();
    if (name.isEmpty()) return kDefaultColor;
    // 字面量：0xRRGGBB / #RRGGBB / 十进制
    {
        QString hex = name;
        if (hex.startsWith(QLatin1Char('#'))) hex.remove(0, 1);
        bool ok = false;
        const uint v = hex.toUInt(&ok, 0);
        if (ok) return static_cast<qint64>(v & 0xFFFFFF);
    }
    name = name.toUpper();
    if (name.startsWith(QLatin1String("C_"))) name.remove(0, 2);
    static const QHash<QString, qint64> kColors = {
        {QStringLiteral("BLACK"),      0x000000}, {QStringLiteral("WHITE"),      0xFFFFFF},
        {QStringLiteral("RED"),        0xFF0000}, {QStringLiteral("GREEN"),      0x00FF00},
        {QStringLiteral("BLUE"),       0x0000FF}, {QStringLiteral("YELLOW"),     0xFFFF00},
        {QStringLiteral("CYAN"),       0x00FFFF}, {QStringLiteral("AQUA"),       0x00FFFF},
        {QStringLiteral("MAGENTA"),    0xFF00FF}, {QStringLiteral("PURPLE"),     0xFF00FF},
        {QStringLiteral("GRAY"),       0x808080}, {QStringLiteral("GREY"),       0x808080},
        {QStringLiteral("SILVER"),     0xC0C0C0}, {QStringLiteral("LIME"),       0x00FF00},
        {QStringLiteral("MAROON"),     0x800000}, {QStringLiteral("NAVY"),       0x000080},
        {QStringLiteral("OLIVE"),      0x808000}, {QStringLiteral("TEAL"),       0x008080},
        {QStringLiteral("PINK"),       0xFFC0CB}, {QStringLiteral("ORANGE"),     0xFFA500},
        {QStringLiteral("GOLD"),       0xFFD700}, {QStringLiteral("BROWN"),      0xA52A2A},
        {QStringLiteral("LIGHTGRAY"),  0xD3D3D3}, {QStringLiteral("DARKGRAY"),   0xA9A9A9},
        // 日文颜色名（Emuera 的内置名）
        {QStringLiteral("白"),          0xFFFFFF}, {QStringLiteral("黒"),          0x000000},
        {QStringLiteral("赤"),          0xFF0000}, {QStringLiteral("緑"),          0x00FF00},
        {QStringLiteral("青"),          0x0000FF}, {QStringLiteral("黄"),          0xFFFF00},
        {QStringLiteral("紫"),          0xFF00FF}, {QStringLiteral("茶"),          0xA52A2A},
    };
    return kColors.value(name, kDefaultColor);
}
bool ExecutionEngine::handleLoadGlobal() {
    // 对齐 C# LOADGLOBAL_Instruction：文件缺失 / 校验失败 -> RESULT=0 并返回
    //（C# 里 LOADGLOBAL 失败不是错误，脚本以 `RESULT = 0` 分支处理）。
    bool ok = false;
    const QString path = m_gameDataDir + QLatin1String("/save_global.dat");
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        const QJsonObject root = doc.object();
        ok = root.value(QStringLiteral("format")).toString() == QStringLiteral("emuera-qt-global");
        if (ok) {
            // 唯一码校验（对齐 C# UniqueCodeEqualTo：别的游戏的存档不读）
            const qint64 code = globalUniqueCode();
            ok = (code == 0) || (root.value(QStringLiteral("uniqueCode")).toVariant().toLongLong() == code);
        }
        if (ok) {
            const QJsonArray globals = root.value(QStringLiteral("globals")).toArray();
            for (int i = 0; i < globals.size(); ++i)
                m_storage->setGlobalInt1D(QStringLiteral("GLOBAL"), i, globals.at(i).toVariant().toLongLong());
            const QJsonArray globalss = root.value(QStringLiteral("globalss")).toArray();
            for (int i = 0; i < globalss.size(); ++i)
                m_storage->setGlobalStr1D(QStringLiteral("GLOBALS"), i, globalss.at(i).toString());
            const QJsonObject vars = root.value(QStringLiteral("vars")).toObject();
            for (auto it = vars.begin(); it != vars.end(); ++it) {
                const QJsonObject entry = it.value().toObject();
                const QJsonArray data = entry.value(QStringLiteral("data")).toArray();
                const bool isStr = entry.value(QStringLiteral("type")).toString() == QLatin1String("str");
                for (int i = 0; i < data.size(); ++i) {
                    if (isStr) m_storage->setGlobalStr1D(it.key(), i, data.at(i).toString());
                    else m_storage->setGlobalInt1D(it.key(), i, data.at(i).toVariant().toLongLong());
                }
            }
            qDebug() << "LOADGLOBAL 成功" << path;
        }
    }
    if (m_storage) m_storage->setSystemVariable(QStringLiteral("RESULT"), 0, ok ? 1 : 0);
    return true;
}

bool ExecutionEngine::handleSaveGlobal() {
    // 对齐 C# SAVEGLOBAL_Instruction -> VEvaluator.SaveGlobal()：
    // 把 GLOBAL / GLOBALS 与用户 `#DIM SAVEDATA GLOBAL` 变量写入 save_global.dat。
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("emuera-qt-global"));
    root.insert(QStringLiteral("uniqueCode"), static_cast<qint64>(globalUniqueCode()));

    auto sizeOf = [this](const QString& name) -> int {
        int size = m_storage->arraySize(name);
        const VariableConfig& cfg = m_storage->variableConfig();
        if (size <= 0) size = cfg.getSize1D(name);
        if (size <= 0) size = cfg.getSize1D(name.toUpper());
        return qMax(size, 0);
    };

    QJsonArray globals;
    {
        const int size = sizeOf(QStringLiteral("GLOBAL"));
        for (int i = 0; i < size; ++i)
            globals.append(static_cast<double>(m_storage->getGlobalInt1D(QStringLiteral("GLOBAL"), i)));
    }
    root.insert(QStringLiteral("globals"), globals);
    QJsonArray globalss;
    {
        const int size = sizeOf(QStringLiteral("GLOBALS"));
        for (int i = 0; i < size; ++i)
            globalss.append(m_storage->getGlobalStr1D(QStringLiteral("GLOBALS"), i));
    }
    root.insert(QStringLiteral("globalss"), globalss);

    // 用户 `#DIM SAVEDATA GLOBAL` 变量（对齐 C# userDefinedGlobalSaveVarList）
    QJsonObject vars;
    if (m_parseTable) {
        const QList<VariableDecl> decls = m_parseTable->variableTable().declarations();
        for (const VariableDecl& decl : decls) {
            if (!decl.isGlobalSave || decl.isReference || decl.isConst) continue;
            const int size = qMax(sizeOf(decl.name), decl.lengths.value(0, 1));
            if (size <= 0) continue;
            QJsonArray data;
            const bool isStr = decl.type == OperandType::Str;
            for (int i = 0; i < size; ++i) {
                if (isStr) data.append(m_storage->getGlobalStr1D(decl.name, i));
                else data.append(static_cast<double>(m_storage->getGlobalInt1D(decl.name, i)));
            }
            QJsonObject entry;
            entry.insert(QStringLiteral("type"), isStr ? QStringLiteral("str") : QStringLiteral("int"));
            entry.insert(QStringLiteral("data"), data);
            vars.insert(decl.name, entry);
        }
    }
    root.insert(QStringLiteral("vars"), vars);

    if (m_gameDataDir.isEmpty()) {
        qWarning() << "SAVEGLOBAL：游戏目录未知，跳过保存";
        return true;
    }
    const QString path = m_gameDataDir + QLatin1String("/save_global.dat");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit errorOccurred(QStringLiteral("SAVEGLOBAL 无法写入 %1").arg(path));
        return true;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
    qDebug() << "SAVEGLOBAL 成功" << path;
    return true;
}

qint64 ExecutionEngine::globalUniqueCode() const {
    // 对齐 C# gamebase.ScriptUniqueCode：由游戏标题/版本派生的唯一码，
    // 防止读入其它游戏的全局存档。GameBase 未装载时返回 0（跳过校验）。
    if (!m_gameBaseData) return 0;
    const QString title = m_gameBaseData->windowTitle();
    if (title.isEmpty()) return 0;
    return static_cast<qint64>(qHash(title + QLatin1Char('|') + m_gameBaseData->version()));
}

void ExecutionEngine::setError(const QString& message) {
    qDebug() << "Execution error:" << message;
    m_running = false;
    emit errorOccurred(message);
}

// Receive instruction from ParseTable
// Handle jump request from ParseTable
// Handle memory space change from ParseTable
