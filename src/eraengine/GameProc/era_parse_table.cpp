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
#include "parse_checks.h"   // 收尾步骤 4/5/6 的自由函数实现（只读 IR 视图）
#include "load_passes.h"    // 收尾步骤 1/2/3 的自由函数实现（可写 IR 视图 = LoadContext）
#include "jump_markings.h"  // 标记区：跳转表的构建（输入只有 ScriptData）
#include "position_state.h" // 位置区：执行位置 + 调用栈的状态转移（自由函数）
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
#include <limits>
#include <optional>
#include "ast/name_interner.h"
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
        const auto* line = lineAt(m_position.script, m_position.line);
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
    if (isFinalized()) {
        if (const auto* line = lineAt(m_position.script, m_position.line)) scope = line->ownerFunction;
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
    // 阶段机留在 EraParseTable（阶段是它的状态）；解析本身是 load_passes.cpp 的
    // 自由函数，数据由一次性的 LoadContext 视图给出。
    if (isFinalized() && !allowAfterFinalize) return nullptr; // Source compilation is restricted to loading.
    return loadpass::parseScopedExpression(makeLoadContext(), expr, scope, quiet, foldConstants);
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
    // @ 与 $ 通吃 = 「索引里有没有这个键」。唯一索引建成后不再需要
    // m_hasLabelCache（旧实现是 for(m_scripts) 全量扫描 + 逐名缓存）。
    return m_labelIndex.contains(label.toUpper());
}

// 「可作为 CALL/JUMP 目标」的存在性判定：与 resolveCallableLabel 完全同源
//（当前脚本必须是 @函数标签，否则查全局 @标签索引）——O(1)。
//
// 为什么要它：TRY 系（TRYCALL/TRYCCALLFORM/TRYJUMPLIST…）的探针用动态
// FORM 名逐个试，eraTW 的 @SETLIST_EXIST 对每个对象类逐 ID 探测
// `EXIST_%CLASS%{O_ID}`（实测一轮 2435 次探针、830ms —— 每次都是
// hasLabel 缓存 miss 后的 for(m_scripts) 全量扫描）。标签表装载后不变，
// 存在性本来就是静态的 —— 直接查全局索引。
//
// 与旧 hasLabel（@/$ 通吃 + 缓存）的行为差异：名字只命中 $GOTO 标签时
// 旧路径是「通过探针 -> enterCall -> callLabel 失败 -> TRY 静默跳过」，
// 现在直接静默跳过 —— 结果相同，还省掉了 enterCall 副作用（setPrivateScope
// 会为每个失败名字在 m_privateScopeCache 里留一个空作用域条目）。
bool EraParseTable::hasCallableLabel(const QString& label) const {
    QString targetScript;
    int targetLine = -1;
    return resolveCallableLabel(label, targetScript, targetLine);
}

// 每函数的形参名集合（大写）。按需构建并缓存；失效点 = clear()/loadScript
//（新脚本可能带来新的 @label 形参）。「变量分词」的装载期解析与收尾定型共用。
// 收尾定型（applyVariableTypes）的并行趟开始前会先建全 —— 定型期间只读不插。
const QSet<QString>* EraParseTable::paramNamesOf(const QString& upperScope) const
{
    return loadpass::paramNamesOf(m_paramNamesByFunction, upperScope);
}

const LogicalLine* EraParseTable::lineAt(const QString& scriptName, int line) const {
    const ScriptData* data = script(scriptName);
    if (!data || line < 0 || line >= data->lines.size()) {
        return nullptr;
    }
    return &data->lines.at(line);
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
    return loadpass::userCallReturnType(m_functions, name);
}

int EraParseTable::functionExistsKind(const QString& name, bool caseInsensitive) const {
    Q_UNUSED(caseInsensitive);
    const auto kindOf = [](const UserFunctionInfo& fn) -> int {
        if (!fn.isMethod) return 1;                       // 通常関数
        return fn.returnType == OperandType::Str ? 3 : 2; // #FUNCTIONS / #FUNCTION
    };
    // C# ExistFunctionMethod（Creator.Method.cs:7216）两条路：
    //   第 2 引数省略或 = 0 -> GlobalStatic.LabelDictionary.GetNonEventLabel()，
    //                         比较方式取 Config.StringComparison（默认 OrdinalIgnoreCase）
    //   第 2 引数 != 0     -> 遍历 NoneventKeys，两边 ToUpper() 后比较
    // 两条路的结果集合**相同**（都是大小写无关 + 只认非事件函数），C# 里第二条只是 O(n)。
    //
    // 本移植此前把「第 2 引数不写」当成大小写敏感，退化成逐个比对 originalName 的线性
    // 扫描 —— FL3.8 的 @FL_INIT_LOADER 用 `FOR LOCAL, 0, 6000` 调 EXISTFUNCTION 扫
    // 6000 次 × 11000 个函数 ≈ 6600 万次字符串比较，选「重新開始」后要几十秒才走到下一步，
    // 看上去就是「状态机直接死」。函数表以大写为键（ICVariable 语义），直接散列查。
    const auto it = m_functions.constFind(name.toUpper());
    if (it == m_functions.constEnd()) return 0;
    if (it.value().isEvent) return 0;   // GetNonEventLabel：事件函数不算（C# 同）
    return kindOf(it.value());
}

QList<LabelRef> EraParseTable::labels(const QString& name) const {
    // 事件导航（callFunction / CALLEVENT）要的是**某个名字的全部 @声明**
    //（#SINGLE/#PRI/#LATER/#ONLY 分组）—— 同一张索引的一个投影：滤掉 $ 标签。
    // 顺序在 finalizeLabelIndex 固化；装载期（LSP 增删脚本）尚未定型，
    // 这时按同一个全序（labelRefLess）惰性排一次。
    const auto it = m_labelIndex.constFind(name.toUpper());
    if (it == m_labelIndex.constEnd()) return {};
    QList<LabelRef> out;
    out.reserve(it.value().size());
    for (const LabelRef& ref : it.value()) {
        if (ref.isFunction) out.append(ref);
    }
    if (!isFinalized() && out.size() > 1) {
        std::sort(out.begin(), out.end(), labelRefLess);
    }
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
    return callLabelAt(scriptName, line, m_position.script, returnLine);
}

bool EraParseTable::callLabelAt(const QString& scriptName, int line,
                                const QString& returnScript, int returnLine) {
    // 压帧（callLabel/entryLine 取自目标行）+ 跨脚本切空间 + 强制定位，
    // 三处直接写状态全部在 position::callLabelAt 里（越界 -> false 且不动状态）。
    PositionEffects fx;
    const bool ok = position::callLabelAt(m_position, makeScriptView(), scriptName, line,
                                          returnScript, returnLine, fx);
    emitEffects(fx);
    return ok;
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
    const QVariant v = ev->evaluate(*ast, m_variableStorage, m_gameBaseData, executionScopeId());
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
    m_functions.clear();
    m_labelIndex.clear();
    m_privateDefaultPlans.clear();   // 私有初值调用计划随函数表一起作废
    m_paramNamesByFunction.clear();  // 形参名集合随函数表一起作废
    m_diagnostics.clear();
    m_variables.clear();
    m_entryPoint.clear();
    // 执行位置一并复位：脚本已不存在，任何残留帧/位置都悬空。
    // 转移本身交给 position::reset（清栈 + PC=0）；它**不清 script** ——
    // 「装到空表」这个语义只属于装载期，所以单独把 script 清掉。
    (void)position::reset(m_position);
    m_position.script.clear();
    m_phase = LoadPhase::Empty;
    // 每次装载都是独立会话：下一次必须由装载器重新预登记声明，
    // 常量/维数求值器也必须随新的变量表重新装配（clearLoadedGame 已把
    // 求值器上的提供者清空，标志不复位就会带着空提供者进入下一局）。
    m_declarationsPreRegistered = false;
    m_constProvidersReady = false;
    m_declarationsResolved = false;
    // 信号位置与旧实现相同（在全部字段复位之后）：装载期 clear 不按 Effects
    // 发信号 —— 旧顺序是 positionChanged 先、callStackChanged 后（与
    // resetPosition 相反）。这一处逐字保留，不许改成规范顺序。
    emit positionChanged(QString(), 0);
    emit callStackChanged(0);
}

bool EraParseTable::loadScript(const QString& scriptName, const QList<LogicalLine>& lines,
                              bool isHeaderFile, const QString& path) {
    // 增量装载（测试/LSP 的 append 路径）：收尾之后再加脚本 = 重新打开装载
    // 阶段，调用方必须再调一次 markLoadComplete 让新脚本被定型/重绑。
    // 旧实现把 m_finalized 留在 true（新脚本不会被重绑，只是“碰巧”在测试里
    // 因为 AST 已在装载前构建而能跑）—— 现在阶段本身回答“还需不需要收尾”。
    if (scriptName.isEmpty() || lines.isEmpty()) {
        return false;
    }
    m_phase = LoadPhase::Loading;

    // 重复装载同名脚本 = 用新 AST **替换** m_scripts 里的旧条目。索引是「名字 ->
    // 行号」的唯一来源，必须同步替换：否则旧版本的声明会留在全序里，firstLabel
    // 可能答出一个在当前 ScriptData 里根本不存在的行。初次装载时 m_scripts
    // 不含该名字，这一趟不发生（不引入每脚本一次的全索引扫描）。
    if (m_scripts.contains(scriptName)) {
        for (auto it = m_labelIndex.begin(); it != m_labelIndex.end();) {
            QList<LabelRef>& list = it.value();
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [&scriptName](const LabelRef& ref) {
                                          return ref.script == scriptName;
                                      }),
                       list.end());
            if (list.isEmpty()) it = m_labelIndex.erase(it);
            else ++it;
        }
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
        // 先收集到**局部**容器，最后只对真正有段的行写进 LineExtras：
        // 直接 `line.mutablePrintDataGroups().clear()` 会给每一个数据消费行
        // 都懒建 extras（那是白占，见 logical_line.h 的冷字段说明）。
        QList<QList<int>> groups;
        int endLine = -1;
        bool inList = false;
        for (int j = i + 1; j < data.lines.size(); ++j) {
            const QString& name = data.lines.at(j).functionName;
            if (name == QLatin1String("ENDDATA")) {
                endLine = j;
                break;
            }
            if (name == QLatin1String("DATALIST")) {
                inList = true;
                groups.append(QList<int>());
            } else if (name == QLatin1String("ENDLIST")) {
                inList = false;
            } else if (name == QLatin1String("DATA") || name == QLatin1String("DATAFORM")) {
                if (inList) {
                    if (groups.isEmpty()) groups.append(QList<int>());
                    groups.last().append(j);
                } else {
                    groups.append(QList<int>{j});
                }
            } else {
                break;
            }
        }
        if (!groups.isEmpty() || endLine >= 0) {
            line.mutablePrintDataGroups() = groups;
            line.mutablePrintDataEndLine() = endLine;
        } else {
            // 重复装载同一脚本时清掉旧索引；没有 extras 时本来就是空的。
            line.clearPrintDataIndex();
        }
    }

    QString currentFunction;   // 用于 #DIM 的作用域判定
    bool afterLabel = false;   // .ERB 中 # 行必须紧跟函数标签（对齐 C# ErbLoader）
    QSet<QString> labelNames;  // 仅用于装载日志的「本脚本标签名个数」，不保留

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
            labelNames.insert(line.labelName.toUpper());

            // 唯一标签索引的**唯一追加点**：@函数标签与 $GOTO 标签都收
            //（键 = 大写折叠；同脚本内按行号升序追加，跨脚本的全序在
            //  finalizeLabelIndex 里按 (脚本名, 行号) 固化）。isFunction 区分
            //  CALL 能不能用它。这是「名字 -> 行号」**唯一**的一张表：
            //  ScriptData 不再另存 labelPositions（旧表是 last-wins，与本索引的
            //  first-wins 会是两个答案）。
            LabelRef ref;
            ref.script = scriptName;
            ref.line = i;
            ref.isFunction = line.kind == LineKind::FunctionLabel;

            // 注册用户自定义函数（@label 即函数入口）—— 构建完整的声明节点
            if (line.kind == LineKind::FunctionLabel) {
                UserFunctionDecl decl;
                decl.name = line.labelName.toUpper();
                decl.originalName = line.labelName;
                decl.script = scriptName;
                decl.labelLine = i;

                // 形参表：把标签里写的名字归类为 ARG / ARGS / 私有变量
                for (const QString& argName : line.labelArgs()) {
                    UserParamDecl p = classifyUserParam(argName);
                    const QString defaultValue = line.labelDefaults().value(decl.params.size());
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
                    // 「变量分词」的形参名集合与函数表同点构建（append-only；
                    // 查找端 paramNamesOf 只读不插 —— 并行装载/收尾趟安全）。
                    QSet<QString> paramNames;
                    paramNames.reserve(decl.params.size());
                    for (const UserParamDecl& p : decl.params)
                        paramNames.insert(p.name.toUpper());
                    m_paramNamesByFunction.insert(decl.name, std::move(paramNames));
                }

                // 保留全部声明（事件函数的 4 组导航需要；同名多份）
                ref.isEvent = decl.isEvent;
                ref.isSingle = decl.isSingle;
                ref.isPri = decl.isPri;
                ref.isLater = decl.isLater;
                ref.isOnly = decl.isOnly;
            }
            // 追加点只有一个：到这里 ref 已经带全了 isFunction / 事件分组位。
            m_labelIndex[line.labelName.toUpper()].append(ref);
        }
    }

    m_scripts.insert(scriptName, data);
    m_privateDefaultPlans.clear();   // 新脚本可能带来新的私有声明 -> 计划重建
    // 注意：m_paramNamesByFunction **不在**这里清 —— 它是 append-only（注册点
    // 插入、first-wins 同 m_functions），在此清会把本次装载刚注册的函数集合
    // 一并抹掉（多脚本装载只剩最后一个脚本有集合）。整表清空在 clear() 里做。

    // 标记区：跳转标记 + 控制转移预绑定。
    // 注意构建的是 **m_scripts 里的那一份**（进表的是上面 insert 的拷贝，局部
    // `data` 已经不是运行期读的那份）—— 顺序与语义和搬出去之前逐字一致。
    if (auto markingIt = m_scripts.find(scriptName); markingIt != m_scripts.end()) {
        jumpmark::buildJumpMarkings(markingIt.value());
    }

    // 绑定/校验统一在 markLoadComplete 里单趟完成（全局定稿 → 重绑 → 诊断）：
    // 跨文件前向引用需要「全部声明就绪」，且避免了每脚本一次全局扫描
    // 在 eraTW（2200+ 脚本）上退化成 O(n²)。

    if (m_position.script.isEmpty()) {
        m_position.script = scriptName;   // 装载期的「默认脚本」，不是位置转移
    }

    qCDebug(eraTrace) << "[parse] 脚本" << scriptName << (isHeaderFile ? "(头文件)" : "")
             << "逻辑行" << data.lines.size() << "标签" << labelNames.size();
    emit parseCompleted(scriptName);
    return true;
}

void EraParseTable::setEntryPoint(const QString& label) {
    m_entryPoint = label;

    // 入口脚本的选择属于**装载期**（在一堆脚本里挑一个），不是位置转移：
    // 按旧实现逐字写字段，不发 memorySpaceChanged。
    //
    // 候选与 CALL/GOTO 同源：唯一标签索引 + 唯一的择一规则 position::firstLabel
    //（全序第一条 = 脚本名 QString::compare 最小，同脚本取行号最小）。
    // 此前这里是 for(m_scripts)（QHash 遍历序）——同一台机器不同进程会挑到
    // 不同的入口脚本（Qt 每进程随机播种 QHash）。
    const ScriptView view = makeScriptView();
    const LabelRef* entry = position::firstLabel(view, label, /*callableOnly=*/false);
    if (entry) {
        m_position.script = entry->script;
    }

    if (m_position.script.isEmpty()) {
        const QStringList commonLabels = {"MAIN_LOOP", "SYSTEM_TITLE", "MAIN", "TITLE"};
        for (const QString& labelToTry : commonLabels) {
            entry = position::firstLabel(view, labelToTry, /*callableOnly=*/false);
            if (entry) {
                m_position.script = entry->script;
                m_entryPoint = labelToTry;
                break;
            }
        }
    }

    if (!m_position.script.isEmpty()) {
        // 入口行同样取自索引（同一条目），不再回头查 labelPositions
        //（那张表对同名多份记的是最后一条，和这里的「第一条」会是两个答案）。
        const int entryLine = entry ? entry->line : -1;
        // 信号仍是 3 条，顺序仍是旧顺序：
        //   resetPosition()   -> callStackChanged(0)、positionChanged(script, 0)
        //   setPosition(...)  -> positionChanged(script, entryLine)
        resetPosition();
        // 入口是「跳转」落点：标签行应被跳过。
        PositionEffects fx;
        position::setPosition(m_position, makeScriptView(), m_position.script,
                              entryLine >= 0 ? entryLine : 0, /*jumped=*/true, fx);
        emitEffects(fx);
    }

    emit entryPointReached(label);
}

QString EraParseTable::getEntryPoint() const {
    return m_entryPoint;
}

int EraParseTable::getLabelPosition(const QString& scriptName, const QString& labelName) const {
    // 「某脚本内某名字在哪一行」与 CALL 目标 / 函数体起点是**同一个**答案：
    // 唯一索引 + 唯一择一规则（该脚本内全序第一条 = 行号最小的一条）。
    // @ 与 $ 都收（callableOnly=false）：本函数既用于函数体起点（ownerFunction，
    // 必然是 @），也用于 findGotoLabelInFunction 找 $ 标签的扫描起点。
    // 脚本必须真实存在 —— 空 onlyScript 在 firstLabel 里表示「不筛选」，
    // 不先挡掉就会退化成跨脚本查询。
    if (!script(scriptName)) return -1;
    const LabelRef* ref = position::firstLabel(makeScriptView(), labelName,
                                               /*callableOnly=*/false, scriptName);
    return ref ? ref->line : -1;
}

bool EraParseTable::resolveJumpTarget(const QString& label, int& position) {
    PositionEffects fx;
    const bool ok = position::resolveJumpTarget(m_position, makeScriptView(), label, position, fx);
    emitEffects(fx);   // 跨脚本命中时会亮 memorySpaceChanged（旧实现同样发）
    return ok;
}

bool EraParseTable::resolveJumpToScript(const QString& scriptName, const QString& label, int& position) {
    PositionEffects fx;
    const bool ok =
        position::resolveJumpToScript(m_position, makeScriptView(), scriptName, label, position, fx);
    emitEffects(fx);
    return ok;
}

QString EraParseTable::getCurrentScript() const {
    return m_position.script;
}

// ===========================================================================
// 位置区 (Position region)
//
// 全部算法在 position_state.cpp（namespace position：PositionState + ScriptView
// -> PositionEffects 的纯函数）。这里每个方法只做三件事：
//   ① 打包视图（两个指针）  ② 调自由函数  ③ 按 Effects 发信号
// —— 类里不再有任何一行位置算术。信号顺序写死在 emitEffects（见
// position_state.h 的契约；唯一例外是 clear()，它按旧顺序自己发两条）。
// ===========================================================================

ScriptView EraParseTable::makeScriptView() const {
    // 只打包指针，不读取内容（与 makeIrView 同形）。
    ScriptView view;
    view.scripts = &m_scripts;
    view.labelIndex = &m_labelIndex;
    return view;
}

void EraParseTable::emitEffects(const PositionEffects& fx, const QString& jumpLabel) {
    // 规范顺序（逐方法与旧实现核对过）：
    //   callStackChanged -> memorySpaceChanged -> positionChanged -> jumpRequested
    // 载荷取自转移**之后**的状态：旧实现 emit 时读的正是同一批值
    //（jumpRequested 的 targetPosition 就是刚设好的 PC）。
    if (fx.callStackChanged)   emit callStackChanged(m_position.depth);
    if (fx.memorySpaceChanged) emit memorySpaceChanged(m_position.script);
    if (fx.positionChanged)    emit positionChanged(m_position.script, m_position.line);
    if (fx.jumpRequested)      emit jumpRequested(m_position.script, jumpLabel, m_position.line);
}

const QString& EraParseTable::currentScript() const {
    return m_position.script;
}

int EraParseTable::currentLine() const {
    return m_position.line;
}

int EraParseTable::depth() const {
    return m_position.depth;
}

const QList<Frame>& EraParseTable::callStack() const {
    return m_position.callStack;
}

Frame EraParseTable::currentFrame() const {
    if (m_position.callStack.isEmpty()) {
        return Frame();
    }
    return m_position.callStack.last();
}

bool EraParseTable::hasPosition() const {
    return !m_position.script.isEmpty();
}

QString EraParseTable::executionScope() const {
    // 作用域 = 正在执行的那一行所属的函数；装载期（无位置/越界）返回空串，
    // 完整说明见 position_state.h 的 executionScope 契约。
    return position::executionScope(m_position, makeScriptView());
}

eravar::ScopeId EraParseTable::executionScopeId() const {
    return position::executionScopeId(m_position, makeScriptView());
}

void EraParseTable::resetPosition() {
    // 亮 callStackChanged -> positionChanged（规范顺序）。
    emitEffects(position::reset(m_position));
}

void EraParseTable::setPosition(const QString& script, int line, bool jumped) {
    PositionEffects fx;
    position::setPosition(m_position, makeScriptView(), script, line, jumped, fx);
    emitEffects(fx);
}

void EraParseTable::advance() {
    PositionEffects fx;
    position::advance(m_position, fx);
    emitEffects(fx);
}

bool EraParseTable::jumpToLine(int line) {
    PositionEffects fx;
    const bool ok = position::jumpToLine(m_position, makeScriptView(), line, fx);
    emitEffects(fx);
    return ok;
}

bool EraParseTable::jumpToLabel(const QString& label) {
    PositionEffects fx;
    const bool ok = position::jumpToLabel(m_position, makeScriptView(), label, fx);
    emitEffects(fx, label);   // jumpRequested 的载荷：label + 刚设好的 PC
    return ok;
}

// @函数标签解析的具体判据（当前脚本优先且必须是 @函数标签 / $ 标签不可调用 /
// 全局索引 O(1) 而非 1500 脚本扫描 / eraMegaten 的同名 $PRINT_TITLE）见
// position_state.h 的 resolveCallableLabel 契约；这里只做转发。
bool EraParseTable::resolveCallableLabel(const QString& label, QString& targetScript,
                                         int& targetLine) const {
    return position::resolveCallableLabel(m_position, makeScriptView(), label,
                                          targetScript, targetLine);
}

bool EraParseTable::callLabel(const QString& label, bool advanceWasCalled) {
    PositionEffects fx;
    const bool ok =
        position::callLabel(m_position, makeScriptView(), label, advanceWasCalled, fx);
    emitEffects(fx);
    return ok;
}

bool EraParseTable::callLabelWithReturn(const QString& label, int returnLine) {
    PositionEffects fx;
    const bool ok =
        position::callLabelWithReturn(m_position, makeScriptView(), label, returnLine, fx);
    emitEffects(fx);
    return ok;
}

bool EraParseTable::jumpLabel(const QString& label) {
    // 空栈退化 / 非空栈压 isJump 帧（继承当前帧返回地址）的完整契约见
    // position_state.h 的 jumpLabel 注释（C# IsJump + Return 的递归语义）。
    PositionEffects fx;
    const bool ok = position::jumpLabel(m_position, makeScriptView(), label, fx);
    emitEffects(fx);
    return ok;
}

bool EraParseTable::returnFromCall() {
    PositionEffects fx;
    const bool ok = position::returnFromCall(m_position, makeScriptView(), fx);
    emitEffects(fx);
    return ok;
}

void EraParseTable::switchToMemorySpace(const QString& scriptName) {
    PositionEffects fx;
    position::switchToMemorySpace(m_position, scriptName, fx);
    emitEffects(fx);
}

// ---------------------------------------------------------------------------
// 变量表：解析 #DIM/#DIMS/#GLOBAL/#GLOBALS/#PRIVATE，并把类型回填到 AST
// ---------------------------------------------------------------------------
namespace {
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
    // 预登记 = 一次装载的开始：并行解析线程从这里起只允许查声明（见
    // ErbLoader::parseOneFileReadOnly 的前置断言），不允许再插入新声明。
    if (m_phase == LoadPhase::Empty) m_phase = LoadPhase::Loading;
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
    const QString rest = loadpass::stripDirectiveComment(s.mid(i)).trimmed();

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

bool EraParseTable::isScopedVariableLine(const QString& rawLine) {
    return loadpass::isScopedVariableLine(rawLine);
}

void EraParseTable::registerScopedVariableLine(const QString& rawLine, const ScriptPosition& position,
                                               const QString& function) {
    const loadpass::ScopedVarSpec spec = loadpass::parseScopedVarSpec(rawLine);
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
    const loadpass::ScopedVarSpec spec = loadpass::parseScopedVarSpec(line.raw);
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


// ---------------------------------------------------------------------------
// 收尾步骤 4/5/6 + CALL 实参校验：实现已搬到 parse_checks.cpp（自由函数，输入是
// 只读的 ParseIrView）。这里只留同名 forwarder —— 保住 public API（LSP / 测试 /
// CLI 在用）与 kLoadPasses 表里的成员函数指针，行为与诊断逐条不变。
//
// 三个步骤**只读**：不写 m_scripts / 不改 AST / 不改装载状态。因此它们可以脱离
// EraParseTable 单独调用（见 test_parse_checks.cpp）。
//
// 重跑幂等由**数据**表达，不靠调用方纪律：每次运行先把诊断收进一个全新 sink，
// 再按 origin 整体替换上一切片（ParseDiagnostics::replaceOrigin）。于是
// 「跑 N 次」与「跑 1 次」逐字节相同；调用方不需要数次数，也没有「整表清空」
// 这一步可以忘（clearDiagnostics 已随该纪律一起删除）。
// ---------------------------------------------------------------------------
loadpass::LoadContext EraParseTable::makeLoadContext() {
    // 只打包引用/指针，不读取内容；表本身仍由 EraParseTable 持有。
    return loadpass::LoadContext{
        m_scripts, m_variables, m_functions, m_constantTable, m_variableStorage,
        m_evaluator, m_gameBaseData, m_diagnostics, m_labelIndex, m_paramNamesByFunction,
        m_declarationsResolved, m_constProvidersReady};
}

parsecheck::ParseIrView EraParseTable::makeIrView() const {
    parsecheck::ParseIrView view;
    view.scripts = &m_scripts;
    view.labelIndex = &m_labelIndex;
    view.variables = &m_variables;
    view.constantTable = m_constantTable;
    view.storage = m_variableStorage;
    view.functions = &m_functions;
    return view;
}

void EraParseTable::resolveStaticTargets() {
    if (!readOnlyPassAllowed("resolveStaticTargets")) return;
    // 本次运行独占一个 sink：不读旧诊断、不受旧切片影响。
    ParseDiagnostics sink;
    parsecheck::resolveStaticTargets(makeIrView(), sink);
    m_diagnostics.replaceOrigin(DiagOrigin::Pass4, sink.all());
}

void EraParseTable::checkCallStatementArgs(const LogicalLine& line, const QString& target) {
    // 不是 pass 表的独立步骤（由 resolveStaticTargets 内部同款逻辑调用）：
    // 只要求同一个阶段，不单独计数。
    if (!isBoundOrLater()) {
        qWarning().noquote() << "[parse] checkCallStatementArgs 在装载完成绑定之前调用，忽略（当前阶段"
                             << loadPhaseName(m_phase) << "）";
        return;
    }
    parsecheck::checkCallStatementArgs(makeIrView(), line, target, m_diagnostics);
}

void EraParseTable::checkStructures() {
    if (!readOnlyPassAllowed("checkStructures")) return;
    ParseDiagnostics sink;
    parsecheck::checkStructures(makeIrView(), sink);
    m_diagnostics.replaceOrigin(DiagOrigin::Pass5, sink.all());
}

void EraParseTable::checkStaticValues() {
    if (!readOnlyPassAllowed("checkStaticValues")) return;
    ParseDiagnostics sink;
    parsecheck::checkStaticValues(makeIrView(), sink);
    m_diagnostics.replaceOrigin(DiagOrigin::Pass6, sink.all());
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
    // 本 pass 的诊断也按 origin 切片替换：增量 append 后重新收尾时，
    // 旧切片不能残留（否则同一批「未被调用」会重复出现）。
    ParseDiagnostics sink;
    const auto commit = [this, &sink] {
        m_diagnostics.replaceOrigin(DiagOrigin::Pass7, sink.all());
    };

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
                commit();
                return;
            default:
                break;
            }
            const QString& n = line.functionName;
            if (n == QLatin1String("TRYCALLLIST") || n == QLatin1String("TRYJUMPLIST")
                || n == QLatin1String("GOTOLIST")) {
                commit();
                return;
            }
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
            sink.add(DiagSeverity::Warning, DiagCode::kNotCalled,
                line->position.filename, line->position.lineNumber, line->position.column, 0,
                QCoreApplication::translate("ParseDiagnostics",
                    "関数@%1は定義されていますが一度も呼び出されません").arg(line->labelName));
        }
    }
    commit();
}

void EraParseTable::resolveGlobalDeclarations() {
    // 实现已搬到 load_passes.cpp（自由函数，输入是可写的 LoadContext）。
    loadpass::LoadContext ctx = makeLoadContext();
    loadpass::resolveGlobalDeclarations(ctx);
}

// 收尾 pass 表：顺序 = 执行顺序（markLoadComplete 只负责按表驱动）。
//   gate       —— 该步受哪个开关门控（数据；驱动逻辑只有一处求值）；
//   phaseAfter —— 该步完成后允许进入的阶段（数据；只读步骤 4-7 要求 Bound）。
const EraParseTable::LoadPass EraParseTable::kLoadPasses[7] = {
    {"1/6 全局声明定稿(resolveGlobalDeclarations)", &loadpass::resolveGlobalDeclarationsStep, nullptr, LoadPassGate::Always, LoadPhase::Loading},
    {"2/6 就地定型(finalizeBindings)",              &loadpass::finalizeBindings,               nullptr, LoadPassGate::Always, LoadPhase::Loading},
    {"3/6 参数校验(finalizeValidation)",            &loadpass::finalizeValidation,             nullptr, LoadPassGate::Always, LoadPhase::Bound},
    {"4/6 跳转目标解析(resolveStaticTargets)",      nullptr, &EraParseTable::resolveStaticTargets, LoadPassGate::Always, LoadPhase::Bound},
    {"5/6 结构配对检查(checkStructures)",           nullptr, &EraParseTable::checkStructures,      LoadPassGate::Always, LoadPhase::Bound},
    {"6/6 静态值检查(checkStaticValues)",           nullptr, &EraParseTable::checkStaticValues,    LoadPassGate::Always, LoadPhase::Bound},
    {"7(可选) 未被调用函数检查(checkNotCalledFunctions)", nullptr, &EraParseTable::checkNotCalledFunctions, LoadPassGate::NotCalledWarning, LoadPhase::Finalized},
};

const char* EraParseTable::loadPhaseName(LoadPhase phase) {
    switch (phase) {
    case LoadPhase::Empty:     return "Empty";
    case LoadPhase::Loading:   return "Loading";
    case LoadPhase::Bound:     return "Bound";
    case LoadPhase::Finalized: return "Finalized";
    }
    return "?";
}

bool EraParseTable::isGateOpen(LoadPassGate gate) const {
    switch (gate) {
    case LoadPassGate::Always:            return true;
    case LoadPassGate::NotCalledWarning:  return m_functionNotCalledWarning;
    }
    return true;
}

bool EraParseTable::readOnlyPassAllowed(const char* entryName) const {
    if (!isBoundOrLater()) {
        qWarning().noquote() << "[parse]" << entryName
                             << "在装载完成绑定之前调用，忽略（当前阶段"
                             << loadPhaseName(m_phase) << "）";
        return false;
    }
    return true;
}

void EraParseTable::markLoadComplete() {
    // 装载收尾（唯一入口）：全局声明定稿 → 全体脚本就地定型/重绑 → 统一出诊断。
    // 必须在**全部**声明就绪后进行：跨文件前向引用（先装载的文件调用后装载文件的
    // 函数）此刻才可解析；诊断随后统一产生，不重复。各步串行（eraTW 桌面 13s+，
    // Android 更久），逐步留痕以便真机上定位「卡在收尾的哪一步」。
    if (m_phase == LoadPhase::Finalized) {
        // 定义好的越界行为：no-op（不重复产出诊断）。
        qWarning().noquote() << "[parse] markLoadComplete 重复调用：已定型，忽略";
        return;
    }
    QElapsedTimer completeTimer;
    completeTimer.start();
    // 视图只打包一次：1/2/3 号步骤吃它（自由函数），4-7 号步骤仍走成员 forwarder。
    loadpass::LoadContext ctx = makeLoadContext();
    for (const LoadPass& pass : kLoadPasses) {
        // 唯一一处按 Gate 求值。
        if (!isGateOpen(pass.gate)) continue;
        const qint64 stepMs = completeTimer.elapsed();   // 每步重置 = 单步耗时口径
        qInfo().noquote() << "[parse] 收尾步骤" << pass.label << "开始";
        if (pass.fn) pass.fn(ctx);
        else (this->*(pass.memberFn))();
        m_phase = pass.phaseAfter;
        qInfo().noquote() << "[parse] 收尾步骤" << pass.label << "完成:"
                          << (completeTimer.elapsed() - stepMs) << "ms";
    }
    // 诊断定序：装载并行 + QHash 随机化哈希种子会让产生顺序在进程间漂移。
    // 排序只在收尾做一次（运行期不排序），让 dump_warnings/UI/MCP 输出可复现。
    m_diagnostics.sortForOutput();
    // 可选步骤被门控跳过时，循环里不会走到它的 phaseAfter。
    if (m_phase != LoadPhase::Finalized) m_phase = LoadPhase::Finalized;
    qInfo().noquote() << "[parse] markLoadComplete：" << m_scripts.size() << "脚本"
                      << completeTimer.elapsed() << "ms";
}


// ---------------------------------------------------------------------------
// 字符串赋值 / #DIM 初值 / 用户函数类型：实现已搬到 load_passes.cpp（收尾步骤 2）。
// ---------------------------------------------------------------------------

void EraParseTable::applyPrivateVariableDefaults(const QString& function) {
    if (function.isEmpty()) return;
    // 「怎么写私有初值」是装载期静态（存储键 FUNC\x1fNAME、声明尺寸、初值表），
    // 每函数首调时构建一次计划，此后每次 CALL 只按计划直写存储 ——
    // 旧路径每次 CALL 都按裸名走 storageName（eraUpperKey 扫描 + 两次哈希）
    // + ensureArraySize（再解析一次）+ 逐项 setGlobal*D（每项再解析一次）。
    // applyPrivateVariableDefaults 恒在 enterCall 之后（被调函数作用域已就位），
    // 复合键解析与作用域无关（恒等），计划与旧路径结果一致。
    if (!m_variableStorage) return;
    for (const PrivateDefaultEntry& e : privateDefaultPlan(function.toUpper()))
        m_variableStorage->applyPrivateDefault(e.storageKey, e.isString, e.ensureSize,
                                               e.defaultInt, e.defaultStr);
}

const QList<EraParseTable::PrivateDefaultEntry>&
EraParseTable::privateDefaultPlan(const QString& fkey) {
    const auto cached = m_privateDefaultPlans.constFind(fkey);
    if (cached != m_privateDefaultPlans.constEnd()) return cached.value();
    QList<PrivateDefaultEntry> plan;
    for (const VariableDecl& d : m_variables.localsOfRef(fkey)) {
        if (d.isConst || !d.isPrivate || d.isReference || d.isCharaData) continue;
        PrivateDefaultEntry e;
        e.storageKey = eravar::makePrivateStorageKey(fkey, d.name);
        e.isString = (d.type == OperandType::Str);
        e.ensureSize = d.lengths.isEmpty() ? 0 : d.lengths.first();
        e.defaultInt = d.defaultInt;
        e.defaultStr = d.defaultStr;
        plan.append(std::move(e));
    }
    return *m_privateDefaultPlans.insert(fkey, std::move(plan));
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

// ---------------------------------------------------------------------------
// 函数调用重绑 + 校验（对齐 C# IdentifierDictionary.GetFunctionMethod）
//
// 把每个 FunctionNode 重新解析为 内置函数 / 用户自定义函数 / 未定义函数，
// 并用最终（就地定型后）的实参 AST 复刻 C# FunctionMethod.CheckArgumentType()。
// 必须在**全部**脚本的变量类型定稿之后进行（跨文件前向引用此刻才可解析）。
// ---------------------------------------------------------------------------

// 收集一个 AST 里的函数调用告警（内置函数参数不符 / 未定义的函数）。
// 同一位置的重复 AST 引用去重；其它行/文件保留各自的诊断。
// 纯函数：只读 AST，结果写入 out —— 可在线程池上按脚本并行调用。
