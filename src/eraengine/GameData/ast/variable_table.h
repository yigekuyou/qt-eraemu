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
#include <functional>
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
    bool        isCharaData = false;         // #DIM(S) CHARADATA：每角色一份
    bool        isGlobalSave = false;        // #DIM SAVEDATA GLOBAL：随 SAVEGLOBAL 持久化
    QList<qint64> defaultInt;   // #DIM X = 1,2 的初值（进入函数时写入）
    QStringList   defaultStr;   // #DIMS S = "a" 的初值
};

class VariableTable {
public:
    // 声明结果：新增 / 同名同义（无害重复，如 .ERH 被重复装载）/ 同名冲突
    enum class DeclStatus { Added, DuplicateSame, Conflict };

    // 返回 false 表示重名（已存在）
    bool add(const VariableDecl& decl);
    // 区分「完全相同的重复声明」与「真正冲突的重复声明」
    DeclStatus addChecked(const VariableDecl& decl);

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
    // perf：执行热路径（每条指令都要解析私有作用域）用 —— 装载完成后 m_locals
    // 不再变化，按函数缓存声明列表，返回引用免去每次调用逐个拷贝 VariableDecl。
    [[nodiscard]] const QList<VariableDecl>& localsOfRef(const QString& function) const;

    void clear();

    // ---- 常数（#DIM CONST name = value）与维数求值 ----
    void setConstInt(const QString& name, qint64 value);
    // `#DIM CONST NAME, N = v0, v1, …`：常数**数组**（下标访问用）
    void setConstArray(const QString& name, const QList<qint64>& values);
    [[nodiscard]] bool constArrayAt(const QString& name, int index, qint64& out) const;
    [[nodiscard]] int constArraySize(const QString& name) const;
    void setConstStr(const QString& name, const QString& value);
    // `#DIMS CONST NAME, N = s0, s1, …`：字符串常数数组（eraTW 的 DISP_MEMO 等）
    void setConstStrArray(const QString& name, const QStringList& values);
    [[nodiscard]] bool constStrArrayAt(const QString& name, int index, QString& out) const;
    [[nodiscard]] int constStrArraySize(const QString& name) const;
    // `#DIM CONST NAME = <非常量字面量的表达式>`（如 `= 人物数量上限`）。
    // 声明顺序 / 跨文件顺序不可靠，所以**不在装载期**求值，而是把表达式留到
    // 求值期惰性计算（`#DIM CONST OBJ_ID_LAST = 人物数量上限` 曾因此恒为 0，
    // 进而 INRANGE(…, OBJ_ID_LAST) 为假 -> eraTW 的 EXISTOBJ 抛 THROW）。
    void setConstExprs(const QString& name, const QStringList& exprs);
    [[nodiscard]] QStringList constExprs(const QString& name) const;
    [[nodiscard]] bool constInt(const QString& name, qint64& out) const;
    [[nodiscard]] bool constStr(const QString& name, QString& out) const;
    [[nodiscard]] int constCount() const { return m_constInt.size() + m_constStr.size(); }

    // 用常数表重新求值所有声明的维数（后声明的常数也能修正先前的声明）
    void resolveDimensions();
    // 维数表达式的兜底求值器（`#DIM X, CLASS_NUM + 1` 这类算式/复合表达式）。
    // 由 EraParseTable 在常量表就绪后注入 —— 求值器内部会回查本表的常数。
    void setDimEvaluator(std::function<qint64(const QString&)> fn) { m_dimEvaluator = std::move(fn); }

    // 把一个已构建的 AST 中的变量节点按表回填类型（强类型回填）
    static void applyTypes(ExpressionNode& node, const VariableTable& table,
                           const QString& function = QString());

private:
    // 求值单个数维表达式：数字字面量或常数名
    [[nodiscard]] bool evalDim(const QString& expr, int& out) const;

    QHash<QString, VariableDecl> m_globals;
    QHash<QString, qint64>  m_constInt;
    QHash<QString, QList<qint64>> m_constArray;
    QHash<QString, QStringList> m_constStrArray;   // 字符串常数数组
    QHash<QString, QString> m_constStr;
    QHash<QString, QStringList> m_constExpr;   // 未折叠的 CONST 初值表达式
    std::function<qint64(const QString&)> m_dimEvaluator;   // 维数表达式兜底求值
    // 反向索引：局部变量名 -> 声明它的函数个数 / 唯一时的类型（空上下文 O(1) 查询）
    QHash<QString, int> m_localNameCount;
    QHash<QString, OperandType> m_uniqueLocalType;
    QHash<QString, QHash<QString, VariableDecl>> m_locals;  // function -> name -> decl
    // localsOfRef 的按函数缓存（装载后 m_locals 不变；执行单线程）
    mutable QHash<QString, QList<VariableDecl>> m_localsRefCache;
};

#endif // AST_VARIABLE_TABLE_H
