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
// test_console_backend.cpp
//
// 验证显示层（GameView/ConsoleBackend，本身即 QAbstractListModel）：
//   1. 输出写入（print/newline/printButton）与对齐
//   2. 行模型信号：未定型尾行 flush 后 insertRows，newline 摘尾行插已提交行
//   3. CLEARLINE / 容量裁剪 -> removeRows；rowCount 与缓冲恒一致
//   4. 按钮命中 -> 输入信号；generation 失效旧按钮
//   5. 跨行图片 / ypos 图层的区块数据（立絵行距、offsetRows 对齐）
//   6. 空输入语义（对齐 C# doInputToEmueraProgram）
//
// 滚动/窗口的簿记已移交 QML ListView（虚拟化 + reuseItems），不再在此测试。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>

#include "ast/print_template.h"
#include "console_backend.h"
#include "graphics_store.h"
#include "resource_image_provider.h"

// 段 -> span 摊平（行结构是「行 -> 段 -> span」两层；断言按最小单位读）
static QList<ConsoleSpan> flatSpansOf(const ConsoleDisplayLine& line) {
    QList<ConsoleSpan> flat;
    for (const ConsoleSegment& seg : line.segments) flat += seg.spans;
    return flat;
}

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) {
        qDebug().noquote() << "  [ok ]" << what;
    } else {
        qDebug().noquote() << "  [FAIL]" << what;
        ++g_failures;
    }
}

static QString textOf(const QVariantList& blocks, const QString& name) {
    for (const QVariant& v : blocks) {
        const QVariantMap m = v.toMap();
        if (m.value("text").toString() == name) return m.value("text").toString();
    }
    return QString();
}

static QVariantMap blockOf(const QVariantList& blocks, const QString& name) {
    for (const QVariant& v : blocks) {
        const QVariantMap m = v.toMap();
        if (m.value("text").toString() == name) return m;
    }
    return QVariantMap();
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "ConsoleBackend test";
    qDebug() << "===================";

    // ---- 1. 输出写入 ----
    qDebug() << "\n1) 输出写入（print/newline）";
    {
        ConsoleBackend console;
        for (const QString& s : QStringList{"A", "B", "C", "D", "E"}) {
            console.print(s);
            console.newline();
        }
        check(console.lineCount() == 5, "lineCount == 5");
        check(console.rowCount() == 5, "rowCount == 5（已提交行全部入模）");
        check(console.lineText(0) == "A" && console.lineText(4) == "E",
              "缓冲行序 A..E");
    }

    // Insertion notifications must bracket the mutation, including head trim.
    {
        ConsoleBackend c;
        c.setMaxLog(3);
        int before = -1;
        QObject::connect(&c, &QAbstractItemModel::rowsAboutToBeInserted,
                         [&](const QModelIndex&, int first, int) {
            before = c.rowCount();
            check(first == before, "insert starts at pre-mutation rowCount");
        });
        QObject::connect(&c, &QAbstractItemModel::rowsInserted,
                         [&](const QModelIndex&, int first, int last) {
            check(c.rowCount() == before + last - first + 1,
                  "rowsInserted observes the completed mutation");
        });
        for (int i = 0; i < 6; ++i) { c.print(QString::number(i)); c.newline(); }
        check(c.rowCount() == 3 && c.lineText(0) == "3", "trimmed insertion preserves rows");
    }

    // ---- 2. 行模型信号：未定型尾行 ----
    qDebug() << "\n2) 行模型信号（尾行 insert/dataChanged）";
    {
        ConsoleBackend c;
        int inserted = 0, removed = 0, dataChanges = 0;
        QObject::connect(&c, &QAbstractItemModel::rowsInserted,
                         [&](const QModelIndex&, int, int) { ++inserted; });
        QObject::connect(&c, &QAbstractItemModel::rowsRemoved,
                         [&](const QModelIndex&, int, int) { ++removed; });
        QObject::connect(&c, &QAbstractItemModel::dataChanged,
                         [&](const QModelIndex&, const QModelIndex&, const QList<int>&) { ++dataChanges; });

        c.print("A");
        check(inserted == 0, "print 不发信号（未定型行等帧发布）");
        c.flush();
        check(inserted == 1 && c.rowCount() == 1, "flush：尾行 insertRows 1 次");
        check(textOf(c.lineBlocks(0), "A") == "A", "尾行数据可读（A）");

        c.print("B");
        c.flush();
        check(dataChanges == 1 && inserted == 1 && removed == 0,
              "尾行内容增长只走 dataChanged（行数不变，不重建）");
        check(c.lineBlocks(0).size() >= 2, "尾行摊平含 A、B 两个区块");

        c.newline();
        check(removed == 1 && inserted == 2 && c.rowCount() == 1,
              "newline：尾行摘除 + 已提交行插入（rowCount 不变）");

        for (int i = 0; i < 100; ++i) { c.print("batch"); c.newline(); }
        check(c.rowCount() == c.lineCount(), "rowCount 与缓冲行数恒一致");
        check(textOf(c.lineBlocks(c.rowCount() - 1), "batch") == "batch",
              "最后一行数据可读");
    }

    // ---- 3. CLEARLINE / 容量裁剪 -> removeRows ----
    qDebug() << "\n3) CLEARLINE 与容量裁剪";
    {
        ConsoleBackend c;
        c.print("A"); c.newline();
        c.print("B"); c.newline();
        c.print("C"); c.newline();
        c.flush();
        check(c.lineCount() == 3 && c.logicalLineCount() == 3, "3 行，LINECOUNT == 3");

        int removed = 0, first = -1, last = -1;
        QObject::connect(&c, &QAbstractItemModel::rowsRemoved,
                         [&](const QModelIndex&, int f, int l) {
                             ++removed; first = f; last = l;
                         });
        c.clearLines(2);
        check(removed == 1 && first == 1 && last == 2, "CLEARLINE 2 -> 尾部区间 removeRows(1..2)");
        check(c.lineCount() == 1 && c.lineText(0) == "A", "只剩 A");
        check(c.logicalLineCount() == 1, "CLEARLINE 后 LINECOUNT == 1");

        c.clearLines(0);
        check(c.lineCount() == 1 && c.logicalLineCount() == 1,
              "CLEARLINE 0 不删除且不改变 LINECOUNT");
        c.clearLines(-1);
        check(c.lineCount() == 1 && c.logicalLineCount() == 1,
              "CLEARLINE 负数不删除且不改变 LINECOUNT");
        c.clearLines(4);
        // C# deleteLine：`while (delNum < num) { if (displayLineList.Count == 0) break; … }`
        // 之后还有 `if (delNum < num) logicalLineCount -= num - delNum;`
        // —— 短删的差额照扣，LINECOUNT **可以为负**（era 系脚本的
        // `CLEARLINE LINECOUNT` 在行为空时就走到这条）。这里剩 1 行、请求 4，
        // 实删 1 -> logicalLineCount = 0 - (4-1) = -3。
        check(c.lineCount() == 0 && c.logicalLineCount() == -3,
              "CLEARLINE 超量请求：按 C# 短删补偿，LINECOUNT 变负（0-3 = -3）");
        // 一時行替换走同一入口但**不**做补偿（C# 只在 LastLineIsTemporary 时
        // deleteLine(1)，请求量与现存行数总是相等）—— 这里直接验证语义边界：
        // 负数计数下再删一行仍是「按实际删除递减」。
        c.print("D"); c.newline(); c.flush();
        check(c.logicalLineCount() == -2, "负数计数下追加一行：-3 + 1 = -2");
    }
    // 「一時行」（REUSELASTLINE / DQPRINT）：
    //   * 一時行本身要出现在屏上；
    //   * 紧跟的下一行输出**顶掉**它（C# addDisplayLine 的 deleteLine(1)），
    //     而不是把它自己（以及下一行）一起吞掉。
    {
        ConsoleBackend c;
        c.print("[A] first"); c.newline();
        c.notifyReuseLastLine("[B] temp1");
        c.flush();
        check(c.lineCount() == 2 && c.lineText(1) == "[B] temp1",
              "一時行（REUSELASTLINE）自身可见");
        check(c.lineBlocks(1).size() > 0, "一時行摊平非空（不是空行）");

        c.print("[C] plain-after-temp");
        c.newline();
        c.flush();
        check(c.lineCount() == 2, "紧随一時行的下一行顶掉它（行数不变）");
        check(c.lineText(0) == "[A] first" && c.lineText(1) == "[C] plain-after-temp",
              "顶掉的是舊一時行，本行文本完整（不再被吞）");
        check(c.logicalLineCount() == 2, "一時行替换不动 LINECOUNT");

        // 连续 REUSELASTLINE（DQPRINT 逐字动画）：整句始终只占一行
        for (int i = 0; i < 5; ++i)
            c.notifyReuseLastLine(QStringLiteral("[D] tick %1").arg(i));
        c.flush();
        check(c.lineCount() == 3 && c.lineText(2) == "[D] tick 4",
              "连续一時行只保留最后一条（动画不堆行）");
        check(c.logicalLineCount() == 3, "连续一時行替换仍不动 LINECOUNT");

        c.clearLines(1);
        check(c.lineCount() == 2 && c.lineText(1) == "[C] plain-after-temp",
              "CLEARLINE 能删掉一時行（按逻辑行）");
        check(c.logicalLineCount() == 2, "删一時行后 LINECOUNT 按实际行数递减");
    }
    {
        ConsoleBackend c;
        c.setMaxLog(24);   // 小容量：min(kTrimBatch, cap) = 24，溢出即裁
        int removedBatches = 0;
        QObject::connect(&c, &QAbstractItemModel::rowsRemoved,
                         [&](const QModelIndex&, int, int) { ++removedBatches; });
        for (int i = 0; i < 100; ++i) { c.print(QStringLiteral("L%1").arg(i)); c.newline(); }
        check(c.lineCount() == 24, "容量 24 -> lineCount 恒为 24");
        check(c.rowCount() == c.lineCount(), "rowCount 与缓冲一致（头部裁剪有信号）");
        check(removedBatches > 0, "头部裁剪发出 removeRows");
        check(c.lineText(0) == QStringLiteral("L76"), "最旧保留 L76（丢最旧 76 行）");
    }

    // ---- 4. 按钮与输入；generation 失效 ----
    qDebug() << "\n4) 按钮与输入";
    {
        ConsoleBackend console;
        int submitted = -1;
        QObject::connect(&console, &ConsoleBackend::inputSubmitted,
                         [&submitted](qint64 v) { submitted = static_cast<int>(v); });

        console.clearAll();
        console.notifyInputRequested("INPUT");   // 输入裁决：等待整数型输入后按钮才可提交
        console.printButton("[1] 选择一", 1);
        console.printButton("[2] 选择二", 2);
        console.newline();

        // 两个按钮在同一行；点击第 2 个
        console.clickAt(console.lineCount() - 1, 1);
        check(submitted == 2, "clickAt(button#1) -> inputSubmitted(2)");

        // 提交后同一批按钮失效（对齐 C# generation）
        submitted = -1;
        console.clickAt(console.lineCount() - 1, 0);
        check(submitted == -1, "stale button after submit ignored");

        // 新一批按钮可点击
        console.printButton("[3] 新选择", 3);
        console.newline();
        console.clickAt(console.lineCount() - 1, 0);
        check(submitted == 3, "new generation button clickable");

        // 输入权限通过 QObject 属性更新，不替换任何行的绘制数据。
        ConsoleBackend partial;
        partial.print("静态文本");
        partial.newline();
        partial.printButton("按钮", 1);
        partial.newline();
        int changedRows = 0;
        QObject::connect(&partial, &QAbstractItemModel::dataChanged,
                         [&](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
            if (roles == QList<int>{ConsoleBackend::BlocksRole}) ++changedRows;
        });
        partial.notifyInputRequested("INPUT");
        check(changedRows == 0, "输入请求不替换按钮行的 BlocksRole");
        partial.notifyInputDone();
        check(changedRows == 0, "输入结束不替换绘制数据");
    }
    {
        ConsoleBackend c;
        check(c.lastLineIsEmpty(), "空控制台的末行视为空");
        c.print("普通行");
        c.newline();
        check(!c.lastLineIsTemporary() && !c.lastLineIsEmpty(),
              "普通提交行不是临时行且非空");
        c.notifyReuseLastLine("临时行");
        c.flush();
        check(c.lastLineIsTemporary() && !c.lastLineIsEmpty(),
              "临时提交行状态可见");
        c.clearLines(2);
        check(c.lastLineIsEmpty(), "删除临时行和前行后末行状态为空");
    }
    {
        ConsoleBackend c;
        c.printButton("old", 9);
        c.newline();
        c.notifyInputRequested("INPUTMOUSEKEY");
        c.notifyInputDone(); // a timer or direct engine input resumed execution
        c.printButton("new", 10);
        c.newline();
        c.notifyInputRequested("INPUT");
        int value = -1;
        QObject::connect(&c, &ConsoleBackend::inputSubmitted, [&](qint64 v) { value = v; });
        c.clickAt(0, 0);
        check(value == -1, "automatic input completion invalidates old buttons");
        c.clickAt(1, 0);
        check(value == 10, "new prompt remains clickable after automatic completion");
    }

    // ---- 提交收尾：正文只留「记账占位行」，不打印提交值 ----
    // C# doInputToEmueraProgram 结尾是 Print(str) + PrintFlush(false)，因为 C# 的
    // 输入框在提交时就被清空、正文是唯一能留下值的地方。本移植的值显示在 QML
    // 输入栏（TextInput），正文再打印一遍就是同一事实的第二份副本（「输出两次」）。
    // 但**行**必须留下：eraTW 的 CLEARLINE 16（15 行菜单 + 1 行回显）按行删。
    {
        ConsoleBackend c;
        c.print("[A]");
        c.newline();
        check(c.buffer().count() == 1, "前提：一行正文");
        c.notifyInputRequested("INPUT");
        c.notifyInputCommitted();
        check(c.buffer().count() == 2, "提交后正文多一条逻辑行（CLEARLINE 记账）");
        check(c.lineText(1).isEmpty(), "占位行不带提交值（值只在输入栏）");
        check(!c.waitingInput(), "提交收尾结束等待");
        // 未定型缓冲为空时也要产出行 —— 这就是 CLEARLINE 数的那一行
        check(c.displayLineText(1).isEmpty(), "占位行显示为空");
    }
    {
        // WAIT / ANYKEY 与 INPUTMOUSEKEY 没有 str（C# PrintFlush(false) 空操作），不占行
        ConsoleBackend c;
        c.print("[A]");
        c.newline();
        c.notifyInputRequested("WAIT");
        c.notifyInputCommitted();
        check(c.buffer().count() == 1, "WAIT 提交不产出行");
        c.notifyInputRequested("INPUTMOUSEKEY");
        c.notifyInputCommitted();
        check(c.buffer().count() == 1, "INPUTMOUSEKEY 提交不产出行");
        // 未在等待时调用：无副作用
        c.notifyInputCommitted();
        check(c.buffer().count() == 1, "非等待状态下提交流程无副作用");
    }

    // ---- 输入类型 ↔ 按钮类型的裁决（C# SelectedString / SelectedInteger）----
    // 用户现场：FL3.8 的 CHARACTER_EDIT 屏（BINPUTS，字符串输入）按钮全是整数
    // 按钮（[取消]=30000），旧规则「字符串输入只收字符串按钮」把整屏按钮变成死键。
    qDebug() << "\n输入裁决";
    {
        ConsoleBackend c;
        int intValue = -1;
        QString strValue;
        bool strSubmitted = false;
        QObject::connect(&c, &ConsoleBackend::inputSubmitted,
                         [&](qint64 v) { intValue = static_cast<int>(v); });
        QObject::connect(&c, &ConsoleBackend::inputSubmittedString,
                         [&](const QString& v) { strSubmitted = true; strValue = v; });

        // 1) 字符串输入（BINPUTS）+ 整数按钮 -> 可点，交按钮值的十进制文本
        c.printButton("[取消]", 30000);
        c.newline();
        c.notifyInputRequested("BINPUTS");
        check(c.acceptsIntegerButtons() && c.acceptsStringButtons(),
              "BINPUTS：整数按钮与字符串按钮都接受（QML clickable 由此取值）");
        c.clickAt(0, 0);
        check(strSubmitted && strValue == "30000",
              QString("BINPUTS + 整数按钮 -> inputSubmittedString(\"30000\")，实得 \"%1\"").arg(strValue));
        check(intValue == -1, "BINPUTS + 整数按钮不走 inputSubmitted");

        // 2) 整数输入（INPUT）+ 字符串按钮 -> 仍被忽略（C# SelectedInteger 返回 null）
        ConsoleBackend d;
        int dInt = -1;
        QString dStr;
        bool dStrSubmitted = false;
        QObject::connect(&d, &ConsoleBackend::inputSubmitted,
                         [&](qint64 v) { dInt = static_cast<int>(v); });
        QObject::connect(&d, &ConsoleBackend::inputSubmittedString,
                         [&](const QString& v) { dStrSubmitted = true; dStr = v; });
        d.printButtonStr("[字符串]", "abc");
        d.newline();
        d.notifyInputRequested("INPUT");
        check(d.acceptsIntegerButtons() && !d.acceptsStringButtons(),
              "INPUT：只接受整数按钮");
        d.clickAt(0, 0);
        check(!dStrSubmitted && dInt == -1, "INPUT + 字符串按钮 -> 忽略（不交付、不推进世代）");

        // 3) 整数输入（INPUT）+ 整数按钮 -> 仍是 inputSubmitted（不回归）
        ConsoleBackend e;
        int eInt = -1;
        bool eStrSubmitted = false;
        QObject::connect(&e, &ConsoleBackend::inputSubmitted,
                         [&](qint64 v) { eInt = static_cast<int>(v); });
        QObject::connect(&e, &ConsoleBackend::inputSubmittedString,
                         [&](const QString&) { eStrSubmitted = true; });
        e.printButton("[1] 选择一", 1);
        e.newline();
        e.notifyInputRequested("INPUT");
        e.clickAt(0, 0);
        check(eInt == 1 && !eStrSubmitted, "INPUT + 整数按钮 -> inputSubmitted(1)");

        // 4) INPUTANY（AnyValue）：字符串按钮仍交字符串（不被整数通道吞掉）
        ConsoleBackend f;
        int fInt = -1;
        QString fStr;
        QObject::connect(&f, &ConsoleBackend::inputSubmitted,
                         [&](qint64 v) { fInt = static_cast<int>(v); });
        QObject::connect(&f, &ConsoleBackend::inputSubmittedString,
                         [&](const QString& v) { fStr = v; });
        f.printButtonStr("[任意]", "任意值");
        f.newline();
        f.notifyInputRequested("INPUTANY");
        check(f.acceptsIntegerButtons() && f.acceptsStringButtons(),
              "INPUTANY：两种按钮都接受");
        f.clickAt(0, 0);
        check(fStr == QString::fromUtf8("任意值") && fInt == -1,
              "INPUTANY + 字符串按钮 -> inputSubmittedString");
    }

    // ---- 菜单项 `[n]` 自动变按钮（对齐 C# ButtonStringCreator）----
    qDebug() << "\n菜单按钮";
    {
        ConsoleBackend c;
        c.print(QString::fromUtf8("[0] 开始游戏"));
        c.newline();
        const ConsoleDisplayLine& l = c.buffer().at(0);
        check(l.segments.size() == 1, "[0] -> 1 段（整行）");
        if (l.segments.size() == 1) {
            check(l.segments.first().isButton, "这一段可点击");
            check(l.segments.first().isInteger && l.segments.first().intValue == 0,
                  "按钮值 == 0");
            check(l.segments.first().spans.size() == 1
                      && l.segments.first().spans.first().text == QString::fromUtf8("[0] 开始游戏"),
                  "整行一个最小单位区块（文本未切碎）");
        }
        check(l.plainText() == QString::fromUtf8("[0] 开始游戏"), "文本保留原样");

        ConsoleBackend c2;
        c2.print(QString::fromUtf8("[9999] 設定完毕"));
        c2.newline();
        check(c2.buffer().at(0).segments.size() == 1
                  && c2.buffer().at(0).segments.first().isButton
                  && c2.buffer().at(0).segments.first().intValue == 9999, "[9999] -> 9999");

        // 非数字 `[abc]`：不是核 → 整行不可点击
        ConsoleBackend c3;
        c3.print(QString::fromUtf8("[abc] 不是数字"));
        c3.newline();
        check(!c3.buffer().at(0).segments.isEmpty()
                  && !c3.buffer().at(0).segments.first().isButton, "[abc] 不建按钮");

        // 十六进制 / 指数 / 带符号（C# 正则允许）
        ConsoleBackend c4;
        c4.print(QString::fromUtf8("[0x10] 十六进制"));
        c4.newline();
        check(c4.buffer().at(0).segments.first().isButton
                  && c4.buffer().at(0).segments.first().intValue == 16,
              "[0x10] -> 16（C# 正则支持 0x 前缀）");
    }

    qDebug() << "\n多个按钮切段";
    {
        ConsoleBackend c;
        c.print(QString::fromUtf8("[1] 选择一 [2] 选择二"));
        c.newline();
        const ConsoleDisplayLine& l = c.buffer().at(0);
        int buttons = 0;
        for (const ConsoleSegment& s : l.segments) if (s.isButton) ++buttons;
        check(buttons == 2, "两个核 -> 两段可点击");
        check(l.plainText() == QString::fromUtf8("[1] 选择一 [2] 选择二"), "切段不改变文本");
    }

    qDebug() << "\nPRINTBUTTON";
    {
        ConsoleBackend c;
        c.printButton(QString::fromUtf8("[←]"), 4);
        c.printButton(QString::fromUtf8("[↓]"), 2);
        c.newline();
        const ConsoleDisplayLine& l = c.buffer().at(0);
        check(l.segments.size() == 2, "两个 PRINTBUTTON -> 2 段");
        check(l.segments.at(0).intValue == 4 && l.segments.at(1).intValue == 2, "值 4/2");
    }

    // ---- 资源名解析不到 -> imageResolutionFailed（引擎据此落可定位诊断）----
    // 资源名多是运行期插值出来的，装载期静态不可判；所以「打了不存在的资源」
    // 由区块生成点（pinImage 失败）上报，EraEngine 拼当前执行位置写日志。
    qDebug() << "\n缺图留痕";
    {
        ConsoleBackend c;
        QStringList missing;
        QObject::connect(&c, &ConsoleBackend::imageResolutionFailed,
                         [&missing](const QString& n) { missing << n; });
        c.printImage(QStringLiteral("NO_SUCH_RESOURCE"), 100, 100);
        c.newline();
        check(missing == QStringList{QStringLiteral("NO_SUCH_RESOURCE")},
              "PRINTIMG 打了不存在的资源 -> imageResolutionFailed(资源名)");

        missing.clear();
        c.reportMissingImage(QString());
        check(missing.isEmpty(), "空资源名不报缺图（<img> 没写 src 是排版问题）");

        // 资源在位（运行期精灵）时不得报：钉住成功 = 真的能画
        GraphicsStore::gCreate(9101, 2, 2);
        GraphicsStore::spriteCreate(QStringLiteral("present_img"), 9101);
        c.printImage(QStringLiteral("present_img"), 100, 100);
        c.newline();
        check(missing.isEmpty(), "资源存在时不报缺图");
        GraphicsStore::clearAll();
        ResourceImageProvider::clearPinnedImages();
    }

    qDebug() << "\n最小单位区块 / 图 / 形";
    {
        ConsoleBackend c;
        c.print("A");
        c.printImage("face_01", 100, 100);   // width/height 是 FontSize 的百分比
        c.printShape("rect", {100});
        c.newline();
        const ConsoleDisplayLine& l = c.buffer().at(0);
        check(flatSpansOf(l).size() == 3, "3 个最小单位区块（文本/图/形）");
        if (flatSpansOf(l).size() == 3) {
            const QList<ConsoleSpan> parts = flatSpansOf(l);
            check(parts.at(0).kind == ConsoleSpanKind::Text, "第 1 个是 text");
            check(parts.at(1).kind == ConsoleSpanKind::Image
                      && parts.at(1).text == "face_01", "第 2 个是 image(face_01)");
            check(parts.at(2).kind == ConsoleSpanKind::Shape
                      && parts.at(2).shapeType == "rect", "第 3 个是 shape(rect)");
            check(parts.at(0).relCol == 0, "区块自带相对列 relCol");
            check(parts.at(0).cols == 1, "文本 'A' 占 1 个单位长");
            check(parts.at(1).cols == 2, "图片 100% FontSize = 18px = 2 个单位长");
        }
    }

    // ---- 5. 跨行图片 / ypos 图层的区块数据 ----
    // 立絵跨多行（rows）、<img ypos=N> 折算成 offsetRows（负 = 往上盖）：
    // eraTW 的画像枠/特效靠它与立絵同坐标叠加。可见性（谁在视口里）由
    // QML ListView 的虚拟化 + cacheBuffer 决定，C++ 数据永远完整产出。
    qDebug() << "\n跨行图片：行距与 ypos 图层叠加";
    {
        ConsoleBackend c;
        c.setFontSize(16);
        c.setLineHeight(16);
        c.setGridColumns(40);
        c.setGridRows(10);

        for (int i = 0; i < 7; ++i) { c.print(QStringLiteral("T%1").arg(i)); c.newline(); }   // 0..6
        c.printImage(QStringLiteral("face"), 400, 400); c.newline();                          // 7
        for (int i = 0; i < 7; ++i) { c.print(QStringLiteral("M%1").arg(i)); c.newline(); }   // 8..14
        c.printImage(QStringLiteral("frame"), 400, 400, -800); c.newline();                   // 15
        for (int i = 0; i < 5; ++i) { c.print(QStringLiteral("B%1").arg(i)); c.newline(); }   // 16..20
        // 400% * 16px = 64px = 4 行高；ypos=-800 -> -800*16/100 = -128px = -8 行
        check(c.lineCount() == 21, "21 行（图片各占 1 个逻辑行）");
        check(c.maxSpanReach() >= 12, "maxSpanReach >= 12（立絵 4 行 + 框上探 8 行，QML cacheBuffer 依据）");

        const QVariantMap face = blockOf(c.lineBlocks(7), "face");
        check(!face.isEmpty(), "第 7 行含立絵区块");
        check(face.value("rows").toInt() == 4, "跨行：rows == 4（64px / 行高 16）");
        check(face.value("height").toInt() == 64, "跨行：height == rows * 行高 = 64px");
        check(face.value("offsetRows").toDouble() == 0.0, "立絵本身没有 ypos");
        check(face.value("lineIndex").toInt() == 7, "lineIndex == 7（点击回传行号）");

        const QVariantMap frame = blockOf(c.lineBlocks(15), "frame");
        check(!frame.isEmpty(), "第 15 行含框图层区块");
        check(frame.value("rows").toInt() == 4, "框同样 4 行高");
        check(qFuzzyCompare(frame.value("offsetRows").toDouble(), -8.0),
              "ypos=-800 -> offsetRows == -8（-128px / 行高 16）");
        check(face.value("row").toInt() + face.value("offsetRows").toDouble()
                  == frame.value("row").toInt() + frame.value("offsetRows").toDouble(),
              "框与立絵**同一纵坐标**（图层叠加对齐 -> 边框落在立绘边缘）");
    }

    // ---- 5a. eraTW 画像枠：两层各占一行，不许有额外空行 ----
    // eraTW 的 `@画像一斉表示` 每一层是一次 HTML_PRINT：立絵（包在 <button> 里）
    // 一层、画像枠（裸 <img ypos='-100'>）一层。ypos 的补偿只够**一行**
    //（-100% × 字号 / 100 = -16px = 一行高），所以两层之间必须**正好隔一行**：
    // 多出一行，框就整体掉到立絵下面一格 —— 正是 QML 里看到的「错开一格」。
    qDebug() << "\n5a) eraTW 画像枠两层之间只隔一行";
    {
        ConsoleBackend c;
        c.setFontSize(16);
        c.setLineHeight(16);
        c.setGridColumns(40);
        c.setGridRows(10);
        c.print(QStringLiteral("▼"));
        c.newline();                                                                    // 0
        c.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(QStringLiteral(
            "<shape type='space' param='0'><p align='left'><button value='1001' title='t'>"
            "<img src='face' width='1125' height='1125' ypos='0'></button>")));  // 1
        c.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(QStringLiteral(
            "<shape type='space' param='0'><p align='left'>"
            "<img src='frame' width='1125' height='1125' ypos='-100'>")));       // 2
        c.flush();
        check(c.lineCount() == 3, "立絵/枠各占一行（没有多出空行）");
        const QVariantMap face = blockOf(c.lineBlocks(1), "face");
        const QVariantMap frame = blockOf(c.lineBlocks(2), "frame");
        check(!face.isEmpty() && !frame.isEmpty(), "两层都在预期的行上");
        check(frame.value("row").toInt() - face.value("row").toInt() == 1,
              "框在立絵的**下一行**（相隔 1 行，不是 2 行）");
        check(frame.value("pxTop").toInt() == -16, "ypos=-100 -> pxTop == -16（一行高）");
        check(face.value("pxTop").toInt() == 0, "立絵本身没有 ypos");
        check(frame.value("row").toInt() * 16 + frame.value("pxTop").toInt()
                  == face.value("row").toInt() * 16 + face.value("pxTop").toInt(),
              "框与立絵同一纵坐标（像素位移正好抵消一行）");
    }

    // ---- 5b. 舞台快照的图锚（updateStageModels 的含图行索引）----
    // 此前每次 flush 都扫 0..viewportStart 的全部历史行找 Image span（backlog
    // 上限 5000 行，eraMegaten 配置 50000）。改为 publishLine 维护「含图行」
    // serial 集合 + updateStageModels 只看含图行。这里锁定**行为不变**：
    // 锚定行被拉进快照、纯文本行不被误认、clearAll 后索引复位。
    qDebug() << "\n5b) 舞台快照的图锚（含图行索引）";
    {
        // 纯文本基线：12 行 / 舞台 5 行 -> 快照从第 7 行起
        {
            ConsoleBackend c;
            c.setFontSize(16);
            c.setLineHeight(16);
            c.setGridColumns(40);
            c.setGridRows(5);
            c.setStageMode(true);
            for (int i = 0; i < 12; ++i) { c.print(QStringLiteral("A%1").arg(i)); c.newline(); }
            c.flush();
            check(c.stageStart() == 7, "纯文本：stageStart == 7（12 - 5）");
            check(c.stageModel()->rowCount() == 5, "纯文本：快照只有视口 5 行");
        }
        // 视口上方（第 3 行）的 8 行高立絵：图像底部盖进视口 -> 锚定行拉进快照
        {
            ConsoleBackend c;
            c.setFontSize(16);
            c.setLineHeight(16);
            c.setGridColumns(40);
            c.setGridRows(5);
            c.setStageMode(true);
            for (int i = 0; i < 3; ++i) { c.print(QStringLiteral("B%1").arg(i)); c.newline(); }
            c.printImage(QStringLiteral("tall_face"), 400, 800); c.newline();   // 行 3：8 行高
            for (int i = 0; i < 8; ++i) { c.print(QStringLiteral("C%1").arg(i)); c.newline(); }
            check(c.lineCount() == 12, "12 行（图占 1 个逻辑行）");
            c.flush();
            check(c.stageStart() == 3, "图锚：视口上方 8 行高的立絵把锚定行 3 拉进快照");
            check(c.stageModel()->rowCount() == 9, "图锚：快照 9 行（3..11）");
            // 更多文本行把视口推到锚定行之外（不再盖到）-> 锚定行不再被拉进
            for (int i = 0; i < 6; ++i) { c.print(QStringLiteral("D%1").arg(i)); c.newline(); }
            c.flush();
            check(c.stageStart() == 13, "图锚出界：18 - 5 == 13（锚不再盖视口）");
        }
        // clearAll 后索引复位：之前的图行 serial 全部失效，不残留
        {
            ConsoleBackend c;
            c.setFontSize(16);
            c.setLineHeight(16);
            c.setGridColumns(40);
            c.setGridRows(5);
            c.setStageMode(true);
            for (int i = 0; i < 3; ++i) { c.print(QStringLiteral("E%1").arg(i)); c.newline(); }
            c.printImage(QStringLiteral("gone_face"), 400, 800); c.newline();
            for (int i = 0; i < 8; ++i) { c.print(QStringLiteral("F%1").arg(i)); c.newline(); }
            c.flush();
            check(c.stageStart() == 3, "清屏前：图锚生效");
            c.clearAll();
            for (int i = 0; i < 10; ++i) { c.print(QStringLiteral("G%1").arg(i)); c.newline(); }
            c.flush();
            check(c.stageStart() == 5, "clearAll 后：索引复位，stageStart == 5（10 - 5）");
        }
    }

    // ---- 5c. 输入回显行（正文最底一行移交 QML 输入栏）----
    // 输入框不在舞台上（QML 的 ConsoleInput 是独立控件）：预留输入行时舞台快照
    // 止于倒数第二行，最底一行由 inputEchoLine 单独递交。C# 的窗口最底一行就是
    // richTextBox1（输入框），era 脚本也会往那一行打提示/按钮。
    qDebug() << "\n5c) 输入回显行（最底一行不进军台快照）";
    {
        ConsoleBackend c;
        c.setFontSize(16);
        c.setLineHeight(16);
        c.setGridColumns(40);
        c.setGridRows(5);
        c.setStageMode(true);
        for (int i = 0; i < 12; ++i) { c.print(QStringLiteral("H%1").arg(i)); c.newline(); }
        c.flush();
        check(c.stageStart() == 7 && c.stageModel()->rowCount() == 5,
              "未预留输入行：舞台照旧占满 5 行（最底一行仍在舞台上）");
        check(c.inputEchoLine().isEmpty(),
              "未预留输入行：不递交回显行（否则与舞台重复）");

        int echoes = 0;
        QObject::connect(&c, &ConsoleBackend::inputEchoLineChanged, [&] { ++echoes; });
        c.setStageInputRowReserved(true);
        check(echoes >= 1, "预留输入行：递交回显行（信号）");
        check(c.stageModel()->rowCount() == 4, "预留输入行：舞台快照少一行（5 → 4）");
        check(c.stageStart() == 7, "预留输入行：舞台起点 12 - 1 - 4 = 7（内容不整体上移）");
        check(textOf(c.inputEchoLine(), QStringLiteral("H11")) == QStringLiteral("H11"),
              "回显行 = 正文最底一行（H11）");
        const QVariantList stageLast =
            c.stageModel()->data(c.stageModel()->index(3, 0),
                                 ConsoleBackend::BlocksRole).toList();
        check(textOf(stageLast, QStringLiteral("H11")).isEmpty()
              && textOf(stageLast, QStringLiteral("H10")) == QStringLiteral("H10"),
              "舞台最后一行是 H10，H11 只在回显行");

        // 未定型尾行（print 后未 newline）同样属于「最底一行」
        c.clearAll();
        for (int i = 0; i < 5; ++i) { c.print(QStringLiteral("K%1").arg(i)); c.newline(); }
        c.print(QStringLiteral("＞"));
        c.flush();
        check(textOf(c.inputEchoLine(), QStringLiteral("＞")) == QStringLiteral("＞"),
              "未定型尾行（＞）立刻成为回显行");
        check(c.stageModel()->rowCount() == 4, "舞台仍是 4 行（不作待输入的尾行留在上面）");

        // 回显行上的按钮保持可点（与正文同源的 generation/clickable 数据）
        c.clearAll();
        c.notifyInputRequested(QStringLiteral("INPUT"));
        for (int i = 0; i < 5; ++i) { c.print(QStringLiteral("M%1").arg(i)); c.newline(); }
        c.printButton(QStringLiteral("[0] 戻る"), 0);
        c.newline();
        c.flush();
        const QVariantMap echoButton = blockOf(c.inputEchoLine(), QStringLiteral("[0] 戻る"));
        check(!echoButton.isEmpty() && echoButton.value("clickable").toBool(),
              "回显行按钮带点击数据（可在输入栏里点）");
        check((echoButton.value("generation").toLongLong()) == qint64(c.generation()),
              "回显行按钮的世代 == 当前世代（未被 BREAKBUTTON 作废）");
        c.notifyInputDone();
    }
    // 不溢出时不摘行：eraTW 标题是 45 行定页画面（gridRows 46 - 输入行 = 45），
    // 全部行都在舞台上，回显栏为空 —— 否则标题会少一行、把最后的按钮挤进输入栏。
    {
        ConsoleBackend c;
        c.setFontSize(16);
        c.setLineHeight(16);
        c.setGridColumns(40);
        c.setGridRows(6);              // 舞台 5 行
        c.setStageMode(true);
        c.setStageInputRowReserved(true);
        for (int i = 0; i < 5; ++i) { c.print(QStringLiteral("T%1").arg(i)); c.newline(); }
        c.flush();
        check(c.inputEchoLine().isEmpty(), "正好装满舞台：不摘行、回显栏为空");
        check(c.stageStart() == 0 && c.stageModel()->rowCount() == 5,
              "正好装满舞台：5 行全在舞台上（自第 0 行起）");
        c.print(QStringLiteral("[1] 存檔再開"));
        c.newline();
        c.flush();
        check(textOf(c.inputEchoLine(), QStringLiteral("[1] 存檔再開"))
                  == QStringLiteral("[1] 存檔再開"),
              "溢出 1 行：最新一行进回显栏");
        check(c.stageStart() == 0 && c.stageModel()->rowCount() == 5,
              "溢出后舞台仍是 5 行（T0..T4 全在舞台上，最新一行不挤上去）");
    }

    // ---- 8. 工具提示样式（TOOLTIP_*）与 BREAKBUTTON ----
    qDebug() << "\n8) 工具提示样式（TOOLTIP_*）/ BREAKBUTTON";
    {
        ConsoleBackend c;
        // 默认值对齐 WinForms ToolTip：InitialDelay 500ms / AutoPopDelay 5000ms
        check(c.toolTipDelay() == 500 && c.toolTipDuration() == 5000,
              "工具提示默认延迟 500ms / 时长 5000ms（WinForms 缺省）");
        check(!c.toolTipCustom() && c.toolTipPointSize() == 0,
              "默认非 OwnerDraw、未指定字号");
        int changed = 0;
        QObject::connect(&c, &ConsoleBackend::toolTipStyleChanged, [&] { ++changed; });
        // TOOLTIP_SETFONT / TOOLTIP_SETFONTSIZE（C# SetToolTipFontName / FontSize，磅）
        // 一条指令 = 一个字段 = 一次写入（信号按字段拆，见 ExecutionEngine）
        c.setToolTipFontFamily(QStringLiteral("ＭＳ ゴシック"));
        check(c.toolTipFontFamily().isEmpty() == false
              && c.toolTipFontFamily() == QStringLiteral("ＭＳ ゴシック"),
              "SETFONT 生效");
        check(changed == 1, "一次变更发一次 toolTipStyleChanged");
        c.setToolTipPointSize(10);
        check(c.toolTipPointSize() == 10, "SETFONTSIZE 生效（磅）");
        check(changed == 2, "字号变更同样只发一次 toolTipStyleChanged");
        // TOOLTIP_CUSTOM / TOOLTIP_FORMAT（OwnerDraw 与 TextFormatFlags）
        c.setToolTipCustom(true);
        c.setToolTipFormat(0);
        check(c.toolTipCustom() && changed == 3, "SETCUSTOM 生效（OwnerDraw）");
        // TOOLTIP_SETDELAY / TOOLTIP_SETDURATION
        c.setToolTipDelay(120);
        c.setToolTipDuration(300);
        check(c.toolTipDelay() == 120 && c.toolTipDuration() == 300, "SETDELAY/SETDURATION 生效");
        // TOOLTIP_SETCOLOR（ARGB 前景/背景）
        c.setToolTipColors(QColor(0xFF, 0xFF, 0x00), QColor(0, 0, 0));
        check(c.toolTipForeColor() == QColor(0xFF, 0xFF, 0x00)
              && c.toolTipBackColor() == QColor(0, 0, 0), "SETCOLOR 生效");

        // BREAKBUTTON：世代号自增 -> 屏上旧按钮点击失效
        ConsoleBackend b;
        b.printButton(QStringLiteral("btn"), 7);
        b.flush();
        const quint64 before = b.generation();
        check(b.hasEnabledButton(), "BREAKBUTTON 前按钮可点");
        b.breakButtons();
        check(b.generation() == before + 1, "BREAKBUTTON 令世代号 +1");
        check(!b.hasEnabledButton(), "BREAKBUTTON 后旧按钮不再可点");
    }

    // ---- 7. HTML_PRINT 的打印缓冲模式（C# PrintHtml(str, toPrintBuffer)）----
    qDebug() << "\n7) HTML_PRINT 缓冲模式（printTemplateBuffered）";
    {
        ConsoleBackend c;
        // 模板 = 「[」+ 文本 X + 「]」
        PrintTemplate html;
        PrintTemplatePart open;  open.kind = PrintTemplatePart::Kind::Text;  open.text = "[";
        PrintTemplatePart body;  body.kind = PrintTemplatePart::Kind::Text;  body.text = "X";
        PrintTemplatePart close; close.kind = PrintTemplatePart::Kind::Text; close.text = "]";
        html.parts = { open, body, close };

        // 非缓冲（默认）：先冲刷挂起的当前行，HTML 自成一行（HTML_PRINT 自动换行）
        c.print(QStringLiteral("A"));
        c.printTemplate(html);
        check(c.lineCount() == 2 && c.lineText(0) == "A" && c.lineText(1) == "[X]",
              QString("非缓冲 HTML_PRINT：A 与 [X] 各成一行（实得 %1 行：%2|%3）")
                  .arg(c.lineCount()).arg(c.lineText(0)).arg(c.lineText(1)));

        // 缓冲（第 2 实参非 0）：挂进当前行 —— 前后 print 接在同一行
        ConsoleBackend b;
        b.print(QStringLiteral("A"));
        b.printTemplateBuffered(html);
        // 观察口径：lineCount 只数**已定型**行；挂起行在模型里是**尾行**
        //（print 打开后由 flush 发布 —— 生产环境由帧节拍驱动）
        b.flush();
        check(b.lineCount() == 0 && b.rowCount() == 1,
              QString("缓冲 HTML_PRINT 不冲刷：未定型行仍只有 1 个（lineCount %1 / rowCount %2）")
                  .arg(b.lineCount()).arg(b.rowCount()));
        b.print(QStringLiteral("B"));
        b.newline();
        check(b.lineCount() == 1 && b.lineText(0) == "A[X]B",
              QString("缓冲模式与后续 PRINT 合成同一行（实得 %1 行：%2）")
                  .arg(b.lineCount()).arg(b.lineText(0)));
        // 下一次**非缓冲** HTML_PRINT 把挂起的缓冲行先发布（C# 的 Flush 时机）：
        //   挂起的 "C" 先成一行（不与 HTML 合并），[X] 再自成一行
        b.print(QStringLiteral("C"));
        b.printTemplate(html);
        check(b.lineCount() == 3 && b.lineText(0) == "A[X]B"
              && b.lineText(1) == "C" && b.lineText(2) == "[X]",
              QString("非缓冲输出先冲刷挂起行，HTML 仍自成一行（实得 %1 行：%2|%3|%4）")
                  .arg(b.lineCount()).arg(b.lineText(0)).arg(b.lineText(1)).arg(b.lineText(2)));
        // 缓冲模板内的 <br>：中间仍然断行，只有尾部留在未定型行
        ConsoleBackend d;
        PrintTemplate br;
        PrintTemplatePart t1; t1.kind = PrintTemplatePart::Kind::Text; t1.text = "1";
        PrintTemplatePart brk; brk.kind = PrintTemplatePart::Kind::Break;
        PrintTemplatePart t2; t2.kind = PrintTemplatePart::Kind::Text; t2.text = "2";
        br.parts = { t1, brk, t2 };
        d.printTemplateBuffered(br);
        d.print(QStringLiteral("3"));
        d.newline();
        check(d.lineCount() == 2 && d.lineText(0) == "1" && d.lineText(1) == "23",
              QString("缓冲模板内的 <br> 照常断行，尾段留在当前行（实得 %1|%2）")
                  .arg(d.lineText(0)).arg(d.lineText(1)));
    }

    // ---- 6. 空输入语义（对齐 C# doInputToEmueraProgram）----
    qDebug() << "\n空输入语义";
    {
        ConsoleBackend c;
        qint64 got = -999;
        QString gotStr = QStringLiteral("<none>");
        int intCount = 0, strCount = 0;
        QObject::connect(&c, &ConsoleBackend::inputSubmitted,
                         [&](qint64 v) { got = v; ++intCount; });
        QObject::connect(&c, &ConsoleBackend::inputSubmittedString,
                         [&](const QString& v) { gotStr = v; ++strCount; });

        // ① 无参 INPUT：空回车必须**什么都不发生**
        c.notifyInputRequested(QStringLiteral("INPUT"));
        c.submitIntegerText(QString());
        check(intCount == 0, "无缺省值的 INPUT：空回车被忽略（不交付）");

        // ② 非法文本同样忽略
        c.submitIntegerText(QStringLiteral("abc"));
        check(intCount == 0, "非整数文本被忽略（C# 的 Int64.TryParse 失败）");

        // ③ 正常整数照常交付
        c.submitIntegerText(QStringLiteral("7"));
        check(intCount == 1 && got == 7, "正常整数 7 照常交付");

        // ④ TINPUT 带缺省：空回车交缺省值（不是 0）
        c.notifyInputRequested(QStringLiteral("TINPUT"), QVariant(qint64(1234)));
        c.submitIntegerText(QString());
        check(intCount == 2 && got == 1234, "TINPUT 缺省 1234：空回车交 1234");

        // ⑤ INPUT 带实参：空回车交该缺省值
        c.notifyInputRequested(QStringLiteral("INPUT"), QVariant(qint64(5)));
        c.submitIntegerText(QString());
        check(intCount == 3 && got == 5, "INPUT 5：空回车交 5");

        // ⑥ 字符串型：空回车同样按缺省值
        c.notifyInputRequested(QStringLiteral("TINPUTS"), QVariant(QStringLiteral("-1")));
        c.submitStringText(QString());
        check(strCount == 1 && gotStr == QLatin1String("-1"),
              "TINPUTS 缺省 \"-1\"：空回车交 \"-1\"");

        // ⑦ kind 必须是输入类型而不是系统状态名（QML 提示文本与校验路由都用它）
        check(c.inputKind() == QLatin1String("TINPUTS") && c.waitingInput(),
              "inputKind 是输入类型（TINPUTS），不是系统状态名");
    }

    // ---- 单字符即时输入（C# InputRequest.OneInput / doInputToEmueraProgram）----
    qDebug() << "\n单字符即时输入（OneInput）";
    {
        ConsoleBackend c;
        check(!c.oneInput(), "没有等待输入时不是 OneInput");

        // TONEINPUT：单字符即时输入（eraTetris 的限时按钮/按键循环）
        c.notifyInputRequested(QStringLiteral("TONEINPUT"));
        check(c.oneInput(), "TONEINPUT 是 OneInput");
        qint64 gotInt = -1;
        int intCount = 0;
        QObject::connect(&c, &ConsoleBackend::inputSubmitted,
                         [&](qint64 v) { gotInt = v; ++intCount; });
        c.submitIntegerText(QStringLiteral("12"));
        check(intCount == 1 && gotInt == 1,
              "OneInput 的键盘输入只取第 1 个字符（12 -> 1）");

        // ONEINPUT / ONEINPUTS 同样是 OneInput
        c.notifyInputDone();
        c.notifyInputRequested(QStringLiteral("ONEINPUT"));
        check(c.oneInput(), "ONEINPUT 是 OneInput");
        c.notifyInputDone();
        c.notifyInputRequested(QStringLiteral("TONEINPUTS"));
        check(c.oneInput(), "TONEINPUTS 是 OneInput");

        QString gotStr;
        int strCount = 0;
        QObject::connect(&c, &ConsoleBackend::inputSubmittedString,
                         [&](const QString& v) { gotStr = v; ++strCount; });
        c.submitStringText(QStringLiteral("ab"));
        check(strCount == 1 && gotStr == QLatin1String("a"),
              "OneInput 的字符串输入只取第 1 个字符（ab -> a）");

        // 普通（非 OneInput）输入保持原样：多字符不截断
        c.notifyInputDone();
        c.notifyInputRequested(QStringLiteral("TINPUT"));
        check(!c.oneInput(), "TINPUT 不是 OneInput");
        gotInt = -1;
        c.submitIntegerText(QStringLiteral("12"));
        check(gotInt == 12, "普通 TINPUT 不截断（12 -> 12）");
        c.notifyInputDone();
        c.notifyInputRequested(QStringLiteral("INPUTS"));
        check(!c.oneInput(), "INPUTS 不是 OneInput");
        gotStr.clear();
        c.submitStringText(QStringLiteral("ab"));
        check(gotStr == QLatin1String("ab"), "普通 INPUTS 不截断（ab -> ab）");
    }

    // ---- 帧呈现闸门（C# need_settimer：计时器等这一帧画完再启动）----
    qDebug() << "\n帧呈现闸门";
    {
        ConsoleBackend c;
        int presented = 0;
        int asked = 0;
        quint64 firstGeneration = 0;
        quint64 secondGeneration = 0;
        QObject::connect(&c, &ConsoleBackend::framePresented, [&] { ++presented; });
        QObject::connect(&c, &ConsoleBackend::presentationRequested,
                         [&](quint64 generation) {
            ++asked;
            if (firstGeneration == 0) firstGeneration = generation;
            else secondGeneration = generation;
        });

        c.notifyInputRequested(QStringLiteral("TINPUT"));
        check(asked == 0, "没有 QML 视图时不请求呈现（CLI 立即计时）");

        c.setFramePacingAttached(true);
        c.notifyInputRequested(QStringLiteral("TINPUT"));
        check(asked == 1 && firstGeneration != 0, "有视图时请求一次呈现并取得 token");
        check(presented == 0, "视图还没回报就不放行");
        c.notifyFramePresented(firstGeneration);
        check(presented == 1, "视图回报后放行计时器");

        // 旧 QML callLater 不能确认新一轮请求；同一请求的重复回报也只能生效一次。
        c.notifyInputRequested(QStringLiteral("TINPUT"));
        check(asked == 2 && secondGeneration != firstGeneration, "新请求使用新 token");
        c.notifyFramePresented(firstGeneration);
        check(presented == 1, "旧 token 不会提前放行新请求");
        c.notifyFramePresented(secondGeneration);
        check(presented == 2, "当前 token 放行新请求");
        c.notifyFramePresented(secondGeneration);
        check(presented == 2, "重复回报不会重复放行");

        // 兜底：视图没能回报（销毁/未接线）时也必须放行，否则限时输入会卡死
        c.notifyInputRequested(QStringLiteral("TINPUT"));
        QEventLoop loop;
        QTimer::singleShot(200, &loop, &QEventLoop::quit);
        loop.exec();
        check(presented == 3, "视图未回报时 50ms 兜底放行");
    }

    // ---- 9. SETBGIMAGE 族与 HTML_PRINT_ISLAND ----
    qDebug() << "\n9) SETBGIMAGE 族 / HTML_PRINT_ISLAND";
    {
        // SETBGIMAGE：深度排序（降序：深度大的在列表前 = 底层）+ 移除/清空
        ConsoleBackend c;
        c.addBackgroundImage("bg_a", 0, 255);
        c.addBackgroundImage("bg_b", 10, 128);
        c.addBackgroundImage("bg_c", 5, 255);
        QVariantList bgs = c.backgroundImages();
        check(bgs.size() == 3
              && bgs.at(0).toMap().value("name").toString() == "bg_b"      // 深度 10 最底
              && bgs.at(1).toMap().value("name").toString() == "bg_c"
              && bgs.at(2).toMap().value("name").toString() == "bg_a"
              && bgs.at(0).toMap().value("alpha").toInt() == 128,
              "SETBGIMAGE 按深度降序列表（深度大 = 底层）、alpha 保留");
        c.removeBackgroundImage("BG_A");   // 大小写不敏感（C# GetSprite 统一大写）
        check(c.backgroundImages().size() == 2, "REMOVEBGIMAGE 按名移除（大小写不敏感）");
        c.clearBackgroundImages();
        check(c.backgroundImages().isEmpty(), "CLEARBGIMAGE 清空全部");

        // PRINTD 系：printIgnoringSetColor 让这一段不带 color（QML 落到 foreColor），
        // 且不改动已设定的 SETCOLOR 状态。
        {
            ConsoleBackend d;
            d.setColor(QColor(0xff, 0x00, 0x00));
            d.print("red");
            d.printIgnoringSetColor("plain");
            d.print("red2");
            d.newline();
            check(d.lineCount() == 1 && d.lineText(0) == QStringLiteral("redplainred2"),
                  "printIgnoringSetColor 与前后文接在同一行");
            const QVariantList blocks =
                d.data(d.index(0), ConsoleBackend::BlocksRole).toList();
            bool sawColor = false, plainHasColor = true, tailHasColor = false;
            for (const QVariant& v : blocks) {
                const QVariantMap m = v.toMap();
                const QString text = m.value("text").toString();
                if (text == QLatin1String("red")) sawColor = m.contains("color");
                else if (text == QLatin1String("plain")) plainHasColor = m.contains("color");
                else if (text == QLatin1String("red2")) tailHasColor = m.contains("color");
            }
            check(sawColor && !plainHasColor && tailHasColor,
                  "PRINTD 只让本段不带 SETCOLOR 颜色（前后段仍带）");
        }

        // HTML_PRINT_ISLAND：独立层 —— 不进正文缓冲、不影响挂起行；CLEAR 清空
        ConsoleBackend b;
        b.print("A");                    // 挂起的正文行
        PrintTemplate island;
        PrintTemplatePart t1; t1.kind = PrintTemplatePart::Kind::Text; t1.text = "HUD";
        PrintTemplatePart brk; brk.kind = PrintTemplatePart::Kind::Break;
        PrintTemplatePart t2; t2.kind = PrintTemplatePart::Kind::Text; t2.text = "line2";
        island.parts = { t1, brk, t2 };
        b.printHtmlIsland(island);
        check(b.lineCount() == 0,
              "HTML_PRINT_ISLAND 不动正文（已定型行数仍为 0）");
        const QVariantList rows = b.htmlIsland();
        check(rows.size() == 2
              && rows.at(0).toMap().value("blocks").toList().at(0).toMap().value("text").toString() == "HUD"
              && rows.at(1).toMap().value("blocks").toList().at(0).toMap().value("text").toString() == "line2",
              "岛层两行（<br> 断行），blocks 与正文行同一形状");
        b.print("B");
        b.newline();
        check(b.lineCount() == 1 && b.lineText(0) == "AB",
              "岛层之后正文照常续行（AB 未被岛内容打断）");
        b.clearHtmlIsland();
        check(b.htmlIsland().isEmpty(), "HTML_PRINT_ISLAND_CLEAR 清空岛层");
        b.clearAll();
        b.printHtmlIsland(island);
        check(!b.htmlIsland().isEmpty(), "岛层可再次使用");
        b.clearAll();
        check(b.htmlIsland().isEmpty(), "clearAll 清屏同时清岛层（C# ClearDisplay）");
    }

    // ---- 10. `<div rect>` 的像素左边界（不允许逐段取整累进）----
    // FL3.8 肖像编辑器把 10 张 80px 缩略图整体放进一个 div：
    //   `<div rect='{COL*80}px,{ROW*80}px,80px,80px'>`
    // 若把每段的 division.x 单独 qRound(80/7) 成列，会得到
    // 0,11,23,34,46,57 —— 第 10 张比真实位置少 3 格（21px）。
    // 现在 flattenLine 直接给**累计像素** pxLeft，QML 只折回一次。
    qDebug() << "\n10) HTML div 连续像素列（80px 缩略图不累进）";
    {
        ConsoleBackend c;
        c.setFontSize(14);          // columnWidthPx() == 7
        c.setLineHeight(14);
        c.setGridColumns(226);
        c.setGridRows(60);
        QString html;
        for (int col = 0; col < 10; ++col) {
            html += QStringLiteral("<div rect='%1px,0px,80px,80px'>"
                                   "<img src='thumb%2' width='80px' height='80px'>"
                                   "</div>")
                        .arg(col * 80).arg(col);
        }
        c.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(html));
        c.flush();
        const QVariantList blocks = c.lineBlocks(0);
        QList<int> pxLefts;
        for (const QVariant& v : blocks) {
            const QVariantMap m = v.toMap();
            if (m.value("kind").toString() != "image") continue;
            pxLefts.append(m.value("pxLeft").toInt());
        }
        check(pxLefts.size() == 10,
              QString("10 张缩略图都在同一行（实得 %1）").arg(pxLefts.size()));
        QList<int> expected;
        for (int col = 0; col < 10; ++col) expected.append(col * 80);
        check(pxLefts == expected,
              QString("pxLeft 是累计像素 0,80,...,720（实得 %1）")
                  .arg([&] {
                      QStringList s;
                      for (int v : pxLefts) s << QString::number(v);
                      return s.join(',');
                  }()));
        // 反证：旧的 col 口径会累进（0,11,23,34,46,57），必须与像素口径分离。
        QList<int> cols;
        for (const QVariant& v : blocks) {
            const QVariantMap m = v.toMap();
            if (m.value("kind").toString() != "image") continue;
            cols.append(m.value("col").toInt());
        }
        // col 是「80px / 7px = 11.43」逐段取整后的列：步进 11/12 交替，
        // 换算回像素会得到 0,77,161,238,… —— 与 pxLeft 的 0,80,160,240,… 不同。
        // 这正是 QML 必须用 pxLeft（像素）而不是 col（量化列）的原因。
        QList<int> colPx;
        for (int c : cols) colPx.append(c * 7);
        check(colPx != pxLefts,
              QString("col × 列宽 != pxLeft（量化列 %1）")
                  .arg([&] {
                      QStringList s;
                      for (int v : colPx) s << QString::number(v);
                      return s.join(',');
                  }()));
        // 没有 div 定位的普通区块：pxLeft == -1（继续按网格列定位）。
        ConsoleBackend d;
        d.setFontSize(14);
        d.setLineHeight(14);
        d.setGridColumns(226);
        d.setGridRows(60);
        d.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(
            QStringLiteral("<img src='plain' width='80px' height='80px'>")));
        d.flush();
        const QVariantMap plain = blockOf(d.lineBlocks(0), "plain");
        check(!plain.isEmpty() && plain.value("pxLeft").toInt() == -1,
              "非 div 区块 pxLeft == -1（走 col × 格宽）");
    }

    // ---- 10c. div 超出 rect 的绘制裁剪（C# graph.SetClip(内容框)）----
    // C# ConsoleDivPart.DrawTo 里 SetClip 只影响**绘制**：子内容越过 rect 就被裁掉，
    // 排版推进不变。C++ 侧给出「本区块还剩多少可见宽/高」（引擎像素，-1 = 不裁），
    // QML 再把区块自己的宽高收进这个上限（clip: true）。
    qDebug() << "\n10c) div 超出 rect 的绘制裁剪（C# SetClip）";
    {
        ConsoleBackend c;
        c.setFontSize(14);          // 列宽 7px、行高 14px
        c.setLineHeight(14);
        c.setGridColumns(226);
        c.setGridRows(60);
        // rect 200×60 里放一张 300px 宽的图：横向必须裁到 200px（纵向正好 60px）。
        c.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(
            QStringLiteral("<div rect='0px,0px,200px,60px'>"
                           "<img src='wide' width='300px' height='60px'></div>")));
        c.flush();
        const QVariantMap wide = blockOf(c.lineBlocks(0), "wide");
        check(!wide.isEmpty(), "rect 内的图区块存在");
        check(wide.value("pxWidth").toInt() == 300,
              QString("图片原始宽仍是 300px（实得 %1）").arg(wide.value("pxWidth").toInt()));
        check(wide.value("clipWidth").toInt() == 200,
              QString("超出 rect 的绘制被裁到 200px（实得 %1）").arg(wide.value("clipWidth").toInt()));
        check(wide.value("clipHeight").toInt() == 60,
              QString("纵向裁剪高 = rect 高 60px（实得 %1）").arg(wide.value("clipHeight").toInt()));

        // padding 四边各 10px：内容框 180×40，左边界同时内缩到 10px。
        ConsoleBackend p;
        p.setFontSize(14);
        p.setLineHeight(14);
        p.setGridColumns(226);
        p.setGridRows(60);
        p.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(
            QStringLiteral("<div rect='0px,0px,200px,60px' padding='10px'>"
                           "<img src='pad' width='300px' height='60px'></div>")));
        p.flush();
        const QVariantMap pad = blockOf(p.lineBlocks(0), "pad");
        check(pad.value("pxLeft").toInt() == 10,
              QString("padding 内缩子内容起点到 10px（实得 %1）").arg(pad.value("pxLeft").toInt()));
        check(pad.value("clipWidth").toInt() == 180,
              QString("裁剪宽 = rect - 左右 padding = 180px（实得 %1）").arg(pad.value("clipWidth").toInt()));
        check(pad.value("clipHeight").toInt() == 40,
              QString("裁剪高 = rect - 上下 padding = 40px（实得 %1）").arg(pad.value("clipHeight").toInt()));

        // 同一 div 里第二张图已经越过右边界一半：只剩 50px 可见。
        ConsoleBackend s;
        s.setFontSize(14);
        s.setLineHeight(14);
        s.setGridColumns(226);
        s.setGridRows(60);
        s.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(
            QStringLiteral("<div rect='0px,0px,200px,60px'>"
                           "<img src='first' width='150px' height='60px'>"
                           "<img src='second' width='150px' height='60px'></div>")));
        s.flush();
        const QVariantMap second = blockOf(s.lineBlocks(0), "second");
        check(second.value("pxLeft").toInt() == 150,
              QString("第二张图从 150px 起（实得 %1）").arg(second.value("pxLeft").toInt()));
        check(second.value("clipWidth").toInt() == 50,
              QString("越过右边界的部分被裁掉（clipWidth 实得 %1）").arg(second.value("clipWidth").toInt()));

        // 没给 width/height 的 <div> 只是分组：不裁剪；div 外的正文也不裁剪。
        ConsoleBackend d;
        d.setFontSize(14);
        d.setLineHeight(14);
        d.setGridColumns(226);
        d.setGridRows(60);
        d.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(
            QStringLiteral("<div><img src='nodim' width='300px' height='60px'></div>")));
        d.printPlain(QStringLiteral("tail"));
        d.flush();
        const QVariantMap nodim = blockOf(d.lineBlocks(0), "nodim");
        check(nodim.value("clipWidth").toInt() == -1 && nodim.value("clipHeight").toInt() == -1,
              "没给 rect 尺寸的 <div> 不裁剪（clipWidth/clipHeight == -1）");
    }

    // ---- 10e. div 自身的盒子（C# BoxBorder.DrawBorder）----
    // FL3.8 拠点地图的每格就是 `<div rect='…' border='2px' bcolor='#C0C0C0'>`：
    // 只画文字不画盒子，格子就只剩孤零零的标签（实机截图上整张地图是空的）。
    // C++ 把盒子拆成「背景填充 1 块 + 四边 4 条」，几何用**引擎像素**。
    qDebug() << "\n10e) div 自身的盒子（C# BoxBorder.DrawBorder）";
    {
        ConsoleBackend c;
        c.setFontSize(14); c.setLineHeight(14);
        c.setGridColumns(226); c.setGridRows(60);
        c.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(
            QStringLiteral("<div rect='100px,0px,200px,60px' border='2px' "
                           "bcolor='#C0C0C0' color='#112233'>A</div>")));
        c.flush();
        QList<QVariantMap> boxes;
        QVariantMap label;
        for (int r = 0; r < c.rowCount(); ++r) {
            for (const QVariant& v : c.lineBlocks(r)) {
                const auto m = v.toMap();
                if (m.value("kind").toString() == "box") boxes.append(m);
                if (m.value("text").toString() == "A") label = m;
            }
        }
        check(boxes.size() == 5,
              QString("背景 1 块 + 四边 4 条 = 5 个盒子区块（实得 %1）").arg(boxes.size()));
        QVariantMap fill, top, bottom, left, right;
        for (const auto& b : boxes) {
            if (b.value("color").toString().compare(QStringLiteral("#ff112233"),
                                                    Qt::CaseInsensitive) == 0) fill = b;
            else if (b.value("boxW").toInt() == 200 && b.value("boxH").toInt() == 2)
                (b.value("pxTop").toInt() == 0 ? top : bottom) = b;
            else if (b.value("boxW").toInt() == 2) (b.value("pxLeft").toInt() == 100 ? left : right) = b;
        }
        check(!fill.isEmpty(), "color 属性画出背景填充块");
        if (!fill.isEmpty()) {
            check(fill.value("boxW").toInt() == 200 && fill.value("boxH").toInt() == 60,
                  QString("背景填充铺满 rect 200x60（实得 %1x%2）")
                      .arg(fill.value("boxW").toInt()).arg(fill.value("boxH").toInt()));
            check(fill.value("pxLeft").toInt() == 100,
                  QString("背景从 rect 左边界 100px 起（实得 %1）").arg(fill.value("pxLeft").toInt()));
        }
        check(!top.isEmpty() && !bottom.isEmpty(),
              "bcolor 的上/下边框条各一条（2px 高，与 rect 同宽）");
        if (!top.isEmpty())
            check(top.value("pxTop").toInt() == 0, "上边框在 rect 顶部");
        if (!bottom.isEmpty())
            check(bottom.value("pxTop").toInt() == 58,
                  QString("下边框贴 rect 底部（pxTop 实得 %1）").arg(bottom.value("pxTop").toInt()));
        check(!left.isEmpty() && !right.isEmpty(),
              "bcolor 的左/右边框条各一条（2px 宽）");
        if (!right.isEmpty())
            check(right.value("pxLeft").toInt() == 298,
                  QString("右边框贴 rect 右边界（pxLeft 实得 %1）").arg(right.value("pxLeft").toInt()));
        check(label.value("pxLeft").toInt() == 102,
              QString("子内容再内缩 border 2px（pxLeft 实得 %1）").arg(label.value("pxLeft").toInt()));

        // 只给 bcolor 不给 color：不画背景，只画四条边。
        ConsoleBackend n;
        n.setFontSize(14); n.setLineHeight(14);
        n.setGridColumns(226); n.setGridRows(60);
        n.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(
            QStringLiteral("<div rect='0px,0px,100px,40px' border='1px' bcolor='#FF0000'>B</div>")));
        n.flush();
        int count = 0;
        for (int r = 0; r < n.rowCount(); ++r)
            for (const QVariant& v : n.lineBlocks(r))
                if (v.toMap().value("kind").toString() == "box") ++count;
        check(count == 4, QString("没有 color 就只有 4 条边（实得 %1）").arg(count));

        // 10f) radius 圆角：背景块带 per-corner 半径，四条边框条两端让出圆角。
        qDebug() << "\n10f) radius 圆角（C# BoxBorder 的 radius 分支）";
        ConsoleBackend rad;
        rad.setFontSize(14); rad.setLineHeight(14);
        rad.setGridColumns(226); rad.setGridRows(60);
        rad.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(
            QStringLiteral("<div rect='0px,0px,100px,50px' border='2px' bcolor='#00FF00' "
                           "color='#223344' radius='10px'>R</div>")));
        rad.flush();
        QVariantMap rfill, rtop;
        for (int r = 0; r < rad.rowCount(); ++r)
            for (const QVariant& v : rad.lineBlocks(r)) {
                const auto m = v.toMap();
                if (m.value("kind").toString() != "box") continue;
                if (m.contains("radii")) { rfill = m; continue; }
                // 上边框条：2px 高、从顶部起、两端各让出 10px -> 宽 80
                if (m.value("boxH").toInt() == 2 && m.value("pxTop").toInt() == 0) rtop = m;
            }
        check(!rfill.isEmpty(), "radius 让背景块带上圆角信息");
        if (!rfill.isEmpty()) {
            const QVariantList rr = rfill.value("radii").toList();
            check(rr.size() == 4 && rr.at(0).toInt() == 10 && rr.at(3).toInt() == 10,
                  QString("四角半径都取 10px（实得 %1 个角）").arg(rr.size()));
        }
        check(!rtop.isEmpty() && rtop.value("boxW").toInt() == 80
              && rtop.value("pxLeft").toInt() == 10,
              QString("上边框条两端各让出 10px 圆角（宽 %1，左起 %2）")
                  .arg(rtop.value("boxW").toInt()).arg(rtop.value("pxLeft").toInt()));

        ConsoleBackend extreme;
        extreme.setFontSize(14); extreme.setLineHeight(14);
        extreme.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(
            QStringLiteral("<div rect='0px,0px,4px,4px' border='999px,-1px,999px,999px' bcolor='#f00'>X</div>")));
        extreme.flush();
        for (int r = 0; r < extreme.rowCount(); ++r)
            for (const QVariant& v : extreme.lineBlocks(r)) {
                const auto m = v.toMap();
                if (m.value("kind").toString() == "box") {
                    check(m.value("boxW").toInt() >= 0 && m.value("boxH").toInt() >= 0,
                          "极端 border 值不会生成负盒子几何");
                    check(m.value("boxW").toInt() <= 4 && m.value("boxH").toInt() <= 4,
                          "border 宽度限制在 rect 尺寸内");
                }
            }
    }

        // 10g) 制表符：C# StringMeasure（GRAPHICS 分支）把 '\t' 当成 8 个空格，
    // GDI 的制表位同样是「平均字宽 × 8」。移植侧测量按半角 1 格、QML 却用
    // QTextLayout 自己画制表符（约 2 格），「测 1 画 2」会让制表符之后的区块
    // 全部压在左边 —— FL3.8 SHOW_STATUS 的「图像的大小」选项行就是重灾区。
    // 所以 span.text 里不再保留 '\t'，一律展开成 kTabColumns 个空格。
    qDebug() << "\n10g) 制表符按 8 个半角格展开（测量/绘制同一口径）";
    {
        ConsoleBackend tabs;
        tabs.setFontSize(14); tabs.setLineHeight(14);
        tabs.setGridColumns(226); tabs.setGridRows(60);
        // "A" + 2 个制表符 + "B" -> 1 + 16 + 1 = 18 格
        tabs.print(QStringLiteral("A\t\tB"));
        tabs.flush();
        QString text;
        int cols = -1;
        for (int r = 0; r < tabs.rowCount(); ++r)
            for (const QVariant& v : tabs.lineBlocks(r)) {
                const auto m = v.toMap();
                if (m.value("kind").toString() != "text") continue;
                text = m.value("text").toString();
                cols = m.value("cols").toInt();
            }
        check(!text.contains(QLatin1Char('\t')), "显示文本里不再有制表符（交给 QML 只画空格）");
        check(cols == 18, QString("2 个制表符算 16 格（实得总格数 %1）").arg(cols));
    }

    // Nested captures retain all rows; positioned layers cannot consume trailing body text.
    {
        ConsoleBackend c;
        c.setFontSize(14); c.setLineHeight(20);
        c.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(QStringLiteral(
            "<div rect='0px,20px,100px,100px'><div rect='0px,20px,100px,80px'>"
            "A<br>B</div></div>TAIL")));
        c.flush();
        bool a = false, b = false, tail = false;
        for (int r = 0; r < c.rowCount(); ++r) {
            for (const QVariant& v : c.lineBlocks(r)) {
                const auto m = v.toMap();
                const QString text = m.value("text").toString();
                if (text == "A") { a = true; check(r * 20 + m.value("pxTop").toInt() == 40, "nested A vertical offset counted once"); }
                if (text == "B") { b = true; check(r * 20 + m.value("pxTop").toInt() == 60, "nested B retains second line"); }
                tail |= text == "TAIL";
            }
        }
        check(a && b && tail, "nested div retains both lines and trailing body text");
        ConsoleBackend empty;
        empty.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(QStringLiteral(
            "<div rect='0px,0px,10px,10px' padding='8'><img src='empty' width='30px' height='30px'></div>")));
        empty.flush();
        const auto clipped = blockOf(empty.lineBlocks(0), "empty");
        check(clipped.value("clipWidth").toInt() == 0 && clipped.value("clipHeight").toInt() == 0,
              "insets exhausting content box produce empty clip, not unlimited drawing");
    }

    // ---- 10b. 同一个 <div> 内的多段必须顺次前进（不能都回到 div 原点）----
    // FL3.8 情報画面表头：`SEX '= @"性別:%RESULTS:1%"`，RESULTS:1 带 <font color>，
    // 于是同一个 div 里出现「性別:」+「男性」两段。若每段都拿 division.x 当左边界，
    // 「男性」会整个压在「性別:」上（屏幕上表现为字形叠加/彩色毛边）。
    qDebug() << "\n10b) 同一 div 内多段顺次前进";
    {
        ConsoleBackend c;
        c.setFontSize(14);          // columnWidthPx() == 7
        c.setLineHeight(14);
        c.setGridColumns(226);
        c.setGridRows(60);
        c.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(
            QStringLiteral("<div rect='2200,0,2000,110'>性別:"
                           "<font color='#FF0080'>男性</font></div>")));
        c.flush();
        const QVariantList blocks = c.lineBlocks(0);
        const QVariantMap label = blockOf(blocks, QStringLiteral("性別:"));
        const QVariantMap value = blockOf(blocks, QStringLiteral("男性"));
        check(!label.isEmpty() && !value.isEmpty(),
              "div 内的两段都出现在同一行");
        // 2200 单位 × 14px / 100 = 308px；「性別:」= 2 全角 + 1 半角 = 5 列 = 35px
        check(label.value("pxLeft").toInt() == 308,
              QString("首段左边界 = div 原点 308px（实得 %1）")
                  .arg(label.value("pxLeft").toInt()));
        check(value.value("pxLeft").toInt() == 308 + 35,
              QString("次段接在前一段右边 343px（实得 %1）")
                  .arg(value.value("pxLeft").toInt()));
        check(value.value("pxLeft").toInt() != label.value("pxLeft").toInt(),
              "两段不会共用同一个 pxLeft（旧实现就是共用的）");

        // 同一个 div 里的两张 80px 缩略图：按**原始像素**前进，不是 11 列 × 7px。
        ConsoleBackend d;
        d.setFontSize(14);
        d.setLineHeight(14);
        d.setGridColumns(226);
        d.setGridRows(60);
        d.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(
            QStringLiteral("<div rect='0,0,400,80'>"
                           "<img src='thumbA' width='80px' height='80px'>"
                           "<img src='thumbB' width='80px' height='80px'></div>")));
        d.flush();
        const QVariantList shots = d.lineBlocks(0);
        const QVariantMap a = blockOf(shots, QStringLiteral("thumbA"));
        const QVariantMap b = blockOf(shots, QStringLiteral("thumbB"));
        check(a.value("pxLeft").toInt() == 0 && b.value("pxLeft").toInt() == 80,
              QString("div 内缩略图按像素前进 0/80（实得 %1/%2）")
                  .arg(a.value("pxLeft").toInt()).arg(b.value("pxLeft").toInt()));

        // div 内的 <br>：每一行都回到 div 的左边距（C# HtmlManager 的 PointX 复位）。
        // 不复位时每一行接着上一行末尾继续右移 —— FL3.8 情報画面「陥落」块就是
        // 一条 42px/行的斜阶梯（实测 700,742,784,854…）。
        ConsoleBackend e;
        e.setFontSize(14);
        e.setLineHeight(14);
        e.setGridColumns(226);
        e.setGridRows(60);
        e.printTemplate(PrintTemplateCompiler::parseRuntimeMarkup(
            QStringLiteral("<div rect='5000,0,3600,600'>・陥落<br>　　無<br>・嗜好</div>")));
        e.flush();
        QList<int> lineStarts;
        for (int line = 0; line < 3; ++line) {
            const QVariantList lb = e.lineBlocks(line);
            lineStarts.append(lb.isEmpty() ? -1 : lb.first().toMap().value("pxLeft").toInt());
        }
        // 5000 单位 × 14px / 100 = 700px
        check(lineStarts == QList<int>({700, 700, 700}),
              QString("div 内 3 行都从 700px 起（实得 %1/%2/%3）")
                  .arg(lineStarts.value(0)).arg(lineStarts.value(1)).arg(lineStarts.value(2)));

        // <div rect> 的肖像之后接正文：正文必须回到行首，不能被 div 右边缘推走。
        // FL3.8「SHOW_PORTRAIT 之后紧接 SMALL_TITLE("基礎") + SHOW_BASE」就是这条：
        // 之前 基礎/体力 整条 HUD 被推到第 173 列（超出画布，右端被裁掉）。
        ConsoleBackend g;
        g.setFontSize(14);
        g.setLineHeight(14);
        g.setGridColumns(226);
        g.setGridRows(60);
        g.printTemplateBuffered(PrintTemplateCompiler::parseRuntimeMarkup(
            QStringLiteral("<div rect='7200,0,200px,200px'>"
                           "<img src='face' width='200px' height='200px'></div>")));
        g.print(QStringLiteral("体力:"));
        g.flush();
        const QVariantList withFace = g.lineBlocks(0);
        const QVariantMap face = blockOf(withFace, QStringLiteral("face"));
        const QVariantMap bodyLabel = blockOf(withFace, QStringLiteral("体力:"));
        check(face.value("col").toInt() == 144,
              QString("肖像 div 定位在第 144 列（实得 %1）").arg(face.value("col").toInt()));
        check(bodyLabel.value("col").toInt() == 0,
              QString("div 之后的正文回到第 0 列（实得 %1）").arg(bodyLabel.value("col").toInt()));    }

    qDebug() << "\n===================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] console backend tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
