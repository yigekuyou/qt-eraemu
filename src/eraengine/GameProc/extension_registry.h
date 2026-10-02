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

class ExecutionEngine;

// ---------------------------------------------------------------------------
// 扩展注册表（Wayland 式声明外置 + Xorg 式 fail-fast）
//
// 核心引擎（execution_engine.cpp）零扩展名 —— EE 专属名单只在本模块：
//   · kEeDefaultExtensions：编译期默认名单（EE 存档系：原版 Creator.cs 注释、
//     EE 发行版才激活的保存文件系函数）；
//   · 运行期清单发现：游戏目录放 emuera_extensions.txt（每行一个名字，'#' 注释；
//     与 test/data/emuera_ee_cmds.txt 同格式 —— 由 test/export_command_tables.py
//     从 C# 权威源码导出的声明数据，wayland-scanner 式）。
//
// 绑定语义（wl_registry.bind 式）：引擎首次执行指令时绑定一次；
// 名单里每个名字登记统一「留痕跳过」桩。PUTFORM/FIND_CHARADATA 等
// 核心已有的名字由 ExecutionEngine::registerStatementFunction 的
// fail-fast 拒绝（Xorg: 0-127 核心段保留，扩展不得覆盖核心）。
// 后续扩展：① 往 kEeDefaultExtensions 加名字；② 放清单文件 —— 均不动核心代码。
// ---------------------------------------------------------------------------
void registerEngineExtensions(ExecutionEngine& engine, const QString& gameDirectory);
