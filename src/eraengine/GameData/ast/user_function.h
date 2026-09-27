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
#ifndef AST_USER_FUNCTION_H
#define AST_USER_FUNCTION_H

#include <QString>
#include <QStringList>
#include <QList>
#include "operand_type.h"

// ---------------------------------------------------------------------------
// 用户自定义函数的 AST 声明（对齐 C# FunctionLabelLine + ErbLoader.parseLabel）
//
// Emuera 里「自定义函数」不是一个独立的语句块节点，而是：
//   * 声明 = `@NAME(...)` / `@NAME, ARG, ARGS:1` 标签行
//            + 紧随其后的 `#FUNCTION(S)` / `#SINGLE` / `#PRI` / `#LATER` / `#ONLY`
//            + 函数体内的 `#DIM(S)` 私有变量
//   * 函数体 = ScriptData::lines 里 [labelLine, endLine) 的连续区间（扁平行号）
//   * 形参绑定 = 标签的实参表把「调用实参的位置」映射到局部槽：
//        ARG / ARG:k      -> 整数局部槽 k        （C# VariableCode.ARG）
//        ARGS / ARGS:k    -> 字符串局部槽 k      （C# VariableCode.ARGS）
//        其它名字          -> 函数私有变量        （C# 私有 #DIM/#DIMS 变量）
//   * 闭包 = 无（Emuera 没有嵌套函数）；作用域就是「函数私有变量 + ARG/ARGS」
//
// 本结构把这套声明**显式建模为 AST 的节点**，供：
//   * 表达式解析期判断 `NAME(...)` 是否为函数（IsMethod → 返回类型）
//   * 调用期按声明做参数绑定（而不是靠位置猜）
//   * 调试/诊断（形参名、返回类型、事件分组、体区间）
// ---------------------------------------------------------------------------

// 形参的绑定目标
enum class UserParamTarget : quint8 {
    Arg,       // ARG[:k]   —— 整数局部槽
    Args,      // ARGS[:k]  —— 字符串局部槽
    LocalVar,  // 函数私有变量（#DIM/#DIMS 声明）
    Unknown,   // 无法归类
};

struct UserParamDecl {
    QString         name;           // 标签里的原始写法（用于显示/诊断）
    UserParamTarget target = UserParamTarget::Unknown;
    int             index = 0;      // ARG:/ARGS: 的下标；LocalVar 时=位置
    QString         varName;        // LocalVar 时的变量名
    OperandType     type = OperandType::Unknown;   // Int/Str（LocalVar 由变量表回填）
    // 类型是否来自真实声明（ARG/ARGS 恒为 true；私有变量要有 #DIM/#DIMS 才算）。
    // 只有 typeKnown 的形参才参与「式中调用」的强类型校验 —— 未声明的名字
    // 无法定类型，强行按 Int 校验会把真实游戏误报成错误。
    bool            typeKnown = false;
    bool            isReference = false;           // 引用型形参（#DIM REF）
    bool            hasDefault = false;
    qint64          defaultInt = 0;
    QString         defaultStr;

    [[nodiscard]] bool isString() const { return type == OperandType::Str; }
};

// 一个用户自定义函数的完整声明
struct UserFunctionDecl {
    QString      name;                 // 大写函数名（C# Config.ICVariable 默认开启）
    QString      script;               // 所在脚本
    int          labelLine = -1;       // @label 的行号
    int          endLine = -1;         // 函数体结束（下一函数标签前一行）

    QList<UserParamDecl> params;       // 形参表（顺序 = 调用实参位置）
    int          maxArgIndex = -1;     // C# ArgLength：ARG:n 的最大 n
    int          maxArgsIndex = -1;    // C# ArgsLength：ARGS:n 的最大 n

    // #FUNCTION / #FUNCTIONS —— 是否可作表达式函数 + 返回类型
    bool         isMethod = false;
    OperandType  returnType = OperandType::Unknown;   // 非方法 -> Unknown（语句函数）

    // 事件 / 系统标签
    bool         isEvent = false;
    bool         isSystem = false;

    // #SINGLE / #PRI / #LATER / #ONLY
    bool         isSingle = false;
    bool         isPri = false;
    bool         isLater = false;
    bool         isOnly = false;

    // #LOCALSIZE / #LOCALSSIZE
    int          localSize = 0;
    int          localsSize = 0;

    [[nodiscard]] bool isValid() const { return labelLine >= 0; }
    [[nodiscard]] int  paramCount() const { return params.size(); }
    [[nodiscard]] bool returnsString() const { return returnType == OperandType::Str; }
};

// 把一个标签形参名归类到绑定目标（对齐 C# ErbLoader.parseLabel 的变量解析）：
//   "ARG" / "ARG:2" -> Arg(0) / Arg(2)
//   "ARGS" / "ARGS:1" -> Args(0) / Args(1)
//   其它 -> LocalVar（名字原样保留，类型由变量表回填）
[[nodiscard]] UserParamDecl classifyUserParam(const QString& rawName);

#endif // AST_USER_FUNCTION_H
