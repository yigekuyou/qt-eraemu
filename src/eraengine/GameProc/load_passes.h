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
#ifndef ERAENGINE_GAMEPROC_LOAD_PASSES_H
#define ERAENGINE_GAMEPROC_LOAD_PASSES_H

#include <QHash>
#include <QList>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QStringList>

#include "era_parse_table.h"        // ScriptData / LabelRef / UserFunctionInfo
#include "ast/parse_diagnostic.h"   // ParseDiagnostics
#include "ast/operand_type.h"       // OperandType

class ConstantTable;
class ExpressionEvaluator;
class ExpressionNode;
class GameBaseData;
class VariableStorage;
class VariableTable;

// ---------------------------------------------------------------------------
// 装载收尾的**写**步骤：kLoadPasses 里的 1/2/3 号 pass
//
// 与 parse_checks.h 的只读步骤相反，这三步就地改写 IR（变量类型、赋值 AST、
// 函数调用节点）并安装常量求值器/私有名表，因此需要一份**可写的**数据视图：
// LoadContext。视图里只有数据（引用/指针），没有解析表对象，也没有任何
// 回调 —— 于是这些步骤可以脱离解析表单独驱动/测试。
//
// 生命周期：LoadContext 是**视图**而不是拥有者。拷贝它只拷贝引用，被引用的
// 表仍由解析表持有 —— 因此求值器/变量表里安装的 lambda 按值捕获
// ctx 之后仍然指向长期存在的表（与旧实现捕获 `this` 的寿命假设一致）。
// ---------------------------------------------------------------------------
namespace loadpass {

struct LoadContext {
    // 1) 脚本表：pass 1/2/3 都要写（重建行实参、就地定型、写行错误状态）。
    QHash<QString, ScriptData>& scripts;
    // 2) 变量表：pass 1 写维数/常量表达式（resolveDimensions、setDimEvaluator），
    //    pass 2/3 读变量类型；必须可变。
    VariableTable& variables;
    // 3) 用户函数表：pass 1 写形参/返回类型（resolveUserFunctionTypes），
    //    pass 2/3 读声明做调用校验；必须可变。
    QHash<QString, UserFunctionInfo>& functions;
    // 4) CSV 常量名表：表达式解析识别「常量名下标」。只读，可为空。
    const ConstantTable* constantTable;
    // 5) 变量存储：pass 1 写全局初值/求值常量，pass 2 末尾安装私有名表；
    //    两个方向都要写，故非 const，可为空。
    VariableStorage* storage;
    // 6) 表达式求值器：pass 1 往它上面装 const provider（setConst* 非 const），
    //    pass 2/3 求值；必须可变，可为空。
    ExpressionEvaluator* evaluator;
    // 7) GAMEBASE_* 的来源：常量表达式求值参数；evaluate() 收非 const，可为空。
    GameBaseData* gameBaseData;
    // 8) 诊断容器：pass 1/2/3 都往里写（错误 + 告警），跨步骤累积。
    ParseDiagnostics& diagnostics;
    // 9) 全局标签索引：pass 1 末尾按 (脚本名, 行号) 排序固化；必须可变。
    QHash<QString, QList<LabelRef>>& labelLists;
    // 10) 形参名集合缓存：装载期解析与 pass 2 就地定型共用；pass 2 先补顶层
    //     空集（并行趟只读），必须可变。
    QHash<QString, QSet<QString>>& paramNames;
    // 11) 「声明已定稿」标志：pass 1 置位，pass 2 断言其前置条件。
    //     用引用而不是搬走 —— 解析表仍要读它（阶段/契约的一部分）。
    bool& declarationsResolved;
    // 12) 「常量 provider 已安装」幂等 guard：pass 1 置位，必须可变。
    bool& constProvidersReady;
    // 13) 当前执行位置（脚本名）：求值器里的常量/维数回调在**运行期**按执行
    //     位置取作用域，所以必须是实时值 -> 引用（只读）。
    const QString& positionScript;
    // 14) 当前执行位置（行号）：同上，只读引用。
    const int& positionLine;
};

// ---- 表达式解析 -----------------------------------------------------------
// parseScopedExpression 是装载期唯一的表达式解析流水线（解析表的
// expressionAst / runtimeVariableReference 也走这里，通过成员 forwarder）。
// 阶段检查（finalized 后不再编译源码）留在解析表的 forwarder 里 ——
// 阶段机属于解析表，解析本身只依赖 ctx 的数据。
//   foldConstants = false -> 不把 `#DIM CONST NAME` 折叠成字面量（GETVAR 族）
QSharedPointer<ExpressionNode> parseScopedExpression(const LoadContext& ctx, const QString& expr,
                                                     const QString& scope, bool quiet,
                                                     bool foldConstants);
QSharedPointer<ExpressionNode> expressionAstInScope(const LoadContext& ctx, const QString& expr,
                                                    const QString& scope, bool quiet = false);

// 用户 @label 在式子调用里的返回类型；Unknown = 交给内置函数解析。
OperandType userCallReturnType(const QHash<QString, UserFunctionInfo>& functions,
                               const QString& name);

// 每函数的形参名集合（大写键）；miss 返回 nullptr。
const QSet<QString>* paramNamesOf(const QHash<QString, QSet<QString>>& paramNames,
                                  const QString& upperScope);

// 「正在执行的那一行属于哪个函数」（运行期回调用；无位置 = 空串）。
QString executionScope(const QHash<QString, ScriptData>& scripts, const QString& scriptName,
                       int line);

// ---- 收尾写步骤（kLoadPasses 的 1/2/3） -----------------------------------
// 全局声明定稿（常量/维数求值器 + 维数 + 全局初值 + 用户函数类型，幂等）。
// 装载器在「先登记 #DIM/#DIMS 再构造 AST」的结算处（装载器预登记结算）就要跑它 —— 那时收尾步骤还没开始，所以
// 它是一个独立的自由函数，由解析表的 forwarder 也在装载期调用。
void resolveGlobalDeclarations(LoadContext& ctx);
// 步骤 1：全局声明定稿 + 全局标签索引排序（顺序不可交换）。
void resolveGlobalDeclarationsStep(LoadContext& ctx);
// 步骤 2：全体脚本就地定型/重绑（字符串赋值 → 类型 → 赋值 AST → 函数调用）。
void finalizeBindings(LoadContext& ctx);
// 步骤 3：全体脚本参数/类型校验（只产诊断，不写 IR）。
void finalizeValidation(LoadContext& ctx);

// 标签索引排序固化（解析表同名 forwarder 的目标）。
void finalizeLabelIndex(LoadContext& ctx);

// ---- 行/声明文本工具（装载期解析与收尾共用） -------------------
// 去掉指令行的 ';' 注释（尊重引号）；#DIM CONST X = 0 ;说明 需要它。
QString stripDirectiveComment(const QString& text);

// VARI / VARS 声明文本的解析结果（见 era_parse_table.cpp 的用法说明）。
struct ScopedVarSpec {
    bool ok = false;
    bool isStr = false;
    QString name;
    QStringList lengths;   // 维度字面量（空 = 标量 / 一维长度 1）
    bool hasInit = false;
    QString init;          // VARI: 整数表达式原文；VARS: 首尾引号之间的文本
};
ScopedVarSpec parseScopedVarSpec(const QString& rawLine);
bool isScopedVariableLine(const QString& rawLine);

}  // namespace loadpass

#endif  // ERAENGINE_GAMEPROC_LOAD_PASSES_H