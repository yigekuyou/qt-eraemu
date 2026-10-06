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

// ---------------------------------------------------------------------------
// 一行源文件（与文件物理行一一对应，保证行号可控）
//   text 为空  —— 该物理行不产生逻辑行（注释/空行/被预处理禁用/行连接被吞并）
// ---------------------------------------------------------------------------
struct ErbSourceLine {
    int physicalLine = 0;   // 1-based
    QString text;           // 已应用 rename 与行连接
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
    [[nodiscard]] QList<ErbSourceLine> process(const QString& content,
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
