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
//     hasLabel(x)        ->  hasLabelInScripts(allLabels, x)   （同一判据：全库标签名）
//     GetLabelDollar(x)  ->  position::firstGotoLabel(...)     （同一份标签索引 + 段身份）
//     userFunction(x)    ->  userFunctionIn(view, x)           （同一张函数登记表）
//
// 不依赖 EraParseTable：EraParseTable 只留同名 forwarder。诊断的 code/severity/
// effect/message 与产生顺序都与改造前逐条相同（sortForOutput 只是事后定序）。
// ---------------------------------------------------------------------------
#include "parse_checks.h"

#include <QCoreApplication>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVector>
#include <QtConcurrent>
#include <algorithm>

#include "ast/expression_ast.h"     // walkExpression / LiteralNode / BinaryOpNode / VariableNode
#include "ast/function_types.h"     // OperandType / isKnown / operandTypeName
#include "ast/index_bounds.h"       // 下标/尺寸判定与处置表（3/6 共用的唯一一份）
#include "ast/system_variables.h"   // sysvar::systemVariableMetadataDyn / systemStorageNames
#include "constant_table.h"
#include "jump_markings.h"   // 配对记录（structureIssues）与词汇表
#include "position_state.h"   // firstGotoLabel：GOTO 目标的**唯一**择一规则（与运行期同一函数）
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

// 全库标签名并集（大小写折叠后的键）：唯一标签索引的键就是
// `line.labelName.toUpper()`（era_parse_table.cpp 的装载循环），因此这里直接并
// 键即可，与「逐个脚本 contains(folded)」是同一个成员判定 —— 重复标签只覆盖同键。
// **只活在一次 pass 调用内**（驱动层建、任务里查）：LSP 会增删脚本，跨调用缓存会过期。
QSet<QString> collectLabelNames(const ParseIrView& view) {
    QSet<QString> all;
    if (!view.labelIndex) return all;
    all.reserve(view.labelIndex->size());
    for (auto it = view.labelIndex->constBegin(); it != view.labelIndex->constEnd(); ++it)
        all.insert(it.key());
    return all;
}

// EraParseTable::hasLabel 的同源判据（全库标签存在性）：
// 索引键是「大写折叠」的，故查表前先 toUpper。
// 旧实现是「每条 CALL 扫全部脚本 × contains」（eraTW ≈ 9.4 万 CALL × 2258 脚本
// ≈ 2×10⁸ 次哈希探测）；现在只查驱动层建好的并集，一次哈希。
//
// **分工（与 GOTO 的段身份互不替代）**：
//   * 本函数回答「**全库**有没有这个名字」—— 与所在行、所在函数段都无关；
//     只有 CALL/JUMP（目标是 @函数名，跨脚本合法）用它。@ 与 $ 同名共存时
//     它也会命中（eg. eraMegaten 的 $PRINT_TITLE / @PRINT_TITLE）—— 与
//     EraParseTable::hasLabel 的旧判据一致，这里不改。
//   * GOTO 的 $标签归属要看**段身份**（(script, LabelRef::functionLine)），
//     由 position::firstGotoLabel 回答（见 resolveStaticTargetsInScript）。
bool hasLabelInScripts(const QSet<QString>& allLabels, const QString& label) {
    return allLabels.contains(label.toUpper());
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
    // 名字与数据一起带着走：GOTO 的段身份要 (script, functionLine) 两元组，
    // 而 ScriptData 里没有自己的名字（position.filename 不是脚本表的键）。
    QVector<QPair<QString, const ScriptData*>> scripts;
    scripts.reserve(names.size());
    for (const QString& n : names) {
        const auto it = view.scripts->constFind(n);
        if (it != view.scripts->constEnd()) scripts.append({n, &it.value()});
    }

    if (scripts.size() < kMinScriptsForParallel
        || QThreadPool::globalInstance()->maxThreadCount() <= 1) {
        for (const auto& script : scripts) fn(script.first, *script.second, diags);
        return;
    }

    // blockingMapped 保持输入顺序返回；每个任务一个 local sink。
    const auto locals = QtConcurrent::blockingMapped(
        scripts, [&fn](const QPair<QString, const ScriptData*>& script) {
            ParseDiagnostics local;   // **本任务独占**，不与任何线程共享
            fn(script.first, *script.second, local);
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
// 本实现与运行期解析**同源**（CALL 走全库标签名并集、GOTO 走**同一段**的 $标签），
// 因此不会出现「装载期说没问题、运行期找不到」的口径差。
//
// GOTO 的「同一个函数」= **段身份**（LabelRef::functionLine），不是函数名：
// 同名函数（eraTW 的「口上」、eraMegaten 的事件）可以声明两次，两份声明是
// 两个不同的段，第一份里的 GOTO 看不到第二份里的 $标签。这里查标签一侧用的是
// position::firstGotoLabel —— 运行期 resolveJumpTarget / findGotoLabelInFunction
// 用的**同一个函数**（索引 + isFunction + script + functionLine 四个条件），
// 不再按 ownerFunction 名字分组（那是一份会与运行期各自演化的第二口径）。
//
//   * CALL/CALLFORM/CALLF/CALLFORMF/JUMP/JUMPFORM：常量目标必须存在
//   * GOTO/GOTOFORM：常量 $标签必须在**本函数段内**（对齐 GetLabelDollar）
//   * TRY* 系（TRYCALL/TRYJUMP/TRYCCALL/TRYCJUMP/TRYGOTO/TRYCALLEVENT…）：
//     语义就是「找不到就跳过」，不报（对齐 C# func.Function.IsTry()）
//   * CALLEVENT / BEGIN / FUNC：C# 不做目标检查（CALLEVENT 缺目标静默），跳过
void resolveStaticTargets(const ParseIrView& view, ParseDiagnostics& diags) {
    // 全库标签索引**每个 pass 建一次**（串行、在驱动层）：任务里只做 O(1) 成员判定。
    const QSet<QString> allLabels = collectLabelNames(view);
    runPerScript(view, diags, [&view, &allLabels](const QString& name, const ScriptData& script,
                                                 ParseDiagnostics& local) {
        resolveStaticTargetsInScript(view, name, script, local, allLabels);
    });
}

// 目标名：常量目标返回标签名（去掉 @/$ 前缀）；运行期求值返回空（不判定）。
static QString staticCallTarget(const LogicalLine& line) {
    if (line.arguments.isEmpty()) return {};
    QString name = line.arguments.first().raw.trimmed();
    if (name.isEmpty()) return {};
    if (name.startsWith(QLatin1Char('@')) || name.startsWith(QLatin1Char('$')))
        name.remove(0, 1);
    if (name.contains(QLatin1Char('%')) || name.contains(QLatin1Char('{'))
        || name.contains(QLatin1String("[[")))
        return {};   // FORM 名 / [[Rename]] 占位：运行期才知道
    return name;
}

// 逐脚本实现：结论只依赖 (view, scriptName, data)，diags 是调用方给的**本线程独占**
// sink，因而可以被驱动端（runPerScript）并行调用。
void resolveStaticTargetsInScript(const ParseIrView& view, const QString& scriptName,
                                  const ScriptData& data, ParseDiagnostics& diags,
                                  const QSet<QString>& allLabels) {
    // 段身份（「这一行属于哪个 @函数段」）的**定义**：最近的前置 @函数标签行
    //（含该行自身）；第一个 @ 之前 = -1。装载期在唯一登记点把同一个值写进
    // LabelRef::functionLine（era_parse_table.cpp 的 currentFunctionLine）。
    // 这里不复用名字（ownerFunction）—— 同名函数的第二份声明是另一个段。
    // 一次前序遍历 + 一个循环变量，与装载期那条状态机同形，不另建表。
    const ScriptView scriptView{view.scripts, view.labelIndex};
    int segment = -1;   // 当前行的段起点（见上）

    for (int i = 0; i < data.lines.size(); ++i) {
        const LogicalLine& line = data.lines.at(i);
        if (line.kind == LineKind::FunctionLabel) segment = i;   // 段从这里开始
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
        const QString target = staticCallTarget(line);
        if (target.isEmpty()) continue;
        QString message;
        if (checkFunction) {
            // 函数一侧与段无关：CALL/JUMP 的目标是**全库**的 @标签名。
            if (hasLabelInScripts(allLabels, target)) {
                // 目标存在 -> 装载期就把实参校验掉（对齐 C# CALL_Instruction.SetJumpTo
                // 里调用 ConvertArg；C# 只在「常量目标且能找到」时才做这一步）。
                checkCallStatementArgs(view, line, target, diags);
                continue;
            }
            message = QCoreApplication::translate("ParseDiagnostics",
                "指定的函数名\"@%1\"不存在（运行到该行会失败）").arg(target);
        } else {
            // 标签一侧与运行期**同一个函数**：本脚本 + 本段 + 只认 $标签。
            if (position::firstGotoLabel(scriptView, target, scriptName, segment))
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
// C# 在装载第 2 趟（nestCheck）里用一个 nestStack 走完每个函数体，把配对不上
// 的结构、越过块边界的指令、SIF 的次行、RETURNF 的位置/类型报到装载期。
//
// 本实现**不再有第二个 nestStack**：同一批判定（含「未解析」）由
// jumpmark::buildJumpMarkings 在装载期那一趟里写进 ScriptData::structureIssues，
// 这里只把记录翻译成诊断文本。于是「配对上了没有」只有一份判定 —— 运行期的
// 跳转表是它的运行期投影，诊断是它的静态投影。
//
// 刻意不做的：C# 里那些 level 0/1 的提示（「SIF文の次の行が空行…」、
// 「関数が定義されるより前に行があります」）——eraTW 这类真实工程里遍地都是，
// 报出来只会淹没真正的问题。
// ---------------------------------------------------------------------------
void checkStructures(const ParseIrView& view, ParseDiagnostics& diags) {
    // 名字用不上（结构检查只看本脚本的行）：段身份只对 GOTO 目标有意义。
    runPerScript(view, diags, [&view](const QString& /*scriptName*/, const ScriptData& script,
                                      ParseDiagnostics& local) {
        checkStructuresInScript(view, script, local);
    });
}

// 逐脚本实现：结论只依赖 records + 本脚本的行（RETURNF 还要函数表）。
//
// **这里不再扫行配对**：配对判定（含所有未解析形态）已经由
// jumpmark::buildJumpMarkings 在装载期写进 data.structureIssues。本函数只是
// 「记录 -> 诊断」的翻译：一个 kind 一个消息模板，名字从记录与行上取。
// 唯一还需要视图数据的是 RETURNF（返回类型要查函数登记表），但它没有栈。
void checkStructuresInScript(const ParseIrView& view, const ScriptData& data,
                             ParseDiagnostics& diags) {
    const auto add = [&diags](const LogicalLine& line, const QString& text) {
        diags.add(DiagSeverity::Error, DiagCode::kStructure,
            line.position.filename, line.position.lineNumber, line.position.column, 0, text);
    };
    const auto lineAt = [&data](int index) -> const LogicalLine* {
        if (index < 0 || index >= data.lines.size()) return nullptr;
        return &data.lines.at(index);
    };

    for (const StructureIssue& issue : data.structureIssues) {
        const LogicalLine* line = lineAt(issue.line);
        if (!line) continue;   // 记录只由本脚本的装载产生；越界 = 数据损坏，跳过
        switch (issue.kind) {
        case StructureIssueKind::UnresolvedOpener:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "%1 に対応する %2 が見つかりません")
                .arg(issue.name, jumpmark::requiredCloser(issue.name)));
            break;
        case StructureIssueKind::OrphanIfBranch:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "IF～ENDIFの外で%1文が使われました").arg(issue.name));
            break;
        case StructureIssueKind::OrphanCaseBranch:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "SELECTCASE～ENDSELECTの外で%1文が使われました").arg(issue.name));
            break;
        case StructureIssueKind::OrphanCloser:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "対応する%1の無い%2文です").arg(issue.name, line->functionName));
            break;
        case StructureIssueKind::OrphanLoop:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "対応するREPEATの無い%1文です").arg(issue.name));
            break;
        case StructureIssueKind::OrphanEndData:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "対応するPRINTDATA系命令もしくはSTRDATAのない%1です").arg(issue.name));
            break;
        case StructureIssueKind::OrphanFunc:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "対応するTRYCALLLIST系命令のない%1です").arg(issue.name));
            break;
        case StructureIssueKind::OrphanData:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "対応するPRINTDATA系命令のない%1です").arg(issue.name));
            break;
        case StructureIssueKind::OrphanCatch:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "対応するTRYC系命令がありません"));
            break;
        case StructureIssueKind::IllegalInBody:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "%1 構文に使用できない命令'%2'が含まれています")
                .arg(issue.name, line->functionName));
            break;
        case StructureIssueKind::IllegalInDataList:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "DATALIST 構文に使用できない命令'%1'が含まれています")
                .arg(line->functionName));
            break;
        case StructureIssueKind::IllegalBeforeCase:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "SELECTCASE 構文の分岐の外に命令'%1'が含まれています")
                .arg(line->functionName));
            break;
        case StructureIssueKind::LabelInBody:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "%1 構文中に $ラベルを定義することはできません").arg(issue.name));
            break;
        case StructureIssueKind::BreakOutsideLoop:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "REPEAT, FOR, WHILE, DOの中以外で%1文が使われました").arg(issue.name));
            break;
        case StructureIssueKind::SifNoNextLine:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "SIF文の次の行がありません"));
            break;
        case StructureIssueKind::SifNextForbidden:
            add(*line, QCoreApplication::translate("ParseDiagnostics",
                "SIF文の次の行を%1文にすることはできません").arg(issue.name));
            break;
        case StructureIssueKind::Returnf: {
            // RETURNF：必须在 #FUNCTION(S) 内，且返回类型要一致（判据未动）。
            const UserFunctionDecl* decl = userFunctionIn(view, line->ownerFunction.toUpper());
            if (!decl || !decl->isMethod) {
                add(*line, QCoreApplication::translate("ParseDiagnostics",
                    "RETURNFは#FUNCTION以外では使用できません"));
                break;
            }
            if (line->arguments.isEmpty()) break;
            const auto& operand = line->arguments.first();
            if (!operand.ast) break;
            const OperandType actual = operand.ast->valueType();
            if (!isKnown(actual) || !isKnown(decl->returnType)) break;
            if (actual != decl->returnType) {
                add(*line,
                    decl->returnType == OperandType::Int
                        ? QCoreApplication::translate("ParseDiagnostics",
                              "#FUNCTIONで始まる関数の戻り値に文字列型が指定されました")
                        : QCoreApplication::translate("ParseDiagnostics",
                              "#FUNCTIONSで始まる関数の戻り値に数値型が指定されました"));
            }
            break;
        }
        }
    }
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
//     常量值来自装载期字段 ExpressionNode::constantIndex（`-1`、`(0-1)`、
//     `#DIM CONST` 名、`(2*5)` 都是同一个 Value）—— 3/6 只判上界，负值明确
//     让给本趟；两边读的是同一个值，不会各算一遍。
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
    // 名字用不上（同上）：静态值检查只读本脚本的表达式 AST。
    runPerScript(view, diags, [&view](const QString& /*scriptName*/, const ScriptData& script,
                                      ParseDiagnostics& local) {
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

    // 6/6 的**唯一**写出点：severity / effect / 生产者全部来自 index_bounds
    // 的处置表。返回 false = 该事实归 3/6（本趟让位，不产生第二条诊断）。
    const auto emitStaticIssue = [&](indexbounds::Kind kind, const LogicalLine& line,
                                     const QString& message) {
        const indexbounds::Policy& policy = indexbounds::issuePolicy(kind);
        if (policy.producer != DiagOrigin::Pass6) return false;
        diags.add(policy.severity, DiagCode::kStaticValue, line.position.filename,
                  line.position.lineNumber, line.position.column, 0, message, {},
                  policy.effect);
        return true;
    };
    // 常量值识别（字面量 / 负字面量）与「3/6 是否已覆盖纯字面量上界」都走
    // index_bounds 的唯一一份判定：`-1` 是 UnaryOp(MINUS, Literal)，3/6 的
    // 纯字面量检查看不见 —— 分工由此成为数据（issuePolicy 的 producer）。

    for (const LogicalLine& line : data.lines) {
        // 6/6 是 3/6 的**残余**：3/6 已判该行失效（RejectLine）时，本趟不再对
        // 同一行补任何事实 —— 同一行不允许 RejectLine 与 ReportOnly 并存。
        // 3/6 判不了的形态在 line.isError == false 时照常进入下面的分支。
        if (line.isError) continue;
        const auto visit = [&](ExpressionNode& node) {
            if (node.kind() == NodeKind::BinaryOp) {
                auto& op = static_cast<BinaryOpNode&>(node);
                const TokenType t = op.op().type();
                if (t != TokenType::DIVIDE && t != TokenType::MODULO) return;
                const auto& right = op.right();
                if (!right || right->kind() != NodeKind::Literal) return;
                const auto& lit = static_cast<const LiteralNode&>(*right);
                if (lit.valueType() != OperandType::Int || lit.intValue() != 0) return;
                // 常量被除数 -> 3/6 的装载期求值已经产事实（RejectLine）；
                // 含变量的被除数 -> 只有本趟能看见（ReportOnly）。
                const indexbounds::Kind kind = indexbounds::isConstantSubtree(node)
                    ? indexbounds::Kind::ConstantDivisionByZero
                    : indexbounds::Kind::DynamicDivisionByZero;
                emitStaticIssue(kind, line, indexbounds::divisionByZeroMessage());
                return;
            }
            // ---- 常量下标边界（步骤 3 看不到尺寸的名字才判）----
            if (node.kind() != NodeKind::Variable) return;
            // VARSIZE 只消费数组身份（语言规定其下标被忽略，见 finalizeValidation
            // 的同款豁免）—— 下标形态（含字符串）不参与运行期求值，不判。
            if (line.functionName == QLatin1String("VARSIZE")) return;
            const auto& var = static_cast<const VariableNode&>(node);
            const int n = var.indices().size();
            if (n <= 0 || n > 3) return;
            // 常量下标值：**只读**装载期写好的字段（ExpressionNode::constantIndex）。
            // 不再在这里辨认 LiteralNode / UnaryOp(MINUS,Literal) / 常量名 ——
            // 形态判定与求值都在收尾定型那一趟完成（index_bounds::
            // isConstantIndexExpression + 装载期 ExpressionEvaluator），本趟只读值。
            //   Unknown（含变量/函数/++）-> 跳过；
            //   Invalid（是常量但求值失败）-> 归 3/6（issuePolicy 的 producer 是
            //     Pass3，该行已被 RejectLine，本趟在对 line.isError 的守卫处让位）。
            int constPos[3] = {0, 0, 0};
            qint64 constVal[3] = {0, 0, 0};
            int constCount = 0;
            for (int i = 0; i < n; ++i) {
                const auto& idx = var.indices().at(i);
                if (!idx || !idx->constantIndex.isValid()) continue;
                constPos[constCount] = i;
                constVal[constCount] = idx->constantIndex.value;
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
                    emitStaticIssue(indexbounds::Kind::UnknownStringIndex, line,
                        indexbounds::unknownStringIndexMessage(var.name(), l->strValue()));
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
                    const int dim = indexbounds::elementDimension(p, meta.dimension,
                                                                 meta.characterData, n);
                    if (dim >= 0 && dim < sizes.size() && sizes.at(dim) > 0)
                        bounds[p] = sizes.at(dim);
                }
            } else if (const VariableDecl* d =
                           view.variables->find(var.name(), line.scopeKey)) {
                // 声明数组：纯字面量 + lengths 已知的位置步骤 3 已报（不重复）；
                // lengths 缺失（标量 #DIM X / REF）时只剩这里补查。角色号位不判。
                const int full = d->dimension + (d->isCharaData ? 1 : 0);
                if (n != full) return;
                for (int p = 0; p < n; ++p) {
                    if (d->isCharaData && p == 0) {
                        charaIdPos[p] = true;
                        continue;
                    }
                    const int dim = indexbounds::elementDimension(p, d->dimension,
                                                                 d->isCharaData, n);
                    if (dim >= 0 && dim < d->lengths.size()
                        && indexbounds::pass3CoversLiteralBounds(d->lengths.at(dim)))
                        veCovered[p] = true;   // 步骤 3 已按 lengths 报过
                }
            }
            // 其余（未声明裸名）：自动扩容无上界 —— 只剩负数可判。

            for (int c = 0; c < constCount; ++c) {
                const int p = constPos[c];
                const qint64 v = constVal[c];
                if (charaIdPos[p]) continue;
                // 3/6 已按 lengths 报过「已声明数组 + 非负常量」；负值即使
                // lengths 已知也归本趟（3/6 只看上界，下界由这里唯一判定）。
                if (veCovered[p] && v >= 0) continue;
                if (v < 0) {
                    emitStaticIssue(indexbounds::Kind::NegativeConstantIndex, line,
                        indexbounds::negativeIndexMessage(var.name(), v));
                } else if (bounds[p] >= 0 && v >= bounds[p]) {
                    emitStaticIssue(indexbounds::Kind::SystemSizeExceeded, line,
                        indexbounds::beyondSizeMessage(var.name(), v, int(bounds[p])));
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
