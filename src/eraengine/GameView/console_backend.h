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
#include <QtQml/qqmlregistration.h>
#include "console_types.h"
#include "GameData/ast/logical_line.h"
#include "console_buffer.h"
#include "console_layout.h"

// ---------------------------------------------------------------------------
// ConsoleBackend —— 显示层的 C++ 后端（C++ 决定「显示什么」，QML 决定「怎么画」）
//
// 混合形态（对齐 C# GameView，但把「绘制」留给 QML）：
//
//   ERB PRINT 族
//      ↓  print / printButton / printImage / printShape …
//   「当前行」打印缓冲（未定型的 part 串）
//      ↓  newline() = flush：ButtonStringCreator 切段 → ConsoleSegment[]
//   ConsoleDisplayLine { ConsoleSegment[] { ConsoleSpan[] } }
//      ↓  visibleLines（QVariantList）
//   QML：每行一个容器，容器里每个**最小单位区块**（ConsoleSpan）一个可视对象：
//        text  → Text          image → Image(image://emuera/<name>)   shape → 自绘
//
// 关键点：
//   * 最小单位区块是 ConsoleSpan，自带 pointX/width；**容器可以随便摆位置**
//     （Row 自然排、或按 pointX 绝对摆都可以），C++ 不强制折行；
//   * 段（ConsoleSegment）负责「可点击 / 点击值 / 世代」；
//   * 图片/图形不解析、不加载：C++ 只给资源名与尺寸参数，QML 自己渲染。
// ---------------------------------------------------------------------------
class ConsoleBackend : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ConsoleBackend)
    QML_UNCREATABLE("ConsoleBackend 由 EraEngine 提供")

    Q_PROPERTY(int  lineCount        READ lineCount        NOTIFY lineCountChanged)
    Q_PROPERTY(int  visibleCount     READ visibleCount     WRITE setVisibleCount     NOTIFY windowChanged)
    Q_PROPERTY(int  scrollOffset     READ scrollOffset     WRITE setScrollOffset     NOTIFY windowChanged)
    Q_PROPERTY(bool followTail       READ followTail                                  NOTIFY windowChanged)
    Q_PROPERTY(int  frameMs          READ frameMs          WRITE setFrameMs          NOTIFY frameMsChanged)
    Q_PROPERTY(QString inputKind     READ inputKind                                  NOTIFY inputRequested)
    Q_PROPERTY(bool waitingInput     READ waitingInput                                NOTIFY waitingInputChanged)    Q_PROPERTY(quint64 generation    READ generation                                 NOTIFY generationChanged)
    // 逻辑网格列/行数：**不是**常量。装载配置后会按「窗口宽 ÷ 单元格宽」重算
    // （eraTW 是 175 列，代码默认只有 80）。若声明为 CONSTANT，QML 会一直用旧值
    // 换算像素格子，而 C++ 给出的区块坐标已按新网格计算，两者比例不一致
    // ——表现就是「内容比窗口宽/高出一倍，只能看见一半」。
    Q_PROPERTY(int gridColumns READ gridColumns NOTIFY windowChanged)
    Q_PROPERTY(int gridRows READ gridRows NOTIFY windowChanged)

public:
    explicit ConsoleBackend(QObject* parent = nullptr);

    // ---- 可见窗口模型（供 QML Instantiator/Repeater 直接建模）----
    // ① 行模型（行 → 段 → 最小单位区块）：
    //    elements = ConsoleDisplayLine::toVariantMap()：{ segments:[{parts:[…],…}], align, … }
    Q_PROPERTY(QVariantList visibleLines READ visibleLines NOTIFY windowChanged)
    [[nodiscard]] QVariantList visibleLines() const;
    // ② 分层模型（root / text / image 三层直接用）：
    //    扁平化的「最小单位区块」列表，**坐标已由 C++ 算好**（含对齐平移与行高）：
    //      { kind, text, x, y, width, height, color, …,
    //        lineIndex, segmentIndex, isButton, clickable, tooltip, generation }
    //    QML 的 text 层只挑 kind=="text"，image 层只挑 kind=="image"/"shape"，
    //    各自在层内按 x/y 自由摆放。
    Q_PROPERTY(QVariantList visibleBlocks READ visibleBlocks NOTIFY windowChanged)
    [[nodiscard]] QVariantList visibleBlocks() const;
    [[nodiscard]] int windowFirstLine() const;

    // ③ 三个「层」各自的区块列表（QML 里一一对应三个 Instantiator）
    //    root 层 = 可见窗口容器；text 层 / image 层 = 区块的宿主
    //    三个属性共享同一份按代数(epoch)的缓存：一次 visibleBlocks() 摊平，
    //    三个层各自取用 —— 此前每个属性各建一遍，同一帧 3× 重复构建。
    Q_PROPERTY(QVariantList textBlocks  READ textBlocks  NOTIFY windowChanged)
    Q_PROPERTY(QVariantList imageBlocks READ imageBlocks NOTIFY windowChanged)
    Q_PROPERTY(QVariantList shapeBlocks READ shapeBlocks NOTIFY windowChanged)
    Q_PROPERTY(int contentHeight READ contentHeight NOTIFY windowChanged)
    [[nodiscard]] QVariantList textBlocks() const { return layerBlocks(QStringLiteral("text")); }
    [[nodiscard]] QVariantList imageBlocks() const { return layerBlocks(QStringLiteral("image")); }
    [[nodiscard]] QVariantList shapeBlocks() const { return layerBlocks(QStringLiteral("shape")); }
    [[nodiscard]] QVariantList layerBlocks(const QString& layer) const;
    [[nodiscard]] int contentHeight() const;

    // ---- 性能诊断（QML 卡顿定位；appemuera 的 D-Bus /debug 也读取）----
    Q_INVOKABLE QString perfReport() const;
    Q_INVOKABLE void resetPerfCounters();

    // ---- 只读属性 ----
    [[nodiscard]] int  lineCount() const;                // 显示行数（缓冲里）
    [[nodiscard]] int  logicalLineCount() const;         // 逻辑行数 = LINECOUNT
    [[nodiscard]] int  visibleCount() const { return m_visibleCount; }
    [[nodiscard]] int  scrollOffset() const { return m_scrollOffset; }
    [[nodiscard]] bool followTail() const { return m_scrollOffset == 0; }
    [[nodiscard]] int  frameMs() const { return m_frameMs; }
    [[nodiscard]] QString inputKind() const { return m_inputKind; }
    [[nodiscard]] bool waitingInput() const { return m_waitingInput; }
    [[nodiscard]] quint64 generation() const { return m_generation; }
    [[nodiscard]] int gridColumns() const { return m_layout.gridColumns(); }
    [[nodiscard]] int gridRows() const { return m_layout.gridRows(); }

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
    void markDirty();
    Q_INVOKABLE void flush();                 // 强制 emit windowChanged

    // 历史保留行数（GuiManager 的 MaxLog）；缩小容量会丢弃最旧的行
    Q_INVOKABLE void setMaxLog(int lines);
    [[nodiscard]] int maxLog() const { return m_buffer.capacity(); }

    // ---- 排版参数（GuiManager / 配置接线）----
    Q_INVOKABLE void setFontSize(int px);
    Q_INVOKABLE void setWindowWidth(int px);
    // 逻辑网格（脚本看到的列/行数）——由引擎按配置窗口与字号设定，装载后不变
    Q_INVOKABLE void setGridColumns(int columns);
    Q_INVOKABLE void setGridRows(int rows);
    // 行高（GuiManager 的 LineHeight）：分层模型里 y = 行序号 × 行高
    Q_INVOKABLE void setLineHeight(int px);
    [[nodiscard]] int lineHeight() const { return m_lineHeight; }
    [[nodiscard]] int fontSize() const { return m_layout.fontSize(); }
    [[nodiscard]] int windowWidth() const { return m_layout.windowWidth(); }
    // 平面重建用的「一列」宽度（半角字符宽）＝ fontPx / 2
    [[nodiscard]] int columnWidth() const { return qMax(1, m_layout.fontSize() / 2); }
    // 是否由 C++ 折行。默认 false：**容器自由渲染位置**，C++ 只给区块与位置数据
    void setWrappingEnabled(bool on) { m_wrapLines = on; }
    [[nodiscard]] bool wrappingEnabled() const { return m_wrapLines; }

    // ---- 输入桥接 ----
    void notifyInputRequested(const QString& kind);
    void notifyInputDone();

    // REUSELASTLINE（C# PrintTemporaryLine）：单行输出并标记「一時行」——
    // 下一个显示行输出会**替换**它（C# addDisplayLine 的 LastLineIsTemporary）。
    // DQPRINT 逐字动画靠它把整句动画在一行内完成。
    void notifyReuseLastLine(const QString& text);
    // ---- QML 调用 ----
    Q_INVOKABLE QVariantMap visibleLine(int index) const;
    Q_INVOKABLE int  visibleLineCount() const;
    Q_INVOKABLE void clickAt(int visibleIndex, int segmentIndex);
    Q_INVOKABLE void scrollBy(int lines);
    Q_INVOKABLE void scrollToBottom();
    Q_INVOKABLE void tick();
    Q_INVOKABLE void submitMouseKey(int type, int r1, int r2, int r3, int r4);
    Q_INVOKABLE void submitInput(qint64 value);
    Q_INVOKABLE void submitInputString(const QString& value);
    // WAIT/WAITANYKEY/FORCEWAIT/ANYKEY：点击控制台任意处或回车即继续
    //（对齐 C# IsWaitingEnterKey 的鼠标/按键裁决）
    Q_INVOKABLE void submitAnyKey();

    // 供 C++/测试
    ConsoleBuffer& buffer() { return m_buffer; }
    const ConsoleBuffer& buffer() const { return m_buffer; }
    [[nodiscard]] const ConsoleLayout& layout() const { return m_layout; }
    [[nodiscard]] QString currentLineText() const;   // 未 flush 的当前行文本（调试）

signals:
    void windowChanged();
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

public slots:
    void setVisibleCount(int count);
    void setScrollOffset(int offset);
    void setFrameMs(int ms);

private:
    int displayLineCount() const { return m_buffer.count() + (m_pendingOpen ? 1 : 0); }
    ConsoleDisplayLine displayLine(int index) const;
    void ensureLineOpen();
    void appendPart(const ConsoleSpan& part);          // 追加到「未定型」串
    void sealSpan();                                   // 未定型 part → 段
    // 把「未定型」串按 ButtonStringCreator 切段（对齐 C# fromCssToButton）
    void flushPendingToSegments(QList<ConsoleSegment>& out) const;
    ConsoleDisplayLine buildLine() const;
    void clampScroll();
    // windowChanged 的统一出口：内容代数(epoch)自增后再广播，
    // 层缓存（m_*Cache）据此失效。所有窗口变化必须走这里。
    void notifyWindowChanged();

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
    int  m_visibleCount = 40;
    int  m_scrollOffset = 0;
    // 缓冲里**最长**的区块能往上探出几行（立絵跨行 + 图片 ypos 负偏移）。
    // visibleBlocks() 靠它决定「窗口上方多扫几行」，否则跨行图会整张消失。
    int  m_maxSpanReach = 1;
    void trackSpanReach(const ConsoleDisplayLine& line);
    int  m_frameMs = 16;
    quint64 m_generation = 1;

    QStringList m_htmlLines;      // printHtml 原文（HTML_POPPRINTINGSTR 消费）
    QString m_inputKind;
    bool    m_waitingInput = false;
    // 最后一行是否为「一時行」（REUSELASTLINE）：下一行显示输出替换它
    bool    m_lastLineTemporary = false;

    QTimer m_timer;

    // ---- 窗口内容代数与层缓存（见 notifyWindowChanged / layerBlocks）----
    int  m_windowEpoch = 1;
    mutable int          m_blockCacheEpoch = -1;   // 缓存对应的代数
    mutable QVariantList m_textCache, m_imageCache, m_shapeCache;
    // 性能计数器（perfReport 读取；D-Bus /debug 转发）
    mutable qint64 m_layerBlockCalls = 0;
    mutable qint64 m_visibleBlockCalls = 0;
    mutable qint64 m_blocksBuilt = 0;        // 累计生成的区块数（含三次层取用）
    mutable qint64 m_blockBuildMs = 0;       // 累计构建耗时（毫秒）
    mutable qint64 m_windowChangedCount = 0; // windowChanged 次数（≈ 模型重置次数）
};

#endif // CONSOLE_BACKEND_H
