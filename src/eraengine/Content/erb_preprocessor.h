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
#ifndef ERB_PREPROCESSOR_H
#define ERB_PREPROCESSOR_H

#include <QString>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QList>

// ---------------------------------------------------------------------------
// 一行源文件（与文件物理行一一对应，保证行号可控）
//   text 为空  —— 该物理行不产生逻辑行（注释/空行/被预处理禁用/行连接被吞并）
// ---------------------------------------------------------------------------
struct ErbSourceLine {
    int physicalLine = 0;   // 1-based
    QString text;           // 已应用 rename 与行连接
};

// ---------------------------------------------------------------------------
// 一趟预处理的**产物**：全部行文本 + 每行的跨度（不是每行一个 QString）
//
// 为什么是「一份文本 + 跨度表」而不是 QList<ErbSourceLine>：
//   这份产物要**常驻到构建期**——同一份文本只预处理一趟（ErbPreprocessor::process
//   每文件 2 -> 1，见 ErbLoader::scanFileDeclarations），所以它的形状直接决定
//   装载峰值。
//
//   实测锚点（eraTW，2260 个文件）：物理行 2,111,136 条，其中非空 2,005,446 条，
//   预处理后正文 49,559,234 字符，而它取代的「全库源文本」52,456,457 字符。
//
//   · 每行一个 QString（QList<ErbSourceLine>）常驻的账：
//       32 B/行（行号 + QString 指针 + 对齐）        67.6 MB
//       24 B QString 头 + ~16 B 分配器开销 × 非空行   80.2 MB
//       2 B/字符（UTF-16）× 正文                    99.1 MB
//       ------------------------------------------------  ≈ 247 MB
//     → 比它取代的全库源文本（2 × 52.5M 字符 ≈ 105 MB）多 ~142 MB，
//       即 eraTW 装载峰值（3.48 GB）的 +4.1%，超过本刀的 2% 判据。
//   · 「正文首尾相接一份 + 每行 12 B (offset,length)」：
//       12 B/行 ≈ 25.3 MB + 正文 99.1 MB ≈ 124 MB，只比源文本多 ~19 MB。
//     实测峰值不升反降（eraTW 3.48 → 3.43 GB，-1.5%；eraMegaten 11.12 → 10.98 GB，
//     -1.2%）：注释/空行/CR 在预处理后不再占地方（52.5M 字符 -> 49.6M 字符）。
//
//   于是行文本全库只有**一份**，且与物理行号一一对应。
//
// 契约与 ErbSourceLine 相同：每个物理行恰好一条；length == 0 表示该行不产生
// 逻辑行（注释/空行/被预处理禁用/行连接被吞并）。
// ---------------------------------------------------------------------------
struct ErbLineSpan {
    // qint32：单文件物理行数与正文长度都在 2^31 以内（单个 ERB 达 2 GiB 才会溢出；
    // 现实中的脚本文件是几十 KB 量级）。
    qint32 physicalLine = 0;   // 1-based 物理行号
    qint32 offset = 0;         // 在 ErbPreprocessed::text 里的起点（UTF-16 单元）
    qint32 length = 0;         // 0 = 该物理行不产生逻辑行
};

class ErbPreprocessed {
public:
    [[nodiscard]] int size() const { return spans_.size(); }
    [[nodiscard]] bool isEmpty() const { return spans_.isEmpty(); }
    [[nodiscard]] int physicalLineAt(int i) const { return spans_.at(i).physicalLine; }
    // 一行的文本（从唯一那份 text_ 里切出；逐行临时拷贝，不常驻）
    [[nodiscard]] QString textAt(int i) const {
        const ErbLineSpan& s = spans_.at(i);
        return s.length > 0 ? text_.mid(s.offset, s.length) : QString();
    }

    // 需要「每行一个 QString」的消费者（单测 / 工具 / 一次性投影）用它：
    // 这是一份**投影**，不是第二份事实 —— 常驻形态始终是 text_ + spans_。
    [[nodiscard]] QList<ErbSourceLine> materialize() const;

    // 构建（= 预处理）用：追加一行（length == 0 的行不占文本空间）
    void appendLine(int physicalLine, const QString& text);
    void reserve(int lineCount, int charCount);
    // 交还预留多出来的容量（这份产物常驻到构建期，容量翻倍就是白占内存）
    void squeeze();

private:
    QString text_;                // 全部非空行文本首尾相接（唯一一份）
    QVector<ErbLineSpan> spans_;  // 每个物理行一条
};

// ---------------------------------------------------------------------------
// ErbPreprocessor
//
// 对齐 C# Emuera 的两个前端步骤：
//   · Sub/EraStreamReader.ReadEnabledLine —— _Rename.csv 替换 + '{'…'}' 行连接
//   · GameProc/ErbLoader.PPState         —— [SKIPSTART]/[SKIPEND]/
//                                           [IF]/[ELSEIF]/[ELSE]/[ENDIF]/
//                                           [IF_DEBUG]/[IF_NDEBUG] 宏开关
//
// 行连接语义（C#）：整行只有 '{' 时开始连接，直到整行只有 '}' 为止；
// 期间每行以单个空格连接，逻辑行取 '{' 所在物理行的行号。
// 空行与纯空白行在连接之外会被跳过（不产生逻辑行）。
// ---------------------------------------------------------------------------
class ErbPreprocessor {
public:
    using RenameMap = QHash<QString, QString>;   // "[[''X''']]" -> 替换文本

    // 宏定义：对象宏 name -> 替换文本；带参宏 name -> (形参列表, 替换模板)
    struct MacroDef {
        QString body;              // 替换文本
        QStringList params;        // 带参宏形参；空 = 对象宏
    };
    using MacroTable = QHash<QString, MacroDef>;

    void setRenameMap(const RenameMap& map) { m_rename = map; }
    [[nodiscard]] const RenameMap& renameMap() const { return m_rename; }

    // 已定义的宏名（来自 .ERH 的 #DEFINE），供 [IF name] / [ELSEIF name] 判定
    void setMacros(const QSet<QString>& macros) { m_macros = macros; }
    [[nodiscard]] const QSet<QString>& macros() const { return m_macros; }

    // 宏定义表（对象宏 + 带参宏），供行文本展开
    void setMacroTable(const MacroTable& table) { m_macroTable = table; }
    [[nodiscard]] const MacroTable& macroTable() const { return m_macroTable; }

    void setDebugMode(bool on) { m_debugMode = on; }
    [[nodiscard]] bool debugMode() const { return m_debugMode; }

    // 处理整个文件内容。warnings 追加「fileName:行: 文本」形式的告警。
    // 返回值是这一趟的**产物**（行 + 跨度）：同一份文本调用方只调用它一次，
    // 扫描/构建读的都是这一份（见 ErbPreprocessed 的说明）。
    [[nodiscard]] ErbPreprocessed process(const QString& content,
                                         QStringList* warnings,
                                         const QString& fileName) const;

    // 解析 _Rename.csv 内容（对齐 C# ParserMediator.LoadEraExRenameFile）
    static RenameMap parseRenameCsv(const QString& content);

    // 从（头）文件内容收集 #DEFINE 宏名（对齐 C# HeaderFileLoader.analyzeSharpDefine）
    static QSet<QString> collectDefines(const QString& content);

    // 收集完整宏定义（#DEFINE name body / #DEFINE name(a,b) body）
    static MacroTable collectMacroTable(const QString& content);

    // ---- 宏展开的中止条件（fuel；触发时停止展开、按原文保留该行并告警）----
    // 主防线：单行**展开次数**上限 —— 对齐 C# `MAX_EXPAND_MACRO = 100`
    // （LexicalAnalyzer.cs:68,1032-1035，超过即报「疑似自我引用/循环引用宏」）。
    static constexpr int kMaxExpandMacro = 100;
    // 轮数上限（既有）
    static constexpr int kMaxMacroPasses = 32;
    // 安全网：单行展开**体积**上限 —— 自引用宏（`#DEFINE A A A`）每轮翻倍，
    // 仅限次数也可能先撑爆内存，故再加一道体积闸（1 MiB/行）。
    static constexpr int kMaxExpandedLineLength = 1 << 20;

    // 中止原因（展开触顶时经出参回传，由 process() 转成告警）
    enum class MacroAbort { None, Count, Size };

    // 对一行源文本应用宏替换（标识符边界匹配、跳过字符串字面量、循环展开至不动点）。
    // 防自引用膨胀：次数上限 kMaxExpandMacro（主）+ 体积上限 kMaxExpandedLineLength（安全网）。
    // abort 为**出参**（不用成员）：平行装载时同一个 ErbPreprocessor 被多个 worker 线程共用，
    // 成员变量会被争用（实测把「次数触顶」写成了「体积触顶」）。
    QString expandMacros(const QString& line, MacroAbort* abort = nullptr) const;

private:
    RenameMap m_rename;
    QSet<QString> m_macros;
    MacroTable m_macroTable;
    bool m_debugMode = false;
};

#endif // ERB_PREPROCESSOR_H
