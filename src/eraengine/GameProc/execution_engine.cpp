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
#include <QDebug>
#include "expression_evaluator.h"
#include "ast/expression_ast.h"
#include "era_parse_table.h"
#include "eraengine.h"
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
    : QObject(parent), m_storage(storage), m_gameBaseData(gameBaseData), m_functionSystem(nullptr), m_running(false), m_currentLine(0), m_executionPosition(0), m_totalInstructionsExecuted(0) {
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
QPair<QString, int> ExecutionEngine::parseLHS(const QString& lhs) {
    // 解析 LHS：VARIABLE / VARIABLE:index / VARIABLE:表达式（如 BAG:COUNT、BAG:(COUNT+1)、CFLAG:MAIN:10）
    QString trimmed = lhs.trimmed();
    // 多级下标：从**第一个** ':' 切开，名字部分不含 ':'
    const int colon = trimmed.indexOf(QLatin1Char(':'));
    if (colon <= 0) {
        return qMakePair(trimmed, -1);
    }
    QString name = trimmed.left(colon).trimmed();
    QString idxText = trimmed.mid(colon + 1).trimmed();
    // 名字后缀的 ':' 也可能是 2D/3D 的第二维（本移植只支持第一维），保留原名字
    bool ok = false;
    const int direct = idxText.toInt(&ok);
    if (ok) {
        return qMakePair(name, direct);
    }
    // 变量下标（BAG:COUNT / BAG:(COUNT + 1)）：用缓存的 AST 求值
    if (m_parseTable) {
        const QSharedPointer<ExpressionNode> ast = m_parseTable->expressionAst(idxText);
        if (ast) {
            ExpressionEvaluator localEvaluator;
            ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
            const qint64 v = ev.evaluate(*ast, m_storage, m_gameBaseData).toLongLong();
            return qMakePair(name, static_cast<int>(v));
        }
    }
    return qMakePair(trimmed, -1);
}



bool ExecutionEngine::handleCompoundAssignment(const QString& lhs, const QString& op, const QString& rhs) {
    // Parse the LHS to get variable name and index
    auto [varName, index] = parseLHS(lhs);
    
    // Get current value
    qint64 currentValue = 0;
    if (index >= 0) {
        currentValue = m_storage->getGlobalInt1D(varName, index);
    } else {
        currentValue = m_storage->getGlobalInt1D(varName, 0);
    }
    
    // Evaluate the RHS expression (prefer cached AST)
    ExpressionEvaluator localEvaluator;
    ExpressionEvaluator& evaluator = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
    const QVariant rhsVar = evalExpressionCached(m_parseTable, evaluator, rhs, m_storage, m_gameBaseData);
    int rhsValue = rhsVar.isValid() ? rhsVar.toInt() : rhs.toInt();
    
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
    if (index >= 0) {
        m_storage->setGlobalInt1D(varName, index, currentValue);
    } else {
        m_storage->setGlobalInt1D(varName, 0, currentValue);
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

    // ---- 输出 ----
    if (name == "PRINTFORM" || name == "PRINTFORMS") {
        return handlePrintForm(args, false);
    }
    if (name == "PRINTFORML" || name == "PRINTFORMW" || name == "PRINTFORMSL"
        || name == "PRINTFORMSW" || name == "PRINTFORMC" || name == "PRINTFORMLC") {
        return handlePrintForm(args, true);
    }
    if (name == "PRINTFORMC" || name == "PRINTC") {
        return handlePrint(args, false);
    }
    if (name == "PRINT") {
        return handlePrint(args, false);
    }
    if (name == "PRINTL") {
        return handlePrint(args, true);
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
            QString text = evalExpressionCached(m_parseTable, ev, ops[0]->raw,
                                                m_storage, m_gameBaseData).toString();
            if (text.isEmpty()) {
                QString t = ops[0]->raw.trimmed();
                if (t.startsWith(QLatin1Char('"'))) t = t.mid(1);
                if (t.endsWith(QLatin1Char('"'))) t.chop(1);
                text = t;
            }
            const QString rawValue = ops[1]->raw.trimmed();
            const bool valueIsString = rawValue.startsWith(QLatin1Char('"'))
                                       || (ops[1]->ast && ops[1]->ast->valueType() == OperandType::Str);
            const QVariant value = evalExpressionCached(m_parseTable, ev, rawValue,
                                                        m_storage, m_gameBaseData);
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
    if (name == "RESETCOLOR") {
        emit consoleResetColor();
        return true;
    }
    if (name == "SETCOLOR") {
        if (args.size() >= 1) {
            emit consoleColor(args[0].raw);
        }
        return true;
    }
    if (name == "ALIGNMENT") {
        emit consoleAlign(args.size() >= 1 ? args[0].raw : QStringLiteral("LEFT"));
        return true;
    }
    if (name == "DRAWLINE") {
        // 对齐 C# DrawLineMethod：用 Config.DrawLineString（默认 "―"）铺满一行
        qint64 count = 0;
        if (!args.isEmpty() && !args.first().raw.trimmed().isEmpty()) {
            bool ok = false;
            count = args.first().raw.trimmed().toLongLong(&ok);
            if (!ok) count = 0;
        }
        const qint64 n = count > 0 ? count : 44;   // 默认铺屏宽（本移植用 44 列近似）
        QString line;
        line.reserve(static_cast<int>(n));
        for (qint64 i = 0; i < n; ++i) line.append(QChar(0x2015));   // ―
        emit consolePrint(line, true);
        return true;
    }

    // ---- 赋值 ----
    if (name == "=") {
        if (args.size() >= 2) {
            // 目的变量是字符串 -> 字符串赋值（此前只有整数路径）。
            // 目的类型从**变量表**取（LHS 操作数在 AST 里不带类型节点）。
            const auto lhsInfo = parseLHS(args[0].raw);
            const OperandType destType =
                m_parseTable ? m_parseTable->variableTable().typeOf(lhsInfo.first, QString())
                             : OperandType::Unknown;
            if (destType == OperandType::Str) {
                return handleStringAssignment(args[0].raw, args[1].raw);
            }
            return handleAssignment(args[0].raw, args[1].raw);
        }
        return true;
    }
    if (name == "+=") {
        // 字符串累加（`A += B` -> A = A + B）
        if (args.size() >= 2) {
            const auto lhsInfo = parseLHS(args[0].raw);
            const OperandType destType =
                m_parseTable ? m_parseTable->variableTable().typeOf(lhsInfo.first, QString())
                             : OperandType::Unknown;
            if (destType == OperandType::Str) {
                return handleStringAssignment(args[0].raw,
                                              args[0].raw + QLatin1String(" + ") + args[1].raw);
            }
        }
    }
    if (name == "+=" || name == "-=" || name == "*=" || name == "/=") {
        if (args.size() >= 2) {
            return handleCompoundAssignment(args[0].raw, name, args[1].raw);
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

    // 其它指令：由显示/子系统各自处理（未实现者静默跳过）
    return true;
}

bool ExecutionEngine::handleStringAssignment(const QString& lhs, const QString& rhs) {
    auto [varName, index] = parseLHS(lhs);
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
    if (bareIdent && m_parseTable) {
        const QSharedPointer<ExpressionNode> ast = m_parseTable->expressionAst(trimmed);
        if (ast) {
            value = evaluator.evaluate(*ast, m_storage, m_gameBaseData).toString();
            evaluated = true;
        }
    }
    if (!evaluated) {
        const QVariant rhsValue = evalExpressionCached(m_parseTable, evaluator, rhs, m_storage, m_gameBaseData);
        value = rhsValue.toString();
    }

    const QString upper = varName.toUpper();
    if (upper == QLatin1String("RESULTS")) {
        m_storage->setLocalStr(0, value);
        return true;
    }
    if (upper == QLatin1String("SAVEDATA_TEXT")) {
        m_storage->setSystemStr(upper, 0, value);
        return true;
    }
    m_storage->setGlobalStr1D(varName, index >= 0 ? index : 0, value);
    return true;
}

bool ExecutionEngine::handleAssignment(const QString& lhs, const QString& rhs) {
    
    // Parse the LHS to get variable name and index
    auto [varName, index] = parseLHS(lhs);
    
    
    // Evaluate the RHS expression using the parse table's cached AST.
    // Pass m_gameBaseData if available so GameBase variables can be resolved.
    ExpressionEvaluator localEvaluator;
    ExpressionEvaluator& evaluator = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
    QVariant rhsValue = evalExpressionCached(m_parseTable, evaluator, rhs, m_storage, m_gameBaseData);
    
    
    // If the evaluated result is not valid, try direct conversion
    if (!rhsValue.isValid() || rhsValue.toString().isEmpty()) {
        bool ok = false;
        int intValue = rhs.toInt(&ok);
        if (ok) {
            rhsValue = QVariant(intValue);
        }
    }
    
    
    // Set the variable value
    if (index >= 0) {
        // Array access
        m_storage->setGlobalInt1D(varName, index, rhsValue.toInt());
    } else {
        // Simple variable access
        m_storage->setGlobalInt1D(varName, 0, rhsValue.toInt());
    }
    
    return true;
}

bool ExecutionEngine::handlePrint(const QList<Operand>& args, bool newline) {
    QString text;
    for (const Operand& arg : args) {
        QString value = arg.raw;
        
        // If this is a variable reference, substitute its value
        if (arg.isVariable) {
            QString varName = value;
            
            // Remove trailing % if present (Emuera format uses %VAR%)
            if (varName.endsWith('%')) {
                varName.chop(1);
            }
            
            // Check if it's a GameBase variable (GAMEBASE_*)
            if (varName.startsWith("GAMEBASE_") && m_gameBaseData) {
                QString key = varName.mid(9);  // Remove "GAMEBASE_" prefix
                value = m_gameBaseData->get(key);
                if (value.isEmpty()) {
                    value = "{%" + varName + "%}";  // Keep original if not found
                }
            }
            // Check if it's a system variable ($)
            else if (varName.startsWith('$')) {
                // System variables not fully implemented yet
                value = "0";
            }
            // Check if it's a regular variable (T series)
            else {
                // Try to get as integer variable
                int intVal = m_storage->getGlobalInt1D(varName, 0);
                value = QString::number(intVal);
            }
        }
        
        text += value + " ";
    }
    // 输出到显示层（ConsoleBackend）；换行由 *L 系决定
    emit consolePrint(text, newline);
    return true;
}

bool ExecutionEngine::handlePrintForm(const QList<Operand>& args, bool newline) {
    ExpressionEvaluator localEvaluator;
    ExpressionEvaluator& evaluator = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;

    QString text;
    for (const Operand& arg : args) {
        if (arg.ast) {
            // StrForm AST：文本 + 内嵌表达式
            text += evaluator.evaluate(*arg.ast, m_storage, m_gameBaseData).toString();
        } else {
            text += arg.raw;
        }
    }
    emit consolePrint(text, newline);
    return true;
}

bool ExecutionEngine::handleResetData() {
    qDebug() << "RESETDATA";
    return true;
}

bool ExecutionEngine::handleLoadGlobal() {
    qDebug() << "LOADGLOBAL";
    return true;
}

void ExecutionEngine::setError(const QString& message) {
    qDebug() << "Execution error:" << message;
    m_running = false;
    emit errorOccurred(message);
}

// Receive instruction from ParseTable
// Handle jump request from ParseTable
// Handle memory space change from ParseTable
