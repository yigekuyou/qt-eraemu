#include <QSaveFile>
#include "../GameData/game_paths.h"
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
#include <QScopedValueRollback>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include "expression_evaluator.h"
#include "ast/expression_ast.h"
#include "ast/ast_builder.h"
#include "ast/function_types.h"   // 函数语句（isBuiltinFunction / builtinFunctionReturnType）
#include "ast/print_template.h"
#include "era_parse_table.h"
#include "extension_registry.h"
#include "constant_table.h"
#include "eraengine_log.h"
#include "GameView/graphics_store.h"

namespace {
QVariant evaluateOperand(ExpressionEvaluator& evaluator, const Operand& operand,
                         VariableStorage* storage, GameBaseData* gameBaseData) {
    if (!operand.ast) throw std::runtime_error("Missing load-time operand AST");
    const auto value = evaluator.evaluate(*operand.ast, storage, gameBaseData);
    if (!value.isValid()) throw std::runtime_error("Invalid operand expression");
    return value;
}
} // namespace

// 求值器统一入口：注入的优先，否则惰性创建 fallback（只服务未接线的裸测环境）
ExpressionEvaluator& ExecutionEngine::getEvaluator() {
    if (!m_expressionEvaluator && !m_fallbackEvaluator)
        m_fallbackEvaluator = std::make_unique<ExpressionEvaluator>();
    return m_expressionEvaluator ? *m_expressionEvaluator : *m_fallbackEvaluator;
}

ExecutionEngine::ExecutionEngine(VariableStorage* storage, GameBaseData* gameBaseData, QObject* parent)
    : QObject(parent), m_storage(storage), m_gameBaseData(gameBaseData), m_running(false), m_currentLine(0), m_executionPosition(0), m_totalInstructionsExecuted(0) {
    connect(&m_erbLoader, &ErbLoader::objectNameChanged, this, &ExecutionEngine::objectNameChanged);

    // ParseTable reference (installed later via setParseTable)
    m_parseTable = nullptr;

    // Initialize function system

    // 扩展实现所需的服务（复杂度由注册类承担：引擎填入，扩展经 services() 取用）。
    // 存档目录用惰性 provider —— setGameDirectory 之后才可知。
    m_extensions.setServices(ExtensionRegistry::Services{
        m_storage,
        [this] {
            return m_gameDirectory.isEmpty()
                ? QString() : GamePaths::join(m_gameDirectory, QStringLiteral("sav"));
        },
        nullptr,   // functionExists（EraEngine 装配后经 setExpressionServices 注入）
        nullptr,   // doingFunction
        nullptr,   // displayLine
        // 语句形实参（式中函数裸写）：AST 直接求值。
        [this](const ExpressionNode* node) -> QVariant {
            if (node == nullptr) return QVariant();
            return getEvaluator().evaluate(*node, m_storage, m_gameBaseData);
        },
        // 宿主侧服务（文本框 / FLOWINPUT / UPDATECHECK）：由 EraEngine 装配后经
        // setHostServices 注入（这里留空 —— ExecutionEngine 拿不到控制台/配置）。
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        // EM/Emuera.NET fork 族服务（ENUM* 名字表 / ENUMFILES 基准目录）：
        // 同样由 EraEngine 装配后经 setForkServices 注入。
        nullptr, nullptr, nullptr, nullptr
    });
}

ExecutionEngine::~ExecutionEngine() {
}

void ExecutionEngine::setParseTable(EraParseTable* parseTable) {
    m_parseTable = parseTable;
    if (m_storage) m_storage->setSaveVariableTable(parseTable ? &parseTable->variableTable() : nullptr);
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

    qCDebug(eraTrace) << "[executeLogicalLine]   Executing Instruction:" << line.functionName;
    if (!executeInstruction(line)) {
        qCDebug(eraTrace) << "[executeLogicalLine]   executeInstruction returned false";
    }
}

// FOR loop control - handles FOR LOCAL, start, end
// NEXT loop control - handles NEXT variable
// LOOP control - handles LOOP [condition]
int ExecutionEngine::lhsDimension(const QString& name) const
{
    return m_expressionEvaluator ? m_expressionEvaluator->variableDimension(name) : 1;
}

// 消费装载期变量节点，统一求值数值/CSV字符串下标。
ExecutionEngine::LhsRef ExecutionEngine::resolveLhsRef(const ExpressionNode* ast)
{
    LhsRef ref;
    if (!ast || ast->kind() != NodeKind::Variable) {
        const LogicalLine* line = m_activeInstruction;
        if (!line && m_parseTable)
            line = m_parseTable->lineAt(m_parseTable->currentScript(), m_parseTable->currentLine());
        QStringList parameters;
        if (line) for (const auto& operand : line->arguments) parameters.append(operand.raw);
        emit errorOccurred(QStringLiteral("变量引用缺少装载期 VariableNode；指令 %1；位置 %2；参数 [%3]；节点 %4")
            .arg(line ? line->functionName : QStringLiteral("<unknown>"),
                 line ? line->position.toString() : QStringLiteral("<unknown>"),
                 parameters.join(QStringLiteral(" | ")),
                 ast ? ast->toString() : QStringLiteral("<null>")));
        return ref;
    }
    const auto& variable = static_cast<const VariableNode&>(*ast);
    ref.name = variable.name();
    try {
        ref.indices = getEvaluator().resolveIndices(variable, m_storage, m_gameBaseData);
        ref.valid = true;
    } catch (const std::exception& error) {
        emit errorOccurred(QString::fromUtf8(error.what()));
    }
    return ref;
}

// 读/写：路由与求值器一致（LOCAL/ARG -> 局部槽；系统变量 -> 系统槽；
// 否则按声明维度走 1D / 2D / 3D）
qint64 ExecutionEngine::readLhs(const LhsRef& ref) {
    if (!ref.valid) throw std::runtime_error("Invalid variable reference");
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
    if (!ref.valid) throw std::runtime_error("Invalid variable reference");
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

bool ExecutionEngine::handleCompoundAssignment(const ExpressionNode* lhs, const QString& op, const QString& rhs, const QSharedPointer<ExpressionNode>& ast) {
    Q_UNUSED(rhs);
    ExpressionEvaluator& evaluator = getEvaluator();
    const QVariant rhsVar = ast ? evaluator.evaluate(*ast, m_storage, m_gameBaseData)
        : throw std::runtime_error("Missing compound RHS AST");
    if (!rhsVar.isValid() || isStringValue(rhsVar)) return false;
    const LhsRef ref = resolveLhsRef(lhs);
    if (!ref.valid) return false;
    auto left = QSharedPointer<LiteralNode>::create(readLhs(ref));
    auto right = QSharedPointer<LiteralNode>::create(rhsVar.toLongLong());
    // Fixed instruction opcode dispatch; no runtime lexer.
    static const QHash<QString, TokenType> operators = {
        {"+=", TokenType::PLUS}, {"-=", TokenType::MINUS},
        {"*=", TokenType::MULTIPLY}, {"/=", TokenType::DIVIDE},
        {"%=", TokenType::MODULO}, {"&=", TokenType::BIT_AND},
        {"|=", TokenType::BIT_OR}, {"^=", TokenType::BIT_XOR},
        {"<<=", TokenType::SHIFT_LEFT}, {">>=", TokenType::SHIFT_RIGHT}
    };
    const auto token = operators.constFind(op);
    if (token == operators.constEnd()) return false;
    BinaryOpNode operation(left, ExpressionToken(token.value(), op), right);
    const QVariant result = evaluator.evaluate(operation, m_storage, m_gameBaseData);
    if (!result.isValid()) return false;
    writeLhs(ref, result.toLongLong());
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

    ExpressionEvaluator& ev = getEvaluator();
    const auto evalInt = [&](const Operand& o) -> qint64 {
        if (o.isString) return o.raw.toLongLong();
        if (o.ast) return ev.evaluate(*o.ast, m_storage, m_gameBaseData).toLongLong();
        return evaluateOperand(ev, o, m_storage, m_gameBaseData).toLongLong();
    };
    const auto evalStr = [&](const Operand& o) -> QString {
        if (o.isString) return o.raw;
        if (o.ast) return stringText(ev.evaluate(*o.ast, m_storage, m_gameBaseData));
        throw std::runtime_error("String operand has no load-time AST");
    };

    const LhsRef ref = resolveLhsRef(a[0]->ast.data());
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
        qCDebug(eraTrace) << "[varset] CVARSET" << name << "元素" << index << "值"
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
            qCDebug(eraTrace) << "[varset] VARSET 标量角色变量" << name << "角色" << charaId;
            if (isString) m_storage->setCharaStr(name, charaId, 0, svalue);
            else m_storage->setCharaInt(name, charaId, 0, value);
            return true;
        }
        if (cdim >= 2) {
            // 二维角色数组：整体赋值（忽略范围）
            const QList<int> lens = declaredLengths(name, fn);
            const int n0 = lens.size() >= 1 && lens.at(0) > 0 ? lens.at(0) : 1;
            const int n1 = lens.size() >= 2 && lens.at(1) > 0 ? lens.at(1) : 1;
            qCDebug(eraTrace) << "[varset] VARSET 二维角色数组" << name << "角色" << charaId
                     << "尺寸" << n0 << "x" << n1;
            for (int x = 0; x < n0; ++x)
                for (int y = 0; y < n1; ++y)
                    m_storage->setCharaInt3D(name, charaId, x, y, value);
            return true;
        }
        if (end < 0) end = variableLength1D(name, fn);
        if (start > end) std::swap(start, end);
        qCDebug(eraTrace) << "[varset] VARSET 角色数组" << name << "角色" << charaId
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
        m_storage->setParameter(name, isString ? toVariant(FlatStringValue(svalue)) : QVariant(value));
        return true;
    }

    // ---- LOCAL / LOCALS / ARG / ARGS ----
    if (upper == QLatin1String("LOCAL") || upper == QLatin1String("LOCALS")
        || upper == QLatin1String("ARG") || upper == QLatin1String("ARGS")) {
        const bool strSlot = (upper == QLatin1String("LOCALS") || upper == QLatin1String("ARGS"));
        if (end < 0) end = variableLength1D(name, fn);
        if (start > end) std::swap(start, end);
        if (end <= 0) return true;
        qCDebug(eraTrace) << "[varset] VARSET" << upper << "区间[" << start << "," << end << ")";
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

    // ---- RESULTS：**全局**字符串数组（C# VariableData.cs:202 Str1DVariableToken，
    //      跨函数共享；LOCALS 才是函数局部）----
    if (upper == QLatin1String("RESULTS")) {
        m_storage->setGlobalStr1D(QStringLiteral("RESULTS"), 0, svalue);
        return true;
    }

    // ---- 系统变量 ----
    if (m_storage->hasSystemVariable(name)) {
        if (end < 0) end = variableLength1D(name, fn);
        if (start > end) std::swap(start, end);
        qCDebug(eraTrace) << "[varset] VARSET 系统变量" << name << "区间[" << start << "," << end << ")";
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
        qCDebug(eraTrace) << "[varset] VARSET 全局多维" << (isString ? "字符串" : "整数") << name
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
    qCDebug(eraTrace) << "[varset] VARSET 全局" << (isString ? "字符串" : "整数") << name
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
    QScopedValueRollback<const LogicalLine*> context(m_activeInstruction, &line);
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
            ExpressionEvaluator& ev = getEvaluator();
            value = evaluateOperand(ev, args.first(), m_storage, m_gameBaseData).toLongLong();
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

    // ---- 角色列表扩展（对齐 C# ADDVOIDCHARA / ADDSPCHARA / ADDDEFCHARA / DELALLCHARA）----
    if (name == "ADDVOIDCHARA" || name == "ADDSPCHARA" || name == "ADDDEFCHARA"
        || name == "DELALLCHARA") {
        if (!m_storage) return true;
        if (name == "ADDVOIDCHARA") {
            m_storage->addVoidChara();          // 无任何设置的空角色（csvNo = -1）
        } else if (name == "DELALLCHARA") {
            m_storage->delAllChara();
        } else if (name == "ADDDEFCHARA") {
            // C#：只允许在 @SYSTEM_TITLE 里用；添加 0 号角色，
            // 且 GameBase.csv「最初からいるキャラ」> 0 时再添加它
            m_storage->addChara(0);
            const qint64 def = m_gameBaseData
                ? m_gameBaseData->get(QStringLiteral("最初からいるキャラ")).toLongLong() : -1;
            if (def > 0) m_storage->addChara(static_cast<int>(def));
        } else {   // ADDSPCHARA <番号>
            ExpressionEvaluator& ev = getEvaluator();
            const qint64 value = args.isEmpty() ? 0
                : evaluateOperand(ev, args.first(), m_storage, m_gameBaseData).toLongLong();
            if (value < 0) {
                emit errorOccurred(QStringLiteral("ADDSPCHARA 的角色番号无效: %1").arg(value));
            } else {
                m_storage->addSpChara(static_cast<int>(value));
            }
        }
        return true;
    }

    // TIMES：装载期 double 常量，写回 qint64 时应用配置中的取整方式。
    if (name == "TIMES") {
        if (!m_storage) return true;
        const auto& ops = line.argument.params;
        if (!line.argument.typeOk || ops.size() != 2 || !ops.at(1).realValue) {
            emit errorOccurred(QStringLiteral("TIMES 的实数参数未通过装载期校验"));
            return true;
        }
        const LhsRef ref = resolveLhsRef(ops.first().ast.data());
        if (ref.valid) {
            const double product = static_cast<double>(readLhs(ref)) * *ops.at(1).realValue;
            const RealRounding mode = m_realRoundingProvider ? m_realRoundingProvider() : RealRounding::Round;
            // qFloor/qCeil 返回 int；用标准库同义操作保留 qint64 的范围。
            const double rounded = mode == RealRounding::Floor ? std::floor(product)
                : mode == RealRounding::Ceil ? std::ceil(product) : std::round(product);
            // double(qint64::max()) 等于 2^63，所以用严格上界，避免未定义转换。
            if (!std::isfinite(rounded) || rounded < -0x1p63 || rounded >= 0x1p63) {
                emit errorOccurred(QStringLiteral("TIMES 的计算结果超出 qint64 范围"));
                return true;
            }
            writeLhs(ref, mode == RealRounding::Round ? qRound64(product) : static_cast<qint64>(rounded));
        }
        return true;
    }

    // ---- POWER <变量>, <X>, <Y>（对齐 C# SP_POWER_Instruction：变量 = X^Y）----
    // 语句形式（3 参、首参变量），与式中函数 POWER(X, Y)（2 参）并存；
    // ast_builder 已把语句形式按指令解析（不走 isFunctionCall）。
    if (name == "POWER") {
        if (!m_storage) return true;
        QList<const Operand*> ops;
        for (const Operand& a : args) {
            if (a.isString || a.raw != QLatin1String(",")) ops.append(&a);
        }
        if (ops.size() >= 3) {
            const LhsRef ref = resolveLhsRef(ops.first()->ast.data());
            if (ref.valid) {
                ExpressionEvaluator& ev = getEvaluator();
                const qint64 x = ops.at(1)->ast
                    ? ev.evaluate(*ops.at(1)->ast, m_storage, m_gameBaseData).toLongLong()
                    : evaluateOperand(ev, *ops.at(1), m_storage, m_gameBaseData).toLongLong();
                const qint64 y = ops.at(2)->ast
                    ? ev.evaluate(*ops.at(2)->ast, m_storage, m_gameBaseData).toLongLong()
                    : evaluateOperand(ev, *ops.at(2), m_storage, m_gameBaseData).toLongLong();
                // 对齐 C# PowerMethod.GetIntValue：double 幂，非数值/无穷/超 64 位范围报错
                const double pow = std::pow(static_cast<double>(x), static_cast<double>(y));
                if (std::isnan(pow)) {
                    m_state.setErrorState();
                    emit errorOccurred(QStringLiteral("累乗結果が非数値です（POWER %1, %2）").arg(x).arg(y));
                } else if (std::isinf(pow) || pow >= 9223372036854775807.0 || pow <= -9223372036854775808.0) {
                    m_state.setErrorState();
                    emit errorOccurred(QStringLiteral("累乗結果(%1)が64ビット符号付き整数の範囲外です（POWER %2, %3）")
                                           .arg(QString::number(pow, 'g', 17)).arg(x).arg(y));
                } else {
                    writeLhs(ref, static_cast<qint64>(pow));
                }
            }
        }
        return true;
    }

    // ---- SAVENOS / PRINTCPERLINE <数值变量>（对齐 C# SP_GETINT 指令形式）----
    // 语句形式是「把配置值**写进**该变量」：
    //   Process.ScriptProc.cs:560  case SAVENOS:
    //       ((SpGetIntArgument)func.Argument).VarToken.SetValue(Config.SaveDataNos, exm);
    //   Process.ScriptProc.cs:554  case PRINTCPERLINE: 同上，写 Config.PrintCPerLine
    // 与 0 参式中函数 SAVENOS() / PRINTCPERLINE() 同名不同形：ast_builder 已把语句
    // 形式按指令解析，argument_parser.h 用 ArgKind::GetInt 保证首参是可赋值变量。
    if (name == "SAVENOS" || name == "PRINTCPERLINE") {
        if (!m_storage) return true;
        QList<const Operand*> ops;
        for (const Operand& a : args) {
            if (a.isString || a.raw != QLatin1String(",")) ops.append(&a);
        }
        if (ops.isEmpty()) return true;          // 裸写：C# 会报缺参，这里按宽容处理
        const LhsRef ref = resolveLhsRef(ops.first()->ast.data());
        if (ref.valid) {
            ExpressionEvaluator& ev = getEvaluator();
            writeLhs(ref, name == QLatin1String("SAVENOS")
                              ? ev.saveDataNos()          // Config.SaveDataNos（默认 20）
                              : ev.printCLayout().second); // Config.PrintCPerLine（默认 3）
        }
        return true;
    }

    // ---- RESETGLOBAL（对齐 C# RESETGLOBAL_Instruction：全全局变量归零/清空）----
    if (name == "RESETGLOBAL") {
        if (m_storage) m_storage->resetGlobals();
        return true;
    }

    // ---- CUPCHECK <角色编号>（对齐 C# CUpdateInUpcheck）----
    //   把 CUP/CDOWN 累计值应用到 PALAM 并打印「名 值+上-下=新值」，然后清零。
    if (name == "CUPCHECK") {
        if (!m_storage) return true;
        ExpressionEvaluator& ev = getEvaluator();
        const qint64 target = args.isEmpty() ? -1
            : evaluateOperand(ev, args.first(), m_storage, m_gameBaseData).toLongLong();
        if (target < 0 || target >= m_storage->charaNum()) return true;
        // 角色变量的元素数来自 VariableSize.csv（arraySize 只查全局数组，对
        // 角色变量恒为 0 —— 此前 CUPCHECK 因此从未真正应用过 CUP/CDOWN）
        const int length = variableLength1D("PALAM", QString());
        const int cupLength = variableLength1D("CUP", QString());
        for (int i = 0; i < length; ++i) {
            const qint64 up = m_storage->getCharaInt("CUP", static_cast<int>(target), i);
            const qint64 down = m_storage->getCharaInt("CDOWN", static_cast<int>(target), i);
            if (up <= 0 && down <= 0) continue;
            qint64 param = m_storage->getCharaInt("PALAM", static_cast<int>(target), i);
            const QString pname = m_storage->getGlobalStr1D("PALAMNAME", i);
            param += up - down;   // C#：负值同样参与（unchecked 加减）
            if (!m_skipDisp) {
                QString text = pname + ' ' + QString::number(m_storage->getCharaInt("PALAM", static_cast<int>(target), i));
                if (up > 0) text += '+' + QString::number(up);
                if (down > 0) text += '-' + QString::number(down);
                text += '=' + QString::number(param);
                emit consolePrint(text, true);
            }
            m_storage->setCharaInt("PALAM", static_cast<int>(target), i, param);
        }
        for (int i = 0; i < cupLength; ++i) {
            m_storage->setCharaInt("CUP", static_cast<int>(target), i, 0);
            m_storage->setCharaInt("CDOWN", static_cast<int>(target), i, 0);
        }
        return true;
    }

    // ---- SKIPDISP <n>（对齐 C#：skipPrint = (iValue != 0)，并写 RESULT）----
    if (name == "SKIPDISP") {
        ExpressionEvaluator& ev = getEvaluator();
        const qint64 value = args.isEmpty() ? 0
            : evaluateOperand(ev, args.first(), m_storage, m_gameBaseData).toLongLong();
        m_skipDisp = (value != 0);
        const qint64 result = m_skipDisp ? 1 : 0;
        if (m_storage) {
            m_storage->setSystemVariable("RESULT", 0, result);
            m_storage->setGlobalInt1D("RESULT", 0, result);
        }
        return true;
    }

    // ---- FORCEKANA <n> / NOSKIP / ENDNOSKIP（显示状态，移植版暂无对应渲染开关）----
    if (name == "FORCEKANA" || name == "NOSKIP" || name == "ENDNOSKIP") {
        if (name == "FORCEKANA" && !args.isEmpty()) {
            ExpressionEvaluator& ev = getEvaluator();
            evaluateOperand(ev, args.first(), m_storage, m_gameBaseData).toLongLong();
        }
        qCDebug(eraTrace) << "[display]" << name << "(状态命令，当前渲染层忽略)";
        return true;
    }

    // ---- OUTPUTLOG（对齐 C#：显示行日志写进 emuera.log）----
    if (name == "OUTPUTLOG") {
        emit consoleOutputLog();
        return true;
    }

    // ---- PRINT_RECT / PRINT_SPACE（对齐 C# Console.PrintShape）----
    //   PRINT_RECT <宽>,<高> 或 <x>,<y>,<宽>,<高>；PRINT_SPACE <宽>
    if (name == "PRINT_RECT" || name == "PRINT_SPACE") {
        if (m_skipDisp) return true;
        QList<int> param;
        for (const Operand& a : args) {
            if (!a.isString && a.raw == QLatin1String(",")) continue;
            ExpressionEvaluator& ev = getEvaluator();
            param.append(static_cast<int>(a.ast
                ? ev.evaluate(*a.ast, m_storage, m_gameBaseData).toLongLong()
                : evaluateOperand(ev, a, m_storage, m_gameBaseData).toLongLong()));
            if (param.size() == 4) break;
        }
        if (name == "PRINT_SPACE") {
            if (!param.isEmpty()) emit consolePrintShape("space", param.mid(0, 1));
        } else if (!param.isEmpty()) {
            emit consolePrintShape("rect", param);
        }
        return true;
    }

    // ---- TOOLTIP_SETCOLOR / TOOLTIP_SETDELAY / TOOLTIP_SETDURATION ----
    // C# 设定 GUI 提示框样式；渲染层当前没有提示框实现，先求值并留痕。
    if (name == "TOOLTIP_SETCOLOR" || name == "TOOLTIP_SETDELAY" || name == "TOOLTIP_SETDURATION") {
        qCDebug(eraTrace) << "[display]" << name << "(tooltip 样式，渲染层暂未实现)";
        return true;
    }

    // ENCODETOUNI command: expand FORM and fill RESULT:0..N. The expression
    // function remains a separate scalar operation in ExpressionEvaluator.
    if (name == "ENCODETOUNI") {
        if (!m_storage) return true;
        QString text;
        if (!args.isEmpty()) {
            const Operand& a = args.first();
            text = a.ast ? stringText(getEvaluator().evaluate(*a.ast, m_storage, m_gameBaseData))
                         : a.raw;
        }
        const int capacity = variableLength1D(QStringLiteral("RESULT"), QString());
        if (text.size() > capacity - 1) {
            m_state.setErrorState();
            emit errorOccurred(QStringLiteral("ENCODETOUNI 的字符串长度 %1 超过 RESULT 可用长度 %2")
                               .arg(text.size()).arg(capacity - 1));
            return true;
        }
        // C# iterates UTF-16 indices using char.ConvertToUtf32. Validate all
        // indices before writing so an invalid surrogate leaves RESULT intact.
        QList<qint64> codes;
        for (qsizetype i = 0; i < text.size(); ++i) {
            const QChar c = text.at(i);
            if (c.isLowSurrogate() || (c.isHighSurrogate()
                && (i + 1 == text.size() || !text.at(i + 1).isLowSurrogate()))) {
                m_state.setErrorState();
                emit errorOccurred(QStringLiteral("ENCODETOUNI 的 UTF-16 代理字符无效"));
                return true;
            }
            codes.append(c.isHighSurrogate() ? QChar::surrogateToUcs4(c, text.at(i + 1))
                                            : c.unicode());
        }
        m_storage->setSystemVariable("RESULT", 0, codes.size());
        for (qsizetype i = 0; i < codes.size(); ++i)
            m_storage->setSystemVariable("RESULT", i + 1, codes.at(i));
        return true;
    }

    // ---- REUSELASTLINE <FORM文本>（C# REUSELASTLINE_Instruction：
    //   FORM_STR_NULLABLE，与 PUTFORM 同形）----
    //   整个实参是**格式串**（文本 + {…}/%…% 展开）：`REUSELASTLINE %RESULTS:0%`
    //   （DQPRINT 逐字动画）此前按表达式归约失败 -> 文本完全丢失、只打印空行。
    //   对齐 C#：实参按 FORM 语义展开后单行输出（PrintTemporaryLine）。
    if (name == "REUSELASTLINE") {
        if (m_skipDisp) return true;
        QString text;
        ExpressionEvaluator& ev = getEvaluator();
        if (!args.isEmpty()) {
            const Operand& a = args.first();
            if (a.ast) {
                text = stringText(ev.evaluate(*a.ast, m_storage, m_gameBaseData));
            } else {
                emit errorOccurred(QStringLiteral("REUSELASTLINE 缺少装载期字符串 AST"));
                return true;
            }
        }
        // C# PrintSingleLine：空文本不产生任何输出（IsNullOrEmpty 早退）；
        // 非空按「一時行」单行输出（下一次显示行输出替换它）
        if (!text.isEmpty()) emit consoleReuseLastLine(text);
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
                if (a.isString || a.raw != QLatin1String(",")) ops.append(&a);
            }
        }
        if (ops.isEmpty()) return true;
        const LhsRef ref = resolveLhsRef(ops.first()->ast.data());
        if (!ref.valid) return true;
        ExpressionEvaluator& ev = getEvaluator();
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
        // perf：实参解析（扫空白 / split / trimmed / expressionAst 查表）只在
        // 首次执行做一次，缓存进 LogicalLine::strArgs —— eraTW 地图逐字符时
        // 这几条指令一次绘制执行上万次，重复解析是最大热点。行内容装载后不可变。
        // The function-call AST is produced by AstBuilder during script load.
        // There is deliberately no first-execution tokenizer or expression fallback.
        if (line.arguments.isEmpty() || !line.arguments.first().ast) {
            emit errorOccurred(QStringLiteral("字符串内置命令缺少装载期 AST"));
            return true;
        }
        const QVariant value = getEvaluator().evaluate(*line.arguments.first().ast,
                                                        m_storage, m_gameBaseData);
        if (name == QLatin1String("STRLENS") || name == QLatin1String("STRLENSU")) {
            if (m_storage) m_storage->setSystemVariable(QStringLiteral("RESULT"), 0,
                                                          value.toLongLong());
        } else if (m_storage) {
            m_storage->setGlobalStr1D(QStringLiteral("RESULTS"), 0, stringText(value));
        }
        return true;
    }

    if (name == QLatin1String("VARSIZE")) {
        if (!m_storage || args.isEmpty() || !args.first().ast
            || args.first().ast->kind() != NodeKind::Variable) {
            emit errorOccurred(QStringLiteral("VARSIZE 的参数必须是装载期变量节点"));
            return true;
        }
        const auto& variable = static_cast<const VariableNode&>(*args.first().ast);
        // Array identity, not element value: do not evaluate any supplied indices.
        auto lengths = m_storage->arrayLengths(variable.name());
        if (!variable.lengths.isEmpty()) lengths = variable.lengths;
        if (m_parseTable) {
            const QString key = m_storage->resolvedStorageName(variable.name());
            const int separator = key.indexOf(QChar(0x1f));
            const QString scope = separator >= 0 ? key.left(separator) : QString();
            const QString target = separator >= 0 ? key.mid(separator + 1) : key;
            const auto* declaration = m_parseTable->variableTable().find(target, scope);
            if (declaration && !declaration->isReference && !declaration->lengths.isEmpty())
                lengths = declaration->lengths;
        }
        for (int dim = 0; dim < lengths.size(); ++dim)
            m_storage->setSystemVariable(QStringLiteral("RESULT"), dim, lengths[dim]);
        return true;
    }

    if (name == QLatin1String("HTML_PRINT")) {
        if (line.arguments.isEmpty() || !line.arguments.first().ast) return false;
        const auto value = getEvaluator().evaluate(*line.arguments.first().ast, m_storage, m_gameBaseData);
        if (!value.isValid() || !isStringValue(value)) return false;
        const QString text = stringText(value);
        const PrintTemplate markup = valueMarkup(value);
        // 标签一律在**装载期**构筑（静态模板编译 + 连续片段写链的拼接，
        // 见 EraParseTable::stitchStringFragmentRuns）。这里只做校验：值的分词
        // 若仍留有装载期没能消化的标签材料（字符串函数拼出的标签、条件分支里
        // 动态拼装的标签），就按文本输出并以 [未完成] 留痕 —— 不做运行期解析。
        if (!PrintTemplateCompiler::coversText(markup, text)
            && !m_reportedUnfinished.contains(QStringLiteral("HTML_PRINT"))) {
            m_reportedUnfinished.insert(QStringLiteral("HTML_PRINT"));
            qWarning() << "[未完成] HTML_PRINT 的字符串含装载期未能构筑的标签（按文本输出）:"
                       << text.left(60) << "行:" << line.position.toString();
        }
        emit consolePrintTemplate(markup);
        return true;
    }

    // ---- 输出（PRINT 族，对齐 C# PRINT_Instruction.DoInstruction）----
    // 形态由后缀决定：PRINT / PRINTL / PRINTS / PRINTFORM / PRINTFORML /
    // PRINTFORMS / PRINTV / PRINTC / PRINTLC / PRINTPLAIN* …
    if (AstBuilder::isPrintFamily(name)) {
        return handlePrintInstruction(line);
    }
    if (name == QLatin1String("PRINT_IMG")) {
        if (m_skipDisp) return true;
        if (args.isEmpty()) return true;
        // C#：ArgBuilder = STR_EXPRESSION，实参是**字符串表达式**
        // （`func.Argument.IsConst ? ConstStr : Term.GetStrValue(exm)`）。
        // 因此必须按字符串求值：`PRINT_IMG SV`（#DIMS 字符串变量）要拿到变量内容，
        // 而不是按整数上下文求值得到 0。此前一律 `evaluate()`（整数上下文）：
        //   PRINT_IMG 画像名   -> 0     （QML 就去找 image://emuera/0，日志刷 provider 失败）
        //   PRINT_IMG 变量     -> 0
        // 只有 `PRINT_IMG "名字"` 是对的。现在：引号字面量直接用，其余先按字符串
        // 表达式求值，求不出来再退回原文（C# 这种情况报「标识符未定义」，我们选择
        // 按字面量当资源名——和 HTML_PRINT 的 <img src='裸名'> 行为一致，且可从
        // 日志看出到底找了哪个名字）。
        ExpressionEvaluator& ev = getEvaluator();
        const Operand& operand = args.first();
        QString resName;
        if (operand.isString) {
            resName = operand.raw;
        } else if (operand.ast) {
            if (!ev.evaluateStr(*operand.ast, m_storage, resName)) resName = operand.raw;
        } else {
            emit errorOccurred(QStringLiteral("PRINT_IMG 的字符串参数缺少装载期 AST"));
            return true;
        }
        emit consolePrintImage(resName, 0, 0, 0);
        return true;
    }
    if (name == "PRINTBUTTON" || name == "PRINTBUTTONC" || name == "PRINTBUTTONLC") {
        // PRINTBUTTON <文本>, <整数|字符串>：打印文本并把它变成按钮（对齐 C# PRINTBUTTON）
        // PRINTBUTTONC / PRINTBUTTONLC：先把文本按 PRINTC 定宽补齐再做成按钮
        // （对齐 C# PrintButtonC -> CreateTypeCString：C=右对齐补左侧、LC=左对齐补右侧）
        // 注意：line.arguments 里逗号也是一个操作数，需要过滤掉
        if (m_skipDisp) return true;
        QList<const Operand*> ops;
        if (line.argument.kind == ArgKind::Button && line.argument.params.size() >= 2) {
            for (const Operand& a : line.argument.params) ops.append(&a);
        } else {
            for (const Operand& a : args) {
                if (a.isString || a.raw != QLatin1String(",")) ops.append(&a);
            }
        }
        if (ops.size() >= 2) {
            ExpressionEvaluator& ev = getEvaluator();
            // AstBuilder 已把带引号的字面量标记为 isString 并去掉外层引号；
            // 再按 raw 解析会把 "123" 错当整数、把 "A" 错当变量。
            const auto evaluateOperand = [&](const Operand& operand) -> QVariant {
                if (operand.isString) return toVariant(flatString(QVariant(operand.raw)));
                if (operand.ast) return ev.evaluate(*operand.ast, m_storage, m_gameBaseData);
                throw std::runtime_error("Button operand has no load-time AST");
            };
            const QVariant textValue = evaluateOperand(*ops[0]);
            QString text = stringText(textValue);
            if (text.isEmpty() && !ops[0]->isString) text = ops[0]->raw.trimmed();
            // ボタン処理に絡んで表示がおかしくなるため、PRINTBUTTONでの改行コードはオミット
            text.remove(QLatin1Char('\n'));
            if (name != "PRINTBUTTON" && !text.isEmpty()) {
                text = padPrintC(text, name == "PRINTBUTTONC");   // C=右对齐 / LC=左对齐
            }
            const QVariant value = evaluateOperand(*ops[1]);
            const bool valueIsString = ops[1]->isString
                                       || (ops[1]->ast && ops[1]->ast->valueType() == OperandType::Str);
            emit consolePrintButton(text, value.toLongLong(), stringText(value), valueIsString);
        }
        return true;
    }
    if (name == "CLEARLINE") {
        // 参数是**表达式**（如 `CLEARLINE LINECOUNT - FIRSTLINE`），不能按字面量解析
        int n = 1;
        if (!args.isEmpty() && !args.first().raw.trimmed().isEmpty()) {
            ExpressionEvaluator& ev = getEvaluator();
            n = static_cast<int>(evaluateOperand(ev, args.first(), m_storage, m_gameBaseData).toLongLong());
        }
        if (n > 0) {
            emit consoleClearLines(n);
        }
        return true;
    }
    if (name == "REDRAW") {
        emit consoleRedraw(args.isEmpty() ? 1 : evaluateOperand(getEvaluator(), args.first(), m_storage, m_gameBaseData).toLongLong());
        return true;
    }
    // 后缀自增/自减语句：`I++` / `BAG:COUNT--`
    if (name == "++" || name == "--") {
        if (args.isEmpty()) return true;
        const LhsRef ref = resolveLhsRef(args.first().ast.data());
        if (!ref.valid) return true;
        const qint64 v = readLhs(ref);
        writeLhs(ref, v + (name == "++" ? 1 : -1));
        return true;
    }

    if (name == "RANDOMIZE") {
        // 对齐 C# RANDOMIZE_Instruction：Randomize(seed) —— 之后随机序列可复现
        qint64 seed = 0;
        if (!args.isEmpty() && m_expressionEvaluator) {
            seed = evaluateOperand(*m_expressionEvaluator, args.first(), m_storage, m_gameBaseData).toLongLong();
        }
        if (m_expressionEvaluator) m_expressionEvaluator->setRandomSeed(static_cast<quint32>(seed));
        qDebug() << "[var] RANDOMIZE 固定种子" << seed;
        return true;
    }

    if (name == "RESETCOLOR") {
        m_colorValue = defaultColorValue();   // C#：还原到 Config.ForeColor
        emit consoleResetColor();
        return true;
    }
    // ---- SETCOLORBYNAME <色名>（对齐 C# SETCOLORBYNAME_Instruction）----
    // C#：`Color.FromName(func.Argument.ConstStr)`，无效色名/透明 -> CodeEE。
    // 实参是**字符串**（STR 型），所以去掉引号后按颜色名解析；解析不出来就报错
    // （不静默忽略，否则「拼错色名」会变成随机颜色）。
    if (name == QLatin1String("SETCOLORBYNAME")) {
        if (m_skipDisp) return true;
        QString nm = args.isEmpty() ? QString() : args.first().raw.trimmed();
        if (nm.size() >= 2 && nm.startsWith(QLatin1Char('"')) && nm.endsWith(QLatin1Char('"'))) {
            nm = nm.mid(1, nm.size() - 2);
        }
        const QColor c(nm);
        if (!c.isValid() || c.alpha() == 0) {
            emit errorOccurred(QStringLiteral("SETCOLORBYNAME: 无效的颜色名 \"%1\"（行 %2）")
                                   .arg(nm, line.position.toString()));
            return true;
        }
        m_colorValue = static_cast<qint64>(c.rgb() & 0xFFFFFF);
        emit consoleColor(nm);
        return true;
    }

    // ---- BAR / BARL <值>, <最大值>, <长度>（对齐 C# BAR_Instruction）----
    // C#：`exm.Console.Print(exm.CreateBar(var, max, length))`，BARL 再换行。
    // 三个实参都是**表达式**（`BAR 体力, MAXBASE:0, 10`），必须求值。
    if (name == QLatin1String("BAR") || name == QLatin1String("BARL")) {
        if (m_skipDisp) return true;
        const QList<Operand>& ops = line.argument.params;
        qint64 v[3] = {0, 0, 0};
        ExpressionEvaluator& ev = getEvaluator();
        for (int i = 0; i < 3 && i < ops.size(); ++i) {
            v[i] = evaluateOperand(ev, ops.at(i), m_storage, m_gameBaseData).toLongLong();
        }
        emit consolePrint(ev.createBar(v[0], v[1], v[2]), name == QLatin1String("BARL"));
        return true;
    }

    if (name == "SETCOLOR") {
        // 参数是**表达式**：`SETCOLOR 0x70C070` / `SETCOLOR C_YELLOW` /
        // `SETCOLOR 現在指定の色`（eraTW 的 COLORMESSAGE 就靠后者还原颜色）。
        // 之前直接透传 raw 文本，变量与函数形式都会失效。
        if (!args.isEmpty() && !args.first().raw.trimmed().isEmpty()) {
            const QString raw = args.first().raw.trimmed();
            ExpressionEvaluator& ev = getEvaluator();
            const QVariant value = evaluateOperand(ev, args.first(), m_storage, m_gameBaseData);
            if (isStringValue(value)) {
                // 颜色名（含 C_* 等常量与 @"..." 形式）
                const QString name = stringText(value).trimmed();
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

    // ---- SETBGCOLOR / SETBGCOLORBYNAME（C# SETBGCOLOR_Instruction，SP_COLOR/STR）----
    //   SETBGCOLOR R, G, B      （eraTW TUTORIAL.ERB:11 `SETBGCOLOR 0, 0, 102`）
    //   SETBGCOLOR 0xRRGGBB / SETBGCOLOR <颜色名>
    //   SETBGCOLORBYNAME <颜色名>
    // 此前落到 reportUnfinished 被忽略。
    if (name == QLatin1String("SETBGCOLOR") || name == QLatin1String("SETBGCOLORBYNAME")) {
        ExpressionEvaluator& ev = getEvaluator();
        const auto evalOperand = [&](const Operand& a) -> QVariant {
            if (a.ast) return ev.evaluate(*a.ast, m_storage, m_gameBaseData);
            const QString raw = a.raw.trimmed();
            if (raw.isEmpty()) return QVariant();
            return evaluateOperand(ev, a, m_storage, m_gameBaseData);
        };
        QList<Operand> parts;
        for (const Operand& a : args) {
            const QString t = a.raw.trimmed();
            if (!t.isEmpty() && t != QLatin1String(",")) parts.append(a);
        }
        if (parts.size() >= 3) {
            const qint64 r = evalOperand(parts.at(0)).toLongLong() & 0xFF;
            const qint64 g = evalOperand(parts.at(1)).toLongLong() & 0xFF;
            const qint64 b = evalOperand(parts.at(2)).toLongLong() & 0xFF;
            m_bgColorValue = (r << 16) | (g << 8) | b;
            emit consoleBgColor(
                QStringLiteral("0x%1").arg(m_bgColorValue, 6, 16, QLatin1Char('0')));
        } else if (parts.size() == 1) {
            const QVariant v = evalOperand(parts.first());
            if (isStringValue(v)) {
                const QString nm = stringText(v).trimmed();
                m_bgColorValue = colorValueOf(nm);
                emit consoleBgColor(nm);
            } else {
                m_bgColorValue = v.toLongLong() & 0xFFFFFF;
                emit consoleBgColor(
                    QStringLiteral("0x%1").arg(m_bgColorValue, 6, 16, QLatin1Char('0')));
            }
        }
        return true;
    }

    // ---- 字体样式（FONTBOLD/FONTITALIC/FONTUNDERLINE/FONTSTRIKE/FONTREGULAR/FONTSTYLE）----
    // GETSTYLE/FONTSTYLE 成对使用（eraTW 的 COLORMESSAGE 保存并还原样式）。
    if (name.startsWith(QLatin1String("FONT")) || name == QLatin1String("SETSTYLE")) {
        if (name == QLatin1String("FONTREGULAR")) {
            m_styleBits = 0;
        } else if (name == QLatin1String("FONTSTYLE") || name == QLatin1String("SETSTYLE")) {
            // FONTSTYLE <位掩码>：整体替换
            if (!args.isEmpty() && !args.first().raw.trimmed().isEmpty()) {
                ExpressionEvaluator& ev = getEvaluator();
                m_styleBits = evaluateOperand(ev, args.first(), m_storage, m_gameBaseData).toLongLong();
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
                ExpressionEvaluator& ev = getEvaluator();
                on = evaluateOperand(ev, args.first(), m_storage, m_gameBaseData).toLongLong() != 0;
            }
            m_styleBits = on ? (m_styleBits | bit) : (m_styleBits & ~bit);
        }
        emit consoleFontStyle(m_styleBits & 1, m_styleBits & 2, m_styleBits & 8, m_styleBits & 4);
        return true;
    }
    if (name == "ALIGNMENT") {
        const QString a = args.size() >= 1 ? args[0].raw.toUpper() : QStringLiteral("LEFT");
        m_currentAlign = (a == QLatin1String("CENTER")) ? 1 : (a == QLatin1String("RIGHT")) ? 2 : 0;
        emit consoleAlign(a);
        return true;
    }
    // ---- SETFONT <字体名>（对齐 C# SETFONT_Instruction：记录当前字体）----
    if (name == "SETFONT") {
        if (!args.isEmpty() && !args.first().raw.trimmed().isEmpty()) {
            m_fontName = args.first().raw.trimmed();
            if (m_fontName.compare(QStringLiteral("DEFAULT"), Qt::CaseInsensitive) == 0
                || m_fontName == QStringLiteral("標準")) {
                m_fontName.clear();
            }
        } else {
            m_fontName.clear();   // SETFONT 省略参数 = 恢复默认
        }
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
                // DRAWLINEFORM 参数是 FORM 串（文本 + %…%/{…}）：裸文本
                // （eraTW OPTION.ERB 的 `DRAWLINEFORM =` / `DRAWLINEFORM －`）
                // 必须按**字面量**处理 —— 此前按表达式求值，`－` 被当负号求成 0、
                // `=` 解析失败，分隔线成了 "0000…" 或空行。
                const auto& ast = args.first().ast;
                if (!ast) {
                    emit errorOccurred(QStringLiteral("DRAWLINEFORM 缺少装载期字符串 AST"));
                    return true;
                }
                barStr = stringText(getEvaluator().evaluate(*ast, m_storage, m_gameBaseData));
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
    if (!line.argument.typeOk && !line.assignOperator.isEmpty()) {
        emit errorOccurred(line.argument.typeError);
        return false;
    }
    if (name == "=") {
        if (args.size() >= 2) {
            // 目的变量是字符串 -> 字符串赋值（此前只有整数路径）。
            // 目的类型优先读取已定型左值节点。
            // Type lookup must not evaluate indexed LHS expressions (e.g. A:I++).
            const OperandType destType =
                args[0].ast ? args[0].ast->valueType() : OperandType::Unknown;
            if (destType == OperandType::Str) {
                return handleStringAssignment(args[0].ast.data(), args[1].raw, args[1].ast,
                                              line.ownerFunction);
            }
            return handleAssignment(args[0].ast.data(), args[1].raw, args[1].ast,
                                    line.assignmentAsts, line.ownerFunction);
        }
        return true;
    }
    // ---- VARSET 族（对齐 C# FunctionCode.VARSET / CVARSET）----
    //   VARSET 名[, 值[, 初値[, 終値]]]：把变量的元素区间整体赋值
    //   CVARSET 角色变量, 下标[, 值[, 初値[, 終値]]]：逐角色设置同一元素
    // 以前 ArgKind::VarSet 只在 argument_parser 里登记、执行期没有任何分支，
    // 于是 `PRINT_STATE.ERB:336 VARSET TLNT_CNT` 之类的清空被**静默跳过**，
    // 计数器不清零 -> 素質/性的特徴 列表越叠越长。
    // ---- SPLIT（核心函数，对齐 C# FunctionCode.SPLIT / SpSplitArgument）----
    //   装载期第3引数（裸数组变量，如 STR_ARRAY）不做表达式归约，
    //   整行**不是**函数调用形态（SPLIT 不在 kBuiltinFunctions 表），
    //   因此走不到 executeFunctionCall 的专用分支 —— 必须在指令分发处处理。
    //   此前落到「其它指令」被静默忽略 -> STR_ARRAY 永不填充 ->
    //   eraTW 的 @TEXTR（COMMON.ERB:229）返回空串，随机抽选全链失效。
    if (name == QLatin1String("SPLIT")) {
        if (handleSplit(line)) return true;
        reportUnfinished(QStringLiteral("SPLIT（参数不足）"), name, line);
        return true;
    }
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
            const OperandType destType =
                args[0].ast ? args[0].ast->valueType() : OperandType::Unknown;
            if (destType == OperandType::Int) {
                // C# 同样拒绝：整数型变量没有 '= 语义
                qWarning() << "[exec] 整数型变量不能使用 '= 赋值，已忽略：" << args[0].raw;
                return true;
            }
            QList<QVariant> values;
            const auto nodes = line.assignmentAsts.isEmpty()
                ? QList<QSharedPointer<ExpressionNode>>{args[1].ast} : line.assignmentAsts;
            for (const auto& node : nodes) {
                const auto value = node ? getEvaluator().evaluate(*node, m_storage, m_gameBaseData) : QVariant();
                if (!value.isValid() || !isStringValue(value)) return false;
                values.append(value);
            }
            const auto ref = resolveLhsRef(args[0].ast.data());
            if (!ref.valid) return false;
            const auto* decl = m_parseTable ? m_parseTable->variableTable().find(ref.name, line.ownerFunction) : nullptr;
            const int start = ref.indices.isEmpty() ? 0 : ref.indices.last();
            const int size = decl && !decl->lengths.isEmpty() ? decl->lengths.last() : m_storage->arraySize(ref.name);
            if ((decl && decl->isConst) || (values.size() > 1 && size > 0
                && (start < 0 || qint64(start)+values.size() > size))) {
                emit errorOccurred(QStringLiteral("字符串数组赋值越界或const左值")); return false;
            }
            for (int i = 0; i < values.size(); ++i) {
                QList<int> indices = ref.indices;
                if (indices.isEmpty()) indices.append(0);
                indices.last() += i;
                LhsRef target = ref;
                target.indices = indices;
                if (!writeStructuredValue(target, values[i], line.ownerFunction)) return false;
            }
            return true;
        }
        return true;
    }
    if (name == "+=") {
        // 字符串累加（`A += B` -> A = A + B）
        if (args.size() >= 2) {
            // Type lookup must not evaluate indexed LHS expressions (e.g. A:I++).
            const OperandType destType =
                args[0].ast ? args[0].ast->valueType() : OperandType::Unknown;
            if (destType == OperandType::Str) {
                if (!args[1].ast) return false;
                const auto ref = resolveLhsRef(args[0].ast.data());
                if (!ref.valid) return false;
                auto lhs = QSharedPointer<VariableNode>::create(ref.name, OperandType::Str);
                for (int index : ref.indices) lhs->addIndex(QSharedPointer<LiteralNode>::create(qint64(index)));
                BinaryOpNode operation(lhs, ExpressionToken(TokenType::PLUS, "+"), args[1].ast);
                const QVariant value = getEvaluator().evaluate(operation, m_storage, m_gameBaseData);
                if (!value.isValid()) return false;
                return writeStructuredValue(ref, value, line.ownerFunction);
            }
        }
    }
    if (name == "*=" && args.size() >= 2 && isStringVariable(args[0].ast && args[0].ast->kind() == NodeKind::Variable ? static_cast<const VariableNode&>(*args[0].ast).name() : QString(), line.ownerFunction)) {
        if (!args[1].ast) return false;
        const auto ref = resolveLhsRef(args[0].ast.data());
        if (!ref.valid) return false;
        auto lhs = QSharedPointer<VariableNode>::create(ref.name, OperandType::Str);
        for (int index : ref.indices) lhs->addIndex(QSharedPointer<LiteralNode>::create(qint64(index)));
        BinaryOpNode operation(lhs, ExpressionToken(TokenType::MULTIPLY, "*"), args[1].ast);
        const QVariant value = getEvaluator().evaluate(operation, m_storage, m_gameBaseData);
        if (!value.isValid() || !isStringValue(value)) return false;
        return writeStructuredValue(ref, value, line.ownerFunction);
    }
    if (name == "+=" || name == "-=" || name == "*=" || name == "/=" || name == "%="
        || name == "&=" || name == "|=" || name == "^=" || name == "<<=" || name == ">>=") {
        if (args.size() >= 2) {
            return handleCompoundAssignment(args[0].ast.data(), name, args[1].raw, args[1].ast);
        }
        return true;
    }
    if (name == "+" || name == "-" || name == "*" || name == "/" || name == "%") {
        // A + 1 等价 A = A + 1
        if (args.size() >= 2) {
            const auto* lhs = args[0].ast.data();
            return handleCompoundAssignment(lhs, name + "=", args[1].raw, args[1].ast);
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
    // ---- DEBUGPRINT 族（对齐 C# FunctionCode.DEBUGPRINT*：写到调试输出）----
    // eraTW 实测 65+ 处使用（DEBUGPRINTFORML 65 / 宽松 116）；此前全部静默
    // 跳过，调试信息完全丢失。对齐 C#：输出到调试端（不进游戏控制台），
    // 这里映射到 eraTrace 调试日志（EMUERA_QDBUG_TRACE=1 可见）。
    // 后缀语义与 PRINT 族一致：V/S=变量求值、FORM=格式串、L/W=换行。
    if (name.startsWith(QLatin1String("DEBUGPRINT"))) {
        ExpressionEvaluator& ev = getEvaluator();
        QString text;
        for (const Operand& operand : args) {
            if (operand.ast) text += stringText(ev.evaluate(*operand.ast, m_storage, m_gameBaseData));
            else if (operand.isString) text += operand.raw;
        }
        if (!name.endsWith(QLatin1Char('L')) && !name.endsWith(QLatin1Char('W')))
            text += QLatin1Char('\n');
        qCDebug(eraTrace).noquote() << "[debugprint]" << text;
        return true;
    }
    // ---- PRINTDATA/STRDATA 段的成员行 ----
    // DATA / DATAFORM / DATALIST / ENDLIST / ENDDATA 正常由宿主（PRINTDATA 系 /
    // STRDATA）一次跳过整段（见 ScriptRunner 的 isPrintDataName 分支），不会单独
    // 执行；只有「宿主没被识别」时才会落到这里。此时按 C# 的段语义**静默跳过** ——
    // 它们是段的组成部分而不是独立指令，此前会误报 [未完成]（02_PRINT 的
    // `DATA 随机串1` / `ENDDATA` 就是这么冒出来的）。
    if (name == QLatin1String("DATA") || name == QLatin1String("DATAFORM")
        || name == QLatin1String("DATALIST") || name == QLatin1String("ENDLIST")
        || name == QLatin1String("ENDDATA")) {
        return true;
    }

    // ---- CLEARTEXTBOX（对齐 C# CLEARTEXTBOX：清空最下方输入栏）----
    // 输入栏在 QML 侧（Console.inputField），C++ 只负责发出清空请求，
    // 由 ConsoleBackend 转发给 QML（不要在这里假装清空）。
    if (name == QLatin1String("CLEARTEXTBOX")) {
        emit clearTextBox();
        return true;
    }

    // ---- 存档系（普通语句形态：LOADDATA 等不经过 executeFunctionCall）----
    if (name == QLatin1String("SAVEDATA")) { handleSaveData(line); return true; }
    if (name == QLatin1String("LOADDATA"))  { handleLoadData(line); return true; }
    if (name == QLatin1String("DELDATA"))   { handleDelData(line); return true; }
    if (name == QLatin1String("CHKDATA"))   { handleChkData(line); return true; }

    // ---- 状态打印 / 角色整理 / 变量存档族（此前未实现、运行期被忽略）----
    // UPCHECK / PRINT_ABL / PRINT_TALENT / PRINT_MARK / PRINT_EXP / PRINT_PALAM /
    // PRINT_ITEM / PRINT_SHOPITEM / HTML_TAGSPLIT / SORTCHARA / SAVEVAR / LOADVAR
    if (name == QLatin1String("UPCHECK")
        || name == QLatin1String("PRINT_ABL") || name == QLatin1String("PRINT_TALENT")
        || name == QLatin1String("PRINT_MARK") || name == QLatin1String("PRINT_EXP")
        || name == QLatin1String("PRINT_PALAM") || name == QLatin1String("PRINT_ITEM")
        || name == QLatin1String("PRINT_SHOPITEM")
        || name == QLatin1String("HTML_TAGSPLIT") || name == QLatin1String("SORTCHARA")
        || name == QLatin1String("SAVEVAR") || name == QLatin1String("LOADVAR")) {
        return handleStatusCommand(line);
    }

    // ---- 扩展注册类（普通语句形态：扩展语句在这里分发）----
    // 语句形态的扩展函数（如 CHKVARDATA）不经过 executeFunctionCall ——
    // 在「其它指令」之前查注册类，命中即执行（桩 = 留痕一次 + 跳过，
    // 由注册类承担，对齐 EE 的容错语义）；未命中继续走「其它指令」。
    if (m_extensions.runStatement(name, line)) {
        return true;
    }

    // [qdbug]（保留的调试桩）：落到「其它指令」的**每一次**执行都留痕 ——
    // reportUnfinished 同名只报一次，会漏掉后续出现的同型行；
    // EMUERA_QDBUG_TRACE=1 时逐次输出，便于定位「指令被静默跳过」的完整现场
    //（如 SPLIT 此前被静默跳过、COLOREDMAP 的 TRY 失败等）。
    if (m_qdbugTrace) {
        qCDebug(eraTrace) << "[qdbug] other-instruction" << name
                          << "行" << line.position.toString()
                          << "|" << line.raw.trimmed().left(60);
    }
    reportUnfinished(QStringLiteral("指令"), name, line);
    return true;
}

// ---------------------------------------------------------------------------
// SPLIT（**核心函数**，对齐 C# FunctionCode.SPLIT / SpSplitArgument）
//
// 设计规则：C# 原型（Process.ScriptProc.cs）已有的函数一律走核心引擎分支，
// 不进语句函数注册表；注册表只收「C# 原型没有的扩展函数」（EmueraEE 系）。
//
//   SPLIT <字符串>, <分隔串>, <数组变量>[, <个数变量>]
//   * 第3引数必须是**数组变量**；第4引数可省略，省略时个数写 RESULT:0
//   * 分割后全部元素从数组下标 0 依次写入（超出数组长度时截断，C# 同款）
//   * 元素个数写入第4变量（或 RESULT:0）
// 此前没有执行分支：`SPLIT ARGS, "/", STR_ARRAY` 被静默跳过，
// STR_ARRAY 永不填充 -> eraTW 的 @TEXTR（COMMON.ERB:229）返回空串，
// 特典/能力文本/処女喪失履歴 等随机抽选全链失效。
// ---------------------------------------------------------------------------
bool ExecutionEngine::handleSplit(const LogicalLine& line)
{
    ExpressionEvaluator& ev = getEvaluator();
    const auto evalStr = [&](const Operand& op) -> QString {
        if (op.isString) return op.raw;
        return op.ast ? stringText(ev.evaluate(*op.ast, m_storage, m_gameBaseData))
                      : throw std::runtime_error("String operand has no load-time AST");
    };
    // 顶层逗号切分（逗号可能是独立 Operand，也可能是表达式的一部分）
    QList<Operand> parts;
    for (const Operand& op : line.arguments) {
        // 只跳过「标点逗号」——引号字面量 ","（分隔符实参）的 raw 也是 ","，
        // 但 isString 为真，必须保留（否则 SPLIT "a,b", ",", ARR 的分隔符丢失）。
        if (!op.isString && op.raw == QLatin1String(",")) continue;
        parts.append(op);
    }
    if (parts.size() < 3) return false;   // 参数不足：交给通用路径留痕

    const QString target = evalStr(parts[0]);
    const QString delimiter = evalStr(parts[1]);
    LhsRef array = resolveLhsRef(parts[2].ast.data());
    if (!array.valid) return false;
    const QStringList elements = delimiter.isEmpty()
        ? QStringList{ target }
        : target.split(delimiter, Qt::KeepEmptyParts);

    // 全部元素从下标 0 依次写入（复用字符串赋值路径：
    // 参数/ARGS/LOCALS/角色变量/全局变量 的解析逻辑保持一致）
    for (int i = 0; i < elements.size(); ++i) {
        array.indices = {i};
        writeStringValue(array, elements.at(i));
    }

    // 个数：第4变量（可省略）-> 否则 RESULT:0
    if (parts.size() >= 4 && !parts[3].raw.trimmed().isEmpty()) {
        const auto count = resolveLhsRef(parts[3].ast.data());
        if (!count.valid) return false;
        writeLhs(count, elements.size());
    } else {
        m_storage->setSystemVariable(QStringLiteral("RESULT"), 0, elements.size());
    }
    qCDebug(eraTrace) << "[funcstmt] SPLIT ->" << elements.size() << "个元素 行"
             << line.position.toString();
    return true;
}

// ---------------------------------------------------------------------------
// 扩展分发优先级（固定，由结构决定，不由注册顺序决定）：
//   ① 核心专用分支（executeInstruction / executeFunctionCall 内联）——「0-127 核心段」
//   ② 扩展注册类查表（ExtensionRegistry::runStatement）——「128-255 扩展段」
//   ③ 通用路径（表达式求值 → kBuiltinFunctions 表）
//
// 注册 API 在 ExtensionRegistry（复杂度由注册类承担：fail-fast / first-wins /
// 留痕桩 / 插入 AST）；EE 扩展 = ee_extension.h 单独一个头文件，只在注册类里
// 被实现（其他位置不得放置 EE 头文件），注册类构造时一次登记，默认全启用。
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

    // ---- C# 原版全量补全（BuiltInFunctionCode.cs 枚举内、此前未实现的分支）----
    // ASSERT <expr>：运行期断言（C# FunctionCode.ASSERT）—— 为假则报错
    if (name == QLatin1String("ASSERT")) {
        ExpressionEvaluator& ev = getEvaluator();
        qint64 v = 0;
        if (!line.arguments.isEmpty()) {
            const Operand& op = line.arguments.first();
            v = op.ast ? ev.evaluate(*op.ast, m_storage, m_gameBaseData).toLongLong()
                       : evaluateOperand(ev, op, m_storage, m_gameBaseData).toLongLong();
        }
        if (!v) {
            m_state.setErrorState();
            emit errorOccurred(QStringLiteral("ASSERT 断言失败：%1").arg(line.raw.trimmed()));
        }
        return true;
    }
    // （SETBGCOLORBYNAME 已在上面的 SETBGCOLOR 分支统一处理）
    // TOOLTIP_SETCOLOR/SETDELAY/SETDURATION（C# FunctionCode.TOOLTIP_*）
    if (name.startsWith(QLatin1String("TOOLTIP_"))) {
        qCDebug(eraTrace) << "[display] TOOLTIP 配置" << name
                 << (line.arguments.isEmpty() ? QString() : line.arguments.first().raw.trimmed());
        return true;
    }
    // ---- PUTFORM（C# FunctionCode.PUTFORM：StrForm 实参 -> SAVEDATA_TEXT 累加）----
    //   对齐 C# Process.ScriptProc.cs:291-293：SAVEDATA_TEXT 非空则 +=、否则 =；
    //   @SAVEINFO 内可多次调用逐段拼接。实参是 StrForm（文本 + {…}/%…%），
    //   装载期已按 StrForm 解析（见 AstBuilder 内建分支）；未能归约时按原文兜底
    //   （概要信息不应丢）。
    if (upper == QLatin1String("PUTFORM")) {
        QString text;
        if (!line.arguments.isEmpty() && line.arguments.first().ast) {
            ExpressionEvaluator& ev = getEvaluator();
            text = stringText(ev.evaluate(*line.arguments.first().ast, m_storage, m_gameBaseData));
        } else if (!line.arguments.isEmpty()) {
            text = line.arguments.first().raw;
        }
        if (m_storage) {
            const QString prev = m_storage->getSystemStr(QStringLiteral("SAVEDATA_TEXT"), 0);
            m_storage->setSystemStr(QStringLiteral("SAVEDATA_TEXT"), 0, prev + text);
        }
        return true;
    }
    // ---- SPLIT：核心函数专用分支（对齐 C# FunctionCode.SPLIT）----
    // 实参形态特殊（裸数组变量/个数变量），不能当普通表达式整行求值。
    if (upper == QLatin1String("SPLIT")) {
        return handleSplit(line);
    }

    // ---- None-op 内建：显示/配置类（对齐 C# FunctionCode.RESETBGCOLOR 等）----
    //   此前注册于 kBuiltinFunctions 但 BuiltinOp::None 无求值分支，
    //   运行期报「内置函数尚未实现求值（返回 0）」。
    if (upper == QLatin1String("RESETBGCOLOR")) {
        // 还原背景色（C# RESETBGCOLOR_Instruction -> Console.ResetBgColor）
        m_bgColorValue = -1;
        emit consoleResetBgColor();
        qCDebug(eraTrace) << "[display] RESETBGCOLOR" << "行" << line.position.toString();
        return true;
    }
    if (upper == QLatin1String("INITRAND")) {
        if (m_expressionEvaluator) m_expressionEvaluator->randomize();
        qDebug() << "[var] INITRAND 随机重置" << "行" << line.position.toString();
        return true;
    }
    if (upper == QLatin1String("DUMPRAND")) {
        if (m_expressionEvaluator)
            qDebug() << "[var] DUMPRAND 状态" << m_expressionEvaluator->randomSeed()
                     << "行" << line.position.toString();
        return true;
    }
    if (upper == QLatin1String("DEBUGCLEAR")) {
        qCDebug(eraTrace) << "[debugprint] DEBUGCLEAR" << "行" << line.position.toString();
        return true;
    }
    // ---- 存档系（核心：C# 原版全量，BuiltInFunctionCode.cs 枚举内）----
    //   SAVEDATA/LOADDATA/DELDATA/CHKDATA：函数调用形态与普通语句形态都
    //   可能出现，统一委托私有方法（实现见 handleSaveData 等）。
    if (upper == QLatin1String("SAVEDATA")) { handleSaveData(line); return true; }
    if (upper == QLatin1String("LOADDATA"))  { handleLoadData(line); return true; }
    if (upper == QLatin1String("DELDATA"))   { handleDelData(line); return true; }
    if (upper == QLatin1String("CHKDATA"))   { handleChkData(line); return true; }

    // ---- 扩展注册类（扩展函数专用：C# 原型没有的函数）----
    // 注册类命中即执行（桩的「留痕 + 跳过」由注册类承担），
    // 未命中返回 false 落到下面的通用路径。
    if (m_extensions.runStatement(upper, line)) {
        return true;
    }

    // ---- STRLENFORM / STRLENFORMU（C# STRLEN_Instruction(argisform=true)）----
    //   实参是 FORM_STR：先展开 {…}/%…% 再取长度
    //     STRLENFORM  -> 语言编码的**字节数**
    //     STRLENFORMU -> UTF-16 **码元数**
    //   eraTW `STRLENFORM %ForagePlaceName(SpotID)%`（MAP_MANAGE.ERB:466）以语句
    //   形式出现，此前整行被「函数语句（实参无法归约）」跳过。
    if (upper == QLatin1String("STRLENFORM") || upper == QLatin1String("STRLENFORMU")) {
        ExpressionEvaluator& ev = getEvaluator();
        QString text;
        if (!line.arguments.isEmpty()) {
            const Operand& a = line.arguments.first();
            if (a.ast) text = stringText(ev.evaluate(*a.ast, m_storage, m_gameBaseData));
            else text = a.raw;
        }
        const qint64 len = (upper == QLatin1String("STRLENFORMU"))
                               ? static_cast<qint64>(text.size())
                               : static_cast<qint64>(ev.langByteCount(text));
        if (m_storage) m_storage->setSystemVariable(QStringLiteral("RESULT"), 0, len);
        return true;
    }

    const bool unfinished = !line.arguments.isEmpty() && !line.arguments.first().ast;

    QVariant value;
    if (unfinished) {
        // 表达式没归约出来（语法不认识 / 参数形态特殊）：明确留痕并跳过。
        reportUnfinished(QStringLiteral("函数语句（实参无法归约）"), name, line);
        return true;
    }
    ExpressionEvaluator& ev = getEvaluator();
    value = ev.evaluate(*line.arguments.first().ast, m_storage, m_gameBaseData);

    // 返回类型决定写哪个寄存器（对齐 C#：Int64 -> RESULT，string -> RESULTS）
    const OperandType ret = builtinFunctionReturnType(upper.toStdString());
    const bool returnsStr = (ret == OperandType::Str)
                            || (ret == OperandType::Unknown && isStringValue(value));
    if (returnsStr) {
        // [qdbug] 修复：RESULTS 全局（C# VariableData.cs:202，跨函数共享）
        m_storage->setGlobalStr1D(QStringLiteral("RESULTS"), 0, stringText(value));
    } else {
        m_storage->setSystemVariable(QStringLiteral("RESULT"), 0, value.toLongLong());
    }
    qCDebug(eraTrace) << "[funcstmt]" << upper << "->" << (returnsStr ? "RESULTS" : "RESULT")
             << value << "行" << line.position.toString();
    return true;
}

// ---------------------------------------------------------------------------
// PRINTDATA 段辅助（C# PRINT_DATA_Instruction / ErbLoader 的 dataList）
// ---------------------------------------------------------------------------
// 打印一条 DATAFORM/DATA 行：按 StrForm 求值后送入 console（不换行）。
// 段内多行的换行、段后的换行（…L/…W）、以及 …W 的等键均由 ScriptRunner 驱动。
void ExecutionEngine::printDataFormLine(const LogicalLine& line) {
    if (m_skipDisp) return;
    emit consolePrint(printDataFormText(line), false);
}

// 只求值不显示：STRDATA 用（C# STRDATA 把所选段的字符串写进变量）
QString ExecutionEngine::printDataFormText(const LogicalLine& line) {
    if (line.arguments.isEmpty()) return QString();
    const Operand& a = line.arguments.first();
    if (a.ast) return stringText(getEvaluator().evaluate(*a.ast, m_storage, m_gameBaseData));
    return a.raw;
}

void ExecutionEngine::printDataNewline() {
    if (m_skipDisp) return;
    emit consolePrint(QString(), true);
}

void ExecutionEngine::requestPrintDataWaitKey() {
    if (m_skipDisp) return;
    emit requestAnyKey();
    m_printWaitKey = true;
}

void ExecutionEngine::assignPrintDataIndex(const Operand& lhs, qint64 value) {
    const LhsRef ref = resolveLhsRef(lhs.ast.data());
    if (ref.name.isEmpty()) return;
    writeLhs(ref, value);
}

// STRDATA <字符串变量>：把被选中段的文本写进变量（不显示）
void ExecutionEngine::assignPrintDataString(const Operand& lhs, const QString& value) {
    // 值已经是**求值后的字符串**，不能再当表达式文本走 handleStringAssignment
    // （裸文本会被解析成变量名 -> 0）。与 SPLIT 一样直接用 writeStringValue。
    writeStringValue(lhs.ast.data(), value);
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

bool ExecutionEngine::handleStringAssignment(const ExpressionNode* lhs, const QString& rhs,
                                             const QSharedPointer<ExpressionNode>& ast,
                                             const QString& ownerFunction) {
    const LhsRef ref = resolveLhsRef(lhs);
    if (ref.name.isEmpty()) {
        return false;
    }
    ExpressionEvaluator& evaluator = getEvaluator();
    Q_UNUSED(rhs);
    if (!ast) { emit errorOccurred(QStringLiteral("字符串赋值缺少装载期 AST")); return false; }
    const QVariant result = evaluator.evaluate(*ast, m_storage, m_gameBaseData);
    if (!result.isValid() || !isStringValue(result)) return false;
    const QString value = stringText(result);
    return writeStructuredValue(ref, result, ownerFunction);
}

// 把**已求值**的字符串写入左值（从 handleStringAssignment 抽出，供 SPLIT 等
// 「值不是表达式文本」的调用方复用 —— SPLIT 的分割结果若再当表达式求值，
// 裸文本 "x" 会被解析成变量 x -> 0）。
bool ExecutionEngine::writeStructuredValue(const LhsRef& ref, const QVariant& value, const QString& ownerFunction) {
    Q_UNUSED(ownerFunction);
    if (ref.name.isEmpty() || !ref.valid) return false;
    if (m_storage->hasParameter(ref.name)) { m_storage->setParameter(ref.name, value); return true; }
    return m_storage->setStringSlot(ref.name, ref.indices, flatString(value));
}

bool ExecutionEngine::writeStringValue(const ExpressionNode* lhs, const QString& value,
                                      const QString& ownerFunction) {
    return writeStringValue(resolveLhsRef(lhs), value, ownerFunction);
}

bool ExecutionEngine::writeStringValue(const LhsRef& ref, const QString& value,
                                      const QString& ownerFunction) {
    return writeStructuredValue(ref, QVariant(value), ownerFunction);
}

bool ExecutionEngine::handleAssignment(const ExpressionNode* lhs, const QString& rhs, const QSharedPointer<ExpressionNode>& ast,
                                       const QList<QSharedPointer<ExpressionNode>>& nodes,
                                       const QString& ownerFunction) {
    Q_UNUSED(rhs);
    ExpressionEvaluator& evaluator = getEvaluator();
    const auto expressions = nodes.isEmpty() ? QList<QSharedPointer<ExpressionNode>>{ast} : nodes;
    QList<qint64> values;
    for (const auto& node : expressions) {
        const QVariant value = node ? evaluator.evaluate(*node, m_storage, m_gameBaseData) : QVariant();
        if (!value.isValid() || isStringValue(value)) return false;
        values.append(value.toLongLong());
    }
    LhsRef ref = resolveLhsRef(lhs);
    if (!ref.valid) return false;
    // 声明查询带作用域：函数内的同名私有变量优先于广域 CONST（否则 `#DIM SHADOW`
    // 会被广域的 `#DIM CONST SHADOW` 判成常量，赋值被丢弃且变量永远读不到写入）。
    const auto* decl = m_parseTable
        ? m_parseTable->variableTable().find(ref.name, ownerFunction) : nullptr;
    const int start = ref.indices.isEmpty() ? 0 : ref.indices.last();
    int size = decl && !decl->lengths.isEmpty() ? decl->lengths.last() : m_storage->arraySize(ref.name);
    if (values.size() > 1 && size > 0 && (start < 0 || qint64(start)+values.size() > size)) {
        emit errorOccurred(QStringLiteral("连续数组赋值越界")); return false;
    }
    if (decl && decl->isConst) { emit errorOccurred(QStringLiteral("不能赋值const变量")); return false; }
    if (ref.indices.isEmpty()) ref.indices.append(0);
    for (int i = 0; i < values.size(); ++i) {
        ref.indices.last() = start+i;
        writeLhs(ref, values[i]);
    }
    return true;
}

// PRINT 族统一实现（对齐 C# GameProc/Function/Instraction.Child.cs PRINT_Instruction）
//
//   mode == Literal        ：操作数就是「命令名之后到行尾的原文」（含尾随空白！）
//   mode == PrintV         ：多个值直接拼接（不补分隔符）
//   mode == StrExpression  ：求一个字符串表达式
//   mode == FormStr        ：求一个 StrForm（文本 + {…}/%…%），装载期已建好节点
//   forms  （FORMS 后缀）  ：装载期分片，变量读取为独立 AST 运算
//   clearPad（C/LC 后缀）  ：按 PRINTC 的定宽列布局补齐后打印
//   newline L/W            ：打印后换行（W 还要等一次按键）
bool ExecutionEngine::handlePrintInstruction(const LogicalLine& line) {
    const QString& name = line.functionName;
    const QList<Operand>& args = line.arguments;
    const AstBuilder::PrintArgInfo info = AstBuilder::printInfo(name);
    if (info.mode == AstBuilder::PrintArgMode::NotPrint) {
        return false;
    }
    // SKIPDISP 1：跳过显示（C# PRINT_Instruction.DoInstruction 的 skipPrint 早退，
    // 连 W 后缀的按键等待一并跳过）
    if (m_skipDisp) {
        return true;
    }

    ExpressionEvaluator& evaluator = getEvaluator();

    const auto valueOf = [&](const Operand& a) -> QString {
        if (a.ast) {
            return stringText(evaluator.evaluate(*a.ast, m_storage, m_gameBaseData));
        }
        return a.raw;
    };

    QString text;
    // 普通打印变量直接来自 Operand.ast；模板片段若已生成则优先按片段求值。
    if (line.printTemplate && !line.printTemplate->parts.isEmpty()) {
        for (const CompiledPrintTemplatePart& part : line.printTemplate->parts) {
            if (part.kind == PrintTemplatePart::Kind::Text) text += part.text;
            else if (part.kind == PrintTemplatePart::Kind::Expression && part.expression)
                text += stringText(evaluator.evaluate(*part.expression, m_storage, m_gameBaseData));
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

    if (info.clearPad) {
        emit consolePrint(padPrintC(text, info.padLeft), false);
    } else if (info.single) {
        // PRINTSINGLE 系：C# EmueraConsole.PrintSingleLine —— 作为**一行**输出，
        // 隐式换行；且不吃 L/W 的等键语义（OutputToConsole 的 SINGLE 分支直接
        // 早退）。此前按普通 PRINT 处理（不换行），eraTW 的
        // `PRINTSINGLEFORMS "== %MAPNAME% " + …` 信息行会与下一行粘连，
        // PANIMATION 的滚动文本也会挤在同一行。
        emit consolePrint(text, true);
    } else {
        emit consolePrint(text, info.newline);
    }
    // W 后缀（PRINTW / PRINTFORMW / …）：换行后再等一次任意键（C# PRINT_WAITINPUT）
    if (info.waitInput && !info.single) {
        emit requestAnyKey();
        m_printWaitKey = true;   // runner 在指令执行后消费并挂起（C# ReadAnyKey 阻塞）
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

void ExecutionEngine::clearForReload() {
    m_saveSnapshots.clear();
    m_globalSnapshot = {};
    m_globalSnapshotValid = false;
    m_variableSnapshots.clear();
    m_currentScript.clear();
    m_currentLine = 0;
    m_executionPosition = 0;
    m_running = false;
    m_totalInstructionsExecuted = 0;
    m_skipDisp = false;
    m_mesSkip = false;
    m_printWaitKey = false;
}

bool ExecutionEngine::handleResetData() {
    // 对齐 C# VariableEvaluator.ResetData：
    //   SetDefaultValue(全部变量回默认) / SetDefaultLocalValue / CharacterList.Clear()
    // 角色列表必须清空 —— 否则 RESETDATA 之后残留旧角色（eraTW 新开游戏时
    // 会出现「继承上一周目角色」的现象）。此前只清了角色列表，内建整型数组
    // （FLAG/DAY/…）、用户广域变量、角色运行时数据全部残留。
    if (m_storage) {
        m_storage->resetForNewGame();
        GraphicsStore::clearAll();
    }
    qDebug() << "[RESETDATA] 变量与角色列表已重置";
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
bool ExecutionEngine::decodeVariableSnapshot(const QByteArray& data, VariableSnapshot& snapshot) {
    return EraBinaryData::decode(data,snapshot) && snapshot.type==EraBinaryData::FileType::Var;
}

void ExecutionEngine::decodeGlobalSnapshot(const QByteArray& data) {
    EraBinaryData::Document schema; schema.type=EraBinaryData::FileType::Global;
    m_globalSnapshotValid = (EraBinaryData::decode(data,m_globalSnapshot) || EraBinaryData::decodeText(data,schema,m_globalSnapshot))
        && m_globalSnapshot.type==EraBinaryData::FileType::Global;
}

void ExecutionEngine::reloadSaveSnapshots() {
    m_saveSnapshots.clear(); m_globalSnapshot = {}; m_globalSnapshotValid=false; m_variableSnapshots.clear();
    if (m_gameDataDir.isEmpty()) return;
    const QString directory = GamePaths::join(m_gameDataDir, QStringLiteral("sav"));
    for (const QString& name : QDir(directory).entryList(QStringList{QStringLiteral("save*.sav")}, QDir::Files)) {
        bool ok = false;
        const qint64 index = name.mid(4, name.size() - 8).toLongLong(&ok);
        if (!ok || index < 0) continue;
        QFile file(GamePaths::join(directory, name));
        EraBinaryData::Document snapshot;
        bool decoded=false;
        if(file.open(QIODevice::ReadOnly)) { const auto bytes=file.readAll(); EraBinaryData::Document schema;
            decoded=EraBinaryData::decode(bytes,snapshot) || EraBinaryData::decodeText(bytes,schema,snapshot); }
        if (decoded && snapshot.type==EraBinaryData::FileType::Normal)
            m_saveSnapshots.insert(index, std::move(snapshot));
        else qWarning().noquote() << "[save] 无法读取或不支持的存档，视为无数据，可覆盖：" << file.fileName();
    }
    // QDir::relativeFilePath() cannot recover a relative name from SAF document
    // URIs: document IDs are opaque and providers may return a different URI for
    // each child. Keep the logical key while recursing, and only use the URI for I/O.
    std::function<void(const QString&, const QString&)> loadVariables =
        [&](const QString& uri, const QString& relative) {
            const QDir dir(uri);
            const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Hidden
                | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo& info : files) {
                const QString path = GamePaths::join(uri, info.fileName());
                QFile file(path);
                VariableSnapshot snapshot;
                const QString key = relative.isEmpty() ? info.fileName()
                    : relative + QLatin1Char('/') + info.fileName();
                if (info.fileName().startsWith("save") && info.fileName().endsWith(".sav")) continue;
                if (info.fileName() == "global.sav") continue;
                if (file.open(QIODevice::ReadOnly) && decodeVariableSnapshot(file.readAll(), snapshot))
                    m_variableSnapshots.insert(QDir::cleanPath(key), std::move(snapshot));
                else if (info.suffix().isEmpty())
                    qWarning().noquote() << "[save] 无法读取或不支持的变量存档，视为无数据，可覆盖：" << path;
            }
            const QStringList dirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QString& child : dirs) {
                const QString childUri = GamePaths::join(uri, child);
                const QString childKey = relative.isEmpty() ? child
                    : relative + QLatin1Char('/') + child;
                loadVariables(childUri, childKey);
            }
        };
    loadVariables(directory, {});
    QFile global(GamePaths::join(m_gameDataDir, QStringLiteral("sav/global.sav")));
    if (global.open(QIODevice::ReadOnly)) {
        decodeGlobalSnapshot(global.readAll());
        if (!m_globalSnapshotValid) {
            m_globalSnapshot = {};
            qWarning().noquote() << "[save] 无法读取或不支持的存档，视为无数据，可覆盖：" << global.fileName();
        }
    }
}

int ExecutionEngine::checkSaveSnapshot(qint64 index) const {
    const auto it=m_saveSnapshots.constFind(index);
    if(it==m_saveSnapshots.constEnd()) return 1;
    if(m_gameBaseData && it->uniqueCode!=0 && it->uniqueCode!=m_gameBaseData->scriptUniqueCode()) return 2;
    if(m_gameBaseData && !m_gameBaseData->acceptsSave(it->uniqueCode,it->scriptVersion)) return 3;
    return 0;
}

bool ExecutionEngine::loadSaveSnapshot(qint64 index) {
    if(!m_storage || checkSaveSnapshot(index)!=0) return false;
    const auto& snapshot=m_saveSnapshots.value(index);
    m_storage->applyBinarySave(snapshot);
    m_storage->setSystemVariable(QStringLiteral("LASTLOAD_NO"),0,index);
    m_storage->setSystemVariable(QStringLiteral("LASTLOAD_VERSION"),0,snapshot.scriptVersion);
    m_storage->setGlobalStr1D(QStringLiteral("LASTLOAD_TEXT"),0,snapshot.description);
    emit gameStateLoaded();
    return true;
}

bool ExecutionEngine::writeSaveSnapshot(qint64 index,const QString& description) {
    if(!m_storage || m_gameDataDir.isEmpty() || index<0 || index>INT_MAX || description.contains('\n') || description.contains('\r')) return false;
    auto snapshot=m_storage->captureBinarySave(EraBinaryData::FileType::Normal);
    snapshot.description=description; snapshot.uniqueCode=globalUniqueCode();
    snapshot.scriptVersion=m_gameBaseData ? m_gameBaseData->scriptVersion() : 0;
    const auto format=m_saveFormatProvider ? m_saveFormatProvider() : QPair<bool,bool>{true,false};
    QByteArray bytes; QString error;
    const bool encoded=format.first ? EraBinaryData::encode(snapshot,bytes,&error,format.second) : EraBinaryData::encodeText(snapshot,bytes,&error);
    if(!encoded) { emit errorOccurred(error); return false; }
    const QString directory=GamePaths::join(m_gameDataDir,QStringLiteral("sav")); QDir().mkpath(directory);
    QSaveFile file(GamePaths::join(directory,QStringLiteral("save%1.sav").arg(index,2,10,QLatin1Char('0'))));
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) return false;
    m_saveSnapshots.insert(index,std::move(snapshot)); return true;
}

bool ExecutionEngine::handleLoadGlobal() {
    const bool ok=m_storage && m_globalSnapshotValid && (!m_gameBaseData || m_gameBaseData->acceptsSave(m_globalSnapshot.uniqueCode,m_globalSnapshot.scriptVersion));
    if(ok) m_storage->applyBinarySave(m_globalSnapshot);
    if(m_storage) m_storage->setSystemVariable(QStringLiteral("RESULT"),0,ok ? 1 : 0);
    return true;
}

bool ExecutionEngine::handleSaveGlobal() {
    if(!m_storage || m_gameDataDir.isEmpty()) return true;
    auto captured=m_storage->captureBinarySave(EraBinaryData::FileType::Global);
    captured.uniqueCode=globalUniqueCode(); captured.scriptVersion=m_gameBaseData ? m_gameBaseData->scriptVersion() : 0;
    QByteArray bytes;
    const QString directory=GamePaths::join(m_gameDataDir,QStringLiteral("sav")); QDir().mkpath(directory);
    QSaveFile file(GamePaths::join(directory,QStringLiteral("global.sav")));
    const auto format=m_saveFormatProvider ? m_saveFormatProvider() : QPair<bool,bool>{true,false};
    const bool encoded=format.first ? EraBinaryData::encode(captured,bytes,nullptr,format.second) : EraBinaryData::encodeText(captured,bytes);
    const bool ok=encoded && file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit();
    if(ok) { m_globalSnapshot=std::move(captured); m_globalSnapshotValid=true; }
    else emit errorOccurred(QStringLiteral("SAVEGLOBAL 写入失败"));
    return true;
}

qint64 ExecutionEngine::globalUniqueCode() const { return m_gameBaseData ? m_gameBaseData->scriptUniqueCode() : 0; }

void ExecutionEngine::setError(const QString& message) {
    qDebug() << "Execution error:" << message;
    m_running = false;
    emit errorOccurred(message);
}

// Receive instruction from ParseTable
// Handle jump request from ParseTable
// Handle memory space change from ParseTable

// ---------------------------------------------------------------------------
// 存档系私有方法（SAVEDATA/LOADDATA/DELDATA/CHKDATA 的实现）
// 对齐 C# FunctionCode.SAVEDATA/LOADDATA/DELDATA + SpSaveDataArgument +
// VariableEvaluator.SaveTo/LoadFrom：文件名 save{##}.sav（C# 1746）；
// 存档采用C#1808协议；格式配置由宿主注入。
// ---------------------------------------------------------------------------
void ExecutionEngine::handleSaveData(const LogicalLine& line)
{
    QList<Operand> parts;
    for (const Operand& op : saveCommandOperands(line)) {
        // 只跳过「标点逗号」——引号字面量 ","（分隔符实参）的 raw 也是 ","，
        // 但 isString 为真，必须保留（否则 SPLIT "a,b", ",", ARR 的分隔符丢失）。
        if (!op.isString && op.raw == QLatin1String(",")) continue;
        parts.append(op);
    }
    if (parts.size() < 2) return;
    ExpressionEvaluator& ev = getEvaluator();
    const auto evalOp = [&](const Operand& op) -> QVariant {
        // 整段引号字面量（如 SAVEDATA 40, "标题"）：raw 已是去引号后的文本，
        // 且无 AST；若再当表达式求值会把中文标题当未定义标识符 -> 0。
        if (op.isString) return QVariant(op.raw);
        return op.ast ? ev.evaluate(*op.ast, m_storage, m_gameBaseData)
                      : throw std::runtime_error("Operand has no load-time AST");
    };
    const qint64 idx = evalOp(parts[0]).toLongLong();
    const QString saveText = stringText(evalOp(parts[1]));
    if (idx < 0) {
        m_state.setErrorState();
        emit errorOccurred(QStringLiteral("SAVEDATAの引数に負の値(%1)が指定されました").arg(idx));
        return;
    }
    if (saveText.contains(QLatin1Char('\n'))) {
        m_state.setErrorState();
        emit errorOccurred(QStringLiteral("SAVEDATAのセーブテキストに改行文字が与えられました"));
        return;
    }
    if (writeSaveSnapshot(idx, saveText)) {
        m_storage->setSystemStr(QStringLiteral("SAVEDATA_TEXT"), 0, saveText);
    } else {
        setError(QStringLiteral("SAVEDATA 写入失败"));
    }
}

void ExecutionEngine::handleLoadData(const LogicalLine& line)
{
    const QList<Operand> operands = saveCommandOperands(line);
    if (operands.isEmpty()) return;
    ExpressionEvaluator& ev = getEvaluator();
    const Operand& op = operands.first();
    const qint64 idx = op.ast ? ev.evaluate(*op.ast, m_storage, m_gameBaseData).toLongLong()
                              : evaluateOperand(ev, op, m_storage, m_gameBaseData).toLongLong();
    if (idx < 0 || idx > INT_MAX || !loadSaveSnapshot(idx)) {
        m_state.setErrorState();
        setError(QStringLiteral("LOADDATA 无效或无法加载的存档 %1").arg(idx));
    }
}

void ExecutionEngine::handleDelData(const LogicalLine& line)
{
    const QList<Operand> operands = saveCommandOperands(line);
    if (operands.isEmpty()) return;
    ExpressionEvaluator& ev = getEvaluator();
    const Operand& op = operands.first();
    const qint64 idx = op.ast ? ev.evaluate(*op.ast, m_storage, m_gameBaseData).toLongLong()
                              : evaluateOperand(ev, op, m_storage, m_gameBaseData).toLongLong();
    if (idx < 0 || idx > INT_MAX) { setError(QStringLiteral("DELDATA 无效编号")); return; }
    const QString path = GamePaths::join(m_gameDataDir, QStringLiteral("sav/save%1.sav").arg(idx, 2, 10, QLatin1Char('0')));
    if (QFile::exists(path) && !QFile::remove(path)) { setError(QStringLiteral("DELDATA 删除失败")); return; }
    m_saveSnapshots.remove(idx);
}

// 存档系私有方法（SAVEDATA/LOADDATA/DELDATA/CHKDATA 的实现）
// ---------------------------------------------------------------------------
// `CHKDATA L_I` / `SAVEDATA L_IDX, "标题"` 有两种行形态：
//   · 语句形态：line.arguments 就是实参（Operand 列表，含 raw / isString）；
//   · 调用形态：整行被归约为 Function(NAME, 实参…)（「语句形式的内部函数」，
//     见 executeFunctionCall），line.arguments 只有一个「整条调用」。
// 这里把调用形态拆回 Operand 列表（AST 取自 FunctionNode::arguments）。
// 此前各 handle* 直接读 line.arguments：调用形态下会把「整条调用」当实参求值 ——
// CHKDATA 变成 CHKDATA(CHKDATA(L_I))，实参恒为「0/1 的存在判定」，eraH 的读档
// 界面因此把每个槽位都报成「存在」（[N] ＯＫ），选中空档位去 LOADDATA 才失败。
QList<Operand> ExecutionEngine::saveCommandOperands(const LogicalLine& line) const
{
    if (line.arguments.size() != 1) return line.arguments;
    const Operand& first = line.arguments.first();
    if (!first.ast || first.ast->kind() != NodeKind::Function) return line.arguments;
    const auto& call = static_cast<const FunctionNode&>(*first.ast);
    if (call.name().compare(line.functionName, Qt::CaseInsensitive) != 0) return line.arguments;
    QList<Operand> out;
    for (const QSharedPointer<ExpressionNode>& arg : call.arguments()) {
        Operand op;
        op.ast = arg;                    // raw 留空：求值一律走 AST
        out.append(op);
    }
    return out;
}

void ExecutionEngine::handleChkData(const LogicalLine& line)
{
    ExpressionEvaluator& ev = getEvaluator();
    const QList<Operand> operands = saveCommandOperands(line);
    const Operand& op = operands.isEmpty() ? Operand() : operands.first();
    const qint64 idx = op.ast ? ev.evaluate(*op.ast, m_storage, m_gameBaseData).toLongLong()
                              : evaluateOperand(ev, op, m_storage, m_gameBaseData).toLongLong();
    const bool exists = checkSaveSnapshot(idx) == 0;
    // 对齐 C# CheckdataMethod：RESULT = EraDataState（0=OK / 1=FILENOTFOUND），
    // RESULTS = 状态说明文本。
    if (m_storage) {
        m_storage->setSystemVariable(QStringLiteral("RESULT"), 0, checkSaveSnapshot(idx));
        m_storage->setGlobalStr1D(QStringLiteral("RESULTS"), 0,
            exists ? QStringLiteral("ＯＫ") : QStringLiteral("ファイルが存在しません"));
    }
    qDebug() << "[save] CHKDATA" << idx << (exists ? "存在" : "不存在")
             << "行" << line.position.toString();
}

// ---------------------------------------------------------------------------
// 状态打印 / 角色整理 / 变量存档族（此前未实现、运行期被忽略的指令集合）
//
//   UPCHECK / PRINT_ABL / PRINT_TALENT / PRINT_MARK / PRINT_EXP / PRINT_PALAM /
//   PRINT_ITEM / PRINT_SHOPITEM / HTML_TAGSPLIT / SORTCHARA / SAVEVAR / LOADVAR
//
// 语义对齐 C# Process.ScriptProc.cs:174-262（PRINT_* / UPCHECK）、
// VariableEvaluator（UpdateInUpcheck / GetCharacterDataString /
// GetCharacterParamString / GetHavingItemsString）、Instraction.Child.cs
// （HTML_TAGSPLIT / SORTCHARA）。
// ---------------------------------------------------------------------------
namespace {

// 操作数整理：剔除「标点逗号」（引号字面量 "," 的 raw 同为逗号但 isString 为真）
QList<const Operand*> statusOperands(const LogicalLine& line) {
    QList<const Operand*> ops;
    ops.reserve(line.arguments.size());
    for (const Operand& a : line.arguments) {
        if (a.isString || a.raw != QLatin1String(",")) ops.append(&a);
    }
    return ops;
}

} // namespace

bool ExecutionEngine::handleStatusCommand(const LogicalLine& line)
{
    if (!m_storage) return true;
    const QString& name = line.functionName;
    ExpressionEvaluator& ev = getEvaluator();
    const QList<const Operand*> ops = statusOperands(line);
    const auto evalOp = [&](const Operand* op) -> QVariant {
        if (!op) return {};
        if (op->isString) return QVariant(op->raw);
        return op->ast ? ev.evaluate(*op->ast, m_storage, m_gameBaseData)
                       : throw std::runtime_error("Operand has no load-time AST");
    };
    // C# PRINT_PALAM / PRINT_SHOPITEM 的每行列数（Config.PrintCPerLine）
    const int perLine = ev.printCLayout().second;

    // ---- UPCHECK：全局 UP/DOWN -> TARGET 的 PALAM（C# UpdateInUpcheck）----
    if (name == QLatin1String("UPCHECK")) {
        const qint64 target = m_storage->getTarget(0);
        const int length = std::min({variableLength1D("PALAM", QString()),
                                     variableLength1D("UP", QString()),
                                     variableLength1D("DOWN", QString())});
        if (target >= 0 && target < m_storage->charaNum()) {
            for (int i = 0; i < length; ++i) {
                const qint64 up = m_storage->getUp(i);
                const qint64 down = m_storage->getDown(i);
                if (up <= 0 && down <= 0) continue;
                const qint64 param = m_storage->getCharaInt("PALAM", static_cast<int>(target), i);
                QString text;
                if (!m_skipDisp) {
                    text = m_storage->getGlobalStr1D("PALAMNAME", i) + QLatin1Char(' ')
                           + QString::number(param);
                    if (up > 0) text += QLatin1Char('+') + QString::number(up);
                    if (down > 0) text += QLatin1Char('-') + QString::number(down);
                }
                const qint64 updated = param + up - down;   // C# unchecked：负值同样参与
                m_storage->setCharaInt("PALAM", static_cast<int>(target), i, updated);
                if (!m_skipDisp) {
                    emit consolePrint(text + QLatin1Char('=') + QString::number(updated), true);
                }
            }
        }
        for (int i = 0; i < length; ++i) {
            m_storage->setUp(i, 0);
            m_storage->setDown(i, 0);
        }
        return true;
    }

    // ---- PRINT_ABL / PRINT_TALENT / PRINT_MARK / PRINT_EXP <角色编号> ----
    //   （C# GetCharacterDataString：非 0 项按 CSV 名表拼串）
    if (name == QLatin1String("PRINT_ABL") || name == QLatin1String("PRINT_TALENT")
        || name == QLatin1String("PRINT_MARK") || name == QLatin1String("PRINT_EXP")) {
        if (ops.isEmpty()) return true;   // 无实参（冒烟脚本）：静默
        const qint64 target = evalOp(ops.first()).toLongLong();
        if (target < 0 || target >= m_storage->charaNum()) {
            m_state.setErrorState();
            emit errorOccurred(QStringLiteral("存在しない登録キャラクタを参照しようとしました（%1 %2）")
                                   .arg(name).arg(target));
            return true;
        }
        const bool isTalent = name == QLatin1String("PRINT_TALENT");
        const bool withLv = name != QLatin1String("PRINT_EXP");
        const QString varName = name.mid(6);              // ABL / TALENT / MARK / EXP
        const QString nameVar = varName + QLatin1String("NAME");
        const int count = std::min(variableLength1D(varName, QString()),
                                   m_storage->arraySize(nameVar));
        QString text;
        for (int i = 0; i < count; ++i) {
            const qint64 v = m_storage->getCharaInt(varName, static_cast<int>(target), i);
            const QString n = m_storage->getGlobalStr1D(nameVar, i);
            if (v == 0 || n.isEmpty()) continue;
            if (isTalent) text += QLatin1Char('[') + n + QLatin1Char(']');
            else text += n + (withLv ? QLatin1String("LV") : QString())
                       + QString::number(v) + QLatin1Char(' ');
        }
        if (!m_skipDisp) emit consolePrint(text, true);
        return true;
    }

    // ---- PRINT_PALAM <角色编号>（C# GetCharacterParamString：PALAMLV 条形图）----
    if (name == QLatin1String("PRINT_PALAM")) {
        if (ops.isEmpty()) return true;
        const qint64 target = evalOp(ops.first()).toLongLong();
        if (target < 0 || target >= m_storage->charaNum()) {
            m_state.setErrorState();
            emit errorOccurred(QStringLiteral("存在しない登録キャラクタを参照しようとしました（%1 %2）")
                                   .arg(name).arg(target));
            return true;
        }
        // 100 以降は否定の珠とかなので表示しない（C# 注释原样）
        const int count = std::min(100, m_storage->arraySize("PALAMNAME"));
        int printed = 0;
        for (int i = 0; i < count; ++i) {
            const qint64 param = m_storage->getCharaInt("PALAM", static_cast<int>(target), i);
            const QString pname = m_storage->getGlobalStr1D("PALAMNAME", i);
            if (param == 0 && pname.isEmpty()) continue;
            qint64 border = m_storage->getPalamlv(1);
            QChar c = QLatin1Char('-');
            if (param >= border) { c = QLatin1Char('='); border = m_storage->getPalamlv(2); }
            if (param >= border) { c = QLatin1Char('>'); border = m_storage->getPalamlv(3); }
            if (param >= border) { c = QLatin1Char('*'); border = m_storage->getPalamlv(4); }
            QString bar;
            if (border <= 0 || param >= border) bar.fill(c, 10);
            else if (param <= 0) bar.fill(QLatin1Char('.'), 10);
            else {
                const int fill = static_cast<int>(param * 10 / border);
                bar = QString(fill, c) + QString(10 - fill, QLatin1Char('.'));
            }
            const QString printStr = pname + QLatin1Char('[') + bar + QLatin1Char(']')
                                   + QString::number(param).rightJustified(6);
            if (!m_skipDisp) {
                emit consolePrint(padPrintC(printStr, true), false);
                ++printed;
                if (perLine > 0 && printed % perLine == 0) emit consolePrint(QString(), true);
            }
        }
        return true;
    }

    // ---- PRINT_ITEM（C# GetHavingItemsString：ITEM × ITEMNAME）----
    if (name == QLatin1String("PRINT_ITEM")) {
        const int count = std::min(variableLength1D("ITEM", QString()),
                                   m_storage->arraySize("ITEMNAME"));
        QString text = QStringLiteral("所持アイテム：");
        int owned = 0;
        for (int i = 0; i < count; ++i) {
            const qint64 v = m_storage->getItem(i);
            if (v == 0) continue;
            ++owned;
            const QString n = m_storage->getGlobalStr1D("ITEMNAME", i);
            if (!n.isEmpty()) text += n;
            text += QLatin1Char('(') + QString::number(v) + QLatin1String(") ");
        }
        if (owned == 0) text += QStringLiteral("なし");
        if (!m_skipDisp) emit consolePrint(text, true);
        return true;
    }

    // ---- PRINT_SHOPITEM（C# ScriptProc：ITEMSALES × ITEMNAME/ITEMPRICE）----
    if (name == QLatin1String("PRINT_SHOPITEM")) {
        const int count = std::min({variableLength1D("ITEMSALES", QString()),
                                    variableLength1D("ITEMNAME", QString()),
                                    variableLength1D("ITEMPRICE", QString())});
        const QString label = ev.moneyLabel();
        const bool moneyFirst = ev.moneyFirst();
        int printed = 0;
        for (int i = 0; i < count; ++i) {
            if (m_storage->getItemsales(i) == 0) continue;
            const QString n = m_storage->getGlobalStr1D("ITEMNAME", i);
            // ITEMPRICE 是常量容器（不在 m_globalInt1D 里）：走系统变量通道
            const QString price = QString::number(m_storage->getSystemVariable(QStringLiteral("ITEMPRICE"), i));
            // 1.52a改変部分：MoneyFirst 对应金额单位前置 / 后置
            const QString printStr = moneyFirst
                ? QStringLiteral("[%1] %2(%3%4)").arg(i).arg(n, label, price)
                : QStringLiteral("[%1] %2(%3%4)").arg(i).arg(n, price, label);
            if (!m_skipDisp) {
                emit consolePrint(padPrintC(printStr, true), false);
                ++printed;
                if (perLine > 0 && printed % perLine == 0) emit consolePrint(QString(), true);
            }
        }
        return true;
    }

    // ---- HTML_TAGSPLIT <字符串>[, <数组变量>, <个数变量>] ----
    //   （C# HtmlManager.HtmlTagSplit：按标签切成 [文本, <tag>, …]；
    //     未闭合 '<' -> 个数变量写 -1）
    if (name == QLatin1String("HTML_TAGSPLIT")) {
        if (ops.isEmpty()) return true;
        const Operand* first = ops.first();
        // 实参可以是引号字面量 / 表达式；裸 HTML 文本（<b>～</b>）不是合法
        // 表达式 —— 求值失败时按原文取用（对齐 ecd/docs 的示例写法）
        QString str;
        if (first->isString) str = first->raw;
        else if (first->ast) str = stringText(ev.evaluate(*first->ast, m_storage, m_gameBaseData));
        else {
            emit errorOccurred(QStringLiteral("HTML_TAGSPLIT 的字符串参数缺少装载期 AST"));
            return true;
        }
        QStringList parts;
        bool ok = true;
        int pos = 0;
        while (pos < str.size()) {
            const int lt = str.indexOf(QLatin1Char('<'), pos);
            if (lt < 0) { parts.append(str.mid(pos)); break; }
            if (lt > pos) { parts.append(str.mid(pos, lt - pos)); pos = lt; }
            const int gt = str.indexOf(QLatin1Char('>'), pos);
            if (gt < 0) { ok = false; break; }
            parts.append(str.mid(pos, gt + 1 - pos));
            pos = gt + 1;
        }
        // C# defaults are RESULT (count) and RESULTS (parts).  The source
        // operand order is string, destination array, optional count.
        LhsRef count;
        count.name = QStringLiteral("RESULT");
        count.valid = true;
        LhsRef target;
        target.name = QStringLiteral("RESULTS");
        target.valid = true;
        if (ops.size() >= 2 && !ops.at(1)->raw.trimmed().isEmpty()) {
            const auto* ast = ops.at(1)->ast.data();
            if (!ast || ast->kind() != NodeKind::Variable) {
                (void)resolveLhsRef(ast); // emit the standard missing-LHS diagnostic
                return false;
            }
            // C# retains only VariableToken: destination indices are ignored.
            target.name = static_cast<const VariableNode&>(*ast).name();
        }
        if (ops.size() >= 3 && !ops.at(2)->raw.trimmed().isEmpty()) {
            count = resolveLhsRef(ops.at(2)->ast.data());
            if (!count.valid) return false;
        }
        if (!ok) {
            writeLhs(count, -1);
            return true;
        }

        // C# Array.Copy starts at zero, regardless of the destination's
        // supplied subscript, and leaves unused destination elements intact.
        const auto* destinationDecl = m_parseTable
            ? m_parseTable->variableTable().find(target.name, line.ownerFunction) : nullptr;
        const int configured = m_storage->variableConfig().getSize1D(target.name.toUpper());
        const int capacity = destinationDecl && !destinationDecl->lengths.isEmpty()
            ? destinationDecl->lengths.first()
            : qMax(configured, m_storage->arraySize(target.name));
        const bool localArray = target.name == QLatin1String("LOCALS") || target.name == QLatin1String("ARGS");
        const int writable = capacity > 0 && !localArray
            ? qMin(static_cast<int>(parts.size()), capacity)
            : static_cast<int>(parts.size());
        writeLhs(count, static_cast<qint64>(parts.size()));
        for (int k = 0; k < writable; ++k) {
            target.indices = {k};
            if (!writeStringValue(target, parts.at(k))) return false;
        }
        return true;
    }

    // ---- SORTCHARA [<角色变量>[, FORWARD|BACK]] ----
    //   （C# SP_SORTCHARA_ArgumentBuilder + VariableEvaluator.SortChara：
    //     默认按 NO:0 升序；FORWARD=升序 / BACK=降序；MASTER 位置固定，
    //     排序后重映射 TARGET / ASSI）
    if (name == QLatin1String("SORTCHARA")) {
        const int n = m_storage->charaNum();
        if (n <= 1) return true;
        QString keyName = QStringLiteral("NO");
        QList<int> keyIdx;
        bool ascending = true;
        QList<const Operand*> rest = ops;
        for (const Operand* op : ops) {
            const QString raw = op->raw.trimmed().toUpper();
            if (raw == QLatin1String("FORWARD") || raw == QLatin1String("BACK")) {
                ascending = raw == QLatin1String("FORWARD");
                rest.removeOne(op);
            }
        }
        if (!rest.isEmpty() && !rest.first()->raw.trimmed().isEmpty()) {
            const LhsRef ref = resolveLhsRef(rest.first()->ast.data());
            if (!ref.name.isEmpty()) {
                keyName = ref.name;
                keyIdx = ref.indices;
            }
        }
        if (!m_storage->isCharaDataVariable(keyName)) {
            m_state.setErrorState();
            emit errorOccurred(QStringLiteral("SORTCHARA 的第 1 参数必须是角色变量：%1").arg(keyName));
            return true;
        }
        const bool keyIsStr = m_storage->isCharaDataString(keyName);
        const qint64 oldTarget = m_storage->getTarget(0);
        const qint64 oldAssi = m_storage->getAssi(0);
        // MASTER 不参与排序（C# SortChara fixMaster=true）
        const qint64 master = m_storage->getMaster(0);
        const int masterPos = (master >= 0 && master < n) ? static_cast<int>(master) : -1;
        QList<int> ordered;   // 注意：Qt 把 slots 定义为宏，变量名不能用 slots
        ordered.reserve(n);
        for (int i = 0; i < n; ++i) {
            if (i != masterPos) ordered.append(i);
        }
        const int e0 = keyIdx.value(0, 0);
        const auto keyValue = [&](int slot) -> QPair<QString, qint64> {
            if (keyIsStr) return {m_storage->getCharaStr(keyName, slot, e0), 0};
            if (keyIdx.size() >= 2)
                return {QString(), m_storage->getCharaInt3D(keyName, slot, e0, keyIdx.at(1))};
            return {QString(), m_storage->getCharaInt(keyName, slot, e0)};
        };
        std::stable_sort(ordered.begin(), ordered.end(), [&](int a, int b) {
            const auto ka = keyValue(a);
            const auto kb = keyValue(b);
            int ret;
            if (keyIsStr) ret = ka.first.compare(kb.first);
            else ret = ka.second < kb.second ? -1 : (ka.second > kb.second ? 1 : 0);
            if (!ascending) ret = -ret;
            return ret < 0;   // 同值保持原顺序（C# 以 temp_CurrentOrder 决胜）
        });
        // 应用置换（desired[位置] = 应落位的原槽位；selection-swap，MASTER 不动）
        QList<int> desired(n);
        {
            int p = 0;
            for (int i = 0; i < n; ++i) desired[i] = (i == masterPos) ? i : ordered.at(p++);
        }
        QList<int> current(n);
        for (int i = 0; i < n; ++i) current[i] = i;   // current[位置] = 当前所在原槽位
        for (int pos = 0; pos < n; ++pos) {
            if (current.at(pos) == desired.at(pos)) continue;
            int j = pos + 1;
            while (j < n && current.at(j) != desired.at(pos)) ++j;
            if (j >= n) break;
            m_storage->swapChara(pos, j);
            std::swap(current[pos], current[j]);
        }
        // 重映射 TARGET / ASSI（C# fixMaster=true 时 MASTER 不变）
        const auto findSlot = [&](qint64 oldSlot) -> qint64 {
            for (int i = 0; i < n; ++i) {
                if (current.at(i) == static_cast<int>(oldSlot)) return i;
            }
            return oldSlot;
        };
        if (oldTarget >= 0 && oldTarget < n) m_storage->setTarget(0, findSlot(oldTarget));
        if (oldAssi >= 0 && oldAssi < n) m_storage->setAssi(0, findSlot(oldAssi));
        return true;
    }

    // ---- SAVEVAR / LOADVAR（EE 扩展）----
    if (name == QLatin1String("SAVEVAR") || name == QLatin1String("LOADVAR")) {
        return handleSaveVarCommand(line);
    }
    return true;
}

// ---------------------------------------------------------------------------
// SAVEVAR / LOADVAR（EmueraEE 扩展；C# 原版注册了但运行期抛 NotImpl）
//
//   EE 顺序：SAVEVAR <变量>…, <文件名>（末尾字符串字面量为文件名）
//   C# 原型：SAVEVAR <文件名>, <保存文字列>, <变量>…（首参为字符串字面量时）
//   LOADVAR <文件名>
//
// 格式：C#1808 Var 二进制（命令约定为本移植扩展）（sav/ 目录）。只存全局、非角色变量（对齐 C# 参数构建期的
// 限制：角色/局部/私有/常量/引用变量不可存）。
// ---------------------------------------------------------------------------
bool ExecutionEngine::handleSaveVarCommand(const LogicalLine& line)
{
    if (!m_storage) return true;
    const QString& name = line.functionName;
    ExpressionEvaluator& ev = getEvaluator();
    const QList<const Operand*> ops = statusOperands(line);
    const auto evalString = [&](const Operand* op) -> QString {
        if (!op) return QString();
        if (op->isString) return op->raw;
        if (op->ast) return stringText(ev.evaluate(*op->ast, m_storage, m_gameBaseData));
        throw std::runtime_error("String operand has no load-time AST");
    };

    // ---- LOADVAR <文件名>：读回并按记录的变量名/类型/尺寸恢复 ----
    if (name == QLatin1String("LOADVAR")) {
        if (ops.isEmpty()) return true;
        const QString fileName = evalString(ops.first());
        if (fileName.isEmpty()) return true;
        const auto found = m_variableSnapshots.constFind(QDir::cleanPath(fileName));
        if (found == m_variableSnapshots.constEnd()) return true;
        if(!m_gameBaseData || m_gameBaseData->acceptsSave(found->uniqueCode,found->scriptVersion))
            m_storage->applyBinarySave(found.value());
        return true;
    }

    // ---- SAVEVAR：拆出 文件名 / 保存文字列 / 变量名表 ----
    QString fileName;
    QString message;
    QList<const Operand*> varOps;
    if (!ops.isEmpty() && ops.first()->isString) {
        // C# 原型：<文件名>, <保存文字列>, <变量>…
        if (ops.size() >= 2) {
            fileName = evalString(ops.at(0));
            message = evalString(ops.at(1));
            for (int i = 2; i < ops.size(); ++i) varOps.append(ops.at(i));
        }
    } else {
        // EE 顺序：<变量>…, <文件名>（末尾的字符串字面量）
        for (int i = 0; i < ops.size(); ++i) {
            if (ops.at(i)->isString && i == ops.size() - 1) fileName = ops.at(i)->raw;
            else varOps.append(ops.at(i));
        }
    }
    if (fileName.isEmpty()) {
        if (!varOps.isEmpty()) fileName = evalString(varOps.takeLast());
        if (fileName.isEmpty()) {
            qWarning() << "[save] SAVEVAR 缺少文件名，忽略。行:" << line.position.toString();
            return true;
        }
    }

    VariableSnapshot snapshot=m_storage->captureBinarySave(EraBinaryData::FileType::Var);
    // SAVEVAR permits explicitly selected GLOBAL entries as an EE extension.
    const auto global=m_storage->captureBinarySave(EraBinaryData::FileType::Global);
    snapshot.variables.append(global.variables);
    QSet<QString> selected;
    for(const Operand* op:varOps) selected.insert(op->raw.trimmed().toUpper());
    for(auto it=snapshot.variables.begin();it!=snapshot.variables.end();) {
        if(!selected.contains(it->name)) it=snapshot.variables.erase(it); else ++it;
    }
    snapshot.description=message; snapshot.uniqueCode=globalUniqueCode();
    snapshot.scriptVersion=m_gameBaseData ? m_gameBaseData->scriptVersion() : 0;
    if (m_gameDataDir.isEmpty()) return true;
    const QString directory = GamePaths::join(m_gameDataDir, QStringLiteral("sav"));
    const QString key = QDir::cleanPath(fileName);
    const QString path = GamePaths::join(directory, key);
    if (path.isEmpty()) return true;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    QByteArray bytes;
    const auto format=m_saveFormatProvider ? m_saveFormatProvider() : QPair<bool,bool>{true,false};
    if(!EraBinaryData::encode(snapshot,bytes,nullptr,format.first && format.second)) return true;
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        emit errorOccurred(QStringLiteral("SAVEVAR 写入失败 %1").arg(path));
        return true;
    }
    m_variableSnapshots.insert(key, std::move(snapshot));
    return true;
}
