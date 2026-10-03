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
#include <QtGlobal>
#include <functional>

#include "ast/ast_builder.h"     // registerFormArgFunction（注册类可以插入 AST）
#include "ast/function_types.h"  // isBuiltinFunction（核心名 fail-fast）
#include "ast/logical_line.h"    // LogicalLine / Operand
#include "eraengine_log.h"       // eraTrace（桩留痕）
#include "variable_storage.h"    // Services::storage（扩展实现写 RESULT/RESULTS）

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

// EE 扩展登记入口（定义在 ee_extension.h；只由注册类构造函数调用）
void registerEeExtensions(ExtensionRegistry& ext);
class ExtensionRegistry {
public:
    // 语句型扩展函数：命中即执行，返回 true 表示已处理（否则调用方落通用路径）
    using StatementFn =
        std::function<bool(const LogicalLine& line, const QList<Operand>& args)>;

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
    };
    void setServices(Services s) { m_services = std::move(s); }
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

    // ---- 查询（执行引擎使用；查询侧零扩展名）------------------------------

    // 注册表是否认识该语句名（实现或桩）
    [[nodiscard]] bool hasStatement(const QString& upperName) const {
        return m_functions.contains(upperName) || m_stubs.contains(upperName);
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
        if (m_functions.contains(upper) || m_stubs.contains(upper)) {
            qWarning() << "[ext] 拒绝注册：" << name << "已被其他扩展注册（first-wins）";
            return {};
        }
        return upper;
    }

    QHash<QString, StatementFn> m_functions;   // 扩展实现（名字 -> 执行器）
    QSet<QString> m_stubs;                     // 只登记名字的桩
    QSet<QString> m_traced;                    // 留痕去重（每个名字只一次）
    Services m_services;                       // 扩展实现所需的服务（引擎填入）
};

// ---------------------------------------------------------------------------
// EE 扩展头在此装入（EE 头只在注册类里被实现）：
// 其他位置不得放置 ee_extension.h —— 引擎/解析层只看见注册类；
// EE 名单（CHKVARDATA/CHKGLOBALDATA/FIND_VARDATA）只存在于注册类这一处，
// 注册类构造时 registerEeExtensions(*this) 一次登记（默认全启用）。
// ---------------------------------------------------------------------------
#include "ee_extension.h"
