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

class ExtensionRegistry;   // 前置声明（完整定义在 extension_registry.h，打破循环）

// ---------------------------------------------------------------------------
// EE 扩展（单独一个头文件 = 扩展函数）
//
// **只在注册类里被实现**（extension_registry.h 顶部装入）—— 其他位置不得
// 放置本头文件：引擎/解析层只看见注册类，扩展名单只存在于注册类一处。
//
// 实现全部在 ee_extension.h/cpp，**只调 ExtensionRegistry 的简单函数**
// （reg / reg(name, 实现) / regForm / services()）——复杂度（fail-fast /
// first-wins / 留痕跳过桩 / 服务桥接）全部由注册类承担。注册类构造时
// registerEeExtensions() 一次登记，默认全启用（无清单文件）。
//
// 名单（test/data/emuera_ee_cmds.txt 已转移到扩展）：
//   · EE 存档系：CHKVARDATA / CHKGLOBALDATA / FIND_VARDATA —— **真实现**
//     （ee_extension.cpp，对齐 C# CheckdataStrMethod / CheckdataMethod /
//     FindFilesMethod）；
//   · EE 扩展命令：test/data/emuera_ee_cmds.txt（C# 权威源码导出）——
//     「留痕一次 + 跳过」桩（待补全，不报错，对齐 EE 的容错语义）。
// PUTFORM / FIND_CHARADATA 属核心（BuiltInFunctionCode.cs 枚举内），
// 不在此登记 —— 扩展不得覆盖核心（注册类 fail-fast 拒绝）。
//
// 后续扩展：往 ee_extension.cpp 加 reg("名字") 或 reg("名字", 实现) ——
// 不动核心代码。
// ---------------------------------------------------------------------------

// EE 扩展登记入口（定义在 ee_extension.cpp；只由注册类构造函数调用）
void registerEeExtensions(ExtensionRegistry& ext);
