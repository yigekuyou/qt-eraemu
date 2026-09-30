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
#include "eraengine_log.h"

// 默认级别 QtWarningMsg：debug 级消息默认不输出，需要时用
//   QT_LOGGING_RULES="era.trace.debug=true"
// 打开。这样高频跟踪不会淹没默认的调试输出。
Q_LOGGING_CATEGORY(eraTrace, "era.trace", QtWarningMsg)
