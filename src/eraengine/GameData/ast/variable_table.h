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
#ifndef AST_VARIABLE_TABLE_H
#define AST_VARIABLE_TABLE_H

#include <QHash>
#include <QString>
#include <QList>
#include "operand_type.h"
#include "expression_ast.h"

// ---------------------------------------------------------------------------
// 变量表（对齐 C# GameData/Variable/VariableData + FunctionLabelLine 的私有变量）
//
//   #DIM  A, 3      -> 函数内私有整型数组（或全局，视作用域）
//   #DIMS S$        -> 字符串变量
//   #GLOBAL/#GLOBALS-> 全局
//   @F(A, B)        -> 函数形参登记为该函数的局部变量
//
// 作用：让 VariableNode 在解析期就能得到**强类型**（Int/Str）与维度信息；
// 供参数校验、常量折叠、错误定位使用。
// ---------------------------------------------------------------------------
enum class VarScope : quint8 { Global, Local };

struct VariableDecl {
    QString     name;
    OperandType type = OperandType::Int;     // Int / Str
    VarScope    scope = VarScope::Global;
    QString     function;                    // Local 时所属函数（标签名）
    int         dimension = 1;
    QList<int>  lengths;
    QStringList lengthExprs;   // 原始维数表达式（可含常数名）
    bool        isPrivate = false;
    bool        isConst = false;
    bool        isArg = false;               // 来自 @F(A,B) 形参表
    bool        isReference = false;         // #DIM(S) REF：调用方数组的别名
    QList<qint64> defaultInt;   // #DIM X = 1,2 的初值（进入函数时写入）
    QStringList   defaultStr;   // #DIMS S = "a" 的初值
};

class VariableTable {
public:
    // 返回 false 表示重名（已存在）
    bool add(const VariableDecl& decl);

    // 查声明：先按函数局部，再按全局
    [[nodiscard]] const VariableDecl* find(const QString& name,
                                           const QString& function = QString()) const;
    [[nodiscard]] bool contains(const QString& name) const;

    // 类型解析：函数上下文优先；无上下文时仅全局 + 「全表唯一局部」
    [[nodiscard]] OperandType typeOf(const QString& name,
                                     const QString& function = QString()) const;

    [[nodiscard]] int count() const;
    [[nodiscard]] QList<VariableDecl> declarations() const;
    // 某函数的局部（私有）变量声明
    [[nodiscard]] QList<VariableDecl> localsOf(const QString& function) const;

    void clear();

    // ---- 常数（#DIM CONST name = value）与维数求值 ----
    void setConstInt(const QString& name, qint64 value);
    // `#DIM CONST NAME, N = v0, v1, …`：常数**数组**（下标访问用）
    void setConstArray(const QString& name, const QList<qint64>& values);
    [[nodiscard]] bool constArrayAt(const QString& name, int index, qint64& out) const;
    [[nodiscard]] int constArraySize(const QString& name) const;
    void setConstStr(const QString& name, const QString& value);
    [[nodiscard]] bool constInt(const QString& name, qint64& out) const;
    [[nodiscard]] bool constStr(const QString& name, QString& out) const;
    [[nodiscard]] int constCount() const { return m_constInt.size() + m_constStr.size(); }

    // 用常数表重新求值所有声明的维数（后声明的常数也能修正先前的声明）
    void resolveDimensions();

    // 把一个已构建的 AST 中的变量节点按表回填类型（强类型回填）
    static void applyTypes(ExpressionNode& node, const VariableTable& table,
                           const QString& function = QString());

private:
    // 求值单个数维表达式：数字字面量或常数名
    [[nodiscard]] bool evalDim(const QString& expr, int& out) const;

    QHash<QString, VariableDecl> m_globals;
    QHash<QString, qint64>  m_constInt;
    QHash<QString, QList<qint64>> m_constArray;
    QHash<QString, QString> m_constStr;
    // 反向索引：局部变量名 -> 声明它的函数个数 / 唯一时的类型（空上下文 O(1) 查询）
    QHash<QString, int> m_localNameCount;
    QHash<QString, OperandType> m_uniqueLocalType;
    QHash<QString, QHash<QString, VariableDecl>> m_locals;  // function -> name -> decl
};

#endif // AST_VARIABLE_TABLE_H
