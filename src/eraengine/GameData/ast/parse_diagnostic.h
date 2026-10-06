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
#ifndef ERAENGINE_AST_PARSE_DIAGNOSTIC_H
#define ERAENGINE_AST_PARSE_DIAGNOSTIC_H

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// ---------------------------------------------------------------------------
// 装载期/解析期诊断（结构化）。
//
// 依据 test/data/language/调试与错误.md「警告等级与加载期警告」：装载期问题要
// **分级**并**可定位**，而不是只往 stderr 丢一行字符串。
//
// 设计（对齐 resilient parsing 的做法：诊断是解析过程的产物，可被调用方统一处理）：
//   * severity —— Info / Warning / Error（Error = 该行不可用；Warning = 已按宽容
//     语义继续，例如未登记的指令按 no-op 跳过）；
//   * code     —— 稳定标识，供「按类统计 / 测试固定化 / 抑制」使用；
//   * position —— "文件:行[:列]"（可空）；
//   * message  —— 人类可读；
//   * snippet  —— 原文片段（可空）。
//
// 兼容：既有代码以「文本告警」为接口（"file:line: msg"），本类内部同时维护该
// 文本视图（texts()），因此可以不破坏既有消费者（dbus loadState 的告警计数等）。
// 文本视图由 add() 唯一入口派生，不构成第二份事实来源。
// ---------------------------------------------------------------------------

enum class DiagSeverity : quint8 {
    Info,
    Warning,
    Error,
};

[[nodiscard]] inline const char* diagSeverityName(DiagSeverity s) noexcept {
    switch (s) {
    case DiagSeverity::Info:    return "info";
    case DiagSeverity::Warning: return "warning";
    case DiagSeverity::Error:   return "error";
    }
    return "warning";
}

// 诊断代码（稳定标识；新增时在此登记，测试据此固定化）
namespace DiagCode {
inline constexpr auto kPreprocess         = "preprocess";          // 预处理（[宏]/行连接…）
inline constexpr auto kUnknownInstruction = "unknown-instruction"; // 未登记的指令名
inline constexpr auto kExprParse          = "expr-parse";          // 表达式归约失败
inline constexpr auto kArgCheck           = "arg-check";           // 参数个数/类型校验
inline constexpr auto kSharpLine          = "sharp-line";          // # 行非法位置/缺名
inline constexpr auto kDeclError          = "decl-error";          // 变量声明错误/重复定义
}  // namespace DiagCode

struct ParseDiagnostic {
    DiagSeverity severity = DiagSeverity::Warning;
    QString code;
    QString position;   // "文件:行[:列]"，可空
    QString message;
    QString snippet;    // 原文片段，可空

    // 兼容既有文本形态："position: message"（无位置时只有 message）
    [[nodiscard]] QString toString() const {
        return position.isEmpty() ? message : (position + QStringLiteral(": ") + message);
    }
};

class ParseDiagnostics {
public:
    void add(const ParseDiagnostic& d) {
        m_items.append(d);
        m_texts.append(d.toString());
    }

    void add(DiagSeverity sev, const QString& code, const QString& position,
             const QString& message, const QString& snippet = {}) {
        add(ParseDiagnostic{sev, code, position, message, snippet});
    }

    // 收下一条既有文本告警；若形如 "文件:行: 文本" 则把前缀拆进 position。
    void addText(DiagSeverity sev, const QString& code, const QString& text);

    void clear() {
        m_items.clear();
        m_texts.clear();
    }

    [[nodiscard]] const QList<ParseDiagnostic>& all() const { return m_items; }
    [[nodiscard]] const QStringList& texts() const { return m_texts; }
    [[nodiscard]] int size() const { return m_items.size(); }
    [[nodiscard]] bool isEmpty() const { return m_items.isEmpty(); }

    [[nodiscard]] int count(DiagSeverity sev) const;
    [[nodiscard]] QMap<QString, int> countByCode() const;

    // 一行摘要："警告 105（unknown-instruction 40 / preprocess 20 / …）"
    [[nodiscard]] QString summarize() const;

private:
    QList<ParseDiagnostic> m_items;
    QStringList m_texts;
};

#endif  // ERAENGINE_AST_PARSE_DIAGNOSTIC_H
