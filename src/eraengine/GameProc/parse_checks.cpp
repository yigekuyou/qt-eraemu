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
// ---------------------------------------------------------------------------
// parse_checks.cpp —— 装载收尾的三个**只读**步骤（4/5/6 号 pass）+ CALL 实参校验
//
// 实现从 era_parse_table.cpp 逐字搬来（同一份判据、同一组诊断、同一产生顺序），
// 只把成员访问换成 ParseIrView 的 const 指针：
//     m_scripts          -> *view.scripts
//     m_variables        -> *view.variables
//     m_constantTable    ->  view.constantTable
//     m_variableStorage  ->  view.storage
//     m_diagnostics.add  ->  diags.add
//     hasLabel(x)        ->  hasLabelInScripts(view, x)        （同一判据：全库 @标签）
//     userFunction(x)    ->  userFunctionIn(view, x)           （同一张函数登记表）
//
// 不依赖 EraParseTable：EraParseTable 只留同名 forwarder。诊断的 code/severity/
// effect/message 与产生顺序都与改造前逐条相同（sortForOutput 只是事后定序）。
// ---------------------------------------------------------------------------
#include "parse_checks.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVector>
#include <QtConcurrent>
#include <algorithm>
#include <limits>
#include <optional>

#include "ast/expression_ast.h"     // walkExpression / LiteralNode / BinaryOpNode / VariableNode
#include "ast/function_types.h"     // OperandType / isKnown / operandTypeName
#include "ast/system_variables.h"   // sysvar::systemVariableMetadataDyn / systemStorageNames
#include "constant_table.h"
#include "user_defined_variable_data.h"   // VariableConfig
#include "variable_storage.h"
#include "ast/variable_table.h"

namespace parsecheck {
namespace {

// EraParseTable::userFunction 的同源实现：m_functions 按大写名字查。
const UserFunctionInfo* userFunctionIn(const ParseIrView& view, const QString& name) {
    if (!view.functions) return nullptr;
    const auto it = view.functions->constFind(name.toUpper());
    return it == view.functions->constEnd() ? nullptr : &it.value();
}

// EraParseTable::hasLabel 的同源判据（全库 @标签存在性）：
// labelPositions 是「大写折叠」的，故查表前先 toUpper。
// 成员版另有 m_hasLabelCache；这里不缓存 —— 结果相同，收尾只跑一趟。
bool hasLabelInScripts(const ParseIrView& view, const QString& label) {
    if (!view.scripts) return false;
    const QString folded = label.toUpper();
    for (auto it = view.scripts->constBegin(); it != view.scripts->constEnd(); ++it) {
        if (it.value().labelPositions.contains(folded)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 三个收尾步骤共用的驱动：「遍历全部脚本」= 逐脚本函数 + 按名字序并行归并
//
//   1. 取 view.scripts 的键并**按名字排序**（QString::compare 序，即 std::sort
//      的默认序 —— 与 QString::operator< 同源，不加 locale）；
//   2. 每个脚本一趟，诊断写进**该任务独占**的 ParseDiagnostics（不许跨任务共享
//      同一个 sink）；
//   3. 按 names 的顺序把每个 sink 的诊断 append 回 diags。
//
// 排序不是为了好看：这三个步骤原来遍历 QHash（Qt 随机化哈希种子，迭代序在
// 不同进程间不同），诊断的**产生顺序**本来就不确定，收尾末尾靠 sortForOutput
// 兜底。并行之后若按完成顺序归并，顺序还会随线程调度抖动 —— 排一次名字就让
// 归并顺序只由输入决定，与调度无关；sortForOutput 仍是「同排序键的重复诊断」
// 的最后兜底（这里不动它）。
//
// 并行粒度刻意停在「一个 pass 内」：三个 pass **之间**仍是顺序执行（pass 表
// 顺序、gate、phaseAfter 一个都不动）。把 4/5/6 同时扔进线程池是
// 3×maxThreadCount 的过度订阅 —— pass 5/6 的脚本数相同但单脚本工作量小得多，
// 它们要么排在长任务后面白等，要么互相抢核，整体收益很小、风险（栈/内存/调度
// 抖动）却更大。
// ---------------------------------------------------------------------------

// 并行阈值：脚本数少于它（测试里通常只有 1~2 个）时，线程池调度 + 结果归并的
// 开销比检查本身还大，而且调试器还能一路跟进去。真实工程远在这条线之上
// （eraTW 2260 个脚本）。线程池只有 1 个线程时并行也没有意义，直接串行。
constexpr int kMinScriptsForParallel = 4;

template <class Fn>
void runPerScript(const ParseIrView& view, ParseDiagnostics& diags, Fn&& fn) {
    if (!view.scripts) return;   // 旧行为：没有脚本时外层循环体不执行
    QStringList names = view.scripts->keys();
    std::sort(names.begin(), names.end());

    // 先把 ScriptData 指针解析出来：任务里就只剩**纯只读**访问，不必让多个线程
    // 各自去 constFind 同一个 QHash（也顺带省掉每个任务一次哈希查找）。
    QVector<const ScriptData*> scripts;
    scripts.reserve(names.size());
    for (const QString& n : names) {
        const auto it = view.scripts->constFind(n);
        if (it != view.scripts->constEnd()) scripts.append(&it.value());
    }

    if (scripts.size() < kMinScriptsForParallel
        || QThreadPool::globalInstance()->maxThreadCount() <= 1) {
        for (const ScriptData* script : scripts) fn(*script, diags);
        return;
    }

    // blockingMapped 保持输入顺序返回；每个任务一个 local sink。
    const auto locals = QtConcurrent::blockingMapped(scripts, [&fn](const ScriptData* script) {
        ParseDiagnostics local;   // **本任务独占**，不与任何线程共享
        fn(*script, local);
        return local;
    });
    for (const ParseDiagnostics& local : locals)
        for (const ParseDiagnostic& d : local.all()) diags.add(d);
}

}  // namespace

// ---------------------------------------------------------------------------
// 收尾步骤 4：静态解析「运行期跳转/调用目标」（对齐 C# ErbLoader.setJumpTo）
//
// C# 在装载第 3 趟 setJumpTo 里把**静态可知**的跳转目标解析一次：解析不到就把
// 该行标成 IsError（运行到就抛）并计入「関数が見つからない警告」。这就是把
// 「跑到那行才炸」的错误提前到装载期的标准做法：只对**常量目标**下结论，
// 目标带格式串（%…% / {…} / [[Rename]]）时视为运行期求值，一律不下结论。
// 本实现与运行期解析**同源**（CALL 走 hasLabel、GOTO 走本函数内 $标签），
// 因此不会出现「装载期说没问题、运行期找不到」的口径差。
//
//   * CALL/CALLFORM/CALLF/CALLFORMF/JUMP/JUMPFORM：常量目标必须存在
//   * GOTO/GOTOFORM：常量 $标签必须在**本函数内**（对齐 GetLabelDollar）
//   * TRY* 系（TRYCALL/TRYJUMP/TRYCCALL/TRYCJUMP/TRYGOTO/TRYCALLEVENT…）：
//     语义就是「找不到就跳过」，不报（对齐 C# func.Function.IsTry()）
//   * CALLEVENT / BEGIN / FUNC：C# 不做目标检查（CALLEVENT 缺目标静默），跳过
void resolveStaticTargets(const ParseIrView& view, ParseDiagnostics& diags) {
    runPerScript(view, diags, [&view](const ScriptData& script, ParseDiagnostics& local) {
        resolveStaticTargetsInScript(view, script, local);
    });
}

// 逐脚本实现：结论只依赖 (view, data)，diags 是调用方给的**本线程独占** sink，
// 因而可以被驱动端（runPerScript）并行调用。
void resolveStaticTargetsInScript(const ParseIrView& view, const ScriptData& data,
                                  ParseDiagnostics& diags) {
    // 目标名：常量目标返回标签名（去掉 @/$ 前缀）；运行期求值返回空（不判定）。
    const auto staticTarget = [](const LogicalLine& line) -> QString {
        if (line.arguments.isEmpty()) return {};
        QString name = line.arguments.first().raw.trimmed();
        if (name.isEmpty()) return {};
        if (name.startsWith(QLatin1Char('@')) || name.startsWith(QLatin1Char('$')))
            name.remove(0, 1);
        if (name.contains(QLatin1Char('%')) || name.contains(QLatin1Char('{'))
            || name.contains(QLatin1String("[[")))
            return {};   // FORM 名 / [[Rename]] 占位：运行期才知道
        return name;
    };

    // 本脚本各函数的 $标签集合（大小写折叠：标签比较遵循 ICFunction 语义）。
    QHash<QString, QSet<QString>> dollarLabels;
    for (const LogicalLine& line : data.lines) {
        if (line.kind != LineKind::GotoLabel) continue;
        dollarLabels[line.ownerFunction.toUpper()].insert(line.labelName.toUpper());
    }
    for (const LogicalLine& line : data.lines) {
        bool checkFunction = false;
        bool checkLabel = false;
        switch (line.runnerInstruction) {
        case RunnerInstruction::CALL:
        case RunnerInstruction::CALLFORM:
        case RunnerInstruction::CALLF:
        case RunnerInstruction::CALLFORMF:
        case RunnerInstruction::JUMP:
        case RunnerInstruction::JUMPFORM:
            checkFunction = true;
            break;
        case RunnerInstruction::GOTO:
        case RunnerInstruction::GOTOFORM:
            checkLabel = true;
            break;
        default:
            break;   // TRY* 系（运行期静默跳过）与 CALLEVENT/FUNC 不检查
        }
        if (!checkFunction && !checkLabel) continue;
        const QString target = staticTarget(line);
        if (target.isEmpty()) continue;
        QString message;
        if (checkFunction) {
            if (hasLabelInScripts(view, target)) {
                // 目标存在 -> 装载期就把实参校验掉（对齐 C# CALL_Instruction.SetJumpTo
                // 里调用 ConvertArg；C# 只在「常量目标且能找到」时才做这一步）。
                checkCallStatementArgs(view, line, target, diags);
                continue;
            }
            message = QCoreApplication::translate("ParseDiagnostics",
                "指定的函数名\"@%1\"不存在（运行到该行会失败）").arg(target);
        } else {
            if (dollarLabels.value(line.ownerFunction.toUpper()).contains(target.toUpper()))
                continue;
            message = QCoreApplication::translate("ParseDiagnostics",
                "指定的标签名\"$%1\"在当前函数内不存在（运行到该行会失败）").arg(target);
        }
        diags.add(DiagSeverity::Error, DiagCode::kLabelNotFound,
            line.position.filename, line.position.lineNumber, line.position.column, 0, message);
    }
}

// ---------------------------------------------------------------------------
// CALL 语句的实参校验（对齐 C# Process.CalledFunction.ConvertArg）
//
// C# 在**装载第 3 趟**（setJumpTo）里对「常量目标且能找到该函数」的 CALL 调一次
// ConvertArg，把实参个数/类型不匹配从「运行到那行才抛 CodeEE」提前到装载期。
// 这里复刻同一组判据（与 checkUserCallArgs 同源，只是实参来自语句的 Operand 表）：
//
//   * 实参个数 > 形参个数            -> 错误（C#「引数の数が…超えています」）
//   * 字符串实参 -> 整型形参         -> 错误（任何配置下都不允许）
//   * 整型实参 -> 字符串形参         -> 允许（CompatiFuncArgAutoConvert；见 checkUserCallArgs）
//   * REF 形参：实参必须是同类型、同维数的非角色数组变量，且**不可省略**
//
// 刻意**不报**的两类（宁缺勿错）：
//   * 实参少于形参：C# 会对 ARG/ARGS/私有变量形参补默认值（parseLabel 的 canDef），
//     实际等价于「永远有默认值」，因此这里不下结论；
//   * 目标带 %…%/{…}/[[…]]（运行期求值）、TRY* 系（语义就是找不到就跳过）。
void checkCallStatementArgs(const ParseIrView& view, const LogicalLine& line,
                            const QString& target, ParseDiagnostics& diags) {
    const UserFunctionDecl* decl = userFunctionIn(view, target.toUpper());
    if (!decl) return;   // 目标存在但没登记成函数（BEGIN/CALLEVENT 之类）：不判定

    // 实参表 = 目标名之后的全部操作数（剔除 "," 独立分隔符，与 ArgumentParser 同口径）
    QList<const Operand*> actuals;
    actuals.reserve(line.arguments.size());
    for (int i = 1; i < line.arguments.size(); ++i) {
        const Operand& o = line.arguments.at(i);
        if (!o.isString && (o.raw == QLatin1String(",") || o.raw == QLatin1String(":"))) continue;
        actuals.append(&o);
    }
    const int want = decl->paramCount();
    if (actuals.size() > want) {
        diags.add(DiagSeverity::Error, DiagCode::kArgCheck,
            line.position.filename, line.position.lineNumber, line.position.column, 0,
            QCoreApplication::translate("ParseDiagnostics",
                "函数 @%1 的实参过多（形参 %2 个，实得 %3 个；运行到该行会失败）")
                .arg(decl->originalName.isEmpty() ? decl->name : decl->originalName)
                .arg(want).arg(actuals.size()));
        return;
    }
    for (int i = 0; i < want; ++i) {
        const UserParamDecl& formal = decl->params.at(i);
        const QString shown = decl->originalName.isEmpty() ? decl->name : decl->originalName;
        // 形参多于实参时（i >= actuals.size()）等价于 C# 的 term == null（实参省略）。
        const Operand* actualPtr = (i < actuals.size()) ? actuals.at(i) : nullptr;
        const bool omitted = !actualPtr || actualPtr->omitted;
        const QSharedPointer<ExpressionNode> ast = actualPtr ? actualPtr->ast : QSharedPointer<ExpressionNode>();
        const auto* var = ast ? dynamic_cast<const VariableNode*>(ast.data()) : nullptr;
        if (formal.isReference) {
            // C# ConvertArg：REF 形参省略 = 错误；实参必须是同类型、同维数的非角色数组变量。
            QString err;
            if (omitted) {
                err = QCoreApplication::translate("ParseDiagnostics",
                    "函数 @%1 的第 %2 个参数是引用传递（REF），不能省略（运行到该行会失败）");
            } else if (!var) {
                err = QCoreApplication::translate("ParseDiagnostics",
                    "函数 @%1 的第 %2 个参数是引用传递（REF），实参必须是数组变量（运行到该行会失败）");
            } else if (var->characterData || var->readOnly
                       || (isKnown(formal.type) && isKnown(var->valueType())
                           && var->valueType() != formal.type)) {
                err = QCoreApplication::translate("ParseDiagnostics",
                    "函数 @%1 的第 %2 个 REF 实参需要同类型、同维数的非角色数组变量（运行到该行会失败）");
            }
            if (!err.isEmpty())
                diags.add(DiagSeverity::Error, DiagCode::kArgCheck,
                    line.position.filename, line.position.lineNumber, line.position.column, 0,
                    err.arg(shown).arg(i + 1));
            continue;
        }
        if (omitted || !ast) continue;
        if (!formal.typeKnown || !isKnown(formal.type)) continue;
        const OperandType actualType = ast->valueType();
        if (!isKnown(actualType)) continue;
        if (actualType == OperandType::Str && formal.type == OperandType::Int) {
            diags.add(DiagSeverity::Error, DiagCode::kArgCheck,
                line.position.filename, line.position.lineNumber, line.position.column, 0,
                QCoreApplication::translate("ParseDiagnostics",
                    "函数 @%1 的第 %2 个实参需要%3，实得字符串（不能从字符串转换为整数；运行到该行会失败）")
                    .arg(shown).arg(i + 1)
                    .arg(QString::fromUtf8(operandTypeName(formal.type))));
        }
    }
}

// ---------------------------------------------------------------------------
// 收尾步骤 5：结构/配对检查（对齐 C# ErbLoader.nestCheck 的诊断部分）
//
// C# 在装载第 2 趟（nestCheck）里用一个 nestStack 走完每个函数体：配对不上的
// 结构、越过块边界的指令、SIF 的次行、RETURNF 的位置/类型都在这里报到装载期。
// 本实现只做**诊断**——配对的跳转标记早已由 buildJumpMarkings 建好（运行期消费），
// 这里按同一判据（同一个 nestStack 语义）把「结构性错误」讲出来。
//
// 刻意不做的：C# 里那些 level 0/1 的提示（「SIF文の次の行が空行…」、
// 「関数が定義されるより前に行があります」）——eraTW 这类真实工程里遍地都是，
// 报出来只会淹没真正的问题。
// ---------------------------------------------------------------------------
namespace {

// END 系列 -> 它要求的配对结构名（C# FunctionIdentifier.getMatchFunction）
QString matchingOpener(const QString& ender) {
    if (ender == QLatin1String("REND")) return QStringLiteral("REPEAT");
    if (ender == QLatin1String("NEXT")) return QStringLiteral("FOR");
    if (ender == QLatin1String("WEND")) return QStringLiteral("WHILE");
    if (ender == QLatin1String("ENDIF")) return QStringLiteral("IF");
    if (ender == QLatin1String("ENDSELECT")) return QStringLiteral("SELECTCASE");
    if (ender == QLatin1String("ENDCATCH")) return QStringLiteral("CATCH");
    if (ender == QLatin1String("ENDLIST")) return QStringLiteral("DATALIST");
    if (ender == QLatin1String("ENDNOSKIP")) return QStringLiteral("NOSKIP");
    if (ender == QLatin1String("ENDFUNC")) return QStringLiteral("TRYCALLLIST");
    return QString();
}

bool isLoopOpener(const QString& name) {
    return name == QLatin1String("REPEAT") || name == QLatin1String("FOR")
        || name == QLatin1String("WHILE") || name == QLatin1String("DO");
}

bool isPrintDataFamily(const QString& name) {
    return name.startsWith(QLatin1String("PRINTDATA")) || name == QLatin1String("STRDATA");
}

bool isTryListOpener(const QString& name) {
    return name == QLatin1String("TRYCALLLIST") || name == QLatin1String("TRYJUMPLIST")
        || name == QLatin1String("TRYGOTOLIST") || name == QLatin1String("GOTOLIST");
}

bool isTryCatchOpener(const QString& name) {
    return name == QLatin1String("TRYCCALL") || name == QLatin1String("TRYCCALLFORM")
        || name == QLatin1String("TRYCJUMP") || name == QLatin1String("TRYCJUMPFORM")
        || name == QLatin1String("TRYCGOTO") || name == QLatin1String("TRYCGOTOFORM");
}

// 开块指令 -> 它需要的收尾指令名（用于「Xに対応するYが見つかりません」）
QString matchingCloser(const QString& opener) {
    if (opener == QLatin1String("IF")) return QStringLiteral("ENDIF");
    if (opener == QLatin1String("REPEAT")) return QStringLiteral("REND");
    if (opener == QLatin1String("FOR")) return QStringLiteral("NEXT");
    if (opener == QLatin1String("WHILE")) return QStringLiteral("WEND");
    if (opener == QLatin1String("DO")) return QStringLiteral("LOOP");
    if (opener == QLatin1String("SELECTCASE")) return QStringLiteral("ENDSELECT");
    if (opener == QLatin1String("CATCH")) return QStringLiteral("ENDCATCH");
    if (opener == QLatin1String("DATALIST")) return QStringLiteral("ENDLIST");
    if (opener == QLatin1String("NOSKIP")) return QStringLiteral("ENDNOSKIP");
    if (isTryCatchOpener(opener)) return QStringLiteral("CATCH");
    if (isPrintDataFamily(opener)) return QStringLiteral("ENDDATA");
    if (isTryListOpener(opener)) return QStringLiteral("ENDFUNC");
    return QString();
}


}  // namespace

void checkStructures(const ParseIrView& view, ParseDiagnostics& diags) {
    runPerScript(view, diags, [&view](const ScriptData& script, ParseDiagnostics& local) {
        checkStructuresInScript(view, script, local);
    });
}

// 逐脚本实现：结论只依赖 (view, data)，diags 是调用方给的**本线程独占** sink，
// 因而可以被驱动端（runPerScript）并行调用。
void checkStructuresInScript(const ParseIrView& view, const ScriptData& data,
                             ParseDiagnostics& diags) {
    // nestStack 的表项 = (结构名, 行, 该结构是否已出现第一个分支)。
    struct Nest { QString name; const LogicalLine* line; bool branchSeen = false; };
    const auto add = [&diags](DiagSeverity sev, const LogicalLine& line, const QString& text) {
        diags.add(sev, DiagCode::kStructure,
            line.position.filename, line.position.lineNumber, line.position.column, 0, text);
    };

    QList<Nest> stack;
    const auto closeBody = [&]() {
        // 函数末尾仍有未闭合的结构：逐条报「Xに対応するYが見つかりません」。
        while (!stack.isEmpty()) {
            const Nest n = stack.takeLast();
            add(DiagSeverity::Error, *n.line,
                QCoreApplication::translate("ParseDiagnostics",
                    "%1 に対応する %2 が見つかりません").arg(n.name, matchingCloser(n.name)));
        }
    };
    for (const LogicalLine& line : data.lines) {
        if (line.kind == LineKind::FunctionLabel) {
            closeBody();
            continue;
        }
        if (line.kind == LineKind::GotoLabel) {
            // C#：PRINTDATA 系 / STRDATA / DATALIST / TRY*LIST 体内不允许 $标签
            if (!stack.isEmpty()
                && (isPrintDataFamily(stack.last().name)
                    || stack.last().name == QLatin1String("DATALIST")
                    || isTryListOpener(stack.last().name))) {
                add(DiagSeverity::Error, line,
                    QCoreApplication::translate("ParseDiagnostics",
                        "%1 構文中に $ラベルを定義することはできません").arg(stack.last().name));
            }
            continue;
        }
        if (line.kind != LineKind::Instruction) continue;   // 空行/注释行/# 行不参与配对
        const QString& name = line.functionName;

        // ---- 块内允许的指令（对齐 C# nestCheck 的 baseFunc 检查）----
        if (!stack.isEmpty()) {
            const QString& base = stack.last().name;
            if (isPrintDataFamily(base)) {
                const bool allowed = name == QLatin1String("DATA")
                    || name == QLatin1String("DATAFORM")
                    || name == QLatin1String("DATALIST")
                    || name == QLatin1String("ENDLIST")
                    || name == QLatin1String("ENDDATA");
                if (!allowed) {
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "%1 構文に使用できない命令'%2'が含まれています").arg(base, name));
                    continue;
                }
            } else if (base == QLatin1String("DATALIST")) {
                const bool allowed = name == QLatin1String("DATA")
                    || name == QLatin1String("DATAFORM") || name == QLatin1String("ENDLIST");
                if (!allowed) {
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "DATALIST 構文に使用できない命令'%1'が含まれています").arg(name));
                    continue;
                }
            } else if (isTryListOpener(base)) {
                if (name != QLatin1String("FUNC") && name != QLatin1String("ENDFUNC")) {
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "%1 構文に使用できない命令'%2'が含まれています").arg(base, name));
                    continue;
                }
            } else if (base == QLatin1String("SELECTCASE")) {
                // SELECTCASE 直下、最初の CASE より前は命令を置けない
                const bool allowed = name == QLatin1String("CASE")
                    || name == QLatin1String("CASEELSE") || name == QLatin1String("ENDSELECT");
                if (!stack.last().branchSeen && !allowed) {
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "SELECTCASE 構文の分岐の外に命令'%1'が含まれています").arg(name));
                    continue;
                }
            }
        }

        // ---- 入栈 ----
        if (name == QLatin1String("IF") || name == QLatin1String("SELECTCASE")
            || name == QLatin1String("REPEAT") || name == QLatin1String("FOR")
            || name == QLatin1String("WHILE") || name == QLatin1String("DO")
            || isTryCatchOpener(name) || isPrintDataFamily(name)
            || isTryListOpener(name) || name == QLatin1String("NOSKIP")) {
            stack.append({name, &line});
            continue;
        }

        // ---- 分支指令 ----
        if (name == QLatin1String("ELSEIF") || name == QLatin1String("ELSE")) {
            if (stack.isEmpty() || stack.last().name != QLatin1String("IF")) {
                add(DiagSeverity::Error, line,
                    QCoreApplication::translate("ParseDiagnostics",
                        "IF～ENDIFの外で%1文が使われました").arg(name));
            } else {
                stack.last().branchSeen = true;
            }
            continue;
        }
        if (name == QLatin1String("CASE") || name == QLatin1String("CASEELSE")) {
            if (stack.isEmpty() || stack.last().name != QLatin1String("SELECTCASE"))
                add(DiagSeverity::Error, line,
                    QCoreApplication::translate("ParseDiagnostics",
                        "SELECTCASE～ENDSELECTの外で%1文が使われました").arg(name));
            else
                stack.last().branchSeen = true;
            continue;
        }
        if (name == QLatin1String("BREAK") || name == QLatin1String("CONTINUE")) {
            bool inLoop = false;
            for (const Nest& n : stack)
                if (isLoopOpener(n.name)) { inLoop = true; break; }
            if (!inLoop)
                add(DiagSeverity::Error, line,
                    QCoreApplication::translate("ParseDiagnostics",
                        "REPEAT, FOR, WHILE, DOの中以外で%1文が使われました").arg(name));
            continue;
        }
        if (name == QLatin1String("FUNC")) {
            if (stack.isEmpty() || !isTryListOpener(stack.last().name))
                add(DiagSeverity::Error, line,
                    QCoreApplication::translate("ParseDiagnostics",
                        "対応するTRYCALLLIST系命令のない%1です").arg(name));
            continue;
        }
        if (name == QLatin1String("DATA") || name == QLatin1String("DATAFORM")) {
            if (stack.isEmpty() || !(isPrintDataFamily(stack.last().name)
                                     || stack.last().name == QLatin1String("DATALIST")))
                add(DiagSeverity::Error, line,
                    QCoreApplication::translate("ParseDiagnostics",
                        "対応するPRINTDATA系命令のない%1です").arg(name));
            continue;
        }
        if (name == QLatin1String("DATALIST")) {
            // DATALIST 是 PRINTDATA 系 / STRDATA 体内的**可重复**子块：
            // 一个 PRINTDATA 里可以连续放多个 DATALIST…ENDLIST（eraTW 的
            // CASINO.ERB 就是这种写法），所以这里必须入栈，否则随后的 ENDLIST
            // 找不到 DATALIST 而误报。
            if (stack.isEmpty() || !isPrintDataFamily(stack.last().name)) {
                add(DiagSeverity::Error, line,
                    QCoreApplication::translate("ParseDiagnostics",
                        "対応するPRINTDATA系命令のない%1です").arg(name));
            } else {
                stack.append({name, &line, false});
            }
            continue;
        }
        if (name == QLatin1String("LOOP")) {
            // LOOP 对应 REPEAT 或 DO（按栈顶判定）
            const bool ok = !stack.isEmpty()
                && (stack.last().name == QLatin1String("DO") || stack.last().name == QLatin1String("REPEAT"));
            if (!ok) {
                add(DiagSeverity::Error, line,
                    QCoreApplication::translate("ParseDiagnostics",
                        "対応するREPEATの無い%1文です").arg(name));
            } else {
                stack.removeLast();
            }
            continue;
        }
        if (name == QLatin1String("REND") || name == QLatin1String("NEXT")
            || name == QLatin1String("WEND") || name == QLatin1String("ENDIF")
            || name == QLatin1String("ENDSELECT") || name == QLatin1String("ENDCATCH")
            || name == QLatin1String("ENDLIST") || name == QLatin1String("ENDDATA")
            || name == QLatin1String("ENDNOSKIP") || name == QLatin1String("ENDFUNC")) {
            const QString want = matchingOpener(name);
            if (name == QLatin1String("ENDDATA")) {
                if (stack.isEmpty() || !(isPrintDataFamily(stack.last().name))) {
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "対応するPRINTDATA系命令もしくはSTRDATAのない%1です").arg(name));
                } else {
                    stack.removeLast();
                }
                continue;
            }
            bool matched = false;
            if (name == QLatin1String("ENDFUNC")) {
                matched = !stack.isEmpty() && isTryListOpener(stack.last().name);
            } else {
                matched = !stack.isEmpty() && stack.last().name == want;
            }
            if (!matched) {
                add(DiagSeverity::Error, line,
                    QCoreApplication::translate("ParseDiagnostics",
                        "対応する%1の無い%2文です").arg(want, name));
            } else {
                stack.removeLast();
            }
            continue;
        }
        if (name == QLatin1String("CATCH")) {
            if (stack.isEmpty() || !isTryCatchOpener(stack.last().name))
                add(DiagSeverity::Error, line,
                    QCoreApplication::translate("ParseDiagnostics",
                        "対応するTRYC系命令がありません"));
            else
                stack.last().name = QStringLiteral("CATCH");
            continue;
        }
        // ---- SIF：次行必须是可跳过的有效指令 ----
        if (name == QLatin1String("SIF")) {
            const int i = line.lineIndex;
            int next = i + 1;
            const int size = int(data.lines.size());
            while (next < size && data.lines.at(next).kind == LineKind::Null) ++next;
            if (next >= size || data.lines.at(next).kind == LineKind::FunctionLabel
                || data.lines.at(next).kind == LineKind::GotoLabel) {
                add(DiagSeverity::Error, line,
                    QCoreApplication::translate("ParseDiagnostics",
                        "SIF文の次の行がありません"));
                continue;
            }
            const LogicalLine& nextLine = data.lines.at(next);
            if (nextLine.kind == LineKind::Instruction) {
                // SIF/TRYC 系也吃下一行（C# PARTIAL）：再套一个就会错位
                const QString& nf = nextLine.functionName;
                if (nf == QLatin1String("SIF") || isTryCatchOpener(nf)
                    || nf == QLatin1String("TRYCALLLIST") || nf == QLatin1String("TRYJUMPLIST")
                    || nf == QLatin1String("TRYGOTOLIST"))
                    add(DiagSeverity::Error, line,
                        QCoreApplication::translate("ParseDiagnostics",
                            "SIF文の次の行を%1文にすることはできません").arg(nf));
            }
            continue;
        }
        // ---- RETURNF：必须在 #FUNCTION(S) 内，且返回类型要一致 ----
        if (name == QLatin1String("RETURNF")) {
            const UserFunctionDecl* decl = userFunctionIn(view, line.ownerFunction.toUpper());
            if (!decl || !decl->isMethod) {
                add(DiagSeverity::Error, line,
                    QCoreApplication::translate("ParseDiagnostics",
                        "RETURNFは#FUNCTION以外では使用できません"));
                continue;
            }
            if (line.arguments.isEmpty()) continue;
            const auto& operand = line.arguments.first();
            if (!operand.ast) continue;
            const OperandType actual = operand.ast->valueType();
            if (!isKnown(actual) || !isKnown(decl->returnType)) continue;
            if (actual != decl->returnType) {
                add(DiagSeverity::Error, line,
                    decl->returnType == OperandType::Int
                        ? QCoreApplication::translate("ParseDiagnostics",
                              "#FUNCTIONで始まる関数の戻り値に文字列型が指定されました")
                        : QCoreApplication::translate("ParseDiagnostics",
                              "#FUNCTIONSで始まる関数の戻り値に数値型が指定されました"));
            }
            continue;
        }
    }
    closeBody();
}

// ---------------------------------------------------------------------------
// 收尾步骤 6：可静态判定的运行期错误（字面量除零 + 数组边界条件）
//
// 【除零】`X / 0`、`X % 0` 在 C# 里是运行期必抛的 CodeEE「0による除算が行なわれました」；
// 除数为**字面量 0** 时「运行必炸」是确定事件，因此提前到装载期报出来。
// 除数不是字面量（`X / Y`）时不判定 —— 那要运行期才知道。
//
// 【数组边界】「变量 X 常量下标越界」的主战场在 validateExpression（步骤 3，按
// 声明 lengths 判，见 expression_ast.cpp）；这里只补它**看不到尺寸**的盲区：
//   · 系统变量/角色系统变量（CFLAG/ITEM/DA/TA…）：walkApply 把 lengths 清空，
//     步骤 3 无从判 —— 尺寸来自 VariableConfig（VariableSize.csv + 默认表）。
//     运行期系统槽是**硬上限**（越界写被静默丢弃，读为 0）—— 今天不跑游戏
//     根本发现不了，正是「希望不需要运行期间才能被检测」的一类。
//   · 负常量下标：任何数组的下标下界恒为 0（运行期读 0/写忽略），lengths
//     未知的位置（未声明名/REF/无尺寸声明）也一律可判。
//     注意 `-1` 不是折叠字面量而是 UnaryOp(MINUS,Literal)（解析器只折叠
//     #DIM CONST 名）—— 连 validateExpression 都看不见这个形态，负下标必须
//     在这里单独收集。
//   （FOR/REPEAT 计数器**不做**循环值检查：计数器写的是「固定下标上的循环值」
//     —— value 是值不是下标（writeLoopCounter：resolveIndices(计数器自身下标) +
//     写 value）。固定下标越界已由上面的常量下标检查覆盖（计数器操作数也在
//     line.arguments 里，会被 walk 到）。第一版按「循环值当下标」实现，
//     FL3.8 的标量计数器 `FOR L_CNT, 0, N` 全是误报 —— 按运行期真实模型删掉。）
// 与 C# 口径一致（ConstantData.cs 对常量下标就是 ParserMediator.Warn + 运行期
// OoRArray/OoRVarArg CodeEE）。本引擎运行期对这些一律宽容（自动扩容/静默忽略），
// 故只报 **Warning**、不禁行 —— 诊断归诊断，行为零变化。
// 去重口径：声明数组 lengths 已知的位置步骤 3 已按 Error 处置（行已失效），
// 这里不再重复报；*NAME 名表变量（ABLNAME…）的真实尺寸 = CSV 行数，与
// VariableSize 同名条目无关，只查负数。
// ---------------------------------------------------------------------------
void checkStaticValues(const ParseIrView& view, ParseDiagnostics& diags) {
    runPerScript(view, diags, [&view](const ScriptData& script, ParseDiagnostics& local) {
        checkStaticValuesInScript(view, script, local);
    });
}

// 逐脚本实现：结论只依赖 (view, data)，diags 是调用方给的**本线程独占** sink，
// 因而可以被驱动端（runPerScript）并行调用。
void checkStaticValuesInScript(const ParseIrView& view, const ScriptData& data,
                               ParseDiagnostics& diags) {
    // 尺寸来源：VariableConfig（VariableSize.csv 覆盖 + 构造即就绪的默认表）。
    const VariableConfig* config =
        view.storage ? &view.storage->variableConfig() : nullptr;

    // *NAME 名表变量（含 CDFLAGNAME1/2）：尺寸 = 对应 CSV 的行数，不是 VariableSize。
    const auto isNameTable = [](const QString& upper) {
        return upper.endsWith(QLatin1String("NAME"))
            || upper == QLatin1String("CDFLAGNAME1") || upper == QLatin1String("CDFLAGNAME2");
    };

    // 系统/角色变量的**元素**每维尺寸；空 = 未知（不判上界）。角色号位不在内。
    const auto systemElementSizes = [&](const QString& upper, int elementDim,
                                        bool characterData) {
        QVector<int> sizes;
        if (!config || isNameTable(upper) || elementDim <= 0) return sizes;
        if (characterData || elementDim == 1) {
            // 角色变量的 VariableSize 条目就是每角色的元素数（对齐运行期
            // execution_engine 的 arraySize 口径）
            sizes.append(config->getSize1D(upper));
        } else if (elementDim == 2) {
            const QPair<int, int> s2 = config->getSize2D(upper);
            sizes.append(s2.first);
            sizes.append(s2.second);
        } else {
            sizes = config->getSize3D(upper);
        }
        return sizes;
    };

    // 常量值识别：字面量，或「负字面量」—— `-1` **不是**折叠字面量而是
    // UnaryOp(MINUS, Literal)（解析器只折叠 #DIM CONST 名，负号不走折叠），
    // 连 validateExpression 的既有检查也只认纯字面量 —— 负形态正是它的盲区。
    // negated = 负形态：该位置的「步骤 3 已报过」结论不成立（它根本看不见）。
    const auto constValueOf = [](const QSharedPointer<ExpressionNode>& ast,
                                 bool* negated = nullptr) -> std::optional<qint64> {
        if (negated) *negated = false;
        if (!ast) return std::nullopt;
        if (ast->kind() == NodeKind::Literal) {
            const auto* l = static_cast<const LiteralNode*>(ast.data());
            if (l->isString()) return std::nullopt;
            return l->intValue();
        }
        if (ast->kind() == NodeKind::UnaryOp) {
            const auto& u = static_cast<const UnaryOpNode&>(*ast);
            if (u.op().type() != TokenType::MINUS || u.isPostfix() || !u.operand()
                || u.operand()->kind() != NodeKind::Literal)
                return std::nullopt;
            const auto* l = static_cast<const LiteralNode*>(u.operand().data());
            if (l->isString()) return std::nullopt;
            // -INT64_MIN 的取反是 UB：这种字面量本身就已被数字诊断拦过，不再判
            if (l->intValue() == std::numeric_limits<qint64>::min()) return std::nullopt;
            if (negated) *negated = true;
            return -l->intValue();
        }
        return std::nullopt;
    };

    for (const LogicalLine& line : data.lines) {
        const auto visit = [&](ExpressionNode& node) {
            if (node.kind() == NodeKind::BinaryOp) {
                auto& op = static_cast<BinaryOpNode&>(node);
                const TokenType t = op.op().type();
                if (t != TokenType::DIVIDE && t != TokenType::MODULO) return;
                const auto& right = op.right();
                if (!right || right->kind() != NodeKind::Literal) return;
                const auto& lit = static_cast<const LiteralNode&>(*right);
                if (lit.valueType() != OperandType::Int || lit.intValue() != 0) return;
                diags.addReportOnly(DiagSeverity::Error, DiagCode::kStaticValue,
                    line.position.filename, line.position.lineNumber,
                    line.position.column, 0,
                    QCoreApplication::translate("ParseDiagnostics",
                        "0 による除算が行われました（运行到该行会失败）"));
                return;
            }
            // ---- 常量下标边界（步骤 3 看不到尺寸的名字才判）----
            if (node.kind() != NodeKind::Variable || line.isError) return;
            // VARSIZE 只消费数组身份（语言规定其下标被忽略，见 finalizeValidation
            // 的同款豁免）—— 下标形态（含字符串）不参与运行期求值，不判。
            if (line.functionName == QLatin1String("VARSIZE")) return;
            const auto& var = static_cast<const VariableNode&>(node);
            const int n = var.indices().size();
            if (n <= 0 || n > 3) return;
            int constPos[3] = {0, 0, 0};
            qint64 constVal[3] = {0, 0, 0};
            bool constNeg[3] = {false, false, false};
            int constCount = 0;
            for (int i = 0; i < n; ++i) {
                bool negated = false;
                const auto v = constValueOf(var.indices().at(i), &negated);
                if (!v) continue;
                constPos[constCount] = i;
                constVal[constCount] = *v;
                constNeg[constCount] = negated;
                ++constCount;
            }
            // 字面字符串下标：运行期 resolveIndex 对字符串下标必须经
            // ConstantTable 映射到列号（CFLAG:0:"攻撃" → 0），映射不到必抛
            // 「Unknown variable index keyword」—— 与除零同级的「运行必炸」，
            // 提前到装载期报（C# 侧对应 CSV 名表的 IdentifierNotFound）。
            // 只认字面量形态：FORM（@"…"）下标的值运行期才知道，不判。
            if (view.constantTable) {
                for (int i = 0; i < n; ++i) {
                    const auto& idx = var.indices().at(i);
                    if (!idx || idx->kind() != NodeKind::Literal) continue;
                    const auto* l = static_cast<const LiteralNode*>(idx.data());
                    if (!l->isString()) continue;
                    if (view.constantTable->indexForVariable(var.name(), l->strValue()) >= 0)
                        continue;
                    diags.addReportOnly(DiagSeverity::Error, DiagCode::kStaticValue,
                        line.position.filename, line.position.lineNumber,
                        line.position.column, 0,
                        QCoreApplication::translate("ParseDiagnostics",
                            "变量 %1 的字符串下标 \"%2\" 未在常数名表中定义（运行到该行会失败）")
                            .arg(var.name(), l->strValue()));
                }
            }
            if (!constCount) return;

            const QString upper = var.name().toUpper();
            const sysvar::SystemVariableMetadata meta =
                sysvar::systemVariableMetadataDyn(var.name().toStdString());
            // bounds[p]：位置 p 的静态上界（-1 = 未知）。
            // veCovered[p]：纯字面量在该位置时步骤 3 已按 lengths 报过（不重复）。
            // charaIdPos[p]：角色号位（运行期角色表决定，装载期不判）。
            qint64 bounds[3] = {-1, -1, -1};
            bool veCovered[3] = {false, false, false};
            bool charaIdPos[3] = {false, false, false};

            // 系统变量（含 CFLAG/TALENT/…、DA/TA/…、LOCAL/ARG 槽位）：走系统/
            // 槽位通道，#DIM 同名声明遮蔽不了（与 computeVariableAccess 同口径）；
            // lengths 在 walkApply 被清空，正是步骤 3 的盲区。
            if (sysvar::systemStorageNames().contains(upper)
                || meta.type != OperandType::Unknown) {
                const int full = meta.dimension + (meta.characterData ? 1 : 0);
                if (n != full) return;   // 维数不符：步骤 3 已按 Error 处置
                const QVector<int> sizes =
                    systemElementSizes(upper, meta.dimension, meta.characterData);
                for (int p = 0; p < n; ++p) {
                    if (meta.characterData && p == 0) {
                        charaIdPos[p] = true;   // 角色号位：运行期角色表决定
                        continue;
                    }
                    const int dim = meta.characterData ? p - 1 : p;
                    if (dim >= 0 && dim < sizes.size() && sizes.at(dim) > 0)
                        bounds[p] = sizes.at(dim);
                }
            } else if (const VariableDecl* d =
                           view.variables->find(var.name(), line.ownerFunction)) {
                // 声明数组：纯字面量 + lengths 已知的位置步骤 3 已报（不重复）；
                // lengths 缺失（标量 #DIM X / REF）时只剩这里补查。角色号位不判。
                const int full = d->dimension + (d->isCharaData ? 1 : 0);
                if (n != full) return;
                for (int p = 0; p < n; ++p) {
                    if (d->isCharaData && p == 0) {
                        charaIdPos[p] = true;
                        continue;
                    }
                    const int dim = d->isCharaData ? p - 1 : p;
                    if (dim >= 0 && dim < d->lengths.size() && d->lengths.at(dim) > 0)
                        veCovered[p] = true;   // 步骤 3 已按 lengths 报过
                }
            }
            // 其余（未声明裸名）：自动扩容无上界 —— 只剩负数可判。

            for (int c = 0; c < constCount; ++c) {
                const int p = constPos[c];
                const qint64 v = constVal[c];
                if (charaIdPos[p]) continue;
                if (veCovered[p] && !constNeg[c]) continue;
                if (v < 0) {
                    diags.addReportOnly(DiagSeverity::Warning, DiagCode::kStaticValue,
                        line.position.filename, line.position.lineNumber,
                        line.position.column, 0,
                        QCoreApplication::translate("ParseDiagnostics",
                            "变量 %1 的常量下标 %2 为负（下标下界恒为 0；运行期该访问静默失败）")
                            .arg(var.name()).arg(v));
                } else if (bounds[p] >= 0 && v >= bounds[p]) {
                    diags.addReportOnly(DiagSeverity::Warning, DiagCode::kStaticValue,
                        line.position.filename, line.position.lineNumber,
                        line.position.column, 0,
                        QCoreApplication::translate("ParseDiagnostics",
                            "变量 %1 的常量下标 %2 越过其尺寸 %3（运行期读取为 0、写入被忽略）")
                            .arg(var.name()).arg(v).arg(bounds[p]));
                }
            }
        };
        for (const Operand& op : line.arguments) {
            if (op.ast) walkExpression(*op.ast, visit);
        }
        if (line.condition) walkExpression(*line.condition, visit);
        for (const auto& ast : line.assignmentAsts) {
            if (ast) walkExpression(*ast, visit);
        }
        for (const auto& ast : line.columnDefaults()) {
            if (ast) walkExpression(*ast, visit);
        }
    }
}

}  // namespace parsecheck
