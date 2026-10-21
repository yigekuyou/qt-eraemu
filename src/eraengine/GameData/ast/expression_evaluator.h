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
#ifndef EXPRESSION_EVALUATOR_H
#define EXPRESSION_EVALUATOR_H

#include <QObject>
#include <QVariant>
#include <QString>
#include <QSet>
#include <QList>
#include <functional>

#include "expression_ast.h"
#include "print_template.h"
#include "variable_storage.h"
#include "../game_base_data.h"
#include "../mt19937.h"
#include "text_encoding.h"

class VariableStorage; // Forward declaration
class ConstantTable;  // Forward declaration（CSV 常量名表）

class ExpressionEvaluator : public QObject
{
    Q_OBJECT

public:
    explicit ExpressionEvaluator(QObject *parent = nullptr);
    ~ExpressionEvaluator() override;

    using FormIgnoreProvider = std::function<bool()>;
    void setFormIgnoreProvider(FormIgnoreProvider provider) { m_formIgnoreProvider = std::move(provider); }
    bool ignoreTripleSymbols() const { return m_formIgnoreProvider && m_formIgnoreProvider(); }


    // NEW: Direct AST evaluation entry points (for cached AST)
    // scope = 求值所在的作用域（函数名 intern id；0 = 顶层）。
    // 私有变量按名解析（Dynamic）时用它选名表；键已自足的节点不看它。
    QVariant evaluate(const ExpressionNode &node, VariableStorage *storage,
                      GameBaseData *gameBaseData = nullptr,
                      eravar::ScopeId scope = eravar::kGlobalScope);
    bool evaluateInt(const ExpressionNode &node, VariableStorage *storage, qint64 &result,
                     eravar::ScopeId scope = eravar::kGlobalScope);
    bool evaluateStr(const ExpressionNode &node, VariableStorage *storage, QString &result,
                     eravar::ScopeId scope = eravar::kGlobalScope);

    // Explicit string views. evaluateText discards structural meaning;
    // evaluateFlat performs one evaluation and returns an already-flattened value.
    [[nodiscard]] QString evaluateText(const ExpressionNode& node,
                                       VariableStorage* storage,
                                       GameBaseData* gameBaseData = nullptr,
                                       eravar::ScopeId scope = eravar::kGlobalScope);
    [[nodiscard]] FlatStringValue evaluateFlat(const ExpressionNode& node,
                                               VariableStorage* storage,
                                               GameBaseData* gameBaseData = nullptr,
                                               eravar::ScopeId scope = eravar::kGlobalScope);

    // 用户自定义函数回调：表达式 `NAME(args)` 命中用户函数时调用（由执行链提供）。
    // 返回 true 表示已处理，out 为返回值。
    // argNodes 与 args 一一对应（省略实参为 nullptr），供执行链识别
    // `#DIM REF` 引用形参的实参变量名（值本身丢失不了变量身份，但引用需要）。
    using UserFunctionInvoker = std::function<bool(const QString &name,
                                                   const QList<QVariant> &args,
                                                   const QList<const ExpressionNode*> &argNodes,
                                                   QVariant &out)>;
    void setUserFunctionInvoker(UserFunctionInvoker invoker) { m_userInvoker = std::move(invoker); }

    // CSV 常量名表（把变量字符串下标解析为整数下标，对齐 C# VariableStrArgTerm）
    void setConstantTable(const ConstantTable* table) { m_constantTable = table; }
    [[nodiscard]] const ConstantTable* constantTable() const { return m_constantTable; }

    // ---- 内置函数（内部命令）所需的运行期上下文 ----
    // 语言编码（对齐 C# LangManager.setEncode(Config.Encode)）：
    // STRLENS/SUBSTRING/STRFIND 的「位置/长度」按该编码的字节数计
    // （日文 Shift-JIS 下 ASCII=1 字节、假名/汉字=2 字节）。
    // `#DIM CONST NAME = value` 常量（求值期查表，装载顺序无关）。
    //
    // scope 是**本次求值的作用域**（evaluate() 的第四参数，原样转发）：
    // 私有常量的可见性只由它决定。回调**不得**再去推导作用域（例如读
    // 「解释器当前执行到哪一行」）—— 装载期求值时那是一个与「这行属于
    // 哪个函数」无关的值，会静默取到别的函数的同名私有常量。
    using ConstProvider = std::function<bool(const QString& name, eravar::ScopeId scope,
                                             QVariant& out)>;
    void setConstProvider(ConstProvider provider) { m_constProvider = std::move(provider); }

    void setLanguageEncoding(TextEncoding enc) { m_langEncoding = enc; }
    [[nodiscard]] TextEncoding languageEncoding() const { return m_langEncoding; }
    // 语言相关的字节长度（对齐 C# LangManager.GetStrlenLang）。公开给
    // 语句形式的 STRLENFORM（FORM_STR 实参）复用 —— 见 ExecutionEngine。
    [[nodiscard]] int langByteCount(const QString& s) const;

    // GETCONFIG / GETCONFIGS 的配置取值回调（返回 false = 未命中）。
    using IntegerConfigProvider = std::function<bool(const QString& key, qint64& value)>;
    void setIntegerConfigProvider(IntegerConfigProvider provider) { m_integerConfigProvider = std::move(provider); }
    using ConfigProvider = std::function<bool(const QString& key, QString& value)>;
    void setConfigProvider(ConfigProvider provider) { m_configProvider = std::move(provider); }

    // MONEYSTR / BARSTR 的显示参数（对齐 C# Config.MoneyLabel/MoneyFirst/BarChar1/BarChar2）
    void setMoneyLabel(const QString& label, bool moneyFirst);
    void setBarChars(QChar filled, QChar empty);

    // BAR / BARL 指令与 BARSTR(...) 函数共用的进度条串（C# ExpressionMediator.CreateBar）：
    // "[" + BarChar1×n + BarChar2×(length-n) + "]"，n = var*length/max（clamp 0..length）。
    // max<=0 / length<=0 / length>=100 在 C# 里是 CodeEE（脚本错误）；这里返回空串，
    // 与既有 BARSTR 行为一致（保守，不打断脚本）。
    [[nodiscard]] QString createBar(qint64 value, qint64 maxValue, qint64 length) const;

    // 角色数（SUMCARRAY/CMATCH/… 的范围上限，对齐 C# VEvaluator.CHARANUM）。
    // 默认回退到 CHARANUM 系统变量；执行链可注入实际值。
    // LINECOUNT：控制台当前行数（C# console.LineCount）
    using LineCountProvider = std::function<qint64()>;
    using CharaCountProvider = std::function<int()>;
    void setCharaCountProvider(CharaCountProvider provider) { m_charaNumProvider = std::move(provider); }
    void setLineCountProvider(LineCountProvider provider) { m_lineCountProvider = std::move(provider); }
    // CHKDATA 存在判定回调（EraEngine 注入：存档目录 + save{##}.sav 探测）
    // 对齐 C# CheckdataMethod（EraSaveFileType.Normal）：RESULT = 1 存在 / 0 不存在
    void setSaveExistsProvider(std::function<qint64(const QString&)> provider) {
        m_saveExistsProvider = std::move(provider);
    }

    // 当前文字颜色 / 字体样式（GETCOLOR / GETSTYLE）——由执行引擎维护并注入。
    // eraTW 的 COLORMESSAGE 会 `SAVE = GETCOLOR()` … `SETCOLOR SAVE` 地保存还原，
    // 因此两端的编码必须自洽（颜色 = 0xRRGGBB，样式 = 位掩码）。
    using ColorProvider = std::function<qint64()>;
    void setColorProvider(ColorProvider provider) { m_colorProvider = std::move(provider); }
    void setStyleProvider(ColorProvider provider) { m_styleProvider = std::move(provider); }
    // GETDEFCOLOR / GETBGCOLOR / GETDEFBGCOLOR / CURRENTREDRAW
    // （缺省：默认文字色 0xFFFFFF、背景 0、允许重绘 1）
    void setDefaultColorProvider(ColorProvider provider) { m_defaultColorProvider = std::move(provider); }
    void setBgColorProvider(ColorProvider provider) { m_bgColorProvider = std::move(provider); }
    void setDefaultBgColorProvider(ColorProvider provider) { m_defaultBgColorProvider = std::move(provider); }
    void setRedrawProvider(ColorProvider provider) { m_redrawProvider = std::move(provider); }
    // CURRENTALIGN / GETFONT / GETFOCUSCOLOR / CLIENTWIDTH / CLIENTHEIGHT /
    // ISSKIP / MESSKIP / MOUSESKIP / GETLINESTR / HTML_GETPRINTEDSTR / DEBUGCLEAR
    // （缺省：左对齐、默认字体、焦点色 0xFFFF00、0x0 客户区、无跳过）
    void setAlignProvider(ColorProvider provider) { m_alignProvider = std::move(provider); }
    void setFontProvider(std::function<QString()> provider) { m_fontProvider = std::move(provider); }
    void setFocusColorProvider(ColorProvider provider) { m_focusColorProvider = std::move(provider); }
    void setClientSizeProvider(std::function<QPair<int,int>()> provider) { m_clientSizeProvider = std::move(provider); }
    void setSkipProvider(std::function<int(int)> provider) { m_skipProvider = std::move(provider); }   // 0=ISSKIP 1=MESSKIP 2=MOUSESKIP
    void setLineStrProvider(std::function<QString(int)> provider) { m_lineStrProvider = std::move(provider); }

    // ---- 扩展式中函数（EE 等）的求值回调 ----
    // 求值归扩展侧：BuiltinOp::Extension 的节点交给此回调（实参已求值）。
    // 引擎不内联任何扩展名/实现；回调由注册类（ExtensionRegistry）提供。
    // 返回 true 表示已处理（out 为返回值）。
    using ExtensionFunctionInvoker = std::function<bool(const QString& name,
                                                       const QList<QVariant>& args,
                                                       const QList<const ExpressionNode*>& argNodes,
                                                       QVariant& out)>;
    void setExtensionFunctionInvoker(ExtensionFunctionInvoker invoker) {
        m_extensionFnInvoker = std::move(invoker);
    }
    void setHtmlPrintedProvider(std::function<QString(int)> getAll, std::function<QString()> pop) {
        m_htmlGetProvider = std::move(getAll);
        m_htmlPopProvider = std::move(pop);
    }
    void setClearProvider(std::function<void()> provider) { m_clearProvider = std::move(provider); }
    void setLineEmptyProvider(std::function<qint64()> provider) { m_lineEmptyProvider = std::move(provider); }
    // PRINTCLENGTH / PRINTCPERLINE（显示层排版参数，由 EraEngine 注入）
    void setPrintCProvider(std::function<QPair<int,int>()> provider) { m_printCProvider = std::move(provider); }
    // PRINTC 排版参数读取（PRINT_PALAM / PRINT_SHOPITEM 的每行列数）
    [[nodiscard]] QPair<int,int> printCLayout() const {
        return m_printCProvider ? m_printCProvider() : qMakePair(25, 3);
    }
    // SAVENOS()（式中函数）：C# GetSaveNosMethod 返回 Config.SaveDataNos
    // （「表示するセーブデータ数」，默认 20）——**不涉及任何文件读写**。
    void setSaveDataNosProvider(std::function<qint64()> provider) { m_saveDataNosProvider = std::move(provider); }
    [[nodiscard]] qint64 saveDataNos() const { return m_saveDataNosProvider ? m_saveDataNosProvider() : 20; }
    // MONEYSTR 显示参数读取（PRINT_SHOPITEM 的金额前后置，对齐 C# Config.MoneyLabel/MoneyFirst）
    [[nodiscard]] QString moneyLabel() const { return m_moneyLabel; }
    [[nodiscard]] bool moneyFirst() const { return m_moneyFirst; }

    // fork 扩展（EM 的 SETVAR）用的写入原语：把**已求值**的值写进变量节点指代的
    // 变量（整数/字符串都走这里）。解析「变量名文本 -> 变量节点」是扩展侧经
    // 解析表服务做的（那边有变量表/常数表/作用域）；求值器只负责最后一步写回。
    bool assignNodeValue(const ExpressionNode& target, VariableStorage* storage,
                         GameBaseData* gameBaseData, const QVariant& value,
                         eravar::ScopeId scope = eravar::kGlobalScope);

    // 存档/文本类函数（SAVETEXT/LOADTEXT/SAVECHARA/LOADCHARA/GSAVE/GLOAD/SAVENOS）
    // 的落盘目录 = 游戏目录下的 sav/（对齐 C# VEvaluator 的保存路径），由 EraEngine 注入。
    void setSaveDirectory(const QString& dir);
    // 游戏目录（对齐 C# `Program.ExeDir`）：LOADTEXT / SAVETEXT 的**路径形态**
    // 与 EXISTFILE 把脚本给的相对路径拼在它之下（`Utils.GetValidPath`）。
    void setBaseDirectory(const QString& dir) { m_baseDirectory = dir; }
    // LOADTEXT / SAVETEXT 允许的扩展名（对齐 C# `Config.ValidExtension`，
    // 配置键「LOADTEXTとSAVETEXTで使える拡張子」，默认 {"txt"}）。
    void setValidExtensionsProvider(std::function<QStringList()> provider) {
        m_validExtensionsProvider = std::move(provider);
    }
    [[nodiscard]] QStringList validExtensions() const;   // 空 = {"txt"}（C# 默认值）
    // 脚本给出的相对路径 -> 基准目录下的绝对路径；**空 = C# GetValidPath 的 null**
    // （绝对路径 / 有根路径被拒绝）。EXISTFILE、LOADTEXT/SAVETEXT 路径形态共用。
    [[nodiscard]] QString resolveScriptPath(const QString& path) const;
    void setSaveCompressionProvider(std::function<bool()> provider) { m_saveCompressionProvider=std::move(provider); }
    void reloadCharacterSnapshots();
    void setCharacterDirectory(const QString& directory) { m_characterDirectory=directory; reloadCharacterSnapshots(); }
    [[nodiscard]] QString saveFilePath(const QString& fileName) const;

    // `#DIM CONST NAME, N = …` 常数数组的下标查询（注入自 VariableTable）
    //   * checker：只判断名字是不是常数数组（**必须能不求值下标**，
    //     否则 `BAG:(I++)` 的下标会被求值两次、副作用算两遍）；
    //   * provider：命中后再按下标取值。
    // 三者都收**本次求值的作用域**（同 ConstProvider），语义按 (name, scope) 查表：
    // 先本函数的私有常量，再广域常量。
    using ConstArrayProvider = std::function<bool(const QString& name, int index,
                                                  eravar::ScopeId scope, QVariant& out)>;
    using ConstArrayChecker  = std::function<bool(const QString& name, eravar::ScopeId scope)>;
    // 常数数组的长度（`#DIM(S) CONST X, N = …` -> N）。VARSIZE / FINDELEMENT 等
    // 「按数组名取长度/遍历」的内建函数必须走这里：常量的元素存在**常数表**里，
    // 不在 VariableStorage，只按存储长度取会得到 0。
    using ConstArraySizeProvider = std::function<int(const QString& name, eravar::ScopeId scope)>;
    void setConstArrayProvider(ConstArrayProvider p) { m_constArrayProvider = std::move(p); }
    void setConstArrayChecker(ConstArrayChecker c) { m_constArrayChecker = std::move(c); }
    void setConstArraySizeProvider(ConstArraySizeProvider p) { m_constArraySizeProvider = std::move(p); }

    // 变量声明维度（1/2/3）：由 EraParseTable 的 VariableTable 注入
    using VariableDimProvider = std::function<int(const QString&)>;
    void setVariableDimProvider(VariableDimProvider p) { m_variableDimProvider = std::move(p); }
    [[nodiscard]] int variableDimension(const QString& name) const;
    [[nodiscard]] QList<int> resolveIndices(const VariableNode& node, VariableStorage* storage,
                                            GameBaseData* gameBaseData,
                                            eravar::ScopeId scope = eravar::kGlobalScope);

    // ================= 节点级整数访问的唯一分派 =================
    // 直接按节点已定型的 kind 分派（键自足：不再与「当前作用域」对账），
    // 未定型/按键不自足的 kind 落到 VariableStorage::readIntByName /
    // writeIntByName（scope 显式传入，供按名解析选私有名表）。
    // 旧 ScriptRunner::readIntVarAccess / writeIntVarAccess 的 switch 已并入这里。
    [[nodiscard]] qint64 readIntNode(const VariableNode& node, const QList<int>& indices,
                                     eravar::ScopeId scope = eravar::kGlobalScope) const;
    void writeIntNode(const VariableNode& node, const QList<int>& indices, qint64 value,
                      eravar::ScopeId scope = eravar::kGlobalScope);
    // 分派所需的存储（作用域守卫 + 名字级原语）。接线时注入，并顺带给存储
    // 注册维数 provider（与 variableDimension 同源）。
    void setVariableStorage(VariableStorage* storage);

    // ---- 随机数（对齐 C# VariableEvaluator + MTRandom/MT19937）----
    //   启动时用系统随机源产生种子；RANDOMIZE / setRandomSeed 可复现。
    [[nodiscard]] Mt19937& random() { return m_rand; }
    [[nodiscard]] const Mt19937& random() const { return m_rand; }
    void setRandomSeed(quint32 seed) { m_rand.reseed(seed); }
    [[nodiscard]] quint32 randomSeed() const { return m_rand.seedUsed(); }
    void randomize() { m_rand.reseed(Mt19937::randomSeed()); }

signals:
    void evaluationFinished(const QString &expression, const QVariant &result);
    void evaluationError(const QString &expression, const QString &errorString);

public slots:
    
private:
    // 变量下标求值：字符串下标经 ConstantTable 映射为整数（如 CFLAG:ARG:現在位置）
    qint64 resolveIndex(const VariableNode& node, int index,
                        VariableStorage* storage, GameBaseData* gameBaseData,
                        eravar::ScopeId scope);

    // AST evaluation methods
    QVariant evaluateUnstructuredNode(const ExpressionNode &node, VariableStorage *storage,
                                      GameBaseData *gameBaseData, eravar::ScopeId scope);
    QVariant evaluateNode(const ExpressionNode &node, VariableStorage *storage,
                          GameBaseData *gameBaseData, eravar::ScopeId scope);
    QVariant evaluateLiteral(const LiteralNode &node);
    // 二元求值的两条路径（整数快路径 / 字符串慢路径）
    QVariant evaluateIntBinary(TokenType op, qint64 l, qint64 r);
    QVariant evaluateStrBinary(TokenType op, const QString& ls, const QString& rs,
                               const QVariant& left, const QVariant& right);
    QVariant evaluateStrForm(const StrFormNode &node, VariableStorage *storage,
                             GameBaseData *gameBaseData, eravar::ScopeId scope);
    QVariant evaluateVariable(const VariableNode &node, VariableStorage *storage,
                              GameBaseData *gameBaseData, eravar::ScopeId scope);

    // ================= 变量读写的解析链（表驱动）=================
    // 旧 evaluateVariable 尾部那条 ~300 行 if 链（顺序即语义）被**逐块**剪成
    // 下面这些独立方法（函数体逐字相同，只改了签名与 return 的包装），再由
    // resolverTable() 这张有序表描述流程：一行 = 旧链的一块。
    //   · kinds     ：该行对哪些 kind 生效（位掩码）；kAllNames 的行是「按运行期
    //                 名字判定的通用谓词」（别名/参数/槽名/常数/终局），必须对所有
    //                 kind 运行 —— 旧链本来对每个名字都跑它们。
    //   · needsKey  ：该行是否消费 accessKey（= VariableNode::usesKey(kind)，
    //                 守卫的适用范围就是这列）。
    //   · read/write：命中即返回；write == nullptr 表示旧写路径没有对应块。
    // 真正的 Dynamic 节点：所有行的 kinds 都含它 -> 全表按序跑 = 旧链原样。
    struct DynamicContext {
        VariableNode::AccessKind kind = VariableNode::AccessKind::Dynamic;  // 定型后的 kind
        const VariableNode* node = nullptr;
        VariableStorage* storage = nullptr;
        GameBaseData* gameBaseData = nullptr;
        eravar::ScopeId scope = eravar::kGlobalScope;   // 按名解析使用的显式作用域
        // 旧链的局部量（逐字沿用）：varName 可被 REF 别名重映射。
        QString varName;
        int count = 0;
        bool aliased = false;
        bool chara = false;
        int declaredDimension = 0;
        int dimension = 0;
        int full = 0;
    };
    enum class WriteResult : quint8 { Unhandled, Written, Rejected };
    struct Resolver {
        quint16 kinds = 0;       // 生效的 kind 位掩码
        bool needsKey = false;   // 该行消费 accessKey（守卫只对这类 kind 生效）
        bool (ExpressionEvaluator::*read)(DynamicContext&, QVariant&) = nullptr;
        WriteResult (ExpressionEvaluator::*write)(DynamicContext&, qint64) = nullptr;
    };
    [[nodiscard]] static const QList<Resolver>& resolverTable();
    // 旧链的公共前缀（旧 820-822：varName / 下标数 / 是否被重映射）。
    // withAlias=false 时不算 aliased（写路径旧代码没有这个量）。
    [[nodiscard]] DynamicContext makeDynamicContext(const VariableNode& node,
                                                    VariableStorage* storage,
                                                    GameBaseData* gameBaseData,
                                                    VariableNode::AccessKind kind,
                                                    bool withAlias,
                                                    eravar::ScopeId scope) const;
    // 已定型 kind 的快路径（旧 switch 的 4 块：键直取）
    bool readKeyedInt(DynamicContext& ctx, QVariant& out);
    bool readKeyedStr(DynamicContext& ctx, QVariant& out);
    bool readKeyedLocalArgInt(DynamicContext& ctx, QVariant& out);
    bool readKeyedLocalArgStr(DynamicContext& ctx, QVariant& out);
    // 旧 Dynamic if 链逐块（顺序 = 表顺序 = 旧链顺序）
    bool readStringParameterAlias(DynamicContext& ctx, QVariant& out);   // 旧 823-836
    bool applyReferenceAlias(DynamicContext& ctx, QVariant& out);        // 旧 837-841（只改 ctx）
    bool bindDimension(DynamicContext& ctx, QVariant& out);              // 旧 842-860（只改 ctx / throw）
    bool readParameter(DynamicContext& ctx, QVariant& out);              // 旧 861
    bool readCharaData(DynamicContext& ctx, QVariant& out);              // 旧 866-879
    bool readGameBase(DynamicContext& ctx, QVariant& out);               // 旧 882-888
    bool readSlotNames(DynamicContext& ctx, QVariant& out);              // 旧 891-941
    bool readLineCount(DynamicContext& ctx, QVariant& out);              // 旧 944-946
    bool readRandom(DynamicContext& ctx, QVariant& out);                 // 旧 950-957
    bool readIntLimits(DynamicContext& ctx, QVariant& out);              // 旧 960-969
    bool readRandData(DynamicContext& ctx, QVariant& out);               // 旧 972-980
    bool readConst(DynamicContext& ctx, QVariant& out);                  // 旧 985-1000
    bool readStringVariable(DynamicContext& ctx, QVariant& out);         // 旧 1004-1035
    bool readLastLoad(DynamicContext& ctx, QVariant& out);               // 旧 1037-1038
    bool readIndexedArray(DynamicContext& ctx, QVariant& out);           // 旧 1041-1078（终局）
    bool readSimpleVariable(DynamicContext& ctx, QVariant& out);         // 旧 1080-1088（终局）
    // 写路径里**确实存在**的 3 块（旧 assignVariable 尾部的顺序）
    WriteResult writeParameter(DynamicContext& ctx, qint64 value);       // 旧 hasParameter 写
    WriteResult writeCharaStringReject(DynamicContext& ctx, qint64 value);
    WriteResult writeSlotStringReject(DynamicContext& ctx, qint64 value);
    QVariant evaluateIndexedVariable(const QString &varName, int index, VariableStorage *storage);
    QVariant evaluateBinaryOp(const BinaryOpNode &node, VariableStorage *storage,
                              GameBaseData *gameBaseData, eravar::ScopeId scope);
    // 变量写入（整型）：++/-- 的副作用
    bool assignVariable(const VariableNode& node, VariableStorage* storage,
                        GameBaseData* gameBaseData, qint64 value, eravar::ScopeId scope);

    QVariant evaluateUnaryOp(const UnaryOpNode &node, VariableStorage *storage,
                             GameBaseData *gameBaseData, eravar::ScopeId scope);
    QVariant evaluateFunction(const FunctionNode &node, VariableStorage *storage,
                              GameBaseData *gameBaseData, eravar::ScopeId scope);

    // ---- 内置函数（内部命令）求值 ----
    // 返回 false 表示该 opcode 尚未实现求值（交给用户函数回调 / 默认 0）。
    bool evaluateBuiltin(const FunctionNode &node, VariableStorage *storage,
                         GameBaseData *gameBaseData, QVariant &out, eravar::ScopeId scope);

    // GDRAWG / GDRAWSPRITE 的颜色矩阵实参：从 2D/3D 数组变量的给定下标起
    // 读 5x5 个整数（/256，对齐 C# ReadColormatrix），失败返回 false。
    bool readColorMatrix(const FunctionNode &node, int argNo, VariableStorage *storage,
                         GameBaseData *gameBaseData, float out[5][5], eravar::ScopeId scope);

    // 实参取值辅助
    qint64  argInt(const FunctionNode &node, int i, VariableStorage *storage, GameBaseData *g,
                   eravar::ScopeId scope);
    QString argStr(const FunctionNode &node, int i, VariableStorage *storage, GameBaseData *g,
                   eravar::ScopeId scope);
    [[nodiscard]] const VariableNode* argVar(const FunctionNode &node, int i) const;
    [[nodiscard]] static bool hasArg(const FunctionNode &node, int i);

    // 字符串变量写入（系统槽/用户槽/角色变量/参数一律按已解析下标路由）。
    bool assignStringVariable(const VariableNode& node, VariableStorage* storage,
                              GameBaseData* gameBaseData, const QString& value,
                              eravar::ScopeId scope);

    // 数组：读一维整型数组内容 / 取长度（对齐 C# VariableTerm 的数组访问）
    QList<qint64> readIntArray(const VariableNode &var, VariableStorage *storage,
                               GameBaseData *gameBaseData, bool charaRange,
                               eravar::ScopeId scope) const;
    // 字符串一维数组内容（FINDELEMENT 等需要按字符串比较，如 BASENAME/ABLNAME）
    QList<QString> readStrArray(const VariableNode &var, VariableStorage *storage,
                                eravar::ScopeId scope) const;
    [[nodiscard]] int charaCount(VariableStorage *storage) const;

    // TOINT / ISNUMERIC 的 Emuera 语义（对齐 C# ToIntMethod / IsNumericMethod）
    [[nodiscard]] qint64 parseIntLikeEmuera(const QString& str) const;
    [[nodiscard]] bool   isNumericLikeEmuera(const QString& str) const;

    // 语言相关的字节长度（对齐 C# LangManager.GetStrlenLang / GetSubStringLang / GetUFTIndex）
    // （langByteCount 已在 public 区声明，供 STRLENFORM 语句形式复用）
    [[nodiscard]] int langByteCountOfChar(QChar c) const;
    [[nodiscard]] QString langSubstring(const QString& s, int startIndex, int length) const;
    [[nodiscard]] int langIndexOf(const QString& target, const QString& word, int langStart) const;

    // Helper methods for expression evaluation

    FormIgnoreProvider m_formIgnoreProvider;
    UserFunctionInvoker m_userInvoker;
    IntegerConfigProvider m_integerConfigProvider;
    ConfigProvider m_configProvider;
    std::function<bool()> m_saveCompressionProvider;
    CharaCountProvider m_charaNumProvider;
    LineCountProvider m_lineCountProvider;
    std::function<qint64(const QString&)> m_saveExistsProvider;
    ColorProvider m_colorProvider;        // GETCOLOR
    ColorProvider m_styleProvider;        // GETSTYLE
    ColorProvider m_defaultColorProvider; // GETDEFCOLOR
    ColorProvider m_bgColorProvider;      // GETBGCOLOR
    ColorProvider m_defaultBgColorProvider; // GETDEFBGCOLOR
    ColorProvider m_redrawProvider;       // CURRENTREDRAW
    ColorProvider m_alignProvider;        // CURRENTALIGN
    std::function<QString()> m_fontProvider;                 // GETFONT
    ColorProvider m_focusColorProvider;   // GETFOCUSCOLOR
    std::function<QPair<int,int>()> m_clientSizeProvider;    // CLIENTWIDTH/HEIGHT
    std::function<int(int)> m_skipProvider;                  // ISSKIP/MESSKIP/MOUSESKIP
    std::function<QString(int)> m_lineStrProvider;           // GETLINESTR
    ExtensionFunctionInvoker    m_extensionFnInvoker;        // 扩展式中函数（BuiltinOp::Extension）
    std::function<QString(int)> m_htmlGetProvider;           // HTML_GETPRINTEDSTR
    std::function<QString()> m_htmlPopProvider;              // HTML_POPPRINTINGSTR
    std::function<void()> m_clearProvider;                   // DEBUGCLEAR
    std::function<qint64()> m_lineEmptyProvider;             // LINEISEMPTY
    QString m_saveDirectory;
    QString m_characterDirectory;
    QHash<QString, EraBinaryData::Document> m_characterSnapshots;
    QString characterFilePath(const QString& name) const;
    std::function<QPair<int,int>()> m_printCProvider;        // PRINTCLENGTH/PERLINE
    std::function<qint64()> m_saveDataNosProvider;           // SAVENOS()
    Mt19937 m_rand;                       // MT19937（启动时随机种子）
    VariableDimProvider m_variableDimProvider;
    VariableStorage* m_storage = nullptr;   // readIntNode/writeIntNode 的分派目标
    ConstArrayProvider  m_constArrayProvider;
    ConstArrayChecker   m_constArrayChecker;
    ConstArraySizeProvider m_constArraySizeProvider;
    QString m_baseDirectory;                             // 游戏目录（Program.ExeDir）
    std::function<QStringList()> m_validExtensionsProvider;   // Config.ValidExtension
    const ConstantTable* m_constantTable = nullptr;
    TextEncoding m_langEncoding = TextEncoding::ShiftJis;
    ConstProvider m_constProvider;   // #DIM CONST
    QSet<QString> m_reportedUnfinished;   // 「未完成」告警去重（同一函数只报一次）
    QString m_moneyLabel = QStringLiteral("$");
    bool    m_moneyFirst = true;
    QChar   m_barFilled = QLatin1Char('*');
    QChar   m_barEmpty  = QLatin1Char('.');
};

#endif // EXPRESSION_EVALUATOR_H
