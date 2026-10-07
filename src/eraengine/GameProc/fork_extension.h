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

class ExtensionRegistry;   // 前置声明（完整定义在 extension_registry.h，打破循环）

// ---------------------------------------------------------------------------
// EM / Emuera.NET fork 扩展（.NET 系命令）—— 与 EE 一样，只在注册类里装入。
//
// 覆盖：MAP_* / ENUM* / XML_* / DT_* / DT_COLUMN_OPTIONS（CALLSHARP 保持桩）。
//
// 实现策略（见 thoughts/design/2026-10-07-dotnet-fork-extension-commands.md）：
// **仿照语义自研**，不引入 SQLite / pugixml 等外部依赖：
//   · MAP_*   -> QHash + 自造 <map><p><k>/<v></p></map>（手写序列化）
//   · ENUM*   -> 引擎名字表（服务注入）+ QDirIterator
//   · XML_*   -> 小 DOM（QXmlStreamReader/Writer，Qt6::Core）+ 自研 XPath 子集
//   · DT_*    -> 内存类型化表（id 主键 / DBNull / 过滤排序子集 / .NET 形状 XML）
//   · CALLSHARP -> **不实现，保持桩**（qtcpp 跨平台 + 目标是 C# 托管 DLL，见 fork_extension.cpp）
//
// 语义依据：emuera.em 的 Creator.cs（methodList）与 Creator.Method.cs（实现），
// 以及本仓库 test/data/functions/*.md（逐条语义文档）。
// ---------------------------------------------------------------------------

// EM/Emuera.NET fork 扩展登记入口（定义在 fork_extension.cpp；只由注册类构造函数调用）
void registerForkExtensions(ExtensionRegistry& ext);
