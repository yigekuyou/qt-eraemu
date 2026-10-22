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
#ifndef ERB_LOADER_H
#define ERB_LOADER_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QList>
#include <QFile>
#include <QTextStream>
#include <QPair>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QTimer>
#include <atomic>
#include <memory>
#include "text_encoding.h"
#include "ast/logical_line.h"
#include "ast/ast_builder.h"
#include "ast/operand_type.h"
#include "erb_preprocessor.h"
#include "ast/parse_diagnostic.h"

class EraParseTable;

// ---------------------------------------------------------------------------
// 一个 ERB 文件解析结果（可在线程中独立产出，随后在主线程合并）
// ---------------------------------------------------------------------------
struct ParsedErbFile {
    QString scriptName;
    QString path;
    QList<LogicalLine> lines;
    QStringList warnings;                                       // 预处理层面告警
    ParseDiagnostics diagnostics;                               // 结构化诊断（分级+代码+位置）
};

// ---------------------------------------------------------------------------
// 装载期可见的「用户变量类型」快照（预扫描产出，解析线程只读）
//
// 定位：**兜底的类型表**。#DIM/#DIMS 的权威登记是「先登记」阶段
// （ErbLoader::preRegisterDeclarations → EraParseTable），解析线程查询时
// 优先查已登记的变量表（resolveExpr / parseOneFile 的 declaredType），
// 只有查不到（例如未预登记的调用路径）才退回本表。
//
// 只带**类型**：维数 / 初值 / const / CHARADATA 等不在表内 —— 那些由
// loadScript 登记进变量表，markLoadComplete 收尾时就地定型到 AST 节点上。
// ---------------------------------------------------------------------------
struct LoadTimeVarTypes {
    QHash<QString, OperandType> globalVars;                 // 变量名(大写) -> 类型
    // 段身份文本（eravar::segmentKeyText）-> 变量名(大写) -> 类型。
    // **不是函数名**：同名 @F 出现在两份脚本/两个段里时，私有类型互不覆盖。
    QHash<QString, QHash<QString, OperandType>> localVars;

    // 作用域查询：段私有优先，其次广域（与 VariableTable::typeOf 同序）
    [[nodiscard]] OperandType typeOf(const QString& name, const QString& scopeKey) const;
};

// ERB 脚本装载器
//
// 现在直接产出「完整 AST」：读文件 -> 逐行 AstBuilder::build -> QList<LogicalLine>。
// 表达式归约复用 AST 缓存；**支持并行装载**：
//   预扫描(函数返回类型 + 用户变量类型) -> 分块并行解析(线程内私有缓存)
//   -> 主线程按序合并 -> markLoadComplete。
class ConstantTable;

class ErbLoader : public QObject {
    Q_OBJECT

public:
    explicit ErbLoader(QObject* parent = nullptr);
    ~ErbLoader() override;

    // Load ERB files
    // root：脚本名的基准目录（脚本名 = 相对 root 的路径，对齐 C# Config.GetFiles）。
    // 缺省时回落到「文件名」——仅供单文件装载使用。
    bool loadFile(const QString& filePath, const QString& root = QString());
    bool loadDirectory(const QString& dirPath, int depth = 0);

    // ---- 异步装载（分块：后台解析 -> 主线程合并，不阻塞 UI）----
    // 完成/进度通过 loadProgress / loadCompleted 信号上报；最终仍需调用方
    // 在 loadCompleted 槽里执行 EraParseTable::markLoadComplete()（声明定稿 + 全体脚本就地定型/重绑 + 诊断）。
    void loadDirectoryAsync(const QString& dirPath, int depth = 0);
    [[nodiscard]] bool isLoading() const { return m_async.active; }
    void cancelLoad();
    // 停止后台任务并等待其退出；调用后释放 loader 持有的脚本/拍平结果。
    void cancelAndWait();

    // ---- 装载阶段（诊断心跳 / UI 阶段文案）----
    //
    // 异步装载是一条「后台读+解析 → 主线程登记/合并」的长流水线：eraTW 在
    // 桌面 12 线程上也要 ~15s，在 Android SAF（每个文件都是一次
    // DocumentsContract 往返 + ContentResolver 读取）上是数分钟量级。此前
    // 只有阶段边界的几条日志，中间窗口完全静默 —— 真机上表现为「卡在正在
    // 装载脚本、logcat 再无输出」，无法判断是慢还是死。
    //
    // 这里把阶段 + 计数器做成**原子量**（后台线程只写，无锁无分配），由主线程
    // 的心跳定时器统一读取、打印与上报：日志与进度因此天然单调、限频，且不会
    // 出现多线程交叉写日志。
    enum LoadStage {
        StageIdle = 0,   // 未装载
        StageEnumerate,  // 枚举 ERB/ERH 文件（含子目录递归）
        StageRead,       // 读取 + 解码 + 函数/变量类型预扫描（后台并行）
        StageRegister,   // 登记 #DIM/#DIMS 声明并结算（主线程）
        StageParse,      // 后台分块解析（QtConcurrent）
        StageMerge,      // 主线程把解析结果并入解析表
    };
    [[nodiscard]] static QString loadStageName(int stage);
    // 上报给 UI 的「粗阶段名」：分块解析与合并是同一段流水线的两半（每块解析完
    // 立刻合并，1Hz 采样会来回跳 —— UI 文案不能跟着闪），故归为同一个名字。
    [[nodiscard]] static QString loadStageGroupName(int stage);
    [[nodiscard]] int loadStage() const { return m_stage.load(std::memory_order_relaxed); }
    [[nodiscard]] int loadStageDone() const { return m_stageDone.load(std::memory_order_relaxed); }
    [[nodiscard]] int loadStageTotal() const { return m_stageTotal.load(std::memory_order_relaxed); }
    [[nodiscard]] int readFailureCount() const { return m_readFailures.load(std::memory_order_relaxed); }
    // 心跳周期（ms）。<=0 关闭心跳（单测用；正常路径默认 1000ms）。
    void setHeartbeatInterval(int ms) { m_heartbeatMs = ms > 0 ? ms : 0; }
    [[nodiscard]] int heartbeatInterval() const { return m_heartbeatMs; }
    // 「阶段/进度多久没变化」判定为停滞的阈值（ms）：心跳日志与 UI 提示共用。
    static constexpr qint64 kStallMs = 10000;

    // 并行装载开关与线程数（默认开启；线程数 <=0 表示用 QThreadPool 默认）
    void setParallelLoad(bool on) { m_parallel = on; }
    [[nodiscard]] bool parallelLoad() const { return m_parallel; }
    void setMaxThreads(int n) { m_maxThreads = n; }
    void setChunkSize(int n) { m_chunkSize = n > 0 ? n : kDefaultChunkSize; }

    // 逐文件 parseStarted/parseFinished 信号开关（默认关）。
    // perf：eraTW 有 2200+ 文件 → 每次装载 ~4500 次信号发射 + 接收端 qDebug，
    // 全部发生在**主线程合并**阶段，是合并耗时的一部分；这两个信号当前只被
    // 诊断日志消费（进度改用 loadProgress），故默认关闭，需要时再打开。
    void setEmitParseSignals(bool on) { m_emitParseSignals = on; }
    [[nodiscard]] bool emitParseSignals() const { return m_emitParseSignals; }

    // 文本读取编码（Auto = 逐文件嗅探；可强制 UTF-8 / Shift-JIS）
    void setReadEncoding(TextEncoding enc) { m_readEncoding = enc; }
    [[nodiscard]] TextEncoding readEncoding() const { return m_readEncoding; }

    // CSV 常量名表（解析期把「常量名下标」识别为字符串，对齐 C# ConstantData）
    void setConstantTable(const ConstantTable* table) { m_constantTable = table; }

    // ---- 预处理配置（_Rename.csv / #DEFINE 宏 / 调试模式）----
    void setRenameMap(const ErbPreprocessor::RenameMap& map) { m_preprocessor.setRenameMap(map); }
    // 从 CSV 目录加载 _Rename.csv（对齐 C# ParserMediator.LoadEraExRenameFile）
    bool loadRenameFile(const QString& filePath);
    void addMacros(const QSet<QString>& macros) {
        QSet<QString> all = m_preprocessor.macros();
        all.unite(macros);
        m_preprocessor.setMacros(all);
    }
    void setDebugMode(bool on) { m_preprocessor.setDebugMode(on); }
    [[nodiscard]] const ErbPreprocessor& preprocessor() const { return m_preprocessor; }

    // Set parse table for AST cache + notifications
    void setParseTable(EraParseTable* parseTable) { m_parseTable = parseTable; }

    // Access loaded AST lines
    QHash<QString, QList<LogicalLine>> getLoadedScripts() const;

    bool scriptExists(const QString& scriptName) const;
    QString resolveScriptName(const QString& scriptName) const;
    QHash<QString, QList<LogicalLine>> getLoadedScriptsCI() const;

    QList<LogicalLine> getLogicalLines(const QString& scriptName);
    QList<LogicalLine> getLogicalLinesCI(const QString& scriptName);

    int getScriptLineCount(const QString& scriptName);
    QString getScriptPath(const QString& scriptName) const;
    // Number of non-empty ERB/ERH files merged into the parse table.
    [[nodiscard]] int loadedFileCount() const { return m_scriptPaths.size(); }

signals:
    void parseStarted(const QString& scriptName);
    void parseLineReady(const QString& scriptName, int lineNumber, const QString& lineContent);
    void parseFinished(const QString& scriptName);
    void parseError(const QString& errorMessage);

    // 异步装载进度（processed / total 文件数）与完成
    void loadProgress(int processed, int total);
    // 装载心跳快照（主线程每秒一次，驱动 UI 的「正在装载」显示）：
    //   stage/name  = 当前阶段（LoadStage / 中文名，见 loadStageGroupName）
    //   done/total  = 该阶段已处理/总数（total<=0 表示总数未知）
    //   elapsedMs   = 本轮装载已耗时
    //   stalled     = done/stage 超过 kStallMs 没变化（慢还是死，UI 上就能分辨）
    // UI 不再自己拼「进度 + 阶段」，一律绑这个快照（EraEngine 的 loadStage/… 属性）。
    void loadHeartbeat(int stage, const QString& name, int done, int total,
                       qint64 elapsedMs, bool stalled);
    void loadCompleted(bool ok);

private:
    using FunctionTypes = QHash<QString, OperandType>;   // 函数名(大写) -> 返回类型

    // 装载准备（后台线程）：目录列举 + _Rename + 宏表 + 函数返回类型预扫描
    struct LoadPrep {
        QStringList files;      // 排序后的全部 .erb/.erh（头文件在前）
        QHash<QString, QString> renames;
        QSet<QString> macros;
        ErbPreprocessor::MacroTable macroTable;
        FunctionTypes functionTypes;
        LoadTimeVarTypes varTypes;   // 用户变量类型（解析阶段要用的「提前量」）
        // 预扫描时**一次读取并解码**的文件内容（path -> 文本）。
        // 解析阶段直接复用，避免「预扫描读一遍、解析再读一遍」的重复 解码 +
        // 每文件编码嗅探（eraTW 约 93MB × 2）。解析按块消费后即从 map 移除，
        // 峰值内存 ≈ 全部源文本（解析开始后随块释放）。
        QHash<QString, QString> decoded;
        bool ok = false;
    };
    LoadPrep prepareLoad(const QString& dirPath, int depth) const;
    // 主线程合并一批解析结果
    bool mergeChunk(QList<ParsedErbFile>&& parsed);
    // 主线程合并「一小批」（见 kMergeBatch）：把一整块解析结果拆成多次
    // 事件循环往返，单次阻塞从 ~80ms 降到 ~10ms（UI 抖动更平）。
    void mergeStep();

    // 异步装载状态机
    struct AsyncState {
        bool active = false;
        bool cancelled = false;
        bool ok = true;
        int total = 0;
        int parsed = 0;         // 已完成后台解析的文件数（切片游标）
        int merged = 0;         // 已并入解析表的文件数（进度用）
        int chunkIndex = 0;     // 已调度的分块序号（诊断日志用）
        int depth = 0;
        QString dirPath;
        QStringList files;      // 已排序（头文件优先），供分块切片
        QHash<QString, QString> decoded;   // path -> 预扫描时已解码的源文本（解析后即释放）
        // 待并入的结果队列（一整块解析完先入队，再由 mergeStep 分批消费）
        QList<ParsedErbFile> pending;
        int pendingPos = 0;
        QElapsedTimer clock;    // 异步装载总耗时（分析用）
        qint64 mergeMs = 0;     // 主线程合并累计（分析用）
        // 阶段耗时（分析用）：后台 prep 完成时刻 / 声明登记耗时 / 分块解析累计
        qint64 prepMs = 0;
        qint64 registerMs = 0;
        qint64 parseMs = 0;
        qint64 chunkStartedMs = 0;   // 当前块开始解析的时刻（clock 口径）
    };
    AsyncState m_async;
    void finishAsync(bool ok);   // 统一收尾：记录耗时 -> 复位 -> 发 loadCompleted
    std::unique_ptr<QFutureWatcher<LoadPrep>> m_prepWatcher;
    std::unique_ptr<QFutureWatcher<ParsedErbFile>> m_chunkWatcher;   // QtConcurrent::mapped 的结果
    QFuture<LoadPrep> m_prepFuture;
    QFuture<ParsedErbFile> m_chunkFuture;
    FunctionTypes m_asyncTypes;
    LoadTimeVarTypes m_asyncVarTypes;   // 预扫描产出；分块解析期间只读
    quint64 m_generation = 0;

private slots:
    void onPrepFinished();
    void onChunkFinished();

private:
    void scheduleNextChunk();

    // ---- 装载心跳（主线程定时器；后台线程只写下面这批原子量）----
    void onHeartbeat();
    // 阶段切换 / 进度推进（后台线程可调用：只写原子量，无锁无分配）。
    void setLoadStage(int stage, int done = -1, int total = -1) const;
    void bumpLoadStage(int n = 1) const;
    void noteReadFailure() const { m_readFailures.fetch_add(1, std::memory_order_relaxed); }
    void startHeartbeat();
    void stopHeartbeat();

    // 递归收集 .erb/.erh 文件（确定性顺序）
    QStringList collectFiles(const QString& dirPath, int depth) const;

    // 供线程内使用：只读函数类型表 + 用户变量类型表（+ 当前函数作用域）
    // quiet=true：解析失败时不打印（用于赋值右值的临时归约，见 AstBuilder::build）
    QSharedPointer<ExpressionNode> resolveExpr(const QString& expr, const FunctionTypes& types,
                                               bool quiet = false,
                                               const LoadTimeVarTypes* varTypes = nullptr,
                                               const QString& scope = QString()) const;

    // 预扫描：收集函数返回类型（#FUNCTION → Int，#FUNCTIONS → Str）
    FunctionTypes scanFunctionTypes(const QString& content) const;

    // 预扫描：收集用户变量类型（#DIM/#DIMS/#GLOBAL/#GLOBALS/#PRIVATE）。
    // 作用域判定与 EraParseTable::loadScript 完全一致：.ERH 的文件级声明是
    // 广域；.ERB 里只有**紧跟函数标签**的 # 行才算声明（否则 loadScript 会
    // 告警并忽略 —— 这里也必须忽略，否则解析期类型会比最终登记的多出来）。
    // scriptName = 脚本名（段身份的一半）；pp = 与后续装载**同一套宏表**的
    // 预处理器（段起点行必须按预处理后的行序计，否则与 loadScript 不一致）。
    void scanVariableTypes(const QString& content, bool isHeaderFile,
                           const QString& scriptName, const ErbPreprocessor& pp,
                           LoadTimeVarTypes& out) const;

    // 「先预处理 → 先登记 #DIM/#DIMS → 再构造 AST」：在并行构建前把声明登记进
    // 解析表并结算（按**预处理后**的行扫描，与构建期所见逐字一致）。
    void preRegisterDeclarations(const QStringList& files,
                                 const QHash<QString, QString>& decoded,
                                 const QSet<QString>& headers,
                                 const QString& root);

    // ---- 并行装载的线程契约（写在类型上，不只是注释）----------------------
    //
    // 并行段 = 「每文件局部收集 + 主线程串行合并」：
    //   * 输入全部是只读快照（下面这个结构里没有可变引用）；
    //   * 输出是 ParsedErbFile 值（每文件独立，线程之间不共享）；
    //   * 共享结构（EraParseTable / m_scripts / 诊断）只在主线程的
    //     mergeParsedFile() 里写。
    //
    // 前置条件（parseOneFileReadOnly 入口断言）：
    //   声明预登记已完成（beginDeclarationPreRegistration 之后）——
    //   并行线程**只查** EraParseTable::variableTable()，绝不插入新声明。
    struct ParallelParseInputs {
        const FunctionTypes* types = nullptr;          // 函数名 -> 返回类型
        const LoadTimeVarTypes* varTypes = nullptr;    // 用户变量类型（可空）
        const QString* content = nullptr;              // 预扫描已解码文本（可空=读盘）
    };
    [[nodiscard]] ParsedErbFile parseOneFileReadOnly(const QString& filePath,
                                                     const ParallelParseInputs& inputs,
                                                     const QString& root) const;

    // 主线程合并（确定性顺序）
    bool mergeParsedFile(ParsedErbFile&& pf);

    // 逐行构建完整 AST（串行路径；会 emit parseLineReady）
    QList<LogicalLine> buildAst(const QString& content, const QString& filePath,
                                const QString& root = QString(),
                                ParseDiagnostics* diagnostics = nullptr);

    QString readFileContent(const QString& filePath) const;
    // 同上，但返回读取是否成功：后台预扫描用它把「读取失败」计数并告警。
    // 读取失败（权限被收回 / SAF 文档失效）此前会静默变成空文本 → 脚本
    // 静默消失；Android 上这是最需要留痕的一类失败。
    QString readFileContent(const QString& filePath, bool* ok) const;

    // 预处理（_Rename 替换 + 行连接 + 预处理指令）-> 物理行一一对应
    QList<ErbSourceLine> prepareLines(const QString& content, const QString& fileName,
                                      QStringList* warnings) const;

    // 在 ERB 目录附近自动探测 CSV/_Rename.csv
    void tryAutoLoadRename(const QString& dirPath);

    ErbPreprocessor m_preprocessor;
    const ConstantTable* m_constantTable = nullptr;
    TextEncoding m_readEncoding = TextEncoding::Auto;

    QHash<QString, QString> m_scriptPaths;
    QHash<QString, QList<LogicalLine>> m_logicalLines;
    QHash<QString, int> m_scriptLineCounts;

    // [qdbug] 验证（eraTW 实测）：并行拍平与顺序拍平结果**完全一致** ——
    // dump_lines 串行/并行各导出 2,075,985 行拍平行，diff 完全相同。
    // 并行合并本身即保序（blockingMapped 结果按输入序返回、主线程按序合并）；
    // 并行解析期的三类不确定性（「未定义」解析 / worker AST 缓存缺失 /
    // 插入顺序）已由 markLoadComplete 的重绑 + labels() 的 (脚本名,行号) 排序 +
    // 懒加载解析兜底。保持并行默认（性能收益），显式 setParallelLoad(false)
    // 可退回串行（dump_lines --parallel 可复核一致性）。
    bool m_parallel = true;
    int  m_maxThreads = 0;
    // 后台解析块大小：解析在后台线程池，块大 → 调度轮次少、吞吐高
    // （eraTW 实测 64/块比 16/块总耗时少 ~2s）。
    static constexpr int kDefaultChunkSize = 64;
    // 主线程合并批大小：一整块（64）解析结果由 mergeStep 分多次并入，
    // 每批后回事件循环 —— 单次主线程阻塞 ≈ kMergeBatch × ~1.3ms/文件
    // （eraTW 8 → ~10ms），远小于整块 80–240ms 的抖动。
    static constexpr int kMergeBatch = 8;
    int  m_chunkSize = kDefaultChunkSize;
    bool m_emitParseSignals = false;   // 逐文件信号默认关闭（见 setEmitParseSignals）
    EraParseTable* m_parseTable;

    // ---- 装载心跳状态（见 LoadStage 的说明）----
    // 后台线程只碰这批 mutable 原子量；主线程的心跳定时器读它们并打印/上报。
    mutable std::atomic<int> m_stage{StageIdle};
    mutable std::atomic<int> m_stageDone{0};
    mutable std::atomic<int> m_stageTotal{0};
    mutable std::atomic<int> m_stageMerged{0};   // 分块流水线里「已并入解析表」的文件数
    mutable std::atomic<int> m_readFailures{0};
    QTimer m_heartbeat;            // 仅异步装载期间运行（主线程）
    int m_heartbeatMs = 1000;      // 上报周期；<=0 关闭
    int m_hbStage = -1;            // 上次上报的阶段（主线程，用于去重）
    int m_hbDone = -1;             // 上次上报的进度
    qint64 m_hbLogMs = 0;          // 上次打印心跳日志的时刻（clock 口径）
    qint64 m_hbChangeMs = 0;       // 上次「阶段或进度发生变化」的时刻（判停滞用）
};

#endif // ERB_LOADER_H
