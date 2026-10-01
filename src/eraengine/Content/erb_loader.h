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
#include <QFutureWatcher>
#include "text_encoding.h"
#include "ast/logical_line.h"
#include "ast/ast_builder.h"
#include "ast/operand_type.h"
#include "erb_preprocessor.h"

class EraParseTable;

// ---------------------------------------------------------------------------
// 一个 ERB 文件解析结果（可在线程中独立产出，随后在主线程合并）
// ---------------------------------------------------------------------------
struct ParsedErbFile {
    QString scriptName;
    QString path;
    QList<LogicalLine> lines;
    QStringList warnings;                                       // 预处理层面告警
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
    bool loadFile(const QString& filePath);
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
    void setChunkSize(int n) { m_chunkSize = n > 0 ? n : 64; }

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
        FunctionTypes functionTypes;
        bool ok = false;
    };
    LoadPrep prepareLoad(const QString& dirPath, int depth) const;
    // 主线程合并一批解析结果
    bool mergeChunk(QList<ParsedErbFile>&& parsed);

    // 异步装载状态机
    struct AsyncState {
        bool active = false;
        bool cancelled = false;
        bool ok = true;
        int total = 0;
        int processed = 0;
        int depth = 0;
        QString dirPath;
        QStringList files;      // 已排序（头文件优先），供分块切片
    };
    AsyncState m_async;
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
    QSharedPointer<ExpressionNode> resolveExpr(const QString& expr, const FunctionTypes& types,
                                               QHash<QString, QSharedPointer<ExpressionNode>>& cache) const;

    // 预扫描：收集函数返回类型（#FUNCTION → Int，#FUNCTIONS → Str）
    FunctionTypes scanFunctionTypes(const QString& content) const;

    // 解析单个文件（线程安全：不接触任何共享可变状态）
    ParsedErbFile parseOneFile(const QString& filePath, const FunctionTypes& types) const;

    // 主线程合并（确定性顺序）
    bool mergeParsedFile(ParsedErbFile&& pf);

    // 逐行构建完整 AST（串行路径；会 emit parseLineReady）
    QList<LogicalLine> buildAst(const QString& content, const QString& filePath);

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
    int  m_chunkSize = 64;

    EraParseTable* m_parseTable;
};

#endif // ERB_LOADER_H
