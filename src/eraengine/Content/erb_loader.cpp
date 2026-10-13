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

ErbLoader::ErbLoader(QObject* parent) : QObject(parent), m_parseTable(nullptr) {}

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
    if (GamePaths::isContent(dirPath)) return; // EraEngine loads sibling CSV explicitly
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
    if (dirPath.isEmpty() || depth > kMaxScanDepth) return out;

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
        out.append(collectFiles(GamePaths::join(dirPath, d), depth + 1));
    }

    QFileInfoList files = dir.entryInfoList(
        QStringList{"*.ERB", "*.erb", "*.ERH", "*.erh"},
        QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::NoSort);
    std::sort(files.begin(), files.end(),
              [&ordinalIgnoreCaseLess](const QFileInfo& x, const QFileInfo& y) {
                  return ordinalIgnoreCaseLess(x.fileName(), y.fileName());
              });
    for (const QFileInfo& fi : files) {
        out.append(GamePaths::join(dirPath, fi.fileName()));
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

OperandType LoadTimeVarTypes::typeOf(const QString& name, const QString& function) const {
    const QString key = name.toUpper();
    if (!function.isEmpty()) {
        const auto fit = localVars.constFind(function.toUpper());
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
    bool quiet, const LoadTimeVarTypes* varTypes, const QString& scope) const {
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
    const AstResolver self = [this, &types, varTypes, &scope](const QString& e) {
        return resolveExpr(e, types, /*quiet=*/false, varTypes, scope);
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
    QSharedPointer<ExpressionNode> ast = parser.parse(tokens);
    return ast;
}

// ---------------------------------------------------------------------------
// 预扫描：收集用户变量类型（#DIM/#DIMS/#GLOBAL/#GLOBALS/#PRIVATE）。
// ---------------------------------------------------------------------------
// 规则与 EraParseTable::loadScript + parseVariableDeclaration 保持一致：
//   * .ERH：文件级 # 行一律是广域变量；
//   * .ERB：只有**紧跟函数标签**（中间只允许空行/注释/# 行）的 # 行才是声明，
//     否则 loadScript 会告警并忽略 —— 这里也必须忽略，否则解析期会比最终
//     登记多出一批类型，反而与运行期不一致；
//   * `#DIM GLOBAL X` / `#GLOBAL X` / `#GLOBALS X` / `#PRIVATE X` 都是广域。
void ErbLoader::preRegisterDeclarations(const QStringList& files,
                                        const QHash<QString, QString>& decoded,
                                        const QSet<QString>& headers) {
    if (!m_parseTable) return;
    m_parseTable->beginDeclarationPreRegistration();
    for (const QString& f : files) {
        const bool isHeader = headers.contains(f);
        QStringList warnings;
        // 关键：按**预处理后**的行登记（与 AstBuilder 所见逐字一致），
        // 否则 [SKIPSTART]/#DEFINE/_Rename 会让声明与最终登记不一致。
        const QList<ErbSourceLine> src = m_preprocessor.process(decoded.value(f), &warnings, f);
        QString currentFunction;
        bool afterLabel = false;
        for (const ErbSourceLine& sl : src) {
            const QString t = sl.text.trimmed();
            if (t.isEmpty() || t.startsWith(QLatin1Char(';'))) continue;   // 空行/注释不结束声明区
            if (t.startsWith(QLatin1Char('@'))) {
                // 标签名 = 首个标识符（与 AstBuilder::build 同一判据），
                // 作用域键必须逐字一致：空格 ( , [ : = 都截断。
                const QString label = t.mid(1);
                int cut = label.size();
                for (int k = 0; k < label.size(); ++k) {
                    const QChar c = label.at(k);
                    if (c.isSpace() || c == QLatin1Char('(') || c == QLatin1Char(',')
                        || c == QLatin1Char('[') || c == QLatin1Char(':') || c == QLatin1Char('=')) {
                        cut = k; break;
                    }
                }
                const QString name = label.left(cut).trimmed();
                if (!name.isEmpty()) currentFunction = name;
                afterLabel = true;
                continue;
            }
            if (t.startsWith(QLatin1Char('#'))) {
                if (!isHeader && !afterLabel) continue;   // 函数声明之外：loadScript 忽略
                m_parseTable->preRegisterDeclaration(sl.text, ScriptPosition(f, sl.physicalLine, 1),
                                                     currentFunction, isHeader);
                continue;
            }
            afterLabel = false;   // 普通指令行：声明区结束
        }
    }
    m_parseTable->finishDeclarationPreRegistration();
}

void ErbLoader::scanVariableTypes(const QString& content, bool isHeaderFile,
                                  LoadTimeVarTypes& out) const {
    const QStringList lines = content.split(QLatin1Char('\n'));
    QString currentFunction;
    bool afterLabel = false;
    for (const QString& raw : lines) {
        const QString t = raw.trimmed();
        // 空行 / 注释（Null 行）：不影响 afterLabel
        if (t.isEmpty() || t.startsWith(QLatin1Char(';'))) continue;

        if (t.startsWith(QLatin1Char('@'))) {
            // 标签名 = '@' 之后到 '(' 或空白为止（eraTW 里 `@FUNC ...` / `@FUNC(ARGS)`）
            QString label = t.mid(1);
            int cut = label.size();
            for (int k = 0; k < label.size(); ++k) {
                const QChar c = label.at(k);
                if (c == QLatin1Char('(') || c.isSpace() || c == QLatin1Char(';')) { cut = k; break; }
            }
            label = label.left(cut).trimmed();
            if (!label.isEmpty()) currentFunction = label.toUpper();
            afterLabel = true;
            continue;
        }

        if (t.startsWith(QLatin1Char('#'))) {
            if (!isHeaderFile && !afterLabel) continue;   // 函数声明之外：loadScript 忽略
            QString s = t.mid(1);
            int i = 0;
            while (i < s.size() && !s.at(i).isSpace()) ++i;
            const QString directive = s.left(i).toUpper();

            bool isStr = false;
            bool isGlobal = false;
            if (directive == QLatin1String("DIM")) {
                // 函数内私有 / 文件级广域（由作用域决定）
            } else if (directive == QLatin1String("DIMS")) {
                isStr = true;
            } else if (directive == QLatin1String("GLOBAL")) {
                isGlobal = true;
            } else if (directive == QLatin1String("GLOBALS")) {
                isGlobal = true;
                isStr = true;
            } else if (directive == QLatin1String("PRIVATE")) {
                isGlobal = true;
            } else {
                continue;   // #FUNCTION/#SINGLE/… 不是变量声明
            }

            QString rest = s.mid(i);
            // 去掉行尾注释（`#DIM X = 5 ; 说明`）；引号内的 ';' 不算注释
            bool inQuote = false;
            for (int k = 0; k < rest.size(); ++k) {
                const QChar c = rest.at(k);
                if (c == QLatin1Char('\\') && k + 1 < rest.size()) { ++k; continue; }
                if (c == QLatin1Char('"')) { inQuote = !inQuote; continue; }
                if (c == QLatin1Char(';') && !inQuote) { rest = rest.left(k); break; }
            }
            rest = rest.trimmed();
            if (rest.isEmpty()) continue;

            // 名字与类型用与 loadScript 同一个解析器，避免两套规则漂移
            UserDefinedVariableData d;
            try {
                d = UserDefinedVariableData::create(rest, isStr, /*isPrivate=*/true, ScriptPosition());
            } catch (const std::exception&) {
                continue;   // 非法声明：loadScript 那边会报错，这里只是不提供类型
            }
            if (d.name.isEmpty()) continue;

            const OperandType type = d.typeIsStr ? OperandType::Str : OperandType::Int;
            const bool global = isHeaderFile || isGlobal || d.global || currentFunction.isEmpty();
            if (global) out.globalVars.insert(d.name.toUpper(), type);
            else out.localVars[currentFunction].insert(d.name.toUpper(), type);
            continue;
        }

        afterLabel = false;   // 普通指令行：声明区结束
    }
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
ParsedErbFile ErbLoader::parseOneFile(const QString& filePath, const FunctionTypes& types,
                                    const QString* cachedContent, const QString& root,
                                    const LoadTimeVarTypes* varTypes) const {
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
        int currentPhysicalLine = 0;
        // 当前函数作用域（#DIM/#DIMS 私有变量的类型查询要用）。与 loadScript
        // 同一规则：遇到函数标签行就切换；本行自身的归约用**切换前**的作用域
        //（标签行的实参列表不是表达式，无影响）。
        QString currentFunction;
        const AstResolver resolver = [this, &types, &pf, &exprFailed, &currentPhysicalLine,
                                     &filePath, &currentFunction, varTypes](const QString& e) {
            auto ast = resolveExpr(e, types, /*quiet=*/false, varTypes, currentFunction);
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
        // 赋值右值的临时归约：静默（字符串赋值随后由 StrFormParser 重新解释）
        const AstResolver quietResolver = [this, &types, &pf, &currentFunction, varTypes](const QString& e) {
            return resolveExpr(e, types, /*quiet*/ true, varTypes, currentFunction);
        };
        for (int i = 0; i < source.size(); ++i) {
            currentPhysicalLine = source.at(i).physicalLine;
            const ScriptPosition pos(filePath, source.at(i).physicalLine, 1);
            // 左值「已声明类型」：取自预扫描类型表（含本文件/跨文件/私有作用域）。
            // 命中 Str 的行在构建期即编译为 StrForm；未命中的标记待收尾。
            const auto declaredType = [this, varTypes, &currentFunction](const QString& name) -> OperandType {
                if (m_parseTable) {
                    const OperandType t = m_parseTable->variableTable().typeOf(name, currentFunction);
                    if (t != OperandType::Unknown) return t;
                }
                return varTypes ? varTypes->typeOf(name, currentFunction) : OperandType::Unknown;
            };
            LogicalLine line = AstBuilder::build(source.at(i).text, pos, resolver, quietResolver,
                                                  &pf.diagnostics, declaredType);
            line.lineIndex = i;
            // 作用域切换在构建**之后**（与 loadScript 的顺序一致：先记 ownerFunction
            // 再在遇到标签行时切换 currentFunction）。
            if (line.kind == LineKind::FunctionLabel) currentFunction = line.labelName;
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
        const bool loaded = m_parseTable->loadScript(
            pf.scriptName, pf.lines,
            pf.path.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive), pf.path);
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
    // 同一次读取里同时产出函数返回类型与**用户变量类型**（见 LoadTimeVarTypes）：
    // 声明必须早于 AST 构建，否则装载期所有用户变量都是「类型未知」。
    QSet<QString> headerSet;
    for (const QString& f : files) {
        if (f.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive)) headerSet.insert(f);
    }
    QList<QPair<QString, FunctionTypes>> scans;
    LoadTimeVarTypes varTypes;
    {
        struct SyncScan { QString content; FunctionTypes functionTypes; LoadTimeVarTypes varTypes; };
        const QList<SyncScan> s2 =
            QtConcurrent::blockingMapped(files, [this, &headerSet](const QString& f) {
                SyncScan scan;
                scan.content = readFileContent(f);
                scan.functionTypes = scanFunctionTypes(scan.content);
                scanVariableTypes(scan.content, headerSet.contains(f), scan.varTypes);
                return scan;
            });
        scans.reserve(s2.size());
        for (int i = 0; i < s2.size(); ++i) {
            scans.append(qMakePair(s2.at(i).content, s2.at(i).functionTypes));
            mergeVarTypes(varTypes, s2.at(i).varTypes);
        }
    }
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

    // decoded 接管源文本；扫描结果不能继续持有已消费文件的共享字符串。
    scans.clear();
    tScan = timer.elapsed();

    // 「先登记 #DIM/#DIMS，再构造 AST」：按预处理后的行登记声明 + 结算，之后
    // 并行构建时解析器即可拿到完整用户变量类型。
    preRegisterDeclarations(files, decoded, headerSet);


    // 2) 分块并行解析 + 主线程合并（限制内存峰值）
    const int chunk = m_chunkSize > 0 ? m_chunkSize : kDefaultChunkSize;
    bool ok = true;
    for (int base = 0; base < files.size(); base += chunk) {
        const QStringList slice = files.mid(base, chunk);
        QElapsedTimer pt; pt.start();
        QList<ParsedErbFile> parsed = QtConcurrent::blockingMapped(slice,
            [this, &functionTypes, &varTypes, &decoded, dirPath](const QString& f) {
                const QString content = decoded.value(f);   // 浅拷贝（隐式共享）
                return parseOneFile(f, functionTypes, &content, dirPath, &varTypes);
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
    // 预扫描的单文件结果（函数返回类型 + 用户变量类型 + 已解码文本）
    struct FileScan {
        QString content;
        FunctionTypes functionTypes;
        LoadTimeVarTypes varTypes;
    };
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
    const auto scanOne = [this, &headerSet](const QString& f) {
        FileScan scan;
        scan.content = readFileContent(f);
        scan.functionTypes = scanFunctionTypes(scan.content);
        scanVariableTypes(scan.content, headerSet.contains(f), scan.varTypes);
        return scan;
    };
    QList<FileScan> scans;
    if (QThreadPool::globalInstance()->maxThreadCount() > 1) {
        scans = QtConcurrent::blockingMapped(filesForScan, scanOne);
    } else {
        scans.reserve(prep.files.size());
        for (const QString& f : filesForScan) scans.append(scanOne(f));
    }
    prep.decoded.reserve(prep.files.size());
    for (int i = 0; i < scans.size(); ++i) {
        prep.decoded.insert(prep.files.at(i), scans.at(i).content);
        const FunctionTypes& m = scans.at(i).functionTypes;
        for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
            prep.functionTypes.insert(it.key(), it.value());
        }
        mergeVarTypes(prep.varTypes, scans.at(i).varTypes);
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
                      << " 主线程合并" << m_async.mergeMs << "ms";
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

void ErbLoader::loadDirectoryAsync(const QString& dirPath, int depth) {
    if (m_async.active) {
        return;
    }
    if (dirPath.isEmpty() || !QFileInfo(dirPath).isDir()) {
        qWarning() << "[load] ERB 目录无效:" << dirPath;
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
    }

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
    m_async.cancelled = true;
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
    m_scriptPaths.clear();
    m_labels.clear();
    m_labelPositions.clear();
    m_scriptLineCounts.clear();
}

void ErbLoader::onPrepFinished() {
    if (m_async.cancelled) {
        finishAsync(false);
        return;
    }
    LoadPrep prep = m_prepFuture.takeResult();
    m_prepWatcher->setFuture(QFuture<LoadPrep>());
    m_prepFuture = {};
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
    m_asyncVarTypes = prep.varTypes;   // 解析期只读的用户变量类型快照
    m_async.files = prep.files;
    m_async.decoded = std::move(prep.decoded);   // 复用预扫描的解码文本
    m_async.total = prep.files.size();
    {
        // 先登记 #DIM/#DIMS（按预处理后的行），再让分块并行构建 AST。
        QSet<QString> headerSet;
        for (const QString& f : m_async.files)
            if (f.endsWith(QLatin1String(".erh"), Qt::CaseInsensitive)) headerSet.insert(f);
        preRegisterDeclarations(m_async.files, m_async.decoded, headerSet);
    }
    m_async.parsed = 0;
    m_async.merged = 0;

    // 预扫描（列举 + 宏表 + 函数返回类型）已在后台完成：这里能看出它为 UI
    // 首帧前贡献了多少等待时间。
    qInfo().noquote() << "[ErbLoader] 异步预扫描完成(读文件+函数/变量类型)"
                      << m_async.clock.elapsed() << "ms，" << m_async.total << "个文件";
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
    const FunctionTypes types = m_asyncTypes;
    // 用户变量类型快照同样是「本次 mapped 运行期间只读」，按值捕获
    //（QHash 隐式共享，拷贝很便宜）。
    const LoadTimeVarTypes varTypes = m_asyncVarTypes;
    // 复用预扫描已解码的源文本（m_async.decoded 在本次 mapped 运行期间只读，
    // 直到 onChunkFinished 才回收 —— 无并发写）。
    m_chunkFuture = QtConcurrent::mapped(slice,
        [this, types, varTypes](const QString& f) {
            const QString content = m_async.decoded.value(f);   // 浅拷贝（隐式共享）
            return parseOneFile(f, types, &content, m_async.dirPath, &varTypes);
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
        if (!mergeParsedFile(std::move(pf))) m_async.ok = false;
        ++m_async.pendingPos;
        ++n;
    }
    m_async.mergeMs += mergeClock.elapsed();
    m_async.merged += n;
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
        const auto declaredType = [this](const QString& name) -> OperandType {
            return m_parseTable ? m_parseTable->variableTable().typeOf(name, QString())
                                : OperandType::Unknown;
        };
        LogicalLine line = AstBuilder::build(source.at(i).text, pos, resolve, quietResolve, diagnostics,
                                             declaredType);
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
