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
#ifndef ERAENGINE_GAMEPROC_DECLARATION_SCAN_H
#define ERAENGINE_GAMEPROC_DECLARATION_SCAN_H

#include <QHash>
#include <QList>
#include <QString>
#include <functional>

#include "logical_line.h"   // ScriptPosition

class ParseDiagnostics;
class VariableStorage;
class VariableTable;

// ---------------------------------------------------------------------------
// 声明扫描（declaration scan）—— 「哪一行是变量声明、它属于哪个段」的**唯一**判据
//
// 声明是**源码的静态事实**：给定预处理后的一行序列，变量的名字/类型/作用域
// 就已经定死了。因此这段判定只能有一份实现，四条消费路径共用它：
//   · ErbLoader::preRegisterDeclarations —— 装载前把全部文件的声明登记进解析表
//                                            （生产路径的权威来源）；
//   · EraParseTable::loadScript          —— 未预登记路径（单测/工具）自己先扫一遍；
//   · EraParseTable::buildScriptAst      —— **构建的前置步骤**：AST 构建之前先把
//                                            本脚本的声明登记进解析表（与
//                                            loadScript 同一个登记入口），
//                                            于是构建期不存在「左值类型未知」的行；
//   · ErbLoader::scanVariableTypes       —— LoadTimeVarTypes（并行解析的类型快照）。
//
// 判据与 AstBuilder 的行分类**同序**（见 ast_builder.cpp 的 build 开头）：
//   空行/注释（';'）        -> Null          —— 不结束声明区
//   '#'                     -> Preprocessor  —— .ERH / 紧跟 @标签 时是声明
//   '@'                     -> FunctionLabel —— 段起点（段身份 = (脚本名, 该行下标)）
//   '$'                     -> GotoLabel     —— 不改段
//   VARI/VARS 形态的语句    -> 声明（作用域 = 当前段）
//   其它                    -> 结束声明区
// ---------------------------------------------------------------------------
namespace declscan {

// 预处理后的一行：与原行数组逐字一致（AstBuilder 看到的就是这份文本）。
struct SourceLine {
    QString text;
    ScriptPosition position;
};

enum class DeclarationKind : quint8 {
    SharpDirective,   // #DIM / #DIMS / #GLOBAL / #GLOBALS / #PRIVATE
    ScopedVariable,   // VARI / VARS（emuera.net 的语句位置声明）
};

// 一条声明的**位置事实**：谁（哪个段）在哪儿声明了什么文本。
// 类型/维度/初值由 registerDeclaration（= 变量表的唯一登记实现）判定，
// 本结构不重复那套判定。
struct DeclarationSite {
    DeclarationKind kind = DeclarationKind::SharpDirective;
    QString rawLine;          // 预处理后的原文（与 AstBuilder 所见逐字一致）
    ScriptPosition position;
    QString function;         // 当前 @函数名（空 = 文件头段）
    QString scriptName;
    int segmentStart = -1;    // 段起点（预处理后行序里的 @ 下标；文件头段 = -1）
    QString scopeKey;         // 段身份文本（eravar::segmentKeyText）；空 = 广域
    bool isHeaderFile = false;
};

struct Scan {
    // 被接受的声明，按行序。
    QList<DeclarationSite> declarations;
    // 出现在**声明区之外**的 # 行：行下标 -> 原文。调用方按自己的诊断口径告警
    // （loadScript 报「函数声明之外使用了 # 行」，预登记路径静默忽略）。
    QHash<int, QString> orphanSharpLines;
    // 每行的段起点（预处理后行序）：@ 行是自己的段起点，其余沿用最近的 @。
    // 与 loadScript 的 currentFunctionLine、解析线程的 segmentStart 是同一个数。
    QList<int> segmentStartOfLine;
};

// 扫描一段预处理后的源码（三种输入形态共用**同一个**扫描循环）。
[[nodiscard]] Scan scanScript(const QList<SourceLine>& lines, const QString& scriptName,
                              bool isHeaderFile);
[[nodiscard]] Scan scanScript(const QList<LogicalLine>& lines, const QString& scriptName,
                              bool isHeaderFile);
// QStringList 形态（工具/单测的源码片段）：位置 = (fileName, 下标 + 1, 1)。
[[nodiscard]] Scan scanScript(const QStringList& lines, const QString& scriptName,
                              const QString& fileName, bool isHeaderFile);

// 一条声明 -> 变量表（**唯一**登记实现：类型/维度/常量/初值/角色数据/诊断）。
void registerDeclarationText(VariableTable& variables, ParseDiagnostics& diagnostics,
                             VariableStorage* storage, const QString& rawLine,
                             const ScriptPosition& position, const QString& currentFunction,
                             const QString& scopeKey, bool isHeaderFile);

// 同上的站点形态：VARI/VARS 走 scopedVariableDeclText 换算成等价的 #DIM(S) 文本。
// 空函数名（函数声明之外）的 VARI/VARS 不登记（与 loadScript 的既有处置一致）。
void registerDeclaration(VariableTable& variables, ParseDiagnostics& diagnostics,
                         VariableStorage* storage, const DeclarationSite& site);

// 一段源码的声明视图：先扫、再登记进调用方给的变量表（返回扫描结果，
// 调用方还要用 segmentStartOfLine 给每行定作用域）。这是「构建期类型表」的
// **唯一**产物 —— 之后按 VariableTable::typeOf 查，与收尾查同一份表实现。
[[nodiscard]] Scan declareScriptVariables(VariableTable& variables, ParseDiagnostics& diagnostics,
                                          const QList<SourceLine>& lines,
                                          const QString& scriptName, bool isHeaderFile);

// VARI / VARS -> 等价的 `#DIM/#DIMS 名字, 长度…` 文本（唯一构造点）。
// 不是声明的行返回空串。
[[nodiscard]] QString scopedVariableDeclText(const QString& rawLine);

}  // namespace declscan

#endif  // ERAENGINE_GAMEPROC_DECLARATION_SCAN_H