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
#include "expression_evaluator.h"
#include "ast/expression_ast.h"
#include "ast/ast_builder.h"
#include "ast/strform_parser.h"
#include "ast/print_template.h"
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
        // 变量下标 / 表达式（BAG:COUNT、BAG:(COUNT + 1)）
        qint64 value = 0;
        bool resolved = false;
        if (m_parseTable) {
            const QSharedPointer<ExpressionNode> ast = m_parseTable->expressionAst(idxText);
            if (ast) {
                ExpressionEvaluator localEvaluator;
                ExpressionEvaluator& ev = m_expressionEvaluator ? *m_expressionEvaluator : localEvaluator;
                value = ev.evaluate(*ast, m_storage, m_gameBaseData).toLongLong();
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
    const QString upper = ref.name.toUpper();
    if (upper == QLatin1String("LOCAL") || upper == QLatin1String("ARG")) {
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
    if (m_storage->hasParameter(ref.name)) { m_storage->setParameter(ref.name, value); return; }
    const QString upper = ref.name.toUpper();
    if (upper == QLatin1String("LOCAL") || upper == QLatin1String("ARG")) {
        m_storage->setLocalInt(ref.first(), value);   // 用户函数局部槽（C# LOCAL）
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
    if (name == "DRAWLINE" || name == "CUSTOMDRAWLINE" || name == "DRAWLINEFORM") {
        // 对齐 C# Process.ScriptProc：DRAWLINE = PrintBar()（用 Config.DrawLineString
        // 循环拼到 DrawableWidth 再裁回）+ NewLine()；
        // CUSTOMDRAWLINE / DRAWLINEFORM = printCustomBar(str) + NewLine()。
        QString barStr = m_drawLineString;               // 默认 "-"（半角）
        if (name != QLatin1String("DRAWLINE")) {
            if (args.isEmpty()) return true;
            barStr = evalExpressionCached(m_parseTable, *m_expressionEvaluator,
                                          args.first().raw, m_storage, m_gameBaseData)
                         .toString();
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

    // 其它指令：由显示/子系统各自处理（未实现者静默跳过）
    return true;
}

bool ExecutionEngine::handleStringAssignment(const QString& lhs, const QString& rhs, const QSharedPointer<ExpressionNode>& ast) {
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
    if (upper == QLatin1String("SAVEDATA_TEXT")) {
        m_storage->setSystemStr(upper, 0, value);
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
