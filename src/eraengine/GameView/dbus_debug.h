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

#include <QObject>
#include <QString>

class EraEngine;

// ---------------------------------------------------------------------------
// EraDBusDebug —— 运行中的 GUI（appemuera）的 D-Bus 检查/控制入口
//
// 服务名 io.yigekuyou.emuera（main.cpp 的单实例注册），对象路径 /debug，
// 接口名 io.yigekuyou.emuera.Debug（Q_CLASSINFO 导出）。EraEngine 构造时
// 自动注册；无会话总线（test_cli / CI）时静默跳过，不影响功能。
//
// 用法（另一个终端）：
//   qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.state
//   qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.perf
//   busctl --user call io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug consoleStats ""
//   busctl --user call io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug sendInput x 400
//       （像键入数字一样喂输入，等价点击/键盘）
//   busctl --user call io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug dumpScreen i 40
//       （抓最近 40 行控制台文本）
// ---------------------------------------------------------------------------
class EraDBusDebug : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.yigekuyou.emuera.Debug")

public:
    explicit EraDBusDebug(EraEngine* engine, QObject* parent = nullptr);

    // 注册到会话总线；总线不可用/路径被占用时返回 false（静默降级）
    bool registerOnBus();

public Q_SLOTS:  // ---- 检查 ----
    QString ping();                       // 存活探测 -> "pong <epoch-ms>"
    QString state();                      // 状态机/执行状态/等待输入/当前脚本行
    QString consoleStats();               // 行数/逻辑行/可见窗口/滚动
    QString perf();                       // 区块构建计数与耗时（QML 卡顿定位）
    void resetPerf();                     // 清零性能计数器
    QString dumpScreen(int lastLines);    // 抓控制台尾部文本
    QString listButtons();                // 当前屏的按钮值（输入候选）
public Q_SLOTS:  // ---- 控制 ----
    void sendInput(qint64 value);         // 等价键入整数（INPUT/TINPUT…）
    void sendInputString(const QString& text);
    void sendAnyKey();                    // 等价回车/点击继续（WAIT 系）
    void scroll(int lines);               // 正=向上，负=向下
    void scrollToBottom();
    void setLoggingRules(const QString& rules);  // 运行期调 QLoggingCategory

private:
    EraEngine* m_engine = nullptr;
};
