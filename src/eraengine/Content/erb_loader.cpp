#include "../GameData/game_paths.h"
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
#include "erb_loader.h"

#include "user_defined_variable_data.h"
#include <QCoreApplication>
#include "era_parse_table.h"
#include "GameData/file_system_io.h"   // FileSystem::resolvePathCase（加载前还原真实大小写）
#include "ast/expression_lexer.h"
#include "ast/expression_parser.h"
#include "ast/function_types.h"
#include "ast/strform_parser.h"
#include "constant_table.h"
#include "text_encoding.h"

#include <QDir>
#include <algorithm>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>
#include <QElapsedTimer>
#include <QDebug>
#include <QSet>
#include <QFutureWatcher>

// ERB 子目录递归扫描深度上限：仅作防御，避免异常目录树导致递归失控。
// Emuera 本身不限制子目录深度；这里保留一个足够大的上限，且统一使用 255
//（Windows 路径/目录层级的兼容上限），不能因为正常的深层数据目录漏载脚本。
static constexpr int kMaxScanDepth = 255;

// 读取失败的逐条告警上限：Android SAF 上权限被收回时可能整目录读不进来，
// 逐文件告警会刷屏；超过上限只计数，收尾日志给出总数。
static constexpr int kMaxReadFailureLogs = 20;

// 诊断开关：EMUERA_THREADS=N 限制装载/收尾的并行度（未设或 <=0 = 用 QThreadPool
// 默认）。环境变量只在下面这一个静态里读一次，写进**全局**线程池；后续所有
// 并行段（预扫描 / 分块解析 / markLoadComplete 的逐脚本 pass）都以
// QThreadPool::globalInstance()->maxThreadCount() 判并行，故一处生效。
// 用途：mem-vs-speed 对照（EMUERA_THREADS=1 即完全串行），不影响默认行为。
static void applyThreadLimit() {
    static const int limit = [] {
        bool ok = false;
        const int n = qEnvironmentVariableIntValue("EMUERA_THREADS", &ok);
        return ok && n > 0 ? n : 0;
    }();
    if (limit > 0) QThreadPool::globalInstance()->setMaxThreadCount(limit);
}

// 脚本名 = **相对装载根目录的路径**（含扩展名）。
// 对齐 C# Config.GetFiles 返回的 KeyValuePair<相対パス, 完全パス>：脚本名是
// 相对路径而不是 basename，所以不同子目录下的同名 .ERB 是**不同脚本**，不会
// 互相覆盖。eraMegaten 有 512 组同名文件（mod 的「空ファイル化」惯例），
// 旧实现用 basename -> m_scripts 后写覆盖先写，被覆盖文件的 @label 还会
// 指向赢家文件的行号（跨文件跳错）。
static QString scriptNameFor(const QString& root, const QString& filePath) {
    if (GamePaths::isContent(filePath)) return filePath; // unique even for opaque IDs
    const QString abs = QFileInfo(filePath).absoluteFilePath();
    if (!root.isEmpty()) {
        const QDir rootDir(QFileInfo(root).absoluteFilePath());
        const QString rel = rootDir.relativeFilePath(abs);
        if (!rel.startsWith(QLatin1String("..")) && !QDir::isAbsolutePath(rel)) {
            return QDir::fromNativeSeparators(rel);
        }
    }
    return QFileInfo(abs).fileName();
}

ErbLoader::ErbLoader(QObject* parent) : QObject(parent), m_parseTable(nullptr) {
    // 装载心跳：异步装载期间由主线程定时读取后台阶段/进度原子量，
    // 打印「装载心跳：阶段=… 进度=… 已耗时=…」，并把同一份快照转发给 UI
    // （loadHeartbeat → EraEngine 的 loadStage/loadProcessed/… 属性）。
    // 详见 LoadStage 的说明：eraTW 在 Android SAF 上是分钟级，中间窗口必须留痕。
    m_heartbeat.setInterval(m_heartbeatMs > 0 ? m_heartbeatMs : 1000);
    m_heartbeat.setTimerType(Qt::CoarseTimer);
    connect(&m_heartbeat, &QTimer::timeout, this, &ErbLoader::onHeartbeat);
}

ErbLoader::~ErbLoader() {
    cancelAndWait();
}

QString ErbLoader::readFileContent(const QString& filePath) const {
    // 编码按文件嗅探（BOM → UTF-8 → Shift-JIS → Latin-1）：
    // 同一游戏目录里 UTF-8 的汉化 ERB 与 Shift-JIS 的原始 ERB 可以共存。
    //
    // 大小写：先用引擎自带的「扫目录还原真实文件名」把整条路径逐段还原
    //（= Windows 的大小写不敏感文件系统在 Linux 上的模拟）。否则「在 Windows 上
    // 能跑、到 Linux 就 file not found」——脚本/头文件/include 里写的
    // `Foo.ERB` 而磁盘上是 `foo.erb` 时必然踩中。已在磁盘上存在时走快路径。
    const QString realPath = FileSystem::resolvePathCase(filePath);
    TextEncoding detected = TextEncoding::Auto;
    return TextCodecUtil::readFile(realPath, m_readEncoding, &detected, nullptr);
}

QString ErbLoader::readFileContent(const QString& filePath, bool* ok) const {
    // 与上个重载同一路径，只是把成功与否交回调用方：后台预扫描需要区分
    // 「空文件」与「读不进来」。Android SAF 下权限被收回 / 文档失效会让
    // QFile::open 失败，旧实现静默得到空文本 → 脚本无声消失（装载仍报 ok）。
    const QString realPath = FileSystem::resolvePathCase(filePath);
    TextEncoding detected = TextEncoding::Auto;
    bool readOk = false;
    const QString text = TextCodecUtil::readFile(realPath, m_readEncoding, &detected, &readOk);
    if (ok) *ok = readOk;
    return readOk ? text : QString();
}

// ---------------------------------------------------------------------------
// 预处理与 _Rename.csv
// ---------------------------------------------------------------------------
ErbPreprocessed ErbLoader::prepareLines(const QString& content, const QString& fileName,
                                        QStringList* warnings) const {
    return m_preprocessor.process(content, warnings, fileName);
}

bool ErbLoader::loadRenameFile(const QString& filePath) {
    bool ok = false;
    const QString text = TextCodecUtil::readFile(FileSystem::resolvePathCase(filePath),
                                                 m_readEncoding, nullptr, &ok);
    if (!ok) return false;
    m_preprocessor.setRenameMap(ErbPreprocessor::parseRenameCsv(text));
    return true;
}

void ErbLoader::tryAutoLoadRename(const QString& dirPath) {
    if (!m_preprocessor.renameMap().isEmpty()) {
        return;
    }
    if (dirPath.isEmpty()) return;

    // content://（Android SAF）与本地路径**走同一套探测**：
    //
    //   旧实现对 content:// 直接 return —— 注释写「EraEngine loads sibling CSV
    //   explicitly」，但引擎侧那条路径被配置项 `_Rename.csvを利用する`（默认 NO）
    //   门控，而中文游戏（eraH 等）的 _fixed.config 用的是译成中文的键
    //   `启用_Rename.csv:YES`，日语键精确匹配的 ConfigLoader 读不出来 →
    //   设备上 Rename 表永远不装载 → `[[口上:主人Ａ射精]]` 保持字符串字面量 →
    //   「函数 C_FIRSTTIME_KOJO 第 1 个实参需要Int，实得字符串」×936（桌面 0，
    //   因为桌面版本函数走了这里的目录自动扫描）。真机实测见 docs/android-saf.md。
    //
    //   QDir/QFileInfo 对 content URI 只做字符串层面的路径切分；entryList 由
    //   Android content 插件支持（collectFiles 同路），readFile 走 QFile on
    //   content://（ERB 文本读取已验证）。
    const QString parent = GamePaths::isContent(dirPath)
                               // content URI 的路径段也是 '/' 分隔：去掉结尾的 /ERB
                               ? (dirPath.contains(QLatin1Char('/'))
                                      ? dirPath.section(QLatin1Char('/'), 0, -2)
                                      : dirPath)
                               : QFileInfo(dirPath).absolutePath();
    // ERB 目录的兄弟目录 CSV / Csv / csv
    QStringList candidates;
    {
        QDir p(parent);
        const QStringList entries = p.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& e : entries) {
            if (e.compare(QLatin1String("CSV"), Qt::CaseInsensitive) != 0) continue;
            // content:// 必须按百分号转义拼接（GamePaths::join），本地路径用 QDir 拼接
            candidates << (GamePaths::isContent(parent) ? GamePaths::join(parent, e)
                                                        : p.absoluteFilePath(e));
        }
    }
    candidates << parent;   // 退化：_Rename.csv 与 ERB 同目录
    for (const QString& dir : candidates) {
        QDir d(dir);
        const QStringList entries = d.entryList(QDir::Files, QDir::Name);
        for (const QString& e : entries) {
            if (e.compare(QLatin1String("_Rename.csv"), Qt::CaseInsensitive) != 0) continue;
            const QString renamePath = GamePaths::isContent(dir)
                                           ? GamePaths::join(dir, e)
                                           : d.absoluteFilePath(e);
            bool ok = false;
            const QString text = TextCodecUtil::readFile(renamePath, m_readEncoding, nullptr, &ok);
            if (!ok) {
                qWarning() << "[load] _Rename.csv 读取失败:" << renamePath;
                continue;
            }
            const ErbPreprocessor::RenameMap map = ErbPreprocessor::parseRenameCsv(text);
            qInfo().noquote() << "[load] _Rename.csv 自动装载:" << renamePath
                              << "词条" << map.size() << "条";
            m_preprocessor.setRenameMap(map);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// 文件收集
// ---------------------------------------------------------------------------
QStringList ErbLoader::collectFiles(const QString& dirPath, int depth) const {
    QStringList out;
    if (dirPath.isEmpty() || depth > kMaxScanDepth) return out;

    QDir dir(dirPath);
    if (!dir.exists()) {
        qWarning() << "[load] ERB 子目录不可读，跳过:" << dirPath;
        return out;
    }

    // 心跳：枚举阶段以「已访问目录数」为进度（总目录数未知，故 total 用 -1）。
    // 目录枚举在 Android SAF 上是每次一趟 DocumentsContract 往返（列出全部
    // 子项），eraTW 有 ~200 个 ERB 子目录；心跳让这一段不再静默。
    bumpLoadStage(1);

    // 顺序与排序都对齐 C# Config.getFiles：
    //   · **先递归子目录，再本目录的文件**（C# 里 dirList 循环在 filepaths 之前）；
    //   · 排序 = Array.Sort(..., OrdinalIgnoreCase)，即「大小写不敏感的码点序」，
    //     不用 QDir::Name 的本地化排序（会随 locale 变、与 Emuera 不一致）。
    // 这个顺序决定同名 @label / @EVENT* 谁生效：C# 的函数表是 first-wins
    // （LabelDictionary：labelAtDic[id][0] 生效），事件函数组也按注册序执行。
    // 实测 eraTW 有 148 组同名函数的两份定义分别落在「目录」与其「子目录」
    // （如 TWけね/ 与 TWけね/未動工/ 各有一份 M_KOJO_MESSAGE_COM_K67_*），
    // 旧顺序（本目录文件先）与 Emuera 正好相反 -> winner 也不同。
    const auto ordinalIgnoreCaseLess = [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    };

    // 一次 entryInfoList 取全（含隐藏/系统项），再在内存里分成「目录」与
    // 「*.ERB/*.ERH 文件」。**不要**分两次调用（旧实现 entryList(Dirs) +
    // entryInfoList(名字过滤)）：content:// 上每次调用都是一整趟全目录枚举，
    // 每个目录白跑一趟——Android 上这正是「枚举慢」的一半来源。
    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::NoSort);

    // 子项路径**直接复用 provider 给回来的 URI**（QFileInfo::absoluteFilePath()），
    // 不再逐项走 GamePaths::join（其快路径每次都要一次 QFileInfo::exists 探测）。
    // 真机实测（OnePlus/Android 16，eraH 920 文件 / 114 目录）：枚举阶段约 1000 次
    // provider 往返，占满 64s 装载总时长里的约 53s（≈51ms/次）—— 这是设备上最大
    // 的单笔开销。条目名来自 provider 自己的列表，所以「父文档 + 子名」这一形态由
    // provider 保证；若某个 provider 的文档 ID 不可拼接，后续**读取**会失败并逐条
    // 告警（见 prepareLoad 的读取失败计数），不会像旧代码那样静默丢脚本。
    // 本地路径仍走 GamePaths::join（= QDir::filePath），保持原有的相对/绝对形态。
    const auto childPath = [&dirPath](const QFileInfo& entry) {
        if (GamePaths::isContent(dirPath)) return entry.absoluteFilePath();
        return GamePaths::join(dirPath, entry.fileName());
    };

    QFileInfoList dirEntries;
    QFileInfoList files;
    for (const QFileInfo& entry : entries) {
        if (entry.isDir()) {
            dirEntries.append(entry);
            continue;
        }
        if (!entry.isFile()) continue;
        const QString name = entry.fileName();
        if (name.endsWith(QLatin1String(".erb"), Qt::CaseInsensitive)
            || name.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive)) {
            files.append(entry);
        }
    }

    std::sort(dirEntries.begin(), dirEntries.end(),
              [&ordinalIgnoreCaseLess](const QFileInfo& x, const QFileInfo& y) {
                  return ordinalIgnoreCaseLess(x.fileName(), y.fileName());
              });
    for (const QFileInfo& d : dirEntries) {
        out.append(collectFiles(childPath(d), depth + 1));
    }

    std::sort(files.begin(), files.end(),
              [&ordinalIgnoreCaseLess](const QFileInfo& x, const QFileInfo& y) {
                  return ordinalIgnoreCaseLess(x.fileName(), y.fileName());
              });
    for (const QFileInfo& fi : files) {
        out.append(childPath(fi));
    }
    return out;
}

// ---------------------------------------------------------------------------
// 线程内表达式归约（只读函数类型表）
// ---------------------------------------------------------------------------
namespace {

// 把一个文件的扫描结果并入总表：顺序 = 文件顺序（头文件在前）= loadScript 的
// 登记顺序；重复声明与 VariableTable::addChecked 一样 **first-wins**，
// 否则解析期类型可能与最终登记的相反。
void mergeVarTypes(LoadTimeVarTypes& dest, const LoadTimeVarTypes& src) {
    for (auto it = src.globalVars.constBegin(); it != src.globalVars.constEnd(); ++it) {
        if (!dest.globalVars.contains(it.key())) dest.globalVars.insert(it.key(), it.value());
    }
    for (auto it = src.localVars.constBegin(); it != src.localVars.constEnd(); ++it) {
        QHash<QString, OperandType>& fn = dest.localVars[it.key()];
        for (auto jt = it.value().constBegin(); jt != it.value().constEnd(); ++jt) {
            if (!fn.contains(jt.key())) fn.insert(jt.key(), jt.value());
        }
    }
}

} // namespace

OperandType LoadTimeVarTypes::typeOf(const QString& name, const QString& scopeKey) const {
    const QString key = name.toUpper();
    if (!scopeKey.isEmpty()) {
        // 段身份文本大小写敏感（脚本名）：**不**做 toUpper。
        const auto fit = localVars.constFind(scopeKey);
        if (fit != localVars.constEnd()) {
            const auto it = fit.value().constFind(key);
            if (it != fit.value().constEnd()) return it.value();
        }
    }
    const auto git = globalVars.constFind(key);
    return git == globalVars.constEnd() ? OperandType::Unknown : git.value();
}

QSharedPointer<ExpressionNode> ErbLoader::resolveExpr(
    const QString& expr, const FunctionTypes& types,
    bool quiet, const LoadTimeVarTypes* varTypes, const QString& scope,
    bool foldConstants) const {
    const QString key = expr.trimmed();
    if (key.isEmpty()) return nullptr;


    ExpressionLexer lexer;
    const QList<ExpressionToken> tokens = lexer.tokenize(key, 1);
    if (tokens.isEmpty()) return nullptr;

    ExpressionParser parser;
    parser.setQuiet(quiet);   // 赋值右值的临时解析：不刷「表达式语法错误」
    // 只回答「用户自定义函数」——即为本文件里声明过 #FUNCTION/#FUNCTIONS 的 @label
    // 提供返回类型。未命中一律 Unknown，让 ExpressionParser 走内置函数目录
    // （kBuiltinFunctions）：resolveFunctionCall 只有在提供者给出「已知类型」时才
    // 把调用当用户函数，若此处把内置函数的返回类型也报出来，内置调用会被误判成
    // 用户函数 —— 装载期的 StrForm 特化（`STRFORM("<FORM 文本>")` 在解析期换成
    // StrFormNode）就永远不会命中，运行期只剩 `S(0)` 读取原始实参文本，
    // 于是 `STRFORM("{123,5,RIGHT}")` 展开成 `{123,5,RIGHT}` 而非 `  123`。
    // 非内置名同样返回 Unknown（markLoadComplete 的 resolveFunctionNodes 会按宽容语义重绑）。
    parser.setFunctionTypeProvider([&types](const QString& name) -> OperandType {
        const auto fit = types.constFind(name.toUpper());
        return fit == types.constEnd() ? OperandType::Unknown : fit.value();
    });
    // 用户变量类型：优先查**已登记**的变量表（「先登记 #DIM/#DIMS 再构造 AST」
    // 之后它是完整的，含函数私有作用域），查不到再退回预扫描类型表；
    // 仍未命中一律 Unknown，交给系统变量表兜底（parseVariable 里的
    // systemVariableTypeDyn）。
    parser.setVariableTypeProvider([this, varTypes, scope](const QString& name) -> OperandType {
        if (m_parseTable) {
            const OperandType t = m_parseTable->variableTable().typeOf(name, scope);
            if (t != OperandType::Unknown) return t;
        }
        return varTypes ? varTypes->typeOf(name, scope) : OperandType::Unknown;
    });
    // 格式化串：@"..." / \@...#...\@（内部表达式继续使用同一函数/变量类型表）
    // **foldConstants 必须透传**：FORM 里的插值表达式（`@"{颜色_赤}"`）与收尾
    // （loadpass::expressionAstInScope 的 form provider）一样要把 `#DIM CONST`
    // 折成字面量 —— 否则「成品 AST」在构建期与收尾两个答案（FL3.8 的
    // `'= @"…{颜色_赤}…"` 共 1113 行就是这么分叉的）。
    const AstResolver self = [this, &types, varTypes, &scope, foldConstants](const QString& e) {
        return resolveExpr(e, types, /*quiet=*/false, varTypes, scope, foldConstants);
    };
    parser.setFormProvider([this, self](const QString& text, bool yenAt) -> QSharedPointer<ExpressionNode> {
        if (yenAt) return StrFormParser::parseYenAt(text, self, m_parseTable && m_parseTable->ignoreTripleSymbols());
        return QSharedPointer<ExpressionNode>(StrFormParser::parse(text, self, m_parseTable && m_parseTable->ignoreTripleSymbols()));
    });
    // 常量名下标：CFLAG:ARG:現在位置（对齐 C# ConstantData.isDefined）
    if (m_constantTable) {
        const ConstantTable* ct = m_constantTable;
        parser.setConstantNameProvider([ct](const QString& var, const QString& name) {
            return ct->indexForVariable(var, name) >= 0;
        });
    }
    // `#DIM CONST NAME = value`：与收尾流水线同一份折叠实现（唯一查值入口
    // VariableTable::constantValue）。声明预登记在 AST 构建之前结算，因此
    // 此刻常数（含函数私有常数）已经就绪，与收尾重解析的答案相同。
    if (foldConstants && m_parseTable) {
        const VariableTable* vt = &m_parseTable->variableTable();
        parser.setConstantValueProvider([vt, scope](const QString& name) -> QVariant {
            return vt->constantValue(name, scope);
        });
    }
    QSharedPointer<ExpressionNode> ast = parser.parse(tokens);
    return ast;
}

// ---------------------------------------------------------------------------
// 预扫描：一个文件 -> 预处理后的行 + 它的**唯一**一份声明扫描
// ---------------------------------------------------------------------------
// 规则与 loadScript 保持一致（判据只有 declscan::scanScript 一份）：
//   * .ERH：文件级 # 行一律是广域变量；
//   * .ERB：只有**紧跟函数标签**（中间只允许空行/注释/# 行）的 # 行才是声明，
//     否则 loadScript 会告警并忽略；
//   * `#DIM GLOBAL X` / `#GLOBAL X` / `#GLOBALS X` / `#PRIVATE X` 都是广域。
//
// 这里做的是**唯一一次**扫描：返回的 Scan 随文件数据一路传给预登记
// （preRegisterDeclarations）、装载（loadScript）与类型快照（varTypes）。
// 三者读同一个 Scan —— 不是同一份源码被扫三遍。
ScannedFile ErbLoader::scanFileDeclarations(const QString& content, bool isHeaderFile,
                                            const QString& scriptName,
                                            const QString& diagnosticName,
                                            const ErbPreprocessor& pp,
                                            LoadTimeVarTypes* varTypes) const {
    // 与 loadScript 完全同一条预处理（行连接 / SKIP / 宏展开），否则段起点
    // 下标会与最终行数组错位 —— 私有类型就查不到（同一病根的第二处）。
    //
    // 这一趟预处理的产物（行 + 告警）原样交回调用方：构建期/装载期直接消费它，
    // 所以同一份文本在这个文件上只被 process 一次（见 ScannedFile 的说明）。
    ScannedFile out;
    out.pre = pp.process(content, &out.warnings, diagnosticName);
    // 扫描吃的是同一批行：这里把紧凑形态摊成 declscan::SourceLine（每行一个
    // QString，**临时**——扫描结束即释放；常驻的是 out.pre）。
    QList<declscan::SourceLine> scanLines;
    scanLines.reserve(out.pre.size());
    for (int i = 0; i < out.pre.size(); ++i)
        scanLines.append({out.pre.textAt(i), ScriptPosition(diagnosticName, out.pre.physicalLineAt(i), 1)});
    out.scan = declscan::scanScript(scanLines, scriptName, isHeaderFile);
    declscan::Scan& scan = out.scan;
    if (!varTypes) return out;
    // 类型快照 = 「同一份声明扫描 + 同一份登记」的产物摊平：登记进临时变量表
    // 再摊平，没有第二套「哪行算声明」「哪个指令是字符串」的判定。
    VariableTable vars;
    ParseDiagnostics diagnostics;   // 声明诊断由正式登记路径给出（这里丢弃）
    declscan::registerScannedDeclarations(vars, diagnostics, nullptr, scan);
    for (const VariableDecl& d : vars.declarations()) {
        if (d.scope == VarScope::Local) varTypes->localVars[d.scopeKey].insert(d.name.toUpper(), d.type);
        else varTypes->globalVars.insert(d.name.toUpper(), d.type);
    }
    return out;
}

// ---------------------------------------------------------------------------
// 声明登记（唯一的登记入口）：消费预扫描已经算好的 Scan
// ---------------------------------------------------------------------------
void ErbLoader::preRegisterDeclarations(const QStringList& files,
                                        const QHash<QString, ScannedFile>& scanned) {
    if (!m_parseTable) return;
    // 本函数在 GUI 线程上跑：期间心跳定时器（也在 GUI 线程）无法触发，
    // 所以进度直接在循环里打印 —— 逐行日志是同步写的，主线程就算被占住，
    // 已经写出的行也已经进了 logcat，真机上就能看到「登记到第几个文件」。
    QElapsedTimer progressClock;
    progressClock.start();
    int processed = 0;
    const int reportEvery = 200;   // 文件数：eraTW 2260 → 约 11 行
    m_parseTable->beginDeclarationPreRegistration();
    for (const QString& f : files) {
        // 段身份 = (脚本名, 段起点行)：脚本名与 loadScript / parseOneFile 用同一个
        // scriptNameFor（相对 root 的路径）—— 扫描时已按这个身份写好，这里只读取。
        const auto it = scanned.constFind(f);
        Q_ASSERT_X(it != scanned.constEnd(), "ErbLoader::preRegisterDeclarations",
                   "预扫描结果缺失：声明登记与扫描必须来自同一次读取");
        if (it == scanned.constEnd()) continue;
        for (const declscan::DeclarationSite& site : it.value().scan.declarations)
            m_parseTable->preRegisterDeclaration(site);
        ++processed;
        if (processed % reportEvery == 0) {
            qInfo().noquote() << "[ErbLoader] 登记声明进行中:" << processed << "/" << files.size()
                              << "个文件，" << progressClock.elapsed() << "ms";
        }
    }
    m_parseTable->finishDeclarationPreRegistration();
}

// ---------------------------------------------------------------------------
// 预扫描：函数返回类型（#FUNCTION → Int，#FUNCTIONS → Str）
// ---------------------------------------------------------------------------
ErbLoader::FunctionTypes ErbLoader::scanFunctionTypes(const QString& content) const {
    FunctionTypes types;
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (!trimmed.startsWith(QLatin1Char('@'))) continue;

        QString label = trimmed.mid(1);
        const int paren = label.indexOf(QLatin1Char('('));
        if (paren > 0) label = label.left(paren);
        label = label.trimmed();
        if (label.isEmpty()) continue;

        // 紧随其后的 #FUNCTION / #FUNCTIONS 决定返回类型；注释/空行不结束声明区
        for (int j = i + 1; j < lines.size(); ++j) {
            const QString dir = lines.at(j).trimmed();
            if (dir.isEmpty() || dir.startsWith(QLatin1Char(';'))) continue;
            if (!dir.startsWith(QLatin1Char('#'))) break;
            const QString upper = dir.toUpper();
            if (upper.startsWith(QLatin1String("#FUNCTIONS"))) {
                types.insert(label.toUpper(), OperandType::Str);
            } else if (upper.startsWith(QLatin1String("#FUNCTION"))) {
                types.insert(label.toUpper(), OperandType::Int);
            }
        }
    }
    return types;
}

// ---------------------------------------------------------------------------
// 单文件解析（线程安全）
// ---------------------------------------------------------------------------
ParsedErbFile ErbLoader::parseOneFileReadOnly(const QString& filePath,
                                              const ParallelParseInputs& inputs,
                                              const QString& root) const {
    // 线程契约（可观察）：并行解析时声明必须已预登记 —— 之后的查询只读不插。
    Q_ASSERT(inputs.types != nullptr);
    Q_ASSERT(!m_parseTable || m_parseTable->declarationsPreRegistered());
    Q_ASSERT(!m_parseTable || m_parseTable->loadPhase() == EraParseTable::LoadPhase::Loading);

    const FunctionTypes& types = *inputs.types;
    const LoadTimeVarTypes* varTypes = inputs.varTypes;
    ParsedErbFile pf;
    pf.path = filePath;
    pf.scriptName = scriptNameFor(root, filePath);
    // 预扫描的产物随行数据往后带（mergeParsedFile -> loadScript 直接消费它）：
    // 这份 Scan 是预扫描对**同一批行**扫出来的，不是对行的第二次扫描。
    if (inputs.scanned) {
        pf.declScan = inputs.scanned->scan;
        // 告警是那一次预处理的产物：沿用，不再为了拿告警把同一份文本重跑一遍。
        pf.warnings = inputs.scanned->warnings;
    }

    // 行也来自那一份产物（inputs.scanned->pre）；只有手里没有产物（单文件
    // 装载）时才在这里读盘 + 预处理一次。
    const ErbPreprocessed source =
        inputs.scanned ? inputs.scanned->pre : prepareLines(readFileContent(filePath), filePath, &pf.warnings);
    if (!source.isEmpty()) {
        pf.lines.reserve(source.size());

        // 结构化诊断出口（本文件；随后经 ParsedErbFile 汇总回灌解析表）
        QSet<QString> exprFailed;   // 同一文件的同一表达式文本只记一次
        int currentPhysicalLine = 0;
        // 当前函数作用域（#DIM/#DIMS 私有变量的类型查询要用）。与 loadScript
        // 同一规则：遇到函数标签行就切换；本行自身的归约用**切换前**的作用域
        //（标签行的实参列表不是表达式，无影响）。
        QString currentFunction;   // 函数名事实（展示/形参），**不是**作用域键
        // 段身份 = (脚本名, 段起点行)：与 preRegisterDeclarations / loadScript
        // 用同一个 eravar::segmentKeyText。文件头段 = -1，也是独立身份。
        int segmentStart = -1;
        QString currentScope = eravar::segmentKeyText(pf.scriptName, segmentStart);
        const AstResolver resolver = [this, &types, &pf, &exprFailed, &currentPhysicalLine,
                                     &filePath, &currentScope, varTypes](const QString& e) {
            auto ast = resolveExpr(e, types, /*quiet=*/false, varTypes, currentScope);
            if (!ast) {
                const QString key = e.trimmed();
                if (!key.isEmpty() && !exprFailed.contains(key)) {
                    exprFailed.insert(key);
                    pf.diagnostics.add(DiagSeverity::Warning, DiagCode::kExprParse,
                                       filePath, currentPhysicalLine, 1, 0,
                                       QCoreApplication::translate("ParseDiagnostics", "表达式无法归约: %1").arg(key.left(80)));
                }
            }
            return ast;
        };
        // 赋值右值的临时归约：静默（字符串左值的右值随后由 StrFormParser 接管）
        const AstResolver quietResolver = [this, &types, &pf, &currentScope, varTypes](const QString& e) {
            return resolveExpr(e, types, /*quiet*/ true, varTypes, currentScope);
        };
        // 「成品 AST」的归约器：赋值左值、`++/--` 目标、赋值右值清单（assignmentAsts）
        // 在构建期就要与收尾（loadpass::expressionAstInScope）逐节点相同，所以启用
        // 同一份常量折叠。这三类 AST 在收尾不再重解 —— 这一趟是唯一来源；
        // 构建期不是变量项的行由收尾按最终声明表兜底（只判不重建）。
        const AstResolver foldedResolver = [this, &types, &currentScope, varTypes](const QString& e) {
            return resolveExpr(e, types, /*quiet*/ true, varTypes, currentScope,
                               /*foldConstants=*/true);
        };
        for (int i = 0; i < source.size(); ++i) {
            currentPhysicalLine = source.physicalLineAt(i);
            const ScriptPosition pos(filePath, source.physicalLineAt(i), 1);
            // 左值「已声明类型」：取自**预扫描已登记**的变量表（#DIM/#DIMS/
            // #GLOBAL(S)/#PRIVATE 与 VARI/VARS 走同一条登记路径，系统变量由
            // VariableTable::typeOf 回退）。传入的是整个左值文本（`LOCAL:1`、
            // `CFLAG:ARG:職場`），基础变量名用 VariableTable::baseName 取 ——
            // 与收尾（applyStringAssignments/validateScriptLines）同一份实现。
            // 命中 Str 的行在构建期即编译为 StrForm；命中 Int 的行右值按表达式。
            const auto declaredType = [this, varTypes, &currentScope](const QString& lvalue) -> OperandType {
                const QString baseName = VariableTable::baseName(lvalue);
                if (m_parseTable) {
                    const OperandType t = m_parseTable->variableTable().typeOf(baseName, currentScope);
                    if (t != OperandType::Unknown) return t;
                }
                return varTypes ? varTypes->typeOf(baseName, currentScope) : OperandType::Unknown;
            };
            LogicalLine line = AstBuilder::build(source.textAt(i), pos, resolver, quietResolver,
                                                  &pf.diagnostics, declaredType, foldedResolver);
            line.lineIndex = i;
            // 作用域切换在构建**之后**（与 loadScript 的顺序一致：行自身先按
            // 旧作用域归约，遇到标签行才切到新段）。@ 行属于它自己开启的新段。
            if (line.kind == LineKind::FunctionLabel) {
                currentFunction = line.labelName;
                segmentStart = i;
                currentScope = eravar::segmentKeyText(pf.scriptName, segmentStart);
            }
            line.segmentStart = segmentStart;
            line.scopeKey = currentScope;
            pf.lines.append(line);
        }
    }
    return pf;
}

// ---------------------------------------------------------------------------
// 主线程合并
// ---------------------------------------------------------------------------
bool ErbLoader::mergeParsedFile(ParsedErbFile&& pf) {
    if (pf.scriptName.isEmpty()) {
        return false;
    }
    // 空文件是合法的（例如只含注释的 .ERH）：不视为失败，但保留诊断。
    if (pf.lines.isEmpty()) {
        if (m_parseTable && !pf.diagnostics.isEmpty()) m_parseTable->addParseDiagnostics(pf.diagnostics);
        return true;
    }

    // perf：逐文件信号（+ 接收端 qDebug）默认关闭 —— eraTW 2200+ 文件会在
    // 主线程合并阶段产生 ~4500 次发射；诊断需要时用 setEmitParseSignals(true) 打开。
    if (m_emitParseSignals) emit parseStarted(pf.scriptName);

    // 标签登记只有一处：EraParseTable::loadScript 的 m_labelIndex
    //（裸名/大小写敏感/last-wins 的第三张表已删 —— 那是同一事实的第三份副本，
    // 口径与索引相反却因答案偶合而长期未被发现）。
    //
    // 脚本清单也只有一处：ScriptData（path + lines）。曾经在这里同步写
    // m_scriptPaths / m_scriptLineCounts 两份副本，且写入**早于** loadScript：
    // loadScript 拒收（定型后 / 空脚本）时副本仍宣称「脚本已装」，日志的文件数
    // 与解析表的脚本数从此对不上。现在清单只从 ScriptData 派生。
    if (m_parseTable) {
        // 行数据只存一份：交给解析表（省内存）
        // 行 + 它们的声明扫描一起交给解析表：同一份源码不再被扫第二遍。
        const declscan::Scan* declScan = pf.declScan.isEmpty() ? nullptr : &pf.declScan;
        const bool loaded = m_parseTable->loadScript(
            pf.scriptName, pf.lines,
            pf.path.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive), pf.path, declScan);
        if (!loaded) {
            pf.diagnostics.add(DiagSeverity::Error, DiagCode::kArgCheck, pf.path, 1, 1, 0,
                               QCoreApplication::translate("ParseDiagnostics", "脚本装载失败"));
            m_parseTable->addParseDiagnostics(pf.diagnostics);
            return false;
        }
        if (!pf.warnings.isEmpty()) {
            m_parseTable->addParseWarnings(pf.warnings);
        }
        if (!pf.diagnostics.isEmpty()) {
            m_parseTable->addParseDiagnostics(pf.diagnostics);
        }
        // 行内 AST 已交给解析表，释放本文件的临时行持有者。
        pf.lines.clear();
    } else {
        m_logicalLines.insert(pf.scriptName, pf.lines);
    }

    if (m_emitParseSignals) emit parseFinished(pf.scriptName);
    return true;
}

// ---------------------------------------------------------------------------
// 装载入口
// ---------------------------------------------------------------------------
bool ErbLoader::loadFile(const QString& filePath, const QString& root,
                         const ScannedFile* scanned) {
    // 产物随数据走：目录装载的预扫描已经把这同一个文件「读一次、预处理一次」
    // 过，行 + 扫描 + 告警都放在调用方手里。传进来就**不再读盘、不再预处理**
    // —— 同一份文本不会有两份副本，也不会被处理第二遍。
    // scanned == nullptr 只出现在「单文件装载」（调用方手里确实没有），那时
    // 这里读唯一的一次、预处理唯一的一次。
    const QString body = scanned ? QString() : readFileContent(filePath);
    if (!scanned && body.isEmpty()) {
        qWarning() << "[load] ERB 读取失败或为空:" << filePath;
        return false;
    }

    ParsedErbFile pf;
    pf.scriptName = scriptNameFor(root, filePath);
    pf.path = filePath;
    // 串行路径：复用 parseTable 缓存。构建期用的那份 Scan 写回 pf.declScan，
    // 随后 mergeParsedFile 把它交给 loadScript（构建 + 装载 = 一次扫描）。
    // 预处理也只有一个来源：buildAst 交回的 warnings 就是它那一趟 prepareLines
    // 的产物（以前为了取告警在这里把同一份文本又预处理了一遍）。
    BuiltAst built = buildAst(body, filePath, root, &pf.diagnostics, scanned);
    pf.lines = std::move(built.lines);
    pf.warnings = std::move(built.warnings);
    pf.declScan = std::move(built.scan);

    return mergeParsedFile(std::move(pf));
}

bool ErbLoader::loadDirectory(const QString& dirPath, int depth) {
    if (dirPath.isEmpty() || !QFileInfo(dirPath).isDir()) {
        qWarning() << "[load] ERB 目录无效:" << dirPath;
        return false;
    }
    if (depth > kMaxScanDepth) return true;

    const QStringList allFiles = collectFiles(dirPath, depth);
    if (allFiles.isEmpty()) return true;

    // 头文件（.ERH）先行：其 #DIM 为全局、#DEFINE 定义宏
    QStringList headers, scripts;
    for (const QString& f : allFiles) {
        if (f.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive)) headers.append(f);
        else scripts.append(f);
    }
    const QStringList files = headers + scripts;

    // _Rename.csv（若未显式设置）
    tryAutoLoadRename(dirPath);

    // 宏表：#DEFINE（仅头文件，对齐 C# HeaderFileLoader）
    //
    // 头文件的**同一份字节只读一次**：宏名表与宏表来自同一次读取，读到的文本
    // 直接留在这里，供随后的预扫描复用（串行/并行两条分支的预扫描都先查它）
    // —— 于是「宏表 -> 预扫描 -> 构建」不加读盘次数。
    QHash<QString, QString> headerText;
    headerText.reserve(headers.size());
    for (const QString& h : headers) headerText.insert(h, readFileContent(h));
    QSet<QString> macros = m_preprocessor.macros();
    ErbPreprocessor::MacroTable macroTable = m_preprocessor.macroTable();
    for (const QString& h : headers) {
        const QString& text = headerText[h];
        macros.unite(ErbPreprocessor::collectDefines(text));
        const ErbPreprocessor::MacroTable t = ErbPreprocessor::collectMacroTable(text);
        for (auto it = t.constBegin(); it != t.constEnd(); ++it) macroTable.insert(it.key(), it.value());
    }
    m_preprocessor.setMacros(macros);
    m_preprocessor.setMacroTable(macroTable);

    if (!m_parallel || files.size() < 2) {
        // 串行分支也**先预扫描、再建 AST**（与并行分支同一条规则，见下面的
        // 「先登记 #DIM/#DIMS，再构造 AST」）：跨文件的声明（.ERH 的 #DIM）必须
        // 在构建期可见，否则赋值左值的类型只能退化成「未声明」。
        // 预扫描只做「读 + 按预处理后的行登记声明」，不产出 AST。
        QSet<QString> headerSet;
        for (const QString& f : files) {
            if (f.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive)) headerSet.insert(f);
        }
        // 每文件**读一次、预处理一次**：产出的 ScannedFile（行 + 扫描 + 告警）
        // 就是登记、构建、装载三处共用的那一份。文本在扫描后即释放（只有头文件
        // 的宏表需要它提前读一遍，见上面的 headerText）。
        QHash<QString, ScannedFile> scanned;
        scanned.reserve(files.size());
        for (const QString& f : files) {
            const bool isHeader = headerSet.contains(f);
            const QString text = isHeader ? headerText.value(f) : readFileContent(f);
            scanned.insert(f, scanFileDeclarations(text, isHeader, scriptNameFor(dirPath, f), f,
                                                   m_preprocessor, /*varTypes=*/nullptr));
        }
        headerText.clear();   // 宏表已建好，头文件文本不再需要（预处理行在 scanned 里）
        preRegisterDeclarations(files, scanned);

        bool ok = true;
        for (const QString& f : files) {
            // 直接把这一份产物交给 loadFile（不拷贝：它在本次调用期间只读）
            const auto it = scanned.constFind(f);
            const bool fileOk = loadFile(f, dirPath, it == scanned.constEnd() ? nullptr : &it.value());
            if (!fileOk) ok = false;
            // 本文件已消费：立即释放它的预处理行与扫描（峰值内存不随文件数累积）。
            scanned.remove(f);
        }
        // 标签数不再单独记账：唯一那份数据在 EraParseTable::m_labelIndex
        //（裸名第三张表已删，见 mergeParsedFile 的注释）。
        qDebug() << "[load] ERB（串行）" << dirPath << ":" << files.size() << "个文件,"
                 << "脚本" << loadedFileCount() << "个";
        return ok;
    }

    // 限制并行度（显式 m_maxThreads 优先；否则 EMUERA_THREADS 诊断开关）
    if (m_maxThreads > 0) {
        QThreadPool::globalInstance()->setMaxThreadCount(m_maxThreads);
    } else {
        applyThreadLimit();
    }

    QElapsedTimer timer;
    timer.start();
    qint64 tScan = 0, tParse = 0, tMerge = 0;

    // 1) 预扫描（并行）：**读+解码一次**，产出函数返回类型表 + 解码文本缓存；
    //    解析阶段复用该文本，避免第二次读盘/解码/编码嗅探。
    // 同一次读取里同时产出函数返回类型与**用户变量类型**（见 LoadTimeVarTypes）：
    // 声明必须早于 AST 构建，否则装载期所有用户变量都是「类型未知」。
    QSet<QString> headerSet;
    for (const QString& f : files) {
        if (f.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive)) headerSet.insert(f);
    }
    // 预扫描的**产物**（每文件：预处理行 + 声明扫描 + 告警）在登记与分块解析
    // 之间只有一份（QHash<QString, ScannedFile>），不再「文本一份 + 扫描一份」。
    QHash<QString, ScannedFile> scanned;
    FunctionTypes functionTypes;
    LoadTimeVarTypes varTypes;
    {
        // 一次读取（头文件复用宏表那一次的文本）→ 一次预处理 + 一次声明扫描。
        struct SyncScan {
            ScannedFile file;
            FunctionTypes functionTypes;
            LoadTimeVarTypes varTypes;
        };
        const QList<SyncScan> s2 =
            QtConcurrent::blockingMapped(files, [this, &headerSet, &dirPath, &headerText](const QString& f) {
                SyncScan scan;
                // 头文件在宏表阶段已经读过这份字节：直接复用（同一个文件不读第二次）。
                const auto pre = headerText.constFind(f);
                const QString content = pre != headerText.constEnd() ? pre.value() : readFileContent(f);
                scan.functionTypes = scanFunctionTypes(content);
                scan.file = scanFileDeclarations(content, headerSet.contains(f),
                                                 scriptNameFor(dirPath, f), f, m_preprocessor,
                                                 &scan.varTypes);
                return scan;
            });
        scanned.reserve(s2.size());
        for (int i = 0; i < s2.size(); ++i) {
            scanned.insert(files.at(i), s2.at(i).file);
            const FunctionTypes& m = s2.at(i).functionTypes;
            for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
                functionTypes.insert(it.key(), it.value());
            }
            mergeVarTypes(varTypes, s2.at(i).varTypes);
        }
    }
    headerText.clear();   // 宏表已建好；头文件文本已变成本文件的预处理行
    tScan = timer.elapsed();

    // 「先登记 #DIM/#DIMS，再构造 AST」：按预处理后的行登记声明 + 结算，之后
    // 并行构建时解析器即可拿到完整用户变量类型。
    preRegisterDeclarations(files, scanned);


    // 2) 分块并行解析 + 主线程合并（限制内存峰值）
    const int chunk = m_chunkSize > 0 ? m_chunkSize : kDefaultChunkSize;
    bool ok = true;
    for (int base = 0; base < files.size(); base += chunk) {
        const QStringList slice = files.mid(base, chunk);
        QElapsedTimer pt; pt.start();
        QList<ParsedErbFile> parsed = QtConcurrent::blockingMapped(slice,
            [this, &functionTypes, &varTypes, &scanned, dirPath](const QString& f) {
                // 本文件的预扫描产物（行 + 扫描 + 告警）：浅拷贝（隐式共享）
                const ScannedFile prod = scanned.value(f);
                const ParallelParseInputs inputs{&functionTypes, &varTypes, &prod};
                return parseOneFileReadOnly(f, inputs, dirPath);
            });
        tParse += pt.elapsed();
        QElapsedTimer mt; mt.start();
        for (ParsedErbFile& pf : parsed) {
            if (!mergeParsedFile(std::move(pf))) ok = false;
        }
        tMerge += mt.elapsed();
        for (const QString& f : slice) {   // 释放本块已消费的预处理行与扫描
            scanned.remove(f);
        }
    }
    qDebug() << "[ErbLoader] files:" << files.size()
             << "scan:" << tScan << "ms  parse:" << tParse << "ms  merge:" << tMerge << "ms"
             << " threadCount:" << QThreadPool::globalInstance()->maxThreadCount();
    // 全部脚本已合并：对 HTML 相关字符串做一次**反向追踪**，把 `HTML_PRINT X`
    // 的实参换成装载期拼好的节点（跨片段的标签此刻闭合，运行期不再解析标签）。
    if (ok && m_parseTable) m_parseTable->traceHtmlStringArguments();
    return ok;
}

// ---------------------------------------------------------------------------
// 异步装载：后台 prepare + 分块解析，主线程合并（不阻塞 UI）
// ---------------------------------------------------------------------------
ErbLoader::LoadPrep ErbLoader::prepareLoad(const QString& dirPath, int depth) const {
    // 预扫描的单文件结果（函数返回类型 + 用户变量类型 + 已解码文本）
    struct FileScan {
        ScannedFile file;          // 预处理行 + 声明扫描 + 告警（一次预处理的产物）
        FunctionTypes functionTypes;
        LoadTimeVarTypes varTypes;
    };
    // 本函数跑在线程池线程上：这里的 qInfo/计时与 m_async.clock（GUI 侧）互不干扰
    QElapsedTimer prepClock;
    prepClock.start();
    LoadPrep prep;
    setLoadStage(StageEnumerate);
    const QStringList allFiles = collectFiles(dirPath, depth);
    qInfo().noquote() << "[ErbLoader] 预扫描:枚举到" << allFiles.size()
                      << "个 ERB/ERH 文件，目录枚举耗时" << prepClock.elapsed() << "ms";
    if (allFiles.isEmpty()) {
        // 「零个 ERB 文件」是一条**显式的装载状态**，不是失败：目录里没有脚本，
        // 装载的就是空集，和同步路径 loadDirectory 的 `allFiles.isEmpty() ->
        // return true`（同一事实）完全一致。返回空的 prep 之后，状态机在
        // onPrepFinished → scheduleNextChunk 的 total=0 判断处显式收尾
        // （finishAsync(true)）；**不需要**在每个 early return 前各自发一次
        // 完成/失败 —— 那正是旧写法散落补救的地方（旧代码在这里留了一个
        // ok=false，于是同一条空目录：同步成功、异步报失败、--async 退出 1
        // 且不产出输出文件）。
        return prep;
    }
    // 头文件（.ERH）先行：其 #DIM 为全局、#DEFINE 定义宏
    QStringList headers, scripts;
    for (const QString& f : allFiles) {
        if (f.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive)) headers.append(f);
        else scripts.append(f);
    }
    prep.files = headers + scripts;

    // 强制性的顺序第一阶段：头文件的 #DEFINE 宏表（解析期要用）。此前这一段
    // 既没有日志也没有进度 —— 头文件多、或 SAF 上单次读取慢时，紧随枚举之后
    // 就是一段静默。用心跳的 Read 阶段上报「已读 N/总数」。
    setLoadStage(StageRead, 0, prep.files.size());
    QElapsedTimer macroClock;
    macroClock.start();
    // 头文件：#DEFINE 宏名与宏表来自同一份内容 —— 只读一次（以前 collectDefines
    // 与 collectMacroTable 各读一遍，同一个 .ERH 被解码两次）。读到的文本连着
    // 「这次读成功了没有」一起留在这里：紧随其后的预扫描对头文件复用同一份字节
    // （不再读盘），读取失败也仍然只计一次、只报一次。
    struct PreRead {
        QString text;
        bool ok = false;
    };
    QHash<QString, PreRead> headerReads;
    headerReads.reserve(headers.size());
    for (const QString& h : headers) {
        PreRead r;
        r.text = readFileContent(h, &r.ok);
        if (!r.ok) {
            noteReadFailure();
            qWarning() << "[load] 头文件读取失败（宏表/全局 #DIM 可能缺失）:" << h;
        }
        prep.macros.unite(ErbPreprocessor::collectDefines(r.text));
        const ErbPreprocessor::MacroTable t = ErbPreprocessor::collectMacroTable(r.text);
        for (auto it = t.constBegin(); it != t.constEnd(); ++it) prep.macroTable.insert(it.key(), it.value());
        headerReads.insert(h, r);
        bumpLoadStage(1);
    }

    // 函数返回类型预扫描（只读）。此前是**单线程**顺序读全部 2200+ 个文件，
    // 是「点开目录后长时间没有任何反应」的主因之一；这里并行化（每文件仍只读一次）。
    // prepareLoad 本身已运行在线程池线程上，blockingMapped 会再切出 N 个子任务；
    // QThreadPool 默认允许多个线程，故线程数 >1 时不会自锁，==1 时退化为顺序。
    // 读+解码**一次**，同时产出：(a) 函数返回类型表，(b) 解码后的源文本缓存。
    // 解析阶段复用 (b)，免去 eraTW 约 93MB 的第二次读取 + 解码 + 编码嗅探。
    // 同一次读取里同时产出：函数返回类型 + 用户变量类型（解析阶段要用，
    // 见 LoadTimeVarTypes 的说明）。头文件的 #DIM 是广域，所以按「是否头文件」
    // 分别扫。eraTW 实测这一遍纯文本扫描 <100ms（不读盘、不解码）。
    const QStringList& filesForScan = prep.files;
    const QSet<QString> headerSet = [&headers] {
        QSet<QString> set;
        for (const QString& h : headers) set.insert(h);
        return set;
    }();
    // 预扫描用的预处理器快照：本函数是 const，不能改 m_preprocessor；而扫描
    // 必须与后续装载用**同一套**宏/Rename（段起点下标完全一致），所以在本地
    // 复制一份只读实例交给各 worker 线程并发 process()（process 是 const）。
    ErbPreprocessor scanPP;
    scanPP.setRenameMap(m_preprocessor.renameMap());
    scanPP.setMacros(prep.macros);
    scanPP.setMacroTable(prep.macroTable);
    scanPP.setDebugMode(m_preprocessor.debugMode());
    const int threadCount = QThreadPool::globalInstance()->maxThreadCount();
    qInfo().noquote() << "[ErbLoader] 预扫描:读+解码+类型扫描开始" << prep.files.size()
                      << "个文件（头文件" << headers.size() << "个，并行线程"
                      << threadCount << "），宏表耗时" << macroClock.elapsed() << "ms";
    QElapsedTimer scanClock;
    scanClock.start();
    // 读取失败的文件（SAF 权限/文档失效）：计数 + 逐条告警（上限 20 条，避免刷屏），
    // 否则脚本会静默缺失而装载仍报 ok=true。
    std::atomic<int> failed{0};
    const auto scanOne = [this, &headerSet, &failed, &headerReads, scanPP, &dirPath](const QString& f) {
        FileScan scan;
        // 头文件：宏表阶段已经读过这份字节（连成败一起带过来），不再读第二次。
        const auto pre = headerReads.constFind(f);
        bool readOk = false;
        const QString content = [&] {
            if (pre != headerReads.constEnd()) {
                readOk = pre.value().ok;
                return pre.value().text;
            }
            return readFileContent(f, &readOk);
        }();
        if (!readOk) {
            noteReadFailure();
            if (failed.fetch_add(1, std::memory_order_relaxed) < kMaxReadFailureLogs)
                qWarning() << "[load] ERB/ERH 读取失败，已跳过:" << f;
        }
        scan.functionTypes = scanFunctionTypes(content);
        // 预处理一次 + 声明扫描一次：类型快照、预登记、构建、loadScript 共用这一份。
        // 预处理的行随 scan.file 常驻到分块解析期（不再在构建期重新预处理）；
        // 原文在本次工作项结束时就释放（它的两份用途都已在上面用完）。
        scan.file = scanFileDeclarations(content, headerSet.contains(f),
                                         scriptNameFor(dirPath, f), f, scanPP,
                                         &scan.varTypes);
        bumpLoadStage(1);
        return scan;
    };
    QList<FileScan> scans;
    if (threadCount > 1) {
        scans = QtConcurrent::blockingMapped(filesForScan, scanOne);
    } else {
        // 线程池只有 1 个线程（拿不到并行度时）：退化为顺序执行。
        // 注意这会显著变慢（eraTW 82MB × 单线程），心跳日志里的进度会一直前进，
        // 但不会「卡住」——真机上看到的就是这条线。
        qWarning() << "[load] 线程池只有 1 个线程，读+解码退化为串行"
                   << prep.files.size() << "个文件（Android 上会明显变慢）";
        scans.reserve(prep.files.size());
        for (const QString& f : filesForScan) scans.append(scanOne(f));
    }
    prep.scanned.reserve(prep.files.size());
    for (int i = 0; i < scans.size(); ++i) {
        // 每文件的产物（预处理行 + 扫描 + 告警）与它一起往后走：同一个键、同一个
        // 生命期，不再有「文本一份、扫描一份」两份 map。
        prep.scanned.insert(prep.files.at(i), std::move(scans[i].file));
        const FunctionTypes& m = scans.at(i).functionTypes;
        for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
            prep.functionTypes.insert(it.key(), it.value());
        }
        mergeVarTypes(prep.varTypes, scans.at(i).varTypes);
    }
    qInfo().noquote() << "[ErbLoader] 预扫描:读+解码+类型扫描完成" << prep.files.size()
                      << "个文件，" << scanClock.elapsed() << "ms，读取失败"
                      << m_readFailures.load(std::memory_order_relaxed);
    return prep;
}

bool ErbLoader::mergeChunk(QList<ParsedErbFile>&& parsed) {
    bool ok = true;
    for (ParsedErbFile& pf : parsed) {
        if (!mergeParsedFile(std::move(pf))) ok = false;
    }
    return ok;
}

void ErbLoader::finishAsync(bool ok) {
    // 统一收尾：先记录阶段耗时（分析「装载慢在哪」的依据），再复位状态，
    // 最后发 loadCompleted。所有退出路径都走这里，避免遗漏复位。
    const qint64 totalMs = m_async.clock.isValid() ? m_async.clock.elapsed() : 0;
    qInfo().noquote() << "[ErbLoader] 异步装载结束 ok=" << ok
                      << " 文件" << m_async.total
                      << " 总耗时" << totalMs << "ms"
                      << " 主线程合并" << m_async.mergeMs << "ms"
                      << " 阶段[枚举+读解码" << m_async.prepMs
                      << " 登记声明" << m_async.registerMs
                      << " 解析" << m_async.parseMs << "]" << "ms";
    if (!ok) {
        qWarning() << "[ErbLoader] 异步装载失败（ok=false），主线程将按失败收尾:" << m_async.dirPath;
    }
    stopHeartbeat();
    // 全部脚本已合并：HTML 字符串反向追踪（与同步路径同一入口，见 loadDirectory）。
    if (ok && m_parseTable) m_parseTable->traceHtmlStringArguments();
    if (m_prepWatcher) disconnect(m_prepWatcher.get(), nullptr, this, nullptr);
    if (m_chunkWatcher) disconnect(m_chunkWatcher.get(), nullptr, this, nullptr);
    if (m_prepWatcher) m_prepWatcher->setFuture(QFuture<LoadPrep>());
    if (m_chunkWatcher) m_chunkWatcher->setFuture(QFuture<ParsedErbFile>());
    m_prepFuture = {};
    m_chunkFuture = {};
    m_asyncTypes.clear();
    m_asyncVarTypes.globalVars.clear();
    m_asyncVarTypes.localVars.clear();
    m_async = AsyncState{};
    emit loadCompleted(ok);
}

// ---------------------------------------------------------------------------
// 装载心跳：后台线程只写原子量，主线程统一读取 → 打印 + 上报
// ---------------------------------------------------------------------------
QString ErbLoader::loadStageName(int stage) {
    switch (stage) {
    case StageEnumerate: return QStringLiteral("枚举文件");
    case StageRead:      return QStringLiteral("读取解码");
    case StageRegister:  return QStringLiteral("登记声明");
    case StageParse:     return QStringLiteral("分块解析");
    case StageMerge:     return QStringLiteral("合并脚本");
    case StageIdle:
    default:             return QStringLiteral("空闲");
    }
}

QString ErbLoader::loadStageGroupName(int stage) {
    switch (stage) {
    // 分块解析（后台）与合并（主线程）交错进行：对 UI 就是同一个阶段。
    case StageParse:
    case StageMerge:    return QStringLiteral("解析与合并脚本");
    case StageEnumerate:return QStringLiteral("枚举脚本文件");
    case StageRead:     return QStringLiteral("读取并解码脚本");
    case StageRegister: return QStringLiteral("登记变量声明");
    case StageIdle:
    default:            return QStringLiteral("空闲");
    }
}

void ErbLoader::setLoadStage(int stage, int done, int total) const {
    m_stage.store(stage, std::memory_order_relaxed);
    // done/total < 0 = 「保持不动」：合并步只切阶段，不回收进度（进度必须单调，
    // 否则心跳里的「已处理/总数」会在「解析」与「合并」之间来回跳）。
    if (done >= 0) m_stageDone.store(done, std::memory_order_relaxed);
    if (total >= 0) m_stageTotal.store(total, std::memory_order_relaxed);
}

void ErbLoader::bumpLoadStage(int n) const {
    m_stageDone.fetch_add(n, std::memory_order_relaxed);
}

void ErbLoader::startHeartbeat() {
    m_hbStage = -1;
    m_hbDone = -1;
    m_hbLogMs = 0;
    m_hbChangeMs = 0;
    if (m_heartbeatMs <= 0) return;   // 显式关闭（单测）
    m_heartbeat.start(m_heartbeatMs);
}

void ErbLoader::stopHeartbeat() {
    if (m_heartbeat.isActive()) m_heartbeat.stop();
    m_stage.store(StageIdle, std::memory_order_relaxed);
    m_stageTotal.store(0, std::memory_order_relaxed);
    m_stageDone.store(0, std::memory_order_relaxed);
    m_stageMerged.store(0, std::memory_order_relaxed);
}

void ErbLoader::onHeartbeat() {
    if (!m_async.active) {
        // 装载已结束/被取消：不该再有心跳
        stopHeartbeat();
        return;
    }
    const int stage = m_stage.load(std::memory_order_relaxed);
    const int done = m_stageDone.load(std::memory_order_relaxed);
    const int total = m_stageTotal.load(std::memory_order_relaxed);
    const qint64 elapsedMs = m_async.clock.isValid() ? m_async.clock.elapsed() : 0;
    const bool stageChanged = (stage != m_hbStage);
    const bool progressed = (done != m_hbDone);
    if (stageChanged || progressed) m_hbChangeMs = elapsedMs;
    // 停滞判定：阶段与进度都超过 kStallMs 没动。UI 用它把「慢」和「死」分开
    // （SAF 上单步可能真的要几十毫秒，但十秒毫无动静就值得提醒）。
    const bool stalled = (elapsedMs - m_hbChangeMs) >= kStallMs;
    // 日志限频：阶段变化 / 进度前进 / 至少 5s 一次（**卡住时也照打**：这条
    // 「进度没有变化」的日志正是判断「慢」还是「死」的依据）。
    const bool logDue = stageChanged || progressed || (elapsedMs - m_hbLogMs >= 5000);
    if (logDue) {
        const QString progress = total > 0 ? QStringLiteral("%1/%2").arg(done).arg(total)
                                           : QStringLiteral("%1（总数未知）").arg(done);
        const int merged = m_stageMerged.load(std::memory_order_relaxed);
        qInfo().noquote() << "[ErbLoader] 装载心跳: 阶段=" << loadStageName(stage)
                          << "进度=" << progress << "已耗时" << elapsedMs << "ms"
                          << (stalled ? QStringLiteral("（进度停滞，可能卡在文件访问）") : QString())
                          << (stage == StageMerge ? QStringLiteral("已并入 %1 个").arg(merged)
                                                  : QString())
                          << (m_readFailures.load(std::memory_order_relaxed) > 0
                                  ? QStringLiteral("读取失败 %1 个").arg(
                                        m_readFailures.load(std::memory_order_relaxed))
                                  : QString());
        m_hbLogMs = elapsedMs;
    }
    if (stageChanged) m_hbStage = stage;
    if (progressed || stageChanged) m_hbDone = done;
    // ---- UI：一次心跳 = 一份完整快照 ----
    // 阶段名用粗粒度（解析/合并交错时 1Hz 采样会来回跳，UI 文案不能跟着闪），
    // 计数用同一口径的单调值；UI 侧（EraEngine 的 loadStage/loadProcessed/…）
    // 只认这一条信号，因此进度条、阶段文案、已耗时、停滞提示天然一致。
    emit loadHeartbeat(stage, loadStageGroupName(stage), done, total > 0 ? total : 0,
                       elapsedMs, stalled);
    // 兼容：没有总量可报的阶段（枚举）仍按旧口径发 loadProgress，让外部
    // 「只看 loadProgress」的消费者也能看到动静。
    if ((progressed || stageChanged) && total <= 0) emit loadProgress(done, 0);
}

void ErbLoader::loadDirectoryAsync(const QString& dirPath, int depth) {
    // 先留痕再校验：这一步之前的任何一次「静默 return」都会让 UI 永远停在
    // 「正在装载脚本」（loadCompleted 不发 → scriptsLoaded 永不到来）。
    // Android 上 QFileInfo(content://…) 的 isDir() 本身就可能要一趟 provider
    // 往返，先打印才能区分「卡在校验里」和「根本没进来」。
    qInfo().noquote() << "[ErbLoader] 异步装载开始:"
                      << (GamePaths::isContent(dirPath)
                              ? QStringLiteral("[content URI] ") + dirPath
                              : dirPath);
    if (m_async.active) {
        // 正常调用链（loadAsync/reloadAsync）都会先 stopAsyncLoad() 复位 active，
        // 能走到这里只可能是旁路调用 —— 大声报错并按失败收尾，绝不悬挂界面。
        qWarning() << "[load] 上一轮异步装载仍在进行，拒绝装载:" << dirPath;
        emit loadCompleted(false);
        return;
    }
    if (dirPath.isEmpty() || !QFileInfo(dirPath).isDir()) {
        qWarning() << "[load] ERB 目录无效:" << dirPath
                   << (GamePaths::isContent(dirPath)
                           ? QStringLiteral("（content URI 可能已失效/权限被收回：请重新选择游戏目录）")
                           : QStringLiteral("（目录不存在或不可读）"));
        emit loadCompleted(false);
        return;
    }
    m_async = AsyncState{};
    m_async.active = true;
    m_async.ok = true;
    m_async.dirPath = dirPath;
    m_async.depth = depth;
    m_async.clock.start();
    if (m_maxThreads > 0) {
        QThreadPool::globalInstance()->setMaxThreadCount(m_maxThreads);
    } else {
        applyThreadLimit();
    }
    qInfo().noquote() << "[ErbLoader] 异步装载参数: 并行" << (m_parallel ? "是" : "否")
                      << "块大小" << m_chunkSize << "线程池"
                      << QThreadPool::globalInstance()->maxThreadCount();
    // 先落到「枚举」阶段：后台任务还没被线程池调度起来的这几十毫秒里，
    // 任何心跳/取消日志都不该显示「空闲」。
    setLoadStage(StageEnumerate, 0, 0);
    startHeartbeat();

    const auto generation = ++m_generation;
    m_prepWatcher = std::make_unique<QFutureWatcher<LoadPrep>>();
    m_chunkWatcher = std::make_unique<QFutureWatcher<ParsedErbFile>>();
    connect(m_prepWatcher.get(), &QFutureWatcher<LoadPrep>::finished, this, [this, generation] {
        if (generation == m_generation && m_async.active && m_prepFuture.isFinished() && m_prepFuture.isValid()) onPrepFinished();
    });
    connect(m_chunkWatcher.get(), &QFutureWatcher<ParsedErbFile>::finished, this, [this, generation] {
        // setFuture(QFuture<ParsedErbFile>()) below also emits finished on some
        // Qt versions.  That empty future is only cleanup; it must never be
        // treated as an empty parsed chunk, otherwise loading stops after the
        // first merge batch.
        if (generation == m_generation && m_async.active
            && m_chunkFuture.isValid() && m_chunkFuture.isFinished()) {
            onChunkFinished();
        }
    });
    m_prepFuture = QtConcurrent::run([this, dirPath, depth] { return prepareLoad(dirPath, depth); });
    m_prepWatcher->setFuture(m_prepFuture);
}

void ErbLoader::cancelLoad() {
    if (!m_async.active) return;
    qInfo().noquote() << "[ErbLoader] 异步装载被取消:"
                      << loadStageName(loadStage()) << m_stageDone.load() << "/" << m_stageTotal.load();
    m_async.cancelled = true;
    stopHeartbeat();
    m_prepFuture.cancel();
    m_chunkFuture.cancel();
}

void ErbLoader::cancelAndWait() {
    ++m_generation;
    cancelLoad();
    if (m_prepFuture.isRunning()) m_prepFuture.waitForFinished();
    if (m_chunkFuture.isRunning()) m_chunkFuture.waitForFinished();
    // QtConcurrent::mapped 可能已经完成但 watcher 尚未送出 finished；先断开
    // 已排队的完成通知，避免下一轮装载误消费旧 future 的结果。
    if (m_prepWatcher) disconnect(m_prepWatcher.get(), nullptr, this, nullptr);
    if (m_chunkWatcher) disconnect(m_chunkWatcher.get(), nullptr, this, nullptr);
    m_prepWatcher.reset();
    m_chunkWatcher.reset();

    m_prepFuture = {};
    m_chunkFuture = {};
    m_preprocessor = ErbPreprocessor{};
    m_async = AsyncState{};
    m_asyncTypes.clear();
    m_asyncVarTypes.globalVars.clear();
    m_asyncVarTypes.localVars.clear();
    m_logicalLines.clear();
    stopHeartbeat();
    m_readFailures.store(0, std::memory_order_relaxed);
}

void ErbLoader::onPrepFinished() {
    if (m_async.cancelled) {
        finishAsync(false);
        return;
    }
    LoadPrep prep = m_prepFuture.takeResult();
    m_prepWatcher->setFuture(QFuture<LoadPrep>());
    m_prepFuture = {};
    m_async.prepMs = m_async.clock.isValid() ? m_async.clock.elapsed() : 0;
    // 没有「准备失败」的分支：prepareLoad 的每个出口都是完成（空目录 = 空集），
    // 零点由下面的 total=0 在 scheduleNextChunk 里显式收尾（finishAsync(true)）。
    // _Rename.csv：未显式设置时按目录自动探测（对齐 C# 从 CsvDir 读取）
    if (m_preprocessor.renameMap().isEmpty()) {
        tryAutoLoadRename(m_async.dirPath);
    }
    m_preprocessor.setMacros(prep.macros);
    m_preprocessor.setMacroTable(prep.macroTable);
    m_asyncTypes = prep.functionTypes;
    m_asyncVarTypes = prep.varTypes;   // 解析期只读的用户变量类型快照
    m_async.files = prep.files;
    // 复用预扫描的产物（每文件：预处理行 + 声明扫描 + 告警，一份值一个生命期）
    m_async.scanned = std::move(prep.scanned);
    m_async.total = prep.files.size();
    {
        // 先登记 #DIM/#DIMS（按预处理后的行），再让分块并行构建 AST。
        // **这一段在 GUI 线程上**（要写解析表，之后并行解析只读它），且此前
        // 完全没有日志：eraTW 桌面实测数秒，Android 上更久 —— 界面在这段时间
        // 是真的冻住的（心跳定时器也发不出来，主线程被占），所以必须在进入前
        // 打一行、并每隔一段再打一行进度，否则真机上就是「毫无反应」。
        setLoadStage(StageRegister, 0, m_async.total);
        qInfo().noquote() << "[ErbLoader] 登记声明(#DIM/#DIMS/#GLOBAL…)开始（主线程）:"
                          << m_async.total << "个文件（预扫描已按头文件分开扫过）";
        QElapsedTimer registerClock;
        registerClock.start();
        // 登记消费预扫描的 Scan：不重新预处理、不重新扫描（同一份源码只扫一次）。
        preRegisterDeclarations(m_async.files, m_async.scanned);
        m_async.registerMs = registerClock.elapsed();
        qInfo().noquote() << "[ErbLoader] 登记声明完成:" << m_async.registerMs << "ms";
    }
    m_async.parsed = 0;
    m_async.merged = 0;

    // 预扫描（列举 + 宏表 + 函数返回类型）已在后台完成：这里能看出它为 UI
    // 首帧前贡献了多少等待时间。
    qInfo().noquote() << "[ErbLoader] 异步预扫描完成(读文件+函数/变量类型)"
                      << m_async.clock.elapsed() << "ms，" << m_async.total << "个文件，读取失败"
                      << m_readFailures.load(std::memory_order_relaxed);
    const auto generation = m_generation;
    emit loadProgress(0, m_async.total);
    if (generation == m_generation && m_async.active) scheduleNextChunk();
}

void ErbLoader::scheduleNextChunk() {
    if (m_async.cancelled) {
        finishAsync(false);
        return;
    }
    const int chunk = m_chunkSize > 0 ? m_chunkSize : kDefaultChunkSize;
    if (m_async.parsed >= m_async.total) {
        finishAsync(m_async.ok);
        return;
    }
    const QStringList slice = m_async.files.mid(m_async.parsed, chunk);
    if (slice.isEmpty()) {
        finishAsync(m_async.ok);
        return;
    }
    // 阶段=解析：心跳据此上报「解析 已调度/总数」。分块序号 + 文件区间一并打印，
    // Android 上「解析到第几块」就是判断是慢还是死的唯一依据。
    ++m_async.chunkIndex;
    m_async.chunkStartedMs = m_async.clock.isValid() ? m_async.clock.elapsed() : 0;
    setLoadStage(StageParse, m_async.parsed, m_async.total);
    qInfo().noquote() << "[ErbLoader] 分块解析 #" << m_async.chunkIndex
                      << "文件 [" << m_async.parsed << "," << (m_async.parsed + slice.size()) << ")"
                      << "/" << m_async.total << "（已耗时" << m_async.chunkStartedMs << "ms）";
    const FunctionTypes types = m_asyncTypes;
    // 用户变量类型快照同样是「本次 mapped 运行期间只读」，按值捕获
    //（QHash 隐式共享，拷贝很便宜）。
    const LoadTimeVarTypes varTypes = m_asyncVarTypes;
    // 复用预扫描的产物（m_async.scanned 在本次 mapped 运行期间只读，直到
    // onChunkFinished 才回收 —— 无并发写）：构建期因此**不再预处理**这份文本。
    m_chunkFuture = QtConcurrent::mapped(slice,
        [this, types, varTypes](const QString& f) {
            const ScannedFile prod = m_async.scanned.value(f);   // 浅拷贝（隐式共享）
            const ParallelParseInputs inputs{&types, &varTypes, &prod};
            return parseOneFileReadOnly(f, inputs, m_async.dirPath);
        });
    m_chunkWatcher->setFuture(m_chunkFuture);
}

void ErbLoader::onChunkFinished() {
    if (m_async.cancelled) {
        finishAsync(false);
        return;
    }
    QList<ParsedErbFile> parsed = m_chunkFuture.results();
    // results() 复制结果：先释放 future/watcher 的原件，再开始修改和合并行数据。
    m_chunkWatcher->setFuture(QFuture<ParsedErbFile>());
    m_chunkFuture = {};
    const int n = parsed.size();
    // 本块后台解析已完成：立即释放这批复用过的预处理行与扫描（用完即弃）
    const int base = m_async.parsed;
    for (int i = base; i < base + n && i < m_async.files.size(); ++i)
        m_async.scanned.remove(m_async.files.at(i));
    m_async.parsed += n;
    // 进度按「后台解析完成」的文件数单调推进（合并步不回收它）
    setLoadStage(StageParse, m_async.parsed, m_async.total);
    if (n == 0) {
        qWarning() << "[ErbLoader] 分块解析 #" << m_async.chunkIndex
                   << "返回 0 个结果（future 结果丢失），提前结束装载";
        finishAsync(m_async.ok);
        return;
    }
    // 本块后台解析耗时（wall time：调度 → 全部结果就绪）
    const qint64 nowMs = m_async.clock.isValid() ? m_async.clock.elapsed() : 0;
    const qint64 chunkParseMs = nowMs - m_async.chunkStartedMs;
    m_async.parseMs += chunkParseMs;
    qInfo().noquote() << "[ErbLoader] 分块解析 #" << m_async.chunkIndex << "完成:"
                      << n << "个文件，" << chunkParseMs << "ms（累计解析" << m_async.parseMs << "ms）";
    // 结果入队，交给 mergeStep 分小批并入（每批后回事件循环，避免长时间阻塞 UI）
    for (ParsedErbFile& pf : parsed) {
        m_async.pending.append(std::move(pf));
    }
    mergeStep();
}

// 主线程合并「一小批」：把一整块（kDefaultChunkSize）解析结果拆成多次
// 事件循环往返，单次阻塞 ≈ kMergeBatch × 每文件合并耗时（eraTW ~10ms）。
void ErbLoader::mergeStep() {
    if (m_async.cancelled) {
        finishAsync(false);
        return;
    }
    QElapsedTimer mergeClock;
    mergeClock.start();
    // 只切阶段，不动进度（done 由 scheduleNextChunk/onChunkFinished 单调推进）；
    // 「已并入」另用 m_stageMerged 单独上报。
    setLoadStage(StageMerge);
    int n = 0;
    while (n < kMergeBatch && m_async.pendingPos < m_async.pending.size()) {
        ParsedErbFile& pf = m_async.pending[m_async.pendingPos];
        if (!mergeParsedFile(std::move(pf))) m_async.ok = false;
        ++m_async.pendingPos;
        ++n;
    }
    m_async.mergeMs += mergeClock.elapsed();
    m_async.merged += n;
    m_stageMerged.store(m_async.merged, std::memory_order_relaxed);
    // 合并阶段的 UI 进度：口径是「已并入解析表的文件数」（与心跳的「已解析」
    // 同属分块流水线，但这里才是这一阶段的真实进度；心跳不在本阶段上报）。
    const auto generation = m_generation;
    emit loadProgress(m_async.merged, m_async.total);
    if (generation != m_generation || !m_async.active) return;

    if (m_async.pendingPos < m_async.pending.size()) {
        // 还有待并：让主线程回事件循环后再继续（保持 UI 响应）
        QMetaObject::invokeMethod(this, [this, generation = m_generation] {
            if (generation == m_generation && m_async.active) mergeStep();
        }, Qt::QueuedConnection);
        return;
    }
    m_async.pending.clear();
    m_async.pendingPos = 0;
    // 本块全部并入：继续下一块的解析
    QMetaObject::invokeMethod(this, [this, generation = m_generation] {
        if (generation == m_generation && m_async.active) scheduleNextChunk();
    }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// 串行 AST 构建（loadFile 路径）
// ---------------------------------------------------------------------------
ErbLoader::BuiltAst ErbLoader::buildAst(const QString& content, const QString& filePath,
                                        const QString& root, ParseDiagnostics* diagnostics,
                                        const ScannedFile* scanned) {
    BuiltAst out;
    if (!m_parseTable) return out;
    const QString scriptName = scriptNameFor(root, filePath);
    const bool isHeaderFile = filePath.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive);

    // 行与告警：有预扫描产物就直接用它（同一份文本不预处理第二遍）；
    // 没有（单文件装载）就在这里预处理唯一的一次，告警随 BuiltAst 原样交回。
    const ErbPreprocessed source =
        scanned ? scanned->pre : prepareLines(content, filePath, &out.warnings);
    if (scanned) out.warnings = scanned->warnings;
    QList<declscan::SourceLine> lines;
    lines.reserve(source.size());
    for (int i = 0; i < source.size(); ++i)
        lines.append({source.textAt(i), ScriptPosition(filePath, source.physicalLineAt(i), 1)});

    // 脚本级构建的唯一入口：声明是它的**前置步骤**（先扫本脚本的 #DIM/#DIMS 与
    // VARI/VARS，再逐行构造），因此构建期不存在「左值类型未知」的赋值行 ——
    // 单文件游戏（没有跨文件预登记）走的正是这条路径。
    // scan：目录装载的预扫描结果（有就直接用）；实际用的那一份随 BuiltAst::scan
    // 交回调用方 —— loadScript 因此不再重扫同一份源码。
    declscan::BuiltScript built =
        m_parseTable->buildScriptAst(scriptName, lines, isHeaderFile, diagnostics,
                                     scanned ? &scanned->scan : nullptr);
    out.scan = std::move(built.scan);
    for (int i = 0; i < built.lines.size() && i < source.size(); ++i)
        emit parseLineReady(scriptName, source.physicalLineAt(i), source.textAt(i));
    out.lines = std::move(built.lines);
    return out;
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------
QHash<QString, QList<LogicalLine>> ErbLoader::getLoadedScripts() const {
    if (m_parseTable) {
        QHash<QString, QList<LogicalLine>> out;
        for (const QString& name : m_parseTable->scriptNames()) {
            if (const ScriptData* sd = m_parseTable->script(name)) {
                out.insert(name, sd->lines);
            }
        }
        return out;
    }
    return m_logicalLines;
}

bool ErbLoader::scriptExists(const QString& scriptName) const {
    if (m_parseTable && m_parseTable->scriptNames().contains(scriptName)) return true;
    return m_logicalLines.contains(scriptName);
}

QString ErbLoader::resolveScriptName(const QString& scriptName) const {
    if (m_parseTable) {
        if (m_parseTable->script(scriptName)) return scriptName;
        const QString lowerName = scriptName.toLower();
        for (const QString& name : m_parseTable->scriptNames()) {
            if (name.toLower() == lowerName) return name;
        }
        return QString();
    }
    if (m_logicalLines.contains(scriptName)) return scriptName;
    const QString lowerName = scriptName.toLower();
    for (auto it = m_logicalLines.constBegin(); it != m_logicalLines.constEnd(); ++it) {
        if (it.key().toLower() == lowerName) return it.key();
    }
    return QString();
}

QHash<QString, QList<LogicalLine>> ErbLoader::getLoadedScriptsCI() const {
    QHash<QString, QList<LogicalLine>> result;
    for (auto it = m_logicalLines.constBegin(); it != m_logicalLines.constEnd(); ++it) {
        result.insert(it.key().toLower(), it.value());
    }
    return result;
}

QList<LogicalLine> ErbLoader::getLogicalLines(const QString& scriptName) {
    if (m_parseTable) {
        if (const ScriptData* sd = m_parseTable->script(scriptName)) return sd->lines;
        return QList<LogicalLine>();
    }
    return m_logicalLines.value(scriptName, QList<LogicalLine>());
}

QList<LogicalLine> ErbLoader::getLogicalLinesCI(const QString& scriptName) {
    if (m_parseTable) {
        if (const ScriptData* sd = m_parseTable->script(scriptName)) return sd->lines;
        const QString lowerName = scriptName.toLower();
        for (const QString& name : m_parseTable->scriptNames()) {
            if (name.toLower() == lowerName) {
                if (const ScriptData* sd = m_parseTable->script(name)) return sd->lines;
            }
        }
        return QList<LogicalLine>();
    }
    if (m_logicalLines.contains(scriptName)) return m_logicalLines.value(scriptName);
    const QString lowerName = scriptName.toLower();
    for (auto it = m_logicalLines.constBegin(); it != m_logicalLines.constEnd(); ++it) {
        if (it.key().toLower() == lowerName) return it.value();
    }
    return QList<LogicalLine>();
}

int ErbLoader::getScriptLineCount(const QString& scriptName) const {
    // 行数只有一个来源：ScriptData::lines.size()（无解析表时 = 行存储条目数）。
    if (m_parseTable) return m_parseTable->scriptLineCount(scriptName);
    return m_logicalLines.value(scriptName).size();
}

QString ErbLoader::getScriptPath(const QString& scriptName) const {
    if (m_parseTable) return m_parseTable->scriptPath(scriptName);
    // 独立装载器没有 ScriptData：路径取自行上的文件身份（buildAst 把 filePath
    // 逐行写进 position.filename），不另存一份「脚本名 -> 路径」。
    const auto it = m_logicalLines.constFind(scriptName);
    if (it == m_logicalLines.constEnd() || it.value().isEmpty()) return QString();
    return it.value().first().position.filename;
}

int ErbLoader::loadedFileCount() const {
    if (m_parseTable) return m_parseTable->scriptCount();
    return m_logicalLines.size();
}
