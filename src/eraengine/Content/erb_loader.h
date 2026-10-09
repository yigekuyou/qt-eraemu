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
    QHash<QString, QSharedPointer<ExpressionNode>> astCache;   // 该文件本地表达式缓存
};

// ERB 脚本装载器
//
// 现在直接产出「完整 AST」：读文件 -> 逐行 AstBuilder::build -> QList<LogicalLine>。
// 表达式归约复用 AST 缓存；**支持并行装载**：
//   预扫描(函数返回类型) -> 分块并行解析(线程内私有缓存) -> 主线程按序合并 -> finalizeParse。
class ConstantTable;

class ErbLoader : public QObject {
    Q_OBJECT

public:
    explicit ErbLoader(QObject* parent = nullptr);

    // Load ERB files
    // root：脚本名的基准目录（脚本名 = 相对 root 的路径，对齐 C# Config.GetFiles）。
    // 缺省时回落到「文件名」——仅供单文件装载使用。
    bool loadFile(const QString& filePath, const QString& root = QString());
    bool loadDirectory(const QString& dirPath, int depth = 0);

    // ---- 异步装载（分块：后台解析 -> 主线程合并，不阻塞 UI）----
    // 完成/进度通过 loadProgress / loadCompleted 信号上报；最终仍需调用方
    // 在 loadCompleted 槽里执行 EraParseTable::finalizeParse()（全量语义阶段）。
    void loadDirectoryAsync(const QString& dirPath, int depth = 0);
    [[nodiscard]] bool isLoading() const { return m_async.active; }
    void cancelLoad();

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
    const LogicalLine* findLabel(const QString& labelName) const;

    bool scriptExists(const QString& scriptName) const;
    QString resolveScriptName(const QString& scriptName) const;
    QHash<QString, QList<LogicalLine>> getLoadedScriptsCI() const;

    QList<LogicalLine> getLogicalLines(const QString& scriptName);
    QList<LogicalLine> getLogicalLinesCI(const QString& scriptName);

    int getLabelPosition(const QString& scriptName, const QString& labelName);
    int getScriptLineCount(const QString& scriptName);
    QString getScriptPath(const QString& scriptName) const;

signals:
    void parseStarted(const QString& scriptName);
    void parseLineReady(const QString& scriptName, int lineNumber, const QString& lineContent);
    void parseFinished(const QString& scriptName);
    void parseError(const QString& errorMessage);

    // 异步装载进度（processed / total 文件数）与完成
    void loadProgress(int processed, int total);
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
        int depth = 0;
        QString dirPath;
        QStringList files;      // 已排序（头文件优先），供分块切片
        QHash<QString, QString> decoded;   // path -> 预扫描时已解码的源文本（解析后即释放）
        // 待并入的结果队列（一整块解析完先入队，再由 mergeStep 分批消费）
        QList<ParsedErbFile> pending;
        int pendingPos = 0;
        QElapsedTimer clock;    // 异步装载总耗时（分析用）
        qint64 mergeMs = 0;     // 主线程合并累计（分析用）
    };
    AsyncState m_async;
    void finishAsync(bool ok);   // 统一收尾：记录耗时 -> 复位 -> 发 loadCompleted
    QFutureWatcher<LoadPrep> m_prepWatcher;
    QFutureWatcher<ParsedErbFile> m_chunkWatcher;   // QtConcurrent::mapped 的结果
    FunctionTypes m_asyncTypes;

private slots:
    void onPrepFinished();
    void onChunkFinished();

private:
    void scheduleNextChunk();

    // 递归收集 .erb/.erh 文件（确定性顺序）
    QStringList collectFiles(const QString& dirPath, int depth) const;

    // 供线程内使用：只读函数类型表 + 本地缓存
    // quiet=true：解析失败时不打印（用于赋值右值的临时归约，见 AstBuilder::build）
    QSharedPointer<ExpressionNode> resolveExpr(const QString& expr, const FunctionTypes& types,
                                               QHash<QString, QSharedPointer<ExpressionNode>>& cache,
                                               bool quiet = false) const;

    // 预扫描：收集函数返回类型（#FUNCTION → Int，#FUNCTIONS → Str）
    FunctionTypes scanFunctionTypes(const QString& content) const;

    // 解析单个文件（线程安全：不接触任何共享可变状态）。
    // cachedContent != nullptr 时直接使用（预扫描已解码），免去重复读取+解码。
    ParsedErbFile parseOneFile(const QString& filePath, const FunctionTypes& types,
                               const QString* cachedContent = nullptr,
                               const QString& root = QString()) const;

    // 主线程合并（确定性顺序）
    bool mergeParsedFile(ParsedErbFile&& pf);

    // 逐行构建完整 AST（串行路径；会 emit parseLineReady）
    QList<LogicalLine> buildAst(const QString& content, const QString& filePath,
                                const QString& root = QString(),
                                ParseDiagnostics* diagnostics = nullptr);

    QString readFileContent(const QString& filePath) const;

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
    QHash<QString, QPair<QString, int>> m_labels;   // label -> (script, lineIndex)
    QHash<QString, int> m_labelPositions;           // "script:label" -> lineIndex
    QHash<QString, int> m_scriptLineCounts;

    // [qdbug] 验证（eraTW 实测）：并行拍平与顺序拍平结果**完全一致** ——
    // dump_lines 串行/并行各导出 2,075,985 行拍平行，diff 完全相同。
    // 并行合并本身即保序（blockingMapped 结果按输入序返回、主线程按序合并）；
    // 并行解析期的三类不确定性（「未定义」解析 / worker AST 缓存缺失 /
    // 插入顺序）已由 finalizeParse 的重绑 + labels() 的 (脚本名,行号) 排序 +
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
};

#endif // ERB_LOADER_H
