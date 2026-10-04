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
#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QtGlobal>
#include <functional>

#include "ast/ast_builder.h"       // registerFormArgFunction（注册类可以插入 AST）
#include "ast/function_types.h"    // isBuiltinFunction（核心名 fail-fast）
#include "ast/logical_line.h"      // LogicalLine / Operand
#include "ast/system_variables.h"  // regVariable（扩展系统变量 -> 运行期扩展表）
#include "constant_table.h"        // ConstantTable::coreCsvForVariable（名表 fail-fast）
#include "eraengine_log.h"         // eraTrace（桩留痕）
#include "variable_storage.h"      // Services::storage（扩展实现写 RESULT/RESULTS）

#include "ee_extension.h"        // EE 扩展（只在这里装入；ee_extension.h 前置声明本类）// ---------------------------------------------------------------------------
// ExtensionRegistry —— 扩展注册类（扩展函数唯一入口）
//
// 对提交 f25cb92 的修正：复杂度全部由注册类承担，扩展侧只剩简单函数调用。
//
// 简单注册函数（C++ 重载：参数不同 -> 不同重载，函数重载掉）：
//   reg(name)        只登记名字 -> 统一「留痕一次 + 跳过」桩（待补全函数）；
//   reg(name, fn)    登记名字 + 实现（重载：参数个数不同）；
//   regForm(name)    声明「实参是 StrForm」-> 注册类插入 AST。
//
// 注册类内部承担的复杂度：
//   · fail-fast：核心名（isBuiltinFunction）拒绝注册 —— 扩展不得覆盖核心
//     （Xorg: 0-127 核心段保留，防止扩展桩遮蔽真实现）；
//   · first-wins：同名重复注册拒绝（Xorg: opcode 已被占用 -> AddExtension 失败）；
//   · 桩留痕：每个名字只留痕一次（对齐 EE 的容错语义，不报错）；
//   · 插入 AST（注册类可以插入 AST）：
//     regForm() 经 AstBuilder::registerFormArgFunction 注入实参形态声明
//     （ast_builder 零函数名，PUTFORM 专用分支撤出 AST，本类集中声明）；
//     reg() 经 AstBuilder::registerExtensionStatement 注入扩展语句名
//     （解析期不再报「未识别的指令」）。
//   · 扩展系统变量：regVariable() 把 fork 专有 CSV 变量（EE 的 DAYNAME/
//     TIMENAME/MONEYNAME…）登记进运行期扩展变量表（system_variables.h）；
//     regNameTable() 为基础变量补名表映射（DAY -> DAY.CSV）。fail-fast：
//     原生系统变量 / 核心已映射名表拒绝注册（扩展不得覆盖核心）。
//   · 音频管线：regAudioPipelines() 登记扩展要维护的播放管线数量（4 字节无符号；
//     C# 原版没有音频，故整块能力住在扩展）。引擎把数量 + 播放池交给 QML
//     播放维护层 —— C++ 决定「第几条管线播什么」，QML 负责真正播放。
//
// 启用语义：默认全启用 —— 无清单文件（游戏不会默认带 emuera_extensions.txt），
// 没有运行期发现，也没有惰性绑定；EE 扩展在注册类构造时一次登记。
//
// 扩展 = 只用本类的简单函数。EE 扩展单独一个头文件（ee_extension.h），
// **只在注册类里被实现**（本头文件底部装入）—— 其他位置不得放置 EE 头文件：
// 引擎/解析层只看见注册类，扩展名单只存在于注册类这一处。
// 只调 reg() 即完成扩展 —— 这才是扩展函数。
// ---------------------------------------------------------------------------
class ExtensionRegistry;
// 音频播放「管线池」（定义在 GameView/audio_pipeline_pool.h）：核心引擎不持有
// 音频后端，只把它当一个「由扩展登记、由引擎装配」的服务转发给 QML 播放层。
class AudioPipelinePool;

// EE 扩展登记入口（定义在 ee_extension.h；只由注册类构造函数调用）
void registerEeExtensions(ExtensionRegistry& ext);
class ExtensionRegistry {
public:
    // 语句型扩展函数：命中即执行，返回 true 表示已处理（否则调用方落通用路径）
    using StatementFn =
        std::function<bool(const LogicalLine& line, const QList<Operand>& args)>;

    // 式中（表达式内）扩展函数：实参已由求值器求值（省略实参为无效 QVariant，
    // argNodes 与之一一对应）；返回 true 表示已处理（out = 返回值）。
    // 实现住扩展侧（ee_extension.cpp），求值经 ExpressionEvaluator 的
    // 扩展函数回调（BuiltinOp::Extension）转回本注册类。
    using ExprFn = std::function<bool(const QList<QVariant>& args,
                                      const QList<const ExpressionNode*>& argNodes,
                                      QVariant& out)>;

    ExtensionRegistry() {
        // 核心函数的实参形态声明也由注册类承担（复杂度集中注册类）：
        // PUTFORM 的实参是 StrForm（文本 + {…}/%…%）—— 注册类插入 AST，
        // ast_builder 零函数名。
        regForm(QStringLiteral("PUTFORM"));
        // STRLENFORM / STRLENFORMU 的实参也是 FORM_STR（C# STRLEN_Instruction
        // argisform=true -> FORM_STR，见 Instraction.Child.cs:1959）。eraTW 里
        // 以**语句形式**出现：`STRLENFORM %ForagePlaceName(SpotID)%` —— 不声明
        // 实参形态时，`%…%` 无法按表达式归约，整行被跳过（函数语句实参无法归约）。
        regForm(QStringLiteral("STRLENFORM"));
        regForm(QStringLiteral("STRLENFORMU"));
        // EE 扩展默认全启用（EE 头只在注册类里被实现 —— 见顶部 #include）
        registerEeExtensions(*this);
    }

    // ---- 扩展实现所需的服务（复杂度由注册类承担）--------------------------
    // 引擎在构造时填入；扩展实现只经 services() 取用，不直接依赖引擎。
    // 惰性 provider：游戏目录在 setGameDirectory 之后才可知（对齐配置的
    // 惰性 provider 模式）。
    struct Services {
        VariableStorage* storage = nullptr;      // 变量表（RESULT / RESULTS 数组写入）
        std::function<QString()> savDirectory;   // 存档目录（CHKVARDATA/FIND_VARDATA 探测用）
        // EE 式中函数所需（EraEngine 注入；惰性 provider）：
        std::function<int(const QString&, bool)> functionExists;   // EXISTFUNCTION(name, ci)
        std::function<QString()>                 doingFunction;    // GETDOINGFUNCTION()
        std::function<QString(int)>              displayLine;      // GETDISPLAYLINE(lineNo)
        // 表达式求值（扩展命令实参：PLAYBGM 的字符串式 / SET*VOLUME 的整数式）。
        // 惰性：解析表装配后才可用；未注入时扩展退回按字面量处理。
        std::function<QVariant(const QString&)>  evaluate;
    };
    void setServices(Services s) { m_services = std::move(s); }
    // 引擎在运行期补挂「式中函数」服务（构造期 storage 已知，但这些 provider
    // 依赖解析表/控制台，由 EraEngine 在装配完成后注入）。
    void setExpressionServices(std::function<int(const QString&, bool)> functionExists,
                               std::function<QString()> doingFunction,
                               std::function<QString(int)> displayLine) {
        m_services.functionExists = std::move(functionExists);
        m_services.doingFunction = std::move(doingFunction);
        m_services.displayLine = std::move(displayLine);
    }
    [[nodiscard]] const Services& services() const { return m_services; }

    // ---- 简单注册函数（复杂度由注册类承担；扩展只写这几行）----------------

    // ① 只登记名字：统一「留痕跳过」桩（待补全函数；运行期每名留痕一次）
    void reg(const QString& name) {
        const QString upper = failFastChecked(name);
        if (upper.isEmpty()) return;
        m_stubs.insert(upper);
        AstBuilder::registerExtensionStatement(upper);
    }

    // ② 登记名字 + 实现（重载：参数个数不同；同 fail-fast / first-wins）
    void reg(const QString& name, StatementFn fn) {
        const QString upper = failFastChecked(name);
        if (upper.isEmpty()) return;
        m_functions.insert(upper, std::move(fn));
        AstBuilder::registerExtensionStatement(upper);
    }

    // ③ 声明「实参是 StrForm（文本 + {…}/%…%）」：注册类插入 AST ——
    //    AstBuilder 装载期按格式串解析实参、不做表达式归约
    //    （AstBuilder 零函数名，声明由本类注入）。
    void regForm(const QString& name) {
        AstBuilder::registerFormArgFunction(name.toUpper());
    }

    // ④ 登记**式中函数**（表达式内可调用）：注入运行期扩展函数表（声明 = 返回
    //    类型 + 参数个数）并保存实现。求值 opcode 统一 BuiltinOp::Extension，
    //    由 ExpressionEvaluator 的扩展回调转回本注册类的 runExpression。
    //    同时登记为扩展语句名：裸写（覆盖组把 EE 名单当命令逐条跑）不报警告，
    //    运行期按桩静默跳过（式中函数本就应带参数，裸写无意义）。
    void regExpr(const QString& name, OperandType ret, int minArgs, int maxArgs, ExprFn fn) {
        const QString upper = failFastChecked(name);
        if (upper.isEmpty()) return;
        m_exprFunctions.insert(upper, std::move(fn));
        AstBuilder::registerExtensionFunction(upper, ret, minArgs, maxArgs);
        AstBuilder::registerExtensionStatement(upper);
    }

    // ⑤ 登记扩展**系统变量**（fork 专有 CSV 变量：EE 的 DAYNAME/TIMENAME/
    //    MONEYNAME…）。注入运行期扩展变量表（system_variables.h），解析期经
    //    systemVariableTypeDyn 与原生的强类型表合并 —— 原生表 constexpr 不变。
    //    size1D 为该变量默认一维长度（<=0 不指定，交由 VariableSize.csv）。
    //    fail-fast：原生系统变量拒绝注册（扩展不得覆盖核心）；first-wins：同名拒绝。
    void regVariable(const QString& name, OperandType type, int size1D = 0) {
        const QString upper = failFastVariableChecked(name);
        if (upper.isEmpty()) return;
        m_variables.insert(upper);
        sysvar::registerExtensionSystemVariable(upper.toStdString(), type,
                                                std::string_view{}, size1D);
    }

    // ⑥ 为基础变量补**名表映射**（`变量:名字` -> 该变量对应的 CSV 名表，如
    //    DAY -> DAY.CSV）。基础变量本身是原生变量，只是它的名表由扩展补齐。
    //    fail-fast：核心已映射的变量拒绝注册（扩展不得改写核心名表）；
    //    first-wins：同名拒绝。
    void regNameTable(const QString& variableName, const QString& csvFileName) {
        const QString upper = variableName.toUpper();
        if (!ConstantTable::coreCsvForVariable(upper).isEmpty()) {
            qWarning() << "[ext] 拒绝注册名表：" << variableName
                       << "核心已映射（扩展不得改写核心名表）";
            return;
        }
        if (m_nameTables.contains(upper)) {
            qWarning() << "[ext] 拒绝注册名表：" << variableName
                       << "已被其他扩展注册（first-wins）";
            return;
        }
        m_nameTables.insert(upper);
        sysvar::registerExtensionNameTable(upper.toStdString(), csvFileName.toStdString());
    }

    // ---- 音频播放管线（扩展能力：C# 原版没有音频）------------------------
    // ⑦ 登记「音效(SE)管线」数量。核心引擎**不含任何音频后端，也不硬编码这个数**
    //    —— 数量（4 字节无符号 quint32）由扩展声明。对齐 EE 源码
    //    （emuera.em `Instraction.Child.cs`）：`Sound[] sound = new Sound[10]`
    //    即 10 条音效管线，BGM 另占 1 条独立管线（`Sound bgm`）。
    //    引擎据此把数量交给 QML 播放维护层（QML 按此维护这么多条 SE 播放器
    //    + 1 条 BGM 播放器）。没有程序会同时播 2^32-1 条 BGM，故 quint32 足够。
    void regAudioPipelines(quint32 soundPipelines) { m_audioPipelines = soundPipelines; }
    [[nodiscard]] quint32 audioPipelines() const { return m_audioPipelines; }

    // 音频播放池由**引擎**在装配后注入（扩展实现经 audioPool() 取用，
    // 把 PLAYBGM/PLAYSOUND/… 变成对池的控制调用）。
    void setAudioPool(AudioPipelinePool* pool) { m_audioPool = pool; }
    [[nodiscard]] AudioPipelinePool* audioPool() const { return m_audioPool; }

    // ---- 查询（执行引擎使用；查询侧零扩展名）------------------------------

    // 注册表是否认识该语句名（实现 / 桩 / 式中函数裸写）
    [[nodiscard]] bool hasStatement(const QString& upperName) const {
        return m_functions.contains(upperName) || m_stubs.contains(upperName)
               || m_exprFunctions.contains(upperName);
    }

    // 注册表是否登记了该扩展系统变量（名字规范为大写；查询侧零扩展名）
    [[nodiscard]] bool hasVariable(const QString& upperName) const {
        return m_variables.contains(upperName);
    }
    // 注册表是否登记了该变量的名表映射
    [[nodiscard]] bool hasNameTable(const QString& upperVariable) const {
        return m_nameTables.contains(upperVariable);
    }

    // 式中函数求值入口（ExpressionEvaluator 的 BuiltinOp::Extension 回调转来）。
    // 未命中的名字返回 false（调用方按求值失败处理）。名字大小写不敏感。
    bool runExpression(const QString& name, const QList<QVariant>& args,
                       const QList<const ExpressionNode*>& argNodes, QVariant& out) {
        const auto it = m_exprFunctions.constFind(name.toUpper());
        if (it == m_exprFunctions.constEnd()) return false;
        return it.value()(args, argNodes, out);
    }

    // 命中即执行；未命中返回 false（调用方落到通用路径）。
    // 桩的「留痕一次 + 跳过」也由注册类承担（对齐 EE 的容错语义，不报错）。
    bool runStatement(const QString& upperName, const LogicalLine& line) {
        if (const auto it = m_functions.constFind(upperName); it != m_functions.constEnd())
            return it.value()(line, line.arguments);
        if (m_stubs.contains(upperName)) {
            if (!m_traced.contains(upperName)) {
                m_traced.insert(upperName);
                qCDebug(eraTrace) << "[ee-ext]" << upperName
                                  << "在运行期被跳过（扩展桩，待补全）。行:"
                                  << line.position.toString();
            }
            return true;
        }
        // 式中函数裸写（无参数、非表达式用法）：静默跳过（式中函数应带参数）。
        if (m_exprFunctions.contains(upperName)) {
            return true;
        }
        return false;
    }

private:
    // fail-fast + first-wins（Xorg：核心段保留 / opcode 已被占用 -> AddExtension 失败）。
    // 返回规范化大写名；被拒绝返回空串 —— 复杂度由注册类承担，扩展侧无感知。
    [[nodiscard]] QString failFastChecked(const QString& name) const {
        const QString upper = name.toUpper();
        if (isBuiltinFunction(upper.toStdString())) {
            qWarning() << "[ext] 拒绝注册：" << name
                       << "属核心函数（kBuiltinFunctions），扩展不得覆盖";
            return {};
        }
        if (m_functions.contains(upper) || m_stubs.contains(upper)
            || m_exprFunctions.contains(upper)) {
            qWarning() << "[ext] 拒绝注册：" << name << "已被其他扩展注册（first-wins）";
            return {};
        }
        return upper;
    }

    // 扩展系统变量的 fail-fast + first-wins（原生变量段保留，扩展不得覆盖）。
    [[nodiscard]] QString failFastVariableChecked(const QString& name) const {
        const QString upper = name.toUpper();
        if (sysvar::systemVariableType(upper.toStdString()) != OperandType::Unknown) {
            qWarning() << "[ext] 拒绝注册：" << name
                       << "属原生系统变量（system_variables.h），扩展不得覆盖";
            return {};
        }
        if (m_variables.contains(upper)) {
            qWarning() << "[ext] 拒绝注册：" << name << "已被其他扩展注册（first-wins）";
            return {};
        }
        return upper;
    }

    QHash<QString, StatementFn> m_functions;    // 扩展语句实现（名字 -> 执行器）
    QHash<QString, ExprFn>      m_exprFunctions;// 扩展式中函数（名字 -> 求值）
    QSet<QString> m_stubs;                      // 只登记名字的桩
    QSet<QString> m_traced;                     // 留痕去重（每个名字只一次）
    QSet<QString> m_variables;                  // 扩展系统变量（名字，大写）
    QSet<QString> m_nameTables;                 // 扩展名表映射（基础变量名，大写）
    Services m_services;                        // 扩展实现所需的服务（引擎填入）
    quint32 m_audioPipelines = 0;               // 扩展登记的音频管线数（4 字节无符号）
    AudioPipelinePool* m_audioPool = nullptr;   // 音频播放池（引擎装配后注入）
};

// ---------------------------------------------------------------------------
// EE 扩展头在此装入（EE 头只在注册类里被实现）：
// 其他位置不得放置 ee_extension.h —— 引擎/解析层只看见注册类；
// EE 名单（CHKVARDATA/CHKGLOBALDATA/FIND_VARDATA）只存在于注册类这一处，
// 注册类构造时 registerEeExtensions(*this) 一次登记（默认全启用）。
// ---------------------------------------------------------------------------
#include "ee_extension.h"
