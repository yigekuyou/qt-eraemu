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

#include <QString>
#include <QStringList>

#include "extension_registry.h"

// ---------------------------------------------------------------------------
// EE 扩展（单独一个头文件 = 扩展函数）
//
// **只在注册类里被实现**（extension_registry.h 底部装入）—— 其他位置不得
// 放置本头文件：引擎/解析层只看见注册类，扩展名单只存在于注册类一处。
//
// 扩展只使用 ExtensionRegistry 的简单函数（reg）——复杂度（fail-fast /
// first-wins / 留痕跳过桩）全部由注册类承担。注册类构造时
// registerEeExtensions() 一次登记，默认全启用（无清单文件：游戏不会默认带
// emuera_extensions.txt）。
//
// 名单（test/data/emuera_ee_cmds.txt 已转移到扩展 —— 本头文件就是它的家）：
//   · EE 存档系：C# Creator.cs 里被注释、EE 注册的函数
//     （CHKVARDATA / CHKGLOBALDATA / FIND_VARDATA）；
//   · EE 扩展命令：test/data/emuera_ee_cmds.txt（C# 权威源码导出，
//     eraTW/…/ERB_EXCOM.khp（EM+EE 发行版命令关键字帮助）− 原版命令）。
//   · 全部以「留痕一次 + 跳过」桩登记（待补全，不报错，对齐 EE 的容错
//     语义）；补全某个函数时把它从名单挪出，改用 reg(名字, 实现)。
// PUTFORM / FIND_CHARADATA 属核心（BuiltInFunctionCode.cs 枚举内 +
// BuiltinOp 真实现），不在此登记 —— 扩展不得覆盖核心（注册类 fail-fast
// 拒绝）；PUTFORM 的 StrForm 实参形态由注册类 regForm 声明（插入 AST）。
//
// 后续扩展：往本头文件加一行 reg("名字")（或 reg("名字", 实现)）——
// 不动核心代码。
// ---------------------------------------------------------------------------
inline void registerEeExtensions(ExtensionRegistry& ext)
{
    // ---- EE 存档系（C# Creator.cs 里被注释、EE 注册）----
    ext.reg(QStringLiteral("CHKVARDATA"));
    ext.reg(QStringLiteral("CHKGLOBALDATA"));
    ext.reg(QStringLiteral("FIND_VARDATA"));

    // ---- EE 扩展命令（源: test/data/emuera_ee_cmds.txt，C# 权威源码导出）----
    static const QStringList kEeCommands = {
        QStringLiteral("BINPUT"),
        QStringLiteral("BINPUTS"),
        QStringLiteral("CLEARMEMORY"),
        QStringLiteral("COLUMNBGCOLOR"),
        QStringLiteral("COLUMNCLEAR"),
        QStringLiteral("COLUMNCOLOR"),
        QStringLiteral("COLUMNCREATE"),
        QStringLiteral("COLUMNDIRECTION"),
        QStringLiteral("COLUMNMOVE"),
        QStringLiteral("COLUMNPRINT"),
        QStringLiteral("COLUMNPRINTL"),
        QStringLiteral("COLUMNPRINTW"),
        QStringLiteral("COLUMNRESIZE"),
        QStringLiteral("COLUMNWAIT"),
        QStringLiteral("EXISTFUNCTION"),
        QStringLiteral("EXISTSOUND"),
        QStringLiteral("FLOWINPUT"),
        QStringLiteral("FORCE_BEGIN"),
        QStringLiteral("FORCE_QUIT"),
        QStringLiteral("FORCE_QUIT_AND_RESTART"),
        QStringLiteral("FSTRJOIN"),
        QStringLiteral("FTOOLTIP_SETDURATION"),
        QStringLiteral("GDASHSTYLE"),
        QStringLiteral("GDRAWGWITHROTATE"),
        QStringLiteral("GDRAWLINE"),
        QStringLiteral("GDRAWTEXT"),
        QStringLiteral("GETDISPLAYLINE"),
        QStringLiteral("GETDOINGFUNCTION"),
        QStringLiteral("GETMEMORYUSAGE"),
        QStringLiteral("GETTEXTBOX"),
        QStringLiteral("GETTEXTSIZE"),
        QStringLiteral("GGETFONT"),
        QStringLiteral("GGETFONTSIZE"),
        QStringLiteral("GGETPEN"),
        QStringLiteral("GGETPENWIDTH"),
        QStringLiteral("GGETTEXTSIZE"),
        QStringLiteral("INPUTANY"),
        QStringLiteral("LCSVISASSI"),
        QStringLiteral("OCLEARLINE"),
        QStringLiteral("PLAYBGM"),
        QStringLiteral("PLAYSOUND"),
        QStringLiteral("QUIT_AND_RESTART"),
        QStringLiteral("SETBGMVOLUME"),
        QStringLiteral("SETSOUNDVOLUME"),
        QStringLiteral("SETTEXTBOX"),
        QStringLiteral("SKIPLOG"),
        QStringLiteral("SPRITEDISPOSEALL"),
        QStringLiteral("STOPBGM"),
        QStringLiteral("STOPSOUND"),
        QStringLiteral("STRJOIN1"),
        QStringLiteral("TINPUTAWAIT"),
        QStringLiteral("TOOLTIP_EXTENSION"),
        QStringLiteral("TOOLTIP_IMG"),
        QStringLiteral("TRYCALLF"),
        QStringLiteral("TRYCALLFORMF"),
        QStringLiteral("UPDATECHECK"),
    };
    for (const QString& name : kEeCommands) {
        ext.reg(name);
    }
}
