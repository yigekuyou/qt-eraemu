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
#include "era_parse_table.h"
#include "process_state.h"
#include "variable_storage.h"
#include "execution_engine.h"
#include "ast/expression_lexer.h"
#include "ast/expression_parser.h"
#include "ast/function_types.h"
#include "ast/strform_parser.h"
#include "ast/print_template.h"
#include "constant_table.h"
#include "user_defined_variable_data.h"
#include <exception>
#include "ast/expression_ast.h"
#include "ast/argument_parser.h"
#include "ast/expression_evaluator.h"
#include <QDebug>
#include <QSet>
#include <algorithm>
#include <utility>

EraParseTable::EraParseTable(ProcessState* state, ExecutionEngine* execEngine, QObject* parent)
    : QObject(parent)
    , m_state(state)
    , m_executionEngine(execEngine)
{
}

EraParseTable::EraParseTable(ProcessState* state, QObject* parent)
    : EraParseTable(state, nullptr, parent)
{
}

EraParseTable::~EraParseTable() = default;

void EraParseTable::setVariableStorage(VariableStorage* storage) {
    m_variableStorage = storage;
}

void EraParseTable::setExpressionEvaluator(ExpressionEvaluator* evaluator) {
    m_evaluator = evaluator;
    if (evaluator) evaluator->setVariableDimProvider([this](const QString& name) {
        const auto* line = lineAt(m_currentScript, m_currentLine);
        const auto* decl = m_variables.find(name, line ? line->ownerFunction : QString());
        return decl ? decl->dimension : 1;
    });
}

// ---------------------------------------------------------------------------
// 只读区：表达式 AST 缓存（唯一解析流水线）
// ---------------------------------------------------------------------------

QSharedPointer<ExpressionNode> EraParseTable::expressionAst(const QString& expr) {
    const QString key = expr.trimmed();
    if (key.isEmpty()) {
        return nullptr;
    }

    QString scope;
    if (m_finalized) {
        if (const auto* line = lineAt(m_currentScript, m_currentLine)) scope = line->ownerFunction;
    }
    const QString scopedKey = scope.toUpper() + QChar(0x1f) + key;
    if (!scope.isEmpty() && m_scopedAstCache.contains(scopedKey)) return m_scopedAstCache.value(scopedKey);
    auto it = m_astCache.constFind(key);
    if (it != m_astCache.constEnd()) {
        if (scope.isEmpty()) return it.value();
        auto copy = cloneExpression(it.value());
        VariableTable::applyTypes(*copy, m_variables, scope);
        m_scopedAstCache.insert(scopedKey, copy);
        return copy;
    }

    ExpressionLexer lexer;
    const QList<ExpressionToken> tokens = lexer.tokenize(key, 1);
    if (tokens.isEmpty()) {
        return nullptr;
    }

    ExpressionParser parser;
    // 强类型：仅用户自定义函数由 Provider 决定；内置函数（内部命令）由
    // ExpressionParser 内部的 kBuiltinFunctions 目录解析（对齐 C# methodDic）。
    // 装载期间用户函数可能尚未 merge，finalizeParse 会再统一重绑一次。
    parser.setFunctionTypeProvider([this](const QString& name) -> OperandType {
        if (const UserFunctionDecl* fn = userFunction(name)) {
            // #FUNCTIONS -> Str；#FUNCTION -> Int；无 # 行的 @label 也按 Int 处理
            // （实测 eraTW 大量「无 #FUNCTION 的 @label」在式中调用且正常工作，
            //  故此处取宽容语义，与 C# 源码里的严厉分支不同）
            if (fn->isMethod) return fn->returnType;
            return OperandType::Int;
        }
        return OperandType::Unknown;
    });
    // 格式化串：@"..." / \@...#...\@
    parser.setFormProvider([this](const QString& text, bool yenAt) -> QSharedPointer<ExpressionNode> {
        const AstResolver resolve = [this](const QString& e) { return expressionAst(e); };
        if (yenAt) return StrFormParser::parseYenAt(text, resolve);
        return QSharedPointer<ExpressionNode>(StrFormParser::parse(text, resolve));
    });
    if (m_constantTable) {
        const ConstantTable* ct = m_constantTable;
        parser.setConstantNameProvider([ct](const QString& var, const QString& name) {
            return ct->indexForVariable(var, name) >= 0;
        });
    }
    // `#DIM CONST NAME = value`：解析期折叠为字面量（与 CSV 常数名区分：这里返回**值**）
    {
        const VariableTable* vt = &m_variables;
        parser.setConstantValueProvider([vt](const QString& name) -> QVariant {
            qint64 iv = 0;
            if (vt->constInt(name, iv)) return QVariant::fromValue<qint64>(iv);
            QString sv;
            if (vt->constStr(name, sv)) return QVariant(sv);
            return QVariant();
        });
    }
    QSharedPointer<ExpressionNode> ast = parser.parse(tokens);
    if (ast) {
        // 立即按变量表定型（新解析的 AST 也保持强类型）
        VariableTable::applyTypes(*ast, m_variables, scope);
        if (scope.isEmpty()) m_astCache.insert(key, ast);
        else m_scopedAstCache.insert(scopedKey, ast);
    }
    return ast;
}

const ScriptData* EraParseTable::script(const QString& scriptName) const {
    auto it = m_scripts.constFind(scriptName);
    return it == m_scripts.constEnd() ? nullptr : &it.value();
}

QString EraParseTable::scriptPath(const QString& scriptName) const {
    if (const ScriptData* data = script(scriptName)) {
        return data->path;
    }
    return QString();
}

bool EraParseTable::hasLabel(const QString& label) const {
    for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) {
        if (it.value().labelPositions.contains(label)) return true;
    }
    return false;
}

const LogicalLine* EraParseTable::lineAt(const QString& scriptName, int line) const {
    const ScriptData* data = script(scriptName);
    if (!data || line < 0 || line >= data->lines.size()) {
        return nullptr;
    }
    return &data->lines.at(line);
}

int EraParseTable::jumpTarget(const QString& scriptName, int line) const {
    const ScriptData* data = script(scriptName);
    if (!data) {
        return -1;
    }
    return data->jumpTo.value(line, -1);
}

QList<int> EraParseTable::ifBranches(const QString& scriptName, int ifLine) const {
    const ScriptData* data = script(scriptName);
    if (!data) {
        return QList<int>();
    }
    return data->ifBranches.value(ifLine, QList<int>());
}

const UserFunctionInfo* EraParseTable::userFunction(const QString& name) const {
    auto it = m_functions.constFind(name.toUpper());
    return it == m_functions.constEnd() ? nullptr : &it.value();
}

QList<LabelRef> EraParseTable::labels(const QString& name) const {
    QList<LabelRef> out = m_labelLists.value(name.toUpper());
    // 并行装载时插入顺序不确定；固定按 (脚本名, 行号) 排序，保证事件导航可复现
    std::sort(out.begin(), out.end(), [](const LabelRef& a, const LabelRef& b) {
        if (a.script != b.script) return a.script < b.script;
        return a.line < b.line;
    });
    return out;
}

int EraParseTable::scriptLineCount(const QString& scriptName) const {
    const ScriptData* data = script(scriptName);
    return data ? data->lines.size() : 0;
}

QString EraParseTable::labelNameAt(const QString& scriptName, int line) const {
    const ScriptData* data = script(scriptName);
    if (!data || line < 0 || line >= data->lines.size()) return QString();
    return data->lines.at(line).labelName;
}

bool EraParseTable::callLabelAt(const QString& scriptName, int line, int returnLine) {
    return callLabelAt(scriptName, line, m_currentScript, returnLine);
}

bool EraParseTable::callLabelAt(const QString& scriptName, int line,
                                const QString& returnScript, int returnLine) {
    const ScriptData* data = script(scriptName);
    if (!data || line < 0 || line >= data->lines.size()) {
        return false;
    }
    pushFrame(Frame(returnScript, returnLine, data->lines.at(line).labelName, line));
    if (scriptName != m_currentScript) {
        switchToMemorySpace(scriptName);
    }
    m_jumped = true;
    setCurrentLineInternal(line, true);
    return true;
}

// ---------------------------------------------------------------------------
// 只读区：条件求值（优先使用缓存 AST）
// ---------------------------------------------------------------------------

bool EraParseTable::evaluateAst(const QSharedPointer<ExpressionNode>& ast, bool& out) {
    if (!ast) {
        return false;
    }
    ExpressionEvaluator local;
    ExpressionEvaluator* ev = m_evaluator ? m_evaluator : &local;
    const QVariant v = ev->evaluate(*ast, m_variableStorage, m_gameBaseData);
    out = v.isValid() && v.toLongLong() != 0;
    return true;
}

bool EraParseTable::evaluateExpression(const QString& expr, bool& out) {
    const QSharedPointer<ExpressionNode> ast = expressionAst(expr);
    if (ast) {
        return evaluateAst(ast, out);
    }
    ExpressionEvaluator local;
    ExpressionEvaluator* ev = m_evaluator ? m_evaluator : &local;
    const QVariant v = ev->evaluate(expr, m_variableStorage, m_gameBaseData);
    out = v.isValid() && v.toLongLong() != 0;
    return true;
}

bool EraParseTable::evaluateCondition(const QString& scriptName, int line, bool& out, int argIndex) {
    const LogicalLine* ll = lineAt(scriptName, line);
    if (!ll) {
        return false;
    }
    if (ll->condition && evaluateAst(ll->condition, out)) {
        return true;
    }
    if (argIndex >= 0 && argIndex < ll->arguments.size() && ll->arguments.at(argIndex).ast) {
        return evaluateAst(ll->arguments.at(argIndex).ast, out);
    }
    return evaluateExpression(ll->raw, out);
}

// ---------------------------------------------------------------------------
// 只读区：装载（AST 由 ErbLoader/AstBuilder 预先构建）
// ---------------------------------------------------------------------------

bool EraParseTable::loadScript(const QString& scriptName, const QList<LogicalLine>& lines,
                              bool isHeaderFile, const QString& path) {
    if (scriptName.isEmpty() || lines.isEmpty()) {
        return false;
    }

    ScriptData data;
    data.lines = lines;
    data.path = path;

    QString currentFunction;   // 用于 #DIM 的作用域判定
    bool afterLabel = false;   // .ERB 中 # 行必须紧跟函数标签（对齐 C# ErbLoader）

    for (int i = 0; i < data.lines.size(); ++i) {
        LogicalLine& line = data.lines[i];
        if (line.kind != LineKind::Null && line.kind != LineKind::Preprocessor
            && line.kind != LineKind::FunctionLabel) {
            afterLabel = false;
        }
        line.ownerFunction = currentFunction;   // 记录所属函数（供变量类型解析）

        // 变量声明（#DIM/#DIMS/#GLOBAL/#GLOBALS/#PRIVATE）
        // 注：Emuera 的函数形参是「引用私有变量」（@F(VAR:0)），类型来自 #DIM，
        //     因此这里只跟踪当前函数名用于作用域判定，不把形参登记为新变量。
        if (line.kind == LineKind::FunctionLabel) {
            currentFunction = line.labelName;
        } else if (line.kind == LineKind::Preprocessor) {
            // .ERH（头文件）：文件级 #DIM = 全局（对齐 C# HeaderFileLoader）
            // .ERB（脚本）  ：# 行必须紧跟函数标签，否则告警并忽略（对齐 C# ErbLoader）
            if (isHeaderFile || afterLabel) {
                parseVariableDeclaration(line, currentFunction, isHeaderFile);
            } else {
                m_parseWarnings.append(QStringLiteral("%1: 函数声明之外使用了 # 行 (%2)")
                                           .arg(line.position.toString(), line.raw.trimmed()));
            }
        }
        if (line.kind == LineKind::FunctionLabel) {
            line.ownerFunction = currentFunction;   // 函数标签行自身
            afterLabel = true;
        }

        if (line.isLabel()) {
            data.labelPositions[line.labelName] = i;

            // 注册用户自定义函数（@label 即函数入口）—— 构建完整的声明节点
            if (line.kind == LineKind::FunctionLabel) {
                UserFunctionDecl decl;
                decl.name = line.labelName.toUpper();
                decl.script = scriptName;
                decl.labelLine = i;

                // 形参表：把标签里写的名字归类为 ARG / ARGS / 私有变量
                for (const QString& argName : line.labelArgs) {
                    UserParamDecl p = classifyUserParam(argName);
                    const QString defaultValue = line.labelDefaults.value(decl.params.size());
                    p.hasDefault = !defaultValue.isEmpty();
                    if (p.hasDefault) {
                        bool ok = false;
                        p.defaultInt = defaultValue.toLongLong(&ok);
                        if (!ok) p.defaultStr = defaultValue;
                    }
                    if (p.target == UserParamTarget::Arg
                        && p.index > decl.maxArgIndex) decl.maxArgIndex = p.index;
                    if (p.target == UserParamTarget::Args
                        && p.index > decl.maxArgsIndex) decl.maxArgsIndex = p.index;
                    decl.params.append(p);
                }

                // 紧随其后的 # 行：是否可作表达式函数 + 事件分组/私有局部尺寸
                for (int j = i + 1; j < lines.size() && lines.at(j).kind == LineKind::Preprocessor; ++j) {
                    const QString dir = lines.at(j).raw.trimmed().toUpper();
                    if (dir.startsWith("#FUNCTIONS")) {
                        decl.isMethod = true;
                        decl.returnType = OperandType::Str;
                    } else if (dir.startsWith("#FUNCTION")) {
                        if (!decl.isMethod) decl.returnType = OperandType::Int;
                        decl.isMethod = true;
                    } else if (dir.startsWith("#SINGLE")) {
                        decl.isSingle = true;
                    } else if (dir.startsWith("#PRI")) {
                        decl.isPri = true;
                    } else if (dir.startsWith("#LATER")) {
                        decl.isLater = true;
                    } else if (dir.startsWith("#ONLY")) {
                        decl.isOnly = true;
                    } else if (dir.startsWith("#LOCALSSIZE")) {
                        decl.localsSize = dir.mid(11).trimmed().toInt();
                    } else if (dir.startsWith("#LOCALSIZE")) {
                        decl.localSize = dir.mid(10).trimmed().toInt();
                    }
                }

                // 事件 / 系统标签（对齐 C# IdentifierDictionary.IsEventLabelName/IsSystemLabelName）
                static const QSet<QString> kEventLabels = {
                    QStringLiteral("EVENTFIRST"), QStringLiteral("EVENTTRAIN"),
                    QStringLiteral("EVENTSHOP"), QStringLiteral("EVENTBUY"),
                    QStringLiteral("EVENTCOM"), QStringLiteral("EVENTTURNEND"),
                    QStringLiteral("EVENTCOMEND"), QStringLiteral("EVENTEND"),
                    QStringLiteral("EVENTLOAD"),
                };
                if (kEventLabels.contains(decl.name)) decl.isEvent = true;
                if (decl.isEvent || decl.name.startsWith(QLatin1String("SYSTEM"))
                    || decl.name.startsWith(QLatin1String("SHOW_"))
                    || decl.name.startsWith(QLatin1String("USER"))
                    || decl.name.startsWith(QLatin1String("COM"))
                    || decl.name.startsWith(QLatin1String("ABLUP"))) {
                    decl.isSystem = true;
                }

                // 函数体结束 = 下一个函数标签前一行（扁平行号区间，供诊断/遍历）
                decl.endLine = lines.size() - 1;
                for (int j = i + 1; j < lines.size(); ++j) {
                    if (lines.at(j).kind == LineKind::FunctionLabel) { decl.endLine = j - 1; break; }
                }

                // 同名函数重复定义：真实游戏（eraTW 的各个「口上」）普遍存在，
                // C# 也是 first-wins，故静默忽略后者（不产生告警）。
                if (!m_functions.contains(decl.name)) {
                    m_functions.insert(decl.name, decl);
                }

                // 保留全部声明（事件函数的 4 组导航需要；同名多份）
                LabelRef ref;
                ref.script = scriptName;
                ref.line = i;
                ref.isEvent = decl.isEvent;
                ref.isSingle = decl.isSingle;
                ref.isPri = decl.isPri;
                ref.isLater = decl.isLater;
                ref.isOnly = decl.isOnly;
                m_labelLists[decl.name].append(ref);
            }
        }
    }

    m_scripts.insert(scriptName, data);

    // 维数求值与类型回填都推迟到 finalizeParse()：
    // 二者都需要「全部声明/常数就绪」，且是全量操作（避免 O(脚本数 × 声明数)）。

    // 标记区：跳转标记 + 控制转移预绑定
    buildJumpMarkings(scriptName);

    if (m_currentScript.isEmpty()) {
        m_currentScript = scriptName;
    }

    qDebug() << "[parse] 脚本" << scriptName << (isHeaderFile ? "(头文件)" : "")
             << "逻辑行" << data.lines.size() << "标签" << data.labelPositions.size();
    emit parseCompleted(scriptName);
    return true;
}

void EraParseTable::setEntryPoint(const QString& label) {
    m_entryPoint = label;

    for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) {
        if (it.value().labelPositions.contains(label)) {
            m_currentScript = it.key();
            break;
        }
    }

    if (m_currentScript.isEmpty()) {
        const QStringList commonLabels = {"MAIN_LOOP", "SYSTEM_TITLE", "MAIN", "TITLE"};
        for (const QString& labelToTry : commonLabels) {
            for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) {
                if (it.value().labelPositions.contains(labelToTry)) {
                    m_currentScript = it.key();
                    m_entryPoint = labelToTry;
                    break;
                }
            }
            if (!m_currentScript.isEmpty()) break;
        }
    }

    if (!m_currentScript.isEmpty()) {
        const int entryLine = getLabelPosition(m_currentScript, m_entryPoint);
        resetPosition();
        m_jumped = true;   // 入口是「跳转」落点：标签行应被跳过
        setCurrentLineInternal(entryLine >= 0 ? entryLine : 0, true);
    }

    emit entryPointReached(label);
}

QString EraParseTable::getEntryPoint() const {
    return m_entryPoint;
}

int EraParseTable::getLabelPosition(const QString& scriptName, const QString& labelName) const {
    const ScriptData* data = script(scriptName);
    if (!data) {
        return -1;
    }
    return data->labelPositions.value(labelName, -1);
}

bool EraParseTable::resolveJumpTarget(const QString& label, int& position) {
    position = getLabelPosition(m_currentScript, label);
    if (position >= 0) {
        return true;
    }

    for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) {
        const int pos = it.value().labelPositions.value(label, -1);
        if (pos >= 0) {
            position = pos;
            switchToMemorySpace(it.key());
            return true;
        }
    }
    return false;
}

bool EraParseTable::resolveJumpToScript(const QString& scriptName, const QString& label, int& position) {
    position = getLabelPosition(scriptName, label);
    if (position >= 0) {
        switchToMemorySpace(scriptName);
        return true;
    }
    return false;
}

QString EraParseTable::getCurrentScript() const {
    return m_currentScript;
}

// ===========================================================================
// 位置区 (Position region)
// ===========================================================================

QString EraParseTable::currentScript() const {
    return m_currentScript;
}

int EraParseTable::currentLine() const {
    return m_currentLine;
}

int EraParseTable::depth() const {
    return m_depth;
}

const QList<Frame>& EraParseTable::callStack() const {
    return m_callStack;
}

Frame EraParseTable::currentFrame() const {
    if (m_callStack.isEmpty()) {
        return Frame();
    }
    return m_callStack.last();
}

bool EraParseTable::hasPosition() const {
    return !m_currentScript.isEmpty();
}

int EraParseTable::lineCountFor(const QString& script) const {
    const ScriptData* data = this->script(script);
    return data ? data->lines.size() : 0;
}

void EraParseTable::setCurrentLineInternal(int line, bool forceEmit) {
    if (line < 0) {
        line = 0;
    }
    if (line == m_currentLine && !forceEmit) {
        return;
    }
    m_currentLine = line;
    emit positionChanged(m_currentScript, m_currentLine);
}

void EraParseTable::pushFrame(const Frame& frame) {
    m_callStack.append(frame);
    m_depth = m_callStack.size();
    emit callStackChanged(m_depth);
}

Frame EraParseTable::popFrame() {
    if (m_callStack.isEmpty()) {
        return Frame();
    }
    const Frame frame = m_callStack.takeLast();
    m_depth = m_callStack.size();
    emit callStackChanged(m_depth);
    return frame;
}

void EraParseTable::resetPosition() {
    m_callStack.clear();
    m_depth = 0;
    m_currentLine = 0;
    emit callStackChanged(0);
    emit positionChanged(m_currentScript, 0);
}

void EraParseTable::setPosition(const QString& script, int line, bool jumped) {
    if (!script.isEmpty() && script != m_currentScript) {
        switchToMemorySpace(script);
    }
    m_jumped = jumped;
    setCurrentLineInternal(line, true);
}

void EraParseTable::advance() {
    m_jumped = false;
    setCurrentLineInternal(m_currentLine + 1);
}

bool EraParseTable::jumpToLine(int line) {
    const int count = lineCountFor(m_currentScript);
    if (line < 0 || (count > 0 && line >= count)) {
        return false;
    }
    m_jumped = true;
    setCurrentLineInternal(line, true);
    return true;
}

bool EraParseTable::jumpToLabel(const QString& label) {
    int target = -1;
    if (!resolveJumpTarget(label, target)) {
        return false;
    }

    m_jumped = true;
    setCurrentLineInternal(target, true);

    emit jumpRequested(m_currentScript, label, target);
    return true;
}

bool EraParseTable::callLabel(const QString& label, bool advanceWasCalled) {
    QString targetScript = m_currentScript;
    int target = getLabelPosition(m_currentScript, label);
    if (target < 0) {
        for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) {
            const int pos = it.value().labelPositions.value(label, -1);
            if (pos >= 0) {
                targetScript = it.key();
                target = pos;
                break;
            }
        }
    }
    if (target < 0) {
        return false;
    }

    const int returnLine = advanceWasCalled ? m_currentLine : m_currentLine + 1;
    pushFrame(Frame(m_currentScript, returnLine, label, target));

    if (targetScript != m_currentScript) {
        switchToMemorySpace(targetScript);
    }
    m_jumped = true;
    setCurrentLineInternal(target, true);
    return true;
}

bool EraParseTable::callLabelWithReturn(const QString& label, int returnLine) {
    QString targetScript = m_currentScript;
    int target = getLabelPosition(m_currentScript, label);
    if (target < 0) {
        for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) {
            const int pos = it.value().labelPositions.value(label, -1);
            if (pos >= 0) {
                targetScript = it.key();
                target = pos;
                break;
            }
        }
    }
    if (target < 0) {
        return false;
    }

    pushFrame(Frame(m_currentScript, returnLine, label, target));
    if (targetScript != m_currentScript) {
        switchToMemorySpace(targetScript);
    }
    m_jumped = true;
    setCurrentLineInternal(target, true);
    return true;
}

bool EraParseTable::returnFromCall() {
    if (m_callStack.isEmpty()) {
        return false;
    }

    const Frame frame = popFrame();
    if (!frame.script.isEmpty() && frame.script != m_currentScript) {
        switchToMemorySpace(frame.script);
    }
    m_jumped = false;
    setCurrentLineInternal(frame.returnLine, true);
    return true;
}

void EraParseTable::switchToMemorySpace(const QString& scriptName) {
    if (m_currentScript == scriptName || scriptName.isEmpty()) {
        return;
    }
    m_currentScript = scriptName;
    emit memorySpaceChanged(scriptName);
}

// ---------------------------------------------------------------------------
// 标记区：跳转标记 + 控制转移预绑定（行号索引，等价 C# JumpTo / NextLine）
// ---------------------------------------------------------------------------

void EraParseTable::buildJumpMarkings(const QString& scriptName) {
    auto scriptIt = m_scripts.find(scriptName);
    if (scriptIt == m_scripts.end()) {
        return;
    }

    ScriptData& data = scriptIt.value();
    data.endifLines.clear();
    data.elseLines.clear();
    data.loopEndLines.clear();
    data.jumpTo.clear();
    data.jumpToEnd.clear();
    data.ifBranches.clear();

    struct IfInfo {
        int ifLine = -1;
        int lastBranch = -1;
        QList<int> branches;   // ELSEIF/ELSE 行（C# IfCaseList）
    };
    struct LoopInfo {
        int startLine = -1;
        QString type;
    };
    // SELECTCASE 组（对齐 C# SELECTCASE_Instruction.IfCaseList + CASE.JumpTo）
    struct SelectInfo {
        int selectLine = -1;
        QList<int> caseLines;      // CASE / CASEELSE 行（按出现顺序）
        int lastCase = -1;
    };

    QList<IfInfo> ifStack;
    QList<LoopInfo> loopStack;
    QList<SelectInfo> selectStack;

    for (int i = 0; i < data.lines.size(); ++i) {
        LogicalLine& ll = data.lines[i];
        ll.lineIndex = i;
        if (i + 1 < data.lines.size()) {
            ll.nextLine = i + 1;
        }
        if (ll.kind == LineKind::FunctionLabel) {
            ifStack.clear();
            loopStack.clear();
            selectStack.clear();
        }
        if (!ll.isInstruction()) {
            continue;
        }

        const QString& name = ll.functionName;

        if (name == QLatin1String("IF")) {
            ifStack.append({i, -1, {}});
        }
        else if (name == QLatin1String("SIF")) {
            // SIF：条件为假则跳过下一行
            int next = i + 1;
            while (next < data.lines.size() && data.lines[next].kind == LineKind::Null) ++next;
            const int after = qMin(next + 1, int(data.lines.size()));
            data.jumpTo[i] = after;
            data.elseLines[i] = after;
            ll.jumpTo = after;
        }
        else if (name == QLatin1String("ELSEIF") || name == QLatin1String("ELSE")) {
            if (!ifStack.isEmpty()) {
                IfInfo& cur = ifStack.last();
                const int prev = (cur.lastBranch >= 0) ? cur.lastBranch : cur.ifLine;
                data.elseLines[prev] = i;
                cur.branches.append(i);
                cur.lastBranch = i;
                // ELSEIF/ELSE 由“顺序落入”执行时，直接跳到 ENDIF 之后（C# state.JumpTo(ENDIF)）
            }
        }
        else if (name == QLatin1String("ENDIF")) {
            if (!ifStack.isEmpty()) {
                const IfInfo cur = ifStack.takeLast();
                const int after = i + 1;
                data.endifLines[cur.ifLine] = after;
                data.jumpTo[cur.ifLine] = after;
                data.lines[cur.ifLine].jumpTo = after;
                data.ifBranches[cur.ifLine] = cur.branches;
                for (int b : cur.branches) {
                    data.endifLines[b] = after;
                    data.jumpTo[b] = after;             // 顺序落入分支 -> 跳过到 ENDIF 之后
                    data.lines[b].jumpTo = after;
                }
            }
        }
        else if (name == QLatin1String("SELECTCASE")) {
            selectStack.append({i, {}, -1});
        }
        else if (name == QLatin1String("CASE") || name == QLatin1String("CASEELSE")) {
            if (!selectStack.isEmpty()) {
                SelectInfo& cur = selectStack.last();
                // 上一个 CASE 体结束 -> 直接跳到 ENDSELECT 之后（C# 是逐级跳到下一个 CASE）
                if (cur.lastCase >= 0) {
                    data.jumpTo[cur.lastCase] = -1;      // 占位，ENDSELECT 时统一填
                    data.lines[cur.lastCase].jumpTo = -2; // 标记「待填」
                }
                cur.caseLines.append(i);
                cur.lastCase = i;
            }
        }
        else if (name == QLatin1String("ENDSELECT")) {
            if (!selectStack.isEmpty()) {
                const SelectInfo cur = selectStack.takeLast();
                const int after = i + 1;
                // 每个 CASE / CASEELSE 顺序落入 -> 跳到 ENDSELECT 之后
                for (int c : cur.caseLines) {
                    data.jumpTo[c] = after;
                    data.lines[c].jumpTo = after;
                }
                // SELECTCASE 的默认目标（无 CASE 命中）
                data.jumpTo[cur.selectLine] = after;
                data.jumpToEnd[cur.selectLine] = i;      // ENDSELECT 行
                data.lines[cur.selectLine].jumpTo = after;
                data.ifBranches[cur.selectLine] = cur.caseLines;
            }
        }
        else if (name == QLatin1String("REPEAT") || name == QLatin1String("WHILE")
                 || name == QLatin1String("FOR") || name == QLatin1String("DO")) {
            loopStack.append({i, name});
        }
        else if (name == QLatin1String("LOOP") || name == QLatin1String("REND") || name == QLatin1String("WEND")
                 || name == QLatin1String("NEXT")) {
            const QString expected = (name == QLatin1String("REND")) ? QStringLiteral("REPEAT")
                                   : (name == QLatin1String("LOOP"))
                                       ? ((!loopStack.isEmpty() && loopStack.last().type == QLatin1String("DO"))
                                           ? QStringLiteral("DO") : QStringLiteral("REPEAT"))
                                   : (name == QLatin1String("WEND")) ? QStringLiteral("WHILE")
                                                                     : QStringLiteral("FOR");
            if (!loopStack.isEmpty() && loopStack.last().type == expected) {
                const LoopInfo info = loopStack.takeLast();
                data.loopEndLines[info.startLine] = i;
                data.jumpToEnd[info.startLine] = i;
                data.jumpTo[i] = info.startLine;
                data.lines[i].jumpTo = info.startLine;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 变量表：解析 #DIM/#DIMS/#GLOBAL/#GLOBALS/#PRIVATE，并把类型回填到 AST
// ---------------------------------------------------------------------------
namespace {
// 去掉指令行的 ';' 注释（尊重引号）；#DIM CONST X = 0 ;说明 需要它
QString stripDirectiveComment(const QString& text) {
    QChar quote;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (!quote.isNull()) {
            if (c == quote) quote = QChar();
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; continue; }
        if (c == QLatin1Char(';')) return text.left(i);
    }
    return text;
}
} // namespace

void EraParseTable::parseVariableDeclaration(const LogicalLine& line, const QString& currentFunction,
                                            bool isHeaderFile) {
    QString s = line.raw.trimmed();
    if (!s.startsWith(QLatin1Char('#'))) return;
    s = s.mid(1);

    int i = 0;
    while (i < s.size() && !s.at(i).isSpace()) ++i;
    const QString directive = s.left(i).toUpper();
    const QString rest = stripDirectiveComment(s.mid(i)).trimmed();

    bool isStr = false;
    bool isGlobal = false;
    bool isPrivate = false;
    if (directive == QLatin1String("DIM")) {
        // 默认：函数内私有 / 文件级全局
    } else if (directive == QLatin1String("DIMS")) {
        isStr = true;
    } else if (directive == QLatin1String("GLOBAL")) {
        isGlobal = true;
    } else if (directive == QLatin1String("GLOBALS")) {
        isGlobal = true;
        isStr = true;
    } else if (directive == QLatin1String("PRIVATE")) {
        isGlobal = true;
        isPrivate = true;
    } else {
        return;   // 其它 # 行（#FUNCTION/#SINGLE/…）不在此处理
    }

    if (rest.isEmpty()) {
        m_parseWarnings.append(QStringLiteral("%1: #%2 缺少变量名")
                                   .arg(line.position.toString(), directive));
        return;
    }

    try {
        const UserDefinedVariableData d =
            UserDefinedVariableData::create(rest, isStr, /*isPrivate=*/true, line.position);

        // CONST 声明的初值即常数（供 #DIM 维数引用）
        if (d.isConst) {
            if (d.typeIsStr) {
                if (!d.defaultStr.isEmpty()) m_variables.setConstStr(d.name, d.defaultStr.first());
            } else if (!d.defaultInt.isEmpty()) {
                // 常数**数组**（`#DIM CONST NAME, N = v0, v1, …`）也要存下来，
                // 供 `NAME:i` 的下标访问使用；标量常数取第一个值
                m_variables.setConstArray(d.name, d.defaultInt);
            }
        }

        VariableDecl decl;
        decl.name = d.name;
        decl.type = d.typeIsStr ? OperandType::Str : OperandType::Int;
        // 全局判定：GLOBAL 关键字 / 头文件文件级 / 不在任何函数内
        decl.scope = (isGlobal || d.global || isHeaderFile || currentFunction.isEmpty())
                         ? VarScope::Global : VarScope::Local;
        decl.function = (decl.scope == VarScope::Local) ? currentFunction : QString();
        decl.dimension = d.dimension;
        decl.lengths = d.lengths;
        decl.lengthExprs = d.lengthExprs;
        decl.isPrivate = isPrivate || !isGlobal;
        decl.isConst = d.isConst;
        decl.isReference = d.reference;
        decl.isCharaData = d.charaData;

        // 用户 `#DIM(S) CHARADATA`：登记为角色数据变量，供读写路径按
        // (角色号, 元素下标) 存取（否则元素互相覆盖，表现为「变量似乎不可变」）。
        if (decl.isCharaData && !decl.isReference && m_variableStorage) {
            m_variableStorage->registerCharaDataVariable(
                decl.name, decl.type == OperandType::Str, decl.dimension);
        }
        if (!d.isConst) {                 // 初值（非 CONST）：进入函数/装载时写入
            decl.defaultInt = d.defaultInt;
            decl.defaultStr = d.defaultStr;
        }

        // 重复声明（first-wins）：与 C# 一样，全局变量重名一律告警（类型/维度是否
        // 一致都不影响后续使用，但重名通常意味着脚本有误）；函数局部重名在真实
        // 游戏里很常见（同名 @label 被口上补丁覆盖），保持静默。
        const VariableTable::DeclStatus status = m_variables.addChecked(decl);
        if (status != VariableTable::DeclStatus::Added && decl.scope == VarScope::Global) {
            m_parseWarnings.append(QStringLiteral("%1: 全局变量 %2 重复定义")
                                       .arg(line.position.toString(), decl.name));
        }
    } catch (const std::exception& e) {
        m_parseWarnings.append(QStringLiteral("%1: #%2 声明错误：%3 (%4)")
                                   .arg(line.position.toString(), directive,
                                        QString::fromUtf8(e.what()), rest));
    }
}

void EraParseTable::applyVariableTypes() {
    // 1) 缓存里的 AST
    for (auto& ast : m_astCache) {
        if (ast) VariableTable::applyTypes(*ast, m_variables);
    }
    // 2) 各行实际引用的 AST（并行装载时 worker 的 AST 不在主缓存中）
    QHash<QString, QHash<const ExpressionNode*, QSharedPointer<ExpressionNode>>> bound;
    QList<QSharedPointer<ExpressionNode>> originals; // keep identity keys alive while rebinding
    for (auto sit = m_scripts.begin(); sit != m_scripts.end(); ++sit) {
        for (LogicalLine& line : sit.value().lines) {
            auto& cache = bound[line.ownerFunction.toUpper()];
            const auto bind = [&](QSharedPointer<ExpressionNode>& ast) {
                if (!ast) return;
                const auto* original = ast.data();
                if (!cache.contains(original)) {
                    originals.append(ast);
                    auto copy = cloneExpression(ast);
                    VariableTable::applyTypes(*copy, m_variables, line.ownerFunction);
                    cache.insert(original, copy);
                }
                ast = cache.value(original);
            };
            for (auto& op : line.arguments) bind(op.ast);
            bind(line.condition);
            ArgumentParser::build(line);
            if (line.functionName == "HTML_PRINT" && !line.arguments.isEmpty())
                line.printTemplate = PrintTemplateCompiler::compile(line.arguments.first().ast);
            else if (line.printTemplate) {
                line.printTemplate = QSharedPointer<PrintTemplate>::create(*line.printTemplate);
                for (auto& part : line.printTemplate->parts) bind(part.expression);
            }
        }
    }
}

void EraParseTable::mergeAstCache(const QHash<QString, QSharedPointer<ExpressionNode>>& localCache) {
    for (auto it = localCache.constBegin(); it != localCache.constEnd(); ++it) {
        if (!m_astCache.contains(it.key())) {
            m_astCache.insert(it.key(), it.value());
        }
    }
}

void EraParseTable::finalizeParse() {
    m_variables.resolveDimensions();
    // #DIM CONST 常量：交给求值器在求值时查表（装载顺序无关）
    if (m_evaluator) {
        const VariableTable* vt = &m_variables;
        m_evaluator->setConstProvider([vt](const QString& name, QVariant& out) -> bool {
            qint64 iv = 0;
            if (vt->constInt(name, iv)) { out = QVariant::fromValue<qint64>(iv); return true; }
            QString sv;
            if (vt->constStr(name, sv)) { out = QVariant(sv); return true; }
            return false;
        });
        // 常数数组（`#DIM CONST X, N = …`）：先判名（避免下标副作用被求两遍）
        m_evaluator->setConstArrayChecker([vt](const QString& name) -> bool {
            return vt->constArraySize(name) > 0;
        });
        m_evaluator->setConstArrayProvider([vt](const QString& name, int index, QVariant& out) -> bool {
            qint64 iv = 0;
            if (!vt->constArrayAt(name, index, iv)) return false;
            out = QVariant::fromValue<qint64>(iv);
            return true;
        });
    }
    applyGlobalVariableDefaults();   // #DIM X = 1 等初值（全局）
    // 用户自定义函数的强类型化（形参类型回填 + 返回类型确定）
    resolveUserFunctionTypes();
    // 字符串赋值的右值改按 StrForm 解析（对齐 C# AnalyseFormattedString）
    applyStringAssignments();
    applyVariableTypes();
    // 函数调用重绑：装载顺序无关（并行分块时，某块的表达式可能先于
    // 它调用的 #FUNCTION 被解析，此时只能当「未定义」；到此处全部脚本已 merge）
    resolveFunctionNodes();
    // 参数/类型校验必须在「变量类型已回填」之后进行：
    // 否则 LFONTS 之类用户 #DIMS 变量在解析期还是默认的 Int，
    // 会误报「需要字符串表达式，实得 Int」。
    validateArguments();
    m_scopedAstCache.clear();
    m_finalized = true;
    qDebug() << "[parse] finalizeParse 完成：脚本" << m_scripts.size()
             << "变量" << m_variables.count() << "用户函数" << m_functions.size()
             << "告警" << m_parseWarnings.size();
}

// ---------------------------------------------------------------------------
// 函数调用重绑 + 校验（对齐 C# IdentifierDictionary.GetFunctionMethod）
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 字符串赋值：右值按 StrForm 解析（对齐 C# SP_SET_ArgumentBuilder）
//
//   C# 里 `STRVAR = <剩余全部文本>` 走 LexicalAnalyzer.AnalyseFormattedString，
//   即「文本 + {expr}/%expr%」——所以 eraTW 的
//       PNAME = 妖怪之山 (山麓)
//       PNAME = %GET_MAPNAME(MAPID)%への道中
//   都是合法的（前者整体是字面文本）。若当表达式解析，前者会变成
//   「未定义的函数 妖怪之山」，后者直接解析失败。
//
//   变量类型要到所有 #DIM/#DIMS 都解析完之后才确定，故放在 finalize。
// ---------------------------------------------------------------------------
namespace {

// 取赋值左值的首标识符（跳过 A:1 / GLOBAL:5 这类下标）
QString leadingIdentifier(const QString& raw) {
    int i = 0;
    while (i < raw.size()) {
        const QChar c = raw.at(i);
        if (c.isSpace() || c == QLatin1Char(':') || c == QLatin1Char('[')
            || c == QLatin1Char('(') || c == QLatin1Char('=')) break;
        ++i;
    }
    return raw.left(i).trimmed();
}

} // namespace

// 把 #DIM/#DIMS 的初值写入存储（对齐 C#：全局变量装载时取初值；
// 函数私有变量每次进入函数时取初值）。ScriptRunner 在进入函数时选择
// 私有名字空间，因此同名数组的初值不会覆盖调用者。
void EraParseTable::applyVariableDefaults(const QList<VariableDecl>& decls) {
    if (!m_variableStorage) return;
    for (const VariableDecl& d : decls) {
        if (d.isConst || !d.isPrivate || d.isReference || d.isCharaData) continue;
        if (!d.lengths.isEmpty())
            m_variableStorage->ensureArraySize(d.name, d.lengths.first(), d.type == OperandType::Str);
        if (d.type == OperandType::Str) {
            for (int i = 0; i < d.defaultStr.size(); ++i) {
                m_variableStorage->setGlobalStr1D(d.name, i, d.defaultStr.at(i));
            }
        } else {
            for (int i = 0; i < d.defaultInt.size(); ++i) {
                m_variableStorage->setGlobalInt1D(d.name, i, d.defaultInt.at(i));
            }
        }
    }
}

void EraParseTable::applyGlobalVariableDefaults() {
    for (const VariableDecl& d : m_variables.declarations()) {
        if (d.scope != VarScope::Global || d.isConst) continue;
        applyVariableDefaults({d});
    }
}

void EraParseTable::applyPrivateVariableDefaults(const QString& function) {
    if (function.isEmpty()) return;
    applyVariableDefaults(m_variables.localsOf(function));
}

void EraParseTable::applyStringAssignments() {
    const AstResolver resolve = [this](const QString& e) { return expressionAst(e); };
    for (auto sit = m_scripts.begin(); sit != m_scripts.end(); ++sit) {
        for (LogicalLine& line : sit.value().lines) {
            if (line.kind != LineKind::Instruction) continue;
            // C# uses two assignment operators with different RHS semantics:
            //   =  : string variables consume the remaining source as a StrForm
            //        (plain text plus {expr}/%expr% interpolation).
            //   '= : expression string assignment; keep the normal expression AST.
            // The initial AST is intentionally built before variable declarations
            // are finalized, so it may look like a call for text such as
            // `白狼天狗服(色固定)`.  Do not use that provisional node to decide
            // whether the RHS is raw text.
            if (line.assignOperator != QLatin1String("=")) continue;
            if (line.arguments.size() != 2) continue;

            Operand& dest = line.arguments[0];
            Operand& value = line.arguments[1];

            const QString varName = leadingIdentifier(dest.raw);
            if (varName.isEmpty()) continue;
            if (m_variables.typeOf(varName, line.ownerFunction) != OperandType::Str) continue;

            // A bare, declared string variable is a value reference (`NAME = RESULTS`).
            // Other RHS text uses formatted-string semantics, even when it looks
            // like a call (`CLOTH = 白狼天狗服(色固定)`).
            const QString rhsName = value.raw.trimmed();
            bool bareName = !rhsName.isEmpty()
                            && (rhsName.at(0).isLetter() || rhsName.at(0) == QLatin1Char('_'));
            for (int i = 1; bareName && i < rhsName.size(); ++i) {
                const QChar c = rhsName.at(i);
                bareName = c.isLetterOrNumber() || c == QLatin1Char('_');
            }
            if (bareName && m_variables.typeOf(rhsName, line.ownerFunction) == OperandType::Str)
                value.ast = resolve(rhsName);
            else
                value.ast = StrFormParser::parse(value.raw, resolve);
        }
    }
}

// ---------------------------------------------------------------------------
// 用户自定义函数的强类型化
//
//   * 形参类型：ARG -> Int、ARGS -> Str、私有变量 -> 变量表里的 #DIM/#DIMS 类型
//   * 返回类型：#FUNCTION -> Int、#FUNCTIONS -> Str；
//     没有 # 行的 @label 也按 Int（式中调用在实测游戏里普遍存在）
// ---------------------------------------------------------------------------
void EraParseTable::resolveUserFunctionTypes() {
    for (auto it = m_functions.begin(); it != m_functions.end(); ++it) {
        UserFunctionDecl& decl = it.value();
        if (!decl.isMethod || !isKnown(decl.returnType)) {
            decl.returnType = OperandType::Int;
        }
        for (UserParamDecl& p : decl.params) {
            switch (p.target) {
            case UserParamTarget::Arg:
                p.type = OperandType::Int;
                break;
            case UserParamTarget::Args:
                p.type = OperandType::Str;
                break;
            case UserParamTarget::LocalVar: {
                const VariableDecl* variable = m_variables.find(p.varName, decl.name);
                const OperandType t = variable ? variable->type
                                               : m_variables.typeOf(p.varName, decl.name);
                if (isKnown(t)) {
                    p.type = t;
                    p.typeKnown = true;   // 私有变量由 #DIM/#DIMS 定类型
                    p.isReference = variable && variable->isReference;
                } else {
                    p.type = OperandType::Int;   // 绑定按整数槽处理，但不参与类型校验
                    p.typeKnown = false;
                }
                break;
            }
            case UserParamTarget::Unknown:
            default:
                if (!isKnown(p.type)) p.type = OperandType::Int;
                break;
            }
        }
    }
}

// 式中调用的强类型校验（复刻 C# Process.CalledFunction.ConvertArg）：
//
//   * 实参个数超过形参个数      -> 错误（C#「引数の数が…超えています」）
//   * 字符串实参 -> 整型形参    -> 错误（C#「文字列型から整数型に変換できません」，
//                                  任何配置下都不允许）
//   * 整型实参 -> 字符串形参    -> 允许（C# CompatiFuncArgAutoConvert /
//                                  「ユーザー関数の引数に自動的にTOSTRを補完する」；
//                                  eraTW 等实际游戏普遍设为 YES）
//
//   仅当形参类型来自真实声明（typeKnown：ARG/ARGS，或私有变量有 #DIM/#DIMS）才判定。
QString EraParseTable::checkUserCallArgs(const UserFunctionDecl& decl,
                                         const QList<OperandType>& argTypes) const {
    const int want = decl.paramCount();
    if (argTypes.size() > want) {
        return QStringLiteral("函数 %1 的实参过多（形参 %2 个，实得 %3 个）")
            .arg(decl.name).arg(want).arg(argTypes.size());
    }
    for (int i = 0; i < argTypes.size(); ++i) {
        const UserParamDecl& p = decl.params.at(i);
        const OperandType actual = argTypes.at(i);
        if (!p.typeKnown || !isKnown(actual) || !isKnown(p.type)) continue;
        if (actual == OperandType::Str && p.type == OperandType::Int) {
            return QStringLiteral("函数 %1 第 %2 个实参需要%3，实得字符串（不能从字符串转换为整数）")
                .arg(decl.name).arg(i + 1)
                .arg(QString::fromUtf8(operandTypeName(p.type)));
        }
    }
    return QString();
}

void EraParseTable::resolveFunctionNodes() {
    const auto userType = [this](const QString& name) -> OperandType {
        if (const UserFunctionDecl* fn = userFunction(name)) {
            return fn->isMethod ? fn->returnType : OperandType::Int;
        }
        return OperandType::Unknown;
    };

    const auto resolveNode = [this, &userType](ExpressionNode& node) {
        if (node.kind() != NodeKind::Function) return;
        auto& fn = static_cast<FunctionNode&>(node);
        fn.setUserFunction(false);
        fn.setBuiltinIndex(-1);

        const FunctionResolution res = resolveFunctionCall(fn.name(), userType);
        if (res.isUserFunction) {
            fn.setUserFunction(true);
            fn.setValueType(res.returnType);
            // 强类型：按声明的形参类型逐个校验实参（复刻 C# UserDefinedMethodTerm.Create）
            const UserFunctionDecl* decl = userFunction(fn.name().toUpper());
            if (decl) {
                QList<OperandType> argTypes;
                argTypes.reserve(fn.arguments().size());
                for (const auto& a : fn.arguments()) {
                    argTypes.append(a ? a->valueType() : OperandType::Unknown);
                }
                fn.setArityError(checkUserCallArgs(*decl, argTypes));
            } else {
                fn.setArityError(QString());
            }
        } else if (res.isBuiltin) {
            fn.setBuiltinIndex(res.builtinIndex);
            fn.setValueType(res.returnType);
            fn.setArityError(validateBuiltinCall(kBuiltinFunctions[res.builtinIndex], fn.arguments()));
        } else if (const UserFunctionDecl* decl = userFunction(fn.name().toUpper())) {
            // 有同名 @label 但既非内置也无 #FUNCTION：按宽容语义当作返回 Int 的用户函数
            // （eraTW 大量如此使用，实际可运行），但仍按声明的形参类型做校验。
            fn.setUserFunction(true);
            fn.setValueType(isKnown(decl->returnType) ? decl->returnType : OperandType::Int);
            QList<OperandType> argTypes;
            argTypes.reserve(fn.arguments().size());
            for (const auto& a : fn.arguments()) {
                argTypes.append(a ? a->valueType() : OperandType::Unknown);
            }
            fn.setArityError(checkUserCallArgs(*decl, argTypes));
        } else {
            fn.setValueType(OperandType::Unknown);
            fn.setArityError(QStringLiteral("未定义的函数 %1").arg(fn.name()));
        }
    };

    for (auto& ast : m_astCache) {
        if (ast) walkExpression(*ast, resolveNode);
    }
    // 并行装载时 worker 的 AST 不在主缓存里，需按行再走一遍
    for (auto sit = m_scripts.begin(); sit != m_scripts.end(); ++sit) {
        for (LogicalLine& line : sit.value().lines) {
            for (const Operand& op : line.arguments) {
                if (op.ast) walkExpression(*op.ast, resolveNode);
            }
            for (const auto& e : line.argument.exprs) {
                if (e) walkExpression(*e, resolveNode);
            }
            for (const Operand& c : line.argument.cases) {
                if (c.ast) walkExpression(*c.ast, resolveNode);
            }
            if (line.condition) walkExpression(*line.condition, resolveNode);
        }
    }
}

// 收集一个 AST 里的函数调用告警（内置函数参数不符 / 未定义的函数）。
// position 为空时只报函数级消息；seen 用于去重（同一表达式被多行共享）。
void EraParseTable::collectFunctionWarnings(const QSharedPointer<ExpressionNode>& ast,
                                            const QString& position, QSet<QString>& seen) {
    if (!ast) return;
    walkExpression(*ast, [this, &seen, &position](ExpressionNode& node) {
        if (node.kind() != NodeKind::Function) return;
        const auto& fn = static_cast<const FunctionNode&>(node);
        const QString& err = fn.arityError();
        if (err.isEmpty()) return;
        const QString key = fn.name().toUpper() + QLatin1Char('\x1f') + err;
        if (seen.contains(key)) return;
        seen.insert(key);
        m_parseWarnings.append(QStringLiteral("%1: %2 [%3]")
                                   .arg(position, err, fn.toString()));
    });
}

void EraParseTable::validateArguments() {
    QSet<QString> seenFunctions;   // 同一函数表达式被多行共享时只报一次
    for (auto sit = m_scripts.begin(); sit != m_scripts.end(); ++sit) {
        for (LogicalLine& line : sit.value().lines) {
            if (line.kind != LineKind::Instruction) continue;
            ArgumentParser::build(line);   // 幂等：重算 kind/params/exprs + 重新校验
            if (line.argument.hasError()) {
                m_parseWarnings.append(QStringLiteral("%1: %2 (%3)")
                                           .arg(line.position.toString(),
                                                line.argument.typeError,
                                                line.raw.trimmed()));
            }
            const QString pos = line.position.toString();

            // 注：CALL 语句**不做**实参类型校验 —— 对齐 C#（SP_CALL_ArgumentBuilder 不调用
            // ConvertArg，实参按「实际类型」装进 Transporter 后由被调函数自行解释）。
            // 强类型校验只发生在「式中调用」NAME(args) 处（见 resolveFunctionNodes）。

            // 函数调用（内部命令/内置函数）参数校验告警
            collectFunctionWarnings(line.condition, pos, seenFunctions);
            for (const Operand& op : line.arguments) {
                collectFunctionWarnings(op.ast, pos, seenFunctions);
            }
            for (const auto& e : line.argument.exprs) {
                collectFunctionWarnings(e, pos, seenFunctions);
            }
            for (const Operand& c : line.argument.cases) {
                collectFunctionWarnings(c.ast, pos, seenFunctions);
            }
        }
    }
}
