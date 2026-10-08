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

// ERB 子目录递归扫描深度上限：**只是防御**（避免符号链接成环把扫描卡死）。
// Emuera 对子目录没有深度限制，而 eraMegaten 的 ERB 树深达 7 层
// （ERB/RPG/スキル関係/CSTR専用スキル/外部作品/アークナイツ/スルト/…）——
// 旧的 `depth > 5` 会静默丢掉 271 个文件（8125/8396），那些文件里的 @函数
// 于是全部「未定义」（AUTO_PU_SKILL_核融巨影 / AUTO_PU_SKILL_黃昏（ＡＮ） 等）。
static constexpr int kMaxScanDepth = 32;

// 脚本名 = **相对装载根目录的路径**（含扩展名）。
// 对齐 C# Config.GetFiles 返回的 KeyValuePair<相対パス, 完全パス>：脚本名是
// 相对路径而不是 basename，所以不同子目录下的同名 .ERB 是**不同脚本**，不会
// 互相覆盖。eraMegaten 有 512 组同名文件（mod 的「空ファイル化」惯例），
// 旧实现用 basename -> m_scripts 后写覆盖先写，被覆盖文件的 @label 还会
// 指向赢家文件的行号（跨文件跳错）。
static QString scriptNameFor(const QString& root, const QString& filePath) {
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

ErbLoader::ErbLoader(QObject* parent) : QObject(parent), m_parseTable(nullptr) {}

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

// ---------------------------------------------------------------------------
// 预处理与 _Rename.csv
// ---------------------------------------------------------------------------
QList<ErbSourceLine> ErbLoader::prepareLines(const QString& content, const QString& fileName,
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

void ErbLoader::tryAutoLoadRename(const QString& dirPath) {    if (!m_preprocessor.renameMap().isEmpty()) {
        return;
    }
    // ERB 目录的兄弟目录 CSV / Csv / csv
    const QDir erbDir(dirPath);
    const QString parent = QFileInfo(dirPath).absolutePath();
    QStringList candidates;
    {
        QDir p(parent);
        const QStringList entries = p.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& e : entries) {
            if (e.compare(QLatin1String("CSV"), Qt::CaseInsensitive) == 0) {
                candidates << p.absoluteFilePath(e);
            }
        }
    }
    candidates << parent;   // 退化：_Rename.csv 与 ERB 同目录
    for (const QString& dir : candidates) {
        QDir d(dir);
        const QStringList entries = d.entryList(QDir::Files, QDir::Name);
        for (const QString& e : entries) {
            if (e.compare(QLatin1String("_Rename.csv"), Qt::CaseInsensitive) != 0) continue;
            bool ok = false;
            const QString text = TextCodecUtil::readFile(d.absoluteFilePath(e), m_readEncoding,
                                                         nullptr, &ok);
            if (!ok) continue;
            m_preprocessor.setRenameMap(ErbPreprocessor::parseRenameCsv(text));
            return;
        }
    }
    Q_UNUSED(erbDir);
}

// ---------------------------------------------------------------------------
// 文件收集
// ---------------------------------------------------------------------------
QStringList ErbLoader::collectFiles(const QString& dirPath, int depth) const {
    QStringList out;
    if (depth > kMaxScanDepth) return out;

    QDir dir(dirPath);
    if (!dir.exists()) return out;

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

    QStringList dirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);
    std::sort(dirs.begin(), dirs.end(), ordinalIgnoreCaseLess);
    for (const QString& d : dirs) {
        out.append(collectFiles(dirPath + "/" + d, depth + 1));
    }

    QFileInfoList files = dir.entryInfoList(
        QStringList{"*.ERB", "*.erb", "*.ERH", "*.erh"},
        QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::NoSort);
    std::sort(files.begin(), files.end(),
              [&ordinalIgnoreCaseLess](const QFileInfo& x, const QFileInfo& y) {
                  return ordinalIgnoreCaseLess(x.fileName(), y.fileName());
              });
    for (const QFileInfo& fi : files) {
        out.append(fi.absoluteFilePath());
    }
    return out;
}

// ---------------------------------------------------------------------------
// 线程内表达式归约（本地缓存 + 只读函数类型表）
// ---------------------------------------------------------------------------
QSharedPointer<ExpressionNode> ErbLoader::resolveExpr(
    const QString& expr, const FunctionTypes& types,
    QHash<QString, QSharedPointer<ExpressionNode>>& cache, bool quiet) const {
    const QString key = expr.trimmed();
    if (key.isEmpty()) return nullptr;

    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();

    ExpressionLexer lexer;
    const QList<ExpressionToken> tokens = lexer.tokenize(key, 1);
    if (tokens.isEmpty()) return nullptr;

    ExpressionParser parser;
    parser.setQuiet(quiet);   // 赋值右值的临时解析：不刷「表达式语法错误」
    parser.setFunctionTypeProvider([&types](const QString& name) -> OperandType {
        const auto fit = types.constFind(name.toUpper());
        return fit == types.constEnd() ? builtinFunctionReturnType(name.toUpper().toStdString())
                                       : fit.value();
    });
    // 格式化串：@"..." / \@...#...\@（内部表达式继续走同一缓存/函数类型表）
    const AstResolver self = [this, &types, &cache](const QString& e) {
        return resolveExpr(e, types, cache);
    };
    parser.setFormProvider([self](const QString& text, bool yenAt) -> QSharedPointer<ExpressionNode> {
        if (yenAt) return StrFormParser::parseYenAt(text, self);
        return QSharedPointer<ExpressionNode>(StrFormParser::parse(text, self));
    });
    // 常量名下标：CFLAG:ARG:現在位置（对齐 C# ConstantData.isDefined）
    if (m_constantTable) {
        const ConstantTable* ct = m_constantTable;
        parser.setConstantNameProvider([ct](const QString& var, const QString& name) {
            return ct->indexForVariable(var, name) >= 0;
        });
    }
    QSharedPointer<ExpressionNode> ast = parser.parse(tokens);
    if (ast) cache.insert(key, ast);
    return ast;
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

        // 紧随其后的 #FUNCTION / #FUNCTIONS 决定返回类型
        for (int j = i + 1; j < lines.size(); ++j) {
            const QString dir = lines.at(j).trimmed();
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
ParsedErbFile ErbLoader::parseOneFile(const QString& filePath, const FunctionTypes& types,
                                    const QString* cachedContent, const QString& root) const {
    ParsedErbFile pf;
    pf.path = filePath;
    pf.scriptName = scriptNameFor(root, filePath);

    // 预扫描阶段已读取并解码过：直接复用，省掉第二次读盘 + 解码 + 编码嗅探。
    const QString content = cachedContent ? *cachedContent : readFileContent(filePath);
    if (!content.isEmpty()) {
        const QList<ErbSourceLine> source = prepareLines(content, filePath, &pf.warnings);
        pf.lines.reserve(source.size());

        // 结构化诊断出口（本文件；随后经 ParsedErbFile 汇总回灌解析表）
        QSet<QString> exprFailed;   // 同一文件的同一表达式文本只记一次
        const AstResolver resolver = [this, &types, &pf, &exprFailed](const QString& e) {
            auto ast = resolveExpr(e, types, pf.astCache);
            if (!ast) {
                const QString key = e.trimmed();
                if (!key.isEmpty() && !exprFailed.contains(key)) {
                    exprFailed.insert(key);
                    pf.diagnostics.add(DiagSeverity::Warning, DiagCode::kExprParse,
                                       QString(),
                                       QStringLiteral("表达式无法归约: %1").arg(key.left(80)));
                }
            }
            return ast;
        };
        // 赋值右值的临时归约：静默（字符串赋值随后由 StrFormParser 重新解释）
        const AstResolver quietResolver = [this, &types, &pf](const QString& e) {
            return resolveExpr(e, types, pf.astCache, /*quiet*/ true);
        };
        for (int i = 0; i < source.size(); ++i) {
            const ScriptPosition pos(filePath, source.at(i).physicalLine, 1);
            LogicalLine line = AstBuilder::build(source.at(i).text, pos, resolver, quietResolver,
                                                  &pf.diagnostics);
            line.lineIndex = i;
            pf.lines.append(line);
        }
    }
    // AST 磁盘缓存：在 worker 线程上序列化**每一个**文件（含空/读取失败者 —— 保证
    // 缓存文件的条目数与文件数一一对应，否则读回时条数不符会被当作损坏）。
    if (m_astDiskCache) {
        pf.cachedBlob = ErbAstDiskCache::serializeParsedFile(pf);
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
    // 空文件是合法的（例如只含注释的 .ERH）：不视为失败
    if (pf.lines.isEmpty()) {
        return true;
    }

    // perf：逐文件信号（+ 接收端 qDebug）默认关闭 —— eraTW 2200+ 文件会在
    // 主线程合并阶段产生 ~4500 次发射；诊断需要时用 setEmitParseSignals(true) 打开。
    if (m_emitParseSignals) emit parseStarted(pf.scriptName);

    for (int i = 0; i < pf.lines.size(); ++i) {
        const LogicalLine& line = pf.lines.at(i);
        if (line.isLabel()) {
            m_labels.insert(line.labelName, {pf.scriptName, i});
            m_labelPositions.insert(pf.scriptName + ":" + line.labelName, i);
        }
    }

    m_scriptPaths.insert(pf.scriptName, pf.path);
    m_scriptLineCounts.insert(pf.scriptName, pf.lines.size());

    if (m_parseTable) {
        // 行数据只存一份：交给解析表（省内存）
        m_parseTable->loadScript(pf.scriptName, pf.lines,
                                 pf.path.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive),
                                 pf.path);
        if (!pf.warnings.isEmpty()) {
            m_parseTable->addParseWarnings(pf.warnings);
        }
        if (!pf.diagnostics.isEmpty()) {
            m_parseTable->addParseDiagnostics(pf.diagnostics);
        }
        // 不合并 worker 缓存：行内 AST 已自带，运行期新表达式按需再解析（懒加载）
        pf.astCache.clear();
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
bool ErbLoader::loadFile(const QString& filePath, const QString& root) {
    const QString content = readFileContent(filePath);
    if (content.isEmpty()) {
        qWarning() << "[load] ERB 读取失败或为空:" << filePath;
        return false;
    }

    ParsedErbFile pf;
    pf.scriptName = scriptNameFor(root, filePath);
    pf.path = filePath;
    pf.lines = buildAst(content, filePath, root, &pf.diagnostics);   // 串行路径：复用 parseTable 缓存
    // buildAst 的预处理告警：再次预处理仅取告警（廉价且无副作用）
    prepareLines(content, filePath, &pf.warnings);

    return mergeParsedFile(std::move(pf));
}

bool ErbLoader::loadDirectory(const QString& dirPath, int depth) {
    if (depth > kMaxScanDepth) return true;
    QDir dir(dirPath);
    if (!dir.exists()) return false;

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
    QSet<QString> macros = m_preprocessor.macros();
    ErbPreprocessor::MacroTable macroTable = m_preprocessor.macroTable();
    for (const QString& h : headers) {
        macros.unite(ErbPreprocessor::collectDefines(readFileContent(h)));
        const ErbPreprocessor::MacroTable t = ErbPreprocessor::collectMacroTable(readFileContent(h));
        for (auto it = t.constBegin(); it != t.constEnd(); ++it) macroTable.insert(it.key(), it.value());
    }
    m_preprocessor.setMacros(macros);
    m_preprocessor.setMacroTable(macroTable);

    if (!m_parallel || files.size() < 2) {
        bool ok = true;
        for (const QString& f : files) {
            if (!loadFile(f, dirPath)) ok = false;
        }
        qDebug() << "[load] ERB（串行）" << dirPath << ":" << files.size() << "个文件,"
                 << "脚本" << m_scriptPaths.size() << "个,标签" << m_labels.size() << "个";
        return ok;
    }

    // 限制并行度（仅当显式指定时调整全局线程池）
    if (m_maxThreads > 0) {
        QThreadPool::globalInstance()->setMaxThreadCount(m_maxThreads);
    }

    QElapsedTimer timer;
    timer.start();
    qint64 tScan = 0, tParse = 0, tMerge = 0;

    // 1) 预扫描（并行）：**读+解码一次**，产出函数返回类型表 + 解码文本缓存；
    //    解析阶段复用该文本，避免第二次读盘/解码/编码嗅探。
    const QList<QPair<QString, FunctionTypes>> scans =
        QtConcurrent::blockingMapped(files, [this](const QString& f) {
            const QString content = readFileContent(f);
            return qMakePair(content, scanFunctionTypes(content));
        });
    FunctionTypes functionTypes;
    QHash<QString, QString> decoded;
    decoded.reserve(files.size());
    for (int i = 0; i < scans.size(); ++i) {
        decoded.insert(files.at(i), scans.at(i).first);
        const FunctionTypes& m = scans.at(i).second;
        for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
            functionTypes.insert(it.key(), it.value());
        }
    }

    tScan = timer.elapsed();

    // 2) 分块并行解析 + 主线程合并（限制内存峰值）
    const int chunk = m_chunkSize > 0 ? m_chunkSize : kDefaultChunkSize;
    bool ok = true;
    for (int base = 0; base < files.size(); base += chunk) {
        const QStringList slice = files.mid(base, chunk);
        QElapsedTimer pt; pt.start();
        QList<ParsedErbFile> parsed = QtConcurrent::blockingMapped(slice,
            [this, &functionTypes, &decoded, dirPath](const QString& f) {
                const QString content = decoded.value(f);   // 浅拷贝（隐式共享）
                return parseOneFile(f, functionTypes, &content, dirPath);
            });
        tParse += pt.elapsed();
        QElapsedTimer mt; mt.start();
        for (ParsedErbFile& pf : parsed) {
            if (!mergeParsedFile(std::move(pf))) ok = false;
        }
        tMerge += mt.elapsed();
        for (const QString& f : slice) decoded.remove(f);   // 释放本块已消费文本
    }
    qDebug() << "[ErbLoader] files:" << files.size()
             << "scan:" << tScan << "ms  parse:" << tParse << "ms  merge:" << tMerge << "ms"
             << " threadCount:" << QThreadPool::globalInstance()->maxThreadCount();
    return ok;
}

// ---------------------------------------------------------------------------
// 异步装载：后台 prepare + 分块解析，主线程合并（不阻塞 UI）
// ---------------------------------------------------------------------------
ErbLoader::LoadPrep ErbLoader::prepareLoad(const QString& dirPath, int depth) const {
    LoadPrep prep;
    const QStringList allFiles = collectFiles(dirPath, depth);
    if (allFiles.isEmpty()) {
        return prep;
    }
    // 头文件（.ERH）先行：其 #DIM 为全局、#DEFINE 定义宏
    QStringList headers, scripts;
    for (const QString& f : allFiles) {
        if (f.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive)) headers.append(f);
        else scripts.append(f);
    }
    prep.files = headers + scripts;

    // 头文件：#DEFINE 宏名与宏表来自同一份内容 —— 只读一次（以前 collectDefines
    // 与 collectMacroTable 各读一遍，同一个 .ERH 被解码两次）。
    for (const QString& h : headers) {
        const QString content = readFileContent(h);
        prep.macros.unite(ErbPreprocessor::collectDefines(content));
        const ErbPreprocessor::MacroTable t = ErbPreprocessor::collectMacroTable(content);
        for (auto it = t.constBegin(); it != t.constEnd(); ++it) prep.macroTable.insert(it.key(), it.value());
    }

    // 函数返回类型预扫描（只读）。此前是**单线程**顺序读全部 2200+ 个文件，
    // 是「点开目录后长时间没有任何反应」的主因之一；这里并行化（每文件仍只读一次）。
    // prepareLoad 本身已运行在线程池线程上，blockingMapped 会再切出 N 个子任务；
    // QThreadPool 默认允许多个线程，故线程数 >1 时不会自锁，==1 时退化为顺序。
    // ---- AST 磁盘缓存命中检查（对标 QML Disk Cache）----
    // 命中则**整库**读回并在本后台线程完成校验，随后跳过全部「读/解码/预处理/
    // 词法/语法」；未命中（含任一文件反序列化失败）删除坏缓存并回落正常解析。
    if (m_astDiskCache) {
        prep.cacheKey = ErbAstDiskCache::computeKey(dirPath, prep.files, int(m_readEncoding),
                                                   m_preprocessor.debugMode());
        ErbAstDiskCache::Reader reader(prep.cacheKey);
        qDebug().noquote() << "[ErbLoader] AST 缓存探测 key=" << prep.cacheKey.left(12)
                           << " open=" << reader.isOpen() << " count=" << reader.count()
                           << " files=" << prep.files.size();
        if (reader.isOpen() && reader.count() == int(prep.files.size())) {
            // 分批：顺序读一批 blob（纯 I/O + 拷贝）→ **并行**反序列化 → 释放该批
            // （单线程反序列化 ≈ 并行解析的耗时，必须并行才有收益；分批是为了
            // 不让 382MB 的 blob 与解析结果同时常驻）。
            const auto deser = [](const QByteArray& b) {
                ParsedErbFile pf;
                if (!ErbAstDiskCache::deserializeParsedFile(b, pf)) return ParsedErbFile();
                return pf;
            };
            const bool parallel = QThreadPool::globalInstance()->maxThreadCount() > 1;
            const int batch = 256;
            QList<ParsedErbFile> all;
            all.reserve(reader.count());
            bool okAll = true;
            while (okAll && reader.isOpen()) {
                QList<QByteArray> blobs;
                blobs.reserve(batch);
                for (int i = 0; i < batch; ++i) {
                    QByteArray b;
                    if (!reader.readBlob(b)) break;
                    blobs.append(std::move(b));
                }
                if (blobs.isEmpty()) break;
                QList<ParsedErbFile> part;
                if (parallel) {
                    part = QtConcurrent::blockingMapped(blobs, deser);
                } else {
                    part.reserve(blobs.size());
                    for (const QByteArray& b : blobs) part.append(deser(b));
                }
                if (part.size() != blobs.size()) { okAll = false; break; }
                for (ParsedErbFile& pf : part) all.append(std::move(pf));
            }
            for (int i = 0; okAll && i < all.size(); ++i) {
                if (all.at(i).path != prep.files.at(i)) okAll = false;   // 失败项 scriptName/path 为空
            }
            if (okAll && all.size() == prep.files.size()) {
                prep.cachedFiles = std::move(all);
                prep.cacheHit = true;
                prep.ok = true;
                return prep;
            }
            ErbAstDiskCache::remove(prep.cacheKey);   // 损坏：丢弃，重新解析
        }
    }

    // 读+解码**一次**，同时产出：(a) 函数返回类型表，(b) 解码后的源文本缓存。
    // 解析阶段复用 (b)，免去 eraTW 约 93MB 的第二次读取 + 解码 + 编码嗅探。
    const auto scanOne = [this](const QString& f) {
        const QString content = readFileContent(f);
        return qMakePair(content, scanFunctionTypes(content));
    };
    QList<QPair<QString, FunctionTypes>> scans;
    if (QThreadPool::globalInstance()->maxThreadCount() > 1) {
        scans = QtConcurrent::blockingMapped(prep.files, scanOne);
    } else {
        scans.reserve(prep.files.size());
        for (const QString& f : prep.files) scans.append(scanOne(f));
    }
    prep.decoded.reserve(prep.files.size());
    for (int i = 0; i < scans.size(); ++i) {
        prep.decoded.insert(prep.files.at(i), scans.at(i).first);
        const FunctionTypes& m = scans.at(i).second;
        for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
            prep.functionTypes.insert(it.key(), it.value());
        }
    }
    prep.ok = true;
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
                      << (m_async.fromCache ? QStringLiteral(" (AST 缓存命中)")
                                            : QStringLiteral(""));
    // AST 磁盘缓存：仅当本次**全部成功**且未命中（即本次是新解析）时才提交。
    if (m_async.cacheWriter) {
        if (ok) {
            if (!m_async.cacheWriter->commit()) qWarning() << "[ErbLoader] AST 缓存写入失败";
        } else {
            m_async.cacheWriter->abort();
        }
    }
    m_async = AsyncState{};
    emit loadCompleted(ok);
}

void ErbLoader::loadDirectoryAsync(const QString& dirPath, int depth) {
    if (m_async.active) {
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
    }

    connect(&m_prepWatcher, &QFutureWatcher<LoadPrep>::finished,
            this, &ErbLoader::onPrepFinished, Qt::UniqueConnection);
    connect(&m_chunkWatcher, &QFutureWatcher<ParsedErbFile>::finished,
            this, &ErbLoader::onChunkFinished, Qt::UniqueConnection);
    m_prepWatcher.setFuture(QtConcurrent::run([this, dirPath, depth] { return prepareLoad(dirPath, depth); }));
}

void ErbLoader::cancelLoad() {
    if (!m_async.active) return;
    m_async.cancelled = true;
}

void ErbLoader::onPrepFinished() {
    LoadPrep prep = m_prepWatcher.result();
    if (m_async.cancelled) {
        finishAsync(false);
        return;
    }
    if (!prep.ok) {
        finishAsync(false);
        return;
    }
    // _Rename.csv：未显式设置时按目录自动探测（对齐 C# 从 CsvDir 读取）
    if (m_preprocessor.renameMap().isEmpty()) {
        tryAutoLoadRename(m_async.dirPath);
    }
    m_preprocessor.setMacros(prep.macros);
    m_preprocessor.setMacroTable(prep.macroTable);
    m_asyncTypes = prep.functionTypes;
    m_async.files = prep.files;
    m_async.decoded = std::move(prep.decoded);   // 复用预扫描的解码文本
    m_async.total = prep.files.size();
    m_async.parsed = 0;
    m_async.merged = 0;

    // ---- AST 磁盘缓存命中：跳过解析，直接把读回的结果交给分批合并 ----
    if (prep.cacheHit) {
        m_async.fromCache = true;
        m_async.parsed = m_async.total;
        for (ParsedErbFile& pf : prep.cachedFiles) m_async.pending.append(std::move(pf));
        qInfo().noquote() << "[ErbLoader] AST 磁盘缓存命中（跳过解析）："
                          << m_async.total << "个文件，预扫描+读回"
                          << m_async.clock.elapsed() << "ms";
        emit loadProgress(0, m_async.total);
        mergeStep();
        return;
    }
    // AST 磁盘缓存未命中：边解析边把结果写盘（Writer 在 worker 侧已产出每个
    // 文件的 blob；这里只做顺序追加，装载成功后原子提交）。
    m_async.cacheKey = prep.cacheKey;
    if (m_astDiskCache && !prep.cacheKey.isEmpty()) {
        auto writer = std::make_unique<ErbAstDiskCache::Writer>(prep.cacheKey);
        if (writer->begin(m_async.total)) {
            m_async.cacheWriter = std::move(writer);
        } else {
            writer->abort();
        }
    }
    // 预扫描（列举 + 宏表 + 函数返回类型）已在后台完成：这里能看出它为 UI
    // 首帧前贡献了多少等待时间。
    qInfo().noquote() << "[ErbLoader] 异步预扫描完成(读文件+函数类型)"
                      << m_async.clock.elapsed() << "ms，" << m_async.total << "个文件";
    emit loadProgress(0, m_async.total);
    scheduleNextChunk();
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
    const FunctionTypes types = m_asyncTypes;
    // 复用预扫描已解码的源文本（m_async.decoded 在本次 mapped 运行期间只读，
    // 直到 onChunkFinished 才回收 —— 无并发写）。
    m_chunkWatcher.setFuture(QtConcurrent::mapped(slice,
        [this, types](const QString& f) {
            const QString content = m_async.decoded.value(f);   // 浅拷贝（隐式共享）
            return parseOneFile(f, types, &content, m_async.dirPath);
        }));
}

void ErbLoader::onChunkFinished() {
    if (m_async.cancelled) {
        finishAsync(false);
        return;
    }
    QList<ParsedErbFile> parsed = m_chunkWatcher.future().results();
    const int n = parsed.size();
    // 本块后台解析已完成：立即释放其解码文本（解析阶段用完即弃）
    const int base = m_async.parsed;
    for (int i = base; i < base + n && i < m_async.files.size(); ++i) {
        m_async.decoded.remove(m_async.files.at(i));
    }
    m_async.parsed += n;
    if (n == 0) {
        finishAsync(m_async.ok);
        return;
    }
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
    int n = 0;
    while (n < kMergeBatch && m_async.pendingPos < m_async.pending.size()) {
        ParsedErbFile& pf = m_async.pending[m_async.pendingPos];
        // AST 磁盘缓存：先把本文件的序列化 blob 落盘，再并入（merge 会清空 lines）。
        // blob 为空（序列化异常）说明缓存不完整 —— 直接放弃本次写盘，避免产出
        // 条目数不符的坏缓存。
        if (m_async.cacheWriter) {
            if (pf.cachedBlob.isEmpty() || !m_async.cacheWriter->writeBlob(pf.cachedBlob)) {
                m_async.cacheWriter->abort();
                m_async.cacheWriter.reset();
            }
        }
        if (!mergeParsedFile(std::move(pf))) m_async.ok = false;
        ++m_async.pendingPos;
        ++n;
    }
    m_async.mergeMs += mergeClock.elapsed();
    m_async.merged += n;
    emit loadProgress(m_async.merged, m_async.total);

    if (m_async.pendingPos < m_async.pending.size()) {
        // 还有待并：让主线程回事件循环后再继续（保持 UI 响应）
        QMetaObject::invokeMethod(this, &ErbLoader::mergeStep, Qt::QueuedConnection);
        return;
    }
    m_async.pending.clear();
    m_async.pendingPos = 0;
    // 本块全部并入：继续下一块的解析
    QMetaObject::invokeMethod(this, &ErbLoader::scheduleNextChunk, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// 串行 AST 构建（loadFile 路径）
// ---------------------------------------------------------------------------
QList<LogicalLine> ErbLoader::buildAst(const QString& content, const QString& filePath,
                                      const QString& root, ParseDiagnostics* diagnostics) {
    QList<LogicalLine> logicalLines;
    const QString scriptName = scriptNameFor(root, filePath);

    const AstResolver resolve = [this](const QString& expr) -> QSharedPointer<ExpressionNode> {
        return m_parseTable ? m_parseTable->expressionAst(expr) : nullptr;
    };
    // 赋值右值的临时归约：静默（见 AstBuilder::build 的 resolveQuiet）
    const AstResolver quietResolve = [this](const QString& expr) -> QSharedPointer<ExpressionNode> {
        return m_parseTable ? m_parseTable->expressionAst(expr, /*quiet*/ true) : nullptr;
    };

    QStringList warnings;
    const QList<ErbSourceLine> source = prepareLines(content, filePath, &warnings);
    logicalLines.reserve(source.size());
    for (int i = 0; i < source.size(); ++i) {
        const ScriptPosition pos(filePath, source.at(i).physicalLine, 1);
        LogicalLine line = AstBuilder::build(source.at(i).text, pos, resolve, quietResolve, diagnostics);
        line.lineIndex = i;
        logicalLines.append(line);
        emit parseLineReady(scriptName, source.at(i).physicalLine, source.at(i).text);
    }
    return logicalLines;
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

const LogicalLine* ErbLoader::findLabel(const QString& labelName) const {
    const auto it = m_labels.constFind(labelName);
    if (it == m_labels.constEnd()) return nullptr;
    const int index = it.value().second;
    if (m_parseTable) {
        const ScriptData* sd = m_parseTable->script(it.value().first);
        if (!sd || index < 0 || index >= sd->lines.size()) return nullptr;
        return &sd->lines.at(index);
    }
    const auto scriptIt = m_logicalLines.constFind(it.value().first);
    if (scriptIt == m_logicalLines.constEnd()) return nullptr;
    if (index < 0 || index >= scriptIt.value().size()) return nullptr;
    return &scriptIt.value().at(index);
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

int ErbLoader::getLabelPosition(const QString& scriptName, const QString& labelName) {
    return m_labelPositions.value(scriptName + ":" + labelName, -1);
}

int ErbLoader::getScriptLineCount(const QString& scriptName) {
    return m_scriptLineCounts.value(scriptName, 0);
}

QString ErbLoader::getScriptPath(const QString& scriptName) const {
    return m_scriptPaths.value(scriptName, QString());
}
