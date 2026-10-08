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
#ifndef EPROTO_EXPORT_H
#define EPROTO_EXPORT_H

// eproto 是一个**动态库**（见 src/tools/CMakeLists.txt）：eralsp / eramcp / 测试
// 都通过它共享同一份「协议编解码 + 语义索引」实现，而不是各自静态复制一份。
// 依据 Qt 文档《Creating Shared Libraries》：本库编译时定义 EPROTO_LIBRARY →
// Q_DECL_EXPORT（Windows 上是 __declspec(dllexport)），使用方只看到 Q_DECL_IMPORT。
#include <QtCore/qglobal.h>

#if defined(EPROTO_LIBRARY)
#  define EPROTO_API Q_DECL_EXPORT
#else
#  define EPROTO_API Q_DECL_IMPORT
#endif

#endif  // EPROTO_EXPORT_H
