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
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <tuple>

// ---------------------------------------------------------------------------
// 装载期/解析期诊断（结构化）。
//
// 依据 test/data/language/调试与错误.md「警告等级与加载期警告」：装载期问题要
// **分级**并**可定位**，而不是只往 stderr 丢一行字符串。
//
// 设计（对齐 resilient parsing 的做法：诊断是解析过程的产物，可被调用方统一处理）：
//   * severity —— Info / Warning / Error（Warning = 已按宽容语义继续，例如未登记
//     的指令按 no-op 跳过）；
//   * effect   —— 该诊断的后果（RejectLine = 该行失效 / ReportOnly = 只报告），
//     与 severity 正交：Error 也可能只报告（见 DiagEffect 说明）；
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

// 诊断的「后果」—— 与 severity 正交。
//   DiagSeverity::Error 同时被用于两种完全不同的处理，消费方无法区分：
//     * RejectLine —— 使该行失效（line.isError = true，运行期不执行该行）；
//     * ReportOnly —— 只报告，该行仍按宽容语义参与运行（例如 checkStaticValues
//                    的可静态判定运行期错误：运行期对这些形态一律宽容）。
// 新增字段放在结构体最后并带默认值，既有逐字段初始化（本文件内的 add 系列）
// 无需改动即可编译；需要表达「使该行失效」的调用点显式写 RejectLine。
enum class DiagEffect : quint8 {
    ReportOnly,
    RejectLine,
};

// 诊断的**生产者切片**：重跑某个收尾步骤时按 origin 整体替换它的切片，
// 于是「跑了 N 次」与「跑了一次」在数据上不可区分（幂等由数据表达，
// 不靠调用方数次数/自己去重）。
//
// 切片身份 = **pass 编号**，不是「谁先跑」：
//   Load          —— 装载期逐行产生的事实（预处理/词法/结构/编码，以及
//                    loadScript 里的 kSharpLine/kDeclError）。这些行在装载时
//                    已经解析完，不会被重跑；切片只增不替换。
//                    例外：**同名脚本被替换装载**时，旧 AST 的那些行级事实
//                    不再成立，用 removeOriginFiles() 精确移除该脚本的条目
//                    （这不是重跑，见该函数说明）。
//   Pass1..Pass7  —— 收尾 pass 表（EraParseTable::m_loadPasses）的每一步：
//                    每次运行拥有**一个完整切片**，
//                    驱动端先把本次产出收进独立 sink，再用
//                    ParseDiagnostics::replaceOrigin 整体换掉上一次的切片
//                    （被 gate 跳过的步骤替换为空，旧切片不残留）。
enum class DiagOrigin : quint8 {
    Load,
    Pass1,
    Pass2,
    Pass3,
    Pass4,
    Pass5,
    Pass6,
    Pass7,
};

[[nodiscard]] inline const char* diagOriginName(DiagOrigin o) noexcept {
    switch (o) {
    case DiagOrigin::Load:  return "load";
    case DiagOrigin::Pass1: return "pass1";
    case DiagOrigin::Pass2: return "pass2";
    case DiagOrigin::Pass3: return "pass3";
    case DiagOrigin::Pass4: return "pass4";
    case DiagOrigin::Pass5: return "pass5";
    case DiagOrigin::Pass6: return "pass6";
    case DiagOrigin::Pass7: return "pass7";
    }
    return "load";
}

[[nodiscard]] inline const char* diagEffectName(DiagEffect e) noexcept {
    switch (e) {
    case DiagEffect::ReportOnly: return "report-only";
    case DiagEffect::RejectLine: return "reject-line";
    }
    return "report-only";
}

// 诊断代码（稳定标识；新增时在此登记，测试据此固定化）
namespace DiagCode {
inline constexpr auto kPreprocess         = "preprocess";          // 预处理（[宏]/行连接…）
inline constexpr auto kUnknownInstruction = "unknown-instruction"; // 未登记的指令名
inline constexpr auto kNumericConversion  = "numeric-conversion"; // 非法或超范围整数字面量
inline constexpr auto kExprParse          = "expr-parse";          // 表达式归约失败
inline constexpr auto kArgCheck           = "arg-check";           // 参数个数/类型校验
inline constexpr auto kSharpLine          = "sharp-line";          // # 行非法位置/缺名
inline constexpr auto kDeclError          = "decl-error";          // 变量声明错误/重复定义
inline constexpr auto kLabelNotFound      = "label-not-found";     // 运行期跳转/调用目标静态可知但不存在
inline constexpr auto kStructure          = "structure";           // 结构/配对错误（IF-ENDIF、REPEAT-REND、SIF 次行…）
inline constexpr auto kNotCalled          = "not-called";          // 定义了但从未被调用的函数（对齐 C# FunctionNotCalledWarning）
inline constexpr auto kStaticValue        = "static-value";        // 可静态判定的运行期错误（除零、数组越界…）
inline constexpr auto kIdentifierScope    = "identifier-scope";    // 只作别的函数私有声明的名字被跨函数引用（对齐 C# IdentifierNotFoundCodeEE）
// 偏离声明编码（**装载期**产生；运行期不做编码转换）：
// 实际按哪个编码读出 != 游戏声明的编码。读文件层不严格绑定声明（保证每个文件
// 都读得进来），但偏离本身要报 —— 见 Content/encoding/text_encoding.h 的
// describeEncodingMismatch()。
inline constexpr auto kEncoding           = "encoding";
}  // namespace DiagCode

struct ParseDiagnostic {
    DiagSeverity severity = DiagSeverity::Warning;
    QString code;
    QString position;   // "文件:行[:列]"（兼容文本视图，可空）
    QString message;
    QString snippet;    // 原文片段，可空

    // ---- 结构化定位：**LSP / MCP 用这份，不要用 position 字符串** ----
    //   LSP 的 Diagnostic.range 要 0 基的 (行, 列) **外加长度**（高亮跨度）；
    //   MCP 工具输出同样只要数字 —— 不该让 agent 去正则拆 "文件:行:列"。
    //   position 字符串保留只为兼容既有文本消费者（dbus 告警计数等）：
    //   它在 Windows 盘符（C:\dir\x.erb）下本来就有二义性，也表达不了跨度。
    //   口径与引擎一致：line/column 从 **1** 起；-1 = 未知（LSP 侧统一减 1）。
    QString file;
    int line = -1;
    int column = -1;
    int length = 0;     // 0 = 点到点（无跨度）

    // 该诊断的后果（见 DiagEffect）：默认「只报告」，向后兼容既有调用点。
    DiagEffect effect = DiagEffect::ReportOnly;

    // 该诊断由哪个生产者产生（见 DiagOrigin）。默认装载期：既有调用点
    // （预处理/词法/写步骤 1/2/3）不改一行就落在 Load 切片里。
    // 只有 ParseDiagnostics::replaceOrigin 会写它 —— 盖章点唯一。
    DiagOrigin origin = DiagOrigin::Load;

    [[nodiscard]] bool hasPosition() const { return line > 0; }

    // 由结构化字段拼出文本位置（position 已给则原样用）
    [[nodiscard]] QString positionText() const {
        if (!position.isEmpty()) return position;
        if (!hasPosition()) return {};
        // **必须用拼接，不能用链式 .arg()**：file 在 Android 上是 content:// URI
        //（脚本名=URI），路径里的 %2F/%3A 百分号转义会被 QString::arg 当占位符吃掉
        //（真机实测：所有 %2F→"<行号>F"、%3A→"<列号>A"，整条位置串被改写成
        //  "…primary1Aeraemu…1904FeraH…"）；桌面路径里若有字面 %2/%3 同样中招。
        // 见 ScriptPosition::toString() 同因。结构化字段（file/line/column）不受影响。
        const QString head = file + QLatin1Char(':') + QString::number(line);
        return column > 0 ? head + QLatin1Char(':') + QString::number(column) : head;
    }

    // 兼容既有文本形态："position: message"（无位置时只有 message）
    [[nodiscard]] QString toString() const {
        const QString p = positionText();
        return p.isEmpty() ? message : (p + QStringLiteral(": ") + message);
    }
};

class ParseDiagnostics {
public:
    // ------------------------------------------------------------------
    // 「本行已被拒绝」的**显式事实**登记表
    //
    // 一份判定：收下一条 effect == RejectLine 的诊断时，add() 顺手把
    // (文件, 行, 消息) 记进 m_rejectFacts —— 这是「该行已有一条 A 级拒绝
    // 诊断、原因文本就是这条」的唯一登记点，与文本视图同源（都在 add 里）。
    //
    // 一个查表：下游生产者若对同一行给出**同一事实的镜像**（典型是
    // validateArguments 的结构告警 "%1 (%2)" —— message 相同、只是多带
    // 原文后缀），先查 hasRejectFact(file, line, message)：命中就说明这条
    // 事实已经由拒绝诊断说过一次，不再重复输出。
    //
    // 键含 message 而不是只含 (文件, 行)：同一行可以有两条**不同**事实
    // （例如左值无效 + 不能赋值 const），只抑制「同一文本」的镜像；后写入的
    // typeError 与前一条拒绝诊断的文本不同时不会命中，因而照常输出。
    // ------------------------------------------------------------------
    [[nodiscard]] bool hasRejectFact(const QString& file, int line, const QString& message) const {
        if (file.isEmpty() || line <= 0 || message.isEmpty()) return false;
        return m_rejectFacts.contains(rejectFactKey(file, line, message));
    }

    void add(const ParseDiagnostic& d) {
        m_items.append(d);
        m_texts.append(d.toString());
        if (d.effect == DiagEffect::RejectLine && !d.file.isEmpty() && d.line > 0
            && !d.message.isEmpty()) {
            m_rejectFacts.insert(rejectFactKey(d.file, d.line, d.message));
        }
    }

    void add(DiagSeverity sev, const QString& code, const QString& position,
             const QString& message, const QString& snippet = {}) {
        // 显式逐字段赋值（不用聚合初始化）：ParseDiagnostic 字段较多，
        // 聚合初始化会触发 -Wmissing-field-initializers（本项目 -Werror）。
        ParseDiagnostic d;
        d.severity = sev;
        d.code = code;
        d.position = position;
        d.message = message;
        d.snippet = snippet;
        add(d);
    }

    // 结构化定位版本（**LSP/MCP 走这条**）：file/line/column 为引擎口径（从 1 起），
    // length = 高亮跨度（0 = 点到点）。position 字符串自动派生，文本消费者不受影响。
    void add(DiagSeverity sev, const QString& code, const QString& file, int line, int column,
             int length, const QString& message, const QString& snippet = {},
             DiagEffect effect = DiagEffect::ReportOnly) {
        ParseDiagnostic d;
        d.severity = sev;
        d.code = code;
        d.file = file;
        d.line = line;
        d.column = column;
        d.length = length;
        d.message = message;
        d.snippet = snippet;
        d.effect = effect;
        // 同时派生文本位置：既有消费者直接读 .position（如 test_parse_diagnostic、
        // dbus 告警计数），不能让它们因为新增结构化字段而拿到空串。
        d.position = d.positionText();
        add(d);
    }

    // 便捷入口：「该诊断使这一行失效」（line.isError = true，运行期不执行）。
    // 与 add() 同形，只是把 effect 固定为 RejectLine —— 调用点读起来即契约。
    void addRejectLine(DiagSeverity sev, const QString& code, const QString& file, int line,
                       int column, int length, const QString& message,
                       const QString& snippet = {}) {
        add(sev, code, file, line, column, length, message, snippet, DiagEffect::RejectLine);
    }

    // 便捷入口：明确「只报告，不使该行失效」（即便默认值就是它，也把契约写在
    // 调用点上）。checkStaticValues 这类「运行期对这些形态一律宽容」的诊断用这条。
    void addReportOnly(DiagSeverity sev, const QString& code, const QString& file, int line,
                       int column, int length, const QString& message,
                       const QString& snippet = {}) {
        add(sev, code, file, line, column, length, message, snippet, DiagEffect::ReportOnly);
    }

    // 收下一条既有文本告警；若形如 "文件:行: 文本" 则把前缀拆进 position。
    void addText(DiagSeverity sev, const QString& code, const QString& text);

    void clear() {
        m_items.clear();
        m_texts.clear();
        m_rejectFacts.clear();
    }

    // 用 items 整体替换 origin 切片：给每条盖上 origin（**唯一盖章点**），
    // 删掉容器里原有的同 origin 条目，再把新条目放回这些条目原来**占据的位置**
    //（没有同 origin 条目时追加到末尾）。逐位替换而不是挪到末尾，是因为
    // sortForOutput 之后同一 pass 的条目可能夹在其它 origin 之间（按 file/line
    // 排序）；只有原位替换才能让「重跑 4→5→4」与「只跑 4→5」产生**相同顺序**
    // 的 all()：首次运行的条数/顺序/文本一字不差，重跑只是换掉自己的切片。
    // texts() 由 m_items 重新派生，all()/texts()/size() 的一一对应关系不变；
    // 排序仍只由收尾末尾的 sortForOutput() 做一次。
    void replaceOrigin(DiagOrigin origin, QList<ParseDiagnostic> items);

    // 从 origin 切片里移除 file ∈ files 的条目。唯一用途：**同名脚本被替换装载**
    // 时，旧 AST 的装载期逐行诊断（Load）必须随旧行一起消失 —— 否则同一行会
    // 同时挂着新旧两版的事实。这不是「重跑」（Load 切片永不重跑），只删被替换
    // 脚本自己的条目；其它脚本的 Load 诊断原位保留。
    // 顺序：其余条目的相对顺序不变（与 replaceOrigin 的「原位」口径一致）。
    void removeOriginFiles(DiagOrigin origin, const QSet<QString>& files);

    // 输出稳定化：收尾各步遍历 m_scripts（QHash，Qt 随机化哈希种子）、且装载
    // 走并行 worker —— 诊断的**产生顺序**在不同进程间不确定（内容与条数不变）。
    // 这里按 (文件, 行, 列, code, message) 稳定排序并重建文本视图，让
    // dump_warnings / LSP / dbus 的输出可复现（基线比对才立得住）。
    // 幂等；只在装载收尾后调用一次，运行期不排序（避免每条诊断都付代价）。
    void sortForOutput() {
        std::stable_sort(m_items.begin(), m_items.end(),
                         [](const ParseDiagnostic& a, const ParseDiagnostic& b) {
                             return std::tie(a.file, a.line, a.column, a.code, a.message)
                                  < std::tie(b.file, b.line, b.column, b.code, b.message);
                         });
        if (m_texts.size() == m_items.size()) {
            // 常规情况：文本视图与结构化条目一一对应，重建以保持两视图一致。
            m_texts.clear();
            m_texts.reserve(m_items.size());
            for (const ParseDiagnostic& d : m_items) m_texts.append(d.toString());
        } else {
            // 兜底（理论上不会发生）：两个视图条数不一致时各自定序。
            m_texts.sort();
        }
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
    // m_rejectFacts 的唯一键形态：文件 \x1f 行 \x1f 消息（\x1f = 单元分隔符，
    // 不会出现在合法路径/消息里；与 load_passes 的去重键同一手法）。
    [[nodiscard]] static QString rejectFactKey(const QString& file, int line,
                                               const QString& message) {
        return file + QLatin1Char('\x1f') + QString::number(line) + QLatin1Char('\x1f')
             + message;
    }

    // 派生数据随 m_items 重建：切片替换 / 按文件移除 / 清空之后，登记表必须与
    // 「当前留存的 RejectLine 诊断」逐条一致，否则抑制判据会读到已删的事实。
    void rebuildRejectFacts() {
        m_rejectFacts.clear();
        for (const ParseDiagnostic& d : m_items) {
            if (d.effect == DiagEffect::RejectLine && !d.file.isEmpty() && d.line > 0
                && !d.message.isEmpty()) {
                m_rejectFacts.insert(rejectFactKey(d.file, d.line, d.message));
            }
        }
    }

    QList<ParseDiagnostic> m_items;
    QStringList m_texts;
    QSet<QString> m_rejectFacts;   // 派生：见 hasRejectFact
};

// 仅供表达式上下文使用；普通 PRINT/字符串原文不得送入数字诊断。
//
// 去重键 = (file, line, column)。reported 是**本次运行**的局部键集合，由调用方
// 持有（一次 pass 运行 / 一个脚本任务）：同一位置只报一次，重跑一次 pass 时
// 集合是新的，于是「本次运行的产出」只由输入决定 —— 不再读共享容器
// （旧实现扫 diagnostics.all()，使 1/2/3 无法改成独立 sink + replaceOrigin，
// 首跑也会因容器里已有条目而少报）。
void diagnoseIntegerLiterals(const QString& expression, const QString& file, int line,
                             int column, ParseDiagnostics& diagnostics,
                             QSet<QString>& reported);

#endif  // ERAENGINE_AST_PARSE_DIAGNOSTIC_H
