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
#include <QCoreApplication>
#include "process_state.h"
#include "eraengine_log.h"   // eraTrace（逐文件高频日志）
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
#include <QElapsedTimer>
#include <QSet>
#include <algorithm>
#include <utility>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>

namespace {
// 并行化开关：任务数 >1 且线程池 >1 时启用（==1 时顺序执行，避免自锁 ——
// finalizeParse 本身可能已在 QtConcurrent 的线程上，blockingMap 会再切子任务）。
inline bool canParallelize(int n) {
    return n > 1 && QThreadPool::globalInstance()->maxThreadCount() > 1;
}
}  // namespace

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

bool EraParseTable::ignoreTripleSymbols() const {
    return m_evaluator && m_evaluator->ignoreTripleSymbols();
}

void EraParseTable::setVariableStorage(VariableStorage* storage) {
    m_variableStorage = storage;
}

void EraParseTable::setExpressionEvaluator(ExpressionEvaluator* evaluator) {
    m_evaluator = evaluator;
    if (evaluator) evaluator->setAstProvider([this](const QString& text) { return expressionAst(text); });
    if (evaluator) evaluator->setVariableDimProvider([this](const QString& name) {
        const auto* line = lineAt(m_currentScript, m_currentLine);
        const auto* decl = m_variables.find(name, line ? line->ownerFunction : QString());
        return decl ? decl->dimension : 1;
    });
}

// ---------------------------------------------------------------------------
// 只读区：表达式 AST 缓存（唯一解析流水线）
// ---------------------------------------------------------------------------

QSharedPointer<ExpressionNode> EraParseTable::expressionAst(const QString& expr, bool quiet) {
    // 作用域来自「当前执行位置」。finalize 阶段这个位置是陈旧的（装载线程的
    // 位置停在别处），所以 finalize 内部的调用必须走 expressionAstInScope 显式
    // 传 ownerFunction，否则会按「无作用域」解析。
    QString scope;
    if (m_finalized) {
        if (const auto* line = lineAt(m_currentScript, m_currentLine)) scope = line->ownerFunction;
    }
    return expressionAstInScope(expr, scope, quiet);
}

QSharedPointer<ExpressionNode> EraParseTable::expressionAstInScope(const QString& expr,
                                                                  const QString& scope,
                                                                  bool quiet) {
    const QString key = expr.trimmed();
    if (key.isEmpty()) {
        return nullptr;
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
    parser.setQuiet(quiet);
    parser.setVariableTypeProvider([this, scope](const QString& name) { return m_variables.typeOf(name, scope); });   // 赋值右值的临时解析：不刷「表达式语法错误」
    // 强类型：仅用户自定义函数由 Provider 决定；内置函数（内部命令）由
    // ExpressionParser 内部的 kBuiltinFunctions 目录解析（对齐 C# methodDic）。
    // 装载期间用户函数可能尚未 merge，finalizeParse 会再统一重绑一次。
    parser.setFunctionTypeProvider([this](const QString& name) -> OperandType {
        // #FUNCTIONS -> Str；#FUNCTION -> Int；无 # 行的 @label 按 Int（宽容语义）。
        // 与内置函数同名的、无 #FUNCTION 的 @label **不覆盖**内置函数（见 userCallReturnType）。
        return userCallReturnType(name);
    });

    // 格式化串：@"..." / \@...#...\@
    parser.setFormProvider([this, scope](const QString& text, bool yenAt) -> QSharedPointer<ExpressionNode> {
        const AstResolver resolve = [this, scope](const QString& e) { return expressionAstInScope(e, scope); };
        if (yenAt) return StrFormParser::parseYenAt(text, resolve, m_evaluator && m_evaluator->ignoreTripleSymbols());
        return QSharedPointer<ExpressionNode>(StrFormParser::parse(text, resolve, m_evaluator && m_evaluator->ignoreTripleSymbols()));
    });
    if (m_constantTable) {
        const ConstantTable* ct = m_constantTable;
        parser.setConstantNameProvider([ct](const QString& var, const QString& name) {
            return ct->indexForVariable(var, name) >= 0;
        });
    }
    // `#DIM CONST NAME = value`：解析期折叠为字面量（与 CSV 常数名区分：这里返回**值**）。
    // **必须按作用域遮蔽**：函数内的可写私有变量（`#DIM 色_RED7`）与别处的
    // `#DIM CONST 色_RED7` 同名时，常量表只有一份（按名字），会把这个可写变量
    // 的**左值**折成字面量 —— eraTW 的 CASINO 色_*（26 行）就是这么整行报
    // 「赋值左值必须为有效变量项」并失效的。C# 里函数内声明的 CONST 是该函数的
    // 私有常量（UserDefinedVariable.cs: ret.Private = isPrivate），可见性受作用域约束。
    {
        const VariableTable* vt = &m_variables;
        parser.setConstantValueProvider([vt, scope](const QString& name) -> QVariant {
            if (!scope.isEmpty()) {
                if (const VariableDecl* d = vt->find(name, scope)) {
                    if (!d->isConst) return QVariant();   // 可写变量遮蔽同名常量
                }
            }
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
    const auto cached = m_hasLabelCache.constFind(label);
    if (cached != m_hasLabelCache.constEnd()) return cached.value();
    bool found = false;
    for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) {
        if (it.value().labelPositions.contains(label.toUpper())) { found = true; break; }
    }
    m_hasLabelCache.insert(label, found);
    return found;
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

int EraParseTable::catchTarget(const QString& scriptName, int trycLine) const {
    const ScriptData* data = script(scriptName);
    if (!data) return -1;
    return data->catchLines.value(trycLine, -1);
}

int EraParseTable::endCatchTarget(const QString& scriptName, int catchLine) const {
    const ScriptData* data = script(scriptName);
    if (!data) return -1;
    return data->endCatchLines.value(catchLine, -1);
}

QList<int> EraParseTable::funcEntryLines(const QString& scriptName, int listLine) const {
    const ScriptData* data = script(scriptName);
    if (!data) return {};
    return data->funcEntries.value(listLine);
}

int EraParseTable::endFuncTarget(const QString& scriptName, int listLine) const {
    const ScriptData* data = script(scriptName);
    if (!data) return -1;
    return data->endFuncLines.value(listLine, -1);
}

const UserFunctionInfo* EraParseTable::userFunction(const QString& name) const {
    auto it = m_functions.constFind(name.toUpper());
    return it == m_functions.constEnd() ? nullptr : &it.value();
}

// 用户函数在**表达式调用**里的返回类型；返回 Unknown 表示「按内置函数解析」。
//
// 对齐 C# IdentifierDictionary.GetFunctionMethod（装配顺序**无关**：标签表全局唯一，
// 装载完成后统一解析；重名标签在装载期另有告警）：
//   1) 用户 @label 优先于内置函数（methodDic）——所以同名时执行用户函数；
//   2) 但 1.721 起「#FUNCTION の無い関数は組み込み関数を上書きしない」：
//      没有 #FUNCTION/#FUNCTIONS 的同名 @label 只是普通流程函数，式子里的同名调用
//      仍走内置函数（C# 源码 'PANCTION.ERB 的 RAND とか'）。
//
// 本移植对「非内置名字 + 无 #FUNCTION 的 @label」保留宽容语义（eraTW 大量这样在式子里
// 调用普通标签，返回 Int）。此前该宽容语义也套用到了与内置函数同名的标签上
// （era_parse_table 侧一律返回 Int），而 erb_loader 侧的解析期类型表只收 #FUNCTION 标签
// ——同一个调用在「解析期」被当成内置函数、到 finalize 又被重绑成用户函数，
// 两步结论不一致。现在两处都走本函数。
OperandType EraParseTable::userCallReturnType(const QString& name) const {
    const UserFunctionDecl* fn = userFunction(name);
    if (!fn) return OperandType::Unknown;
    if (fn->isMethod) return isKnown(fn->returnType) ? fn->returnType : OperandType::Int;
    // 无 #FUNCTION：#FUNCTION が無い関数は組み込み関数を上書きしない
    const std::string upper = name.toUpper().toStdString();
    if (builtinFunctionIndex(upper) >= 0 || findExtensionFunction(upper) != nullptr) {
        return OperandType::Unknown;
    }
    return OperandType::Int;
}

int EraParseTable::functionExistsKind(const QString& name, bool caseInsensitive) const {
    const auto kindOf = [](const UserFunctionInfo& fn) -> int {
        if (!fn.isMethod) return 1;                       // 通常関数
        return fn.returnType == OperandType::Str ? 3 : 2; // #FUNCTIONS / #FUNCTION
    };
    if (caseInsensitive) {
        if (const UserFunctionInfo* fn = userFunction(name)) return kindOf(*fn);
        return 0;
    }
    // 大小写敏感：按声明处原始名精确匹配（原版 EmueraEE 第二参为 0 时的默认行为）
    for (auto it = m_functions.constBegin(); it != m_functions.constEnd(); ++it) {
        if (it.value().originalName == name) return kindOf(it.value());
    }
    return 0;
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
        if (m_state) m_state->setErrorState();
        return false;
    }
    ExpressionEvaluator local;
    ExpressionEvaluator* ev = m_evaluator ? m_evaluator : &local;
    const QVariant v = ev->evaluate(*ast, m_variableStorage, m_gameBaseData);
    if (!v.isValid()) {
        if (m_state) m_state->setErrorState();
        return false;
    }
    out = v.toLongLong() != 0;
    return true;
}

bool EraParseTable::evaluateExpression(const QString& expr, bool& out) {
    const QSharedPointer<ExpressionNode> ast = expressionAst(expr);
    if (!ast) {
        if (m_state) m_state->setErrorState();
        return false;
    }
    return evaluateAst(ast, out);
}

bool EraParseTable::evaluateCondition(const QString& scriptName, int line, bool& out, int argIndex) {
    const LogicalLine* ll = lineAt(scriptName, line);
    if (!ll) {
        return false;
    }
    if (ll->condition) {
        return evaluateAst(ll->condition, out);
    }
    if (argIndex >= 0 && argIndex < ll->arguments.size() && ll->arguments.at(argIndex).ast) {
        return evaluateAst(ll->arguments.at(argIndex).ast, out);
    }
    return evaluateExpression(ll->raw, out);
}

// ---------------------------------------------------------------------------
// 只读区：装载（AST 由 ErbLoader/AstBuilder 预先构建）
// ---------------------------------------------------------------------------

void EraParseTable::clear() {
    // QUIT 关闭游戏：释放全部解析期数据（AST/标记区/函数表/标签/告警/变量声明）
    m_scripts.clear();
    m_astCache.clear();
    m_scopedAstCache.clear();
    m_hasLabelCache.clear();
    m_functions.clear();
    m_labelLists.clear();
    m_diagnostics.clear();
    m_variables.clear();
    m_entryPoint.clear();
    // 执行位置一并复位：脚本已不存在，任何残留帧/位置都悬空
    m_currentScript.clear();
    m_currentLine = 0;
    m_callStack.clear();
    m_depth = 0;
    m_jumped = false;
    m_finalized = false;
    emit positionChanged(QString(), 0);
    emit callStackChanged(0);
}

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
                m_diagnostics.add(DiagSeverity::Warning, DiagCode::kSharpLine,
                                  line.position.filename, line.position.lineNumber, line.position.column, 0,
                                  QCoreApplication::translate("ParseDiagnostics", "函数声明之外使用了 # 行 (%1)")
                                      .arg(line.raw.trimmed()));
            }
        }
        if (line.kind == LineKind::FunctionLabel) {
            line.ownerFunction = currentFunction;   // 函数标签行自身
            afterLabel = true;
        }

        if (line.isLabel()) {
            // 标签名大小写不敏感（对齐 C# ErbLoader.LabelDic 以 ToUpper 为键）：
            // 存储与查找统一折叠为大写。否则会出现「声明 @ForagePlaceName、
            // 表达式调用把名字 toUpper 成 FORAGEPLACENAME 后查不到标签」的错配
            // —— eraTW @SHOW_GATHERING_LIST 的 %ForagePlaceName(...)% 恒返回 0
            // （「採集場所一覧」选项显示成数字 0、输入 99 无法返回）正是此例。
            data.labelPositions[line.labelName.toUpper()] = i;

            // 注册用户自定义函数（@label 即函数入口）—— 构建完整的声明节点
            if (line.kind == LineKind::FunctionLabel) {
                UserFunctionDecl decl;
                decl.name = line.labelName.toUpper();
                decl.originalName = line.labelName;
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

                // 紧随其后的 # 行：是否可作表达式函数 + 事件分组/私有局部尺寸。
                // **空行/注释行不结束声明区**（LineKind::Null）：eraTW 大量写成
                //   @MOA_K28
                //   ;説明
                //   (空行)
                //   #FUNCTIONS
                // 以前在注释/空行处 break -> isMethod 丢失 -> 该 @label 在式子里
                // 不被当作用户函数（%MOA_K28()% 报「FORM插值类型错误」、调用恒返回 0）。
                for (int j = i + 1; j < lines.size()
                     && (lines.at(j).kind == LineKind::Preprocessor || lines.at(j).kind == LineKind::Null); ++j) {
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
    m_hasLabelCache.clear();   // 新脚本可能带来新标签

    // 维数求值与类型回填都推迟到 finalizeParse()：
    // 二者都需要「全部声明/常数就绪」，且是全量操作（避免 O(脚本数 × 声明数)）。

    // 标记区：跳转标记 + 控制转移预绑定
    buildJumpMarkings(scriptName);

    if (m_currentScript.isEmpty()) {
        m_currentScript = scriptName;
    }

    qCDebug(eraTrace) << "[parse] 脚本" << scriptName << (isHeaderFile ? "(头文件)" : "")
             << "逻辑行" << data.lines.size() << "标签" << data.labelPositions.size();
    emit parseCompleted(scriptName);
    return true;
}

void EraParseTable::setEntryPoint(const QString& label) {
    m_entryPoint = label;

    for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) {
        if (it.value().labelPositions.contains(label.toUpper())) {
            m_currentScript = it.key();
            break;
        }
    }

    if (m_currentScript.isEmpty()) {
        const QStringList commonLabels = {"MAIN_LOOP", "SYSTEM_TITLE", "MAIN", "TITLE"};
        for (const QString& labelToTry : commonLabels) {
            for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) {
                if (it.value().labelPositions.contains(labelToTry.toUpper())) {
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
    return data->labelPositions.value(labelName.toUpper(), -1);
}

bool EraParseTable::resolveJumpTarget(const QString& label, int& position) {
    position = getLabelPosition(m_currentScript, label);
    if (position >= 0) {
        return true;
    }

    for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) {
        const int pos = it.value().labelPositions.value(label.toUpper(), -1);
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

const QString& EraParseTable::currentScript() const {
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
    // [qdbug] 修复（eraMegaten 实测差距）：CALL 只能调用 **@函数标签**
    //   （对齐 C# FunctionIdentifierDictionary：函数字典只收 @label，
    //   $GOTO 标签不可调用）。eraMegaten 的 SYSTEM_TITLE.erb:127
    //   `$PRINT_TITLE`（GOTO 标签）与 TITLE_STOCK.ERB:17 `@PRINT_TITLE`
    //   同名共存，此前 CALL 优先命中当前脚本的 $ 标签 -> 自跳死循环。
    //   此处命中行必须是 FunctionLabel，否则继续向后找（跨脚本）。
    const auto callable = [this](const QString& sn, int pos) -> bool {
        const ScriptData* d = script(sn);
        return d && pos >= 0 && pos < d->lines.size()
               && d->lines.at(pos).kind == LineKind::FunctionLabel;
    };
    QString targetScript = m_currentScript;
    int target = getLabelPosition(m_currentScript, label);
    if (target < 0 || !callable(targetScript, target)) {
        target = -1;
        for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) {
            const int pos = it.value().labelPositions.value(label.toUpper(), -1);
            if (pos >= 0 && callable(it.key(), pos)) {
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
            const int pos = it.value().labelPositions.value(label.toUpper(), -1);
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

bool EraParseTable::jumpLabel(const QString& label) {
    if (m_callStack.isEmpty()) {
        // 顶层（系统入口）JUMP：没有可继承的返回帧，退化为普通跳转
        return jumpToLabel(label);
    }
    QString targetScript = m_currentScript;
    int target = getLabelPosition(m_currentScript, label);
    if (target < 0) {
        for (auto it = m_scripts.constBegin(); it != m_scripts.constEnd(); ++it) {
            const int pos = it.value().labelPositions.value(label.toUpper(), -1);
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

    // 新帧继承当前帧的返回地址（对齐 C# IsJump + Return 的递归语义）
    const Frame current = m_callStack.last();
    pushFrame(Frame(current.script, current.returnLine, label, target, /*jump=*/true));
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
    data.catchLines.clear();
    data.endCatchLines.clear();
    data.endFuncLines.clear();
    data.funcEntries.clear();

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

    // TRYC / CATCH / ENDCATCH 的配对栈（对齐 C# ErbLoader.ParseFunction 的 nestStack）
    //   进入 TRYC*  -> 压入 (TRY，行号)
    //   遇到 CATCH  -> 若栈顶是 TRY：记录 tryc->catch，弹栈后压入 (CATCH，行号)
    //   遇到 ENDCATCH -> 若栈顶是 CATCH：记录 catch->endcatch，弹栈
    // 注意：eraTW 里绝大多数 TRYC* 是**没有** CATCH 的（972 : 73），
    // 所以这里只认「栈顶就是 TRY」的 CATCH；否则该 CATCH 属于别的结构，忽略。
    struct CatchInfo {
        bool isCatch = false;
        int  line = -1;
    };
    // TRYCALLLIST/TRYJUMPLIST/TRYGOTOLIST 的配对栈（对齐 C# ErbLoader nestStack）
    //   进入 TRY*LIST -> 压栈；体内 FUNC 条目 -> 归入栈顶列表；
    //   ENDFUNC -> 弹栈并记录列表行 -> ENDFUNC（C# pf.JumpTo = func）。
    struct ListInfo {
        int line = -1;
        QList<int> funcLines;
    };

    QList<IfInfo> ifStack;
    QList<LoopInfo> loopStack;
    QList<SelectInfo> selectStack;
    QList<CatchInfo> catchStack;
    QList<ListInfo> listStack;

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
            catchStack.clear();
            listStack.clear();
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
        // ---- TRYC 异常块：TRYCCALL(FORM) / TRYCJUMP(FORM) / TRYCGOTO(FORM) ----
        else if (name == QLatin1String("TRYCCALL") || name == QLatin1String("TRYCCALLFORM")
                 || name == QLatin1String("TRYCJUMP") || name == QLatin1String("TRYCJUMPFORM")
                 || name == QLatin1String("TRYCGOTO") || name == QLatin1String("TRYCGOTOFORM")) {
            catchStack.append({false, i});
        }
        else if (name == QLatin1String("CATCH")) {
            if (!catchStack.isEmpty() && !catchStack.last().isCatch) {
                const CatchInfo tryInfo = catchStack.takeLast();
                data.catchLines[tryInfo.line] = i;          // TRYC -> CATCH
                catchStack.append({true, i});
            }
        }
        else if (name == QLatin1String("ENDCATCH")) {
            if (!catchStack.isEmpty() && catchStack.last().isCatch) {
                const CatchInfo catchInfo = catchStack.takeLast();
                data.endCatchLines[catchInfo.line] = i;     // CATCH -> ENDCATCH
            }
        }
        // ---- TRYCALLLIST/TRYJUMPLIST/TRYGOTOLIST（+GOTOLIST 宽松别名）----
        // 体内只有 FUNC 单行条目，ENDFUNC 收尾（对齐 C# ErbLoader nestStack）。
        else if (name == QLatin1String("TRYCALLLIST") || name == QLatin1String("TRYJUMPLIST")
                 || name == QLatin1String("TRYGOTOLIST") || name == QLatin1String("GOTOLIST")) {
            ListInfo info;
            info.line = i;
            listStack.append(info);
        }
        else if (name == QLatin1String("FUNC")) {
            if (!listStack.isEmpty()) {
                listStack.last().funcLines.append(i);
            }
        }
        else if (name == QLatin1String("ENDFUNC")) {
            if (!listStack.isEmpty()) {
                const ListInfo listInfo = listStack.takeLast();
                data.endFuncLines[listInfo.line] = i;       // TRY*LIST -> ENDFUNC
                data.funcEntries[listInfo.line] = listInfo.funcLines;
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

// `#DIM CONST NAME = <初值>` 的「初值」部分（按顶层逗号切分）。
// 空初值返回空表 —— eraTW 用它表示「初值写在其后的 { … } 块里」。
QStringList constValueExpressions(const QString& declRest) {
    QChar quote;
    int depth = 0;
    for (int i = 0; i < declRest.size(); ++i) {
        const QChar c = declRest.at(i);
        if (!quote.isNull()) { if (c == quote) quote = QChar(); continue; }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; continue; }
        if (c == QLatin1Char('(') || c == QLatin1Char('[')) { ++depth; continue; }
        if (c == QLatin1Char(')') || c == QLatin1Char(']')) { if (depth > 0) --depth; continue; }
        if (c == QLatin1Char('=') && depth == 0) {
            const QString tail = declRest.mid(i + 1).trimmed();
            if (tail.isEmpty()) return QStringList();
            QStringList out;
            QString current;
            int d2 = 0;
            QChar q2;
            for (const QChar ch : tail) {
                if (!q2.isNull()) { current += ch; if (ch == q2) q2 = QChar(); continue; }
                if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) { q2 = ch; current += ch; continue; }
                if (ch == QLatin1Char('(') || ch == QLatin1Char('[')) { ++d2; current += ch; continue; }
                if (ch == QLatin1Char(')') || ch == QLatin1Char(']')) { if (d2 > 0) --d2; current += ch; continue; }
                if (d2 == 0 && ch == QLatin1Char(',')) { out << current.trimmed(); current.clear(); continue; }
                current += ch;
            }
            if (!current.trimmed().isEmpty()) out << current.trimmed();
            return out;
        }
    }
    return QStringList();
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
        m_diagnostics.add(DiagSeverity::Warning, DiagCode::kSharpLine,
                          line.position.filename, line.position.lineNumber, line.position.column, 0,
                          QCoreApplication::translate("ParseDiagnostics", "#%1 缺少变量名").arg(directive));
        return;
    }

    try {
        const UserDefinedVariableData d =
            UserDefinedVariableData::create(rest, isStr, /*isPrivate=*/true, line.position);

        // CONST 声明的初值即常数（供 #DIM 维数引用）
        if (d.isConst) {
            // 非常量字面量的初值（`#DIM CONST OBJ_ID_LAST = 人物数量上限`）：
            // 按字面量解析只会得到 0（曾因此 INRANGE(…, OBJ_ID_LAST) 恒假，
            // eraTW 的 EXISTOBJ 抛 THROW），而声明顺序又不可靠（跨文件！），
            // 所以保留表达式原文，交给求值期**惰性**计算。
            const QStringList valueExprs = constValueExpressions(rest);
            const bool allLiteral = !valueExprs.isEmpty()
                && std::all_of(valueExprs.begin(), valueExprs.end(), [](const QString& v) {
                       qint64 n = 0;
                       return parseIntegerLiteral(v.trimmed(), n);
                   });
            if (d.typeIsStr) {
                if (d.defaultStr.size() > 1) {
                    // 字符串常数**数组**（`#DIMS CONST X, N = s0, s1, …`，
                    // eraTW 的 DISP_MEMO）：逐下标提供，此前只存第一个值。
                    m_variables.setConstStrArray(d.name, d.defaultStr);
                } else if (!d.defaultStr.isEmpty()) {
                    m_variables.setConstStr(d.name, d.defaultStr.first());
                }
            } else if (!allLiteral && !valueExprs.isEmpty()) {
                m_variables.setConstExprs(d.name, valueExprs);
            } else if (valueExprs.isEmpty()) {
                // `#DIM CONST X, N =` + 紧随其后的 `{ … }` 多行初值块：
                // 本移植尚未支持该写法（常量数组的初值会丢），明确留痕。
                if (d.dimension > 1)
                    qWarning() << "[未完成] #DIM CONST 的多行初值块（{ }）尚未实现:"
                               << d.name << line.position.toString();
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
        // 随 SAVEGLOBAL/LOADGLOBAL 持久化到 save_global.dat 的用户变量：
        // `#DIM SAVEDATA GLOBAL X`（C# IsGlobal && IsSavedata）与 `#DIM GLOBAL X`
        //（ecd/docs「全局变量随 SAVEGLOBAL/LOADGLOBAL 往返」）都算。
        // 注意 `#DIM GLOBAL X` 的 GLOBAL 写在变量名前（d.global），与
        // `#GLOBAL X` 指令（isGlobal）两种写法都要认。
        decl.isGlobalSave = (d.save || d.global) && (d.global || isGlobal);

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
            m_diagnostics.add(DiagSeverity::Warning, DiagCode::kDeclError,
                              line.position.filename, line.position.lineNumber, line.position.column, 0,
                              QCoreApplication::translate("ParseDiagnostics", "全局变量 %1 重复定义").arg(decl.name));
        }
    } catch (const std::exception& e) {
        m_diagnostics.add(DiagSeverity::Error, DiagCode::kDeclError,
                          line.position.filename, line.position.lineNumber, line.position.column, 0,
                          QCoreApplication::translate("ParseDiagnostics", "#%1 声明错误：%2 (%3)")
                              .arg(directive, QString::fromUtf8(e.what()), rest));
    }
}

void EraParseTable::applyVariableTypes() {
    // 1) 缓存里的 AST（共享容器，顺序）
    for (auto& ast : m_astCache) {
        if (ast) VariableTable::applyTypes(*ast, m_variables);
    }
    // 2) 各行实际引用的 AST（并行装载时 worker 的 AST 不在主缓存中）。
    //    **按脚本并行**：每个脚本的克隆缓存原本就是按 ownerFunction 分的，
    //    而函数名全局唯一（一个函数体只在一个脚本里），所以「按脚本」与
    //    「全局按函数」的结果完全等价；VariableTable::applyTypes 取的是
    //    const 变量表（只读），ArgumentParser::build 只改传入的行 —— 无共享写。
    //    eraTW 实测这一步 ~5s（2.2M 行），是 finalizeParse 的最大热点。
    QList<ScriptData*> scripts;
    scripts.reserve(m_scripts.size());
    for (auto it = m_scripts.begin(); it != m_scripts.end(); ++it) scripts.append(&it.value());

    const auto processScript = [this](ScriptData* sd) {
        QHash<QString, QHash<const ExpressionNode*, QSharedPointer<ExpressionNode>>> bound;
        QList<QSharedPointer<ExpressionNode>> originals; // keep identity keys alive while rebinding
        for (LogicalLine& line : sd->lines) {
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
    };
    if (canParallelize(scripts.size())) {
        QtConcurrent::blockingMap(scripts, processScript);
    } else {
        for (ScriptData* sd : scripts) processScript(sd);
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
    // 维数求值放在**常量表就绪之后**：`#DIM X, CLASS_NUM + 1` 这种维数表达式
    // 需要先能查到常量（以前只能识别「数字」和「单个常量名」，
    // 于是 eraTW 的 `#DIMS CLASS_NAME, CLASS_NUM + 1` 退化成 0 元素数组，
    // FINDELEMENT 恒返回 -1 -> EXISTOBJ 抛 THROW）。
    // #DIM CONST 常量：交给求值器在求值时查表（装载顺序无关）
    if (m_evaluator) {
        const VariableTable* vt = &m_variables;
        // 惰性常量表达式缓存（`#DIM CONST OBJ_ID_LAST = 人物数量上限`）
        //   LazyConst = 逐元素的求值结果（标量常量就是单元素）
        using LazyConst = QList<QVariant>;
        auto lazyConst = QSharedPointer<QHash<QString, LazyConst>>::create();
        auto lazyDepth = QSharedPointer<int>::create(0);
        // 求值并缓存某个常量声明的全部元素；返回 nullptr 表示「不是表达式型常量」
        const auto lazyConstValues = [this, vt, lazyConst, lazyDepth](const QString& name)
            -> QSharedPointer<LazyConst> {
            const QString key = name.toUpper();
            const auto cached = lazyConst->constFind(key);
            if (cached != lazyConst->constEnd())
                return QSharedPointer<LazyConst>::create(cached.value());
            const QStringList exprs = vt->constExprs(name);
            if (exprs.isEmpty() || !m_evaluator) return QSharedPointer<LazyConst>();
            if (*lazyDepth >= 16) return QSharedPointer<LazyConst>();   // 循环定义保护
            ++*lazyDepth;
            LazyConst values;
            values.reserve(exprs.size());
            for (const QString& e : exprs)
                values.append(m_evaluator->evaluate(e.trimmed(), m_variableStorage, m_gameBaseData));
            --*lazyDepth;
            lazyConst->insert(key, values);
            return QSharedPointer<LazyConst>::create(values);
        };
        const auto evalLazyConst = [lazyConstValues](const QString& name, int index,
                                                     QVariant& out) -> bool {
            const QSharedPointer<LazyConst> values = lazyConstValues(name);
            if (!values || values->isEmpty()) return false;
            const int i = (index < 0) ? 0 : index;
            out = values->value(i < values->size() ? i : 0);
            return true;
        };
        m_evaluator->setConstProvider([vt, evalLazyConst](const QString& name, QVariant& out) -> bool {
            qint64 iv = 0;
            if (vt->constInt(name, iv)) { out = QVariant::fromValue<qint64>(iv); return true; }
            QString sv;
            if (vt->constStr(name, sv)) { out = QVariant(sv); return true; }
            return evalLazyConst(name, 0, out);
        });
        // 常数数组（`#DIM CONST X, N = …`）：先判名（避免下标副作用被求两遍）
        m_evaluator->setConstArrayChecker([vt](const QString& name) -> bool {
            return vt->constArraySize(name) > 0 || vt->constStrArraySize(name) > 0;
        });
        m_evaluator->setConstArrayProvider([vt, evalLazyConst](const QString& name, int index,
                                                               QVariant& out) -> bool {
            QString sv;
            if (vt->constStrArrayAt(name, index, sv)) {
                out = QVariant(sv);
                return true;
            }
            qint64 iv = 0;
            if (vt->constArrayAt(name, index, iv)) {
                out = QVariant::fromValue<qint64>(iv);
                return true;
            }
            // 没有字面量值：可能是表达式型常量数组（`= (1<<N)-1, …`）
            return evalLazyConst(name, index, out);
        });
    }
    // 维数表达式求值（`#DIM X, A + 1` / `#DIM X, MAXBASE - 1`）：常数表已就绪，
    // 用求值器把维数表达式折叠成整数
    m_variables.setDimEvaluator([this](const QString& expr) -> qint64 {
        if (!m_evaluator) return 0;
        const QVariant v = m_evaluator->evaluate(expr, m_variableStorage, m_gameBaseData);
        return v.isValid() ? v.toLongLong() : 0;
    });
    QElapsedTimer sub;   // 语义阶段各步耗时：eraTW 上 finalizeParse ~10s，需定位热点
    sub.start();
    qint64 tDim = 0, tDefaults = 0, tFnTypes = 0, tStrAssign = 0, tVarTypes = 0,
           tRebind = 0, tValidate = 0;
    m_variables.resolveDimensions();
    tDim = sub.restart();

    applyGlobalVariableDefaults();   // #DIM X = 1 等初值（全局）
    tDefaults = sub.restart();
    // 用户自定义函数的强类型化（形参类型回填 + 返回类型确定）
    resolveUserFunctionTypes();
    tFnTypes = sub.restart();
    // 字符串赋值的右值改按 StrForm 解析（对齐 C# AnalyseFormattedString）
    applyStringAssignments();
    tStrAssign = sub.restart();
    applyVariableTypes();
    tVarTypes = sub.restart();
    // 函数调用重绑：装载顺序无关（并行分块时，某块的表达式可能先于
    // 它调用的 #FUNCTION 被解析，此时只能当「未定义」；到此处全部脚本已 merge）
    resolveFunctionNodes();
    tRebind = sub.restart();
    // Validate every subtree before runtime short circuiting. Only operator/literal
    // trees are evaluated here; user functions and variable values remain dynamic.
    const auto constantTree = [](const ExpressionNode& root) {
        bool constant = true;
        walkExpression(const_cast<ExpressionNode&>(root), [&](ExpressionNode& n) {
            if (n.kind() == NodeKind::Variable || n.kind() == NodeKind::Function) constant = false;
            if (n.kind() == NodeKind::UnaryOp) {
                const auto op = static_cast<const UnaryOpNode&>(n).op().type();
                if (op == TokenType::INCREMENT || op == TokenType::DECREMENT) constant = false;
            }
        });
        return constant;
    };
    for (auto& script : m_scripts) {
        for (auto& line : script.lines) {
            const auto validate = [&](const QSharedPointer<ExpressionNode>& ast) {
                if (!ast) return;
                QString error = validateExpression(*ast);
                if (error.isEmpty() && m_evaluator && m_variableStorage) {
                    walkExpression(*ast, [&](ExpressionNode& n) {
                        if (error.isEmpty() && n.kind() != NodeKind::Literal && constantTree(n)
                            && !m_evaluator->evaluate(n, m_variableStorage, m_gameBaseData).isValid())
                            error = QCoreApplication::translate("ParseDiagnostics", "装载期常量表达式求值失败");
                    });
                }
                if (!error.isEmpty()) {
                    line.argument.typeOk = false;
                    line.argument.typeError = error;
                    line.isError = true;
                    line.errMes = error;
                    m_diagnostics.add(DiagSeverity::Error, DiagCode::kArgCheck,
                        line.position.filename, line.position.lineNumber, line.position.column, 0, error);
                }
            };
            validate(line.condition);
            for (const auto& operand : line.arguments) validate(operand.ast);
            if (!line.assignOperator.isEmpty() && line.arguments.size() == 2) {
                const auto& dest = line.arguments[0].raw;
                const QString name = dest.section(':', 0, 0).trimmed();
                const auto* decl = m_variables.find(name, line.ownerFunction);
                auto target = cloneExpression(expressionAstInScope(dest, line.ownerFunction, true));
                if (target) VariableTable::applyTypes(*target, m_variables, line.ownerFunction);
                // 诊断带上**具体原因**（未定义变量 / 维数不符 / AST 解析失败）：
                // 只报「必须为有效变量项」时，eraTW 这种 10k 变量的工程无从下手。
                const QString lvalueError = !target ? QCoreApplication::translate("ParseDiagnostics", "左值表达式解析失败")
                          : target->kind() != NodeKind::Variable ? QCoreApplication::translate("ParseDiagnostics", "左值不是变量项")
                          : validateExpression(*target);
                if (!lvalueError.isEmpty()) {
                    line.argument.typeOk = false;
                    line.argument.typeError = QCoreApplication::translate("ParseDiagnostics", "赋值左值必须为有效变量项（%1：%2）")
                                                  .arg(dest.left(60), lvalueError);
                    line.isError = true;
                    line.errMes = line.argument.typeError;
                    m_diagnostics.add(DiagSeverity::Error, DiagCode::kArgCheck, line.position.filename,
                        line.position.lineNumber, line.position.column, 0, line.argument.typeError);
                }
                const bool rawForm = line.assignOperator == "=" && m_variables.typeOf(name, line.ownerFunction) == OperandType::Str;
                if (!rawForm) {
                    const auto values = AstBuilder::assignmentValues(line.arguments[1].raw);
                    for (const auto& text : values) {
                        auto ast = cloneExpression(expressionAstInScope(text, line.ownerFunction, true));
                        if (ast) { VariableTable::applyTypes(*ast, m_variables, line.ownerFunction); validate(ast); }
                        else {
                            line.argument.typeOk = false;
                            line.argument.typeError = QCoreApplication::translate("ParseDiagnostics", "赋值右值无法解析或包含空项");
                            line.isError = true;
                            line.errMes = line.argument.typeError;
                            m_diagnostics.add(DiagSeverity::Error, DiagCode::kArgCheck, line.position.filename,
                                line.position.lineNumber, line.position.column, 0, line.argument.typeError);
                        }
                    }
                    if (values.size() > 1 && line.assignOperator != "=" && line.assignOperator != "'=") {
                        line.argument.typeOk = false;
                        line.argument.typeError = QCoreApplication::translate("ParseDiagnostics", "复合赋值不允许列表");
                        line.isError = true;
                        line.errMes = line.argument.typeError;
                    }
                }
                if (decl && decl->isConst) {
                    line.argument.typeOk = false;
                    line.argument.typeError = QCoreApplication::translate("ParseDiagnostics", "不能赋值const变量");
                    line.isError = true;
                    line.errMes = line.argument.typeError;
                }
            }
        }
    }

    // 参数/类型校验必须在「变量类型已回填」之后进行：
    // 否则 LFONTS 之类用户 #DIMS 变量在解析期还是默认的 Int，
    // 会误报「需要字符串表达式，实得 Int」。
    validateArguments();
    tValidate = sub.restart();
    qDebug().noquote() << "[parse] finalizeParse 分步(ms): resolveDimensions" << tDim
                       << " defaults" << tDefaults << " fnTypes" << tFnTypes
                       << " strAssign" << tStrAssign << " varTypes" << tVarTypes
                       << " resolveFunctionNodes" << tRebind << " validateArguments" << tValidate;
    m_scopedAstCache.clear();
    m_finalized = true;
    qDebug() << "[parse] finalizeParse 完成：脚本" << m_scripts.size()
             << "变量" << m_variables.count() << "用户函数" << m_functions.size()
             << "告警" << m_diagnostics.size();
    if (!m_diagnostics.isEmpty()) {
        qDebug().noquote() << "[parse] 诊断汇总：" << m_diagnostics.summarize();
    }
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
    // FORM 里的插值表达式也要用**行的作用域**解析：否则 `%PNAME%`（函数内私有
    // 字符串变量）会按无作用域解析失败，整行报「字符串赋值FORM无效」。
    for (auto sit = m_scripts.begin(); sit != m_scripts.end(); ++sit) {
        for (LogicalLine& line : sit.value().lines) {
            const QString owner = line.ownerFunction;
            const AstResolver resolve = [this, owner](const QString& e) {
                return expressionAstInScope(e, owner);
            };
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

            value.ast = StrFormParser::parse(value.raw.trimmed(), resolve, m_evaluator && m_evaluator->ignoreTripleSymbols());
            if (!value.ast) {
                const QString error = QCoreApplication::translate("ParseDiagnostics", "字符串赋值FORM无效");
                line.argument.typeOk = false;
                line.argument.typeError = error;
                line.isError = true;
                line.errMes = error;
                m_diagnostics.add(DiagSeverity::Error, DiagCode::kArgCheck,
                    line.position.filename, line.position.lineNumber, line.position.column, 0, error);
            }
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
        return QCoreApplication::translate("ParseDiagnostics", "函数 %1 的实参过多（形参 %2 个，实得 %3 个）")
            .arg(decl.name).arg(want).arg(argTypes.size());
    }
    for (int i = 0; i < argTypes.size(); ++i) {
        const UserParamDecl& p = decl.params.at(i);
        const OperandType actual = argTypes.at(i);
        if (!p.typeKnown || !isKnown(actual) || !isKnown(p.type)) continue;
        if (actual == OperandType::Str && p.type == OperandType::Int) {
            return QCoreApplication::translate("ParseDiagnostics", "函数 %1 第 %2 个实参需要%3，实得字符串（不能从字符串转换为整数）")
                .arg(decl.name).arg(i + 1)
                .arg(QString::fromUtf8(operandTypeName(p.type)));
        }
    }
    return QString();
}

void EraParseTable::resolveFunctionNodes() {
    // 与解析期同一判据（era_parse_table.cpp:userCallReturnType）：
    // 用户函数优先，但无 #FUNCTION 时不覆盖同名内置函数。
    const auto userType = [this](const QString& name) -> OperandType {
        return userCallReturnType(name);
    };

    const auto resolveNode = [this, &userType](ExpressionNode& node) {
        if (node.kind() != NodeKind::Function) return;
        auto& fn = static_cast<FunctionNode&>(node);
        fn.setUserFunction(false);
        fn.setBuiltinIndex(-1);
        fn.setExtensionSpec(nullptr);

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
            if (res.extensionSpec) fn.setExtensionSpec(res.extensionSpec);
            else fn.setBuiltinIndex(res.builtinIndex);
            fn.setValueType(res.returnType);
            if (const BuiltinFunctionSpec* spec = fn.builtinSpec()) {
                fn.setArityError(validateBuiltinCall(*spec, fn.arguments()));
            }
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
            fn.setArityError(QCoreApplication::translate("ParseDiagnostics", "未定义的函数 %1").arg(fn.name()));
        }
    };

    // 自底向上解析：`STRLENS(RAND(100))` 里内层 RAND 的返回类型必须先定下来，
    // 外层 STRLENS 才能校验实参类型（否则内层还停留在解析期的内置类型）。
    // walkExpression 是先序（父→子）；把节点收集后逆序应用即得 子→父 的求值顺序。
    // 例：A(B(C,D),E) 先序为 A,B,C,D,E，逆序 E,D,C,B,A —— 任一子节点都在其父之前。
    const auto resolveAst = [&resolveNode](const QSharedPointer<ExpressionNode>& ast) {
        if (!ast) return;
        QList<ExpressionNode*> nodes;
        walkExpression(*ast, [&nodes](ExpressionNode& n) { nodes.append(&n); });
        for (auto it = nodes.crbegin(); it != nodes.crend(); ++it) resolveNode(**it);
    };

    for (auto& ast : m_astCache) resolveAst(ast);
    // 并行装载时 worker 的 AST 不在主缓存里，需按行再走一遍。
    // **按脚本并行**：每个脚本的行走的是自己的 AST，互不相干；resolveNode 只读
    // m_functions / 静态表，写入的是本脚本节点的字段 —— 无共享写。
    QList<ScriptData*> scripts;
    scripts.reserve(m_scripts.size());
    for (auto it = m_scripts.begin(); it != m_scripts.end(); ++it) scripts.append(&it.value());

    const auto processScript = [&resolveAst](ScriptData* sd) {
        for (LogicalLine& line : sd->lines) {
            for (const Operand& op : line.arguments) resolveAst(op.ast);
            for (const auto& e : line.argument.exprs) resolveAst(e);
            for (const Operand& c : line.argument.cases) resolveAst(c.ast);
            resolveAst(line.condition);
        }
    };
    if (canParallelize(scripts.size())) {
        QtConcurrent::blockingMap(scripts, processScript);
    } else {
        for (ScriptData* sd : scripts) processScript(sd);
    }
}

// 收集一个 AST 里的函数调用告警（内置函数参数不符 / 未定义的函数）。
// 同一位置的重复 AST 引用去重；其它行/文件保留各自的诊断。
// 纯函数：只读 AST，结果写入 out —— 可在线程池上按脚本并行调用。
void EraParseTable::collectFunctionWarnings(const QSharedPointer<ExpressionNode>& ast,
                                            const ScriptPosition& position, WarningCollector& out) {
    if (!ast) return;
    walkExpression(*ast, [&out, &position](ExpressionNode& node) {
        // 三元分支类型：对齐 C# OperatorMethodManager.ReduceTernaryTerm —— 只允许
        // (long,long,long) 与 (long,string,string)，其余抛 CodeEE
        //「三項演算子の使用法が不正です」（解析期 CodeEE 由 ParserMediator 收成告警）。
        if (node.kind() == NodeKind::If) {
            const auto& ifn = static_cast<const IfNode&>(node);
            const OperandType ct = ifn.condition() ? ifn.condition()->valueType() : OperandType::Unknown;
            const OperandType tt = ifn.thenExpr() ? ifn.thenExpr()->valueType() : OperandType::Unknown;
            const OperandType et = ifn.elseExpr() ? ifn.elseExpr()->valueType() : OperandType::Unknown;
            const bool condBad = isKnown(ct) && ct != OperandType::Int;
            const bool branchBad = isKnown(tt) && isKnown(et)
                                   && (tt != et || (tt != OperandType::Int && tt != OperandType::Str));
            if (condBad || branchBad) {
                const QString err = QCoreApplication::translate("ParseDiagnostics", 
                    "三項演算子の使用法が不正です"
                    "（条件须为整数；真/假分支须同为整数或同为字符串）");
                const QString key = position.toString() + QStringLiteral("\x1fternary\x1f") + err;
                if (!out.seen.contains(key)) {
                    out.seen.insert(key);
                    out.warnings.append(QStringLiteral("%1 [%2]").arg(err, node.toString()));
                    out.keys.append(key);
                }
            }
        }
        if (node.kind() != NodeKind::Function) return;
        const auto& fn = static_cast<const FunctionNode&>(node);
        const QString& err = fn.arityError();
        if (err.isEmpty()) return;
        const QString key = position.toString() + QLatin1Char('\x1f') + fn.name().toUpper() + QLatin1Char('\x1f') + err;
        if (out.seen.contains(key)) return;
        out.seen.insert(key);
        out.warnings.append(QStringLiteral("%1 [%2]").arg(err, fn.toString()));
        out.keys.append(key);
    });
}

void EraParseTable::validateArguments() {
    // **按脚本并行 + 有序合并**：
    //   · 每个脚本独立产出一份 (warnings, keys) —— 脚本内顺序与串行版逐字一致；
    //   · 合并阶段按 m_scripts 的遍历顺序拼接，并用全局 seen 去重。
    //   QHash 的遍历顺序与 keys() 一致，故并行版的「脚本顺序」= 串行版顺序，
    //   于是告警文本、顺序、去重结果与串行实现**完全相同**（可在同一份数据上
    //   逐字节比对）。eraTW 上这一步 ~2.8–3.6s，是 finalizeParse 的第二热点。
    QList<ScriptData*> scripts;
    scripts.reserve(m_scripts.size());
    for (auto it = m_scripts.begin(); it != m_scripts.end(); ++it) scripts.append(&it.value());

    const auto processScript = [](ScriptData* sd) -> WarningCollector {
        WarningCollector out;
        for (LogicalLine& line : sd->lines) {
            if (line.kind != LineKind::Instruction) continue;
            const qsizetype before = out.warnings.size();
            const QString semanticError = !line.argument.typeOk ? line.argument.typeError : QString();
            ArgumentParser::build(line);   // 幂等：重算 kind/params/exprs + 重新校验
            if (!semanticError.isEmpty()) {
                line.argument.typeOk = false;
                line.argument.typeError = semanticError;
                line.isError = true;
                line.errMes = semanticError;
            }
            if (line.argument.hasError()) {
                // 结构告警：不参与去重（与串行版一致，key 留空）
                out.warnings.append(QStringLiteral("%1 (%2)")
                                        .arg(line.argument.typeError, line.raw.trimmed()));
                out.keys.append(QString());
            }
            const ScriptPosition& pos = line.position;

            // 注：CALL 语句**不做**实参类型校验 —— 对齐 C#（SP_CALL_ArgumentBuilder 不调用
            // ConvertArg，实参按「实际类型」装进 Transporter 后由被调函数自行解释）。
            // 强类型校验只发生在「式中调用」NAME(args) 处（见 resolveFunctionNodes）。

            // 函数调用（内部命令/内置函数）参数校验告警
            collectFunctionWarnings(line.condition, pos, out);
            for (const Operand& op : line.arguments) {
                collectFunctionWarnings(op.ast, pos, out);
            }
            for (const auto& e : line.argument.exprs) {
                collectFunctionWarnings(e, pos, out);
            }
            for (const Operand& c : line.argument.cases) {
                collectFunctionWarnings(c.ast, pos, out);
            }
            for (qsizetype i = before; i < out.warnings.size(); ++i)
                out.positions.append(line.position);
        }
        return out;
    };

    QList<WarningCollector> perScript;
    if (canParallelize(scripts.size())) {
        perScript = QtConcurrent::blockingMapped(scripts, processScript);
    } else {
        perScript.reserve(scripts.size());
        for (ScriptData* sd : scripts) perScript.append(processScript(sd));
    }

    // 有序合并 + 跨脚本去重：只有第一次出现的 key 会被保留，等价于串行版
    // 「脚本顺序 × 脚本内顺序」下的首次出现。
    QSet<QString> seen;
    for (const WarningCollector& c : perScript) {
        for (int i = 0; i < c.warnings.size(); ++i) {
            const QString& key = c.keys.at(i);
            if (!key.isEmpty()) {
                if (seen.contains(key)) continue;
                seen.insert(key);
            }
            const auto& pos = c.positions.at(i);
            m_diagnostics.add(DiagSeverity::Warning, DiagCode::kArgCheck,
                              pos.filename, pos.lineNumber, pos.column, 0,
                              c.warnings.at(i));
        }
    }
}
