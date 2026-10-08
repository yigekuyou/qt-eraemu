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
//   只读区 : m_scripts（ScriptData）+ m_astCache（表达式串 -> 不可变 AST）
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
    // 调用后脚本表为空，需重新 loadScript + finalizeParse 才能执行。
    void clear();

    [[nodiscard]] QString scriptPath(const QString& scriptName) const;
    // 是否存在某标签（跨全部脚本）
    [[nodiscard]] bool hasLabel(const QString& label) const;

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

    // ---- 只读区查询 ----
    [[nodiscard]] const ScriptData* script(const QString& scriptName) const;
    [[nodiscard]] QStringList scriptNames() const { return m_scripts.keys(); }
    [[nodiscard]] QSharedPointer<ExpressionNode> expressionAst(const QString& expr, bool quiet = false);
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

    // 全部脚本装载完成后调用一次：重算维数 + 把变量类型回填到所有缓存 AST。
    // （装载中只做增量，避免 O(脚本数 × AST 总数)）
    void finalizeParse();

    // 并行装载 merge：把 worker 的本地表达式缓存并入主缓存（first-wins）
    void mergeAstCache(const QHash<QString, QSharedPointer<ExpressionNode>>& localCache);

    // 用缓存 AST 求值条件；返回 false 表示求值失败。
    bool evaluateCondition(const QString& scriptName, int line, bool& out, int argIndex = -1);
    bool evaluateAst(const QSharedPointer<ExpressionNode>& ast, bool& out);
    bool evaluateExpression(const QString& expr, bool& out);

    // ---- 位置区查询 ----
    // perf：返回引用而非按值拷贝（stepOnce/executeLine 每条指令都调用，
    // 按值返回会带来 QString 原子引用计数 + 拷贝；调用方均为立即使用或自行拷贝）。
    [[nodiscard]] const QString& currentScript() const;
    [[nodiscard]] int currentLine() const;
    [[nodiscard]] int depth() const;
    [[nodiscard]] const QList<Frame>& callStack() const;
    [[nodiscard]] Frame currentFrame() const;
    [[nodiscard]] bool hasPosition() const;

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
    // 函数私有变量初值（#DIM X = 7）——进入函数时由执行侧调用
    void applyPrivateVariableDefaults(const QString& function);
    bool returnFromCall();
    void switchToMemorySpace(const QString& scriptName);

private:
    void buildJumpMarkings(const QString& scriptName);

    // 变量声明解析（#DIM/#DIMS/#GLOBAL/#GLOBALS/#PRIVATE）与类型回填
    void parseVariableDeclaration(const LogicalLine& line, const QString& currentFunction,
                                  bool isHeaderFile);
    void applyVariableTypes();
    // 字符串赋值的右值改按 StrForm 解析（对齐 C# AnalyseFormattedString）
    void applyStringAssignments();
    // #DIM/#DIMS 初值：全局装载时写入
    void applyVariableDefaults(const QList<VariableDecl>& decls);
    void applyGlobalVariableDefaults();
    // 用户自定义函数的强类型化：形参类型由变量表回填（#DIM/#DIMS + ARG/ARGS），
    // 返回类型由 #FUNCTION(S) 决定。必须在 finalizeParse 内、变量声明解析之后。
    void resolveUserFunctionTypes();
    // 调用点类型校验（CALL 语句 + 式中调用）
    QString checkUserCallArgs(const UserFunctionDecl& decl,
                              const QList<OperandType>& argTypes) const;
    // 参数个数/类型校验（finalizeParse 内、类型回填之后调用）
    void validateArguments();
    // 函数调用重绑 + 校验（finalizeParse 内、类型回填之后调用）：
    // 把每个 FunctionNode 重新解析为 内置函数 / 用户自定义函数 / 未定义函数，
    // 并用最终（类型回填后）的实参 AST 复刻 C# FunctionMethod.CheckArgumentType()。
    void resolveFunctionNodes();
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
    QHash<QString, QSharedPointer<ExpressionNode>> m_scopedAstCache;
    QHash<QString, ScriptData> m_scripts;
    // perf：hasLabel 的结果按标签名记忆（此前每次 TRYCALL/TRYCALLFORM 都遍历
    // 全部脚本的 labelPositions；eraTW 地图逐字符 TRYCALLFORM 时是热点）。
    // 装载期每插入一个脚本即失效重建。
    mutable QHash<QString, bool> m_hasLabelCache;
    QHash<QString, QSharedPointer<ExpressionNode>> m_astCache;
    QHash<QString, UserFunctionInfo> m_functions;
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
