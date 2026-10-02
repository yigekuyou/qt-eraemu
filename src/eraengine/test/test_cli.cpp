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
// test_cli —— 交互式调试控制台（GUI 无关）
//
// 流程与 `appemuera` **完全一致**：
//     setGameDirectory(dir)  →  装载配置/CSV/ERB  →  runSystem()
//     →  系统状态机与脚本交替推进，直到「等待输入」/ 结束 / 出错
// 区别只在呈现与交互：这里把 ConsoleBackend 的行缓冲打印到 stdout，
// 用标准输入提供 `INPUT` / `INPUTS` / ONEINPUT 的值，便于无 GUI 地复现与调试。
//
// 用法：
//     test_cli <游戏目录>            交互式
//     test_cli <游戏目录> --script 0,1,0   非交互：按序列自动输入（调试/回归）
//     test_cli --help
//
// 交互命令：
//     整数 / 文本     -> provideInput / provideInputString
//     :q 或 EOF       -> 退出
//     :state          -> 打印系统状态 / 执行状态 / 调用栈
//     :v NAME[:i[:j]] -> dump 任意变量（整数/字符串/二维）
//     :e EXPR         -> 求值任意表达式（调试表达式/常量）
//     :vars           -> 打印若干系统变量（RESULT / 常用变量）
//     :model          -> 显示模型：逐 span（文本/颜色/粗斜体/内联图/图形）逐按钮
//     :check          -> 渲染自检：未展开的 %..%/{..}、本应是按钮的 [n]、空行、对齐
//     x               -> 注入一次鼠标点击（INPUTMOUSEKEY）
//     :branches       -> 打印当前输入的「合法分支」（引擎 AST 静态分析）
//
// 渲染诊断（「错误渲染」怎么测）：
//   * 每行输出都会附带注解：[CENTER]/[btn:0,1]/[!未展开格式]/[!字面按钮] …
//   * `--dump-model`：非交互模式下也打印显示模型结构
//   * `--check`     ：非交互模式下做渲染自检，**发现可疑即退出码 2**（可进 CI）
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QSet>
#include <algorithm>
#include <optional>
#include <random>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QSocketNotifier>
#include <QElapsedTimer>
#include <unistd.h>   // STDIN_FILENO
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <iostream>
#include <string>

#include "eraengine.h"
#include "process_state.h"
#include "system_state_machine.h"
#include "console_backend.h"
#include "console_plane.h"
#include "variable_storage.h"

namespace {

// ---------------------------------------------------------------------------
// 渲染诊断工具
// ---------------------------------------------------------------------------

const char* alignName(int a) {
    switch (a) { case 1: return "CENTER"; case 2: return "RIGHT"; default: return "LEFT"; }
}

// 该行是否含「未展开的格式化标记」（%VAR% / {EXPR}）——说明 StrForm 没生效
bool hasUnresolvedForm(const QString& text) {
    if (text.contains(QLatin1Char('{'))) return true;
    // % 成对出现才可疑（避免误判百分号）
    const int first = text.indexOf(QLatin1Char('%'));
    return first >= 0 && text.indexOf(QLatin1Char('%'), first + 1) > first + 1;
}

// 文本是不是「[数字] …」这种选中项？（Emuera 里应被 ButtonStringCreator 变成按钮）
bool looksLikeMenuButton(const QString& text) {
    const QString t = text.trimmed();
    if (!t.startsWith(QLatin1Char('['))) return false;
    const int close = t.indexOf(QLatin1Char(']'));
    if (close <= 1) return false;
    bool ok = false;
    t.mid(1, close - 1).trimmed().toLongLong(&ok);
    return ok;
}

// 行注解：[ALIGN][btn:…][!warn]
QString lineAnnotation(const ConsoleDisplayLine& line) {
    QString a;
    if (line.align != ConsoleAlign::Left) {
        a += QStringLiteral("[%1]").arg(QString::fromLatin1(alignName(int(line.align))));
    }
    QStringList vals;
    for (const ConsoleSegment& seg : line.segments) {
        if (!seg.isButton) continue;
        vals << (seg.isInteger ? QString::number(seg.intValue) : seg.strValue);
    }
    if (!vals.isEmpty()) a += QStringLiteral("[btn:%1]").arg(vals.join(QLatin1Char(',')));
    const QString text = line.plainText();
    if (hasUnresolvedForm(text)) a += QStringLiteral("[!未展开格式]");
    if (vals.isEmpty() && looksLikeMenuButton(text)) a += QStringLiteral("[!字面按钮未生效]");
    if (text.trimmed().isEmpty()) a += QStringLiteral("[空行]");
    return a;
}

// 显示模型：逐段（ConsoleSegment）/ 逐最小单位区块（ConsoleSpan）
[[maybe_unused]] void dumpModel(const ConsoleDisplayLine& line, int absIndex) {
    std::cout << "  #" << absIndex << " align=" << alignName(int(line.align))
              << " lineNo=" << line.lineNo
              << " logical=" << (line.isLogicalLine ? 1 : 0)
              << " segments=" << line.segments.size()
              << " parts=" << line.spanCount()
              << " width=" << line.width()
              << "  text=\"" << line.plainText().toStdString() << "\"\n";
    for (int i = 0; i < line.segments.size(); ++i) {
        const ConsoleSegment& seg = line.segments.at(i);
        std::cout << "    segment" << i
                  << (seg.isButton ? " [button]" : " [text]")
                  << " col=" << seg.relCol << " cols=" << seg.cols
                  << " parts=" << seg.spans.size();
        if (seg.isButton) {
            std::cout << " value="
                      << (seg.isInteger ? QString::number(seg.intValue).toStdString()
                                        : seg.strValue.toStdString())
                      << " gen=" << seg.generation;
            if (!seg.tooltip.isEmpty())
                std::cout << " tooltip=\"" << seg.tooltip.toStdString() << "\"";
            if (!seg.enabled) std::cout << " DISABLED";
        }
        std::cout << "\n";
        for (int k = 0; k < seg.spans.size(); ++k) {
            const ConsoleSpan& sp = seg.spans.at(k);
            std::cout << "        part" << k << " col=" << sp.col << " cols=" << sp.cols
                      << " row=" << sp.row;
            switch (sp.kind) {
            case ConsoleSpanKind::Image: std::cout << " kind=image src=" << sp.text.toStdString(); break;
            case ConsoleSpanKind::Shape: std::cout << " kind=shape type=" << sp.shapeType.toStdString(); break;
            default:        std::cout << " kind=text  \"" << sp.text.toStdString() << "\""; break;
            }
            if (sp.style.color.isValid())
                std::cout << " color=" << sp.style.color.name(QColor::HexRgb).toStdString();
            if (sp.style.bold)      std::cout << " bold";
            if (sp.style.italic)    std::cout << " italic";
            if (sp.style.underline) std::cout << " underline";
            if (!sp.style.fontName.isEmpty())
                std::cout << " font=" << sp.style.fontName.toStdString();
            std::cout << "\n";
        }
    }
}

// 显示模型（扁平）：兼容旧的逐 span / 逐 button 输出
[[maybe_unused]] void dumpModelFlat(const ConsoleDisplayLine& line, int absIndex) {
    std::cout << "  #" << absIndex << " align=" << alignName(int(line.align))
              << " spans=" << line.spanCount() << " buttons=" << line.buttonIndices().size()
              << "  text=\"" << line.plainText().toStdString() << "\"\n";
    const QList<ConsoleSpan> parts = line.flatSpans();
    for (int i = 0; i < parts.size(); ++i) {
        const ConsoleSpan& sp = parts.at(i);
        std::cout << "      span" << i;
        switch (sp.kind) {
        case ConsoleSpanKind::Image: std::cout << " kind=image src=" << sp.text.toStdString(); break;
        case ConsoleSpanKind::Shape: std::cout << " kind=shape type=" << sp.shapeType.toStdString(); break;
        default:        std::cout << " kind=text  \"" << sp.text.toStdString() << "\""; break;
        }
        if (sp.style.color.isValid())
            std::cout << " color=" << sp.style.color.name(QColor::HexRgb).toStdString();
        if (sp.style.bold)      std::cout << " bold";
        std::cout << "\n";
    }
    for (int i = 0; i < line.segments.size(); ++i) {
        const ConsoleSegment& seg = line.segments.at(i);
        if (!seg.isButton) continue;
        std::cout << "      button(segment" << i << ") value="
                  << (seg.isInteger ? QString::number(seg.intValue).toStdString()
                                    : seg.strValue.toStdString())
                  << (seg.tooltip.isEmpty() ? "" : (" tooltip=\"" + seg.tooltip.toStdString() + "\""))
                  << (seg.enabled ? "" : " DISABLED") << "\n";
    }
}

// 当前屏幕快照（以 ConsoleBackend 的缓冲为准；CLEARLINE 之后的行不会残留）
QList<QString> screenLines(ConsoleBackend* console, bool withAnnotation, bool withModel,
                           QList<ConsoleDisplayLine>* rawOut = nullptr) {
    QList<QString> out;
    if (!console) return out;
    const QList<ConsoleDisplayLine>& lines = console->buffer().lines();
    if (rawOut) *rawOut = lines;
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        ConsoleDisplayLine l = lines.at(lineIndex);
        // buffer 中的行已经由 ConsoleBackend 布局；这里再次用同一布局
        // 生成诊断坐标，保证 --model 与 QML visibleBlocks 使用同一单位。
        console->layout().placeLine(l, lineIndex);
        QString text = l.plainText();
        if (withAnnotation) {
            const QString ann = lineAnnotation(l);
            if (!ann.isEmpty()) text += QStringLiteral("   ") + ann;
        }
        out.append(text);
        if (withModel) {
            QStringList detail;
            for (const ConsoleSegment& seg : l.segments) {
                for (const ConsoleSpan& sp : seg.spans) {
                    QString d = QStringLiteral("    part ");
                    switch (sp.kind) {
                    case ConsoleSpanKind::Image: d += QStringLiteral("image src=") + sp.text; break;
                    case ConsoleSpanKind::Shape: d += QStringLiteral("shape ") + sp.shapeType; break;
                    default: d += QStringLiteral("text \"") + sp.text + QStringLiteral("\""); break;
                    }
                    d += QStringLiteral(" col=%1 cols=%2 row=%3").arg(sp.col).arg(sp.cols).arg(sp.row);
                    if (sp.style.color.isValid())
                        d += QStringLiteral(" color=%1").arg(sp.style.color.name(QColor::HexRgb));
                    if (seg.isButton) d += QStringLiteral(" [btn]");
                    detail.append(d);
                }
            }
            out += detail;
        }
    }
    return out;
}

// 平面重建工具（定义在后面）
QStringList planeLines(ConsoleBackend* console, bool withRuler, bool withDebug, bool withColor,
                       bool withAnsi);
QStringList planeIssues(ConsoleBackend* console, bool withDebug);

// 把「当前屏幕」打印出来（仅在变化时）；用于发现渲染错误：
//   * 帧没有被 CLEARLINE 清掉（越堆越多）
//   * 未展开的 %..% / {..}、本应是按钮的 [n]
//   * 对齐/颜色/内联图/图形
//   * withPlane：用「二维字符平面」呈现（root/text/image 分层模型重建）
bool renderScreen(ConsoleBackend* console, QList<QString>& last, bool withAnnotation,
                  bool withModel, const QString& tag, bool withPlane = false,
                  bool withDebug = false, bool withColor = false, bool withAnsi = false) {
    const QList<QString> now = withPlane ? [&]() {
            QStringList pl = planeLines(console, /*withRuler*/ true, withDebug, withColor, withAnsi);
            return QList<QString>(pl.begin(), pl.end());
        }()
                                       : screenLines(console, withAnnotation, withModel);
    if (now == last) return false;
    last = now;
    std::cout << "\n";
    // 只在 tag 非空时带标题（首帧/每帧）
    if (!tag.isEmpty()) std::cout << "──── " << tag.toStdString() << " ────\n";
    for (const QString& l : now) std::cout << l.toStdString() << "\n";
    return true;
}

// ---------------------------------------------------------------------------
// 平面重建（分层模型 → 二维字符平面）
//
// QML 的 root / text / image / shape 各层**铺满同一个平面**，区块的 x/y 就是
// 相对 root 的绝对坐标。所以可以按 (x / 列宽) 定列、(y / 行高) 定行，把整屏
// 还原成字符矩阵 —— 文本照抄、图片用 ▨ 占位、图形用 ─ 占位。
// 这样无需 GUI 就能看出「2 维排版」是否正确（列有没有对齐、图有没有落到右侧面板）。
// ---------------------------------------------------------------------------
ConsolePlaneOptions planeOptions(ConsoleBackend* console, bool withDebug = false, bool withColor = false) {
    ConsolePlaneOptions opt;
    opt.windowWidth = console ? console->windowWidth() : 760;
    // 终端安全：把 □ ■ ▨ ─ 这些「宽度有歧义」的符号换成 2 个 ASCII 字符，
    // 于是任何终端/字体下都严格「1 字符 = 1 个区块长」，列才对得齐；
    // 行尾附带本行占用的单位数，便于核对（不依赖终端字体）。
    opt.terminalSafe = !withDebug;   // 调试模式不使用 ASCII 替换
    opt.withWidths = true;
    opt.debugCompare = withDebug;    // 调试对比模式
    opt.debugColor = withColor;      // 调试颜色模式
    return opt;
}

QStringList planeLines(ConsoleBackend* console, bool withRuler, bool withDebug, bool withColor,
                       bool withAnsi) {
    if (!console) return {};
    ConsolePlaneOptions opt = planeOptions(console, withDebug, withColor);
    opt.ansiColors = withAnsi;
    return withRuler ? ConsolePlane::renderWithRuler(console->buffer(), console->layout(), opt)
                     : ConsolePlane::render(console->buffer(), console->layout(), opt);
}

// 平面几何自检（越界/重叠/未对齐/尺寸缺失…）
QStringList planeIssues(ConsoleBackend* console, bool withDebug) {
    if (!console) return {};
    return ConsolePlane::issueTexts(
        ConsolePlane::inspect(console->buffer(), console->layout(), planeOptions(console, withDebug)));
}

// 渲染自检：返回可疑项（供 --check 与 :check 使用）
QStringList renderSuspects(ConsoleBackend* console) {
    QStringList out;
    if (!console) return out;
    const QList<ConsoleDisplayLine>& lines = console->buffer().lines();
    int blanks = 0;
    for (int i = 0; i < lines.size(); ++i) {
        const ConsoleDisplayLine& l = lines.at(i);
        const QString t = l.plainText();
        if (t.trimmed().isEmpty()) { ++blanks; continue; }
        if (hasUnresolvedForm(t))
            out << QStringLiteral("第 %1 行有未展开的格式化标记: %2").arg(i).arg(t.left(60));
        if (l.buttonIndices().isEmpty() && looksLikeMenuButton(t))
            out << QStringLiteral("第 %1 行像选中项但没有按钮: %2").arg(i).arg(t.left(60));
    }
    if (blanks > lines.size() / 2 && lines.size() > 4)
        out << QStringLiteral("屏幕空行过多（%1/%2）—— 可能是本帧没被清掉或绘制缺失")
                   .arg(blanks).arg(lines.size());
    // 平面几何诊断（分层模型的坐标/尺寸）
    out += planeIssues(console, false);  // 自检不需要调试模式
    return out;
}

bool isStringInput(const QString& kind) {
    const QString k = kind.toUpper();
    return k.contains(QLatin1String("INPUTS")) || k.contains(QLatin1String("ARGS"));
}

// ---------------------------------------------------------------------------
// ControlObject —— 外部输入通道（DBus / Unix socket / TCP）的公共落点
//
//   命令行协议（stdin / socket 共用；DBus 用方法调用）：
//     0 / 123          -> 数值输入（provideInput）
//     s <文本>         -> 字符串输入（INPUTS -> RESULTS）
//     k <t> <r1> <r2> <r3> <r4>  -> INPUTMOUSEKEY 事件（类型/坐标/按键）
//     x                -> 注入一次鼠标左键点击
//     :state :vars :screen :model :check :ast EXPR -> 调试命令
//     q                -> 退出
// ---------------------------------------------------------------------------
class ControlObject : public QObject {
    Q_OBJECT
public:
    explicit ControlObject(QObject* parent = nullptr) : QObject(parent) {}
    std::function<void(const QString&)> onCommand;
    std::function<QStringList()> screenProvider;
    std::function<QStringList()> debugProvider;
    std::function<QStringList()> suspectsProvider;
    bool stderrDebug = true;

public slots:
    Q_SCRIPTABLE void input(int value) { if (onCommand) onCommand(QString::number(value)); }
    Q_SCRIPTABLE void inputString(const QString& value) {
        if (onCommand) onCommand(QStringLiteral("s ") + value);
    }
    Q_SCRIPTABLE void mouseKey(int type, int r1, int r2, int r3, int r4) {
        if (onCommand)
            onCommand(QStringLiteral("k %1 %2 %3 %4 %5").arg(type).arg(r1).arg(r2).arg(r3).arg(r4));
    }
    Q_SCRIPTABLE void sendCommand(const QString& command) { if (onCommand) onCommand(command); }
    Q_SCRIPTABLE QStringList screen() const { return screenProvider ? screenProvider() : QStringList(); }
    Q_SCRIPTABLE QStringList debugInfo() const { return debugProvider ? debugProvider() : QStringList(); }
    Q_SCRIPTABLE QStringList suspects() const { return suspectsProvider ? suspectsProvider() : QStringList(); }
    Q_SCRIPTABLE void quit() { if (onCommand) onCommand(QStringLiteral("q")); }

signals:
    Q_SCRIPTABLE void screenChanged(const QStringList& lines);
    Q_SCRIPTABLE void debugMessage(const QString& message);

public:
    void notifyScreen(const QStringList& lines) { emit screenChanged(lines); }
    void debug(const QString& message) {
        if (stderrDebug) std::cerr << "[test_cli] " << message.toStdString() << '\n';
        emit debugMessage(message);
    }
};

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    std::cout << std::unitbuf;      // 终端输出不缓冲
    std::cerr << std::unitbuf;      // stderr 调试流不缓冲，便于外部 harness 实时读取

    QString directory;
    QStringList scripted;      // --script 0,1,0
    bool interactive = true;
    bool appendLog = false;    // --log：追加式（不反映 CLEARLINE）
    bool withModel = false;    // --model：打印 span/button 结构
    bool withPlane = false;    // --plane：用二维字符平面呈现（分层模型）
    bool withDebug = false;    // --debug：调试对比模式（方块替换为 [ ]）
    bool withColor = false;    // --color：调试颜色模式（用 ANSI 颜色表示位置矩阵）
    bool withAnsi = false;     // --ansi：按 span 样式输出 ANSI 真彩色
    bool stderrDebug = true;   // stderr 默认开启，便于外部调试器实时读取
    quint32 seedValue = 0;     // --seed 的值
    bool checkMode = false;    // --check：渲染自检
    int  frameLimit = 30;      // 屏幕快照最多打印多少帧
    bool useDBus = false;      // --dbus：注册 DBus 服务
    QString socketPath;        // --socket [路径]：Unix socket
    int  tcpPort = 0;          // --tcp <端口>：TCP socket
    bool paced = false;
    int frameMs = 16;
    int  runMs = 0;            // --run-ms N：输入用尽后继续跑 N 毫秒（供外部注入）
    bool hasSeed = false;      // --seed N：固定 MT19937 随机种子（复现整局）
    bool autoBranch = false;   // --auto-branch：分支输入处按 seed 随机挑「合法分支」
    std::mt19937 branchRng;    // 分支选择的随机源（--seed 播种，可复现）
    int  branchSeedValue = 0;  // --branch-seed N：分支选择的种子（与 --seed 分开设置）
    bool hasBranchSeed = false;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
            qInfo().noquote()
                << "usage: test_cli <游戏目录> [--script 0,1,0] [--log] [--model] [--check] [--frames N]\n"
                << "  --paced   按事件循环分片执行；--frame-ms N 设置显示刷新间隔（默认16ms，不限制执行速度）\n"
                << "  --log     追加式日志（旧行为；不反映 CLEARLINE）\n"
                << "  --model   屏幕快照同时打印 span/button 结构\n"
                << "  --plane   用「二维字符平面」呈现屏幕（root/text/image 分层模型重建）\n"
                << "  --debug   调试对比模式：用 [ ] 替换方块字符，便于对比两个输出\n"
                << "  --color   调试颜色模式：用 ANSI 颜色表示位置矩阵（需配合 --debug）\n"
                << "  --ansi    按 span 样式输出 ANSI 真彩色（自动启用 --plane；显示彩色方块）\n"
                << "  --frames N  最多打印 N 帧屏幕快照（默认 30，0=不限）\n"
                << "  --check   渲染自检（未展开格式/%/无按钮的 [n]/空行过多）→ 可疑时退出码 2\n"
                << "  --dbus           注册 DBus 服务 io.yigekuyou.emuera.testcli (/testcli)\n"
                << "                   方法：input/inputString/mouseKey/sendCommand/screen/debugInfo/suspects/quit\n"
                << "                   信号：screenChanged/debugMessage\n"
                << "  --stderr-debug   将运行状态、外部命令和屏幕帧摘要写入 stderr（默认开启）\n"
                << "  --no-stderr-debug  关闭 test_cli 自身的 stderr 调试摘要\n"
                << "  --socket [路径]  Unix socket（默认 /tmp/emuera-test-cli.sock）\n"
                << "  --tcp <端口>      TCP socket\n"
                << "  --run-ms N       输入用尽后继续跑 N 毫秒（给外部注入留时间）\n"
                << "  --seed N         固定 MT19937 随机种子（整局可复现）\n"
                << "  --auto-branch    输入用尽后，在「合法分支」（AST 静态分析出的\n"
                << "                   SELECTCASE CASE 常量）里按种子随机挑一个继续：\n"
                << "                   非分支数会落空并卡死引擎，挑合法分支则不会\n"
                << "                   （:branches 命令可查看当前输入的合法分支）\n"
                << "  --branch-seed N  分支选择的种子（与 --seed 分开设置；未给时用 --seed）\n"
                << "  输入源：stdin / --script / DBus / socket，行协议见文件头注释";
            return 0;
        }
        if (a == QLatin1String("--paced")) { paced = true; continue; }
        if (a == QLatin1String("--auto-branch")) {
            autoBranch = true;
            interactive = false;   // 自动行走模式（交互模式留给手输）
            continue;
        }
        if (a == QLatin1String("--frame-ms") && i + 1 < argc) {
            frameMs = qMax(1, QString::fromLocal8Bit(argv[++i]).toInt()); continue;
        }
        if (a == QLatin1String("--dbus")) {
            useDBus = true;
            continue;
        }
        if (a == QLatin1String("--stderr-debug")) {
            stderrDebug = true;
            continue;
        }
        if (a == QLatin1String("--no-stderr-debug")) {
            stderrDebug = false;
            continue;
        }
        if (a == QLatin1String("--socket")) {
            socketPath = (i + 1 < argc) ? QString::fromLocal8Bit(argv[++i])
                                        : QStringLiteral("/tmp/emuera-test-cli.sock");
            continue;
        }
        if (a == QLatin1String("--tcp")) {
            if (i + 1 < argc) tcpPort = QString::fromLocal8Bit(argv[++i]).toInt();
            continue;
        }
        if (a == QLatin1String("--run-ms")) {
            if (i + 1 < argc) runMs = QString::fromLocal8Bit(argv[++i]).toInt();
            continue;
        }
        if (a == QLatin1String("--seed")) {   // 固定随机种子（复现/回归）
            if (i + 1 < argc) {
                seedValue = static_cast<quint32>(QString::fromLocal8Bit(argv[++i]).toUInt());
                hasSeed = true;
            }
            continue;
        }
        if (a == QLatin1String("--branch-seed") && i + 1 < argc) {
            branchSeedValue = QString::fromLocal8Bit(argv[++i]).toInt();
            hasBranchSeed = true;
            continue;
        }
        if (a == QLatin1String("--log")) {   // 追加式日志（旧行为）
            appendLog = true;
            continue;
        }
        if (a == QLatin1String("--model")) { // 屏幕快照同时打印显示模型
            withModel = true;
            continue;
        }
        if (a == QLatin1String("--plane")) { // 用「二维字符平面」呈现屏幕（分层模型 → 平面）
            withPlane = true;
            continue;
        }
        if (a == QLatin1String("--debug")) { // 调试对比模式：方块替换为 [ ]
            withDebug = true;
            continue;
        }
        if (a == QLatin1String("--color")) { // 调试颜色模式：用 ANSI 颜色表示位置矩阵
            withColor = true;
            continue;
        }
        if (a == QLatin1String("--ansi")) { // 按实际 span 颜色呈现终端平面
            withAnsi = true;
            withPlane = true;
            continue;
        }
        if (a == QLatin1String("--check")) { // 渲染自检；发现可疑退出码 2
            checkMode = true;
            continue;
        }
        if (a == QLatin1String("--frames")) {
            if (i + 1 < argc) frameLimit = QString::fromLocal8Bit(argv[++i]).toInt();
            continue;
        }
        if (a == QLatin1String("--script")) {
            if (i + 1 < argc) {
                scripted = QString::fromLocal8Bit(argv[++i]).split(QLatin1Char(','), Qt::SkipEmptyParts);
                interactive = false;
            }
            continue;
        }
        if (directory.isEmpty()) directory = a;
    }
    if (directory.isEmpty()) directory = QStringLiteral(".");
    // 分支选择的随机源：--branch-seed 单独设置；未给时回落 --seed（默认 0）
    branchRng.seed(hasBranchSeed ? branchSeedValue : seedValue);

    if (!QDir(directory).exists()) {
        qWarning().noquote() << "目录不存在:" << directory;
        return 2;
    }

    EraEngine engine;
    std::cout << "== 装载 " << directory.toStdString() << " ==\n";
    engine.setGameDirectory(directory);   // 与 appemuera 相同：装载并停在等待输入

    ConsoleBackend* console = engine.getConsole();
    ProcessState* state = engine.getProcessState();
    SystemStateMachine* machine = engine.getSystemStateMachine();
    machine->setPacingEnabled(paced); // deterministic synchronous headless fallback
    console->setFrameMs(frameMs);
    // 死循环诊断：超限即报错并给出位置。
    // 上限要足够大——eraTW 的新开局（145 个角色的房间/服装/初始化）单批就超过
    // 200 万条指令；真死循环会无限跑下去，所以高上限仍能捕获，只是晚一点。
    engine.getScriptRunner()->setStepLimit(200000000);
    if (hasSeed) engine.setRandomSeed(seedValue);      // 固定随机种子 -> 整局可复现

    // ---- 外部输入通道：stdin / --script / DBus / socket 统一走「命令队列」----
    QList<QString> pending;
    bool stdinOpen = interactive;
    ControlObject control;
    control.stderrDebug = stderrDebug;
    control.onCommand = [&pending, &control](const QString& cmd) {
        pending.append(cmd);
        control.debug(QStringLiteral("command queued: %1").arg(cmd));
    };
    control.screenProvider = [&]() {
        QList<QString> snap = screenLines(console, true, withModel);
        return QStringList(snap.begin(), snap.end());
    };
    control.debugProvider = [&]() {
        return QStringList{
            QStringLiteral("systemState=%1").arg(state ? SystemStateMachine::stateName(state->getSystemState()) : QStringLiteral("-")),
            QStringLiteral("execState=%1").arg(state ? int(state->getExecState()) : -1),
            QStringLiteral("inputKind=%1").arg(console ? console->inputKind() : QString()),
            QStringLiteral("script=%1").arg(engine.getParseTable() ? engine.getParseTable()->currentScript() : QString()),
            QStringLiteral("line=%1").arg(engine.getParseTable() ? engine.getParseTable()->currentLine() : -1),
            QStringLiteral("depth=%1").arg(engine.getParseTable() ? engine.getParseTable()->depth() : -1),
            QStringLiteral("screenLines=%1").arg(console ? console->buffer().count() : 0),
            QStringLiteral("pendingCommands=%1").arg(pending.size())
        };
    };
    control.suspectsProvider = [&]() { return renderSuspects(console); };
    control.debug(QStringLiteral("loaded %1; external input and terminal input ready").arg(directory));
    QObject::connect(machine, &SystemStateMachine::errorOccurred, [&control](const QString& m) {
        std::cout << "[状态机错误] " << m.toStdString() << "\n";
        control.debug(QStringLiteral("state machine error: %1").arg(m));
    });

    // stdin（管道/终端）：非阻塞读，统一进 pending —— 这样管道 `printf 'i 0\nq\n' | test_cli` 也能驱动
    auto* stdinNotifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, &control);
    QObject::connect(stdinNotifier, &QSocketNotifier::activated, [&pending, stdinNotifier]() {
        char buf[4096];
        while (true) {
            const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf) - 1);
            if (n <= 0) {
                if (n == 0) stdinNotifier->setEnabled(false);   // EOF
                return;
            }
            buf[n] = '\0';
            const QList<QByteArray> parts = QByteArray(buf, static_cast<int>(n)).split('\n');
            for (const QByteArray& rawLine : parts) {
                const QString line = QString::fromUtf8(rawLine).trimmed();
                if (!line.isEmpty()) pending.append(line);
            }
            if (n < static_cast<ssize_t>(sizeof(buf) - 1)) return;
        }
    });

    // socket 客户端（可选）
    QList<QLocalSocket*> localClients;
    QList<QTcpSocket*> tcpClients;
    if (!socketPath.isEmpty()) {
        QLocalServer::removeServer(socketPath);
        auto* srv = new QLocalServer(&control);
        if (srv->listen(socketPath)) {
            std::cout << "== Unix socket: " << socketPath.toStdString() << " ==\n";
            control.debug(QStringLiteral("Unix socket ready: %1").arg(socketPath));
            QObject::connect(srv, &QLocalServer::newConnection, [&]() {
                while (QLocalSocket* c = srv->nextPendingConnection()) {
                    localClients.append(c);
                    QObject::connect(c, &QLocalSocket::readyRead, [&, c]() {
                        while (c->canReadLine()) {
                            const QString line = QString::fromUtf8(c->readLine()).trimmed();
                            if (!line.isEmpty()) pending.append(line);
                        }
                    });
                    QObject::connect(c, &QLocalSocket::disconnected, [&, c]() {
                        localClients.removeAll(c);
                        c->deleteLater();
                    });
                }
            });
        } else {
            std::cout << "== Unix socket 监听失败: " << socketPath.toStdString() << " ==\n";
        }
    }
    if (tcpPort > 0) {
        auto* srv = new QTcpServer(&control);
        if (srv->listen(QHostAddress::LocalHost, static_cast<quint16>(tcpPort))) {
            std::cout << "== TCP socket: 127.0.0.1:" << tcpPort << " ==\n";
            QObject::connect(srv, &QTcpServer::newConnection, [&]() {
                while (QTcpSocket* c = srv->nextPendingConnection()) {
                    tcpClients.append(c);
                    QObject::connect(c, &QTcpSocket::readyRead, [&, c]() {
                        while (c->canReadLine()) {
                            const QString line = QString::fromUtf8(c->readLine()).trimmed();
                            if (!line.isEmpty()) pending.append(line);
                        }
                    });
                    QObject::connect(c, &QTcpSocket::disconnected, [&, c]() {
                        tcpClients.removeAll(c);
                        c->deleteLater();
                    });
                }
            });
        } else {
            std::cout << "== TCP 监听失败: " << tcpPort << " ==\n";
        }
    }
    if (useDBus) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (bus.registerObject(QStringLiteral("/testcli"), &control,
                               QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)
            && bus.registerService(QStringLiteral("io.yigekuyou.emuera.testcli"))) {
            std::cout << "== DBus: io.yigekuyou.emuera.testcli /testcli"
                         "（qdbus io.yigekuyou.emuera.testcli /testcli input 0）==\n";
            control.debug(QStringLiteral("DBus ready: io.yigekuyou.emuera.testcli /testcli"));
        } else {
            std::cout << "== DBus 注册失败（是否已有实例？）==\n";
        }
    }

    // 把屏幕推给已连接的 socket 客户端（方便外部 harness 读）
    auto pushScreen = [&]() {
        if (localClients.isEmpty() && tcpClients.isEmpty()) return;
        QList<QString> snap = screenLines(console, true, withModel);
        const QByteArray payload =
            (QStringLiteral("SCREEN ") + QString::number(snap.size()) + QLatin1Char('\n')
             + snap.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
        for (QLocalSocket* c : localClients) c->write(payload);
        for (QTcpSocket* c : tcpClients) c->write(payload);
    };

    // 命令解析：统一入口（数值 / s 文本 / k 鼠标键 / x / :命令 / q）
    auto applyCommand = [&](const QString& cmd) -> bool {
        const QString c = cmd.trimmed();
        if (c == QLatin1String("q") || c == QLatin1String(":q")) return false;
        if (c == QLatin1String("x")) {
            engine.provideMouseKey(1, 10, 10, 1, 0);
            return true;
        }
        if (c.startsWith(QLatin1String("k "))) {
            const QStringList f = c.mid(2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            engine.provideMouseKey(f.value(0).toInt(), f.value(1).toInt(), f.value(2).toInt(),
                                   f.value(3).toInt(), f.value(4).toInt());
            return true;
        }
        if (c.startsWith(QLatin1String("s ")) || c.startsWith(QLatin1String("s\t"))) {
            engine.provideInputString(c.mid(2));
            return true;
        }
        if (c.startsWith(QLatin1String("i "))) {
            engine.provideInput(c.mid(2).toLongLong());
            return true;
        }
        if (c.startsWith(QLatin1Char(':'))) return true;   // : 命令在循环里处理
        if (isStringInput(console ? console->inputKind() : QString())) {
            engine.provideInputString(c);
        } else {
            engine.provideInput(c.toLongLong());
        }
        return true;
    };

    QElapsedTimer runClock;                 // --run-ms 的总预算
    runClock.start();

    int printed = 0;                       // --log 模式的行游标
    QList<QString> lastScreen;
    int framesShown = 0;
    QStringList suspects;
    QSet<QString> suspectsSeen;   // 累计所有帧的可疑项（清屏后也保留）
    auto collectSuspects = [&]() {
        const QStringList sus = renderSuspects(console);
        for (const QString& x : sus) suspectsSeen.insert(x);
    };
    auto showScreen = [&](const QString& tag) {
        if (checkMode) collectSuspects();
        if (appendLog) return;
        if (frameLimit > 0 && framesShown >= frameLimit) return;
        const bool changed = renderScreen(console, lastScreen, /*annotation*/ true, withModel,
                                          tag, withPlane, withDebug, withColor, withAnsi);
        if (!changed) return;
        ++framesShown;
        const QStringList dbusScreen = control.screenProvider ? control.screenProvider() : QStringList();
        control.notifyScreen(dbusScreen);
        control.debug(QStringLiteral("screen changed: %1, lines=%2, frame=%3")
                          .arg(tag).arg(dbusScreen.size()).arg(framesShown));
    };
    auto drainLog = [&]() {
        if (!appendLog) return;
        const QList<ConsoleDisplayLine>& lines = console->buffer().lines();
        for (int i = printed; i < lines.size(); ++i) {
            std::cout << lines.at(i).plainText().toStdString() << "\n";
        }
        printed = lines.size();
    };

    std::cout << "== 启动系统状态机 ==" << "\n";
    engine.runSystem();
    showScreen(QStringLiteral("首屏"));
    drainLog();

    // ---- 调试命令（任何状态下都能执行：:state / :v / :plane / :geometry …）----
    // 返回 true = 已处理；false = 请求退出
    auto runDebugCommand = [&](const QString& cmd) -> bool {
        if (cmd == ":state") {
            const ExecState cur = state ? state->getExecState() : ExecState::Halt;
            std::cout << "  systemState="
                      << (state ? SystemStateMachine::stateName(state->getSystemState())
                                        : QStringLiteral("-")).toStdString()
                      << " execState=" << int(cur)
                      << " 调用栈深度=" << (engine.getParseTable() ? engine.getParseTable()->depth() : -1)
                      << " 当前脚本=" << (engine.getParseTable() ? engine.getParseTable()->currentScript().toStdString() : std::string())
                      << " 行=" << (engine.getParseTable() ? engine.getParseTable()->currentLine() : -1) << "\n";
            return true;
        }
        if (cmd == ":screen") {
            QList<QString> dummy;
            renderScreen(console, dummy, true, withModel, QStringLiteral("当前屏幕"), withPlane, withDebug, withColor, withAnsi);
            return true;
        }
        if (cmd == ":model") {
            withModel = !withModel;
            QList<QString> dummy;
            renderScreen(console, dummy, true, withModel, QStringLiteral("当前屏幕（模型）"), withPlane, withDebug, withColor, withAnsi);
            return true;
        }
        if (cmd == ":plane") {          // 二维字符平面（分层模型重建 + 列标尺）
            withPlane = !withPlane;
            QList<QString> dummy;
            renderScreen(console, dummy, true, withModel,
                         QStringLiteral("当前平面（%1）").arg(withPlane ? "开" : "关"), withPlane, withDebug, withColor, withAnsi);
            return true;
        }
        if (cmd == ":geometry") {       // 逐区块的 绝对/相对 位置与尺寸
            const QVariantList blocks = console ? console->visibleBlocks() : QVariantList();
            std::cout << "  区块数 " << blocks.size()
                      << "  行高 " << (console ? console->lineHeight() : 0)
                      << "  字号 " << (console ? console->fontSize() : 0)
                      << "  root 宽 " << (console ? console->windowWidth() : 0)
                      << "  列宽 " << (console ? console->columnWidth() : 0) << "\n";
            for (const QVariant& v : blocks) {
                const QVariantMap m = v.toMap();
                std::cout << "    [" << m.value("layer").toString().toStdString() << "]"
                          << " grid=(" << m.value("col").toInt() << "," << m.value("row").toInt() << ")"
                          << " rel=(" << m.value("relCol").toInt() << "," << m.value("relRow").toInt() << ")"
                          << " size=" << m.value("cols").toInt() << "x" << m.value("rows").toInt()
                          << " (px " << m.value("width").toInt() << "x" << m.value("height").toInt() << ")"
                          << (m.value("isButton").toBool() ? " [button]" : "")
                          << (m.contains("color") ? " color=" + m.value("color").toString().toStdString() : "")
                          << " \"" << m.value("plain").toString().toStdString()
                          << m.value("text").toString().toStdString() << "\"\n";
            }
            return true;
        }
        if (cmd == ":check") {
            const QStringList sus = renderSuspects(console);
            std::cout << "  渲染自检：" << (sus.isEmpty() ? "未发现可疑项" : "") << "\n";
            for (const QString& s : sus) std::cout << "  [!] " << s.toStdString() << "\n";
            return true;
        }
        if (cmd == ":branches") {   // 当前输入的「合法分支」（引擎 AST 静态分析）
            const QVariantList branches = console ? console->inputBranches() : QVariantList();
            std::cout << "  合法分支: ";
            if (branches.isEmpty()) {
                std::cout << "（无 —— 非分支输入 / 有 CASEELSE 兜底，不受限制）\n";
            } else {
                for (const QVariant& b : branches) std::cout << b.toLongLong() << ' ';
                std::cout << "\n";
            }
            return true;
        }
        if (cmd.startsWith(QLatin1String(":lines "))) {   // :lines SCRIPT —— dump 引擎侧逻辑行
            const QString scriptName = cmd.mid(7).trimmed();
            EraParseTable* table = engine.getParseTable();
            if (!table || !table->script(scriptName)) {
                std::cout << "  脚本不存在: " << scriptName.toStdString() << "\n";
                return true;
            }
            const ScriptData* sd = table->script(scriptName);
            for (int i = 0; i < sd->lines.size(); ++i) {
                const LogicalLine& ll = sd->lines[i];
                if (!ll.isInstruction()) continue;
                const QString fn = ll.functionName;
                if (fn == "CASE" || fn == "CASEELSE" || fn == "SELECTCASE" || fn == "ENDSELECT") {
                    std::cout << "  " << i << " " << fn.toStdString() << "  "
                              << ll.raw.left(70).toStdString() << "\n";
                }
            }
            return true;
        }
        if (cmd.startsWith(QLatin1String(":ast "))) {   // :ast EXPR —— 导出强类型表达式 AST
            const QString expr = cmd.mid(5).trimmed();
            EraParseTable* table = engine.getParseTable();
            const QSharedPointer<ExpressionNode> ast = table ? table->expressionAst(expr) : nullptr;
            if (!ast) {
                std::cout << "  AST <null> expr=" << expr.toStdString() << "\n";
            } else {
                std::cout << expressionAstDump(*ast).toStdString() << "\n";
            }
            return true;
        }
        if (cmd.startsWith(QLatin1String(":e "))) {   // :e EXPR —— 求值任意表达式
            const QString expr = cmd.mid(3);
            ExpressionEvaluator* ev = engine.getExpressionEvaluator();
            const QVariant r = ev ? ev->evaluate(expr, engine.getVariableStorage(),
                                                 engine.gameBaseData())
                                  : QVariant();
            std::cout << "  " << expr.toStdString() << " = " << r.toString().toStdString()
                      << "  (int " << r.toLongLong() << ")\n";
            return true;
        }
        if (cmd.startsWith(QLatin1String(":v "))) {   // :v NAME[:i[:j]] —— dump 任意变量
            const QString spec = cmd.mid(3);
            const QStringList parts = spec.split(QLatin1Char(':'));
            VariableStorage* vs = engine.getVariableStorage();
            if (!vs || parts.isEmpty() || parts.first().isEmpty()) {
                std::cout << "  用法: :v NAME[:i[:j]]\n";
                return true;
            }
            const QString nm = parts.at(0);
            std::cout << "  " << spec.toStdString() << " = ";
            if (parts.size() == 1) {
                const qint64 iv = vs->getGlobalInt1D(nm, 0);
                const QString sv = vs->getGlobalStr1D(nm, 0);
                std::cout << iv << (sv.isEmpty() ? "" : (" / \"" + sv.toStdString() + "\""));
            } else if (parts.size() == 2) {
                const int i = parts.at(1).toInt();
                const qint64 iv = vs->getGlobalInt1D(nm, i);
                const QString sv = vs->getGlobalStr1D(nm, i);
                std::cout << iv << (sv.isEmpty() ? "" : (" / \"" + sv.toStdString() + "\""));
            } else {
                std::cout << vs->getGlobalInt2D(nm, parts.at(1).toInt(), parts.at(2).toInt());
            }
            std::cout << "\n";
            return true;
        }
        if (cmd == ":vars") {
            VariableStorage* vs = engine.getVariableStorage();
            std::cout << "  RESULT=" << (vs ? vs->getSystemVariable(QStringLiteral("RESULT"), 0) : 0)
                      << " DAY=" << (vs ? vs->getSystemVariable(QStringLiteral("DAY"), 0) : 0)
                      << " MONEY=" << (vs ? vs->getSystemVariable(QStringLiteral("MONEY"), 0) : 0)
                      << " RESULTS=\"" << (vs ? vs->getLocalStr(0).toStdString() : std::string()) << "\"\n";
            return true;
        }
        return true;      // 未知 `:` 命令：吞掉
    };

    int step = 0;

    for (;;) {
        // 挂起的调试命令（:…）在任何状态下都先执行 —— 否则实时等待
        // （TONEINPUT/INPUTMOUSEKEY/AWAIT）会把控制权一直占住，没法调试
        while (!pending.isEmpty()) {
            const QString c = pending.first();
            if (c == QLatin1String(":q") || c == QLatin1String("q")) goto endMainLoop;
            if (!c.startsWith(QLatin1Char(':'))) break;   // 输入命令留给正常流程
            pending.takeFirst();
            std::cout << "> " << c.toStdString() << "   (调试)\n";
            runDebugCommand(c);
        }
        if (state->getExecState() == ExecState::Continue) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            continue;
        }
        showScreen(QStringLiteral("第 %1 帧").arg(framesShown + 1));
        drainLog();
        const ExecState st = state->getExecState();
        const QString stName = SystemStateMachine::stateName(state->getSystemState());

        if (st == ExecState::Halt) {
            std::cout << "\n== 脚本结束（Halt） 状态=" << stName.toStdString() << " ==\n";
            break;
        }
        if (st == ExecState::Error) {
            std::cout << "\n== 执行出错（Error） 状态=" << stName.toStdString() << " ==\n";
            break;
        }
        if (st != ExecState::WaitInput && st != ExecState::WaitSystemInput) {
            std::cout << "\n== 非等待状态 " << int(st) << " 状态=" << stName.toStdString() << " ==\n";
            break;
        }

        const QString kind = console ? console->inputKind() : QString();

        // 实时等待（AWAIT / INPUTMOUSEKEY 超时）：没有输入 UI 请求 → 跑事件循环让 QTimer 到点
        // 限时/实时输入（TONEINPUT / INPUTMOUSEKEY）与 AWAIT：跑定时器，无需用户输入
        const bool timerWait = (st == ExecState::WaitSystemInput && kind.isEmpty())
                               || kind.contains(QLatin1String("INPUTMOUSEKEY"))
                               || kind.contains(QLatin1String("TONEINPUT"));
        const bool hasExternal = !localClients.isEmpty() || !tcpClients.isEmpty() || useDBus;
        if (timerWait) {
            // 实时等待：AWAIT / TONEINPUT / INPUTMOUSEKEY 本身不需要用户操作，
            // 必须**真的跑事件循环**让 QTimer 到点（EraEngine 用 QTimer::singleShot）。
            // 期间随时接受外部注入（DBus / socket / stdin 的 'k …'）。
            if (!interactive && scripted.isEmpty() && !hasExternal && runMs <= 0) {
                // 输入源用尽 —— 但**限时等待还有 QTimer 这个输入源**：
                // 给它一段有界时间到点（TINPUT 50 / INPUTMOUSEKEY 50 的超时
                // 交付）；无计时器的等待（INPUTMOUSEKEY 0）到时退出。
                // 用 generation 判定「定时器真的交付了输入」（同 enum 值的
                // 新等待不算：deliverInputValues -> execStateChanged ->
                // notifyInputDone 会推 generation）。
                const quint64 gen0 = console ? console->generation() : 0;
                QElapsedTimer bounded; bounded.start();
                while (state->getExecState() == st && pending.isEmpty() && scripted.isEmpty()) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(5);
                    if (bounded.elapsed() > 3000) break;
                }
                if (state->getExecState() == st
                    && (!console || console->generation() == gen0)) {
                    std::cout << "\n（输入源用尽，停止实时循环）\n";
                    break;
                }
                continue;   // 定时器到点交付 -> 回主循环继续推进
            }
            static bool bannerShown = false;
            if (!bannerShown) {
                std::cout << "\n--- 实时等待（AWAIT / 限时输入，跑定时器）"
                             "---（外部通道：DBus/socket/stdin 的 'k t r1 r2 r3 r4' 注入鼠标键）\n";
                bannerShown = true;
            }
            int pump = 0;
            while (state->getExecState() == st && pending.isEmpty() && scripted.isEmpty()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(5);
                if (++pump % 40 == 0) pushScreen();       // 让 socket 客户端看到进展
                if (!interactive && runMs > 0 && runClock.elapsed() > runMs) break;
            }
            if (state->getExecState() == ExecState::Continue) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            continue;
        }
        showScreen(QStringLiteral("第 %1 帧").arg(framesShown + 1));
            drainLog();
            pushScreen();
            if (pending.isEmpty() && scripted.isEmpty()) {
                if (state->getExecState() != st) {
                    bannerShown = false;
                    continue;                               // 定时器到点 -> 重新判断
                }
                if (!interactive && runMs > 0 && runClock.elapsed() > runMs) {
                    std::cout << "\n（--run-ms 到期，退出）\n";
                    break;
                }
                std::cout << "\n（实时等待超时，退出）\n";
                break;
            }
            // 有外部命令（i/k/x/s…）：**不要** continue —— 否则会一直在实时等待里
            // 打转、命令永远不被消费。落到下面的「等待输入/命令处理」即可。
        }

        std::cout << "\n--- 等待输入 [" << kind.toStdString() << "] 状态=" << stName.toStdString()
                  << " 步=" << step << " ---\n";

        // 命令来源优先级：外部通道(dbus/socket) -> --script -> stdin
        QString cmd;
        bool haveCmd = false;
        if (!pending.isEmpty()) {
            cmd = pending.takeFirst();
            haveCmd = true;
            std::cout << "> " << cmd.toStdString() << "   (外部通道)\n";
        } else if (!scripted.isEmpty()) {
            cmd = scripted.takeFirst();
            haveCmd = true;
            std::cout << "> " << cmd.toStdString() << "\n";
        } else if (autoBranch && !interactive) {
            // 分支选择（--auto-branch）：只在「合法分支」里挑 —— 非分支数会落空
            // （无 CASEELSE 时 GOTO 回菜单重画再等），自动行走就是卡死；挑合法
            // 分支则永不卡死。种子：--branch-seed 单独设置（未给时用 --seed）。
            // 仅非交互模式（交互模式留给手输，:branches 可查看合法分支）。
            // 候选优先级：AST 静态分析（SELECTCASE CASE 常量）
            //          -> 屏幕上已打印且仍可点击的按钮值（运行期合法输入：
            //             ASK_YN / 菜单按钮 —— 非按钮值会被守卫循环吃掉）。
            QVariantList picks = console ? console->inputBranches() : QVariantList();
            if (picks.isEmpty() && console) {
                QSet<qint64> btns;
                const QVariantList blocks = console->visibleBlocks();
                for (const QVariant& v : blocks) {
                    const QVariantMap m = v.toMap();
                    if (!m.value(QStringLiteral("isButton")).toBool()) continue;
                    if (!m.value(QStringLiteral("clickable")).toBool()) continue;
                    const QVariant bv = m.value(QStringLiteral("btnValue"));
                    if (bv.typeId() == QMetaType::QString) continue;   // 字符串按钮不给数值输入
                    btns.insert(bv.toLongLong());
                }
                for (const qint64 b : btns) picks.append(QVariant::fromValue<qint64>(b));
            }
            if (!picks.isEmpty()) {
                std::uniform_int_distribution<int> pick(0, static_cast<int>(picks.size()) - 1);
                cmd = QString::number(picks.at(pick(branchRng)).toLongLong());
                haveCmd = true;
                std::cout << "> " << cmd.toStdString()
                          << "   (分支选择 seed="
                          << (hasBranchSeed ? branchSeedValue : seedValue) << ")\n";
            }
        } else if (stdinOpen || hasExternal || runMs > 0) {
            // stdin 由 QSocketNotifier 非阻塞喂进 pending；外部通道由 socket/dbus 回调喂。
            // 交互/外部模式下一直等（临时挂起让数据到达），非交互且无外部输入则收尾。
            const bool waitForever = interactive || stdinOpen || hasExternal;
            while (pending.isEmpty()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(5);
                if (!waitForever && runMs > 0 && runClock.elapsed() > runMs) break;
                if (!waitForever && runMs <= 0) break;
            }
            if (!pending.isEmpty()) {
                cmd = pending.takeFirst();
                haveCmd = true;
                std::cout << "> " << cmd.toStdString() << "   (stdin/外部)\n";
            }
        }
        if (!haveCmd) {
            // 没有输入源了：若开了外部通道/--run-ms，继续跑让外部注入；否则收尾
            if (!hasExternal && runMs <= 0) {
                std::cout << "（输入源用尽，退出）\n";
                break;
            }
            if (runMs > 0 && runClock.elapsed() > runMs) {
                std::cout << "（--run-ms 到期，退出）\n";
                break;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(20);
            pushScreen();
            continue;
        }
        std::string line = cmd.toStdString();
        if (line == ":q" || line == "q") break;
        if (line == "x" || line.rfind("k ", 0) == 0) {   // 鼠标键
            applyCommand(cmd);
            ++step;
            pushScreen();
            continue;
        }
        if (cmd.startsWith(QLatin1Char(':'))) {
            if (cmd == QLatin1String(":q") || cmd == QLatin1String("q")) break;
            std::cout << "> " << cmd.toStdString() << "   (调试)\n";
            runDebugCommand(cmd);
            continue;
        }
        applyCommand(cmd);
        ++step;
        pushScreen();
    }
endMainLoop:

    showScreen(QStringLiteral("末屏"));
    drainLog();

    // 渲染自检（累计所有帧）
    if (checkMode) {
        suspects = QStringList(suspectsSeen.begin(), suspectsSeen.end());
        std::sort(suspects.begin(), suspects.end());
        std::cout << "\n== 渲染自检 ==\n";
        if (suspects.isEmpty()) {
            std::cout << "  未发现可疑项\n";
        } else {
            for (const QString& s : suspects) std::cout << "  [!] " << s.toStdString() << "\n";
        }
    }
    std::cout << "== 结束 ==\n";
    return (checkMode && !suspects.isEmpty()) ? 2 : 0;
}

#include "test_cli.moc"
