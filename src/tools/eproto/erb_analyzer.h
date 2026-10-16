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
#ifndef EPROTO_ERB_ANALYZER_H
#define EPROTO_ERB_ANALYZER_H

#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "eproto_export.h"

// ---------------------------------------------------------------------------
// ErbAnalyzer —— 语义**只**来自 eraemu 自己的前端（GameData/ast/AstBuilder +
// Content/ErbPreprocessor），本头文件因此**不**包含任何引擎头文件。
//
// 为什么刻意把引擎类型挡在 .cpp 里：引擎的 AST 头（GameData/ast/logical_line.h）
// 直接 `#include <QColor>`，一旦在公共头里出现，任何消费者（编辑器插件、别的
// QML/C++ 应用）都被迫链接 QtGui —— 一个「LSP/MCP 前端」不该拖一整个 GUI 库。
// 这里只对外暴露**派生数据**（诊断/标签/行数），引擎的 LogicalLine 留在实现里。
// ---------------------------------------------------------------------------

namespace eproto {

// LSP 口径的 0 起行列诊断（从引擎的 ParseDiagnostics 派生）
struct EPROTO_API ErbDiagnostic {
    int     line = 0;
    int     startCol = 0;
    int     endCol = 0;
    QString severity;   // "error" | "warning" | "information"
    QString code;       // 引擎 DiagCode（如 unknown-instruction / expr-parse）
    QString message;
    QString snippet;
};

struct EPROTO_API ErbSymbol {
    QString name;
    int     line = 0;                 // 0 起
    bool    isGotoLabel = false;      // $label（GotoLabel）而非 @label
};

// 一份 ERB 文档的解析结果（全部为派生数据，不含引擎类型）。
struct EPROTO_API ErbDocument {
    QString              fileName;             // 解析时用的名字（路径或 uri）
    QString              text;                 // 源文本
    int                  lineCount = 0;        // eraemu AST 的逻辑行数
    QList<ErbDiagnostic> attributed;           // 已归属到物理行的诊断（LSP 口径）
    QList<ErbSymbol>     symbols;              // 从 AST 取的标签
    QStringList          preprocessWarnings;   // ErbPreprocessor 的告警
    bool                 parsed = false;
};

// 一个目录里全部 ERB/ERH 的读盘结果（纯派生数据；键是绝对路径）。
// encodingNotes 是「实际读出的编码 != 声明的编码」的说明文本（见 describeEncodingMismatch）。
struct EPROTO_API WorkspaceSources {
    QMap<QString, QString> sources;
    QMap<QString, QString> encodingNotes;
};

class EPROTO_API ErbAnalyzer {
public:
    ErbAnalyzer() = default;

    // 用 eraemu 自己的前端解析一段 ERB 文本（ErbPreprocessor + AstBuilder）。
    [[nodiscard]] ErbDocument parse(const QString& text, const QString& fileName) const;

    // Analyze an in-memory workspace through EraParseTable (装载收尾走 markLoadComplete)。
    // Keys are document identities (paths or URIs); values are source text.
    // Headers are loaded first, then scripts, with deterministic ordering.
    // No files are read and no script code is executed. Includes cross-file
    // declarations, function binding, argument/type and preprocessing diagnostics.
    // renameMap 是 CSV/_Rename.csv 的 `[[''名前'']] -> 替换文本` 表（对齐装载器的
    // ErbLoader::tryAutoLoadRename）。传空表示不做换字 —— 此时脚本里的 [[…]] 会被
    // 当成字符串字面量，产生大量假诊断；整局上下文（analyzeGameWorkspace）会自动装载。
    [[nodiscard]] QMap<QString, ErbDocument> analyzeWorkspace(
        const QMap<QString, QString>& sources,
        const QHash<QString, QString>& renameMap = {},
        bool notCalledWarning = false) const;

    // 从游戏根目录装载 CSV/_Rename.csv（键是 `[[名前]]` 形态，值=替换文本）。
    // 找不到文件返回空表（不报错 —— 很多游戏没有这个文件）。
    [[nodiscard]] static QHash<QString, QString> readRenameMap(const QString& gameRoot);

    // 游戏配置里的「関数が呼び出されなかった警告の扱い」（对齐 C# FunctionNotCalledWarning）：
    // 只有显式设为 DISPLAY/ONCE 才算开启（C# 的默认值与 eraTW 都是 IGNORE）。
    [[nodiscard]] static bool readFunctionNotCalledWarning(const QString& gameRoot);

    // 文件所在的「游戏 ERB 工作区」目录：向上找最近的名为 ERB 的祖先目录
    // （大小写不敏感 —— 与引擎装载范围 <游戏>/ERB 一致）；找不到返回空。
    // eraTW/eraH/eraMegaten 等真实游戏都把脚本放在 <游戏>/ERB 下。
    [[nodiscard]] static QString detectWorkspaceDir(const QString& filePath);

    // 递归读取目录下全部 *.erb/*.erh（复用引擎 FileSystem::collectTree 的一次性
    // 路径树 —— 与常量表/角色 CSV 装载同一套文件 IO）。declaredEncodingDir 用于
    // 「声明的编码」查找（游戏根目录）；留空表示不判定编码偏差。
    // 读不出的文件被跳过（与 LSP 现行为一致，不静默当成空文件）。
    [[nodiscard]] static WorkspaceSources readWorkspace(const QString& dir,
                                                        const QString& declaredEncodingDir = {});

    // 以「整局游戏」为上下文分析：把 erbDir 下全部脚本一次性交给 analyzeWorkspace
    // （跨文件声明、函数绑定、类型收尾全量生效），返回键为绝对路径的文档集；
    // 编码偏差作为 DiagCode::kEncoding 诊断挂在对应文档上。
    // 结果按目录缓存（签名 = 文件集 + 大小 + mtime），未变化时后续调用零读盘。
    [[nodiscard]] QMap<QString, ErbDocument> analyzeGameWorkspace(const QString& erbDir) const;


    // 工作区/游戏目录「声明的编码」名（读 `emuera.config` / `CSV/_fixed.config` 的
    // `TextEncoding`/`テキストエンコーディング`/`文字コード`，或 `内部で使用する東アジア言語`）。
    // 返回空 = 没声明。刻意返回**编码名字符串**（派生数据），不把引擎的 TextEncoding
    // 类型带进这个头（理由见文件头）。
    // 用途：读文件层不严格绑定 `Config.Encode`（保证每个文件都读得进来），
    // 但「实际按什么编码读出」与「声明的是什么」不符时，要让编辑器看得到（DiagCode::encoding）。
    [[nodiscard]] static QString declaredEncodingName(const QString& dir);

    // ---- 引擎登记表（来自 eraemu 自己的 constexpr 表）----
    [[nodiscard]] static QStringList instructionNames();   // kInstructionSpecs
    [[nodiscard]] static QStringList functionNames();      // kBuiltinFunctions
    [[nodiscard]] static QString instructionSpec(const QString& name);  // 参数形态
    [[nodiscard]] static QString functionSpec(const QString& name);     // 返回类型/参数

private:
    // analyzeGameWorkspace 的整局缓存（工具进程单线程使用，不加锁）。
    struct GameCache {
        QString                    signature;   // 文件集 + size + mtime 摘要
        QMap<QString, ErbDocument> documents;
    };
    mutable QHash<QString, GameCache> m_gameCache;   // erbDir 绝对路径 -> 缓存
};

}  // namespace eproto

#endif  // EPROTO_ERB_ANALYZER_H
