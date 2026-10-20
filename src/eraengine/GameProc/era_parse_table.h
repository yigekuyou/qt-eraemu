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
#ifndef ERA_PARSE_TABLE_H
#define ERA_PARSE_TABLE_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QList>
#include <QMetaType>
#include <QSharedPointer>
#include "ast/logical_line.h"
#include "ast/variable_table.h"
#include "ast/user_function.h"
#include "ast/parse_diagnostic.h"

// Forward declarations
class ConstantTable;
class ProcessState;
class ExecutionEngine;
class VariableStorage;
class GameBaseData;
class ExpressionEvaluator;

// ---------------------------------------------------------------------------
// 调用帧 (Call frame)
//
// CALL 时压栈、RETURN 时弹栈。一个帧只记录“如何回到调用者”，不持有任何
// 指针 / 引用 / 自有资源，因此是纯值类型。
// 对应官方 C# 的 CalledFunction.returnAddress 与 Process.functionList 栈。
// ---------------------------------------------------------------------------
struct Frame {
    QString script;
    int     returnLine = 0;
    QString callLabel;
    int     entryLine = -1;   // 被调函数 @label 的行号（用于识别函数体边界）
    bool    isJump = false;   // JUMP 系帧：RETURN 时连同本帧一起递归弹出（C# IsJump）

    Frame() = default;
    Frame(const QString& s, int line, const QString& label, int entry = -1, bool jump = false)
        : script(s), returnLine(line), callLabel(label), entryLine(entry), isJump(jump) {}

    bool operator==(const Frame& other) const {
        return script == other.script
            && returnLine == other.returnLine
            && callLabel == other.callLabel;
    }
};
Q_DECLARE_METATYPE(Frame)

// ---------------------------------------------------------------------------
// ScriptData - 只读区：一个脚本的完整 AST + 跳转标记（扁平，无树）
//
// lines 已经是「完整 AST」：每行是 ast::LogicalLine（含归约后的实参 AST、
// 条件 AST、以及扁平行号控制流字段）。标记区在此之上补充 O(1) 跳转表。
// ---------------------------------------------------------------------------
struct ScriptData {
    QList<LogicalLine>  lines;          // 完整 AST 逻辑行（对齐 C# LogicalLine 数组）
    QHash<QString, int> labelPositions; // @label（**大写折叠**）-> 行号（故查表须 label.toUpper()）
    QHash<int, int>     endifLines;     // 标记区：IF/SIF 行 -> ENDIF+1
    QHash<int, int>     elseLines;      // 标记区：IF/SIF 行 -> ELSEIF/ELSE/ENDIF
    QHash<int, int>     loopEndLines;   // 标记区：REPEAT/WHILE/FOR 行 -> 配对行
    QHash<int, int>     jumpTo;         // 控制转移：行 -> 目标行（C# JumpTo）
    QHash<int, int>     jumpToEnd;      // 循环开始行 -> 结束行
    QHash<int, QList<int>> ifBranches;  // IF 行 -> 其 ELSEIF/ELSE 行列表（C# IfCaseList）
    // TRYC 系异常块的配对（对齐 C# ErbLoader 的 nestStack + JumpToEndCatch）
    //   catchLines    : TRYC 行     -> 配对的 CATCH 行（失败时跳到它**之后**）
    //   endCatchLines : CATCH 行    -> 配对的 ENDCATCH 行（顺序落入时跳到它之后）
    QHash<int, int>     catchLines;
    QHash<int, int>     endCatchLines;
    // TRYCALLLIST/TRYJUMPLIST/TRYGOTOLIST 的配对（对齐 C# callList + JumpTo=ENDFUNC）
    //   funcEntries  : TRY*LIST 行 -> 体内各 FUNC 条目行号（按出现顺序）
    //   endFuncLines : TRY*LIST 行 -> 配对的 ENDFUNC 行（全部候选失败时跳到它**之后**）
    QHash<int, int>        endFuncLines;
    QHash<int, QList<int>> funcEntries;
    QString             path;           // 源文件路径（供入口点/诊断显示）
};

// 用户自定义函数 = AST 里的声明节点（见 ast/user_function.h）。
// 兼容旧名：UserFunctionInfo == UserFunctionDecl。
using UserFunctionInfo = UserFunctionDecl;

// ---------------------------------------------------------------------------
// LabelRef —— 一个函数标签的声明位置（同名标签可以有多条）
//
// 对齐 C# LabelDictionary.labelAtDic（名称 -> List<FunctionLabelLine>）：
// 事件函数（@EVENTTRAIN 等）在真实游戏里会被大量「口上」重复声明，
// 系统状态机需要按 #SINGLE/#PRI/#LATER/#ONLY 分成 4 组依次调用。
// 因此除 labelPositions（名称 -> 最后一条，用于普通 GOTO/CALL）之外，
// 还要保留全部声明。
// ---------------------------------------------------------------------------
struct LabelRef {
    QString script;
    int     line = -1;
    bool    isEvent = false;
    bool    isSingle = false;
    bool    isPri = false;
    bool    isLater = false;
    bool    isOnly = false;

    bool operator==(const LabelRef& o) const {
        return script == o.script && line == o.line;
    }
};
Q_DECLARE_METATYPE(LabelRef)

// ---------------------------------------------------------------------------
// EraParseTable —— 内存运行时容器（只读区 / 标记区 / 位置区）
//
//   只读区 : m_scripts（ScriptData，包含装载期绑定的 AST）
//   标记区 : ScriptData 内的 endifLines / elseLines / loopEndLines / jumpTo
//   位置区 : m_currentScript / m_currentLine(PC) / m_callStack / m_depth
// ---------------------------------------------------------------------------
class EraParseTable : public QObject {
    Q_OBJECT

public:
    explicit EraParseTable(ProcessState* state, QObject* parent = nullptr);
    explicit EraParseTable(ProcessState* state, ExecutionEngine* execEngine, QObject* parent = nullptr);
    ~EraParseTable();

    // 装载一个脚本的完整 AST（只读区填充 + 标记区构建）
    bool loadScript(const QString& scriptName, const QList<LogicalLine>& lines,
                    bool isHeaderFile = false, const QString& path = QString());

    // 卸载全部脚本（QUIT 关闭游戏）：释放 AST/标签/函数表/告警等解析期内存。
    // 调用后脚本表为空，需重新 loadScript + markLoadComplete 才能执行。
    void clear();

    [[nodiscard]] QString scriptPath(const QString& scriptName) const;
    // 是否存在某标签（跨全部脚本）
    [[nodiscard]] bool hasLabel(const QString& label) const;
    // 「可作为 CALL/JUMP 目标」的存在性（@函数标签）：与 resolveCallableLabel 同源，
    // O(1)。TRY 系探针（动态 FORM 名逐个试）用它 —— 旧 hasLabel 缓存 miss 后
    // 是 for(m_scripts) 全量扫描（eraTW @SETLIST_EXIST 一轮 2435 次探针 830ms）。
    [[nodiscard]] bool hasCallableLabel(const QString& label) const;

    void setEntryPoint(const QString& label);
    QString getEntryPoint() const;

    int getLabelPosition(const QString& scriptName, const QString& labelName) const;

    bool resolveJumpTarget(const QString& label, int& position);
    bool resolveJumpToScript(const QString& scriptName, const QString& label, int& position);

    QString getCurrentScript() const;

    void setGameBaseData(GameBaseData* data) { m_gameBaseData = data; }
    void setVariableStorage(VariableStorage* storage);
    void setExpressionEvaluator(ExpressionEvaluator* evaluator);

    // CSV 常量名表（解析期识别「常量名下标」）
    void setConstantTable(const ConstantTable* table) { m_constantTable = table; }
    [[nodiscard]] const ConstantTable* constantTable() const { return m_constantTable; }

    [[nodiscard]] bool ignoreTripleSymbols() const;

    // ---- 可选诊断开关（默认与 C# 的 config 默认值一致：都关/IGNORE）----
    // 未被调用函数告警（对齐 C# FunctionNotCalledWarning；默认 IGNORE）。
    [[nodiscard]] bool functionNotCalledWarning() const { return m_functionNotCalledWarning; }
    void setFunctionNotCalledWarning(bool on) { m_functionNotCalledWarning = on; }

    // ---- 只读区查询 ----
    [[nodiscard]] const ScriptData* script(const QString& scriptName) const;
    [[nodiscard]] QStringList scriptNames() const { return m_scripts.keys(); }
    // Load-time compilation only; finalized tables reject source text.
    [[nodiscard]] QSharedPointer<ExpressionNode> expressionAst(const QString& expr, bool quiet = false);
    // 同上，但**显式指定作用域**（ownerFunction）。finalize 阶段 m_currentScript/
    // m_currentLine 是陈旧值，直接调 expressionAst 会按「无作用域」解析 ——
    // 私有变量遮蔽同名 #DIM CONST、FORM 插值里的局部变量都会解析错。
    [[nodiscard]] QSharedPointer<ExpressionNode> expressionAstInScope(const QString& expr,
                                                                      const QString& scope,
                                                                      bool quiet = false);
    // ---- 运行期「文本 -> 变量引用」（GETVAR / GETVARS / SETVAR 的实现基础）----
    // 对齐 C# GetVarMethod 的 `LexicalAnalyzer.Analyse` + `ReduceExpressionTerm`：
    // 变量名是运行期拼出来的字符串，只能在运行期解析。与上面装载期解析的差别：
    //   ① **允许在收尾（markLoadComplete）之后调用** —— 变量表/常数表/名表都还在，
    //      「不再编译新源码」这条装载期约束不该挡住运行期的一次性解析；
    //   ② **不做 `#DIM CONST` 常量折叠** —— C# 拿到的 VariableTerm 对常量也成立，
    //      GETVAR 必须能识别出「这是常量」并按常数表取值（否则 `GETVAR("衣装部位_頭")`
    //      会拿到一个字面量、被当成「不是变量」）；
    //   ③ 静默（运行期解析失败不该再产生装载期诊断）。
    // 作用域 = 当前执行行所属的函数（私有变量可见，与 C# allowPrivate 同义）。
    [[nodiscard]] QSharedPointer<ExpressionNode> runtimeVariableReference(const QString& text);
    // EXISTVAR 的变量存在性/属性查询（对齐 C# ExistVarMethod ->
    // IdentifierDictionary.GetVariableToken(name, null, allowPrivate=true)）：
    // 位掩码 1=整型 2=字符串 4=常量 8=二维 16=三维；0 = 不存在。
    [[nodiscard]] int variableFlags(const QString& name) const;

    [[nodiscard]] const LogicalLine* lineAt(const QString& scriptName, int line) const;
    [[nodiscard]] int jumpTarget(const QString& scriptName, int line) const;

    // IF 行 -> 其 ELSEIF/ELSE 分支行列表（C# IfCaseList）；空表示无分支
    [[nodiscard]] QList<int> ifBranches(const QString& scriptName, int ifLine) const;

    // TRYC 系异常块：TRYC 行 -> 配对 CATCH 行 / CATCH 行 -> 配对 ENDCATCH 行
    // （-1 = 没有配对；对齐 C# InstructionLine.JumpToEndCatch）
    [[nodiscard]] int catchTarget(const QString& scriptName, int trycLine) const;
    [[nodiscard]] int endCatchTarget(const QString& scriptName, int catchLine) const;

    // TRYCALLLIST/TRYJUMPLIST/TRYGOTOLIST：行 -> 体内 FUNC 条目行 / 配对 ENDFUNC 行
    // （-1 = 没有配对；对齐 C# InstructionLine.callList / JumpTo）
    [[nodiscard]] QList<int> funcEntryLines(const QString& scriptName, int listLine) const;
    [[nodiscard]] int endFuncTarget(const QString& scriptName, int listLine) const;

    // 用户自定义函数注册表（对齐 C# FunctionLabelLine）
    [[nodiscard]] const UserFunctionInfo* userFunction(const QString& name) const;
    // 式子调用该名字时的返回类型；Unknown = 交给内置函数解析。
    // 对齐 C#：用户 @label 优先于内置函数，但无 #FUNCTION/#FUNCTIONS 的 @label
    // 不覆盖同名内置函数（1.721「#FUNCTION の無い関数は組み込み関数を上書きしない」）。
    [[nodiscard]] OperandType userCallReturnType(const QString& name) const;
    [[nodiscard]] const QHash<QString, UserFunctionInfo>& userFunctions() const { return m_functions; }
    // EXISTFUNCTION 语义（EmueraEE）：通常=1 / #FUNCTION=2 / #FUNCTIONS=3 / 未知=0。
    // caseInsensitive=false 时按声明处原始大小写精确匹配。
    [[nodiscard]] int functionExistsKind(const QString& name, bool caseInsensitive) const;

    // 同名标签的全部声明（按 脚本名 + 行号 排序；供事件四分组导航）
    // 对齐 C# LabelDictionary.GetEventLabels / SortLabels
    [[nodiscard]] QList<LabelRef> labels(const QString& name) const;

    // 某脚本的行数（用于计算「脚本结束」的返回地址）
    [[nodiscard]] int scriptLineCount(const QString& scriptName) const;
    // 某行上的标签名（函数标签；用于取函数私有变量初值）
    [[nodiscard]] QString labelNameAt(const QString& scriptName, int line) const;

    // 以显式返回地址跳到「某脚本的某一行」（事件分组导航用）
    bool callLabelAt(const QString& script, int line, int returnLine);
    // 同上，但显式指定「返回脚本」（系统层调用脚本函数时用：返回脚本 = 被调脚本自身，
    // 返回地址 = 该脚本末尾 → 函数体结束即回到系统层）
    bool callLabelAt(const QString& script, int line, const QString& returnScript, int returnLine);

    // 解析期诊断（分级 + 代码 + 位置；对齐 C# ParserMediator 的结构化告警）。
    // texts() 为兼容既有「文本告警」消费者（dbus loadState 的计数）而保留。
    [[nodiscard]] const ParseDiagnostics& parseDiagnostics() const { return m_diagnostics; }
    [[nodiscard]] const QStringList& parseWarnings() const { return m_diagnostics.texts(); }
    [[nodiscard]] int parseWarningCount() const { return m_diagnostics.size(); }
    // 装载期（预处理/词法/结构）告警：由 ErbLoader 汇总后回灌（Warning，code=preprocess）
    void addParseWarnings(const QStringList& warnings) {
        for (const QString& w : warnings) {
            m_diagnostics.addText(DiagSeverity::Warning, DiagCode::kPreprocess, w);
        }
    }
    // 装载期结构化诊断（ErbLoader 在 worker 线程收集：未识别指令/表达式归约失败等）
    void addParseDiagnostics(const ParseDiagnostics& d) {
        for (const ParseDiagnostic& x : d.all()) m_diagnostics.add(x);
    }

    // 变量表（#DIM/#DIMS/#GLOBAL/#PRIVATE + 函数形参）
    [[nodiscard]] const VariableTable& variableTable() const { return m_variables; }

    // 装载收尾（唯一入口）：全局声明定稿 → 全体脚本就地定型/重绑 → 统一出诊断，
    // 并把只读区定型（之后不再编译新源码）。
    void markLoadComplete();

    // 收尾子步骤：把 m_labelLists 每个同名列表按 (脚本名, 行号) 排序固化一次。
    // labels() 不再每次调用重排；resolveCallableLabel 的「第一份」有确定含义。
    void finalizeLabelIndex();

    // ---- HTML 字符串的**反向追踪**（装载器在装载期调用一次）-----------------
    // 从 `HTML_PRINT X` 出发回溯 X 的写链（`X = f1` / `X += f2` / `X += f3` …），
    // 把整条链在装载期拼成一棵 **PLUS 树**装回该行的实参：`PrintTemplateCompiler::
    // compile` 会把 PLUS 链压平成**一整条 markup**，于是跨片段的标签在装载期即闭合
    //（eraTW 钟表把 `<img …>` 写成两行），运行期只按既有路径求值 —— 不做标签解析。
    // 追踪不可靠（找不到整赋值基 / 途中有控制流等）时**放弃**，绝不半拼。
    // 必须在 markLoadComplete() 之前调用（定型与模板编译都在收尾完成）。
    void traceHtmlStringArguments();

    // ---- 「先预处理 → 先登记 #DIM/#DIMS → 再构造 AST」 ---------------------
    // 装载器按**预处理后**的行登记声明（与构建期所见逐字一致），随后结算
    // 维数/初值/用户函数类型；于是构建期解析器即可拿到完整用户变量类型，
    // 字符串赋值在构建期即按 StrForm 定型，收尾只需校验用户变量合法性。
    void beginDeclarationPreRegistration();
    void preRegisterDeclaration(const QString& rawLine, const ScriptPosition& position,
                                const QString& function, bool isHeaderFile);
    void finishDeclarationPreRegistration();   // 结算：维数/初值/用户函数类型
    [[nodiscard]] bool declarationsPreRegistered() const { return m_declarationsPreRegistered; }

    // ---- VARI / VARS（emuera.em 的**作用域变量声明**，emuera.net 系）--------
    // `VARI NAME` / `VARI NAME = 整式` / `VARI NAME, l1{, l2{, l3}}`：
    // 在当前函数里声明一个**动态私有变量**（VARS 是字符串版）。语义与
    // `#DIM(S) NAME{, l1…}` 相同，只是名字/类型在语句位置给出，且初值在
    // 运行期写到元素 0（仅当长度恰为 1）。受 setting.json 的
    // UseScopedVariableInstruction 门控（默认关闭；FL3.8 显式开启）。
    // 与 #DIM 一样必须**先登记再构造 AST**，否则同函数内后续的
    // `COLUMN:0 = "x"` 会按 Int 定型。
    // isScopedVariableLine()：判定一行是否为 VARI/VARS（供装载器预扫描）。
    // registerScopedVariableLine()：登记声明（幂等；同名取首个声明）。
    [[nodiscard]] static bool isScopedVariableLine(const QString& rawLine);
    void registerScopedVariableLine(const QString& rawLine, const ScriptPosition& position,
                                    const QString& function);
    // 构造完行之后：把「目标变量 + 初值」接成交互操作数（见 .cpp 说明）。
    void applyScopedVariableLine(LogicalLine& line, const QString& function);

    // 用缓存 AST 求值条件；返回 false 表示求值失败。
    bool evaluateCondition(const QString& scriptName, int line, bool& out, int argIndex = -1);
    bool evaluateAst(const QSharedPointer<ExpressionNode>& ast, bool& out);

    // ---- 位置区查询 ----
    // perf：返回引用而非按值拷贝（stepOnce/executeLine 每条指令都调用，
    // 按值返回会带来 QString 原子引用计数 + 拷贝；调用方均为立即使用或自行拷贝）。
    [[nodiscard]] const QString& currentScript() const;
    [[nodiscard]] int currentLine() const;
    [[nodiscard]] int depth() const;
    [[nodiscard]] const QList<Frame>& callStack() const;
    [[nodiscard]] Frame currentFrame() const;
    [[nodiscard]] bool hasPosition() const;
    // 当前执行行所属的函数名（空 = 不在任何函数内）。
    // 运行期常数解析的作用域：私有 `#DIM CONST` 只在其声明函数内可见，
    // 与 C# IdentifierDictionary.GetVariableToken 用 line.ParentLabelLine
    // 查私有变量表等价（执行到哪个函数的行，就只看得见该函数的私有常量）。
    [[nodiscard]] QString executionScope() const;

    // 上一次位置变化是不是「跳转」（相对顺序推进）。用于区分
    // 「顺序落入函数标签」（= 函数结束）与「跳转/调用到函数标签」（= 跳过标签）。
    // 对齐 C# runScriptProc 每轮先 ShiftNextLine 再判断的语义。
    [[nodiscard]] bool jumpedToCurrent() const { return m_jumped; }

signals:
    void entryPointReached(const QString& label);
    void jumpRequested(const QString& targetScript, const QString& label, int targetPosition);
    void parseCompleted(const QString& scriptName);
    void memorySpaceChanged(const QString& scriptName);
    void positionChanged(const QString& script, int line);
    void callStackChanged(int depth);

public slots:
    void resetPosition();
    void setPosition(const QString& script, int line, bool jumped = true);
    void advance();
    bool jumpToLine(int line);
    bool jumpToLabel(const QString& label);
    bool callLabel(const QString& label, bool advanceWasCalled = false);
    // 以显式返回地址压帧并跳转（执行链同步调用用户函数时使用）
    bool callLabelWithReturn(const QString& label, int returnLine);
    // JUMP / TRYJUMP / TRYJUMPLIST：跳转但**不产生新的返回地址** —— 新帧继承
    // 当前帧的返回地址（对齐 C# CalledFunction.IsJump + ProcessState.Return 的
    // 「JUMP 帧弹出后立刻递归 Return」效果：JUMP 目标函数 RETURN 时直接回到
    // 当前函数的调用者）。
    bool jumpLabel(const QString& label);
    // @函数标签解析（上面三者的共用实现）：当前脚本优先（且必须是 FunctionLabel），
    // 否则查 m_labelLists 全局索引（markLoadComplete 已排序固化）。
    // 旧实现是 for(m_scripts) 全量扫描 + 每脚本一次 label.toUpper() ——
    // 1500+ 脚本的语料上每次跨文件 CALL/式中调用都要 ~0.7ms。
    [[nodiscard]] bool resolveCallableLabel(const QString& label, QString& targetScript,
                                             int& targetLine) const;
    // 函数私有变量初值（#DIM X = 7）——进入函数时由执行侧调用
    void applyPrivateVariableDefaults(const QString& function);
    // 每函数的形参名集合（大写，按需构建/缓存）。「变量分词」的形参判定用：
    // 形参的值绑定（Parameter）/ ARG 别名 / REF 引用绑定都是运行期语义，
    // 命中形参名的变量引用无法静态定键 -> AccessKind::Dynamic。
    // 装载期解析（parseScopedExpression）与收尾定型（applyVariableTypes）
    // **共用同一张表** —— 收尾的赋值 AST 重建在定型趟之后跑，重建出的节点
    // 不再回定型；若装载期不带形参表，重建节点会带上「私有数组」的错描述符。
    const QSet<QString>* paramNamesOf(const QString& upperScope) const;
    bool returnFromCall();
    void switchToMemorySpace(const QString& scriptName);

private:
    // 「怎么写私有初值」是装载期静态：存储键 = FUNC\x1fNAME、声明尺寸、初值表。
    // 每函数构建一次（首调时），applyPrivateVariableDefaults 只按计划直写 ——
    // 旧路径每次 CALL 都按裸名走 storageName（toUpper 扫描 + 两次哈希）
    // + ensureArraySize（又一次解析），CALL 密集的游戏每个函数调用都付。
    struct PrivateDefaultEntry {
        QString     storageKey;   // 复合键；复合键不在作用域映射表里，解析恒等
        bool        isString = false;
        int         ensureSize = 0;      // 声明尺寸（只扩不缩）
        QList<qint64> defaultInt;
        QStringList   defaultStr;
    };
    [[nodiscard]] const QList<PrivateDefaultEntry>& privateDefaultPlan(const QString& fkey);
    void buildJumpMarkings(const QString& scriptName);

    // expressionAstInScope / runtimeVariableReference 的共用实现。
    //   foldConstants      = false -> 不把 `#DIM CONST NAME` 折叠成字面量
    //   allowAfterFinalize = true  -> 收尾之后仍允许（运行期一次性解析）
    [[nodiscard]] QSharedPointer<ExpressionNode> parseScopedExpression(const QString& expr,
                                                                      const QString& scope,
                                                                      bool quiet,
                                                                      bool foldConstants,
                                                                      bool allowAfterFinalize);

    // 变量声明解析（#DIM/#DIMS/#GLOBAL/#GLOBALS/#PRIVATE）与类型回填
    void parseVariableDeclaration(const LogicalLine& line, const QString& currentFunction,
                                  bool isHeaderFile);
    // 文本版（预扫描「先登记 #DIM/#DIMS 再构造 AST」用）：直接按声明文本登记。
    void parseVariableDeclarationText(const QString& rawLine, const ScriptPosition& position,
                                      const QString& currentFunction, bool isHeaderFile);
    // 全体脚本变量类型回填（**就地定型**，见 .cpp 说明）。
    void applyVariableTypes();
    // 字符串赋值的右值改按 StrForm 解析（对齐 C# AnalyseFormattedString）
    void applyStringAssignments();
    // #DIM/#DIMS 初值：全局装载时写入
    void applyVariableDefaults(const QList<VariableDecl>& decls);
    void applyGlobalVariableDefaults();
    // 用户自定义函数的强类型化：形参类型由变量表回填（#DIM/#DIMS + ARG/ARGS），
    // 返回类型由 #FUNCTION(S) 决定。必须在 markLoadComplete 内、变量声明解析之后。
    void resolveUserFunctionTypes();
    // 调用点类型校验（CALL 语句 + 式中调用）
    QString checkUserCallArgs(const UserFunctionDecl& decl,
                              const QList<OperandType>& argTypes) const;
    // 参数个数/类型校验（markLoadComplete 内、类型回填之后调用）
    void validateArguments();
    // 函数调用重绑 + 校验（markLoadComplete 内、类型回填之后调用）：
    // 把每个 FunctionNode 重新解析为 内置函数 / 用户自定义函数 / 未定义函数，
    // 并用最终（类型回填后）的实参 AST 复刻 C# FunctionMethod.CheckArgumentType()。
    void resolveFunctionNodes();
    // 全体脚本绑定：字符串赋值/FUNC/变量类型/赋值 AST/函数调用解析（一趟）。
    void finalizeBindings();
    // 全体脚本校验：产出诊断，须在**全部绑定完成后**统一调用（跨文件前向引用）。
    void finalizeValidation();

public:
    // ---- 只读检查步骤（可单独调用）---------------------------------------
    // 下列三步只读：不改变运行行为、不改 AST 语义，只读取已定型的数据并产出
    // 诊断。可以脱离 markLoadComplete 单独调用（例如 LSP/测试按需重跑），
    // 但**重复调用会重复产生诊断** —— 调用方负责去重或先 clearDiagnostics。
    void resolveStaticTargets();      // 收尾步骤 4：静态解析运行期跳转/调用目标（对齐 C# setJumpTo）
    // CALL 语句的实参校验（对齐 C# ConvertArg 在 setJumpTo 里的装载期调用）：
    // 只在「常量目标且目标存在」时下结论 —— 个数超限、字符串实参传整型形参、
    // REF 实参不能省略/必须是同类型数组。
    void checkCallStatementArgs(const LogicalLine& line, const QString& target);
    // 收尾步骤 5：结构/配对检查（对齐 C# ErbLoader.nestCheck 的诊断部分）：
    // IF/ELSEIF/ELSE/ENDIF、REPEAT/REND、FOR/NEXT、WHILE/WEND、DO/LOOP、
    // SELECTCASE/CASE/ENDSELECT、TRYC/CATCH/ENDCATCH、TRY*LIST/FUNC/ENDFUNC、
    // PRINTDATA/DATA/DATALIST/ENDLIST/ENDDATA/STRDATA、NOSKIP/ENDNOSKIP、
    // BREAK/CONTINUE 的位置、$标签在受限块内、SIF 的次行、RETURNF 的位置与类型。
    void checkStructures();
    // 收尾步骤 6：可静态判定的运行期错误（字面量除零 / 常量下标越界）。
    // 这类错误在 C# 里是运行期 CodeEE（"0による除算が行なわれました" /
    // "配列型変数Xの第N引数(…)は配列の範囲外です"）；当操作数是**字面量**时，
    // 「运行到那行才炸」是确定事件，因此提前到装载期报出来。
    void checkStaticValues();

private:
    // 收尾步骤 7（可选）：定义了但从未被调用的函数（对齐 C# ErbLoader.checkScript 的
    // FunctionNotCalledWarning）。C# 的**默认值是 IGNORE**，且只要有 CALLFORM 系
    // （名字运行期求值）就无法判定「没被调用」，此时全部不报 —— eraTW 两种条件都命中，
    // 因此默认关闭、真实游戏一般不开。开启后也用同一套判据，避免误报。
    void checkNotCalledFunctions();
    // 全局声明定稿：常量/维数求值器 + 维数 + 全局初值 + 用户函数类型（幂等）。
    void resolveGlobalDeclarations();
    // 收尾步骤 1 的组合入口（顺序不变）：resolveGlobalDeclarations()
    // 之后紧接 finalizeLabelIndex()。仅为收尾 pass 表提供一个单函数指针。
    void resolveGlobalDeclarationsStep();

    // 收尾 pass 表条目（定义见 .cpp 的 EraParseTable::kLoadPasses）。
    // 表的顺序就是执行顺序；日志文案也来自这里，新增步骤只改表。
    struct LoadPass {
        const char* label;             // 日志步骤名（含 "N/6" 编号）
        void (EraParseTable::*fn)();   // 步骤入口
        bool optional;                 // 受开关门控（未开启则整步跳过）
        bool readOnly;                 // 只读（可独立调用/并行参考；当前仍串行）
    };
    static const LoadPass kLoadPasses[7];
    // 校验告警收集器：按脚本并行时每脚本各持一个，随后**按脚本顺序**合并。
    //   warnings : 告警文本，保持脚本内产生顺序；
    //   keys     : 与 warnings 一一对应；非空 = 该告警的「去重 key」
    //              （同一函数表达式跨行共享时只报一次），空串 = 不去重（结构告警）；
    //   seen     : 本脚本内去重（跨脚本去重在有序合并阶段再统一做）。
    struct WarningCollector {
        QList<QString> warnings;
        QList<QString> keys;
        QList<ScriptPosition> positions;
        QSet<QString> seen;
    };
    // 纯函数（不触碰成员）：只读 AST + 写入 out —— 线程安全，供并行校验使用。
    static void collectFunctionWarnings(const QSharedPointer<ExpressionNode>& ast,
                                        const ScriptPosition& position, WarningCollector& out);

    void setCurrentLineInternal(int line, bool forceEmit = false);
    void pushFrame(const Frame& frame);
    Frame popFrame();
    int lineCountFor(const QString& script) const;

    bool m_finalized = false;
    // 未被调用的函数告警开关（对齐 C# Config.FunctionNotCalledWarning；默认 IGNORE）
    bool m_functionNotCalledWarning = false;
    bool m_constProvidersReady = false;   // 常量/维数求值器是否已设置
    bool m_declarationsPreRegistered = false;   // 声明已由装载器预登记（loadScript 不再重复登记）
    QHash<QString, ScriptData> m_scripts;
    // perf：hasLabel 的结果按标签名记忆（此前每次 TRYCALL/TRYCALLFORM 都遍历
    // 全部脚本的 labelPositions；eraTW 地图逐字符 TRYCALLFORM 时是热点）。
    // 装载期每插入一个脚本即失效重建。
    mutable QHash<QString, bool> m_hasLabelCache;
    QHash<QString, UserFunctionInfo> m_functions;
    mutable QHash<QString, QSet<QString>> m_paramNamesByFunction;   // 形参名集合缓存（loadScript/clear 时失效）
    // 每函数的私有初值调用计划（loadScript/clear 时失效：新脚本可能带来新私有声明）
    QHash<QString, QList<PrivateDefaultEntry>> m_privateDefaultPlans;
    // 名称 -> 全部声明（同名函数可重复声明；事件函数靠它做 4 组导航）
    QHash<QString, QList<LabelRef>> m_labelLists;
    ParseDiagnostics m_diagnostics;
    const ConstantTable* m_constantTable = nullptr;
    VariableTable m_variables;

    QString m_entryPoint;
    ProcessState* m_state;
    ExecutionEngine* m_executionEngine;
    VariableStorage* m_variableStorage = nullptr;
    GameBaseData* m_gameBaseData = nullptr;   // GAMEBASE_* 求值（条件表达式）
    ExpressionEvaluator* m_evaluator = nullptr;

    QString       m_currentScript;
    int           m_currentLine = 0;
    QList<Frame>  m_callStack;
    int           m_depth = 0;
    bool          m_jumped = false;
};

#endif // ERA_PARSE_TABLE_H
