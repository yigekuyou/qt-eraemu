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
#include "ast/expression_lexer.h"
#include "ast/expression_parser.h"
#include "ast/function_types.h"
#include "ast/strform_parser.h"
#include "constant_table.h"
#include "text_encoding.h"

#include <QDir>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>
#include <QElapsedTimer>
#include <QDebug>
#include <QSet>
#include <QFutureWatcher>

ErbLoader::ErbLoader(QObject* parent) : QObject(parent), m_parseTable(nullptr) {}

QString ErbLoader::readFileContent(const QString& filePath) const {
    // 编码按文件嗅探（BOM → UTF-8 → Shift-JIS → Latin-1）：
    // 同一游戏目录里 UTF-8 的汉化 ERB 与 Shift-JIS 的原始 ERB 可以共存。
    TextEncoding detected = TextEncoding::Auto;
    return TextCodecUtil::readFile(filePath, m_readEncoding, &detected, nullptr);
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
    const QString text = TextCodecUtil::readFile(filePath, m_readEncoding, nullptr, &ok);
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
    if (depth > 5) return out;

    QDir dir(dirPath);
    if (!dir.exists()) return out;

    const QFileInfoList files = dir.entryInfoList(
        QStringList{"*.ERB", "*.erb", "*.ERH", "*.erh"},
        QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : files) {
        out.append(fi.absoluteFilePath());
    }

    const QStringList dirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& d : dirs) {
        out.append(collectFiles(dirPath + "/" + d, depth + 1));
    }
    return out;
}

// ---------------------------------------------------------------------------
// 线程内表达式归约（本地缓存 + 只读函数类型表）
// ---------------------------------------------------------------------------
QSharedPointer<ExpressionNode> ErbLoader::resolveExpr(
    const QString& expr, const FunctionTypes& types,
    QHash<QString, QSharedPointer<ExpressionNode>>& cache) const {
    const QString key = expr.trimmed();
    if (key.isEmpty()) return nullptr;

    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();

    ExpressionLexer lexer;
    const QList<ExpressionToken> tokens = lexer.tokenize(key, 1);
    if (tokens.isEmpty()) return nullptr;

    ExpressionParser parser;
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
ParsedErbFile ErbLoader::parseOneFile(const QString& filePath, const FunctionTypes& types) const {
    ParsedErbFile pf;
    pf.path = filePath;
    pf.scriptName = QFileInfo(filePath).baseName();

    const QString content = readFileContent(filePath);
    if (content.isEmpty()) return pf;

    const QList<ErbSourceLine> source = prepareLines(content, filePath, &pf.warnings);
    pf.lines.reserve(source.size());

    const AstResolver resolver = [this, &types, &pf](const QString& e) {
        return resolveExpr(e, types, pf.astCache);
    };
    for (int i = 0; i < source.size(); ++i) {
        const ScriptPosition pos(filePath, source.at(i).physicalLine, 1);
        LogicalLine line = AstBuilder::build(source.at(i).text, pos, resolver);
        line.lineIndex = i;
        pf.lines.append(line);
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

    emit parseStarted(pf.scriptName);

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
        // 不合并 worker 缓存：行内 AST 已自带，运行期新表达式按需再解析（懒加载）
        pf.astCache.clear();
        pf.lines.clear();
    } else {
        m_logicalLines.insert(pf.scriptName, pf.lines);
    }

    emit parseFinished(pf.scriptName);
    return true;
}

// ---------------------------------------------------------------------------
// 装载入口
// ---------------------------------------------------------------------------
bool ErbLoader::loadFile(const QString& filePath) {
    const QString content = readFileContent(filePath);
    if (content.isEmpty()) return false;

    ParsedErbFile pf;
    pf.scriptName = QFileInfo(filePath).baseName();
    pf.path = filePath;
    pf.lines = buildAst(content, filePath);   // 串行路径：复用 parseTable 缓存
    // buildAst 的预处理告警：再次预处理仅取告警（廉价且无副作用）
    prepareLines(content, filePath, &pf.warnings);

    return mergeParsedFile(std::move(pf));
}

bool ErbLoader::loadDirectory(const QString& dirPath, int depth) {
    if (depth > 5) return true;
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
    for (const QString& h : headers) {
        macros.unite(ErbPreprocessor::collectDefines(readFileContent(h)));
    }
    m_preprocessor.setMacros(macros);

    if (!m_parallel || files.size() < 2) {
        bool ok = true;
        for (const QString& f : files) {
            if (!loadFile(f)) ok = false;
        }
        return ok;
    }

    // 限制并行度（仅当显式指定时调整全局线程池）
    if (m_maxThreads > 0) {
        QThreadPool::globalInstance()->setMaxThreadCount(m_maxThreads);
    }

    QElapsedTimer timer;
    timer.start();
    qint64 tScan = 0, tParse = 0, tMerge = 0;

    // 1) 预扫描（并行）：函数返回类型表（只读，供表达式强类型）
    const QList<FunctionTypes> scans = QtConcurrent::blockingMapped(files, [this](const QString& f) {
        return scanFunctionTypes(readFileContent(f));
    });
    FunctionTypes functionTypes;
    for (const FunctionTypes& m : scans) {
        for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
            functionTypes.insert(it.key(), it.value());
        }
    }

    tScan = timer.elapsed();

    // 2) 分块并行解析 + 主线程合并（限制内存峰值）
    const int chunk = m_chunkSize > 0 ? m_chunkSize : 64;
    bool ok = true;
    for (int base = 0; base < files.size(); base += chunk) {
        const QStringList slice = files.mid(base, chunk);
        QElapsedTimer pt; pt.start();
        QList<ParsedErbFile> parsed = QtConcurrent::blockingMapped(slice,
            [this, &functionTypes](const QString& f) {
                return parseOneFile(f, functionTypes);
            });
        tParse += pt.elapsed();
        QElapsedTimer mt; mt.start();
        for (ParsedErbFile& pf : parsed) {
            if (!mergeParsedFile(std::move(pf))) ok = false;
        }
        tMerge += mt.elapsed();
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

    for (const QString& h : headers) {
        prep.macros.unite(ErbPreprocessor::collectDefines(readFileContent(h)));
    }
    // 函数返回类型预扫描（只读，线程内）
    for (const QString& f : prep.files) {
        const FunctionTypes m = scanFunctionTypes(readFileContent(f));
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

void ErbLoader::loadDirectoryAsync(const QString& dirPath, int depth) {
    if (m_async.active) {
        return;
    }
    m_async = AsyncState{};
    m_async.active = true;
    m_async.ok = true;
    m_async.dirPath = dirPath;
    m_async.depth = depth;
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
    const LoadPrep prep = m_prepWatcher.result();
    if (m_async.cancelled) {
        m_async = AsyncState{};
        emit loadCompleted(false);
        return;
    }
    if (!prep.ok) {
        m_async = AsyncState{};
        emit loadCompleted(false);
        return;
    }
    // _Rename.csv：未显式设置时按目录自动探测（对齐 C# 从 CsvDir 读取）
    if (m_preprocessor.renameMap().isEmpty()) {
        tryAutoLoadRename(m_async.dirPath);
    }
    m_preprocessor.setMacros(prep.macros);
    m_asyncTypes = prep.functionTypes;
    m_async.files = prep.files;
    m_async.total = prep.files.size();
    m_async.processed = 0;
    emit loadProgress(0, m_async.total);
    scheduleNextChunk();
}

void ErbLoader::scheduleNextChunk() {
    if (m_async.cancelled) {
        m_async = AsyncState{};
        emit loadCompleted(false);
        return;
    }
    const int chunk = m_chunkSize > 0 ? m_chunkSize : 64;
    if (m_async.processed >= m_async.total) {
        const bool ok = m_async.ok;
        m_async = AsyncState{};
        emit loadCompleted(ok);
        return;
    }
    const QStringList slice = m_async.files.mid(m_async.processed, chunk);
    if (slice.isEmpty()) {
        const bool ok = m_async.ok;
        m_async = AsyncState{};
        emit loadCompleted(ok);
        return;
    }
    const FunctionTypes types = m_asyncTypes;
    m_chunkWatcher.setFuture(QtConcurrent::mapped(slice,
        [this, types](const QString& f) { return parseOneFile(f, types); }));
}

void ErbLoader::onChunkFinished() {
    if (m_async.cancelled) {
        m_async = AsyncState{};
        emit loadCompleted(false);
        return;
    }
    QList<ParsedErbFile> parsed = m_chunkWatcher.future().results();
    const int n = parsed.size();
    if (!mergeChunk(std::move(parsed))) {
        m_async.ok = false;
    }
    m_async.processed += n;
    emit loadProgress(m_async.processed, m_async.total);
    if (n == 0) {
        const bool ok = m_async.ok;
        m_async = AsyncState{};
        emit loadCompleted(ok);
        return;
    }
    // 让主线程回到事件循环后再做下一块（保持 UI 响应）
    QMetaObject::invokeMethod(this, &ErbLoader::scheduleNextChunk, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// 串行 AST 构建（loadFile 路径）
// ---------------------------------------------------------------------------
QList<LogicalLine> ErbLoader::buildAst(const QString& content, const QString& filePath) {
    QList<LogicalLine> logicalLines;
    const QString scriptName = QFileInfo(filePath).baseName();

    const AstResolver resolve = [this](const QString& expr) -> QSharedPointer<ExpressionNode> {
        return m_parseTable ? m_parseTable->expressionAst(expr) : nullptr;
    };

    QStringList warnings;
    const QList<ErbSourceLine> source = prepareLines(content, filePath, &warnings);
    logicalLines.reserve(source.size());
    for (int i = 0; i < source.size(); ++i) {
        const ScriptPosition pos(filePath, source.at(i).physicalLine, 1);
        LogicalLine line = AstBuilder::build(source.at(i).text, pos, resolve);
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
