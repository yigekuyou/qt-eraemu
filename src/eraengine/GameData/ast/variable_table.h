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
#include <QSet>
#include <functional>
#include <QString>
#include <QList>
#include <QMap>
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
    QString     function;                    // Local 时所属函数（标签名，仅展示/形参语义）
    QString     scopeKey;                    // Local 时的段身份文本（私有域的唯一键）
    int         dimension = 1;
    QList<int>  lengths;
    // 「维数 -> 扁平偏移」的装载期几何：strides[i] = lengths[i+1..] 的乘积。
    // 只在 dimension >= 2 时写（单维保持轻量形态，strides 为空）；
    // elementCount = 各维乘积（dimension <= 1 时等于 lengths[0]，空 lengths 视作 1）。
    // 运行期只读这两个字段，绝不再按维度重新推导。
    QList<int>  strides;
    qint64      elementCount = 0;
    QStringList lengthExprs;   // 原始维数表达式（可含常数名）
    bool        isPrivate = false;
    bool        isConst = false;
    bool        isArg = false;               // 来自 @F(A,B) 形参表
    bool        isReference = false;         // #DIM(S) REF：调用方数组的别名
    bool        isCharaData = false;         // #DIM(S) CHARADATA：每角色一份
    bool        isSavedata = false;
    bool        isGlobal = false;
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

    // 查声明：先按段私有，再按全局。scopeKey = 段身份文本（eravar::segmentKeyText）
    // —— **不是函数名**；空串 = 无私有上下文。
    //
    // 段身份文本在这里是**大小写敏感**的（含脚本名，与 m_locals 的键同形）。
    // 但存储键的段身份部分经 eravar::makePrivateStorageKey **统一大写化**（唯一
    // 构造点），于是「按存储键回查声明」（VARSIZE / shapeOf）拿到的是大写形态。
    // 因此精确命中失败时再按「大写 -> 原文」索引命中一次 —— 只是把同一条段身份
    // 换个大小写写法，不引入任何新语义。
    [[nodiscard]] const VariableDecl* find(const QString& name,
                                           const QString& scopeKey = QString()) const;
    // 按**名字**查 CHARADATA 声明（忽略作用域）。
    // 角色数据在本移植里是名字级身份：识别与存储都以名字为键
    // （VariableStorage::m_charaDataVars），同一个名字不可能按段分成两份形状。
    // 因此形状查询（VARSIZE）必须能按名字找到那条声明 —— 否则
    // `#DIM CHARADATA X, 3, 4` 在全局上下文（VARSIZE("X")）报 0。
    // 同名多份声明 first-wins（与 addChecked 一致）。
    [[nodiscard]] const VariableDecl* findCharaData(const QString& name) const;
    [[nodiscard]] bool contains(const QString& name) const;
    // 该名字是否**只**作为某个函数的私有变量/常量存在（广域声明与系统变量都没有）。
    // 对齐 C# IdentifierDictionary.privateDimList：在这种名字于当前函数解析不到时，
    // C# 抛 IdentifierNotFoundCodeEE「変数"X"はこの関数中では定義されていません」。
    [[nodiscard]] bool isPrivateOnlyName(const QString& name) const;

    // 类型解析：段上下文优先；无上下文时仅全局 + 「全表唯一局部」
    [[nodiscard]] OperandType typeOf(const QString& name,
                                     const QString& scopeKey = QString()) const;

    [[nodiscard]] int count() const;
    [[nodiscard]] QList<VariableDecl> declarations() const;
    // perf：declarations() 的缓存版（装载完成后不再变化；clear/维数定稿时失效）。
    // saveShapes / resetForNewGame / applyBinarySave 每次存读档要调 2~4 次，
    // 旧写法每次都重建 18515 项的 QList（每项多个 QString/QList 浅拷贝）。
    [[nodiscard]] const QList<VariableDecl>& declarationsRef() const;
    // 某段（段身份文本）的私有变量声明
    [[nodiscard]] QList<VariableDecl> localsOf(const QString& scopeKey) const;
    // perf：执行热路径（每条指令都要解析私有作用域）用 —— 装载完成后 m_locals
    // 不再变化，按函数缓存声明列表，返回引用免去每次调用逐个拷贝 VariableDecl。
    [[nodiscard]] const QList<VariableDecl>& localsOfRef(const QString& scopeKey) const;
    // perf：私有作用域的**非 const 名字表**（setPrivateScope 的入参）也按函数缓存。
    // 此前每次 CALL/RETURN 都重建 QStringList（eraTW 地图逐字符 TRYCALLFORM 时
    // 一次绘制上万次），这里装载后不变，返回引用即可。
    [[nodiscard]] const QStringList& localNamesOfRef(const QString& scopeKey) const;

    // ---- 私有作用域名表（装载期建立，运行期只读）----
    // 「某函数里有哪些私有变量、各自的存储键是什么」在声明定稿之后就是**静态
    // 事实**。此前的实现把它放在 VariableStorage::setPrivateScope 里、每次换函数
    // 按调用方传进来的名字表重建（QHash 分配 + 每个名字一次 makePrivateStorageKey），
    // 且正确性依赖调用方把名字传全 —— 漏一个名字那个变量就静默退化成裸名。
    // 现在由声明（scope == Local 且非 const，与 localNamesOfRef 同一过滤口径）
    // 一次建好，经 VariableStorage::installPrivateScopeTable 装进存储层。
    // 形状：ScopeId（段身份 intern id） -> 名字(大写) -> 存储键 "段身份\x1fNAME"。
    using PrivateNameTable = QHash<eravar::ScopeId, QHash<QString, QString>>;
    // 前置条件：声明已定稿（resolveGlobalDeclarations 之后）。装载期调用一次即可。
    [[nodiscard]] PrivateNameTable buildPrivateNameTable() const;

    void clear();

    // ---- 常数（#DIM CONST name = value）与维数求值 ----
    //
    // CONST 是**有作用域**的，与广域/局部变量同源（对齐 C#：
    //   * 函数内的 `#DIM CONST X` 走 LogicalLine.AddPrivateVariable()，登记在
    //     **该函数标签**的私有变量表里 —— `ErbLoader`/`UserDefinedVariableData`
    //     把 isPrivate 记进 data.Private；
    //   * 查询走 IdentifierDictionary.GetVariableToken(key, …, allowPrivate)：
    //     先用 `line.ParentLabelLine.GetPrivateVariable(key)` 查**当前函数**的私有
    //     变量，找不到才回落到广域/系统变量）。
    //
    // 因此这里按「归属作用域 owner」分离存储：
    //   * owner 为空串 = 广域常量（.ERH 文件级 / 函数外 / `#DIM GLOBAL CONST`），
    //     处处可见；
    //   * owner 非空   = 该函数的私有常量，**只在该函数作用域内可见**。
    //
    // 查询统一走 (name, scope)：先本作用域（私有常量），再广域；并且**本作用域
    // 声明的同名可写变量（`#DIM X`）遮蔽同名常量**（C# 先查私有变量表）。
    // 此前常数表只有一张扁平的 name→value 映射，运行期求值按名字直接命中 ——
    // 于是任何一处 `#DIM CONST 削除行数` 都会盖住别处同名的私有变量
    // （eraTW 的 `SETCOLOR 色_RED7` / `CLEARLINE 削除行数 + 1` 都读成了常量的值）。
    void setConstInt(const QString& name, qint64 value, const QString& owner = QString());
    // `#DIM CONST NAME, N = v0, v1, …`：常数**数组**（下标访问用）
    void setConstArray(const QString& name, const QList<qint64>& values, const QString& owner = QString());
    [[nodiscard]] bool constArrayAt(const QString& name, int index, qint64& out,
                                    const QString& scope = QString()) const;
    [[nodiscard]] int constArraySize(const QString& name, const QString& scope = QString()) const;
    void setConstStr(const QString& name, const QString& value, const QString& owner = QString());
    // `#DIMS CONST NAME, N = s0, s1, …`：字符串常数数组（eraTW 的 DISP_MEMO 等）
    void setConstStrArray(const QString& name, const QStringList& values, const QString& owner = QString());
    [[nodiscard]] bool constStrArrayAt(const QString& name, int index, QString& out,
                                       const QString& scope = QString()) const;
    [[nodiscard]] int constStrArraySize(const QString& name, const QString& scope = QString()) const;
    // `#DIM CONST NAME = <非常量字面量的表达式>`（如 `= 人物数量上限`）。
    // 声明顺序 / 跨文件顺序不可靠，所以**不在装载期**求值，而是把表达式留到
    // 求值期惰性计算（`#DIM CONST OBJ_ID_LAST = 人物数量上限` 曾因此恒为 0，
    // 进而 INRANGE(…, OBJ_ID_LAST) 为假 -> eraTW 的 EXISTOBJ 抛 THROW）。
    void setConstExprs(const QString& name, const QStringList& exprs, const QString& owner = QString());
    [[nodiscard]] QStringList constExprs(const QString& name, const QString& scope = QString()) const;
    [[nodiscard]] bool constInt(const QString& name, qint64& out, const QString& scope = QString()) const;
    [[nodiscard]] bool constStr(const QString& name, QString& out, const QString& scope = QString()) const;
    [[nodiscard]] int constCount() const;

    // 用常数表重新求值所有声明的维数（后声明的常数也能修正先前的声明）
    void resolveDimensions();
    // 维数是否已定稿（resolveDimensions 跑过且之后没有新增声明）。
    // 存档映射（erasave::Map）按 `VariableDecl::lengths` 构表：未定稿时
    // lengths 还可能是“声明文本的形状”而不是求值后的形状 —— 存档入口用它
    // 把「声明可建表」变成可检查的前置条件。
    [[nodiscard]] bool dimensionsResolved() const { return m_dimensionsResolved; }
    // 维数表达式的兜底求值器（`#DIM X, CLASS_NUM + 1` 这类算式/复合表达式）。
    // 由 EraParseTable 在常量表就绪后注入 —— 求值器内部会回查本表的常数。
    // 第二参数 = 声明所在函数（私有常量要与维数同作用域才可见）。
    void setDimEvaluator(std::function<qint64(const QString&, const QString&)> fn) {
        m_dimEvaluator = std::move(fn);
    }

    // 把一个已构建的 AST 中的变量节点按表回填类型（强类型回填），并在同一趟里
    // 计算装载期「变量分词」访问描述符（VariableNode::accessKind/accessKey）。
    // paramNames：该函数全部形参名（大写；可为 null）—— 命中形参名的变量引用
    // 无论取值/别名/REF 绑定都是运行期的，标记为 Dynamic 回退旧路。
    //
    // evalConst：装载期常量求值器（可选）。装着它时，同一趟定型顺手把每个
    // 下标节点的 `ExpressionNode::constantIndex` 写成**算好的值**：
    //   · 形态不恒定（含变量/函数/++）        -> Unknown；
    //   · 形态恒定且求值成功                  -> Value；
    //   · 形态恒定但求值失败（除零/溢出/未定义）-> Invalid。
    // 这是该字段的**唯一**写入点：之后 3/6 与 6/6 只读字段，不再各自辨认
    // LiteralNode / UnaryOp(MINUS,Literal)。求值器由装载层注入（它知道
    // ExpressionEvaluator / storage / 作用域），本表不依赖求值器类型。
    // 返回 nullopt = 求值失败；返回值必须是「运行期同一表达式的结果」。
    using ConstantIndexEvaluator = std::function<std::optional<qint64>(const ExpressionNode&)>;
    // needsScope（可选出参）：同一趟 walk 里 OR 出「这棵树是否有任一
    // VariableNode 的 kind 需要执行位置」（Param/Chara/Const/Dynamic，见
    // VariableNode::needsExecutionScope）。调用方先置 false；本函数只做 |=。
    // 条件 AST 的唯一写入点就是这里（收尾定型 -> LogicalLine::conditionNeedsScope）。
    static void applyTypes(ExpressionNode& node, const VariableTable& table,
                           const QString& function = QString(),
                           const QSet<QString>* paramNames = nullptr,
                           const ConstantIndexEvaluator& evalConst = {},
                           bool* needsScope = nullptr);

    // ---- 调试：访问描述符判定分布（默认关闭；EMUERA_KIND_STATS=1 才计数）----
    // 只给 dump_warnings --kind-stats 用：一次装载后按 kind 数变量引用节点。
    // 不在默认路径上（未设环境变量时 computeVariableAccess 只多一次静态 bool 判断）。
    [[nodiscard]] static QMap<QString, qint64> accessKindHistogram();
    static void resetAccessKindHistogram();

private:
    // 求值单个数维表达式：数字字面量或常数名（scope = 声明所在函数）
    [[nodiscard]] bool evalDim(const QString& expr, int& out, const QString& scope = QString()) const;
    // 本作用域是否声明了同名**可写变量**（遮蔽同名常量）
    [[nodiscard]] bool shadowedByVariable(const QString& name, const QString& scope) const;
    // 常数表的统一查询：先 scope 归属的私有常量，再广域常量
    template <typename T>
    [[nodiscard]] const T* findScopedConst(const QHash<QString, QHash<QString, T>>& store,
                                           const QString& name, const QString& scope) const;

    QHash<QString, VariableDecl> m_globals;
    // 常数表按「归属作用域 owner（空串 = 广域）-> 名字 -> 值」分离，
    // 与变量声明的广域/局部划分一一对应（见头文件上方说明）。
    QHash<QString, QHash<QString, qint64>>  m_constInt;
    QHash<QString, QHash<QString, QList<qint64>>> m_constArray;
    QHash<QString, QHash<QString, QStringList>> m_constStrArray;   // 字符串常数数组
    QHash<QString, QHash<QString, QString>> m_constStr;
    QHash<QString, QHash<QString, QStringList>> m_constExpr;   // 未折叠的 CONST 初值表达式
    std::function<qint64(const QString&, const QString&)> m_dimEvaluator;   // 维数表达式兜底求值
    // 反向索引：局部变量名 -> 声明它的函数个数 / 唯一时的类型（空上下文 O(1) 查询）
    QHash<QString, int> m_localNameCount;
    QHash<QString, OperandType> m_uniqueLocalType;
    QHash<QString, QHash<QString, VariableDecl>> m_locals;  // function -> name -> decl
    // m_locals 的段身份键的**大写形态 -> 原文**索引：只为「按存储键回查声明」
    // 服务（存储键的段身份部分统一大写化，见 find 的说明）。精确键优先。
    QHash<QString, QString> m_localScopeByUpper;
    // CHARADATA 声明按名字索引（见 findCharaData）：名字级身份，与段无关。
    QHash<QString, VariableDecl> m_charaData;
    // localsOfRef 的按函数缓存（装载后 m_locals 不变；执行单线程）
    mutable QHash<QString, QList<VariableDecl>> m_localsRefCache;
    // localNamesOfRef 的按函数缓存（同上）
    mutable QHash<QString, QStringList> m_localNamesRefCache;
    // declarationsRef 的缓存（失效点与上面两个相同：clear / 维数定稿）
    mutable QList<VariableDecl> m_declarationsCache;
    // 维数定稿标志：新增声明会把它复位（新增的声明还没求值过维数）。
    bool m_dimensionsResolved = false;
};

#endif // AST_VARIABLE_TABLE_H
