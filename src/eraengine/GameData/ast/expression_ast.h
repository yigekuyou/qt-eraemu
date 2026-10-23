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
#ifndef EXPRESSION_AST_H
#define EXPRESSION_AST_H

#include <QString>
#include <QList>
#include <QSharedPointer>
#include <functional>
#include <variant>
#include <optional>
#include <bit>
#include <QJsonObject>
#include <QJsonDocument>
#include "expression_lexer.h"
#include "name_interner.h"
#include "operand_type.h"
#include "operator_table.h"
#include "function_types.h"
#include "index_bounds.h"   // ExpressionIssue::indexKind（下标事实的唯一分类）

// ---------------------------------------------------------------------------
// 强类型表达式 AST（对齐 C# GameData/Expression）
//
//   C#                       C++（本文件）
//   IOperandTerm          -> ExpressionNode        （带 valueType()）
//   SingleTerm            -> LiteralNode
//   VariableTerm          -> VariableNode
//   StrFormTerm           -> StrFormNode
//   OperatorTerm/Method   -> BinaryOpNode/UnaryOpNode
//   FunctionMethodTerm    -> FunctionNode（带解析期返回类型）
//   Ternary*              -> IfNode
//
// 每个节点都标注 OperandType（Int/Str/Void），供解析期类型检查与求值期严格分派。
// 节点不可变、可共享（QSharedPointer）。
// ---------------------------------------------------------------------------

// 节点类别（tag）：求值器用 switch(tag) 分派（跳转表 + 分支预测），
// 取代 dynamic_cast 链（去掉 RTTI 热路径开销）。
enum class NodeKind : quint8 {
    Literal, Variable, BinaryOp, UnaryOp, Function, If, StrForm
};

// ---------------------------------------------------------------------------
// 节点在「表达式文本」内的字符区间（源码位置 span）。
//
// 节点使用表达式内的相对偏移；所属指令提供绝对文件、行和列。
// 绝对位置由调用方按行的 ScriptPosition 换算（列 = 行首列 + span.begin）。
//   begin/end 为相对偏移，begin 含、end 不含；-1 表示未知。
// ---------------------------------------------------------------------------
struct SourceSpan {
    int begin = -1;
    int end   = -1;
    [[nodiscard]] bool valid() const noexcept { return begin >= 0 && end >= begin; }
    [[nodiscard]] int length() const noexcept { return valid() ? end - begin : 0; }
};

// 装载期整数/进制字面量归约（对齐 C# LexicalAnalyzer.ReadInt64）：
//   0x.. 十六进制、0b.. 二进制；`e/E` 使用十进制指数，`p/P` 使用二进制指数
//   （如 `1e3`、`0x1p4`、`0b1p10`）。
//   AST 字面量保存 Signed64 或 Bits64；Expression 求值不再从文本重新转换进制。
//   失败返回 false，由装载调用方生成 numeric-conversion 错误诊断。
[[nodiscard]] bool parseIntegerLiteral(const QString& text, qint64& out);
// 64 位位模式解析：允许 1p63 这类合法的最高位掩码，返回无符号位模式。
[[nodiscard]] bool parseIntegerLiteralBits(const QString& text, quint64& out);

struct CompiledPrintTemplate;
class ExpressionNode {
public:
    virtual ~ExpressionNode() = default;
    QSharedPointer<CompiledPrintTemplate> markupTemplate;
    [[nodiscard]] virtual NodeKind kind() const noexcept = 0;
    [[nodiscard]] virtual QString toString() const = 0;
    [[nodiscard]] virtual OperandType valueType() const = 0;

    [[nodiscard]] bool isInteger() const { return valueType() == OperandType::Int; }
    [[nodiscard]] bool isString() const { return valueType() == OperandType::Str; }
    // 是否在解析期就已确定类型（未知类型走执行期宽松/分支预测路径）
    [[nodiscard]] bool isStaticallyTyped() const { return isKnown(valueType()); }

    // 源码区间（表达式文本内偏移；见 SourceSpan 说明）。解析器填充。
    [[nodiscard]] SourceSpan span() const noexcept { return m_span; }
    void setSpan(const SourceSpan& s) noexcept { m_span = s; }

    // ---- 装载期常量求值结果（下标判定只读这个字段）----
    // 收尾定型时由 walkApply 用**该行所属函数的作用域**一次性写入；运行期只读，
    // 不再按 AST 形态做第二次模式匹配（旧路：LiteralNode / UnaryOp(MINUS,Literal)
    // 两处各自辨认）。
    indexbounds::ConstantIndex constantIndex;

private:
    SourceSpan m_span;
};

// SingleTerm：字面量（整数或字符串）
class LiteralNode : public ExpressionNode {
public:
    explicit LiteralNode(const ExpressionToken& token);
    LiteralNode(qint64 value);
    LiteralNode(const QString& value);

    [[nodiscard]] NodeKind kind() const noexcept override { return NodeKind::Literal; }
    [[nodiscard]] QString toString() const override;
    [[nodiscard]] OperandType valueType() const override { return m_type; }

    enum class IntegerRepresentation { Signed64, Bits64 };
    [[nodiscard]] IntegerRepresentation integerRepresentation() const {
        return std::holds_alternative<quint64>(m_integer) ? IntegerRepresentation::Bits64 : IntegerRepresentation::Signed64;
    }
    [[nodiscard]] qint64 intValue() const {
        return std::holds_alternative<qint64>(m_integer) ? std::get<qint64>(m_integer)
            : std::bit_cast<qint64>(std::get<quint64>(m_integer));
    }
    [[nodiscard]] quint64 integerBits() const { return std::bit_cast<quint64>(intValue()); }
    [[nodiscard]] const QString& strValue() const { return m_str; }
    [[nodiscard]] ExpressionToken token() const { return m_token; }

private:
    ExpressionToken m_token;
    OperandType m_type = OperandType::Int;
    std::variant<qint64, quint64> m_integer = qint64(0);
    QString m_str;
};

// VariableTerm：变量（可带下标）。类型：$前缀/已知字符串变量为 Str，否则 Int。
class VariableNode : public ExpressionNode {
public:
    explicit VariableNode(const QString& name, OperandType type = OperandType::Int);

    [[nodiscard]] NodeKind kind() const noexcept override { return NodeKind::Variable; }
    [[nodiscard]] QString toString() const override;
    [[nodiscard]] OperandType valueType() const override { return m_type; }
    void setValueType(OperandType t) { m_type = t; }

    [[nodiscard]] const QString& name() const { return m_name; }
    int dimension = -1;
    bool characterData = false;
    bool readOnly = false;
    QList<int> lengths;
    // ---- 装载期「分词」出的访问描述符 ----
    // 名字 -> 存储键/槽位 的解析（eraUpperKey 扫描、m_references/m_privateNames、
    // 系统变量名链、形参表）此前每次运行期读写都要重来一遍；而执行到本节点时的
    // 私有作用域由 stepOnce 按 line.ownerFunction 决定 —— 对节点而言是**静态的**。
    // 收尾定型（walkApply）时一次算成 kind + accessKey，运行期直接分派。
    // kind == Dynamic（形参/REF/角色变量/常数/系统变量/伪变量）回退旧路，语义不变。
    // accessKey：**名字 intern 表的整数 id**（scope == 0 = 无私有作用域）。
    //   旧形态是 "FUNC\x1fNAME" 字符串复合键，由 computeVariableAccess 与
    //   setPrivateScope 各拼一次、必须逐字节一致；现在身份是 id 相等，
    //   存储层字符串键只由 eravar::makePrivateStorageKey 一处产生。
    enum class AccessKind : quint8 {
        Dynamic = 0,                          // 真正未知（未声明但带系统元数据的名字等）
        GlobalInt1, GlobalInt2, GlobalInt3,   // 用户全局/私有数组（含未声明名）
        GlobalStr1, GlobalStr2, GlobalStr3,
        LocalSlot, ArgSlot,                   // LOCAL / ARG 整型槽位
        LocalStr, ArgStr,                     // LOCALS / ARGS 字符串槽位
        // ---- 以下 5 类「封闭集合」：装载期就能判定**类别**，只是「取哪个槽/
        //      哪个角色号/哪个绑定」是运行期的事。它们**不使用 accessKey**
        //      （accessKey 恒为空），访问方式由名字类别决定，与私有作用域无关。----
        Param,    // 形参（值绑定 / ARG 别名 / REF 引用绑定，含表项 isReference）
        Pseudo,   // 伪变量（RAND/RANDDATA/LINECOUNT/EMUERA_VERSION/GAMEBASE_*/RESULTS/…）
        System,   // 有存储槽的系统变量（sysvar::systemStorageNames）
        Chara,    // 角色数据变量（内建 CFLAG/… 与 #DIM CHARADATA）
        Const,    // 常数（#DIM CONST，含常数数组）
    };
    AccessKind accessKind = AccessKind::Dynamic;
    eravar::VariableKey accessKey;

    // 该 kind 是否**消费** accessKey（静态存储键）。
    // Global*/槽位 的 kind 直接按 accessKey 存取（键在装载期按声明所属函数
    // 算好、自身即带作用域，运行期不再与「当前作用域」对账）；
    // Param/Pseudo/System/Chara/Const/Dynamic 没有静态键，运行期按名字解析。
    [[nodiscard]] static constexpr bool usesKey(AccessKind k) noexcept {
        switch (k) {
        case AccessKind::GlobalInt1:
        case AccessKind::GlobalInt2:
        case AccessKind::GlobalInt3:
        case AccessKind::GlobalStr1:
        case AccessKind::GlobalStr2:
        case AccessKind::GlobalStr3:
        case AccessKind::LocalSlot:
        case AccessKind::ArgSlot:
        case AccessKind::LocalStr:
        case AccessKind::ArgStr:
            return true;
        case AccessKind::Dynamic:
        case AccessKind::Param:
        case AccessKind::Pseudo:
        case AccessKind::System:
        case AccessKind::Chara:
        case AccessKind::Const:
            return false;
        }
        return false;
    }
    // 该 kind 的读写是否**消费执行位置（作用域）**。
    // 唯一的判据：该 kind 的读写路径里是否有代码拿 scope 去把**名字**解析成
    // 存储键 / 槽位（storageName / resolvedStorageName / getStringSlot(…, scope) /
    // 常数表的 scope 参数），或把 scope 直接交给按名原语。逐 kind 对照运行期
    // 的分派点 —— 读：readIntNode + evaluateVariable 的 resolver 表；
    // 写：writeIntNode + assignVariable —— 一条不漏：
    //
    //   GlobalInt1/2/3、GlobalStr1/2/3
    //       读 readIntNode/readKeyedInt（getGlobalInt*DRaw）、readKeyedStr
    //       （getStringSlotRaw）；写 writeIntNode、assignVariable（setGlobal*DRaw）。
    //       静态键直取，scope 不进任何接口。
    //       （下标表达式的 scope 由 walkApply 递归计入，属于下标节点自己的 kind。）
    //   LocalSlot / ArgSlot
    //       读 readKeyedLocalArgInt、写 writeIntNode/assignVariable：
    //       getLocalInt / setLocalInt / getArgInt / setArgInt，只用槽号。
    //   LocalStr / ArgStr
    //       读 readKeyedLocalArgStr（getStringSlotRaw("LOCALS"/"ARGS")）；
    //       写 writeIntNode/assignVariable 落进 writeIntByName 的 ARGS/LOCALS
    //       分支直接返回（字符串槽不自增），不产生写入。
    //   System
    //       读 readIntNode 回退 readIntByName、写 writeIntByName：名单里的名字
    //       全是整型（无字符串系统名），且 hasSystemVariable 优先于按名取槽 ——
    //       脚本私有同名声明遮蔽不了系统槽，scope 不改变结果。
    //   Pseudo
    //       readGameBase / readLineCount / readRandom / readIntLimits /
    //       readRandData / readLastLoad 都按**名字**命中，不解析 ctx.varName。
    //       只有「脚本把保留名当私有变量声明」（`#DIMS RESULTS` 之类，Emuera
    //       下同样不生效的错误写法）才会让 readStringParameterAlias 的 aliased
    //       分支抢在前头；该写法五语料 0 命中，维持 false 以免每个 RAND 条件
    //       都去取位置。
    //   Param
    //       readStringParameterAlias：aliased && Str 且**带下标**时走
    //           storage->resolvedStorageName(ctx.varName, ctx.scope)
    //           storage->getStringSlot(target, {idx}, ctx.scope)
    //       读的是该形参在**本函数私有作用域**里的槽位（`#DIMS S, N` 的数组
    //       元素，声明默认值），不是裸名槽。判成 false 时 aliased 恒假、这一
    //       分支被跳过：值绑定过的形参先命中 readParameter（实测读到
    //       m_parameters，见 test_condition_scope 的 1b 段），未绑定的落到
    //       裸名槽 —— 两种都是静默读到另一份存储。
    //       另外 readCharaData 对 CHARADATA 形参同样收 scope
    //       （resolvedStorageName + getStringSlot(name, ids, ctx.scope)）。
    //       写侧不取 scope：writeIntByName / writeStructuredValue 对
    //       hasParameter 的名字一律短路到值绑定（setParameter）；条件求值只读，
    //       判据按读侧定。
    //   Chara
    //       读 readCharaData：resolvedStorageName(ctx.varName, ctx.scope) +
    //       getStringSlot(name, ids, ctx.scope) / getCharaInt(3D)；
    //       写 writeIntByName 的 isCharaDataVariable 分支用同一个 resolvedStorageName。
    //   Const
    //       readConst：m_constArrayChecker / m_constArrayProvider / m_constProvider
    //       都收 scope（私有 #DIM CONST 按函数作用域查表；常量不进私有名表，
    //       所以只能在这里消费）。常量不可写。
    //   Dynamic
    //       回退 readIntByName(name, indices, scope)，以及 resolver 表里的
    //       readStringParameterAlias / applyReferenceAlias / readCharaData /
    //       readConst / readStringVariable —— 全部按名解析。
    //
    // 这是「条件求值要不要取 executionScopeId()」的装载期判据：
    // 由 walkApply 一趟 OR 出来写进 LogicalLine::conditionNeedsScope，运行期只读。
    [[nodiscard]] static constexpr bool needsExecutionScope(AccessKind k) noexcept {
        switch (k) {
        case AccessKind::Param:
        case AccessKind::Chara:
        case AccessKind::Const:
        case AccessKind::Dynamic:
            return true;
        case AccessKind::GlobalInt1:
        case AccessKind::GlobalInt2:
        case AccessKind::GlobalInt3:
        case AccessKind::GlobalStr1:
        case AccessKind::GlobalStr2:
        case AccessKind::GlobalStr3:
        case AccessKind::LocalSlot:
        case AccessKind::ArgSlot:
        case AccessKind::LocalStr:
        case AccessKind::ArgStr:
        case AccessKind::Pseudo:
        case AccessKind::System:
            return false;
        }
        return true;   // 未知 kind：保守取位置（不会发生）
    }
    // kind -> 位掩码的一位（运行期 resolver 表的 kinds 列用）。
    [[nodiscard]] static constexpr quint16 bit(AccessKind k) noexcept {
        return static_cast<quint16>(1u << static_cast<unsigned>(k));
    }
    // 运行期 readIntNode/writeIntNode/evaluateVariable/assignVariable：
    // usesKey(kind) 的节点直接用 accessKey（键自足，无作用域守卫）；
    // 其余节点走按名解析，位置由**显式传入的** eravar::ScopeId 决定。
    [[nodiscard]] bool isArray() const { return !m_indices.isEmpty(); }

    void addIndex(QSharedPointer<ExpressionNode> index);
    [[nodiscard]] const QList<QSharedPointer<ExpressionNode>>& indices() const { return m_indices; }

private:
    QString m_name;
    OperandType m_type;
    QList<QSharedPointer<ExpressionNode>> m_indices;
};

// OperatorTerm（二元）
class BinaryOpNode : public ExpressionNode {
public:
    BinaryOpNode(QSharedPointer<ExpressionNode> left,
                 const ExpressionToken& op,
                 QSharedPointer<ExpressionNode> right);

    [[nodiscard]] NodeKind kind() const noexcept override { return NodeKind::BinaryOp; }
    [[nodiscard]] QString toString() const override;
    // 类型按需计算：子节点类型（如变量表回填）变化后仍保持一致
    [[nodiscard]] OperandType valueType() const override {
        const OperandType lt = m_left ? m_left->valueType() : OperandType::Unknown;
        const OperandType rt = m_right ? m_right->valueType() : OperandType::Unknown;
        return inferBinaryType(m_op.type(), lt, rt).value_or(OperandType::Int);
    }
    [[nodiscard]] bool typesValid() const {
        const OperandType lt = m_left ? m_left->valueType() : OperandType::Unknown;
        const OperandType rt = m_right ? m_right->valueType() : OperandType::Unknown;
        return inferBinaryType(m_op.type(), lt, rt).has_value();
    }

    [[nodiscard]] QSharedPointer<ExpressionNode> left() const { return m_left; }
    [[nodiscard]] ExpressionToken op() const { return m_op; }
    [[nodiscard]] QSharedPointer<ExpressionNode> right() const { return m_right; }

private:
    QSharedPointer<ExpressionNode> m_left;
    ExpressionToken m_op;
    QSharedPointer<ExpressionNode> m_right;
};

// 一元运算符
class UnaryOpNode : public ExpressionNode {
public:
    UnaryOpNode(const ExpressionToken& op, QSharedPointer<ExpressionNode> operand,
                bool postfix = false);

    [[nodiscard]] NodeKind kind() const noexcept override { return NodeKind::UnaryOp; }
    [[nodiscard]] QString toString() const override;
    [[nodiscard]] OperandType valueType() const override {
        const OperandType ot = m_operand ? m_operand->valueType() : OperandType::Unknown;
        return inferUnaryType(m_op.type(), ot).value_or(OperandType::Int);
    }
    [[nodiscard]] bool typesValid() const {
        const OperandType ot = m_operand ? m_operand->valueType() : OperandType::Unknown;
        return inferUnaryType(m_op.type(), ot).has_value();
    }

    [[nodiscard]] ExpressionToken op() const { return m_op; }
    [[nodiscard]] QSharedPointer<ExpressionNode> operand() const { return m_operand; }
    // 前置还是后置（++/--）；C# 对应 unaryDic / unaryAfterDic
    [[nodiscard]] bool isPostfix() const { return m_postfix; }

private:
    ExpressionToken m_op;
    QSharedPointer<ExpressionNode> m_operand;
    bool m_postfix = false;
};

// FunctionMethodTerm：函数调用
//
// 解析期把调用解析成三类（对齐 C# IdentifierDictionary.GetFunctionMethod）：
//   * 内置函数（methodDic 命中）        -> isBuiltin()，返回类型来自 kBuiltinFunctions，
//                                          并复刻 FunctionMethod.CheckArgumentType() 校验；
//   * 用户自定义函数（#FUNCTION(S)）    -> isUserFunction()，返回类型由 Provider 提供；
//   * 未定义函数                        -> 两者皆否，arityError() = 「未定义的函数」。
class FunctionNode : public ExpressionNode {
public:
    FunctionNode(const QString& name, const QList<QSharedPointer<ExpressionNode>>& args);

    [[nodiscard]] NodeKind kind() const noexcept override { return NodeKind::Function; }
    [[nodiscard]] QString toString() const override;
    [[nodiscard]] OperandType valueType() const override { return m_type; }
    void setValueType(OperandType t) { m_type = t; }
    [[nodiscard]] bool isUserFunction() const { return m_isUserFunction; }
    void setUserFunction(bool v) { m_isUserFunction = v; }

    // 装载期按用户函数 REF 形参绑定；仅该实参的变量根节点允许数组身份。
    void setReferenceArguments(const QList<bool>& args) { m_referenceArguments = args; }
    [[nodiscard]] bool isReferenceArgument(int index) const {
        return index >= 0 && index < m_referenceArguments.size() && m_referenceArguments.at(index);
    }

    // ---- 内置函数（内部命令）+ 扩展式中函数 ----
    //   两者都「是函数、有返回类型」，但求值来源不同：内置走 kBuiltinFunctions，
    //   扩展走注册类的回调（BuiltinOp::Extension）。isBuiltin() 覆盖两者，
    //   求值分派据此进入 evaluateBuiltin。
    [[nodiscard]] bool isBuiltin() const noexcept {
        return m_builtinIndex >= 0 || m_extensionSpec != nullptr;
    }
    [[nodiscard]] int  builtinIndex() const noexcept { return m_builtinIndex; }
    void setBuiltinIndex(int index) noexcept { m_builtinIndex = index; }
    void setExtensionSpec(const BuiltinFunctionSpec* spec) noexcept { m_extensionSpec = spec; }
    [[nodiscard]] const BuiltinFunctionSpec* builtinSpec() const noexcept {
        return m_builtinIndex >= 0 ? &kBuiltinFunctions[m_builtinIndex] : m_extensionSpec;
    }
    [[nodiscard]] BuiltinOp builtinOp() const noexcept {
        const BuiltinFunctionSpec* s = builtinSpec();
        return s ? s->op : BuiltinOp::None;
    }

    // 解析期参数校验错误（对齐 C# CheckArgumentType 的返回消息）；空串 = 通过
    [[nodiscard]] const QString& arityError() const noexcept { return m_arityError; }
    void setArityError(const QString& error) { m_arityError = error; }

    // 名字（原始写法；调用前统一 toUpper() 比较，对齐 Config.ICFunction）
    [[nodiscard]] const QString& name() const { return m_name; }
    [[nodiscard]] const QList<QSharedPointer<ExpressionNode>>& arguments() const { return m_args; }

    // 空实参占位（`FINDELEMENT(A, X, 1, , 1)`）：解析器用 LiteralNode(0) 占位，
    // 但「省略」与「显式 0」语义不同 —— 对齐 C# 的 `arguments[i] == null`
    // （FINDELEMENT 第 4 实参省略时取「数组末尾」）。这里单独记一笔。
    void markArgOmitted(int index) {
        if (index < 0) return;
        if (m_argOmitted.size() <= index) m_argOmitted.resize(index + 1, false);
        m_argOmitted[index] = true;
    }
    [[nodiscard]] bool isArgOmitted(int index) const {
        return index >= 0 && index < m_argOmitted.size() && m_argOmitted.at(index);
    }

private:
    QString m_name;
    QList<QSharedPointer<ExpressionNode>> m_args;
    QList<bool> m_referenceArguments;
    QList<bool> m_argOmitted;   // 该位实参是否写成空（见 markArgOmitted）
    OperandType m_type = OperandType::Int;   // 默认整数（C# 未标注时的宽松处理）
    bool m_isUserFunction = false;
    int  m_builtinIndex = -1;                // kBuiltinFunctions 下标；-1 = 非原生内置
    const BuiltinFunctionSpec* m_extensionSpec = nullptr; // 扩展式中函数声明（运行期表）
    QString m_arityError;                    // 空串 = 参数校验通过
};

// ---------------------------------------------------------------------------
// 内置函数调用校验（对齐 C# FunctionMethod.CheckArgumentType）
//
//   返回空串表示通过；否则返回可直接并入解析告警的中文消息。
//   `args` 已由解析器归约为强类型 AST（变量类型由 VariableTable 回填）。
// ---------------------------------------------------------------------------
[[nodiscard]] QString validateBuiltinCall(const BuiltinFunctionSpec& spec,
                                          const QList<QSharedPointer<ExpressionNode>>& args);

// 解析某个函数调用名：内置 / 用户 / 未定义。
//   userTypeProvider 为 nullptr 或返回 Unknown 视为非用户函数。
struct FunctionResolution {
    bool     isBuiltin = false;
    bool     isUserFunction = false;
    int      builtinIndex = -1;
    // 扩展式中函数（运行期注册表命中）时指向其声明；否则 nullptr。
    const BuiltinFunctionSpec* extensionSpec = nullptr;
    OperandType returnType = OperandType::Unknown;
};
[[nodiscard]] FunctionResolution resolveFunctionCall(
    const QString& name,
    const std::function<OperandType(const QString&)>& userTypeProvider);

// ---------------------------------------------------------------------------
// AST 遍历（前序深度优先）——用于解析期校验/类型回填等全局扫描。
// ---------------------------------------------------------------------------
void walkExpression(ExpressionNode& node,
                    const std::function<void(ExpressionNode&)>& visit);

// 面向诊断/测试的结构化导出：保留节点类别、强类型、变量下标和函数解析状态。
[[nodiscard]] QJsonObject expressionAstJson(const ExpressionNode& node);
[[nodiscard]] QString expressionAstDump(const ExpressionNode& node, int indent = 0);

// 三目：cond ? then # else
class IfNode : public ExpressionNode {
public:
    IfNode(QSharedPointer<ExpressionNode> condition,
           QSharedPointer<ExpressionNode> thenExpr,
           QSharedPointer<ExpressionNode> elseExpr = nullptr);

    [[nodiscard]] NodeKind kind() const noexcept override { return NodeKind::If; }
    [[nodiscard]] QString toString() const override;
    [[nodiscard]] OperandType valueType() const override {
        const OperandType tt = m_thenExpr ? m_thenExpr->valueType() : OperandType::Unknown;
        const OperandType et = m_elseExpr ? m_elseExpr->valueType() : OperandType::Unknown;
        if (isKnown(tt) && isKnown(et)) return tt == et ? tt : OperandType::Unknown;
        if (isKnown(tt)) return tt;
        if (isKnown(et)) return et;
        return OperandType::Int;
    }

    [[nodiscard]] QSharedPointer<ExpressionNode> condition() const { return m_condition; }
    [[nodiscard]] QSharedPointer<ExpressionNode> thenExpr() const { return m_thenExpr; }
    [[nodiscard]] QSharedPointer<ExpressionNode> elseExpr() const { return m_elseExpr; }

private:
    QSharedPointer<ExpressionNode> m_condition;
    QSharedPointer<ExpressionNode> m_thenExpr;
    QSharedPointer<ExpressionNode> m_elseExpr;
};

// ---------------------------------------------------------------------------
// StrForm（格式化串）：对齐 C# StrForm/StrFormPart + FormattedStringMethod
//   文本片段 + 内嵌表达式片段（{expr} / %expr%）
// ---------------------------------------------------------------------------
enum class StrFormPartType { Text, Expression };

struct StrFormPart {
    StrFormPartType type = StrFormPartType::Text;
    QString text;                                   // Text
    QSharedPointer<ExpressionNode> expression;      // Expression
    // Occurrence within one compiled template; equal IDs are the raw/markup views
    // of one source occurrence, distinct IDs must be evaluated independently.
    int occurrenceId = -1;

    QSharedPointer<ExpressionNode> width;
    bool leftAlign = false;
    OperandType expectedType = OperandType::Unknown;

    static StrFormPart makeText(const QString& t) {
        StrFormPart part;
        part.type = StrFormPartType::Text;
        part.text = t;
        return part;
    }
    static StrFormPart makeExpr(QSharedPointer<ExpressionNode> e) {
        StrFormPart part;
        part.type = StrFormPartType::Expression;
        part.expression = std::move(e);
        return part;
    }
};

class StrFormNode : public ExpressionNode {
public:
    // rawText 是该节点对应的原始 FORM 文本；parts 是同一文本在装载期生成的
    // 可执行结构。读取原始参数时使用 rawText()，求值时使用本节点本身。
    explicit StrFormNode(QList<StrFormPart> parts, QString rawText = {});

    [[nodiscard]] NodeKind kind() const noexcept override { return NodeKind::StrForm; }
    [[nodiscard]] QString toString() const override;
    [[nodiscard]] OperandType valueType() const override { return OperandType::Str; }

    [[nodiscard]] const QList<StrFormPart>& parts() const { return m_parts; }
    [[nodiscard]] bool isConst() const;   // 所有内嵌表达式均为字面量（供常量折叠）

    [[nodiscard]] const QString& rawText() const { return m_rawText; }

private:
    QList<StrFormPart> m_parts;
    QString m_rawText;
};

// 校验结果：message 为空 = 通过；indexKind 非空 = 该失败是 index_bounds 的
// 一类事实（调用方按 issuePolicy 取 severity/effect/唯一生产者）。
struct ExpressionIssue {
    QString message;
    std::optional<indexbounds::Kind> indexKind;
    [[nodiscard]] bool ok() const { return message.isEmpty(); }
};

[[nodiscard]] ExpressionIssue validateExpressionIssue(const ExpressionNode& node,
                                                      bool requireIndices = true);
// 兼容入口（运行期 evaluate() 与既有调用点）：只要文本。
[[nodiscard]] QString validateExpression(const ExpressionNode& node, bool requireIndices = true);

QSharedPointer<ExpressionNode> cloneExpression(const QSharedPointer<ExpressionNode>& node);

// C# 命名别名（便于对照阅读）
using OperandTerm = ExpressionNode;   // IOperandTerm
using SingleTerm  = LiteralNode;      // SingleTerm
using VariableTerm = VariableNode;    // VariableTerm
using StrFormTerm = StrFormNode;      // StrFormTerm

#endif // EXPRESSION_AST_H
