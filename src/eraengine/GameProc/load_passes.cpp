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
#include "load_passes.h"

#include <QCoreApplication>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <utility>

#include "ast/argument_parser.h"
#include "ast/ast_builder.h"
#include "ast/expression_ast.h"
#include "ast/expression_evaluator.h"
#include "ast/expression_lexer.h"
#include "ast/expression_parser.h"
#include "ast/function_types.h"
#include "ast/name_interner.h"
#include "ast/print_template.h"
#include "ast/strform_parser.h"
#include "ast/variable_table.h"
#include "constant_table.h"
#include "variable_storage.h"

// ---------------------------------------------------------------------------
// 装载收尾 1/2/3 号 pass 的实现（从 era_parse_table.cpp 整体搬出）。
//
// 搬家原则：逐字搬运 —— 不改逻辑、不改常量、不改诊断文案、不合并步骤、
// 不重排循环。唯一的变化是数据入口：解析表的成员 -> LoadContext 字段。
// ---------------------------------------------------------------------------
namespace loadpass {
namespace {

// 并行化开关：任务数 >1 且线程池 >1 时启用（==1 时顺序执行，避免自锁 ——
// 绑定/校验在调用方线程（装载收尾）执行，blockingMap 会再切子任务）。
inline bool canParallelize(int n) {
    return n > 1 && QThreadPool::globalInstance()->maxThreadCount() > 1;
}
}  // namespace

// ---------------------------------------------------------------------------
// 表达式解析（装载期唯一流水线）
// ---------------------------------------------------------------------------

QSharedPointer<ExpressionNode> parseScopedExpression(const LoadContext& ctx, const QString& expr,
                                                     const QString& scope, bool quiet,
                                                     bool foldConstants) {
    const QString key = expr.trimmed();
    if (key.isEmpty()) {
        return nullptr;
    }

    ExpressionLexer lexer;
    const QList<ExpressionToken> tokens = lexer.tokenize(key, 1);
    if (tokens.isEmpty()) {
        return nullptr;
    }

    ExpressionParser parser;
    parser.setQuiet(quiet);
    parser.setVariableTypeProvider([ctx, scope](const QString& name) { return ctx.variables.typeOf(name, scope); });   // 赋值右值的临时解析：不刷「表达式语法错误」
    // 强类型：仅用户自定义函数由 Provider 决定；内置函数（内部命令）由
    // ExpressionParser 内部的 kBuiltinFunctions 目录解析（对齐 C# methodDic）。
    // 装载期间用户函数可能尚未 merge，markLoadComplete 会再统一重绑一次。
    parser.setFunctionTypeProvider([ctx](const QString& name) -> OperandType {
        // #FUNCTIONS -> Str；#FUNCTION -> Int；无 # 行的 @label 按 Int（宽容语义）。
        // 与内置函数同名的、无 #FUNCTION 的 @label **不覆盖**内置函数（见 userCallReturnType）。
        return userCallReturnType(ctx.functions, name);
    });

    // 格式化串：@"..." / \@...#...\@
    parser.setFormProvider([ctx, scope](const QString& text, bool yenAt) -> QSharedPointer<ExpressionNode> {
        const AstResolver resolve = [ctx, scope](const QString& e) { return parseScopedExpression(ctx, e, scope, false, true); };
        if (yenAt) return StrFormParser::parseYenAt(text, resolve, ctx.evaluator && ctx.evaluator->ignoreTripleSymbols());
        return QSharedPointer<ExpressionNode>(StrFormParser::parse(text, resolve, ctx.evaluator && ctx.evaluator->ignoreTripleSymbols()));
    });
    if (ctx.constantTable) {
        const ConstantTable* ct = ctx.constantTable;
        parser.setConstantNameProvider([ct](const QString& var, const QString& name) {
            return ct->indexForVariable(var, name) >= 0;
        });
    }
    // `#DIM CONST NAME = value`：解析期折叠为字面量（与 CSV 常数名区分：这里返回**值**）。
    // 按 (名字, 作用域) 查常数表 —— 常数表本身按「归属作用域」分离（见
    // VariableTable），因此：
    //   * 函数内的私有常量只在本函数内折叠（C#：函数内 `#DIM CONST` 是该函数的
    //     私有常量，UserDefinedVariableData.Private）；
    //   * 本作用域的可写私有变量（`#DIM 色_RED7`）遮蔽别处的同名常量 ——
    //     否则会把这个可写变量的**左值**折成字面量（eraTW 的 CASINO 色_* 26 行
    //     就是这么整行报「赋值左值必须为有效变量项」并失效的）。
    // 运行期变量名解析（GETVAR 族）走 foldConstants=false：C# 的 ReduceExpressionTerm
    // 对常量也返回 VariableTerm，GETVAR 要能**识别**常量（见 runtimeVariableReference）。
    if (foldConstants) {
        const VariableTable* vt = &ctx.variables;
        parser.setConstantValueProvider([vt, scope](const QString& name) -> QVariant {
            qint64 iv = 0;
            if (vt->constInt(name, iv, scope)) return QVariant::fromValue<qint64>(iv);
            QString sv;
            if (vt->constStr(name, sv, scope)) return QVariant(sv);
            return QVariant();
        });
    }
    QSharedPointer<ExpressionNode> ast = parser.parse(tokens);
    if (ast) {
        // 立即按变量表定型（新解析的 AST 也保持强类型）。**带形参表**：
        // 「变量分词」的描述符在这里就算 —— 收尾定型（applyVariableTypes）之后
        // 还有赋值 AST 重建等再解析路径，那些节点不会再回定型趟，
        // 形参名必须在此处就判成 Dynamic（否则形参读会被标成私有数组）。
        VariableTable::applyTypes(*ast, ctx.variables, scope,
                                  paramNamesOf(ctx.paramNames, scope.toUpper()));
    }
    PrintTemplateCompiler::prepare(ast);
    return ast;
}


QSharedPointer<ExpressionNode> expressionAstInScope(const LoadContext& ctx, const QString& expr,
                                                    const QString& scope, bool quiet) {
    return parseScopedExpression(ctx, expr, scope, quiet, /*foldConstants=*/true);
}



// 每函数的形参名集合（大写）。按需构建并缓存；失效点 = clear()/loadScript
//（新脚本可能带来新的 @label 形参）。「变量分词」的装载期解析与收尾定型共用。
// 收尾定型（applyVariableTypes）的并行趟开始前会先建全 —— 定型期间只读不插。
const QSet<QString>* paramNamesOf(const QHash<QString, QSet<QString>>& paramNames,
                                  const QString& upperScope)
{
    // **只查不插**：并行装载的 worker（逐行解析表达式）与收尾定型趟
    //（按脚本并行）都会走到这里 —— 任何按需插入都是共享 QHash 上的竞态
    //（eraMegaten 2260 脚本并行装载实测崩溃）。集合在两处构建：
    //   · loadScript 注册函数时（与 ctx.functions.insert 同点、同线程纪律）；
    //   · applyVariableTypes 并行趟开始前（收尾前的兜底，覆盖一切）。
    // miss（函数未知/顶层空作用域）返回 nullptr —— 与「该函数没有形参」等价。
    const auto it = paramNames.constFind(upperScope);
    return it != paramNames.constEnd() ? &it.value() : nullptr;
}


// 「正在执行的那一行属于哪个函数」。装载期若尚未开始执行（脚本名为空 /
// 位置越界）返回空串 —— 此时只有广域常量可见（与旧成员实现同）。
QString executionScope(const QHash<QString, ScriptData>& scripts, const QString& scriptName,
                       int line) {
    const auto it = scripts.constFind(scriptName);
    if (it == scripts.constEnd() || line < 0 || line >= it.value().lines.size()) return QString();
    return it.value().lines.at(line).ownerFunction;
}


namespace {
// 用户函数表查询（与原成员实现的同一判据：大写键）。
const UserFunctionInfo* userFunction(const QHash<QString, UserFunctionInfo>& functions,
                                     const QString& name) {
    auto it = functions.constFind(name.toUpper());
    return it == functions.constEnd() ? nullptr : &it.value();
}
}  // namespace

// 用户函数在**表达式调用**里的返回类型；返回 Unknown 表示「按内置函数解析」。
//
// 对齐 C# IdentifierDictionary.GetFunctionMethod（装配顺序**无关**：标签表全局唯一，
// 装载完成后统一解析；重名标签在装载期另有告警）：
//   1) 用户 @label 优先于内置函数（methodDic）——所以同名时执行用户函数；
//   2) 但 1.721 起「#FUNCTION の無い関数は組み込み関数を上書きしない」：
//      没有 #FUNCTION/#FUNCTIONS 的同名 @label 只是普通流程函数，式子里的同名调用
//      仍走内置函数（C# 源码 'PANCTION.ERB 的 RAND とか'）。
//
// 本移植对「非内置名字 + 无 #FUNCTION 的 @label」保留宽容语义（eraTW 大量这样在式子里
// 调用普通标签，返回 Int）。此前该宽容语义也套用到了与内置函数同名的标签上
// （era_parse_table 侧一律返回 Int），而 erb_loader 侧的解析期类型表只收 #FUNCTION 标签
// ——同一个调用在「解析期」被当成内置函数、到 finalize 又被重绑成用户函数，
// 两步结论不一致。现在两处都走本函数。
OperandType userCallReturnType(const QHash<QString, UserFunctionInfo>& functions,
                        const QString& name) {
    const UserFunctionDecl* fn = userFunction(functions, name);
    if (!fn) return OperandType::Unknown;
    if (fn->isMethod) return isKnown(fn->returnType) ? fn->returnType : OperandType::Int;
    // 无 #FUNCTION：#FUNCTION が無い関数は組み込み関数を上書きしない
    // 内置表走 O(1) 索引；toStdString() 的堆分配只在「既非内置、又可能是扩展」
    // 时付（findExtensionFunction 的键类型是 std::string）。
    const QString upper = name.toUpper();
    if (builtinFunctionIndex(upper) >= 0) return OperandType::Unknown;
    if (findExtensionFunction(upper.toStdString()) != nullptr) return OperandType::Unknown;
    return OperandType::Int;
}

// ---------------------------------------------------------------------------
// 行/声明文本工具（装载期解析与收尾共用）
// ---------------------------------------------------------------------------

// 去掉指令行的 ';' 注释（尊重引号）；#DIM CONST X = 0 ;说明 需要它
QString stripDirectiveComment(const QString& text) {
    QChar quote;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (!quote.isNull()) {
            if (c == quote) quote = QChar();
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; continue; }
        if (c == QLatin1Char(';')) return text.left(i);
    }
    return text;
}


namespace {

// 变量名：与 #DIM 同一判据 —— 不以数字开头、除 '_' 外不含符号（CJK 亦可用）。
bool isVariableName(const QString& name) {
    if (name.isEmpty() || name.at(0).isDigit()) return false;
    for (const QChar c : name) {
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_'))) return false;
    }
    return true;
}
}  // namespace



ScopedVarSpec parseScopedVarSpec(const QString& rawLine) {
    ScopedVarSpec spec;
    const QString text = stripDirectiveComment(rawLine).trimmed();
    int i = 0;
    while (i < text.size() && !text.at(i).isSpace()) ++i;
    const QString head = text.left(i).toUpper();
    if (head == QLatin1String("VARI")) spec.isStr = false;
    else if (head == QLatin1String("VARS")) spec.isStr = true;
    else return spec;

    const QString rest = text.mid(i).trimmed();
    if (rest.isEmpty()) return spec;

    // C# 用第一个 '=' 切成 左（名字+长度）/ 右（初值）；引号不参与切分
    //（字符串初值里的 '=' 必然落在最后一个引号之前，故与 C# 同结果）。
    QString left = rest;
    QString right;
    const int eq = rest.indexOf(QLatin1Char('='));
    if (eq >= 0) { left = rest.left(eq); right = rest.mid(eq + 1); }

    const QStringList parts = left.split(QLatin1Char(','));
    if (parts.isEmpty()) return spec;
    const QString name = parts.first().trimmed();
    if (!isVariableName(name)) return spec;
    spec.name = name;

    for (int k = 1; k < parts.size(); ++k) {
        const QString dim = parts.at(k).trimmed();
        bool isNumber = false;
        dim.toLongLong(&isNumber);   // 维度必须是整数字面量（C# int.Parse）
        // 解析失败时 C# 抛未受控的 FormatException（装载中止）。这里不登记
        // 该行（变量按未声明处理）—— 不静默造出一个维度错误的变量。
        if (!isNumber) return ScopedVarSpec();
        spec.lengths.append(dim);
    }

    // 只有「不带长度列表」时才解析初值（C#：leftSplit.Length > 1 走数组分支，
    // 该分支里 `= …` 被忽略）。
    if (parts.size() == 1 && !right.trimmed().isEmpty()) {
        if (spec.isStr) {
            const int a = right.indexOf(QLatin1Char('"'));
            const int b = right.lastIndexOf(QLatin1Char('"'));
            if (a >= 0 && b > a) { spec.init = right.mid(a + 1, b - a - 1); spec.hasInit = true; }
        } else {
            spec.init = right.trimmed();
            spec.hasInit = true;
        }
    }
    spec.ok = true;
    return spec;
}


bool isScopedVariableLine(const QString& rawLine) {
    return parseScopedVarSpec(rawLine).ok;
}

// ---------------------------------------------------------------------------
// 收尾写步骤的辅助（与旧实现逐字一致）
// ---------------------------------------------------------------------------
namespace {

// 取赋值左值的首标识符（跳过 A:1 / GLOBAL:5 这类下标）
QString leadingIdentifier(const QString& raw) {
    int i = 0;
    while (i < raw.size()) {
        const QChar c = raw.at(i);
        if (c.isSpace() || c == QLatin1Char(':') || c == QLatin1Char('[')
            || c == QLatin1Char('(') || c == QLatin1Char('=')) break;
        ++i;
    }
    return raw.left(i).trimmed();
}



// 把 #DIM/#DIMS 的初值写入存储（对齐 C#：全局变量装载时取初值；
// 函数私有变量每次进入函数时取初值）。ScriptRunner 在进入函数时选择
// 私有名字空间，因此同名数组的初值不会覆盖调用者。
void applyVariableDefaults(LoadContext& ctx, const QList<VariableDecl>& decls) {
    if (!ctx.storage) return;
    for (const VariableDecl& d : decls) {
        if (d.isConst || !d.isPrivate || d.isReference || d.isCharaData) continue;
        if (!d.lengths.isEmpty())
            ctx.storage->ensureArraySize(d.name, d.lengths.first(), d.type == OperandType::Str);
        if (d.type == OperandType::Str) {
            for (int i = 0; i < d.defaultStr.size(); ++i) {
                ctx.storage->setGlobalStr1D(d.name, i, d.defaultStr.at(i));
            }
        } else {
            for (int i = 0; i < d.defaultInt.size(); ++i) {
                ctx.storage->setGlobalInt1D(d.name, i, d.defaultInt.at(i));
            }
        }
    }
}



void applyGlobalVariableDefaults(LoadContext& ctx) {
    for (const VariableDecl& d : ctx.variables.declarations()) {
        if (d.scope != VarScope::Global || d.isConst) continue;
        applyVariableDefaults(ctx, {d});
    }
}



void resolveUserFunctionTypes(LoadContext& ctx) {
    for (auto it = ctx.functions.begin(); it != ctx.functions.end(); ++it) {
        UserFunctionDecl& decl = it.value();
        if (!decl.isMethod || !isKnown(decl.returnType)) {
            decl.returnType = OperandType::Int;
        }
        for (UserParamDecl& p : decl.params) {
            switch (p.target) {
            case UserParamTarget::Arg:
                p.type = OperandType::Int;
                break;
            case UserParamTarget::Args:
                p.type = OperandType::Str;
                break;
            case UserParamTarget::LocalVar: {
                const VariableDecl* variable = ctx.variables.find(p.varName, decl.name);
                const OperandType t = variable ? variable->type
                                               : ctx.variables.typeOf(p.varName, decl.name);
                if (isKnown(t)) {
                    p.type = t;
                    p.typeKnown = true;   // 私有变量由 #DIM/#DIMS 定类型
                    p.isReference = variable && variable->isReference;
                } else {
                    p.type = OperandType::Int;   // 绑定按整数槽处理，但不参与类型校验
                    p.typeKnown = false;
                }
                break;
            }
            case UserParamTarget::Unknown:
            default:
                if (!isKnown(p.type)) p.type = OperandType::Int;
                break;
            }
        }
    }
}



// 式中调用的强类型校验（复刻 C# Process.CalledFunction.ConvertArg）：
//
//   * 实参个数超过形参个数      -> 错误（C#「引数の数が…超えています」）
//   * 字符串实参 -> 整型形参    -> 错误（C#「文字列型から整数型に変換できません」，
//                                  任何配置下都不允许）
//   * 整型实参 -> 字符串形参    -> 允许（C# CompatiFuncArgAutoConvert /
//                                  「ユーザー関数の引数に自動的にTOSTRを補完する」；
//                                  eraTW 等实际游戏普遍设为 YES）
//
//   仅当形参类型来自真实声明（typeKnown：ARG/ARGS，或私有变量有 #DIM/#DIMS）才判定。
QString checkUserCallArgs(const UserFunctionDecl& decl,
                          const QList<OperandType>& argTypes) {
    const int want = decl.paramCount();
    if (argTypes.size() > want) {
        return QCoreApplication::translate("ParseDiagnostics", "函数 %1 的实参过多（形参 %2 个，实得 %3 个）")
            .arg(decl.name).arg(want).arg(argTypes.size());
    }
    for (int i = 0; i < argTypes.size(); ++i) {
        const UserParamDecl& p = decl.params.at(i);
        const OperandType actual = argTypes.at(i);
        if (!p.typeKnown || !isKnown(actual) || !isKnown(p.type)) continue;
        if (actual == OperandType::Str && p.type == OperandType::Int) {
            return QCoreApplication::translate("ParseDiagnostics", "函数 %1 第 %2 个实参需要%3，实得字符串（不能从字符串转换为整数）")
                .arg(decl.name).arg(i + 1)
                .arg(QString::fromUtf8(operandTypeName(p.type)));
        }
    }
    return QString();
}



void applyStringAssignments(LoadContext& ctx) {
    // FORM 里的插值表达式也要用**行的作用域**解析：否则 `%PNAME%`（函数内私有
    // 字符串变量）会按无作用域解析失败，整行报「字符串赋值FORM无效」。
    for (auto sit = ctx.scripts.begin(); sit != ctx.scripts.end(); ++sit) {
        for (LogicalLine& line : sit.value().lines) {
            // 只处理构建期未定型的行：构建期已按（预扫描）类型表定型的
            // 字符串/FORM/CASE 不再重解析（收尾只扫描未定型项）。
            if (!line.strAssignPending && !line.needsCompletion) continue;
            const QString owner = line.ownerFunction;
            const AstResolver resolve = [ctx, owner, &line](const QString& e) {
                diagnoseIntegerLiterals(e, line.position.filename, line.position.lineNumber,
                    line.position.column + qMax(0, int(line.raw.indexOf(e))), ctx.diagnostics);
                return expressionAstInScope(ctx, e, owner);
            };
            if (line.kind != LineKind::Instruction) continue;
            // FORM construction may need private string types unavailable in a worker.
            // Recompile the source in its owner scope while compilation is permitted.
            if (line.assignOperator.isEmpty() && (line.raw.contains("@\"")
                || AstBuilder::printInfo(line.functionName).mode == AstBuilder::PrintArgMode::FormStr
                || line.argument.kind == ArgKind::FormStr)) {
                const auto compiled = AstBuilder::build(line.raw, line.position, resolve, resolve);
                line.arguments = compiled.arguments;
                line.condition = compiled.condition;
                line.printTemplate = compiled.printTemplate;
                ArgumentParser::build(line);
            }
            if (line.argument.kind == ArgKind::Case) {
                line.caseCache.clear();
                AstBuilder::buildCaseClauses(line, resolve);
            }
            // C# uses two assignment operators with different RHS semantics:
            //   =  : string variables consume the remaining source as a StrForm
            //        (plain text plus {expr}/%expr% interpolation).
            //   '= : expression string assignment; keep the normal expression AST.
            // The initial AST is intentionally built before variable declarations
            // are finalized, so it may look like a call for text such as
            // `白狼天狗服(色固定)`.  Do not use that provisional node to decide
            // whether the RHS is raw text.
            if (line.assignOperator != QLatin1String("=")) continue;
            if (line.arguments.size() != 2) continue;

            Operand& dest = line.arguments[0];
            Operand& value = line.arguments[1];

            const QString varName = leadingIdentifier(dest.raw);
            if (varName.isEmpty()) continue;
            if (ctx.variables.typeOf(varName, line.ownerFunction) != OperandType::Str) continue;

            value.ast = StrFormParser::parse(value.raw.trimmed(), resolve, ctx.evaluator && ctx.evaluator->ignoreTripleSymbols());
            PrintTemplateCompiler::prepare(value.ast);
            if (!value.ast) {
                const QString error = QCoreApplication::translate("ParseDiagnostics", "字符串赋值FORM无效");
                line.argument.typeOk = false;
                line.argument.typeError = error;
                line.isError = true;
                line.errMes = error;
                ctx.diagnostics.addRejectLine(DiagSeverity::Error, DiagCode::kArgCheck,
                    line.position.filename, line.position.lineNumber, line.position.column, 0, error);
            }
        }
    }
}




void applyVariableTypes(LoadContext& ctx) {
    // 2) 各行实际引用的 AST（并行装载时 worker 的 AST 不在主缓存中）。
    //    **按脚本并行**：函数名全局唯一（一个函数体只在一个脚本里），
    //    VariableTable::applyTypes 取的是 const 变量表（只读），
    //    ArgumentParser::build 只改传入的行 —— 无共享写。
    //    eraTW 实测这一步 ~5s（2.2M 行），是 markLoadComplete 的最大热点。
    //
    //    **就地定型**：不再克隆「定型副本」再换指针。打印模板的分词片段
    //（CompiledPrintTemplatePart::expression / attributes / rawParts）与行实参
    //    指向的是**同一批 AST 节点**，就地改类型即同时生效；因此收尾既不需要
    //    克隆整棵树，也不需要再对模板递归重绑。
    QList<ScriptData*> scripts;
    scripts.reserve(ctx.scripts.size());
    for (auto it = ctx.scripts.begin(); it != ctx.scripts.end(); ++it) {
        scripts.append(&it.value());
    }

    // 形参名集合在 loadScript 注册函数时已同点建好（append-only）；这里只对
    // **顶层空作用域**补一个空集合（函数外没有形参），并行定型趟从此只读。
    ctx.paramNames.insert(QString(), {});

    const auto processScript = [ctx](ScriptData* sd) {
        // 同一节点可能被多行/多片段引用（模板分词、DEFAULT 列表…）：每个作用域
        // 只定型一次（与旧「按 ownerFunction 建缓存」等价）。行按所属函数连续存放，
        // 因此只在作用域切换时查表，避免逐行 toUpper + 哈希。
        QHash<QString, QSet<const ExpressionNode*>> typed;
        QSet<const ExpressionNode*>* seen = nullptr;
        const QSet<QString>* paramNames = nullptr;
        QString scope;
        for (LogicalLine& line : sd->lines) {
            if (!seen || line.ownerFunction != scope) {
                scope = line.ownerFunction;
                const QString scopeKey = scope.toUpper();
                seen = &typed[scopeKey];
                paramNames = paramNamesOf(ctx.paramNames, scopeKey);
            }
            const auto bind = [ctx, seen, &scope, paramNames](QSharedPointer<ExpressionNode>& ast) {
                if (!ast || seen->contains(ast.data())) return;
                seen->insert(ast.data());
                VariableTable::applyTypes(*ast, ctx.variables, scope, paramNames);
                PrintTemplateCompiler::prepare(ast);
            };
            for (auto& op : line.arguments) bind(op.ast);
            bind(line.condition);
            for (auto& ast : line.columnDefaults) bind(ast);
            ArgumentParser::build(line);
            for (const auto& ast : line.assignmentAsts) PrintTemplateCompiler::prepare(ast);
            // line.printTemplate（PRINT/PRINTFORM/PRINTS 的分词）无需单独处理：
            // 其片段表达式就是上面这批已被就地定型的节点。
        }
    };
    if (canParallelize(scripts.size())) {
        QtConcurrent::blockingMap(scripts, processScript);
    } else {
        for (ScriptData* sd : scripts) processScript(sd);
    }
}



// ---------------------------------------------------------------------------
// 函数调用重绑 + 校验（对齐 C# IdentifierDictionary.GetFunctionMethod）
//
// 把每个 FunctionNode 重新解析为 内置函数 / 用户自定义函数 / 未定义函数，
// 并用最终（就地定型后）的实参 AST 复刻 C# FunctionMethod.CheckArgumentType()。
// 必须在**全部**脚本的变量类型定稿之后进行（跨文件前向引用此刻才可解析）。
// ---------------------------------------------------------------------------
void resolveFunctionNodes(LoadContext& ctx) {
    // 与解析期同一判据（era_parse_table.cpp:userCallReturnType）：
    // 用户函数优先，但无 #FUNCTION 时不覆盖同名内置函数。
    const auto userType = [ctx](const QString& name) -> OperandType {
        return userCallReturnType(ctx.functions, name);
    };

    const auto resolveNode = [ctx, &userType](ExpressionNode& node) {
        if (node.kind() != NodeKind::Function) return;
        auto& fn = static_cast<FunctionNode&>(node);
        fn.setReferenceArguments({});
        fn.setUserFunction(false);
        fn.setBuiltinIndex(-1);
        fn.setExtensionSpec(nullptr);

        const FunctionResolution res = resolveFunctionCall(fn.name(), userType);
        if (res.isUserFunction) {
            fn.setUserFunction(true);
            fn.setValueType(res.returnType);
            // 强类型：按声明的形参类型逐个校验实参（复刻 C# UserDefinedMethodTerm.Create）
            const UserFunctionDecl* decl = userFunction(ctx.functions, fn.name().toUpper());
            if (decl) {
                QList<OperandType> argTypes;
                argTypes.reserve(fn.arguments().size());
                for (const auto& a : fn.arguments()) {
                    argTypes.append(a ? a->valueType() : OperandType::Unknown);
                }
                fn.setArityError(checkUserCallArgs(*decl, argTypes));
            } else {
                fn.setArityError(QString());
            }
        } else if (res.isBuiltin) {
            if (res.extensionSpec) fn.setExtensionSpec(res.extensionSpec);
            else fn.setBuiltinIndex(res.builtinIndex);
            fn.setValueType(res.returnType);
            if (const BuiltinFunctionSpec* spec = fn.builtinSpec()) {
                fn.setArityError(validateBuiltinCall(*spec, fn.arguments()));
            }
        } else if (const UserFunctionDecl* decl = userFunction(ctx.functions, fn.name().toUpper())) {
            // 有同名 @label 但既非内置也无 #FUNCTION：按宽容语义当作返回 Int 的用户函数
            // （eraTW 大量如此使用，实际可运行），但仍按声明的形参类型做校验。
            fn.setUserFunction(true);
            fn.setValueType(isKnown(decl->returnType) ? decl->returnType : OperandType::Int);
            QList<OperandType> argTypes;
            argTypes.reserve(fn.arguments().size());
            for (const auto& a : fn.arguments()) {
                argTypes.append(a ? a->valueType() : OperandType::Unknown);
            }
            fn.setArityError(checkUserCallArgs(*decl, argTypes));
        } else {
            fn.setValueType(OperandType::Unknown);
            fn.setArityError(QCoreApplication::translate("ParseDiagnostics", "未定义的函数 %1").arg(fn.name()));
        }
        if (fn.isUserFunction()) {
            const auto* decl = userFunction(ctx.functions, fn.name().toUpper());
            if (!decl) return;
            QList<bool> refs;
            for (int i = 0; i < decl->params.size(); ++i) {
                const auto& param = decl->params[i];
                refs.append(param.isReference);
                if (!param.isReference || !fn.arityError().isEmpty()) continue;
                const auto* formal = ctx.variables.find(param.varName, decl->name);
                const auto* actual = i < fn.arguments().size() && fn.arguments()[i]
                    ? dynamic_cast<const VariableNode*>(fn.arguments()[i].data()) : nullptr;
                // C# CalledFunction.ConvertArg / ReferenceToken.MatchType：REF 必须
                // 接收同类型、同维数的非角色数组，不能使用标量自动转换。
                if (!actual || fn.isArgOmitted(i) || !formal || actual->characterData || actual->readOnly
                    || actual->valueType() != formal->type
                    || actual->dimension != formal->dimension || actual->dimension <= 0) {
                    fn.setArityError(QCoreApplication::translate("ParseDiagnostics",
                        "函数 %1 第 %2 个 REF 实参需要同类型、同维数的非角色数组变量")
                        .arg(decl->name).arg(i + 1));
                }
            }
            fn.setReferenceArguments(refs);
        }
    };

    // 自底向上解析：`STRLENS(RAND(100))` 里内层 RAND 的返回类型必须先定下来，
    // 外层 STRLENS 才能校验实参类型（否则内层还停留在解析期的内置类型）。
    // walkExpression 是先序（父→子）；把节点收集后逆序应用即得 子→父 的求值顺序。
    // 例：A(B(C,D),E) 先序为 A,B,C,D,E，逆序 E,D,C,B,A —— 任一子节点都在其父之前。
    const auto resolveAst = [&resolveNode](const QSharedPointer<ExpressionNode>& ast) {
        if (!ast) return;
        QList<ExpressionNode*> nodes;
        walkExpression(*ast, [&nodes](ExpressionNode& n) { nodes.append(&n); });
        for (auto it = nodes.crbegin(); it != nodes.crend(); ++it) resolveNode(**it);
    };

    // 并行装载时 worker 的 AST 不在主缓存里，需按行再走一遍。
    // **按脚本并行**：每个脚本的行走的是自己的 AST，互不相干；resolveNode 只读
    // ctx.functions / 静态表，写入的是本脚本节点的字段 —— 无共享写。
    QList<ScriptData*> scripts;
    scripts.reserve(ctx.scripts.size());
    for (auto it = ctx.scripts.begin(); it != ctx.scripts.end(); ++it) {
        scripts.append(&it.value());
    }

    const auto processScript = [&resolveAst](ScriptData* sd) {
        for (LogicalLine& line : sd->lines) {
            for (const Operand& op : line.arguments) resolveAst(op.ast);
            for (const auto& ast : line.assignmentAsts) resolveAst(ast);
            for (const auto& e : line.argument.exprs) resolveAst(e);
            for (const Operand& c : line.argument.cases) resolveAst(c.ast);
            resolveAst(line.condition);
        }
    };
    if (canParallelize(scripts.size())) {
        QtConcurrent::blockingMap(scripts, processScript);
    } else {
        for (ScriptData* sd : scripts) processScript(sd);
    }
}



void installPrivateScopes(LoadContext& ctx) {
    if (!ctx.storage) return;
    // 前置条件（可观察，不只是注释）：声明定稿（resolveGlobalDeclarations 里的
    // resolveDimensions）之后才能安装 —— 未定稿就安装会把错的维数/名字表固化。
    Q_ASSERT_X(ctx.declarationsResolved, "installPrivateScopes",
               "private name table installed before declarations were finalized");
    ctx.storage->installPrivateScopeTable(ctx.variables.buildPrivateNameTable());
}



}  // namespace

void resolveGlobalDeclarations(LoadContext& ctx) {
    // ---- 常量/维数求值器（幂等，首次调用时设置） ----
    if (ctx.evaluator && !ctx.constProvidersReady) {
        ctx.constProvidersReady = true;
        const VariableTable* vt = &ctx.variables;
        // 表达式型常量的键 = 「归属作用域 ✕ 名字」：广域常量（owner 空）与同名的
        // 私有常量必须分开存放，否则一个函数里的 `#DIM CONST X = 1` 会盖住另一个
        // 函数里的 `#DIM CONST X = 2`。
        // 唯一复合键构造点（空 owner -> "\x1fNAME"，与旧形态逐字节相同）
        const auto constKey = [](const QString& owner, const QString& name) {
            return eravar::makePrivateStorageKey(owner, name);
        };
        const auto constOwnerOf = [vt](const VariableDecl& d) -> QString {
            return (d.scope == VarScope::Global) ? QString() : d.function;
        };
        QHash<QString, QList<QSharedPointer<ExpressionNode>>> constantNodes;
        for (const auto& declaration : vt->declarations()) {
            if (!declaration.isConst) continue;
            const QString owner = constOwnerOf(declaration);
            const QString key = constKey(owner, declaration.name);
            if (constantNodes.contains(key)) continue;
            // 表达式按**声明它的函数**作用域解析（私有常量只在该函数内可见）
            for (const auto& source : vt->constExprs(declaration.name, owner))
                constantNodes[key].append(expressionAstInScope(ctx, source, owner));
        }
        // 惰性常量表达式缓存（`#DIM CONST OBJ_ID_LAST = 人物数量上限`）
        //   LazyConst = 逐元素的求值结果（标量常量就是单元素）
        using LazyConst = QList<QVariant>;
        auto lazyConst = QSharedPointer<QHash<QString, LazyConst>>::create();
        auto lazyDepth = QSharedPointer<int>::create(0);
        // 求值并缓存某个常量声明的全部元素；返回 nullptr 表示「不是表达式型常量」。
        // 定位顺序与 VariableTable 的常数查询一致：先 scope 归属，再广域。
        const auto lazyConstValues = [ctx, constantNodes, constKey, lazyConst, lazyDepth](
                                         const QString& name, const QString& scope)
            -> QSharedPointer<LazyConst> {
            QString key = constKey(scope, name);
            if (!constantNodes.contains(key)) key = constKey(QString(), name);
            if (!constantNodes.contains(key)) return QSharedPointer<LazyConst>();
            const auto cached = lazyConst->constFind(key);
            if (cached != lazyConst->constEnd())
                return QSharedPointer<LazyConst>::create(cached.value());
            const auto exprs = constantNodes.value(key);
            if (exprs.isEmpty() || !ctx.evaluator) return QSharedPointer<LazyConst>();
            if (*lazyDepth >= 16) return QSharedPointer<LazyConst>();   // 循环定义保护
            ++*lazyDepth;
            LazyConst values;
            values.reserve(exprs.size());
            const eravar::ScopeId exprScope = eravar::internName(scope);
            for (const auto& ast : exprs) {
                values.append(ast ? ctx.evaluator->evaluate(*ast, ctx.storage, ctx.gameBaseData, exprScope)
                                  : QVariant());
            }
            --*lazyDepth;
            lazyConst->insert(key, values);
            return QSharedPointer<LazyConst>::create(values);
        };
        // 运行期常数求值的作用域 = **当前执行行所属的函数**。
        // C# 在 GetVariableToken 里用 line.ParentLabelLine 找私有变量，与此等价：
        // 执行到哪个函数的行，就只看得见那个函数的私有常量。
        const auto evalLazyConst = [lazyConstValues](const QString& name, const QString& scope,
                                                     int index, QVariant& out) -> bool {
            const QSharedPointer<LazyConst> values = lazyConstValues(name, scope);
            if (!values || values->isEmpty()) return false;
            const int i = (index < 0) ? 0 : index;
            out = values->value(i < values->size() ? i : 0);
            return true;
        };
        ctx.evaluator->setConstProvider([ctx, vt, evalLazyConst](const QString& name, QVariant& out) -> bool {
            const QString scope = executionScope(ctx.scripts, ctx.positionScript, ctx.positionLine);
            qint64 iv = 0;
            if (vt->constInt(name, iv, scope)) { out = QVariant::fromValue<qint64>(iv); return true; }
            QString sv;
            if (vt->constStr(name, sv, scope)) { out = QVariant(sv); return true; }
            return evalLazyConst(name, scope, 0, out);
        });
        // 常数数组（`#DIM CONST X, N = …`）：先判名（避免下标副作用被求两遍）
        ctx.evaluator->setConstArrayChecker([ctx, vt](const QString& name) -> bool {
            const QString scope = executionScope(ctx.scripts, ctx.positionScript, ctx.positionLine);
            return vt->constArraySize(name, scope) > 0 || vt->constStrArraySize(name, scope) > 0;
        });
        ctx.evaluator->setConstArraySizeProvider([ctx, vt](const QString& name) -> int {
            const QString scope = executionScope(ctx.scripts, ctx.positionScript, ctx.positionLine);
            const int n = vt->constArraySize(name, scope);
            return n > 0 ? n : vt->constStrArraySize(name, scope);
        });
        ctx.evaluator->setConstArrayProvider([ctx, vt, evalLazyConst](const QString& name, int index,
                                                                     QVariant& out) -> bool {
            const QString scope = executionScope(ctx.scripts, ctx.positionScript, ctx.positionLine);
            QString sv;
            if (vt->constStrArrayAt(name, index, sv, scope)) { out = QVariant(sv); return true; }
            qint64 iv = 0;
            if (vt->constArrayAt(name, index, iv, scope)) { out = QVariant::fromValue<qint64>(iv); return true; }
            return evalLazyConst(name, scope, index, out);
        });
        ctx.variables.setDimEvaluator([ctx](const QString& expr, const QString& scope) -> qint64 {
            if (!ctx.evaluator) return 0;
            const auto ast = expressionAstInScope(ctx, expr, scope);
            const QVariant v = ast ? ctx.evaluator->evaluate(*ast, ctx.storage, ctx.gameBaseData,
                                                          eravar::internName(scope))
                                   : QVariant();
            return v.isValid() ? v.toLongLong() : 0;
        });
    }

    // ---- 维数/默认值/函数类型（全局：需要全部声明/常量就绪） ----
    ctx.variables.resolveDimensions();
    applyGlobalVariableDefaults(ctx);
    resolveUserFunctionTypes(ctx);
    ctx.declarationsResolved = true;   // 声明定稿：installPrivateScopes 的前置条件
}



namespace {
// 这些指令的**第一个操作数**是「目标名」（@函数名 / $标签名），不是表达式：
// C# 走标签解析路径（ReduceLabelName 之类），不经 GetVariableToken，因此不参与
// 「变量作用域」判定。`GOTO SKIP` 里的 SKIP 是 `$SKIP` 标签 —— 若按变量判定，
// eraTW 会把 67 处 `GOTO <标签>` 全报成「变量未定义」（SKIP/END/FINISH/… 恰好
// 也是别处的私有变量名）。
bool firstArgumentIsTargetName(RunnerInstruction instruction) {
    switch (instruction) {
    case RunnerInstruction::GOTO:
    case RunnerInstruction::GOTOFORM:
    case RunnerInstruction::CALLEVENT:
    case RunnerInstruction::TRYCALLEVENT:
    case RunnerInstruction::CALL:
    case RunnerInstruction::TRYCALL:
    case RunnerInstruction::CALLFORM:
    case RunnerInstruction::TRYCALLFORM:
    case RunnerInstruction::TRYCCALL:
    case RunnerInstruction::TRYCCALLFORM:
    case RunnerInstruction::CALLF:
    case RunnerInstruction::TRYCALLF:
    case RunnerInstruction::CALLFORMF:
    case RunnerInstruction::TRYCALLFORMF:
    case RunnerInstruction::JUMP:
    case RunnerInstruction::TRYJUMP:
    case RunnerInstruction::JUMPFORM:
    case RunnerInstruction::TRYJUMPFORM:
    case RunnerInstruction::TRYGOTO:
    case RunnerInstruction::TRYGOTOFORM:
    case RunnerInstruction::TRYCJUMP:
    case RunnerInstruction::TRYCJUMPFORM:
    case RunnerInstruction::TRYCGOTO:
    case RunnerInstruction::TRYCGOTOFORM:
        return true;
    default:
        return false;
    }
}
}  // namespace



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


// 收集一个 AST 里的函数调用告警（内置函数参数不符 / 未定义的函数）。
// 同一位置的重复 AST 引用去重；其它行/文件保留各自的诊断。
// 纯函数：只读 AST，结果写入 out —— 可在线程池上按脚本并行调用。
void collectFunctionWarnings(const QSharedPointer<ExpressionNode>& ast,
                             const ScriptPosition& position, WarningCollector& out) {
    if (!ast) return;
    walkExpression(*ast, [&out, &position](ExpressionNode& node) {
        // 三元分支类型：对齐 C# OperatorMethodManager.ReduceTernaryTerm —— 只允许
        // (long,long,long) 与 (long,string,string)，其余抛 CodeEE
        //「三項演算子の使用法が不正です」（解析期 CodeEE 由 ParserMediator 收成告警）。
        if (node.kind() == NodeKind::If) {
            const auto& ifn = static_cast<const IfNode&>(node);
            const OperandType ct = ifn.condition() ? ifn.condition()->valueType() : OperandType::Unknown;
            const OperandType tt = ifn.thenExpr() ? ifn.thenExpr()->valueType() : OperandType::Unknown;
            const OperandType et = ifn.elseExpr() ? ifn.elseExpr()->valueType() : OperandType::Unknown;
            const bool condBad = isKnown(ct) && ct != OperandType::Int;
            const bool branchBad = isKnown(tt) && isKnown(et)
                                   && (tt != et || (tt != OperandType::Int && tt != OperandType::Str));
            if (condBad || branchBad) {
                const QString err = QCoreApplication::translate("ParseDiagnostics", 
                    "三項演算子の使用法が不正です"
                    "（条件须为整数；真/假分支须同为整数或同为字符串）");
                const QString key = position.toString() + QStringLiteral("\x1fternary\x1f") + err;
                if (!out.seen.contains(key)) {
                    out.seen.insert(key);
                    out.warnings.append(QStringLiteral("%1 [%2]").arg(err, node.toString()));
                    out.keys.append(key);
                }
            }
        }
        if (node.kind() != NodeKind::Function) return;
        const auto& fn = static_cast<const FunctionNode&>(node);
        const QString& err = fn.arityError();
        if (err.isEmpty()) return;
        const QString key = position.toString() + QLatin1Char('\x1f') + fn.name().toUpper() + QLatin1Char('\x1f') + err;
        if (out.seen.contains(key)) return;
        out.seen.insert(key);
        out.warnings.append(QStringLiteral("%1 [%2]").arg(err, fn.toString()));
        out.keys.append(key);
    });
}



// 逐脚本参数校验（收尾步骤 3 的第二半）。
//
// 旧形状是「自建脚本列表 -> 并行 -> 有序归并 + **全局** seen 去重」的一整步；
// 现在驱动归 finalizeValidation（它与逐行校验共处**同一个任务**、按脚本名序
// 归并），这里只剩**一个**脚本的循环，诊断写进调用方给的 sink（本任务独占）。
//
// 去重仍然做，但 `seen` 变成脚本内局部量：键 = position.toString()（含**文件名**）
// + 消息，不同脚本的文件名不同 -> 跨脚本永远撞不上键，因此「脚本内去重」对同一份
// 输入与原来的「全局去重」逐条等价（脚本内仍是首次出现优先）。
void validateArguments(const LoadContext& /*ctx*/, ScriptData& script, ParseDiagnostics& sink) {
    WarningCollector out;
    for (LogicalLine& line : script.lines) {
        if (line.kind != LineKind::Instruction) continue;
        const qsizetype before = out.warnings.size();
        const QString semanticError = !line.argument.typeOk ? line.argument.typeError : QString();
        // ArgumentParser::build 重算 kind/params/exprs（按最终变量类型）。赋值右值
        // 的 AST 无需再 prepare —— applyVariableTypes（收尾第 3 步）已对全体
        // assignmentAsts 调过一次，finalizeBindings 重建的那些也由
        // expressionAstInScope 在解析时 prepare。
        ArgumentParser::build(line);
        if (!semanticError.isEmpty()) {
            line.argument.typeOk = false;
            line.argument.typeError = semanticError;
            line.isError = true;
            line.errMes = semanticError;
        }
        if (line.argument.hasError()) {
            // 结构告警：不参与去重（与串行版一致，key 留空）
            out.warnings.append(QStringLiteral("%1 (%2)")
                                    .arg(line.argument.typeError, line.raw.trimmed()));
            out.keys.append(QString());
        }
        const ScriptPosition& pos = line.position;

        // 注：CALL 语句**不做**实参类型校验 —— 对齐 C#（SP_CALL_ArgumentBuilder 不调用
        // ConvertArg，实参按「实际类型」装进 Transporter 后由被调函数自行解释）。
        // 强类型校验只发生在「式中调用」NAME(args) 处（见 resolveFunctionNodes）。

        // 函数调用（内部命令/内置函数）参数校验告警
        collectFunctionWarnings(line.condition, pos, out);
        for (const Operand& op : line.arguments) {
            collectFunctionWarnings(op.ast, pos, out);
        }
        for (const auto& e : line.argument.exprs) {
            collectFunctionWarnings(e, pos, out);
        }
        for (const Operand& c : line.argument.cases) {
            collectFunctionWarnings(c.ast, pos, out);
        }
        for (qsizetype i = before; i < out.warnings.size(); ++i)
            out.positions.append(line.position);
    }
    // 脚本内有序去重后写 sink：只有第一次出现的 key 会被保留。
    QSet<QString> seen;
    for (int i = 0; i < out.warnings.size(); ++i) {
        const QString& key = out.keys.at(i);
        if (!key.isEmpty()) {
            if (seen.contains(key)) continue;
            seen.insert(key);
        }
        const auto& pos = out.positions.at(i);
        sink.add(DiagSeverity::Warning, DiagCode::kArgCheck,
                 pos.filename, pos.lineNumber, pos.column, 0,
                 out.warnings.at(i));
    }
}


void finalizeLabelIndex(LoadContext& ctx) {
    // 把 ctx.labelLists 每个同名列表按 (脚本名, 行号) 排序固化：
    //   · labels()（callFunction / CALLEVENT 的事件导航）不再每次调用重排；
    //   · resolveCallableLabel（跨文件 CALL/式中调用/JUMP）取「第一份」有了
    //     确定含义 —— 并行装载的插入顺序是不确定的。
    // 全部 @函数标签合计 ~1.1 万个（eraTW/FL3.8），一次排序是毫秒级。
    for (auto it = ctx.labelLists.begin(); it != ctx.labelLists.end(); ++it) {
        QList<LabelRef>& list = it.value();
        std::sort(list.begin(), list.end(), [](const LabelRef& a, const LabelRef& b) {
            if (a.script != b.script) return a.script < b.script;
            return a.line < b.line;
        });
    }
}



void resolveGlobalDeclarationsStep(LoadContext& ctx) {
    // 收尾步骤 1：全局声明定稿 + 全局标签索引排序固化（顺序不可交换）。
    resolveGlobalDeclarations(ctx);   // 声明预登记后 #FUNCTION 表才随 loadScript 补齐，此处再结算一次
    finalizeLabelIndex(ctx);          // 全局标签索引排序固化（labels()/resolveCallableLabel 依赖）
}



// 全体脚本绑定（一趟）：字符串赋值/FUNC → 变量类型就地定型 → 赋值 AST → 函数调用解析。
// 要求 resolveGlobalDeclarations() 已就绪（变量维数与函数类型已定）。
void finalizeBindings(LoadContext& ctx) {
    // 每步内部按脚本并行（见各方法）；不再逐脚本调用（那样有 2200+ 次调用开销，
    // eraTW 上从 ~10s 退化到 ~33s）。
    applyStringAssignments(ctx);


    // FUNC 重编译（仅构建期未定型的行）
    for (auto& script : ctx.scripts) {
    for (auto& line : script.lines) {
        if (line.functionName != QLatin1String("FUNC")) continue;
        const AstResolver resolve = [ctx, scope = line.ownerFunction](const QString& text) {
            return expressionAstInScope(ctx, text, scope);
        };
        for (int i = 0; i < line.arguments.size(); ++i) {
            auto& operand = line.arguments[i];
            if (i == 0)
                operand.ast = StrFormParser::parse(operand.raw, resolve,
                    ctx.evaluator && ctx.evaluator->ignoreTripleSymbols());
            else if (operand.isString)
                operand.ast = QSharedPointer<LiteralNode>::create(operand.raw);
            else
                operand.ast = resolve(operand.raw);
        }
    }
    }

    // 变量类型回填（全体脚本）
    applyVariableTypes(ctx);

    // 赋值 AST 重建（仅构建期未定型的行）
    for (auto& script : ctx.scripts) {
    for (auto& line : script.lines) {
        if (line.assignOperator.isEmpty() || line.arguments.size() != 2) continue;
        const QString name = line.arguments[0].raw.section(':', 0, 0).trimmed();
        if (line.assignOperator == "=" && ctx.variables.typeOf(name, line.ownerFunction) == OperandType::Str) continue;
        line.assignmentAsts.clear();
        for (const auto& text : AstBuilder::assignmentValues(line.arguments[1].raw)) {
            // expressionAstInScope 已按 ownerFunction 定型并 prepare —— 不再克隆/重定型。
            line.assignmentAsts.append(expressionAstInScope(ctx, text, line.ownerFunction, true));
        }
    }

    }

    // 函数调用解析（全体脚本）
    resolveFunctionNodes(ctx);

    // 收尾的最后一步：私有名表（「每个函数有哪些私有变量、存储键是什么」）
    // 由声明一次性建好并装进存储层 —— 此刻声明（resolveGlobalDeclarations）
    // 与类型回填都已完成，名表是静态事实。
    installPrivateScopes(ctx);
}



// 收尾步骤 3 的第一半（原 finalizeValidation 的逐行循环体，逐字搬运）：
// 只写 `script` 的行与调用方给的 sink —— 因此可以被线程池按脚本并行调用。
namespace {

void validateScriptLines(const LoadContext& ctx, ScriptData& script, ParseDiagnostics& sink) {
    const auto constantTree = [](const ExpressionNode& root) {
        bool constant = true;
        walkExpression(const_cast<ExpressionNode&>(root), [&](ExpressionNode& n) {
            if (n.kind() == NodeKind::Variable || n.kind() == NodeKind::Function) constant = false;
            if (n.kind() == NodeKind::UnaryOp) {
                const auto op = static_cast<const UnaryOpNode&>(n).op().type();
                if (op == TokenType::INCREMENT || op == TokenType::DECREMENT) constant = false;
            }
        });
        return constant;
    };
    for (auto& line : script.lines) {
        // 指令的「变量槽」实参允许整数组出现（`VARSET 集合`、`CALL F(集合)`）：
        // 这些位置**不该**要求下标，否则 2 维以上数组会被误判「缺少参数或下标维数错误」
        // （eraTW 里 CALL 的 REF 实参 / VARSET 的整数组实参全是这种写法）。
        const ArgKind argKind = line.argument.kind;
        const bool varSlotArgs = argKind == ArgKind::Call || argKind == ArgKind::CallF
            || argKind == ArgKind::Var || argKind == ArgKind::VarSet
            || argKind == ArgKind::Swap || argKind == ArgKind::Bit
            || argKind == ArgKind::Power || argKind == ArgKind::GetInt
            || argKind == ArgKind::VarStr || argKind == ArgKind::ArrayControl
            || argKind == ArgKind::SortChara || argKind == ArgKind::ForNext
            // CALL 族全名单（TRYCALL/TRYCCALLFORM/JUMP/GOTO…）：规范表只登记了
            // 其中几个，其余按名字前缀回落 Raw —— 整数组实参（REF 形参的
            // `CALL F(WPN_MATERIAL_NEEDS)`，2 维声明）会被 requireIndices 误报
            // 「缺少参数或下标维数错误」。C# ConvertArg 对 REF 形参收的就是
            // **整数组变量**（eraTW/FL3.8 都这么写）。
            || AstBuilder::isCallFamilyInstruction(line.functionName.toUpper())
            // VARI/VARS（声明型语句）：目标操作数是裸名字，维度由声明决定
            //（applyScopedVariableLine 重建后的 [目标, 初值] 同样不该查下标）。
            || isScopedVariableLine(line.raw);
        // 本行已经报过「跨函数引用私有变量」的名字（同一行会对共享 AST 走两遍）
        QSet<QString> reportedScopeNames;
        // 原始文本实参的语句（`ArgKind::Raw`：ALIGNMENT / SETCOLORBYNAME /
        // PRINTL / GOTO 标签 / NEXT …）在 C# 走 `STR_ArgumentBuilder`：
        // 实参整体是**字面字符串**（`st.Substring()` → `SingleTerm(rowStr)`），
        // 裸标识符不会被解析成变量 —— `ALIGNMENT LEFT` 的 LEFT 是关键字，
        // `SETCOLORBYNAME Red` 的 Red 是颜色名。构建期出于「未知裸标识符 =
        // 字符串」的宽容语义仍会把它折成 VariableNode，但那是**装饰**：
        // 运行期读的是 `operand.raw`（见 ExecutionEngine 的 ALIGNMENT 分支）。
        // 因此这类语句不能作为「跨函数引用私有变量」的判据 ——
        // 否则 eraMegaten 会多出 323 条假阳性（LEFT/RIGHT/Red/Blue 恰好在
        // PICTURE.ERB 里各有一条私有的 #DIMS 同名声明）。
        // 赋值语句（`=` 族）也登记成 Raw，但左右值是真表达式：那两条路径
        // 单独调用 validate（左值 / assignmentAsts），不受此开关影响。
        const bool rawTextArgs = argKind == ArgKind::Raw && line.assignOperator.isEmpty();
        const auto validate = [&](const QSharedPointer<ExpressionNode>& ast,
                                  bool requireIndices = true) {
            if (!ast) return;
            QString error = validateExpression(*ast, requireIndices);
            if (error.isEmpty() && ctx.evaluator && ctx.storage) {
                walkExpression(*ast, [&](ExpressionNode& n) {
                    if (error.isEmpty() && n.kind() != NodeKind::Literal && constantTree(n)
                        && !ctx.evaluator->evaluate(n, ctx.storage, ctx.gameBaseData,
                                                  eravar::internName(line.ownerFunction)).isValid())
                        error = QCoreApplication::translate("ParseDiagnostics", "装载期常量表达式求值失败");
                });
            }
            // 作用域：名字只是**别的函数的私有变量/常量**时，在本函数里解析不到。
            // 对齐 C# IdentifierDictionary.ThrowException（privateDimList ->
            // IdentifierNotFoundCodeEE「変数"X"はこの関数中では定義されていません」）。
            // 此前这种引用会被「扁平常量表/私有变量泄漏」悄悄取值 —— eraTW 的
            // `@スロット配列_一覧`（CASINO_SLOT_HELP.ERB:732）读别的函数的
            // `#DIM CONST RD7` 就是这一类。
            bool scopeError = false;
            if (error.isEmpty() && !rawTextArgs) {
                walkExpression(*ast, [&](ExpressionNode& n) {
                    if (!error.isEmpty() || scopeError || n.kind() != NodeKind::Variable) return;
                    const auto& var = static_cast<const VariableNode&>(n);
                    if (isKnown(ctx.variables.typeOf(var.name(), line.ownerFunction))) return;
                    if (!ctx.variables.isPrivateOnlyName(var.name())) return;
                    // 形参名（`@ASSERT_EQ(GOT, WANT, SNAME)` 的 GOT/WANT 是 ARG
                    // 的**别名**，不在变量表里）在本函数内是合法的。
                    if (const UserFunctionDecl* decl = userFunction(ctx.functions, line.ownerFunction.toUpper())) {
                        for (const UserParamDecl& p : decl->params) {
                            if (p.name.compare(var.name(), Qt::CaseInsensitive) == 0
                                || (!p.varName.isEmpty()
                                    && p.varName.compare(var.name(), Qt::CaseInsensitive) == 0))
                                return;
                        }
                    }
                    // 一行可能对同一棵 AST 走两遍（行实参 + 赋值右值 assignmentAsts
                    // 共享节点）—— 同一个名字只报一次，避免 LSP 出现重复标记。
                    const QString scopeKey = var.name().toUpper();
                    if (reportedScopeNames.contains(scopeKey)) return;
                    reportedScopeNames.insert(scopeKey);
                    error = QCoreApplication::translate("ParseDiagnostics",
                        "变量 %1 在本函数中未定义（它只是别的函数的私有变量；运行到该行会失败）")
                        .arg(var.name());
                    scopeError = true;
                });
            }
            if (!error.isEmpty()) {
                line.argument.typeOk = false;
                line.argument.typeError = error;
                line.isError = true;
                line.errMes = error;
                sink.addRejectLine(DiagSeverity::Error,
                    scopeError ? DiagCode::kIdentifierScope : DiagCode::kArgCheck,
                    line.position.filename, line.position.lineNumber, line.position.column, 0, error);
            }
        };
        // Increment/decrement statements have one LHS, unlike assignments.
        // Rebind it after declarations are known, in the owning function scope.
        if ((line.functionName == "++" || line.functionName == "--")
            && line.arguments.size() == 1) {
            auto& operand = line.arguments[0];
            // expressionAstInScope 已按 ownerFunction 定型 —— 不再克隆/重定型。
            operand.ast = expressionAstInScope(ctx, operand.raw, line.ownerFunction, true);
            const bool ok = operand.ast && operand.ast->kind() == NodeKind::Variable
                && !static_cast<const VariableNode&>(*operand.ast).readOnly
                && operand.ast->valueType() != OperandType::Str;
            // 这一步是 `X ++` 的**权威**判定：构建期的临时 AST 在「无作用域」
            // 上下文里解析，可能把「与别处同名常量重名的函数私有变量」折成字面量
            // （eraTW 的 `削除行数 ++`：CASINO_SLOT 里的 `#DIM CONST 削除行数`
            // 会让 COMF625 的同名私有变量在构建期变成 `51`），于是收尾第 3 步
            // 的 ArgumentParser 会拿那个陈旧的字面量报「第一个参数需要变量节点」。
            // 因此这里通过时必须**清掉**那次残留的错误状态（typeOk/errMes），
            // 否则语义已经判定正确的行仍会带着旧错误进入 validateArguments。
            line.argument.typeOk = ok;
            line.argument.typeError.clear();
            line.isError = !ok;
            line.errMes.clear();
            if (!ok) {
                const QString error = QCoreApplication::translate("ParseDiagnostics",
                    "%1 的变量参数无法装载：%2 (%3)")
                    .arg(line.functionName, operand.raw, line.position.toString());
                line.argument.typeError = error;
                line.errMes = error;
                sink.addRejectLine(DiagSeverity::Error, DiagCode::kArgCheck,
                    line.position.filename, line.position.lineNumber, line.position.column, 0, error);
            }
        }
        // ArgumentParser treats a FUNC name as a raw CALL identity; list
        // execution requires an AST for it just as for every actual argument.
        if (line.functionName == QLatin1String("FUNC")) {
            for (const auto& operand : line.arguments) {
                if (operand.ast) continue;
                const QString error = QCoreApplication::translate("ParseDiagnostics",
                    "%1 的参数缺少装载期 AST：%2").arg(line.functionName, operand.raw);
                line.argument.typeOk = false;
                line.argument.typeError = error;
                line.isError = true;
                line.errMes = error;
                sink.addRejectLine(DiagSeverity::Error, DiagCode::kArgCheck,
                    line.position.filename, line.position.lineNumber, line.position.column, 0, error);
            }
        }
        validate(line.condition);
        // DT_COLUMN_OPTIONS 的 DEFAULT 表达式（columnDefaults）是运行期求值的
        // 普通表达式，此前从不校验 —— 类型/边界问题同样应当装载期就报出来。
        for (const auto& ast : line.columnDefaults) validate(ast);
        const bool skipTargetOperand = firstArgumentIsTargetName(line.runnerInstruction);
        for (int operandIndex = 0; operandIndex < line.arguments.size(); ++operandIndex) {
            const auto& operand = line.arguments.at(operandIndex);
            // 第一个操作数是跳转/调用目标名（函数名 / $标签）：不是表达式
            if (skipTargetOperand && operandIndex == 0) continue;
            // VARSIZE consumes only array identity; its indices (including
            // out-of-range constants) are ignored by the language command.
            if (line.functionName == QLatin1String("VARSIZE") && operand.ast
                && operand.ast->kind() == NodeKind::Variable) continue;
            validate(operand.ast, !varSlotArgs);
        }
        if (!line.assignOperator.isEmpty() && line.arguments.size() == 2) {
            const auto& dest = line.arguments[0].raw;
            const QString name = dest.section(':', 0, 0).trimmed();
            const auto* decl = ctx.variables.find(name, line.ownerFunction);
            // expressionAstInScope 已按 ownerFunction 定型 —— 不再克隆/重定型。
            const auto target = expressionAstInScope(ctx, dest, line.ownerFunction, true);
            line.arguments[0].ast = target;
            // 诊断带上**具体原因**（未定义变量 / 维数不符 / AST 解析失败）：
            // 只报「必须为有效变量项」时，eraTW 这种 10k 变量的工程无从下手。
            const QString lvalueError = !target ? QCoreApplication::translate("ParseDiagnostics", "左值表达式解析失败")
                      : target->kind() != NodeKind::Variable ? QCoreApplication::translate("ParseDiagnostics", "左值不是变量项")
                      : validateExpression(*target);
            if (!lvalueError.isEmpty()) {
                line.argument.typeOk = false;
                line.argument.typeError = QCoreApplication::translate("ParseDiagnostics", "赋值左值必须为有效变量项（%1：%2）")
                                              .arg(dest.left(60), lvalueError);
                line.isError = true;
                line.errMes = line.argument.typeError;
                sink.addRejectLine(DiagSeverity::Error, DiagCode::kArgCheck,
                    line.position.filename, line.position.lineNumber, line.position.column, 0,
                    line.argument.typeError);
            }
            const bool rawForm = line.assignOperator == "=" && ctx.variables.typeOf(name, line.ownerFunction) == OperandType::Str;
            if (!rawForm) {
                const auto values = AstBuilder::assignmentValues(line.arguments[1].raw);
                for (int valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
                    const auto& text = values[valueIndex];
                    diagnoseIntegerLiterals(text, line.position.filename, line.position.lineNumber,
                        line.position.column + qMax(0, int(line.raw.indexOf(text))), sink);
                    const auto& ast = line.assignmentAsts[valueIndex];
                    if (ast) validate(ast);
                    else {
                        line.argument.typeOk = false;
                        line.argument.typeError = QCoreApplication::translate("ParseDiagnostics", "赋值右值无法解析或包含空项");
                        line.isError = true;
                        line.errMes = line.argument.typeError;
                        sink.addRejectLine(DiagSeverity::Error, DiagCode::kArgCheck,
                            line.position.filename, line.position.lineNumber, line.position.column, 0,
                            line.argument.typeError);
                    }
                }
                if (values.size() > 1 && line.assignOperator != "=" && line.assignOperator != "'=") {
                    line.argument.typeOk = false;
                    line.argument.typeError = QCoreApplication::translate("ParseDiagnostics", "复合赋值不允许列表");
                    line.isError = true;
                    line.errMes = line.argument.typeError;
                }
            }
            if (decl && decl->isConst) {
                line.argument.typeOk = false;
                line.argument.typeError = QCoreApplication::translate("ParseDiagnostics", "不能赋值const变量");
                line.isError = true;
                line.errMes = line.argument.typeError;
            }
        }
    }
}

}  // namespace

// 全体脚本校验：产出诊断（错误 + 告警）。必须在全部脚本**绑定**完成后统一执行，
// 否则跨文件前向引用会被误报「未定义的函数」。只在 markLoadComplete 调用一次。
// 参数/类型校验必须在「变量类型已就地定型」之后：否则 LFONTS 之类用户 #DIMS 变量
// 在解析期还是默认的 Int，会误报「需要字符串表达式，实得 Int」。
//
// ---------------------------------------------------------------------------
// 收尾步骤 3：全体脚本参数/类型校验（只产诊断，不改跨脚本状态）
//
// **逐脚本并行**，驱动方式与 parse_checks.cpp 的 runPerScript() 同一套：
//   序幕（本线程）：取脚本名 -> 排序（QString::compare 序）-> 预解析 ScriptData*
//     指针 —— 任务里只剩纯指针访问，不必各自去 constFind 同一个 QHash。
//   任务（线程池）：validateScriptLines + validateArguments **按原顺序**跑完
//     **同一个**脚本。两步之间有脚本内顺序依赖（validateArguments 读刚写的
//     line.argument.typeOk 与 line.arguments[0].ast），因此必须待在同一个任务里。
//     诊断写进本任务独占的 ParseDiagnostics（不与任何线程共享）。
//   归并（本线程）：blockingMapped 保持输入顺序返回，按**脚本名序**逐条 add 回
//     ctx.diagnostics。sortForOutput 仍是「同排序键的重复诊断」的最后兜底。
//
// 跨脚本依赖：**没有** —— 两步只读 ctx.variables / ctx.functions /
// ctx.constantTable / ctx.evaluator / ctx.storage（pass 1/2 已定型的只读表），
// 写的是自己那个 ScriptData 的行。eravar::internName 内部有 std::mutex
//（name_interner.cpp），是唯一的全局串行点。
//
// 并行粒度刻意停在「一个 pass 内」：pass 之间仍顺序执行（pass 表、gate、
// phaseAfter 一个都不动）。
// ---------------------------------------------------------------------------

// 并行阈值：与 parse_checks.cpp 的 kMinScriptsForParallel **同名同值**（那边有
// 完整理由：脚本少时调度/归并开销大于检查本身，调试器还要能一路跟进去）。
// 两个文件各持一份 constexpr，改阈值时两处一起改。
constexpr int kMinScriptsForParallel = 4;

void finalizeValidation(LoadContext& ctx) {
    QStringList names = ctx.scripts.keys();
    std::sort(names.begin(), names.end());

    QList<ScriptData*> scripts;
    scripts.reserve(names.size());
    for (const QString& name : names) {
        const auto it = ctx.scripts.find(name);
        if (it != ctx.scripts.end()) scripts.append(&it.value());
    }

    // 任务里只读 ctx：取 const 视图，写操作只能落到 script 与任务独占的 sink。
    const LoadContext& view = ctx;
    const auto processScript = [&view](ScriptData* sd) {
        ParseDiagnostics local;   // **本任务独占**，不与任何线程共享
        validateScriptLines(view, *sd, local);
        validateArguments(view, *sd, local);
        return local;
    };

    if (scripts.size() < kMinScriptsForParallel
        || QThreadPool::globalInstance()->maxThreadCount() <= 1) {
        for (ScriptData* sd : scripts) {
            validateScriptLines(ctx, *sd, ctx.diagnostics);
            validateArguments(ctx, *sd, ctx.diagnostics);
        }
        return;
    }

    const QList<ParseDiagnostics> locals = QtConcurrent::blockingMapped(scripts, processScript);
    for (const ParseDiagnostics& local : locals)
        for (const ParseDiagnostic& d : local.all()) ctx.diagnostics.add(d);
}


}  // namespace loadpass
