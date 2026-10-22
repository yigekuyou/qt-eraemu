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
#include "structure_issue.h"

// Forward declarations
class ConstantTable;
class ProcessState;
class ExecutionEngine;
class VariableStorage;
class GameBaseData;
class ExpressionEvaluator;

// 只读收尾步骤（4/5/6 号 pass）的输入视图，实现见 parse_checks.h。
// 这里只前置声明：makeIrView() 的定义在 .cpp（那边才需要完整类型）。
namespace parsecheck { struct ParseIrView; }
namespace loadpass { struct LoadContext; }

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
// 位置区状态：执行位置 + 调用栈。纯数据，没有任何行为 ——
// 转移函数在 position_state.h（namespace position），本类只负责
// 「调转移 + 按 Effects 发信号」。
// ---------------------------------------------------------------------------
struct PositionState {
    QString       script;              // 当前内存空间（空 = 还没有位置）
    int           line = 0;            // PC
    QList<Frame>  callStack;
    int           depth = 0;           // == callStack.size()，缓存给人读
    bool          jumped = false;      // 上一次位置变化是不是跳转
    // executionScopeId() 的缓存：按「当前行所属段」记忆。段身份由
    // LogicalLine::scopeKey 直接给出（装载期唯一写入），运行期没有名字回查。
    eravar::ScopeId   scopeId = eravar::kGlobalScope;
    bool              scopeIdValid = false;
};

// 一次位置转移**对外要广播什么**（纯数据，不带顺序语义）。
// 壳子按 position_state.h 里写死的顺序 emit；Effects 只回答「发哪个」。
// 载荷（脚本名/行号/深度/标签）不在这里 —— 转移完成后从 PositionState 与
// 调用参数里取，与旧实现 emit 时读的正是同一批值。
struct PositionEffects {
    bool callStackChanged = false;
    bool memorySpaceChanged = false;
    bool positionChanged = false;
    bool jumpRequested = false;
};

// ---------------------------------------------------------------------------
// ScriptData - 只读区：一个脚本的完整 AST + 跳转标记（扁平，无树）
//
// lines 已经是「完整 AST」：每行是 ast::LogicalLine（含归约后的实参 AST、
// 条件 AST、以及扁平行号控制流字段）。标记区在此之上补充 O(1) 跳转表。
// ---------------------------------------------------------------------------
struct ScriptData {
    QList<LogicalLine>  lines;          // 完整 AST 逻辑行（对齐 C# LogicalLine 数组）
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
    // 配对语法的未解析记录：装载期由 jumpmark::buildJumpMarkings 一次写入，
    // 收尾第 5 步（parsecheck::checkStructuresInScript）只读它出诊断。
    // **唯一**一份「哪些结构没配上」的判定 —— 跳转表是它的运行期投影。
    QList<StructureIssue> structureIssues;
    QString             path;           // 源文件路径（供入口点/诊断显示）
};

// 用户自定义函数 = AST 里的声明节点（见 ast/user_function.h）。
// 兼容旧名：UserFunctionInfo == UserFunctionDecl。
using UserFunctionInfo = UserFunctionDecl;

// ---------------------------------------------------------------------------
// LabelRef —— 一个标签（@函数标签 / $GOTO 标签）的声明位置
//
// 对齐 C# LabelDictionary.labelAtDic（名称 -> List<FunctionLabelLine>）：
// 事件函数（@EVENTTRAIN 等）在真实游戏里会被大量「口上」重复声明，
// 系统状态机需要按 #SINGLE/#PRI/#LATER/#ONLY 分成 4 组依次调用，
// 所以**全部声明**都要留下来（labels() 的 4 组导航就是这张表的投影）。
// ---------------------------------------------------------------------------
struct LabelRef {
    QString script;
    int     line = -1;
    // 所属函数段的起点（= 该段那个 @函数标签的行号；首个 @ 之前的文件头段 = -1）。
    // 对齐 C# FunctionLabelLine.ParentLabelLine：GOTO 的 $ 标签只在**自己那一段**
    // 内有效，这里把这个「段身份」放在声明旁边，运行期一次查表就能过滤，
    // 不必为了判定归属去扫行（旧 findGotoLabelInFunction 每次 GOTO 从函数入口
    // 逐行扫到下一个 @）。与 isFunction 同源：装载期在**唯一登记点**写死，
    // 同一段里的所有行共享这个值；同名函数重复声明时两份声明是两个不同的段。
    int     functionLine = -1;
    // @函数标签 = true（CALL / JUMP / 式中调用的合法目标）；
    // $GOTO 标签 = false（只有 GOTO 认它；C# FunctionIdentifierDictionary 只收 @）。
    bool    isFunction = true;
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

// 「(脚本名, 行号)」的全序 —— **唯一**的裁决点。
// 脚本名 QString::compare 升序，同脚本内行号升序。
// C# 的原序是 FunctionLabelLine.CompareTo = (FileIndex, LineNo)，即「装载得早的
// 那份赢」（first-wins）。本移植的装载是并行的，装载序本身不确定，所以用
// 脚本名代替 FileIndex —— 这是同一语义的唯一确定化：排序规则只有本函数一处。
// 三个投影共用它：
//   * labelRefLess         —— 标签索引（finalizeLabelIndex / labels）
//   * functionDeclLess     —— 用户函数表（EraParseTable::refreshUserFunction）
//   * position::firstLabel —— 取排序后的第一条（索引已固化）
inline bool scriptLineLess(const QString& aScript, int aLine,
                           const QString& bScript, int bLine) {
    if (aScript != bScript) return QString::compare(aScript, bScript) < 0;
    return aLine < bLine;
}

inline bool labelRefLess(const LabelRef& a, const LabelRef& b) {
    return scriptLineLess(a.script, a.line, b.script, b.line);
}

// 用户函数声明与标签同序：同一份 (脚本名, @label 行号)。
// 表里每个 (脚本, 函数名) 只保留行号最小的那一份，跨脚本重名时脚本名最小者赢
// —— 与 m_labelIndex 的 @ 标签第一条、position::firstLabel 完全同源。
inline bool functionDeclLess(const UserFunctionInfo& a, const UserFunctionInfo& b) {
    return scriptLineLess(a.script, a.labelLine, b.script, b.labelLine);
}

// ---------------------------------------------------------------------------
// 位置区唯一需要的两张表（唯一标签索引 + 行数）。
// 不复用 parsecheck::ParseIrView —— 那 5 个字段里位置区一个都不读，
// 视图只装真正被读的数据。
//
// labelIndex：裸标签名（**大写折叠**） -> 该名字的**全部**声明，按
// labelRefLess 排成一个全序。同脚本内的声明在 loadScript 追加时即按行号升序
// （单次装载循环），全序里的跨脚本部分由装载收尾 1/6（loadpass::
// finalizeLabelIndex）排序固化。因此「只查某脚本」（onlyScript 非空）在装载
// 期就已确定，与收尾时机无关；跨脚本挑选才依赖收尾的那一次排序。
// 「裸标签属于哪个脚本」「某脚本内某名字在哪一行」都只有这一份数据可查
// （见 position::firstLabel）。
// ---------------------------------------------------------------------------
struct ScriptView {
    const QHash<QString, ScriptData>* scripts = nullptr;
    const QHash<QString, QList<LabelRef>>* labelIndex = nullptr;
};

// ---------------------------------------------------------------------------
// EraParseTable —— 内存运行时容器（只读区 / 标记区 / 位置区）
//
//   只读区 : m_scripts（ScriptData，包含装载期绑定的 AST）
//   标记区 : ScriptData 内的 endifLines / elseLines / loopEndLines / jumpTo
//   位置区 : m_position（script / line(PC) / callStack / depth）
// ---------------------------------------------------------------------------
class EraParseTable : public QObject {
    Q_OBJECT

public:
    explicit EraParseTable(ProcessState* state, QObject* parent = nullptr);
    explicit EraParseTable(ProcessState* state, ExecutionEngine* execEngine, QObject* parent = nullptr);
    ~EraParseTable();

    // -----------------------------------------------------------------
    // 装载阶段（显式状态机）—— 每个公开入口声明它要求的阶段
    //
    //   Empty      : 没有脚本（构造后 / clear() 之后）。loadScript 从这里开始。
    //   Loading    : 脚本/声明正在增长。loadScript、预登记、并行解析只允许在这里。
    //   Bound      : markLoadComplete 的前三步（声明定稿 / 就地定型 / 参数校验）
    //                已完成；只读检查步骤（4/5/6/7）允许运行，也可重复运行。
    //   Finalized  : markLoadComplete 返回；只读区定型，不再编译新源码。
    //
    // 越界调用的定义：qWarning 到 stderr + no-op（返回 false / 不做任何事）。
    // 不用 Q_ASSERT —— 这些入口在 release 里也必须安全，且“忽略”不会产生
    // 与阶段相关的错误数据（诊断容器不变）。
    // -----------------------------------------------------------------
    enum class LoadPhase : quint8 { Empty, Loading, Bound, Finalized };
    [[nodiscard]] LoadPhase loadPhase() const { return m_phase; }
    [[nodiscard]] bool isFinalized() const { return m_phase == LoadPhase::Finalized; }
    [[nodiscard]] bool isBoundOrLater() const {
        return m_phase == LoadPhase::Bound || m_phase == LoadPhase::Finalized;
    }
    [[nodiscard]] static const char* loadPhaseName(LoadPhase phase);

    // 只读收尾步骤（4/5/6/7）的诊断各有**一个 origin 切片**：步骤驱动把本次
    // 运行的全部诊断合并进 ParseDiagnostics 时统一盖 origin，再整体替换旧切片
    //（ParseDiagnostics::replaceOrigin）。因此重复调用是**数据幂等**的：
    // 跑两次/三次与跑一次逐字节相同，调用方不需要数次数、也不需要先清空。

    // 装载一个脚本的完整 AST（只读区填充 + 标记区构建）。
    // 要求阶段：Empty / Loading。**Finalized 时拒绝**（qWarning + return false）：
    // 定型之后再装脚本会让「已定型的只读区」与「新源码」共存，必须由调用方
    // 显式声明意图 —— 先 beginIncrementalLoad()（Finalized -> Loading），
    // 装载完再 markLoadComplete() 让新脚本被定型/重绑。
    // 协议是显式的：不存在「loadScript 顺手重置状态机」的隐式行为。
    bool loadScript(const QString& scriptName, const QList<LogicalLine>& lines,
                    bool isHeaderFile = false, const QString& path = QString());

    // 增量装载的显式入口：Finalized -> Loading（其余阶段 = qWarning + false）。
    // 仅在「已定型 + 要追加/替换脚本」时使用；调用方随后必须
    // loadScript(...) 至少一次并 markLoadComplete()。
    // 替换同名脚本时，旧 AST 的装载期逐行诊断（Load）会随之移除，
    // 因此「重载同一个脚本」与「加载它的当前版本」诊断逐条相同。
    bool beginIncrementalLoad();

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
    // 诊断/测试用：某脚本内**第一份**同名 @函数的段身份文本（名字 -> 段身份）。
    // 同名 @F 的第二份会歧义，因此**运行期路径绝不使用**本函数（变量作用域
    // 一律读行上的 scopeKey）。
    [[nodiscard]] QString segmentKeyOfLabel(const QString& scriptName,
                                            const QString& labelName) const;

    // GOTO 的 $ 标签解析：本脚本内、**同一函数段**（段起点行 == functionSegmentLine）
    // 的 $ 标签里取行号最小的一条；找不到返回 -1。
    // 运行期只做一次索引查询 + 声明数（通常 1~3）次字段比较 —— 不扫行、不解析、
    // 不做逐行大小写比较（名字折叠与段归属都在装载期做完了）。
    // functionSegmentLine = -1 表示「第一个 @ 之前的文件头段」。
    [[nodiscard]] int firstGotoLabelPosition(const QString& scriptName, int functionSegmentLine,
                                             const QString& labelName) const;

    // 当前执行位置所在的函数段起点行（= 当前行的 @标签行；文件头 = -1）。
    // 有调用帧时帧里的 entryLine 就是段起点（事件导航用 callLabelAt 可以显式指定
    // 同名函数的**非首份**声明，所以不能用名字回查索引）；没有帧（depth==0 的
    // 系统入口）时直接读行上的 segmentStart（装载期写入的段起点下标）。
    [[nodiscard]] int currentFunctionSegment() const;

    bool resolveJumpTarget(const QString& label, int& position);
    bool resolveJumpToScript(const QString& scriptName, const QString& label, int& position);

    QString getCurrentScript() const;

    void setGameBaseData(GameBaseData* data) { m_gameBaseData = data; }
    void setVariableStorage(VariableStorage* storage);
    // 已接上的变量存储（只读；checkStaticValues 的尺寸来源）。未接上 = nullptr。
    [[nodiscard]] const VariableStorage* variableStorage() const { return m_variableStorage; }
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
    // 全部脚本的只读视图（收尾的只读检查步骤 / 外部取证用；不提供可变版本）
    [[nodiscard]] const QHash<QString, ScriptData>& scripts() const { return m_scripts; }
    // Load-time compilation only; finalized tables reject source text.
    [[nodiscard]] QSharedPointer<ExpressionNode> expressionAst(const QString& expr, bool quiet = false);
    // 同上，但**显式指定段身份文本**。finalize 阶段当前位置是
    // 陈旧值，直接调 expressionAst 会按「无作用域」解析 ——
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
    // 标记区的读侧（跳转/分支/CATCH/FUNC 列表查表）见 jump_markings.h ——
    // 输入只有 ScriptData*，不需要经由本类的成员函数。

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
    // 唯一标签索引本身（只读）：供只读收尾 pass 建「全库标签名并集」与测试取证。
    [[nodiscard]] const QHash<QString, QList<LabelRef>>& labelIndex() const {
        return m_labelIndex;
    }

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
    // 要求阶段：Empty / Loading（Empty = 空装载，仍算一次有效收尾）。
    // 已 Finalized 时 qWarning + no-op —— 要再收尾必须先 beginIncrementalLoad()
    // + loadScript。增量重收尾是**幂等**的：每个 pass 的诊断切片原位替换，
    // 反复「装载 → 收尾」后的诊断集合等于「同样输入一次性装载」。
    void markLoadComplete();

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
    // scriptName / segmentStart = 段身份的两半（**预处理后行序**里的 @ 标签下标，
    // 文件头段 = -1）。函数名只用于展示（VariableDecl::function），不再充当
    // 私有作用域键 —— 同名 @F 的两段因此得到两个不同身份。
    void preRegisterDeclaration(const QString& rawLine, const ScriptPosition& position,
                                const QString& function, bool isHeaderFile,
                                const QString& scriptName, int segmentStart);
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
                                    const QString& function, const QString& scriptName,
                                    int segmentStart);
    // 构造完行之后：把「目标变量 + 初值」接成交互操作数（见 .cpp 说明）。
    void applyScopedVariableLine(LogicalLine& line, const QString& function,
                                 const QString& scopeKey);

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
    // 当前执行行的**段身份文本**（空 = 尚未装载/手工行）。运行期常数解析
    // 的作用域：私有 `#DIM CONST` 只在其声明段内可见。这是 identity 文本，
    // **不是函数名** —— 同名 @F 的两段互不可见。
    [[nodiscard]] QString executionScopeKey() const;
    // 同上，intern id（0 = 顶层）。按最后一次位置变化缓存 —— 求值入口把它
    // 作为显式作用域参数，不依赖存储层的「当前帧」。
    [[nodiscard]] eravar::ScopeId executionScopeId() const;

    // 段身份自检：解析线程（parseOneFileReadOnly）写进 logical line 的 scopeKey
    // 与 loadScript 按 (脚本名, 段起点) 重算的值不一致的累计条数。必须是 0；
    // 新测试 test_segment_scope 直接断言它，作为「两侧同值」的可执行证据。
    [[nodiscard]] quint64 segmentIdentityMismatches() const { return m_segmentIdentityMismatches; }

    // 上一次位置变化是不是「跳转」（相对顺序推进）。用于区分
    // 「顺序落入函数标签」（= 函数结束）与「跳转/调用到函数标签」（= 跳过标签）。
    // 对齐 C# runScriptProc 每轮先 ShiftNextLine 再判断的语义。
    [[nodiscard]] bool jumpedToCurrent() const { return m_position.jumped; }

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
    // @函数标签解析（callLabel / jumpLabel / hasCallableLabel 的共用实现）：
    // 当前脚本优先（且必须是 FunctionLabel），否则查唯一标签索引（m_labelIndex）
    // 里该名字的**全序第一条** —— 跨脚本重名的择一规则只在 position::firstLabel
    // 一处（脚本名 QString::compare 最小，同脚本取行号最小）。
    // 旧实现是 for(m_scripts) 全量扫描 + 每脚本一次 label.toUpper() ——
    // 1500+ 脚本的语料上每次跨文件 CALL/式中调用都要 ~0.7ms。
    [[nodiscard]] bool resolveCallableLabel(const QString& label, QString& targetScript,
                                             int& targetLine) const;
    // 函数私有变量初值（#DIM X = 7）——进入函数时由执行侧调用
    void applyPrivateVariableDefaults(const QString& scopeKey);
    // 每函数的形参名集合（大写，按需构建/缓存）。「变量分词」的形参判定用：
    // 形参的值绑定（Parameter）/ ARG 别名 / REF 引用绑定都是运行期语义，
    // 命中形参名的变量引用无法静态定键 -> AccessKind::Dynamic。
    // 装载期解析（parseScopedExpression）与收尾定型（applyVariableTypes）
    // **共用同一张表** —— 收尾的赋值 AST 重建在定型趟之后跑，重建出的节点
    // 不再回定型；若装载期不带形参表，重建节点会带上「私有数组」的错描述符。
    const QSet<QString>* paramNamesOf(const QString& scopeKey) const;
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
    [[nodiscard]] const QList<PrivateDefaultEntry>& privateDefaultPlan(const QString& scopeKey);

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
                                  const QString& scopeKey, bool isHeaderFile);
    // 文本版（预扫描「先登记 #DIM/#DIMS 再构造 AST」用）：直接按声明文本登记。
    // scopeKey 为空 = 广域声明；非空 = 该段身份的私有域（唯一键）。
    void parseVariableDeclarationText(const QString& rawLine, const ScriptPosition& position,
                                      const QString& currentFunction, const QString& scopeKey,
                                      bool isHeaderFile);

    // ---- 收尾写步骤（1/2/3 号 pass）只有自由函数实现 ----------------------
    // 实现已整体搬到 load_passes.cpp（自由函数，输入是可写的 LoadContext）；
    // kLoadPasses 直接指向自由函数，类内不再有 1/2/3 的成员入口。
    // 唯一保留的成员入口是 resolveGlobalDeclarations —— 装载期预登记结算
    // （finishDeclarationPreRegistration）在收尾开始前就要用它。
    void resolveGlobalDeclarations();

public:
    // ---- 只读检查步骤（可单独调用）---------------------------------------
    // 下列三步只读：不改变运行行为、不改 AST 语义，只读取已定型的数据并产出
    // 诊断。要求阶段：Bound / Finalized（即声明定稿 + 绑定完成之后）。
    // **每次运行拥有自己的 origin 切片**：驱动端先把本次诊断收进一个全新
    // sink，再 replaceOrigin 整体换掉上一切片 —— 重复调用不重复产出，
    // 调用方不必数次数/去重。阶段不满足时 qWarning + no-op（不产出诊断）。
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
    // 实现 = runCheckNotCalledFunctions（pass 表条目，origin=Pass7）。

    // 收尾 pass 表条目（定义见 .cpp 的 EraParseTable::kLoadPasses）。
    // 表的顺序就是执行顺序；日志文案也来自这里，新增步骤只改表。
    // 门控是**数据**（Gate），不是「读某个 bool 的 if」：markLoadComplete 里
    // 只有一处按 Gate 求值，增删开关不必再改驱动逻辑。
    // origin 也是**数据**：切片身份 = pass 编号（1..7），驱动把每一步的产出
    // 整体替换到自己的 origin 切片里 —— 门控关闭时替换为空，旧切片不残留。
    enum class LoadPassGate : quint8 { Always, NotCalledWarning };
    struct LoadPass {
        const char* label;                        // 日志步骤名（含 "N/6" 编号）
        // 1/2/3 号步骤：吃 LoadContext 的自由函数（实现已搬到 load_passes.cpp）。
        // 第二个参数是**本次运行的独立 sink**（不放进 LoadContext：视图会被
        // installPrivateScopes 之类逃逸的 lambda 按值捕获，窄生命周期引用会悬空）。
        void (*fn)(loadpass::LoadContext&, ParseDiagnostics&);
        // 4/5/6/7 号步骤的成员入口（私有 run* 形态：只写调用方给的 sink，
        // 不自己做 replaceOrigin —— 盖章与切片替换只在驱动里发生一次）。
        // fn 与 memberFn 恰有一个非空，由 markLoadComplete 的那一处 if 分派。
        void (EraParseTable::*memberFn)(ParseDiagnostics&) const;
        DiagOrigin origin;                        // 该步的诊断切片身份
        LoadPassGate gate;                        // 受哪个开关门控（数据）
        LoadPhase phaseAfter;                     // 本步完成后允许进入的阶段（数据）
    };
    static const LoadPass kLoadPasses[7];
    [[nodiscard]] bool isGateOpen(LoadPassGate gate) const;
    // 只读步骤的统一守卫：阶段不符 -> qWarning + false（调用方 no-op）；
    // 阶段符合 -> true。不记录运行次数：每个 pass 的诊断按 origin 切片替换，
    // 重跑幂等由 ParseDiagnostics 的数据结构保证，不靠调用方纪律。
    [[nodiscard]] bool readOnlyPassAllowed(const char* entryName) const;
    // pass 表的 4/5/6/7 成员实现（只写 sink；origin 由驱动统一盖）。
    void runResolveStaticTargets(ParseDiagnostics& sink) const;
    void runCheckStructures(ParseDiagnostics& sink) const;
    void runCheckStaticValues(ParseDiagnostics& sink) const;
    void runCheckNotCalledFunctions(ParseDiagnostics& sink) const;
    // 只读收尾步骤（4/5/6 号 pass）的视图构造：只做指针打包，不读取内容。
    // 三个步骤的实现是 parse_checks.cpp 里的自由函数，public 方法只做转发。
    [[nodiscard]] parsecheck::ParseIrView makeIrView() const;

    // 收尾写步骤（1/2/3 号 pass）的视图构造：只打包引用/指针，不读取内容。
    // 三个步骤的实现是 load_passes.cpp 里的自由函数，成员入口只做转发。
    [[nodiscard]] loadpass::LoadContext makeLoadContext();

    // 位置区转移（实现全部在 position_state.cpp 的自由函数里）。
    // 位置区的唯一成员状态是 m_position；本类不再有 setCurrentLineInternal /
    // pushFrame / popFrame / lineCountFor —— 它们已经是 position:: 的基元。

    // 位置区视图构造：只打包两个指针，不读取内容（与 makeIrView 同形）。
    [[nodiscard]] ScriptView makeScriptView() const;

    // Effects -> 信号。顺序在本函数里**写死**（见 position_state.h 的契约）：
    // callStackChanged -> memorySpaceChanged -> positionChanged -> jumpRequested。
    // 载荷全部取自转移**之后**的状态（旧实现 emit 时读的也是同一批值）。
    void emitEffects(const PositionEffects& fx, const QString& jumpLabel = QString());

    // ---- 用户函数表（m_functions）的装载期维护 --------------------------
    // 唯一写入口是 loadScript：先按脚本名精确删掉旧贡献（返回被删掉的函数名），
    // 扫描新行时重建本脚本的贡献，再对「旧 ∪ 新」的每个名字重算获胜者。
    // 查询端（userFunction / userFunctions / functionExistsKind / 收尾 pass）
    // 只读 m_functions，不在这里做任何按行号/按名字的再解析。
    QSet<QString> dropScriptFunctionContribution(const QString& scriptName);
    void refreshUserFunction(const QString& upperName);
    // 收尾步骤 1 回填类型后，把获胜声明同步回按脚本的贡献副本（见 .cpp）。
    void syncFunctionContributionsFromWinners();

    // 装载阶段（唯一真相；之前的 m_finalized 只是它的一个派生问题）。
    LoadPhase m_phase = LoadPhase::Empty;
    // 未被调用的函数告警开关（对齐 C# Config.FunctionNotCalledWarning；默认 IGNORE）
    bool m_functionNotCalledWarning = false;
    bool m_constProvidersReady = false;   // 常量/维数求值器是否已设置
    bool m_declarationsPreRegistered = false;   // 声明已由装载器预登记（loadScript 不再重复登记）
    // 声明已定稿（resolveGlobalDeclarations 的 resolveDimensions 跑过）：
    // 私有名表安装（installPrivateScopes）的前置条件。
    bool m_declarationsResolved = false;
    QHash<QString, ScriptData> m_scripts;
    // 用户函数的**当前投影**（大写函数名 -> 获胜声明）：查询端唯一读的口径。
    // 内容完全由 m_functionDeclsByScript 派生，见 refreshUserFunction()。
    QHash<QString, UserFunctionInfo> m_functions;
    // 用户函数的**装载期贡献**：脚本 -> (大写函数名 -> 该脚本内第一份声明)。
    // 同名脚本被替换时整份贡献按脚本名精确删除（不是 append-only）；同一脚本内
    // 同名多段只留行号最小的一份（与标签索引 first-wins 同）。
    QHash<QString, QHash<QString, UserFunctionInfo>> m_functionDeclsByScript;
    // 反查：大写函数名 -> 贡献它的脚本集合。跨脚本重名时用它 O(重名脚本数)
    // 选出获胜者，不必为了一个名字扫描全部脚本。
    QHash<QString, QSet<QString>> m_functionScriptsByName;
    mutable QHash<QString, QSet<QString>> m_paramNamesBySegment;   // 段身份文本 -> 形参名集合（clear 时失效）
    // 每函数的私有初值调用计划（loadScript/clear 时失效：新脚本可能带来新私有声明）
    QHash<QString, QList<PrivateDefaultEntry>> m_privateDefaultPlans;
    // **唯一**的标签索引：裸标签名（大写）-> 全部声明（@ 与 $ 都收），
    // 全序由 labelRefLess 定（脚本名 QString::compare -> 行号）。
    // 条目只在 loadScript 的标签登记处追加；顺序只在 finalizeLabelIndex 固化。
    // CALL/JUMP 解析、GOTO 解析、入口点挑选、hasLabel 全查这一张表。
    QHash<QString, QList<LabelRef>> m_labelIndex;
    ParseDiagnostics m_diagnostics;
    const ConstantTable* m_constantTable = nullptr;
    VariableTable m_variables;

    QString m_entryPoint;
    ProcessState* m_state;
    ExecutionEngine* m_executionEngine;
    VariableStorage* m_variableStorage = nullptr;
    GameBaseData* m_gameBaseData = nullptr;   // GAMEBASE_* 求值（条件表达式）
    ExpressionEvaluator* m_evaluator = nullptr;

    // 位置区：执行位置 + 调用栈。纯数据；算法全在 position_state.h。
    // mutable 的唯一原因：executionScopeId() 是 const，但要更新其中的纯缓存
    //（scopeIdName / scopeId / scopeIdValid，与搬到 PositionState 之前一样是缓存，
    // 不改变任何可观察的执行位置语义）。
    mutable PositionState m_position;
    quint64 m_segmentIdentityMismatches = 0;
};

#endif // ERA_PARSE_TABLE_H
