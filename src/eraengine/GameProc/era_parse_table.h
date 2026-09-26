#ifndef ERA_PARSE_TABLE_H
#define ERA_PARSE_TABLE_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QList>
#include <QQueue>
#include <QMetaType>
#include "script_line.h"
#include "execution/memory_block.h"
#include "execution/script_memory_space.h"

// Forward declarations
class ProcessState;
class ExecutionEngine;

// ---------------------------------------------------------------------------
// 调用帧 (Call frame)
//
// CALL 时压栈、RETURN 时弹栈。一个帧只记录“如何回到调用者”，不持有任何
// 指针 / 引用 / 自有资源，因此是纯值类型：拷贝、移动、放进 QList 都安全，
// 所有权一目了然（栈拥有所有帧）。
//
//   script     —— 调用发生时所处的脚本；被调函数返回后回到此脚本
//   returnLine —— 返回地址：CALL 所在行的下一行（PC 语义）
//   callLabel  —— 被调用的 label 名（调试 / 回溯用）
//
// 对应官方 C# 的 CalledFunction.returnAddress（Emuera/GameProc/
// Process.CalledFunction.cs）与 Process.functionList 栈。
// ---------------------------------------------------------------------------
struct Frame {
    QString script;
    int     returnLine = 0;
    QString callLabel;

    Frame() = default;
    Frame(const QString& s, int line, const QString& label)
        : script(s), returnLine(line), callLabel(label) {}

    bool operator==(const Frame& other) const {
        return script == other.script
            && returnLine == other.returnLine
            && callLabel == other.callLabel;
    }
};
Q_DECLARE_METATYPE(Frame)

// ---------------------------------------------------------------------------
// EraParseTable —— 内存运行时容器
//
// 目标结构分三区：
//   1. 只读区  : m_parsedScripts / m_labelPositions / m_executionQueues 以及
//                (计划中的) m_astCache。启动阶段一次填充，之后只读。
//   2. 位置区  : m_currentScript / m_currentLine(PC) / m_callStack ——
//                本文件本次实现的重点，运行时可变，体积很小。
//   3. 标记区  : (计划中) ScriptData 内的 endifLines 等 O(1) 跳转标记。
//
// 线程安全：
//   - 只读区在启动（加载）阶段由 loader 线程填充；若 loader 使用
//     QtConcurrent 并行解析，填充阶段需要 QMutex 保护，填充完成后只读，
//     运行期无需加锁。
//   - 位置区只由“持有本对象的执行线程”读写（与 QObject 亲和线程一致），
//     因此不需要互斥量；跨线程访问必须经由 Qt 信号/槽（QueuedConnection）
//     编组到本对象的线程。
// ---------------------------------------------------------------------------
class EraParseTable : public QObject {
    Q_OBJECT

public:
    explicit EraParseTable(ProcessState* state, QObject* parent = nullptr);
    // Constructor with ExecutionEngine pointer
    explicit EraParseTable(ProcessState* state, ExecutionEngine* execEngine, QObject* parent = nullptr);
    ~EraParseTable();

    // Load scripts into parse table (只读区填充)
    bool loadScript(const QString& scriptName, const QList<LogicalLine>& lines);
    bool loadDirectory(const QString& dirPath, int depth = 0);

    // Set entry point for execution
    void setEntryPoint(const QString& label);

    // Get entry point
    QString getEntryPoint() const;

    // Get execution queue for current script
    QList<LogicalLine> getExecutionQueue() const;

    // Get label position in script
    int getLabelPosition(const QString& scriptName, const QString& labelName) const;

    // Resolve jump target (returns position in current script)
    bool resolveJumpTarget(const QString& label, int& position);

    // Resolve jump to different script
    bool resolveJumpToScript(const QString& scriptName, const QString& label, int& position);

    // Get current script name
    QString getCurrentScript() const;

    // Get memory space for current script
    ScriptMemorySpace* getCurrentMemorySpace() const;

    // Get memory space manager
    MemorySpaceManager* getMemorySpaceManager() const;

    // Set variable storage for condition evaluation
    void setVariableStorage(VariableStorage* storage);

    // =======================================================================
    // 位置区 (Position region) —— 查询
    // =======================================================================

    // 当前脚本（等价于 getCurrentScript()，语义更清晰的新名字）
    [[nodiscard]] QString currentScript() const;

    // 程序计数器：当前脚本内的逻辑行号（0 起）
    [[nodiscard]] int currentLine() const;

    // 调用栈深度，恒等于 callStack().size()
    [[nodiscard]] int depth() const;

    // 调用栈（自栈底到栈顶）
    [[nodiscard]] const QList<Frame>& callStack() const;

    // 栈顶帧；空栈时返回默认 Frame
    [[nodiscard]] Frame currentFrame() const;

    // 是否存在有效执行位置（脚本已确定）
    [[nodiscard]] bool hasPosition() const;

signals:
    // Entry point signal - sent when execution starts
    void entryPointReached(const QString& label);

    // Jump request signal - sent when jump occurs
    void jumpRequested(const QString& targetScript, const QString& label, int targetPosition);

    // State check signal - sent to check if state changed
    void checkState();

    // Parse completed signal
    void parseCompleted(const QString& scriptName);

    // Memory space changed signal - sent when switching to different script
    void memorySpaceChanged(const QString& scriptName);

    // 位置区变化信号：PC 或当前脚本发生改变
    void positionChanged(const QString& script, int line);

    // 位置区变化信号：调用栈深度发生改变（CALL/RETURN）
    void callStackChanged(int depth);

public slots:
    // =======================================================================
    // 位置区 (Position region) —— 变换
    // 这些槽是位置区唯一被允许的入口，保证 PC / 调用栈 / 深度始终自洽。
    // =======================================================================

    // 清空调用栈并把 PC 归零（保留当前脚本）
    void resetPosition();

    // 绝对定位到 (script, line)；script 与当前不同则切换脚本并发出信号
    void setPosition(const QString& script, int line);

    // 顺序前进一行（PC++）——“取出即前进”的显式表达
    void advance();

    // 脚本内跳转；line 越界返回 false（GOTO 的底座）
    bool jumpToLine(int line);

    // 按 label 解析并跳转（可跨脚本），不压栈（GOTO / 入口语义）
    bool jumpToLabel(const QString& label);

    // 压入调用帧并跳转到 label（CALL 语义），返回地址 = 当前行 + 1
    // If advanceWasCalled is true, currentLine already points to next instruction
    bool callLabel(const QString& label, bool advanceWasCalled = false);

    // 弹出调用帧并恢复到返回地址（RETURN 语义）；空栈返回 false
    bool returnFromCall();

    // Handle execution result from execution side
    void onExecutionResult(const QString& scriptName, int lineNumber, bool success);

    // Handle jump from execution side
    void onJumpRequest(const QString& label);

    // Handle jump to different script
    void onJumpToScript(const QString& scriptName, const QString& label);

    // Handle state change from ProcessState
    void onStateChange();

    // Handle state unchanged signal from ProcessState
    void onStateUnchanged();

    // Handle state changed signal from ProcessState
    void onStateChanged();

    // Handle request for next instruction
    void onRequestNextInstruction();

    // Handle execution completion
    void onExecutionComplete(const QString& scriptName);

    // Execute instructions until state changes or queue is empty
    bool pumpInstructions();

    // Start the execution pump (called after entry point is set)
    void startExecutionPump();

    // Switch to different memory space (public for testing)
    void switchToMemorySpace(const QString& scriptName);

private:
    // Build execution queue for a script
    void buildExecutionQueue(const QString& scriptName);

    // Build memory space tree for a script
    void buildMemorySpaceTree(const QString& scriptName);

    // Get or create memory space for script
    ScriptMemorySpace* getOrCreateMemorySpace(const QString& scriptName);

    // Reconstruct operand from instruction arguments
    QString reconstructOperand(const ScriptLine& scriptLine) const;

    // Reconstruct full command text from instruction data
    QString reconstructCommand(const ScriptLine& scriptLine) const;

    // -----------------------------------------------------------------------
    // 位置区内部辅助
    // -----------------------------------------------------------------------

    // 唯一的 PC 写入点；line 为负时截断为 0。
    // forceEmit=false 时仅在值变化时发出 positionChanged。
    void setCurrentLineInternal(int line, bool forceEmit = false);

    // 压栈 / 弹栈，并同步 m_depth + 发出 callStackChanged
    void pushFrame(const Frame& frame);
    Frame popFrame();

    // 获取当前执行索引（用于记录 CALL 时的返回地址）
    int getCurrentExecutionIndex() const;

    // 记录 CALL 指令的位置（在 advance 之前），用于正确计算返回地址
    void recordCallPosition();

    // 某脚本的逻辑行数（0 表示未知）
    int lineCountFor(const QString& script) const;

    // Parse table data (只读区)
    QHash<QString, QList<LogicalLine>> m_parsedScripts;
    // Execution queues - one per script, stores remaining instructions to execute
    QHash<QString, QList<LogicalLine>> m_executionQueues;
    QHash<QString, int> m_executionQueueIndices;  // Current index in each script's queue
    QHash<QString, QHash<QString, int>> m_labelPositions;

    // Entry point
    QString m_entryPoint;

    // Process state (for state checking)
    ProcessState* m_state;

    // Memory space manager
    MemorySpaceManager* m_memorySpaceManager;

    // Current memory space
    ScriptMemorySpace* m_currentMemorySpace;

    // Execution engine (for instruction execution)
    ExecutionEngine* m_executionEngine;

    // =======================================================================
    // 位置区 (Position region) —— 运行时可变，很小
    // 仅由持有本对象的执行线程访问，无需加锁。
    // =======================================================================
    QString       m_currentScript;   // 当前脚本名
    int           m_currentLine;     // PC（程序计数器）
    QList<Frame>  m_callStack;       // 调用栈
    int           m_depth;           // == m_callStack.size()
};

#endif // ERA_PARSE_TABLE_H
