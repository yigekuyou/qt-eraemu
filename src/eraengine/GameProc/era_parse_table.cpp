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
#include "ast/system_variables.h"
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
// 绑定/校验在调用方线程（装载收尾）执行，blockingMap 会再切子任务）。
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
    if (!evaluator) return;
    evaluator->setVariableDimProvider([this](const QString& name) {
        const auto* line = lineAt(m_currentScript, m_currentLine);
        const auto* decl = m_variables.find(name, line ? line->ownerFunction : QString());
        return decl ? decl->dimension : 1;
    });
}

// EXISTVAR 的查询：对齐 C# ExistVarMethod ->
// `IdentifierDictionary.GetVariableToken(name, null, allowPrivate=true)`。
// 位掩码与 C# 的 5 个判定一一对应：
//     1 = IsInteger   2 = IsString   4 = IsConst   8 = IsArray2D   16 = IsArray3D
// 查询顺序也与 C# 相同：
//   ① allowPrivate 的**当前函数私有变量/常量**（函数外的同名广域声明优先于它，
//      与 VariableTable::find 的顺序一致：局部 -> 广域）；
//   ② 系统变量（含 fork 专有扩展变量），类型/维数取自 systemVariables.h 的元数据
//      （元数据的 dimension 是**元素维数**：CFLAG = 1、CDFLAG = 2）。
// 名字**带下标**时 C# 查不到（字典键是裸名），这里同样返回 0。
int EraParseTable::variableFlags(const QString& name) const {
    if (name.isEmpty()) return 0;
    // ① 用户变量 / 常量
    if (const VariableDecl* decl = m_variables.find(name, executionScope())) {
        int flags = (decl->type == OperandType::Str) ? 2 : 1;
        if (decl->isConst) flags |= 4;
        // C# 的 IsArray2D/IsArray3D 看的是 VariableCode 的**元素维数**标志：
        // 角色变量并不因此升维（CFLAG = `__ARRAY_1D__ | __CHARACTER_DATA__`，
        // IsArray2D 为 false；CDFLAG 才是 `__ARRAY_2D__`）。声明里的 dimension
        // 正是元素维数（`#DIMS CHARADATA X, 3` -> 1）。
        if (decl->dimension == 2) flags |= 8;
        else if (decl->dimension >= 3) flags |= 16;
        return flags;
    }
    // ② 系统变量（原生 + 扩展）
    const sysvar::SystemVariableMetadata meta =
        sysvar::systemVariableMetadata(name.toStdString());
    if (meta.type == OperandType::Unknown) {
        const auto* ext = sysvar::findExtensionSystemVariable(name.toStdString());
        if (ext == nullptr) return 0;
        return ext->type == OperandType::Str ? 2 : 1;
    }
    int flags = (meta.type == OperandType::Str) ? 2 : 1;
    if (meta.dimension == 2) flags |= 8;
    else if (meta.dimension >= 3) flags |= 16;
    return flags;
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
    return parseScopedExpression(expr, scope, quiet, /*foldConstants=*/true,
                                 /*allowAfterFinalize=*/false);
}

QSharedPointer<ExpressionNode> EraParseTable::runtimeVariableReference(const QString& text) {
    // 见头文件说明：运行期、不折叠常量、静默。
    return parseScopedExpression(text, executionScope(), /*quiet=*/true,
                                 /*foldConstants=*/false, /*allowAfterFinalize=*/true);
}

QSharedPointer<ExpressionNode> EraParseTable::parseScopedExpression(const QString& expr,
                                                                   const QString& scope,
                                                                   bool quiet, bool foldConstants,
                                                                   bool allowAfterFinalize) {
    if (m_finalized && !allowAfterFinalize) return nullptr; // Source compilation is restricted to loading.
    const QString key = expr.trimmed();
    if (key.isEmpty()) {
        return nullptr;
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
    // 装载期间用户函数可能尚未 merge，markLoadComplete 会再统一重绑一次。
    parser.setFunctionTypeProvider([this](const QString& name) -> OperandType {
        // #FUNCTIONS -> Str；#FUNCTION -> Int；无 # 行的 @label 按 Int（宽容语义）。
        // 与内置函数同名的、无 #FUNCTION 的 @label **不覆盖**内置函数（见 userCallReturnType）。
        return userCallReturnType(name);
    });

    // 格式化串：@"..." / \@...#...\@
    parser.setFormProvider([this, scope](const QString& text, bool yenAt) -> QSharedPointer<ExpressionNode> {
        const AstResolver resolve = [this, scope](const QString& e) { return parseScopedExpression(e, scope, false, true, false); };
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
    // 按 (名字, 作用域) 查常数表 —— 常数表本身按「归属作用域」分离（见
    // VariableTable），因此：
    //   * 函数内的私有常量只在本函数内折叠（C#：函数内 `#DIM CONST` 是该函数的
    //     私有常量，UserDefinedVariableData.Private）；
    //   * 本作用域的可写私有变量（`#DIM 色_RED7`）遮蔽别处的同名常量 ——
    //     否则会把这个可写变量的**左值**折成字面量（eraTW 的 CASINO 色_* 26 行
    //     就是这么整行报「赋值左值必须为有效变量项」并失效的）。
    // 运行期变量名解析（GETVAR 族）走 foldConstants=false：C# 的 ReduceExpressionTerm
    // 对常量也返回 VariableTerm，GETVAR 要能**识别**常量（见 runtimeVariableReference）。
    if (foldConstants) {
        const VariableTable* vt = &m_variables;
        parser.setConstantValueProvider([vt, scope](const QString& name) -> QVariant {
            qint64 iv = 0;
            if (vt->constInt(name, iv, scope)) return QVariant::fromValue<qint64>(iv);
            QString sv;
            if (vt->constStr(name, sv, scope)) return QVariant(sv);
            return QVariant();
        });
    }
    QSharedPointer<ExpressionNode> ast = parser.parse(tokens);
    if (ast) {
        // 立即按变量表定型（新解析的 AST 也保持强类型）
        VariableTable::applyTypes(*ast, m_variables, scope);
    }
    PrintTemplateCompiler::prepare(ast);
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
    if (m_state) m_state->setErrorState();
    return false;
}

// ---------------------------------------------------------------------------
// 只读区：装载（AST 由 ErbLoader/AstBuilder 预先构建）
// ---------------------------------------------------------------------------

void EraParseTable::clear() {
    // QUIT 关闭游戏：释放全部解析期数据（AST/标记区/函数表/标签/告警/变量声明）
    m_scripts.clear();
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
    // 每次装载都是独立会话：下一次必须由装载器重新预登记声明，
    // 常量/维数求值器也必须随新的变量表重新装配（clearLoadedGame 已把
    // 求值器上的提供者清空，标志不复位就会带着空提供者进入下一局）。
    m_declarationsPreRegistered = false;
    m_constProvidersReady = false;
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

    // PRINTDATA/STRDATA 段结构属于脚本静态索引，装载时一次构建，运行期只读。
    auto isDataConsumer = [](const QString& name) {
        return name == QLatin1String("STRDATA") || name == QLatin1String("STRDATAL")
               || name == QLatin1String("STRDATAW") || name == QLatin1String("PRINTDATA")
               || name == QLatin1String("PRINTDATAL") || name == QLatin1String("PRINTDATAW")
               || name == QLatin1String("PRINTDATAD") || name == QLatin1String("PRINTDATADL")
               || name == QLatin1String("PRINTDATADW") || name == QLatin1String("PRINTDATAK")
               || name == QLatin1String("PRINTDATAKL") || name == QLatin1String("PRINTDATAKW");
    };
    for (int i = 0; i < data.lines.size(); ++i) {
        LogicalLine& line = data.lines[i];
        // Also bind callers that supply LogicalLine directly rather than AstBuilder.
        line.runnerInstruction = classifyRunnerInstruction(line.functionName);
        if (!isDataConsumer(line.functionName)) continue;
        line.printDataGroups.clear();
        line.printDataEndLine = -1;
        bool inList = false;
        for (int j = i + 1; j < data.lines.size(); ++j) {
            const QString& name = data.lines.at(j).functionName;
            if (name == QLatin1String("ENDDATA")) {
                line.printDataEndLine = j;
                break;
            }
            if (name == QLatin1String("DATALIST")) {
                inList = true;
                line.printDataGroups.append(QList<int>());
            } else if (name == QLatin1String("ENDLIST")) {
                inList = false;
            } else if (name == QLatin1String("DATA") || name == QLatin1String("DATAFORM")) {
                if (inList) {
                    if (line.printDataGroups.isEmpty()) line.printDataGroups.append(QList<int>());
                    line.printDataGroups.last().append(j);
                } else {
                    line.printDataGroups.append(QList<int>{j});
                }
            } else {
                break;
            }
        }
    }

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
                // 已由装载器预登记（先预处理→先登记→再构造）：不再重复登记。
                if (!m_declarationsPreRegistered)
                    parseVariableDeclaration(line, currentFunction, isHeaderFile);
            } else {
                m_diagnostics.add(DiagSeverity::Warning, DiagCode::kSharpLine,
                                  line.position.filename, line.position.lineNumber, line.position.column, 0,
                                  QCoreApplication::translate("ParseDiagnostics", "函数声明之外使用了 # 行 (%1)")
                                      .arg(line.raw.trimmed()));
            }
        } else if (line.kind == LineKind::Instruction && isScopedVariableLine(line.raw)) {
            // VARI / VARS：语句位置的私有变量声明（是否已登记由预扫描决定）。
            applyScopedVariableLine(line, currentFunction);
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
                        if (!ok) {
                            p.defaultStr = defaultValue;
                            if (p.defaultStr.size() >= 2 && p.defaultStr.startsWith(QLatin1Char('"'))
                                && p.defaultStr.endsWith(QLatin1Char('"')))
                                p.defaultStr = p.defaultStr.mid(1, p.defaultStr.size() - 2);
                        }
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

    // 标记区：跳转标记 + 控制转移预绑定
    buildJumpMarkings(scriptName);

    // 绑定/校验统一在 markLoadComplete 里单趟完成（全局定稿 → 重绑 → 诊断）：
    // 跨文件前向引用需要「全部声明就绪」，且避免了每脚本一次全局扫描
    // 在 eraTW（2200+ 脚本）上退化成 O(n²)。

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

QString EraParseTable::executionScope() const {
    // 常数/私有变量的作用域由「正在执行的那一行属于哪个函数」决定。
    // 装载期若尚未开始执行（m_currentScript 为空 / 位置越界）返回空串 ——
    // 此时只有广域常量可见，正是 #DIM 维数之类装载期查询想要的语义。
    const auto* line = lineAt(m_currentScript, m_currentLine);
    return line ? line->ownerFunction : QString();
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

void EraParseTable::beginDeclarationPreRegistration() {
    m_declarationsPreRegistered = true;
}

void EraParseTable::preRegisterDeclaration(const QString& rawLine, const ScriptPosition& position,
                                           const QString& function, bool isHeaderFile) {
    parseVariableDeclarationText(rawLine, position, function, isHeaderFile);
}

void EraParseTable::finishDeclarationPreRegistration() {
    // 声明全部就绪：结算常量求值器 / 维数 / 全局初值 / 用户函数类型。
    resolveGlobalDeclarations();
}

void EraParseTable::parseVariableDeclarationText(const QString& rawLine, const ScriptPosition& position,
                                                const QString& currentFunction, bool isHeaderFile) {
    QString s = rawLine.trimmed();
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
                          position.filename, position.lineNumber, position.column, 0,
                          QCoreApplication::translate("ParseDiagnostics", "#%1 缺少变量名").arg(directive));
        return;
    }

    const int priorDiagnostics = m_diagnostics.size();
    diagnoseIntegerLiterals(rest, position.filename, position.lineNumber,
                            position.column + qMax(0, int(rawLine.indexOf(rest))), m_diagnostics);
    if (m_diagnostics.size() != priorDiagnostics) return; // 错误初值不得登记成 0。
    try {
        const UserDefinedVariableData d =
            UserDefinedVariableData::create(rest, isStr, /*isPrivate=*/true, position);

        // CONST 声明的初值即常数（供 #DIM 维数引用）。
        // **归属作用域**：与 decl.scope 同一判据（GLOBAL 关键字 / 头文件文件级 /
        // 不在任何函数内 = 广域；否则是该函数的**私有常量**）。
        // 私有常量只在声明它的函数内可见（对齐 C#：函数内 #DIM 走
        // LogicalLine.AddPrivateVariable，查询经 GetVariableToken 只查当前函数）。
        const QString constOwner = (isGlobal || d.global || isHeaderFile
                                    || currentFunction.isEmpty())
                                       ? QString() : currentFunction;
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
                    m_variables.setConstStrArray(d.name, d.defaultStr, constOwner);
                } else if (!d.defaultStr.isEmpty()) {
                    m_variables.setConstStr(d.name, d.defaultStr.first(), constOwner);
                }
            } else if (!allLiteral && !valueExprs.isEmpty()) {
                m_variables.setConstExprs(d.name, valueExprs, constOwner);
            } else if (valueExprs.isEmpty()) {
                // `#DIM CONST X, N =` + 紧随其后的 `{ … }` 多行初值块：
                // 本移植尚未支持该写法（常量数组的初值会丢），明确留痕。
                if (d.dimension > 1)
                    qWarning() << "[未完成] #DIM CONST 的多行初值块（{ }）尚未实现:"
                               << d.name << position.toString();
            } else if (!d.defaultInt.isEmpty()) {
                // 常数**数组**（`#DIM CONST NAME, N = v0, v1, …`）也要存下来，
                // 供 `NAME:i` 的下标访问使用；标量常数取第一个值
                m_variables.setConstArray(d.name, d.defaultInt, constOwner);
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
        decl.isSavedata = d.save;
        decl.isGlobal = d.global || isGlobal;
        decl.isGlobalSave = decl.isSavedata && decl.isGlobal;

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
                              position.filename, position.lineNumber, position.column, 0,
                              QCoreApplication::translate("ParseDiagnostics", "全局变量 %1 重复定义").arg(decl.name));
        }
    } catch (const std::exception& e) {
        m_diagnostics.add(DiagSeverity::Error, DiagCode::kDeclError,
                          position.filename, position.lineNumber, position.column, 0,
                          QCoreApplication::translate("ParseDiagnostics", "#%1 声明错误：%2 (%3)")
                              .arg(directive, QString::fromUtf8(e.what()), rest));
    }
}

void EraParseTable::parseVariableDeclaration(const LogicalLine& line, const QString& currentFunction,
                                            bool isHeaderFile) {
    parseVariableDeclarationText(line.raw, line.position, currentFunction, isHeaderFile);
}

// ---------------------------------------------------------------------------
// VARI / VARS（emuera.em 的**作用域变量声明**）--------------------------------
//
//   `VARI NAME`                  → 整数标量私有变量（初值 0）
//   `VARI NAME = <整数表达式>`     → 声明 + 初值
//   `VARI NAME, l1{, l2{, l3}}`  → 1/2/3 维整数数组（**不写初值**）
//   `VARS ...`                   → 同上的字符串版（初值须用双引号包围）
//
// 语义与「函数内就地写一条 #DIM/#DIMS」等价，区别只在：名字/类型写在语句
// 位置、初值在**运行期**写到元素 0（且仅当长度恰为 1）。是 emuera.net 系
// 的私有扩展，受 setting.json 的 UseScopedVariableInstruction 门控
//（默认关闭；FL3.8 的 setting.json 显式开了）。依据见
// test/data/commands/VARI.md / VARS.md（emuera.em 源码逐行推定）。
//
// 本移植此前把 VARI/VARS 登记为**静默跳过桩**：声明被丢掉 → 同函数的
// `TEXT = "…"` 被当成「未声明变量」（全局槽），并且收尾的作用域检查会把
// 每一处使用报成「变量 TEXT 在本函数中未定义」（FL3.8 有 2469 处噪声）。
// 这里补上「登记私有变量」这一步；运行期初值见 ExecutionEngine。
// ---------------------------------------------------------------------------
namespace {
struct ScopedVarSpec {
    bool ok = false;
    bool isStr = false;
    QString name;
    QStringList lengths;   // 维度字面量（空 = 标量 / 一维长度 1）
    bool hasInit = false;
    QString init;          // VARI: 整数表达式原文；VARS: 首尾引号之间的文本
};

// 变量名：与 #DIM 同一判据 —— 不以数字开头、除 '_' 外不含符号（CJK 亦可用）。
bool isVariableName(const QString& name) {
    if (name.isEmpty() || name.at(0).isDigit()) return false;
    for (const QChar c : name) {
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_'))) return false;
    }
    return true;
}

ScopedVarSpec parseScopedVarSpec(const QString& rawLine) {
    ScopedVarSpec spec;
    const QString text = stripDirectiveComment(rawLine).trimmed();
    int i = 0;
    while (i < text.size() && !text.at(i).isSpace()) ++i;
    const QString head = text.left(i).toUpper();
    if (head == QLatin1String("VARI")) spec.isStr = false;
    else if (head == QLatin1String("VARS")) spec.isStr = true;
    else return spec;

    const QString rest = text.mid(i).trimmed();
    if (rest.isEmpty()) return spec;

    // C# 用第一个 '=' 切成 左（名字+长度）/ 右（初值）；引号不参与切分
    //（字符串初值里的 '=' 必然落在最后一个引号之前，故与 C# 同结果）。
    QString left = rest;
    QString right;
    const int eq = rest.indexOf(QLatin1Char('='));
    if (eq >= 0) { left = rest.left(eq); right = rest.mid(eq + 1); }

    const QStringList parts = left.split(QLatin1Char(','));
    if (parts.isEmpty()) return spec;
    const QString name = parts.first().trimmed();
    if (!isVariableName(name)) return spec;
    spec.name = name;

    for (int k = 1; k < parts.size(); ++k) {
        const QString dim = parts.at(k).trimmed();
        bool isNumber = false;
        dim.toLongLong(&isNumber);   // 维度必须是整数字面量（C# int.Parse）
        // 解析失败时 C# 抛未受控的 FormatException（装载中止）。这里不登记
        // 该行（变量按未声明处理）—— 不静默造出一个维度错误的变量。
        if (!isNumber) return ScopedVarSpec();
        spec.lengths.append(dim);
    }

    // 只有「不带长度列表」时才解析初值（C#：leftSplit.Length > 1 走数组分支，
    // 该分支里 `= …` 被忽略）。
    if (parts.size() == 1 && !right.trimmed().isEmpty()) {
        if (spec.isStr) {
            const int a = right.indexOf(QLatin1Char('"'));
            const int b = right.lastIndexOf(QLatin1Char('"'));
            if (a >= 0 && b > a) { spec.init = right.mid(a + 1, b - a - 1); spec.hasInit = true; }
        } else {
            spec.init = right.trimmed();
            spec.hasInit = true;
        }
    }
    spec.ok = true;
    return spec;
}
} // namespace

bool EraParseTable::isScopedVariableLine(const QString& rawLine) {
    return parseScopedVarSpec(rawLine).ok;
}

void EraParseTable::registerScopedVariableLine(const QString& rawLine, const ScriptPosition& position,
                                               const QString& function) {
    const ScopedVarSpec spec = parseScopedVarSpec(rawLine);
    if (!spec.ok) return;
    if (function.isEmpty()) {
        // C# 把 VARI/VARS 绑到「当前函数标签」（ParentLabelLine）；函数外没有
        // 可绑的标签。这里按「不登记」处理并留一条告警，避免造出一个假的广域变量。
        m_diagnostics.add(DiagSeverity::Warning, DiagCode::kSharpLine, position.filename,
                          position.lineNumber, position.column, 0,
                          QCoreApplication::translate("ParseDiagnostics",
                              "%1 只能用在函数内（函数声明之外的声明会被忽略）")
                              .arg(spec.isStr ? QStringLiteral("VARS") : QStringLiteral("VARI")));
        return;
    }
    // 复用 #DIM/#DIMS 的登记路径：类型/维度/作用域判定完全同源。
    QString declText = (spec.isStr ? QStringLiteral("#DIMS ") : QStringLiteral("#DIM ")) + spec.name;
    for (const QString& l : spec.lengths) declText += QStringLiteral(", ") + l;
    parseVariableDeclarationText(declText, position, function, /*isHeaderFile=*/false);
}

void EraParseTable::applyScopedVariableLine(LogicalLine& line, const QString& function) {
    const ScopedVarSpec spec = parseScopedVarSpec(line.raw);
    if (!spec.ok) return;
    if (!m_declarationsPreRegistered)
        registerScopedVariableLine(line.raw, line.position, function);
    if (function.isEmpty()) { line.arguments.clear(); return; }

    // 运行期交接用两个操作数：
    //   [0] 目标变量（AST = VariableNode，供既有赋值路径复用）
    //   [1] 初值（若有；VARS 是字面量字符串 —— 按 C# 不走 FORM 解析）
    // 无初值时只留 [0]，ExecutionEngine 什么都不写（对齐 C# 的「空 else」）。
    Operand target(spec.name);
    target.ast = expressionAstInScope(spec.name, function, /*quiet=*/true);
    line.arguments = { target };
    if (spec.hasInit) {
        Operand init(spec.init);
        if (spec.isStr) {
            init.ast = QSharedPointer<LiteralNode>::create(spec.init);
        } else {
            init.ast = expressionAstInScope(spec.init, function, /*quiet=*/true);
            if (!init.ast) return;   // 初值无法解析：声明仍在，只是这次不写初值
        }
        line.arguments.append(init);
    }
}

void EraParseTable::applyVariableTypes() {
    // 2) 各行实际引用的 AST（并行装载时 worker 的 AST 不在主缓存中）。
    //    **按脚本并行**：函数名全局唯一（一个函数体只在一个脚本里），
    //    VariableTable::applyTypes 取的是 const 变量表（只读），
    //    ArgumentParser::build 只改传入的行 —— 无共享写。
    //    eraTW 实测这一步 ~5s（2.2M 行），是 markLoadComplete 的最大热点。
    //
    //    **就地定型**：不再克隆「定型副本」再换指针。打印模板的分词片段
    //（CompiledPrintTemplatePart::expression / attributes / rawParts）与行实参
    //    指向的是**同一批 AST 节点**，就地改类型即同时生效；因此收尾既不需要
    //    克隆整棵树，也不需要再对模板递归重绑。
    QList<ScriptData*> scripts;
    scripts.reserve(m_scripts.size());
    for (auto it = m_scripts.begin(); it != m_scripts.end(); ++it) {
        scripts.append(&it.value());
    }

    const auto processScript = [this](ScriptData* sd) {
        // 同一节点可能被多行/多片段引用（模板分词、DEFAULT 列表…）：每个作用域
        // 只定型一次（与旧「按 ownerFunction 建缓存」等价）。行按所属函数连续存放，
        // 因此只在作用域切换时查表，避免逐行 toUpper + 哈希。
        QHash<QString, QSet<const ExpressionNode*>> typed;
        QSet<const ExpressionNode*>* seen = nullptr;
        QString scope;
        for (LogicalLine& line : sd->lines) {
            if (!seen || line.ownerFunction != scope) {
                scope = line.ownerFunction;
                seen = &typed[scope.toUpper()];
            }
            const auto bind = [this, seen, &scope](QSharedPointer<ExpressionNode>& ast) {
                if (!ast || seen->contains(ast.data())) return;
                seen->insert(ast.data());
                VariableTable::applyTypes(*ast, m_variables, scope);
                PrintTemplateCompiler::prepare(ast);
            };
            for (auto& op : line.arguments) bind(op.ast);
            bind(line.condition);
            for (auto& ast : line.columnDefaults) bind(ast);
            ArgumentParser::build(line);
            for (const auto& ast : line.assignmentAsts) PrintTemplateCompiler::prepare(ast);
            // line.printTemplate（PRINT/PRINTFORM/PRINTS 的分词）无需单独处理：
            // 其片段表达式就是上面这批已被就地定型的节点。
        }
    };
    if (canParallelize(scripts.size())) {
        QtConcurrent::blockingMap(scripts, processScript);
    } else {
        for (ScriptData* sd : scripts) processScript(sd);
    }
}

// ---------------------------------------------------------------------------
// 收尾步骤 4：静态解析「运行期跳转/调用目标」（对齐 C# ErbLoader.setJumpTo）
//
// C# 在装载第 3 趟 setJumpTo 里把**静态可知**的跳转目标解析一次：解析不到就把
// 该行标成 IsError（运行到就抛）并计入「関数が見つからない警告」。这就是把
// 「跑到那行才炸」的错误提前到装载期的标准做法：只对**常量目标**下结论，
// 目标带格式串（%…% / {…} / [[Rename]]）时视为运行期求值，一律不下结论。
// 本实现与运行期解析**同源**（CALL 走 hasLabel、GOTO 走本函数内 $标签），
// 因此不会出现「装载期说没问题、运行期找不到」的口径差。
//
//   * CALL/CALLFORM/CALLF/CALLFORMF/JUMP/JUMPFORM：常量目标必须存在
//   * GOTO/GOTOFORM：常量 $标签必须在**本函数内**（对齐 GetLabelDollar）
//   * TRY* 系（TRYCALL/TRYJUMP/TRYCCALL/TRYCJUMP/TRYGOTO/TRYCALLEVENT…）：
//     语义就是「找不到就跳过」，不报（对齐 C# func.Function.IsTry()）
//   * CALLEVENT / BEGIN / FUNC：C# 不做目标检查（CALLEVENT 缺目标静默），跳过
void EraParseTable::resolveStaticTargets() {
    // 目标名：常量目标返回标签名（去掉 @/$ 前缀）；运行期求值返回空（不判定）。
    const auto staticTarget = [](const LogicalLine& line) -> QString {
        if (line.arguments.isEmpty()) return {};
        QString name = line.arguments.first().raw.trimmed();
        if (name.isEmpty()) return {};
        if (name.startsWith(QLatin1Char('@')) || name.startsWith(QLatin1Char('$')))
            name.remove(0, 1);
        if (name.contains(QLatin1Char('%')) || name.contains(QLatin1Char('{'))
            || name.contains(QLatin1String("[[")))
            return {};   // FORM 名 / [[Rename]] 占位：运行期才知道
        return name;
    };

    for (auto script = m_scripts.constBegin(); script != m_scripts.constEnd(); ++script) {
        const ScriptData& data = script.value();
        // 本脚本各函数的 $标签集合（大小写折叠：标签比较遵循 ICFunction 语义）。
        QHash<QString, QSet<QString>> dollarLabels;
        for (const LogicalLine& line : data.lines) {
            if (line.kind != LineKind::GotoLabel) continue;
            dollarLabels[line.ownerFunction.toUpper()].insert(line.labelName.toUpper());
        }
        for (const LogicalLine& line : data.lines) {
            bool checkFunction = false;
            bool checkLabel = false;
            switch (line.runnerInstruction) {
            case RunnerInstruction::CALL:
            case RunnerInstruction::CALLFORM:
            case RunnerInstruction::CALLF:
            case RunnerInstruction::CALLFORMF:
            case RunnerInstruction::JUMP:
            case RunnerInstruction::JUMPFORM:
                checkFunction = true;
                break;
            case RunnerInstruction::GOTO:
            case RunnerInstruction::GOTOFORM:
                checkLabel = true;
                break;
            default:
                break;   // TRY* 系（运行期静默跳过）与 CALLEVENT/FUNC 不检查
            }
            if (!checkFunction && !checkLabel) continue;
            const QString target = staticTarget(line);
            if (target.isEmpty()) continue;
            QString message;
            if (checkFunction) {
                if (hasLabel(target)) {
                    // 目标存在 -> 装载期就把实参校验掉（对齐 C# CALL_Instruction.SetJumpTo
                    // 里调用 ConvertArg；C# 只在「常量目标且能找到」时才做这一步）。
                    checkCallStatementArgs(line, target);
                    continue;
                }
                message = QCoreApplication::translate("ParseDiagnostics",
                    "指定的函数名\"@%1\"不存在（运行到该行会失败）").arg(target);
            } else {
                if (dollarLabels.value(line.ownerFunction.toUpper()).contains(target.toUpper()))
                    continue;
                message = QCoreApplication::translate("ParseDiagnostics",
                    "指定的标签名\"$%1\"在当前函数内不存在（运行到该行会失败）").arg(target);
            }
            m_diagnostics.add(DiagSeverity::Error, DiagCode::kLabelNotFound,
                line.position.filename, line.position.lineNumber, line.position.column, 0, message);
        }
    }
}

// ---------------------------------------------------------------------------
// CALL 语句的实参校验（对齐 C# Process.CalledFunction.ConvertArg）
//
// C# 在**装载第 3 趟**（setJumpTo）里对「常量目标且能找到该函数」的 CALL 调一次
// ConvertArg，把实参个数/类型不匹配从「运行到那行才抛 CodeEE」提前到装载期。
// 这里复刻同一组判据（与 checkUserCallArgs 同源，只是实参来自语句的 Operand 表）：
//
//   * 实参个数 > 形参个数            -> 错误（C#「引数の数が…超えています」）
//   * 字符串实参 -> 整型形参         -> 错误（任何配置下都不允许）
//   * 整型实参 -> 字符串形参         -> 允许（CompatiFuncArgAutoConvert；见 checkUserCallArgs）
//   * REF 形参：实参必须是同类型、同维数的非角色数组变量，且**不可省略**
//
// 刻意**不报**的两类（宁缺勿错）：
//   * 实参少于形参：C# 会对 ARG/ARGS/私有变量形参补默认值（parseLabel 的 canDef），
//     实际等价于「永远有默认值」，因此这里不下结论；
//   * 目标带 %…%/{…}/[[…]]（运行期求值）、TRY* 系（语义就是找不到就跳过）。
void EraParseTable::checkCallStatementArgs(const LogicalLine& line, const QString& target) {
    const UserFunctionDecl* decl = userFunction(target.toUpper());
    if (!decl) return;   // 目标存在但没登记成函数（BEGIN/CALLEVENT 之类）：不判定

    // 实参表 = 目标名之后的全部操作数（剔除 "," 独立分隔符，与 ArgumentParser 同口径）
    QList<const Operand*> actuals;
    actuals.reserve(line.arguments.size());
    for (int i = 1; i < line.arguments.size(); ++i) {
        const Operand& o = line.arguments.at(i);
        if (!o.isString && (o.raw == QLatin1String(",") || o.raw == QLatin1String(":"))) continue;
        actuals.append(&o);
    }
    const int want = decl->paramCount();
    if (actuals.size() > want) {
        m_diagnostics.add(DiagSeverity::Error, DiagCode::kArgCheck,
            line.position.filename, line.position.lineNumber, line.position.column, 0,
            QCoreApplication::translate("ParseDiagnostics",
                "函数 @%1 的实参过多（形参 %2 个，实得 %3 个；运行到该行会失败）")
                .arg(decl->originalName.isEmpty() ? decl->name : decl->originalName)
                .arg(want).arg(actuals.size()));
        return;
    }
    for (int i = 0; i < want; ++i) {
        const UserParamDecl& formal = decl->params.at(i);
        const QString shown = decl->originalName.isEmpty() ? decl->name : decl->originalName;
        // 形参多于实参时（i >= actuals.size()）等价于 C# 的 term == null（实参省略）。
        const Operand* actualPtr = (i < actuals.size()) ? actuals.at(i) : nullptr;
        const bool omitted = !actualPtr || actualPtr->omitted;
        const QSharedPointer<ExpressionNode> ast = actualPtr ? actualPtr->ast : QSharedPointer<ExpressionNode>();
        const auto* var = ast ? dynamic_cast<const VariableNode*>(ast.data()) : nullptr;
        if (formal.isReference) {
            // C# ConvertArg：REF 形参省略 = 错误；实参必须是同类型、同维数的非角色数组变量。
            QString err;
            if (omitted) {
                err = QCoreApplication::translate("ParseDiagnostics",
                    "函数 @%1 的第 %2 个参数是引用传递（REF），不能省略（运行到该行会失败）");
            } else if (!var) {
                err = QCoreApplication::translate("ParseDiagnostics",
                    "函数 @%1 的第 %2 个参数是引用传递（REF），实参必须是数组变量（运行到该行会失败）");
            } else if (var->characterData || var->readOnly
                       || (isKnown(formal.type) && isKnown(var->valueType())
                           && var->valueType() != formal.type)) {
                err = QCoreApplication::translate("ParseDiagnostics",
                    "函数 @%1 的第 %2 个 REF 实参需要同类型、同维数的非角色数组变量（运行到该行会失败）");
            }
            if (!err.isEmpty())
                m_diagnostics.add(DiagSeverity::Error, DiagCode::kArgCheck,
                    line.position.filename, line.position.lineNumber, line.position.column, 0,
                    err.arg(shown).arg(i + 1));
            continue;
        }
        if (omitted || !ast) continue;
        if (!formal.typeKnown || !isKnown(formal.type)) continue;
        const OperandType actualType = ast->valueType();
        if (!isKnown(actualType)) continue;
        if (actualType == OperandType::Str && formal.type == OperandType::Int) {
            m_diagnostics.add(DiagSeverity::Error, DiagCode::kArgCheck,
                line.position.filename, line.position.lineNumber, line.position.column, 0,
                QCoreApplication::translate("ParseDiagnostics",
                    "函数 @%1 的第 %2 个实参需要%3，实得字符串（不能从字符串转换为整数；运行到该行会失败）")
                    .arg(shown).arg(i + 1)
                    .arg(QString::fromUtf8(operandTypeName(formal.type))));
        }
    }
}

// ---------------------------------------------------------------------------
// 收尾步骤 5：结构/配对检查（对齐 C# ErbLoader.nestCheck 的诊断部分）
//
// C# 在装载第 2 趟（nestCheck）里用一个 nestStack 走完每个函数体：配对不上的
// 结构、越过块边界的指令、SIF 的次行、RETURNF 的位置/类型都在这里报到装载期。
// 本实现只做**诊断**——配对的跳转标记早已由 buildJumpMarkings 建好（运行期消费），
// 这里按同一判据（同一个 nestStack 语义）把「结构性错误」讲出来。
//
// 刻意不做的：C# 里那些 level 0/1 的提示（「SIF文の次の行が空行…」、
// 「関数が定義されるより前に行があります」）——eraTW 这类真实工程里遍地都是，
// 报出来只会淹没真正的问题。
// ---------------------------------------------------------------------------
namespace {

// END 系列 -> 它要求的配对结构名（C# FunctionIdentifier.getMatchFunction）
QString matchingOpener(const QString& ender) {
    if (ender == QLatin1String("REND")) return QStringLiteral("REPEAT");
    if (ender == QLatin1String("NEXT")) return QStringLiteral("FOR");
    if (ender == QLatin1String("WEND")) return QStringLiteral("WHILE");
    if (ender == QLatin1String("ENDIF")) return QStringLiteral("IF");
    if (ender == QLatin1String("ENDSELECT")) return QStringLiteral("SELECTCASE");
    if (ender == QLatin1String("ENDCATCH")) return QStringLiteral("CATCH");
    if (ender == QLatin1String("ENDLIST")) return QStringLiteral("DATALIST");
    if (ender == QLatin1String("ENDNOSKIP")) return QStringLiteral("NOSKIP");
    if (ender == QLatin1String("ENDFUNC")) return QStringLiteral("TRYCALLLIST");
    return QString();
}

bool isLoopOpener(const QString& name) {
    return name == QLatin1String("REPEAT") || name == QLatin1String("FOR")
        || name == QLatin1String("WHILE") || name == QLatin1String("DO");
}

bool isPrintDataFamily(const QString& name) {
    return name.startsWith(QLatin1String("PRINTDATA")) || name == QLatin1String("STRDATA");
}

bool isTryListOpener(const QString& name) {
    return name == QLatin1String("TRYCALLLIST") || name == QLatin1String("TRYJUMPLIST")
        || name == QLatin1String("TRYGOTOLIST") || name == QLatin1String("GOTOLIST");
}

bool isTryCatchOpener(const QString& name) {
    return name == QLatin1String("TRYCCALL") || name == QLatin1String("TRYCCALLFORM")
        || name == QLatin1String("TRYCJUMP") || name == QLatin1String("TRYCJUMPFORM")
        || name == QLatin1String("TRYCGOTO") || name == QLatin1String("TRYCGOTOFORM");
}

// 这些指令的**第一个操作数**是「目标名」（@函数名 / $标签名），不是表达式：
// C# 走标签解析路径（ReduceLabelName 之类），不经 GetVariableToken，因此不参与
// 「变量作用域」判定。`GOTO SKIP` 里的 SKIP 是 `$SKIP` 标签 —— 若按变量判定，
// eraTW 会把 67 处 `GOTO <标签>` 全报成「变量未定义」（SKIP/END/FINISH/… 恰好
// 也是别处的私有变量名）。
bool firstArgumentIsTargetName(RunnerInstruction instruction) {
    switch (instruction) {
    case RunnerInstruction::GOTO:
    case RunnerInstruction::GOTOFORM:
    case RunnerInstruction::CALLEVENT:
    case RunnerInstruction::TRYCALLEVENT:
    case RunnerInstruction::CALL:
    case RunnerInstruction::TRYCALL:
    case RunnerInstruction::CALLFORM:
    case RunnerInstruction::TRYCALLFORM:
    case RunnerInstruction::TRYCCALL:
    case RunnerInstruction::TRYCCALLFORM:
    case RunnerInstruction::CALLF:
    case RunnerInstruction::TRYCALLF:
    case RunnerInstruction::CALLFORMF:
    case RunnerInstruction::TRYCALLFORMF:
    case RunnerInstruction::JUMP:
    case RunnerInstruction::TRYJUMP:
    case RunnerInstruction::JUMPFORM:
    case RunnerInstruction::TRYJUMPFORM:
    case RunnerInstruction::TRYGOTO:
    case RunnerInstruction::TRYGOTOFORM:
    case RunnerInstruction::TRYCJUMP:
    case RunnerInstruction::TRYCJUMPFORM:
    case RunnerInstruction::TRYCGOTO:
    case RunnerInstruction::TRYCGOTOFORM:
        return true;
    default:
        return false;
    }
}

// 开块指令 -> 它需要的收尾指令名（用于「Xに対応するYが見つかりません」）
QString matchingCloser(const QString& opener) {
    if (opener == QLatin1String("IF")) return QStringLiteral("ENDIF");
    if (opener == QLatin1String("REPEAT")) return QStringLiteral("REND");
    if (opener == QLatin1String("FOR")) return QStringLiteral("NEXT");
    if (opener == QLatin1String("WHILE")) return QStringLiteral("WEND");
    if (opener == QLatin1String("DO")) return QStringLiteral("LOOP");
    if (opener == QLatin1String("SELECTCASE")) return QStringLiteral("ENDSELECT");
    if (opener == QLatin1String("CATCH")) return QStringLiteral("ENDCATCH");
    if (opener == QLatin1String("DATALIST")) return QStringLiteral("ENDLIST");
    if (opener == QLatin1String("NOSKIP")) return QStringLiteral("ENDNOSKIP");
    if (isTryCatchOpener(opener)) return QStringLiteral("CATCH");
    if (isPrintDataFamily(opener)) return QStringLiteral("ENDDATA");
    if (isTryListOpener(opener)) return QStringLiteral("ENDFUNC");
    return QString();
}


}  // namespace

void EraParseTable::checkStructures() {
    // nestStack 的表项 = (结构名, 行, 该结构是否已出现第一个分支)。
    struct Nest { QString name; const LogicalLine* line; bool branchSeen = false; };
    const auto add = [this](DiagSeverity sev, const LogicalLine& line, const QString& text) {
        m_diagnostics.add(sev, DiagCode::kStructure,
            line.position.filename, line.position.lineNumber, line.position.column, 0, text);
    };

    for (auto script = m_scripts.constBegin(); script != m_scripts.constEnd(); ++script) {
        const ScriptData& data = script.value();
        QList<Nest> stack;
        const auto closeBody = [&]() {
            // 函数末尾仍有未闭合的结构：逐条报「Xに対応するYが見つかりません」。
            while (!stack.isEmpty()) {
                const Nest n = stack.takeLast();
                add(DiagSeverity::Error, *n.line,
                    QCoreApplication::translate("ParseDiagnostics",
                        "%1 に対応する %2 が見つかりません").arg(n.name, matchingCloser(n.name)));
            }
        };
        for (const LogicalLine& line : data.lines) {
            if (line.kind == LineKind::FunctionLabel) {
                closeBody();
                continue;
            }
            if (line.kind == LineKind::GotoLabel) {
                // C#：PRINTDATA 系 / STRDATA / DATALIST / TRY*LIST 体内不允许 $标签
                if (!stack.isEmpty()
                    && (isPrintDataFamily(stack.last().name)
                        || stack.last().name == QLatin1String("DATALIST")
                        || isTryListOpener(stack.last().name))) {
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "%1 構文中に $ラベルを定義することはできません").arg(stack.last().name));
                }
                continue;
            }
            if (line.kind != LineKind::Instruction) continue;   // 空行/注释行/# 行不参与配对
            const QString& name = line.functionName;

            // ---- 块内允许的指令（对齐 C# nestCheck 的 baseFunc 检查）----
            if (!stack.isEmpty()) {
                const QString& base = stack.last().name;
                if (isPrintDataFamily(base)) {
                    const bool allowed = name == QLatin1String("DATA")
                        || name == QLatin1String("DATAFORM")
                        || name == QLatin1String("DATALIST")
                        || name == QLatin1String("ENDLIST")
                        || name == QLatin1String("ENDDATA");
                    if (!allowed) {
                        add(DiagSeverity::Error, line,
                            QCoreApplication::translate("ParseDiagnostics",
                                "%1 構文に使用できない命令'%2'が含まれています").arg(base, name));
                        continue;
                    }
                } else if (base == QLatin1String("DATALIST")) {
                    const bool allowed = name == QLatin1String("DATA")
                        || name == QLatin1String("DATAFORM") || name == QLatin1String("ENDLIST");
                    if (!allowed) {
                        add(DiagSeverity::Error, line,
                            QCoreApplication::translate("ParseDiagnostics",
                                "DATALIST 構文に使用できない命令'%1'が含まれています").arg(name));
                        continue;
                    }
                } else if (isTryListOpener(base)) {
                    if (name != QLatin1String("FUNC") && name != QLatin1String("ENDFUNC")) {
                        add(DiagSeverity::Error, line,
                            QCoreApplication::translate("ParseDiagnostics",
                                "%1 構文に使用できない命令'%2'が含まれています").arg(base, name));
                        continue;
                    }
                } else if (base == QLatin1String("SELECTCASE")) {
                    // SELECTCASE 直下、最初の CASE より前は命令を置けない
                    const bool allowed = name == QLatin1String("CASE")
                        || name == QLatin1String("CASEELSE") || name == QLatin1String("ENDSELECT");
                    if (!stack.last().branchSeen && !allowed) {
                        add(DiagSeverity::Error, line,
                            QCoreApplication::translate("ParseDiagnostics",
                                "SELECTCASE 構文の分岐の外に命令'%1'が含まれています").arg(name));
                        continue;
                    }
                }
            }

            // ---- 入栈 ----
            if (name == QLatin1String("IF") || name == QLatin1String("SELECTCASE")
                || name == QLatin1String("REPEAT") || name == QLatin1String("FOR")
                || name == QLatin1String("WHILE") || name == QLatin1String("DO")
                || isTryCatchOpener(name) || isPrintDataFamily(name)
                || isTryListOpener(name) || name == QLatin1String("NOSKIP")) {
                stack.append({name, &line});
                continue;
            }

            // ---- 分支指令 ----
            if (name == QLatin1String("ELSEIF") || name == QLatin1String("ELSE")) {
                if (stack.isEmpty() || stack.last().name != QLatin1String("IF")) {
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "IF～ENDIFの外で%1文が使われました").arg(name));
                } else {
                    stack.last().branchSeen = true;
                }
                continue;
            }
            if (name == QLatin1String("CASE") || name == QLatin1String("CASEELSE")) {
                if (stack.isEmpty() || stack.last().name != QLatin1String("SELECTCASE"))
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "SELECTCASE～ENDSELECTの外で%1文が使われました").arg(name));
                else
                    stack.last().branchSeen = true;
                continue;
            }
            if (name == QLatin1String("BREAK") || name == QLatin1String("CONTINUE")) {
                bool inLoop = false;
                for (const Nest& n : stack)
                    if (isLoopOpener(n.name)) { inLoop = true; break; }
                if (!inLoop)
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "REPEAT, FOR, WHILE, DOの中以外で%1文が使われました").arg(name));
                continue;
            }
            if (name == QLatin1String("FUNC")) {
                if (stack.isEmpty() || !isTryListOpener(stack.last().name))
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "対応するTRYCALLLIST系命令のない%1です").arg(name));
                continue;
            }
            if (name == QLatin1String("DATA") || name == QLatin1String("DATAFORM")) {
                if (stack.isEmpty() || !(isPrintDataFamily(stack.last().name)
                                         || stack.last().name == QLatin1String("DATALIST")))
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "対応するPRINTDATA系命令のない%1です").arg(name));
                continue;
            }
            if (name == QLatin1String("DATALIST")) {
                // DATALIST 是 PRINTDATA 系 / STRDATA 体内的**可重复**子块：
                // 一个 PRINTDATA 里可以连续放多个 DATALIST…ENDLIST（eraTW 的
                // CASINO.ERB 就是这种写法），所以这里必须入栈，否则随后的 ENDLIST
                // 找不到 DATALIST 而误报。
                if (stack.isEmpty() || !isPrintDataFamily(stack.last().name)) {
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "対応するPRINTDATA系命令のない%1です").arg(name));
                } else {
                    stack.append({name, &line, false});
                }
                continue;
            }
            if (name == QLatin1String("LOOP")) {
                // LOOP 对应 REPEAT 或 DO（按栈顶判定）
                const bool ok = !stack.isEmpty()
                    && (stack.last().name == QLatin1String("DO") || stack.last().name == QLatin1String("REPEAT"));
                if (!ok) {
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "対応するREPEATの無い%1文です").arg(name));
                } else {
                    stack.removeLast();
                }
                continue;
            }
            if (name == QLatin1String("REND") || name == QLatin1String("NEXT")
                || name == QLatin1String("WEND") || name == QLatin1String("ENDIF")
                || name == QLatin1String("ENDSELECT") || name == QLatin1String("ENDCATCH")
                || name == QLatin1String("ENDLIST") || name == QLatin1String("ENDDATA")
                || name == QLatin1String("ENDNOSKIP") || name == QLatin1String("ENDFUNC")) {
                const QString want = matchingOpener(name);
                if (name == QLatin1String("ENDDATA")) {
                    if (stack.isEmpty() || !(isPrintDataFamily(stack.last().name))) {
                        add(DiagSeverity::Error, line,
                            QCoreApplication::translate("ParseDiagnostics",
                                "対応するPRINTDATA系命令もしくはSTRDATAのない%1です").arg(name));
                    } else {
                        stack.removeLast();
                    }
                    continue;
                }
                bool matched = false;
                if (name == QLatin1String("ENDFUNC")) {
                    matched = !stack.isEmpty() && isTryListOpener(stack.last().name);
                } else {
                    matched = !stack.isEmpty() && stack.last().name == want;
                }
                if (!matched) {
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "対応する%1の無い%2文です").arg(want, name));
                } else {
                    stack.removeLast();
                }
                continue;
            }
            if (name == QLatin1String("CATCH")) {
                if (stack.isEmpty() || !isTryCatchOpener(stack.last().name))
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "対応するTRYC系命令がありません"));
                else
                    stack.last().name = QStringLiteral("CATCH");
                continue;
            }
            // ---- SIF：次行必须是可跳过的有效指令 ----
            if (name == QLatin1String("SIF")) {
                const int i = line.lineIndex;
                int next = i + 1;
                const int size = int(data.lines.size());
                while (next < size && data.lines.at(next).kind == LineKind::Null) ++next;
                if (next >= size || data.lines.at(next).kind == LineKind::FunctionLabel
                    || data.lines.at(next).kind == LineKind::GotoLabel) {
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "SIF文の次の行がありません"));
                    continue;
                }
                const LogicalLine& nextLine = data.lines.at(next);
                if (nextLine.kind == LineKind::Instruction) {
                    // SIF/TRYC 系也吃下一行（C# PARTIAL）：再套一个就会错位
                    const QString& nf = nextLine.functionName;
                    if (nf == QLatin1String("SIF") || isTryCatchOpener(nf)
                        || nf == QLatin1String("TRYCALLLIST") || nf == QLatin1String("TRYJUMPLIST")
                        || nf == QLatin1String("TRYGOTOLIST"))
                        add(DiagSeverity::Error, line,
                            QCoreApplication::translate("ParseDiagnostics",
                                "SIF文の次の行を%1文にすることはできません").arg(nf));
                }
                continue;
            }
            // ---- RETURNF：必须在 #FUNCTION(S) 内，且返回类型要一致 ----
            if (name == QLatin1String("RETURNF")) {
                const UserFunctionDecl* decl = userFunction(line.ownerFunction.toUpper());
                if (!decl || !decl->isMethod) {
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "RETURNFは#FUNCTION以外では使用できません"));
                    continue;
                }
                if (line.arguments.isEmpty()) continue;
                const auto& operand = line.arguments.first();
                if (!operand.ast) continue;
                const OperandType actual = operand.ast->valueType();
                if (!isKnown(actual) || !isKnown(decl->returnType)) continue;
                if (actual != decl->returnType) {
                    add(DiagSeverity::Error, line,
                        decl->returnType == OperandType::Int
                            ? QCoreApplication::translate("ParseDiagnostics",
                                  "#FUNCTIONで始まる関数の戻り値に文字列型が指定されました")
                            : QCoreApplication::translate("ParseDiagnostics",
                                  "#FUNCTIONSで始まる関数の戻り値に数値型が指定されました"));
                }
                continue;
            }
        }
        closeBody();
    }
}

// ---------------------------------------------------------------------------
// 收尾步骤 6：可静态判定的运行期错误（字面量除零）
//
// `X / 0`、`X % 0` 在 C# 里是运行期必抛的 CodeEE「0による除算が行なわれました」；
// 除数为**字面量 0** 时「运行必炸」是确定事件，因此提前到装载期报出来。
// 除数不是字面量（`X / Y`）时不判定 —— 那要运行期才知道。
//
// 数组越界**不在这里**：validateExpression 已有更精确的「变量 X 常量下标越界」
//（它按维度/角色变量的下标语义算 dim，见 expression_ast.cpp），重复报只会变噪声。
// ---------------------------------------------------------------------------
void EraParseTable::checkStaticValues() {
    for (auto script = m_scripts.constBegin(); script != m_scripts.constEnd(); ++script) {
        const ScriptData& data = script.value();
        for (const LogicalLine& line : data.lines) {
            const auto visit = [&](ExpressionNode& node) {
                if (node.kind() != NodeKind::BinaryOp) return;
                auto& op = static_cast<BinaryOpNode&>(node);
                const TokenType t = op.op().type();
                if (t != TokenType::DIVIDE && t != TokenType::MODULO) return;
                const auto& right = op.right();
                if (!right || right->kind() != NodeKind::Literal) return;
                const auto& lit = static_cast<const LiteralNode&>(*right);
                if (lit.valueType() != OperandType::Int || lit.intValue() != 0) return;
                m_diagnostics.add(DiagSeverity::Error, DiagCode::kStaticValue,
                    line.position.filename, line.position.lineNumber,
                    line.position.column, 0,
                    QCoreApplication::translate("ParseDiagnostics",
                        "0 による除算が行われました（运行到该行会失败）"));
            };
            for (const Operand& op : line.arguments) {
                if (op.ast) walkExpression(*op.ast, visit);
            }
            if (line.condition) walkExpression(*line.condition, visit);
            for (const auto& ast : line.assignmentAsts) {
                if (ast) walkExpression(*ast, visit);
            }
            for (const auto& ast : line.columnDefaults) {
                if (ast) walkExpression(*ast, visit);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 收尾步骤 7（可选）：定义了但从未被调用的函数（对齐 C# 的 FunctionNotCalledWarning）
//
// C# 的做法（ErbLoader.checkScript 后半段）：
//   * 「根」= 事件/系统标签（@EVENT…/@SYSTEM_…，Depth=0）+ 带 #FUNCTION(S) 的标签；
//   * setJumpTo 每解析出一个 CALL/JUMP 的**静态**目标，就把该标签的 Depth 记为
//     根深度 +1，于是从根出发做广度遍历就能得到「可达 = 被调用」的集合；
//   * 只要出现过 CALLFORM 系（名字运行期求值）或 TRYCALLLIST/TRYJUMPLIST，
//     就**整体放弃**这个判定（useCallForm）——名字在运行期才知道，静态没有依据；
//   * 剩下 Depth 仍为 -1 的标签就是「定义了但没被调用」。
//
// 本实现同判据（含 useCallForm 的整体放弃）。默认**关闭**：C#
// `関数が呼び出されなかった警告の扱い` 的默认值就是 IGNORE，eraTW 也显式写了 IGNORE，
// 而且它用了大量 CALLFORM —— 两条都命中，开着也不会有什么输出。
// ---------------------------------------------------------------------------
void EraParseTable::checkNotCalledFunctions() {
    if (!m_functionNotCalledWarning) return;

    // 1) 收集全部函数标签（含同名重复声明）与「是否是根」
    struct LabelInfo { QList<const LogicalLine*> declLines; bool root = false; bool called = false; };
    QHash<QString, LabelInfo> labels;
    QList<QString> order;
    for (auto script = m_scripts.constBegin(); script != m_scripts.constEnd(); ++script) {
        for (const LogicalLine& line : script.value().lines) {
            if (line.kind != LineKind::FunctionLabel) continue;
            const QString key = line.labelName.toUpper();
            auto it = labels.find(key);
            if (it == labels.end()) {
                it = labels.insert(key, LabelInfo{});
                order.append(key);
            }
            it->declLines.append(&line);
            if (const UserFunctionDecl* decl = userFunction(key)) {
                if (decl->isEvent || decl->isSystem || decl->isMethod) it->root = true;
            }
        }
    }

    // 2) useCallForm：CALLFORM 系 / TRYCALLLIST 系一旦出现，名字就是运行期求值的，
    //    静态无从判定「没被调用」—— 整体放弃（与 C# 一致）。
    for (auto script = m_scripts.constBegin(); script != m_scripts.constEnd(); ++script) {
        for (const LogicalLine& line : script.value().lines) {
            switch (line.runnerInstruction) {
            case RunnerInstruction::CALLFORM:
            case RunnerInstruction::CALLFORMF:
            case RunnerInstruction::JUMPFORM:
            case RunnerInstruction::GOTOFORM:
                return;
            default:
                break;
            }
            const QString& n = line.functionName;
            if (n == QLatin1String("TRYCALLLIST") || n == QLatin1String("TRYJUMPLIST")
                || n == QLatin1String("GOTOLIST"))
                return;
        }
    }

    // 3) 调用图：函数体 -> 静态目标（statement 的 CALL/JUMP 与表达式里的用户函数）
    QHash<QString, QSet<QString>> edges;
    QList<QString> pending;
    for (auto script = m_scripts.constBegin(); script != m_scripts.constEnd(); ++script) {
        QString current;
        for (const LogicalLine& line : script.value().lines) {
            if (line.kind == LineKind::FunctionLabel) {
                current = line.labelName.toUpper();
                continue;
            }
            if (current.isEmpty()) continue;
            const auto mark = [&](const QString& target) {
                if (target.isEmpty()) return;
                edges[current].insert(target.toUpper());
            };
            switch (line.runnerInstruction) {
            case RunnerInstruction::CALL:
            case RunnerInstruction::CALLF:
            case RunnerInstruction::JUMP:
                if (!line.arguments.isEmpty()) {
                    QString name = line.arguments.first().raw.trimmed();
                    if (name.startsWith(QLatin1Char('@'))) name.remove(0, 1);
                    if (!name.contains(QLatin1Char('%')) && !name.contains(QLatin1Char('{'))
                        && !name.contains(QLatin1String("[[")))
                        mark(name);
                }
                break;
            default:
                break;
            }
            // 表达式里的用户函数调用（`X = USERFUNC(…)`）同样算「被调用」
            const auto scanAst = [&](const QSharedPointer<ExpressionNode>& ast) {
                if (!ast) return;
                walkExpression(*ast, [&](ExpressionNode& node) {
                    if (node.kind() != NodeKind::Function) return;
                    const auto& fn = static_cast<const FunctionNode&>(node);
                    if (fn.isUserFunction()) mark(fn.name());
                });
            };
            for (const Operand& op : line.arguments) scanAst(op.ast);
            scanAst(line.condition);
            for (const auto& ast : line.assignmentAsts) scanAst(ast);
        }
    }

    // 4) 从根出发做广度遍历
    QList<QString> queue;
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        if (it->root) queue.append(it.key());
    }
    for (int i = 0; i < queue.size(); ++i) {
        const QString key = queue.at(i);
        auto lit = labels.find(key);
        if (lit != labels.end()) {
            if (lit->called) continue;
            lit->called = true;
        }
        for (const QString& next : edges.value(key)) {
            const auto nit = labels.constFind(next);
            if (nit == labels.constEnd() || nit->called) continue;
            queue.append(next);
        }
    }

    // 5) 报「定义了但没被调用」
    for (const QString& key : order) {
        const LabelInfo& info = labels.value(key);
        if (info.called || info.root) continue;
        for (const LogicalLine* line : info.declLines) {
            m_diagnostics.add(DiagSeverity::Warning, DiagCode::kNotCalled,
                line->position.filename, line->position.lineNumber, line->position.column, 0,
                QCoreApplication::translate("ParseDiagnostics",
                    "関数@%1は定義されていますが一度も呼び出されません").arg(line->labelName));
        }
    }
}

void EraParseTable::markLoadComplete() {
    // 装载收尾（唯一入口）：全局声明定稿 → 全体脚本就地定型/重绑 → 统一出诊断。
    // 必须在**全部**声明就绪后进行：跨文件前向引用（先装载的文件调用后装载文件的
    // 函数）此刻才可解析；诊断随后统一产生，不重复。
    QElapsedTimer completeTimer;
    completeTimer.start();
    // 收尾各步在 GUI 线程上串行执行（eraTW 桌面实测合计 13s+，Android 上更久），
    // 期间心跳定时器也发不出来。逐步留痕：真机上「卡在收尾的哪一步」是能看出来的。
    qint64 stepMs = completeTimer.elapsed();
    qInfo().noquote() << "[parse] 收尾步骤 1/6 全局声明定稿(resolveGlobalDeclarations) 开始:"
                      << m_scripts.size() << "脚本";
    resolveGlobalDeclarations();   // 声明预登记后 #FUNCTION 表才随 loadScript 补齐，此处再结算一次
    qInfo().noquote() << "[parse] 收尾步骤 1/6 完成:" << (completeTimer.elapsed() - stepMs) << "ms";
    stepMs = completeTimer.elapsed();
    qInfo().noquote() << "[parse] 收尾步骤 2/6 就地定型(finalizeBindings) 开始";
    finalizeBindings();
    qInfo().noquote() << "[parse] 收尾步骤 2/6 完成:" << (completeTimer.elapsed() - stepMs) << "ms";
    stepMs = completeTimer.elapsed();
    qInfo().noquote() << "[parse] 收尾步骤 3/6 参数校验(finalizeValidation) 开始";
    finalizeValidation();
    qInfo().noquote() << "[parse] 收尾步骤 3/6 完成:" << (completeTimer.elapsed() - stepMs) << "ms";
    stepMs = completeTimer.elapsed();
    qInfo().noquote() << "[parse] 收尾步骤 4/6 跳转目标解析(resolveStaticTargets) 开始";
    resolveStaticTargets();
    qInfo().noquote() << "[parse] 收尾步骤 4/6 完成:" << (completeTimer.elapsed() - stepMs) << "ms";
    stepMs = completeTimer.elapsed();
    qInfo().noquote() << "[parse] 收尾步骤 5/6 结构配对检查(checkStructures) 开始";
    checkStructures();
    qInfo().noquote() << "[parse] 收尾步骤 5/6 完成:" << (completeTimer.elapsed() - stepMs) << "ms";
    stepMs = completeTimer.elapsed();
    qInfo().noquote() << "[parse] 收尾步骤 6/6 静态值检查(checkStaticValues) 开始";
    checkStaticValues();
    qInfo().noquote() << "[parse] 收尾步骤 6/6 完成:" << (completeTimer.elapsed() - stepMs) << "ms";
    // 可选步骤：未被调用的函数告警（默认关闭，见 setFunctionNotCalledWarning）
    if (m_functionNotCalledWarning) {
        stepMs = completeTimer.elapsed();
        qInfo().noquote() << "[parse] 收尾步骤 7(可选) 未被调用函数检查(checkNotCalledFunctions) 开始";
        checkNotCalledFunctions();
        qInfo().noquote() << "[parse] 收尾步骤 7(可选) 完成:" << (completeTimer.elapsed() - stepMs) << "ms";
    }
    m_finalized = true;   // 只读区定型：装载完成后不再编译新源码
    qInfo().noquote() << "[parse] markLoadComplete：" << m_scripts.size() << "脚本"
                      << completeTimer.elapsed() << "ms";
}

void EraParseTable::resolveGlobalDeclarations() {
    // ---- 常量/维数求值器（幂等，首次调用时设置） ----
    if (m_evaluator && !m_constProvidersReady) {
        m_constProvidersReady = true;
        const VariableTable* vt = &m_variables;
        // 表达式型常量的键 = 「归属作用域 ✕ 名字」：广域常量（owner 空）与同名的
        // 私有常量必须分开存放，否则一个函数里的 `#DIM CONST X = 1` 会盖住另一个
        // 函数里的 `#DIM CONST X = 2`。
        const auto constKey = [](const QString& owner, const QString& name) {
            return owner.toUpper() + QChar(0x1f) + name.toUpper();
        };
        const auto constOwnerOf = [vt](const VariableDecl& d) -> QString {
            return (d.scope == VarScope::Global) ? QString() : d.function;
        };
        QHash<QString, QList<QSharedPointer<ExpressionNode>>> constantNodes;
        for (const auto& declaration : vt->declarations()) {
            if (!declaration.isConst) continue;
            const QString owner = constOwnerOf(declaration);
            const QString key = constKey(owner, declaration.name);
            if (constantNodes.contains(key)) continue;
            // 表达式按**声明它的函数**作用域解析（私有常量只在该函数内可见）
            for (const auto& source : vt->constExprs(declaration.name, owner))
                constantNodes[key].append(expressionAstInScope(source, owner));
        }
        // 惰性常量表达式缓存（`#DIM CONST OBJ_ID_LAST = 人物数量上限`）
        //   LazyConst = 逐元素的求值结果（标量常量就是单元素）
        using LazyConst = QList<QVariant>;
        auto lazyConst = QSharedPointer<QHash<QString, LazyConst>>::create();
        auto lazyDepth = QSharedPointer<int>::create(0);
        // 求值并缓存某个常量声明的全部元素；返回 nullptr 表示「不是表达式型常量」。
        // 定位顺序与 VariableTable 的常数查询一致：先 scope 归属，再广域。
        const auto lazyConstValues = [this, constantNodes, constKey, lazyConst, lazyDepth](
                                         const QString& name, const QString& scope)
            -> QSharedPointer<LazyConst> {
            QString key = constKey(scope, name);
            if (!constantNodes.contains(key)) key = constKey(QString(), name);
            if (!constantNodes.contains(key)) return QSharedPointer<LazyConst>();
            const auto cached = lazyConst->constFind(key);
            if (cached != lazyConst->constEnd())
                return QSharedPointer<LazyConst>::create(cached.value());
            const auto exprs = constantNodes.value(key);
            if (exprs.isEmpty() || !m_evaluator) return QSharedPointer<LazyConst>();
            if (*lazyDepth >= 16) return QSharedPointer<LazyConst>();   // 循环定义保护
            ++*lazyDepth;
            LazyConst values;
            values.reserve(exprs.size());
            for (const auto& ast : exprs) {
                values.append(ast ? m_evaluator->evaluate(*ast, m_variableStorage, m_gameBaseData) : QVariant());
            }
            --*lazyDepth;
            lazyConst->insert(key, values);
            return QSharedPointer<LazyConst>::create(values);
        };
        // 运行期常数求值的作用域 = **当前执行行所属的函数**。
        // C# 在 GetVariableToken 里用 line.ParentLabelLine 找私有变量，与此等价：
        // 执行到哪个函数的行，就只看得见那个函数的私有常量。
        const auto evalLazyConst = [lazyConstValues](const QString& name, const QString& scope,
                                                     int index, QVariant& out) -> bool {
            const QSharedPointer<LazyConst> values = lazyConstValues(name, scope);
            if (!values || values->isEmpty()) return false;
            const int i = (index < 0) ? 0 : index;
            out = values->value(i < values->size() ? i : 0);
            return true;
        };
        m_evaluator->setConstProvider([this, vt, evalLazyConst](const QString& name, QVariant& out) -> bool {
            const QString scope = executionScope();
            qint64 iv = 0;
            if (vt->constInt(name, iv, scope)) { out = QVariant::fromValue<qint64>(iv); return true; }
            QString sv;
            if (vt->constStr(name, sv, scope)) { out = QVariant(sv); return true; }
            return evalLazyConst(name, scope, 0, out);
        });
        // 常数数组（`#DIM CONST X, N = …`）：先判名（避免下标副作用被求两遍）
        m_evaluator->setConstArrayChecker([this, vt](const QString& name) -> bool {
            const QString scope = executionScope();
            return vt->constArraySize(name, scope) > 0 || vt->constStrArraySize(name, scope) > 0;
        });
        m_evaluator->setConstArraySizeProvider([this, vt](const QString& name) -> int {
            const QString scope = executionScope();
            const int n = vt->constArraySize(name, scope);
            return n > 0 ? n : vt->constStrArraySize(name, scope);
        });
        m_evaluator->setConstArrayProvider([this, vt, evalLazyConst](const QString& name, int index,
                                                                     QVariant& out) -> bool {
            const QString scope = executionScope();
            QString sv;
            if (vt->constStrArrayAt(name, index, sv, scope)) { out = QVariant(sv); return true; }
            qint64 iv = 0;
            if (vt->constArrayAt(name, index, iv, scope)) { out = QVariant::fromValue<qint64>(iv); return true; }
            return evalLazyConst(name, scope, index, out);
        });
        m_variables.setDimEvaluator([this](const QString& expr, const QString& scope) -> qint64 {
            if (!m_evaluator) return 0;
            const auto ast = expressionAstInScope(expr, scope);
            const QVariant v = ast ? m_evaluator->evaluate(*ast, m_variableStorage, m_gameBaseData) : QVariant();
            return v.isValid() ? v.toLongLong() : 0;
        });
    }

    // ---- 维数/默认值/函数类型（全局：需要全部声明/常量就绪） ----
    m_variables.resolveDimensions();
    applyGlobalVariableDefaults();
    resolveUserFunctionTypes();
}

// 全体脚本绑定（一趟）：字符串赋值/FUNC → 变量类型就地定型 → 赋值 AST → 函数调用解析。
// 要求 resolveGlobalDeclarations() 已就绪（变量维数与函数类型已定）。
void EraParseTable::finalizeBindings() {
    // 每步内部按脚本并行（见各方法）；不再逐脚本调用（那样有 2200+ 次调用开销，
    // eraTW 上从 ~10s 退化到 ~33s）。
    applyStringAssignments();


    // FUNC 重编译（仅构建期未定型的行）
    for (auto& script : m_scripts) {
    for (auto& line : script.lines) {
        if (line.functionName != QLatin1String("FUNC")) continue;
        const AstResolver resolve = [this, scope = line.ownerFunction](const QString& text) {
            return expressionAstInScope(text, scope);
        };
        for (int i = 0; i < line.arguments.size(); ++i) {
            auto& operand = line.arguments[i];
            if (i == 0)
                operand.ast = StrFormParser::parse(operand.raw, resolve,
                    m_evaluator && m_evaluator->ignoreTripleSymbols());
            else if (operand.isString)
                operand.ast = QSharedPointer<LiteralNode>::create(operand.raw);
            else
                operand.ast = resolve(operand.raw);
        }
    }
    }

    // 变量类型回填（全体脚本）
    applyVariableTypes();

    // 赋值 AST 重建（仅构建期未定型的行）
    for (auto& script : m_scripts) {
    for (auto& line : script.lines) {
        if (line.assignOperator.isEmpty() || line.arguments.size() != 2) continue;
        const QString name = line.arguments[0].raw.section(':', 0, 0).trimmed();
        if (line.assignOperator == "=" && m_variables.typeOf(name, line.ownerFunction) == OperandType::Str) continue;
        line.assignmentAsts.clear();
        for (const auto& text : AstBuilder::assignmentValues(line.arguments[1].raw)) {
            // expressionAstInScope 已按 ownerFunction 定型并 prepare —— 不再克隆/重定型。
            line.assignmentAsts.append(expressionAstInScope(text, line.ownerFunction, true));
        }
    }

    }

    // 函数调用解析（全体脚本）
    resolveFunctionNodes();
}

// 全体脚本校验：产出诊断（错误 + 告警）。必须在全部脚本**绑定**完成后统一执行，
// 否则跨文件前向引用会被误报「未定义的函数」。只在 markLoadComplete 调用一次。
// 参数/类型校验必须在「变量类型已就地定型」之后：否则 LFONTS 之类用户 #DIMS 变量
// 在解析期还是默认的 Int，会误报「需要字符串表达式，实得 Int」。
void EraParseTable::finalizeValidation() {
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
            // 指令的「变量槽」实参允许整数组出现（`VARSET 集合`、`CALL F(集合)`）：
            // 这些位置**不该**要求下标，否则 2 维以上数组会被误判「缺少参数或下标维数错误」
            // （eraTW 里 CALL 的 REF 实参 / VARSET 的整数组实参全是这种写法）。
            const ArgKind argKind = line.argument.kind;
            const bool varSlotArgs = argKind == ArgKind::Call || argKind == ArgKind::CallF
                || argKind == ArgKind::Var || argKind == ArgKind::VarSet
                || argKind == ArgKind::Swap || argKind == ArgKind::Bit
                || argKind == ArgKind::Power || argKind == ArgKind::GetInt
                || argKind == ArgKind::VarStr || argKind == ArgKind::ArrayControl
                || argKind == ArgKind::SortChara || argKind == ArgKind::ForNext;
            // 本行已经报过「跨函数引用私有变量」的名字（同一行会对共享 AST 走两遍）
            QSet<QString> reportedScopeNames;
            // 原始文本实参的语句（`ArgKind::Raw`：ALIGNMENT / SETCOLORBYNAME /
            // PRINTL / GOTO 标签 / NEXT …）在 C# 走 `STR_ArgumentBuilder`：
            // 实参整体是**字面字符串**（`st.Substring()` → `SingleTerm(rowStr)`），
            // 裸标识符不会被解析成变量 —— `ALIGNMENT LEFT` 的 LEFT 是关键字，
            // `SETCOLORBYNAME Red` 的 Red 是颜色名。构建期出于「未知裸标识符 =
            // 字符串」的宽容语义仍会把它折成 VariableNode，但那是**装饰**：
            // 运行期读的是 `operand.raw`（见 ExecutionEngine 的 ALIGNMENT 分支）。
            // 因此这类语句不能作为「跨函数引用私有变量」的判据 ——
            // 否则 eraMegaten 会多出 323 条假阳性（LEFT/RIGHT/Red/Blue 恰好在
            // PICTURE.ERB 里各有一条私有的 #DIMS 同名声明）。
            // 赋值语句（`=` 族）也登记成 Raw，但左右值是真表达式：那两条路径
            // 单独调用 validate（左值 / assignmentAsts），不受此开关影响。
            const bool rawTextArgs = argKind == ArgKind::Raw && line.assignOperator.isEmpty();
            const auto validate = [&](const QSharedPointer<ExpressionNode>& ast,
                                      bool requireIndices = true) {
                if (!ast) return;
                QString error = validateExpression(*ast, requireIndices);
                if (error.isEmpty() && m_evaluator && m_variableStorage) {
                    walkExpression(*ast, [&](ExpressionNode& n) {
                        if (error.isEmpty() && n.kind() != NodeKind::Literal && constantTree(n)
                            && !m_evaluator->evaluate(n, m_variableStorage, m_gameBaseData).isValid())
                            error = QCoreApplication::translate("ParseDiagnostics", "装载期常量表达式求值失败");
                    });
                }
                // 作用域：名字只是**别的函数的私有变量/常量**时，在本函数里解析不到。
                // 对齐 C# IdentifierDictionary.ThrowException（privateDimList ->
                // IdentifierNotFoundCodeEE「変数"X"はこの関数中では定義されていません」）。
                // 此前这种引用会被「扁平常量表/私有变量泄漏」悄悄取值 —— eraTW 的
                // `@スロット配列_一覧`（CASINO_SLOT_HELP.ERB:732）读别的函数的
                // `#DIM CONST RD7` 就是这一类。
                bool scopeError = false;
                if (error.isEmpty() && !rawTextArgs) {
                    walkExpression(*ast, [&](ExpressionNode& n) {
                        if (!error.isEmpty() || scopeError || n.kind() != NodeKind::Variable) return;
                        const auto& var = static_cast<const VariableNode&>(n);
                        if (isKnown(m_variables.typeOf(var.name(), line.ownerFunction))) return;
                        if (!m_variables.isPrivateOnlyName(var.name())) return;
                        // 形参名（`@ASSERT_EQ(GOT, WANT, SNAME)` 的 GOT/WANT 是 ARG
                        // 的**别名**，不在变量表里）在本函数内是合法的。
                        if (const UserFunctionDecl* decl = userFunction(line.ownerFunction.toUpper())) {
                            for (const UserParamDecl& p : decl->params) {
                                if (p.name.compare(var.name(), Qt::CaseInsensitive) == 0
                                    || (!p.varName.isEmpty()
                                        && p.varName.compare(var.name(), Qt::CaseInsensitive) == 0))
                                    return;
                            }
                        }
                        // 一行可能对同一棵 AST 走两遍（行实参 + 赋值右值 assignmentAsts
                        // 共享节点）—— 同一个名字只报一次，避免 LSP 出现重复标记。
                        const QString scopeKey = var.name().toUpper();
                        if (reportedScopeNames.contains(scopeKey)) return;
                        reportedScopeNames.insert(scopeKey);
                        error = QCoreApplication::translate("ParseDiagnostics",
                            "变量 %1 在本函数中未定义（它只是别的函数的私有变量；运行到该行会失败）")
                            .arg(var.name());
                        scopeError = true;
                    });
                }
                if (!error.isEmpty()) {
                    line.argument.typeOk = false;
                    line.argument.typeError = error;
                    line.isError = true;
                    line.errMes = error;
                    m_diagnostics.add(DiagSeverity::Error,
                        scopeError ? DiagCode::kIdentifierScope : DiagCode::kArgCheck,
                        line.position.filename, line.position.lineNumber, line.position.column, 0, error);
                }
            };
            // Increment/decrement statements have one LHS, unlike assignments.
            // Rebind it after declarations are known, in the owning function scope.
            if ((line.functionName == "++" || line.functionName == "--")
                && line.arguments.size() == 1) {
                auto& operand = line.arguments[0];
                // expressionAstInScope 已按 ownerFunction 定型 —— 不再克隆/重定型。
                operand.ast = expressionAstInScope(operand.raw, line.ownerFunction, true);
                const bool ok = operand.ast && operand.ast->kind() == NodeKind::Variable
                    && !static_cast<const VariableNode&>(*operand.ast).readOnly
                    && operand.ast->valueType() != OperandType::Str;
                // 这一步是 `X ++` 的**权威**判定：构建期的临时 AST 在「无作用域」
                // 上下文里解析，可能把「与别处同名常量重名的函数私有变量」折成字面量
                // （eraTW 的 `削除行数 ++`：CASINO_SLOT 里的 `#DIM CONST 削除行数`
                // 会让 COMF625 的同名私有变量在构建期变成 `51`），于是收尾第 3 步
                // 的 ArgumentParser 会拿那个陈旧的字面量报「第一个参数需要变量节点」。
                // 因此这里通过时必须**清掉**那次残留的错误状态（typeOk/errMes），
                // 否则语义已经判定正确的行仍会带着旧错误进入 validateArguments。
                line.argument.typeOk = ok;
                line.argument.typeError.clear();
                line.isError = !ok;
                line.errMes.clear();
                if (!ok) {
                    const QString error = QCoreApplication::translate("ParseDiagnostics",
                        "%1 的变量参数无法装载：%2 (%3)")
                        .arg(line.functionName, operand.raw, line.position.toString());
                    line.argument.typeError = error;
                    line.errMes = error;
                    m_diagnostics.add(DiagSeverity::Error, DiagCode::kArgCheck,
                        line.position.filename, line.position.lineNumber, line.position.column, 0, error);
                }
            }
            // ArgumentParser treats a FUNC name as a raw CALL identity; list
            // execution requires an AST for it just as for every actual argument.
            if (line.functionName == QLatin1String("FUNC")) {
                for (const auto& operand : line.arguments) {
                    if (operand.ast) continue;
                    const QString error = QCoreApplication::translate("ParseDiagnostics",
                        "%1 的参数缺少装载期 AST：%2").arg(line.functionName, operand.raw);
                    line.argument.typeOk = false;
                    line.argument.typeError = error;
                    line.isError = true;
                    line.errMes = error;
                    m_diagnostics.add(DiagSeverity::Error, DiagCode::kArgCheck,
                        line.position.filename, line.position.lineNumber, line.position.column, 0, error);
                }
            }
            validate(line.condition);
            const bool skipTargetOperand = firstArgumentIsTargetName(line.runnerInstruction);
            for (int operandIndex = 0; operandIndex < line.arguments.size(); ++operandIndex) {
                const auto& operand = line.arguments.at(operandIndex);
                // 第一个操作数是跳转/调用目标名（函数名 / $标签）：不是表达式
                if (skipTargetOperand && operandIndex == 0) continue;
                // VARSIZE consumes only array identity; its indices (including
                // out-of-range constants) are ignored by the language command.
                if (line.functionName == QLatin1String("VARSIZE") && operand.ast
                    && operand.ast->kind() == NodeKind::Variable) continue;
                validate(operand.ast, !varSlotArgs);
            }
            if (!line.assignOperator.isEmpty() && line.arguments.size() == 2) {
                const auto& dest = line.arguments[0].raw;
                const QString name = dest.section(':', 0, 0).trimmed();
                const auto* decl = m_variables.find(name, line.ownerFunction);
                // expressionAstInScope 已按 ownerFunction 定型 —— 不再克隆/重定型。
                const auto target = expressionAstInScope(dest, line.ownerFunction, true);
                line.arguments[0].ast = target;
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
                    for (int valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
                        const auto& text = values[valueIndex];
                        diagnoseIntegerLiterals(text, line.position.filename, line.position.lineNumber,
                            line.position.column + qMax(0, int(line.raw.indexOf(text))), m_diagnostics);
                        const auto& ast = line.assignmentAsts[valueIndex];
                        if (ast) validate(ast);
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
    // 参数校验（全体脚本）
    validateArguments();
}

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
            // 只处理构建期未定型的行：构建期已按（预扫描）类型表定型的
            // 字符串/FORM/CASE 不再重解析（收尾只扫描未定型项）。
            if (!line.strAssignPending && !line.needsCompletion) continue;
            const QString owner = line.ownerFunction;
            const AstResolver resolve = [this, owner, &line](const QString& e) {
                diagnoseIntegerLiterals(e, line.position.filename, line.position.lineNumber,
                    line.position.column + qMax(0, int(line.raw.indexOf(e))), m_diagnostics);
                return expressionAstInScope(e, owner);
            };
            if (line.kind != LineKind::Instruction) continue;
            // FORM construction may need private string types unavailable in a worker.
            // Recompile the source in its owner scope while compilation is permitted.
            if (line.assignOperator.isEmpty() && (line.raw.contains("@\"")
                || AstBuilder::printInfo(line.functionName).mode == AstBuilder::PrintArgMode::FormStr
                || line.argument.kind == ArgKind::FormStr)) {
                const auto compiled = AstBuilder::build(line.raw, line.position, resolve, resolve);
                line.arguments = compiled.arguments;
                line.condition = compiled.condition;
                line.printTemplate = compiled.printTemplate;
                ArgumentParser::build(line);
            }
            if (line.argument.kind == ArgKind::Case) {
                line.caseCache.clear();
                AstBuilder::buildCaseClauses(line, resolve);
            }
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
            PrintTemplateCompiler::prepare(value.ast);
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
// HTML 字符串的**反向追踪**（装载器在装载期调用一次）
//
// 消费者驱动：从 `HTML_PRINT X` 出发向前回溯 X 的写链 ——
//
//     X = f1                 ← 基（最近的整赋值）
//     X += f2                ← 追加片段
//     X += f3
//     HTML_PRINT X           ← 消费者（本行）
//
// 把整条链在装载期拼成一棵 **PLUS 树**装回该行的实参。`PrintTemplateCompiler::
// compile` 会把 PLUS 链与 FORM 一起压平成**一整条 markup**（见 print_template.cpp
// 的 append：「FORM 可以补全加法两侧开始的标签」），于是跨片段的标签在装载期即闭合、
// `prepare()` 把分词结果附着到节点上，运行期只按既有路径求值 —— 不做任何标签解析。
// eraTW 的 INFO_CLOCK_IMAGE 正是把 `<img src='…' height='…'` 与 `ypos='…'>` 分两次
// `+=`：单个片段里标签不闭合，此前只能原样打成文本。
//
// 可靠性优先（追踪不到**完整**的链就放弃，绝不半拼）：
//   · 只在同一函数作用域内回溯（函数体外不会执行）；
//   · 只穿过「空行/注释」与「对别的变量的赋值」；遇到控制流/标签/函数调用等无法
//     判定的行即放弃 —— 否则可能漏掉条件分支里的写，拼出的值与运行期不符；
//   · 必须回溯到一个整赋值作为基；基的右值是**简单字符串变量**时再递归一层
//     （`save = markup` 这种中转）；
//   · 片段数 < 2 不重建（单片段构建期已经编译好）。
// ---------------------------------------------------------------------------
void EraParseTable::traceHtmlStringArguments() {
    // 简单变量名（不带下标/属性/函数调用）：追踪只认这种形态
    const auto simpleName = [](const QString& raw) -> QString {
        const QString t = raw.trimmed();
        if (t.isEmpty()) return {};
        for (const QChar c : t) {
            if (!(c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$'))) return {};
        }
        return t;
    };
    // 一条「片段写」：`VAR = f` / `VAR += f`（左值简单名 + 右值是字符串 AST）
    const auto fragmentWrite = [this, &simpleName](const LogicalLine& line, QString& name,
                                                   bool& append,
                                                   QSharedPointer<ExpressionNode>& value) -> bool {
        append = false;
        if (line.kind != LineKind::Instruction) return false;
        if (line.arguments.size() != 2) return false;
        const QString op = line.assignOperator;
        if (op != QLatin1String("=") && op != QLatin1String("+=")) return false;
        const QString lhs = simpleName(line.arguments[0].raw);
        if (lhs.isEmpty()) return false;
        if (!line.arguments[1].ast || !line.arguments[1].ast->isString()) return false;
        const OperandType nodeType = line.arguments[0].ast ? line.arguments[0].ast->valueType()
                                                           : OperandType::Unknown;
        if (nodeType != OperandType::Str
            && m_variables.typeOf(lhs, line.ownerFunction) != OperandType::Str) return false;
        name = lhs;
        append = op == QLatin1String("+=");
        value = line.arguments[1].ast;
        return true;
    };
    // 消费者：`HTML_PRINT <简单字符串变量>`
    const auto htmlPrintTarget = [&](const LogicalLine& line, QString& var) -> bool {
        if (line.kind != LineKind::Instruction) return false;
        if (line.functionName != QLatin1String("HTML_PRINT")) return false;
        if (line.arguments.size() != 1) return false;
        var = simpleName(line.arguments[0].raw);
        return !var.isEmpty();
    };

    // 反向回溯 var 在 scope 内、第 before 行之前的写链；成功时给出**程序顺序**的片段。
    // depth：中转变量（`X = Y`）再追一层的余量。
    std::function<bool(const QList<LogicalLine>&, const QString&, const QString&, int, int,
                       QList<QSharedPointer<ExpressionNode>>&)> traceChain;
    traceChain = [&](const QList<LogicalLine>& lines, const QString& scope, const QString& var,
                     int before, int depth,
                     QList<QSharedPointer<ExpressionNode>>& out) -> bool {
        QList<QSharedPointer<ExpressionNode>> reversed;      // 逆序收集
        for (int j = before - 1; j >= 0; --j) {
            const LogicalLine& line = lines.at(j);
            if (line.kind == LineKind::Null || line.kind == LineKind::Preprocessor) continue;
            if (line.ownerFunction != scope) return false;    // 跨函数：链断，放弃
            QString name; bool append = false; QSharedPointer<ExpressionNode> value;
            if (!fragmentWrite(line, name, append, value)) return false;   // 控制流/调用/其它
            if (name.compare(var, Qt::CaseInsensitive) != 0) continue;     // 别的变量：透过
            if (append) { reversed.prepend(value); continue; }             // 追加片段
            // 整赋值 = 链的基（`X = Y` 的中转可再追一层）
            QList<QSharedPointer<ExpressionNode>> head;
            const QString via = (depth == 0 && value->kind() == NodeKind::Variable)
                ? simpleName(static_cast<const VariableNode&>(*value).name()) : QString();
            if (!via.isEmpty()) {
                if (!traceChain(lines, scope, via, j, depth + 1, head)) return false;
            } else {
                head.append(value);
            }
            head += reversed;
            out = head;
            return true;
        }
        return false;                                        // 找不到基：放弃
    };

    int traced = 0;
    QStringList tracedAt;                    // 留痕（便于核对到底重建了哪些打印点）
    for (auto it = m_scripts.begin(); it != m_scripts.end(); ++it) {
        const QString& scriptName = it.key();
        QList<LogicalLine>& lines = it.value().lines;
        for (int i = 0; i < lines.size(); ++i) {
            QString var;
            if (!htmlPrintTarget(lines.at(i), var)) continue;
            QList<QSharedPointer<ExpressionNode>> fragments;
            if (!traceChain(lines, lines.at(i).ownerFunction, var, i, 0, fragments)) continue;
            if (fragments.size() < 2) continue;              // 单片段：构建期已编译
            QSharedPointer<ExpressionNode> joined = fragments.first();
            for (int k = 1; k < fragments.size(); ++k)
                joined = QSharedPointer<BinaryOpNode>::create(
                    joined, ExpressionToken(TokenType::PLUS, QStringLiteral("+")), fragments.at(k));
            lines[i].arguments[0].ast = joined;              // 实参换成装载期拼好的节点
            ++traced;
            if (tracedAt.size() < 8)
                tracedAt.append(QStringLiteral("%1:%2 %3(%4段)")
                                    .arg(scriptName).arg(lines.at(i).position.lineNumber)
                                    .arg(var).arg(fragments.size()));
        }
    }
    if (traced > 0)
        qInfo().noquote() << "[parse] HTML_PRINT 字符串反向追踪：" << traced
                          << "条实参在装载期重建 →" << tracedAt.join(QStringLiteral(", "));
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

// ---------------------------------------------------------------------------
// 函数调用重绑 + 校验（对齐 C# IdentifierDictionary.GetFunctionMethod）
//
// 把每个 FunctionNode 重新解析为 内置函数 / 用户自定义函数 / 未定义函数，
// 并用最终（就地定型后）的实参 AST 复刻 C# FunctionMethod.CheckArgumentType()。
// 必须在**全部**脚本的变量类型定稿之后进行（跨文件前向引用此刻才可解析）。
// ---------------------------------------------------------------------------
void EraParseTable::resolveFunctionNodes() {
    // 与解析期同一判据（era_parse_table.cpp:userCallReturnType）：
    // 用户函数优先，但无 #FUNCTION 时不覆盖同名内置函数。
    const auto userType = [this](const QString& name) -> OperandType {
        return userCallReturnType(name);
    };

    const auto resolveNode = [this, &userType](ExpressionNode& node) {
        if (node.kind() != NodeKind::Function) return;
        auto& fn = static_cast<FunctionNode&>(node);
        fn.setReferenceArguments({});
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
        if (fn.isUserFunction()) {
            const auto* decl = userFunction(fn.name().toUpper());
            if (!decl) return;
            QList<bool> refs;
            for (int i = 0; i < decl->params.size(); ++i) {
                const auto& param = decl->params[i];
                refs.append(param.isReference);
                if (!param.isReference || !fn.arityError().isEmpty()) continue;
                const auto* formal = m_variables.find(param.varName, decl->name);
                const auto* actual = i < fn.arguments().size() && fn.arguments()[i]
                    ? dynamic_cast<const VariableNode*>(fn.arguments()[i].data()) : nullptr;
                // C# CalledFunction.ConvertArg / ReferenceToken.MatchType：REF 必须
                // 接收同类型、同维数的非角色数组，不能使用标量自动转换。
                if (!actual || fn.isArgOmitted(i) || !formal || actual->characterData || actual->readOnly
                    || actual->valueType() != formal->type
                    || actual->dimension != formal->dimension || actual->dimension <= 0) {
                    fn.setArityError(QCoreApplication::translate("ParseDiagnostics",
                        "函数 %1 第 %2 个 REF 实参需要同类型、同维数的非角色数组变量")
                        .arg(decl->name).arg(i + 1));
                }
            }
            fn.setReferenceArguments(refs);
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

    // 并行装载时 worker 的 AST 不在主缓存里，需按行再走一遍。
    // **按脚本并行**：每个脚本的行走的是自己的 AST，互不相干；resolveNode 只读
    // m_functions / 静态表，写入的是本脚本节点的字段 —— 无共享写。
    QList<ScriptData*> scripts;
    scripts.reserve(m_scripts.size());
    for (auto it = m_scripts.begin(); it != m_scripts.end(); ++it) {
        scripts.append(&it.value());
    }

    const auto processScript = [&resolveAst](ScriptData* sd) {
        for (LogicalLine& line : sd->lines) {
            for (const Operand& op : line.arguments) resolveAst(op.ast);
            for (const auto& ast : line.assignmentAsts) resolveAst(ast);
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
    //   逐字节比对）。eraTW 上这一步 ~2.8–3.6s，是 markLoadComplete 的第二热点。
    QList<ScriptData*> scripts;
    scripts.reserve(m_scripts.size());
    for (auto it = m_scripts.begin(); it != m_scripts.end(); ++it) {
        scripts.append(&it.value());
    }

    const auto processScript = [](ScriptData* sd) -> WarningCollector {
        WarningCollector out;
        for (LogicalLine& line : sd->lines) {
            if (line.kind != LineKind::Instruction) continue;
            const qsizetype before = out.warnings.size();
            const QString semanticError = !line.argument.typeOk ? line.argument.typeError : QString();
            // ArgumentParser::build 重算 kind/params/exprs（按最终变量类型）。赋值右值
            // 的 AST 无需再 prepare —— applyVariableTypes（收尾第 3 步）已对全体
            // assignmentAsts 调过一次，finalizeBindings 重建的那些也由
            // expressionAstInScope 在解析时 prepare。
            ArgumentParser::build(line);
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
