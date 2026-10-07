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
#ifndef CONSOLE_BACKEND_H
#define CONSOLE_BACKEND_H

#include <QObject>
#include <QTimer>
#include <QColor>
#include <QVariantMap>
#include <QVariantList>
#include <QAbstractListModel>
#include <QHash>
#include <QtQml/qqmlregistration.h>
#include "console_types.h"
#include "GameData/ast/logical_line.h"
#include "console_buffer.h"
#include "console_layout.h"

// ---------------------------------------------------------------------------
// ConsoleBackend —— 显示层 C++ 后端，**同时就是行模型**（QAbstractListModel）
//
// 职责边界（对齐 Qt 文档的 Model/View 分工）：
//   * C++ 只负责「规划如何绘制」：打印族维护行缓冲、ConsoleLayout 测量
//     区块网格坐标/尺寸，data() 按需摊平某一行（带按行缓存）；
//   * 缓冲的每次变更**直接翻译成模型区间信号**（QAbstractItemModel 文档）：
//       newline()      -> 尾行摘除 + 新行 insertRows
//       REUSELASTLINE  -> 一時行替换 = 尾部 removeRows + insertRows
//       CLEARLINE      -> 尾部 removeRows
//       容量裁剪       -> 头部 removeRows（批量化）
//       改字号/行高    -> dataChanged(全区间)（行数不变）
//       CLEARTEXT      -> modelReset
//   * 「哪些行需要存在、什么时候创建/复用委托、滚到哪」全部交给 QML 的
//     ListView（虚拟化 + reuseItems + cacheBuffer）—— C++ 不再持有
//     窗口/滚动偏移/增量 diff 的任何簿记。
//
// 模型行 = 缓冲区的一个**显示行**（不是区块）：行高恒为一格（图片/立絵
// 以区块身份溢出绘制，不把后续行往下推 —— 控制台是网格叠加语义）；
// 尾部的「未定型行」（print 后未 newline）是最后一行，内容随打印增长。
//
// 按行摊平缓存以 **行 serial**（ConsoleBuffer 发号，单调递增）为键：
// 缓冲下标会被头部裁剪/CLEARLINE 平移，serial 不会 —— 缓存只在
// [firstSerial, lastSerial] 区间外清理孤儿条目，永不整表作废。
// ---------------------------------------------------------------------------
class ConsoleBackend : public QAbstractListModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(ConsoleBackend)
    QML_UNCREATABLE("ConsoleBackend 由 EraEngine 提供")

    Q_PROPERTY(int  lineCount        READ lineCount                            NOTIFY lineCountChanged)
    // 逻辑行数 = LINECOUNT（只增不减，除 CLEARLINE；C# MaxLog 裁剪不回退它）
    Q_PROPERTY(int  logicalLineCount READ logicalLineCount                     NOTIFY lineCountChanged)
    // 缓冲里**最长**的区块能往上探出几行（立絵跨行 + 图片 ypos 负偏移）。
    // QML 用它设定 ListView 的 cacheBuffer，跨行图在锚点行滚出视口后仍保持可见。
    Q_PROPERTY(int  maxSpanReach     READ maxSpanReach                         NOTIFY lineCountChanged)
    Q_PROPERTY(int  frameMs          READ frameMs          WRITE setFrameMs    NOTIFY frameMsChanged)
    Q_PROPERTY(QString inputKind     READ inputKind                            NOTIFY inputRequested)
    Q_PROPERTY(bool waitingInput     READ waitingInput                         NOTIFY waitingInputChanged)
    Q_PROPERTY(quint64 generation    READ generation                           NOTIFY generationChanged)
    // 逻辑网格列/行数：**不是**常量。装载配置后会按「窗口宽 ÷ 单元格宽」重算
    // （eraTW 是 175 列，代码默认只有 80）。若声明为 CONSTANT，QML 会一直用旧值
    // 换算像素格子，而 C++ 给出的区块坐标已按新网格计算，两者比例不一致
    // ——表现就是「内容比窗口宽/高出一倍，只能看见一半」。
    Q_PROPERTY(int gridColumns READ gridColumns NOTIFY lineCountChanged)
    Q_PROPERTY(int gridRows READ gridRows NOTIFY lineCountChanged)
    // 本体即行模型（QML ListView 直接用；CONSTANT：指针生命周期 = 后端本身）
    Q_PROPERTY(QAbstractListModel* lineModel READ lineModel CONSTANT)

public:
    enum Roles {
        BlocksRole = Qt::UserRole + 1,   // QVariantList：该行的最小单位区块列表
        LineIndexRole,                   // int：行号（= 模型行 = 缓冲下标）
        SpanRowsRole,                    // int：该行最高的区块行距（立絵 = 跨行数）
    };

    explicit ConsoleBackend(QObject* parent = nullptr);

    // ---- QAbstractListModel ----
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] QAbstractListModel* lineModel() { return this; }

    // ---- 只读属性 ----
    [[nodiscard]] int  lineCount() const { return m_buffer.count(); }
    [[nodiscard]] int  logicalLineCount() const { return m_buffer.logicalLineCount(); }
    [[nodiscard]] int  maxSpanReach() const { return m_maxSpanReach; }
    [[nodiscard]] int  frameMs() const { return m_frameMs; }
    [[nodiscard]] QString inputKind() const { return m_inputKind; }
    [[nodiscard]] bool waitingInput() const { return m_waitingInput; }
    [[nodiscard]] quint64 generation() const { return m_generation; }
    [[nodiscard]] int gridColumns() const { return m_layout.gridColumns(); }
    [[nodiscard]] int gridRows() const { return m_layout.gridRows(); }
    [[nodiscard]] int lineHeight() const { return m_lineHeight; }
    [[nodiscard]] int fontSize() const { return m_layout.fontSize(); }
    // 平面重建用的「一列」宽度（半角字符宽）＝ fontPx / 2
    [[nodiscard]] int columnWidth() const { return qMax(1, m_layout.fontSize() / 2); }
    // 是否由 C++ 折行。默认 false：**容器自由渲染位置**，C++ 只给区块与位置数据
    void setWrappingEnabled(bool on) { m_wrapLines = on; }
    [[nodiscard]] bool wrappingEnabled() const { return m_wrapLines; }

    // ---- 显示写入接口（对齐 C# EmueraConsole.Print）----
    Q_INVOKABLE void print(const QString& text);                  // 追加文本（不换行）
    Q_INVOKABLE void printPlain(const QString& text);             // 整段不可点击（PRINTPLAIN）
    Q_INVOKABLE void printHtml(const QString& html);              // HTML_PRINT 常用标签子集
    Q_INVOKABLE void newline();                                   // 结束当前行（flush）
    Q_INVOKABLE void printButton(const QString& text, qint64 value, const QString& tooltip = QString());
    Q_INVOKABLE void printButtonStr(const QString& text, const QString& value, const QString& tooltip = QString());
    // 内联图（C# PrintImg / <img>）：只给资源名与目标像素尺寸，QML 侧 Image 自己加载
    Q_INVOKABLE void printImage(const QString& resourceName, int width = 0, int height = 0, int ypos = 0);
    // 图形（C# PrintShape / <shape>）：type ∈ space/rect/polygon，param 为百分比
    Q_INVOKABLE void printShape(const QString& type, const QList<int>& params);
    // OUTPUTLOG（C# Console.OutputLog）：把全部显示行文本写进 path
    Q_INVOKABLE bool outputLog(const QString& path) const;
    // GETLINESTR：第 lineNo 逻辑行的文本
    Q_INVOKABLE QString lineText(int lineNo) const;
    // GETDISPLAYLINE：第 lineNo 逻辑行（跳过折行续行）的文本；越界为空串。
    // 对齐 EE readme「0 起算、LINECOUNT 恒为空、LINECOUNT 次循环取全行」。
    [[nodiscard]] QString displayLineText(int lineNo) const;
    // BINPUT/BINPUTS：是否存在「当前屏幕刚打印的」可点击按钮（尾部按钮块 + 当前行）。
    [[nodiscard]] bool hasEnabledButton() const;
    // HTML_GETPRINTEDSTR / HTML_POPPRINTINGSTR：printHtml 收到的原文缓冲
    Q_INVOKABLE QString htmlPrintedStr(int lineNo) const;
    Q_INVOKABLE QString htmlPopPrintingStr();
    // LINEISEMPTY（C# Console.EmptyLine）：当前打印缓冲是否为空
    Q_INVOKABLE bool currentLineEmpty() const;
    Q_INVOKABLE void clearLines(int n);                           // CLEARLINE（按逻辑行数删）
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void setAlignment(ConsoleAlign align);
    Q_INVOKABLE void setColor(const QColor& color);
    Q_INVOKABLE void resetColor();
    // SETBGCOLOR / RESETBGCOLOR（文字背景色）
    Q_INVOKABLE void setBgColor(const QColor& color);
    Q_INVOKABLE void resetBgColor();
    Q_INVOKABLE void setFontStyle(bool bold, bool italic, bool underline, bool strike);

    void printTemplate(const PrintTemplate& output);
    Q_INVOKABLE void flush();                 // 发布脏状态（尾行 insert/dataChanged）
    Q_INVOKABLE void tick();                  // 帧节拍：脏则 flush

    // 历史保留行数（GuiManager 的 MaxLog）；缩小容量会从头部丢最旧的行
    Q_INVOKABLE void setMaxLog(int lines);
    [[nodiscard]] int maxLog() const { return m_buffer.capacity(); }

    // ---- 排版参数（GuiManager / 配置接线）----
    Q_INVOKABLE void setFontSize(int px);
    // 逻辑网格（脚本看到的列/行数）——由引擎按配置窗口与字号设定，装载后不变；
    // 没有「随窗口像素重排」的入口（窗口缩放是 QML 舞台的等比 transform）
    Q_INVOKABLE void setGridColumns(int columns);
    Q_INVOKABLE void setGridRows(int rows);
    // 行高（GuiManager 的 LineHeight）
    Q_INVOKABLE void setLineHeight(int px);

    // ---- 诊断（test_cli :geometry / D-Bus /debug；返回「当前屏幕」的扁平区块）----
    Q_INVOKABLE QVariantList screenBlocks() const;
    Q_INVOKABLE QVariantList lineBlocks(int row) const;
    // 性能诊断（摊平耗时/模型信号计数；appemuera 的 D-Bus /debug 读取）
    Q_INVOKABLE QString perfReport() const;
    Q_INVOKABLE void resetPerfCounters();

    // ---- 输入桥接 ----
    // defaultValue：本次等待的缺省值（C# InputRequest.HasDefValue/DefIntValue/
    // DefStrValue）。空回车时用它；无缺省则**忽略**这次回车（继续等输入）。
    void notifyInputRequested(const QString& kind, const QVariant& defaultValue = QVariant());
    void notifyInputDone();

    // REUSELASTLINE（C# PrintTemporaryLine）：单行输出并标记「一時行」——
    // 下一个显示行输出会**替换**它（C# addDisplayLine 的 LastLineIsTemporary）。
    // DQPRINT 逐字动画靠它把整句动画在一行内完成。
    void notifyReuseLastLine(const QString& text);
    // ---- QML 调用 ----
    Q_INVOKABLE void clickAt(int lineIndex, int segmentIndex);
    Q_INVOKABLE void submitMouseKey(int type, int r1, int r2, int r3, int r4);
    // QML 键盘入口：直接上报 Qt 键码与 Qt 修饰符（KeyEvent.key /
    // KeyEvent.modifiers），由 C++ 的键码模型（GameView/key_map.h）换算成
    // INPUTMOUSEKEY 期待的 Emuera 键码 —— QML 不再硬编码任何数字键码。
    // 等价于 submitMouseKey(3, keyCode, keyData, 0, 0)（对齐 C# PressPrimitiveKey）。
    Q_INVOKABLE void submitQtKey(int qtKey, int qtModifiers);
    Q_INVOKABLE void submitInput(qint64 value);
    Q_INVOKABLE void submitInputString(const QString& value);
    // 提交（整数型等待）。text 为空时按 C# doInputToEmueraProgram 处理：
    //   有缺省值 -> 交缺省值；没有 -> **忽略**（不交付，继续等）；
    //   非空但解析不出整数 -> 同样忽略。
    // 以前 QML 直接交 `parseInt(text)||0`，空回车会变成 RESULT=0 ——
    // eraTW 外出列表里 0 == MAIN_MAP 就是「从外面回家」=「没操作就自动返回」。
    Q_INVOKABLE void submitIntegerText(const QString& text);
    // 提交（字符串型等待）：空串同样按缺省值规则处理。
    Q_INVOKABLE void submitStringText(const QString& text);
    // WAIT/WAITANYKEY/FORCEWAIT/ANYKEY：点击控制台任意处或回车即继续
    //（对齐 C# IsWaitingEnterKey 的鼠标/按键裁决）
    Q_INVOKABLE void submitAnyKey();

    // ---- 滚动请求（所有权在 QML 视图；C++/D-Bus/菜单只能请求）----
    Q_INVOKABLE void requestScrollBy(int lines) { emit scrollRequested(lines); }
    Q_INVOKABLE void requestScrollToBottom() { emit scrollToBottomRequested(); }

    // 供 C++/测试
    ConsoleBuffer& buffer() { return m_buffer; }
    const ConsoleBuffer& buffer() const { return m_buffer; }
    [[nodiscard]] const ConsoleLayout& layout() const { return m_layout; }
    [[nodiscard]] QString currentLineText() const;   // 未 flush 的当前行文本（调试）

public slots:
    void setFrameMs(int ms);

signals:
    void lineCountChanged();
    void cleared();
    void frameMsChanged();
    void generationChanged();
    void inputRequested(const QString& kind);
    void waitingInputChanged();
    void mouseKeySubmitted(int type, int r1, int r2, int r3, int r4);
    void inputSubmitted(qint64 value);
    void inputSubmittedString(const QString& value);
    // CLEARTEXTBOX：QML 输入栏清空请求（C# Console.ClearTextBox）
    void clearTextBoxRequested();
    // 滚动请求（D-Bus /debug、菜单栏；QML 视图消费 —— contentY 是视图的状态）
    void scrollRequested(int lines);
    void scrollToBottomRequested();

private:
    int displayLineCount() const { return m_buffer.count() + (m_pendingOpen ? 1 : 0); }
    ConsoleDisplayLine displayLine(int index) const;
    void ensureLineOpen();
    void appendPart(const ConsoleSpan& part);          // 追加到「未定型」串
    void sealSpan();                                   // 未定型 part → 段
    // 把「未定型」串按 ButtonStringCreator 切段（对齐 C# fromCssToButton）
    void flushPendingToSegments(QList<ConsoleSegment>& out) const;
    ConsoleDisplayLine buildLine() const;
    void markDirty();                                  // 内容变化：等 flush 发布
    // 尾行摊平（flattenLine）时把行内坐标补齐成区块数据
    QVariantList flattenLine(const ConsoleDisplayLine& line, int row) const;
    // 该行最高的区块行距（立絵行 > 1；委托高度恒为一格，此值供诊断/测试）
    int lineSpanRows(const ConsoleDisplayLine& line) const;
    // 记录「区块最多往上探出几行」（QML 的 cacheBuffer 依据）
    void trackSpanReach(const ConsoleDisplayLine& line);
    // 按行缓存：键 = 行 serial。头部裁剪/CLEARLINE 只按 serial 区间清理
    // 孤儿条目 —— 永不因「行号前移」整表作废。
    void pruneHeadCacheIfNeeded() const;
    void pruneTailCache() const;
    void invalidateLineCache();

    ConsoleBuffer m_buffer;
    ConsoleLayout m_layout;

    // ---- 「当前行」打印缓冲（C# PrintStringBuffer）----
    QList<ConsoleSpan>    m_pendingParts;     // 还没成段的 part（m_stringList/builder）
    QList<ConsoleSegment> m_sealed;           // 已经定型的段（m_buttonList）
    ConsoleStyle m_style;
    ConsoleAlign m_align = ConsoleAlign::Left;
    bool         m_pendingOpen = false;
    bool         m_dirty = false;
    bool         m_wrapLines = false;         // 容器自由 → 默认不折行

    int  m_lineHeight = 19;
    int  m_maxSpanReach = 1;
    int  m_frameMs = 16;
    quint64 m_generation = 1;

    QStringList m_htmlLines;      // printHtml 原文（HTML_POPPRINTINGSTR 消费）
    QString m_inputKind;
    // 本次等待的缺省值（C# InputRequest.HasDefValue/DefIntValue/DefStrValue）：
    // 空回车时交付它；无效表示「无缺省」-> 空回车被忽略。
    QVariant m_inputDefault;
    bool    m_waitingInput = false;
    // 最后一行是否为「一時行」（REUSELASTLINE）：下一行显示输出替换它
    bool    m_lastLineTemporary = false;

    QTimer m_timer;

    // ---- 模型发布状态 ----
    // 尾部「未定型行」是否已在模型里（print 打开后由 flush 插入，newline 摘除）。
    // m_pendingOpen 是打印状态，m_tailRow 是模型状态 —— 每帧只发一次信号。
    bool m_tailRow = false;
    bool m_tailDirty = false;
    int  m_publishedLineCount = 0;

    // ---- 按行摊平缓存（serial 键；显示行提交后不可变 -> 每行只摊平一次）----
    // 失效：clearAll 全清；字号/行高/网格变化（区块尺寸被烘焙进缓存值）全清。
    // 头部裁剪/CLEARLINE 只按 [firstSerial, lastSerial] 区间清理孤儿条目。
    mutable QHash<quint64, QVariantList> m_lineCache;
    mutable quint64 m_cacheFirstSerial = 0;   // 上次剪枝时现存行的最小 serial

    // 性能计数器（perfReport 读取；D-Bus /debug 转发）
    mutable qint64 m_lineCacheBuilds = 0;    // 摊平次数（缓存未命中）
    mutable qint64 m_lineCacheMs = 0;        // 摊平累计耗时
    qint64 m_insertedRows = 0;               // insertRows 发出的行数
    qint64 m_removedRows = 0;                // removeRows 发出的行数
};

#endif // CONSOLE_BACKEND_H
