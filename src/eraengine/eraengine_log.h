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
// ---------------------------------------------------------------------------
// 日志类别
//
// 引擎的调试输出分两档：
//   * 生命周期/状态类（装载、状态迁移、函数调用、输入交付、错误）—— 直接用
//     qDebug/qWarning，量小、默认就该看到；
//   * 高频跟踪类（每次变量写入、每次图片请求、逐行执行）—— 用 qCDebug(eraTrace)。
//     它们按语句量级产生输出（eraTW 跑一屏可达数十万行），因此**默认关闭**，
//     需要时用规则开启：
//
//         QT_LOGGING_RULES="era.trace.debug=true"      # 打开高频跟踪
//         QT_LOGGING_RULES="era.*.debug=false"         # 关掉引擎的全部 debug
//
// 发布构建再配合 QT_NO_DEBUG_OUTPUT 把 qDebug 整体裁掉（qWarning 保留）。
// ---------------------------------------------------------------------------
#ifndef ERAENGINE_LOG_H
#define ERAENGINE_LOG_H

#include <QLoggingCategory>

// 高频跟踪（默认关闭 debug 级）
Q_DECLARE_LOGGING_CATEGORY(eraTrace)

#endif // ERAENGINE_LOG_H
