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
#ifndef ERAENGINE_CONFIG_I18N_H
#define ERAENGINE_CONFIG_I18N_H

#include <QCoreApplication>
#include <QString>

// ---------------------------------------------------------------------------
// 装载期/解析期告警的 i18n 入口。
//
// 依据 Qt 文档《QTranslator》《Internationalization with Qt》：
//   * 源文本（source text）= 简体中文，直接写在代码里，**不做语言分支**；
//   * 译文由 .ts/.qm 提供，运行期用 QTranslator 安装到 QCoreApplication；
//   * 未安装 .qm 时 QCoreApplication::translate() **原样返回源文本**，所以
//     「没有译文」与「不翻译」行为一致 —— 测试里按中文子串断言不受影响。
//
// 调用约定（重要）：告警文本一律写成**直接的** translate 调用，且上下文写字面量：
//
//     QCoreApplication::translate("ParseDiagnostics", "未定义的函数 %1").arg(name);
//
//   而不是把字面量交给自写包装函数（如 diagTr("…")）：lupdate 只认 translate /
//   tr / QT_TRANSLATE_NOOP 这些**已知函数名**，自定义包装不会被提取进 .ts。
//   同理，上下文必须是**字面量**——lupdate 从字面量取 <context>，写常量取不到。
// ---------------------------------------------------------------------------
namespace eraengine {

// 诊断（装载/解析告警）共用的翻译上下文；同时也是 .ts 里 <context><name> 的值。
// 仅供文档/测试引用：各调用点必须**直接写字面量** "ParseDiagnostics"（原因见上）。
inline constexpr const char* kDiagContext = "ParseDiagnostics";

// 安装翻译器（Qt 自带 + 本体）。语言取 EMUERA_LANG 环境变量，缺省用系统区域
// （QLocale::system().name()，形如 zh_CN / ja / en）。
//
// 查找位置：
//   * Qt 自带     —— qtbase_<lang>.qm，位于 QLibraryInfo::TranslationsPath；
//   * 本体译文    —— emuera_<lang>.qm，位于 :/i18n 资源、<exe>/translations、<exe>。
// 找不到 .qm 属正常（源文本即中文），返回已安装的翻译器数量；qApp 为空时返回 0。
// 幂等：进程内只真正安装一次。
int installTranslators();

}  // namespace eraengine

#endif  // ERAENGINE_CONFIG_I18N_H
