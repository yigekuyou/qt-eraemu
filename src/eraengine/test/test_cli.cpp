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
//     :vars           -> 打印若干系统变量（RESULT / 常用变量）
//     :model          -> 显示模型：逐 span（文本/颜色/粗斜体/内联图/图形）逐按钮
//     :check          -> 渲染自检：未展开的 %..%/{..}、本应是按钮的 [n]、空行、对齐
//     x               -> 注入一次鼠标点击（INPUTMOUSEKEY）
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

#include <QDBusConnection>
#include <QDBusMessage>
#include <QSocketNotifier>
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
    if (!line.buttons.isEmpty()) {
        QStringList vals;
        for (const ConsoleButton& b : line.buttons) {
            vals << (b.isInteger ? QString::number(b.intValue) : b.strValue);
        }
        a += QStringLiteral("[btn:%1]").arg(vals.join(QLatin1Char(',')));
    }
    const QString text = line.plainText();
    if (hasUnresolvedForm(text)) a += QStringLiteral("[!未展开格式]");
    if (line.buttons.isEmpty() && looksLikeMenuButton(text)) a += QStringLiteral("[!字面按钮未生效]");
    if (text.trimmed().isEmpty() && line.spans.isEmpty()) a += QStringLiteral("[空行]");
    return a;
}

// 显示模型：逐 span / 逐 button
void dumpModel(const ConsoleDisplayLine& line, int absIndex) {
    std::cout << "  #" << absIndex << " align=" << alignName(int(line.align))
              << " spans=" << line.spans.size() << " buttons=" << line.buttons.size()
              << "  text=\"" << line.plainText().toStdString() << "\"\n";
    for (int i = 0; i < line.spans.size(); ++i) {
        const ConsoleSpan& sp = line.spans.at(i);
        std::cout << "      span" << i;
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
    for (int i = 0; i < line.buttons.size(); ++i) {
        const ConsoleButton& b = line.buttons.at(i);
        std::cout << "      button" << i << " spans[" << b.startSpan << ".."
                  << (b.startSpan + b.spanCount - 1) << "] value="
                  << (b.isInteger ? QString::number(b.intValue).toStdString() : b.strValue.toStdString())
                  << (b.tooltip.isEmpty() ? "" : (" tooltip=\"" + b.tooltip.toStdString() + "\""))
                  << (b.enabled ? "" : " DISABLED") << "\n";
    }
}

// 把 ConsoleBackend 的新行打印到 stdout（最后一行可能仍在追加，可选是否打印）
// 当前屏幕快照（以 ConsoleBackend 的缓冲为准；CLEARLINE 之后的行不会残留）
QList<QString> screenLines(ConsoleBackend* console, bool withAnnotation, bool withModel,
                           QList<ConsoleDisplayLine>* rawOut = nullptr) {
    QList<QString> out;
    if (!console) return out;
    const QList<ConsoleDisplayLine>& lines = console->buffer().lines();
    if (rawOut) *rawOut = lines;
    for (const ConsoleDisplayLine& l : lines) {
        QString text = l.plainText();
        if (withAnnotation) {
            const QString ann = lineAnnotation(l);
            if (!ann.isEmpty()) text += QStringLiteral("   ") + ann;
        }
        out.append(text);
        if (withModel) {
            // 模型细节也拼进来（前缀缩进，便于区分）
            QStringList detail;
            for (const ConsoleSpan& sp : l.spans) {
                QString d = QStringLiteral("    span ");
                switch (sp.kind) {
                case ConsoleSpanKind::Image: d += QStringLiteral("image src=") + sp.text; break;
                case ConsoleSpanKind::Shape: d += QStringLiteral("shape ") + sp.shapeType; break;
                default: d += QStringLiteral("text \"") + sp.text + QStringLiteral("\""); break;
                }
                if (sp.style.color.isValid()) d += QStringLiteral(" color=") + sp.style.color.name(QColor::HexRgb);
                if (sp.style.bold) d += QStringLiteral(" bold");
                if (sp.style.italic) d += QStringLiteral(" italic");
                detail.append(d);
            }
            for (const ConsoleButton& b : l.buttons) {
                detail.append(QStringLiteral("    button value=")
                              + (b.isInteger ? QString::number(b.intValue) : b.strValue)
                              + QStringLiteral(" spans[%1..%2]").arg(b.startSpan)
                                    .arg(b.startSpan + b.spanCount - 1));
            }
            out += detail;
        }
    }
    return out;
}

// 把「当前屏幕」打印出来（仅在变化时）；用于发现渲染错误：
//   * 帧没有被 CLEARLINE 清掉（越堆越多）
//   * 未展开的 %..% / {..}、本应是按钮的 [n]
//   * 对齐/颜色/内联图/图形
bool renderScreen(ConsoleBackend* console, QList<QString>& last, bool withAnnotation,
                  bool withModel, const QString& tag) {
    const QList<QString> now = screenLines(console, withAnnotation, withModel);
    if (now == last) return false;
    last = now;
    std::cout << "\n";
    // 只在 tag 非空时带标题（首帧/每帧）
    if (!tag.isEmpty()) std::cout << "──── " << tag.toStdString() << " ────\n";
    for (const QString& l : now) std::cout << l.toStdString() << "\n";
    return true;
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
        if (l.buttons.isEmpty() && looksLikeMenuButton(t))
            out << QStringLiteral("第 %1 行像选中项但没有按钮: %2").arg(i).arg(t.left(60));
    }
    if (blanks > lines.size() / 2 && lines.size() > 4)
        out << QStringLiteral("屏幕空行过多（%1/%2）—— 可能是本帧没被清掉或绘制缺失")
                   .arg(blanks).arg(lines.size());
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
//     :state :vars :screen :model :check  -> 调试命令
//     q                -> 退出
// ---------------------------------------------------------------------------
class ControlObject : public QObject {
    Q_OBJECT
public:
    explicit ControlObject(QObject* parent = nullptr) : QObject(parent) {}
    std::function<void(const QString&)> onCommand;
    std::function<QStringList()> screenProvider;
    std::function<QStringList()> suspectsProvider;

public slots:
    Q_SCRIPTABLE void input(int value) { if (onCommand) onCommand(QString::number(value)); }
    Q_SCRIPTABLE void inputString(const QString& value) {
        if (onCommand) onCommand(QStringLiteral("s ") + value);
    }
    Q_SCRIPTABLE void mouseKey(int type, int r1, int r2, int r3, int r4) {
        if (onCommand)
            onCommand(QStringLiteral("k %1 %2 %3 %4 %5").arg(type).arg(r1).arg(r2).arg(r3).arg(r4));
    }
    Q_SCRIPTABLE QStringList screen() const { return screenProvider ? screenProvider() : QStringList(); }
    Q_SCRIPTABLE QStringList suspects() const { return suspectsProvider ? suspectsProvider() : QStringList(); }
    Q_SCRIPTABLE void quit() { if (onCommand) onCommand(QStringLiteral("q")); }

signals:
    Q_SCRIPTABLE void screenChanged(const QStringList& lines);

public:
    void notifyScreen(const QStringList& lines) { emit screenChanged(lines); }
};

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    QString directory;
    QStringList scripted;      // --script 0,1,0
    bool interactive = true;
    bool appendLog = false;    // --log：追加式（不反映 CLEARLINE）
    bool withModel = false;    // --model：打印 span/button 结构
    bool checkMode = false;    // --check：渲染自检
    int  frameLimit = 30;      // 屏幕快照最多打印多少帧
    bool useDBus = false;      // --dbus：注册 DBus 服务
    QString socketPath;        // --socket [路径]：Unix socket
    int  tcpPort = 0;          // --tcp <端口>：TCP socket
    int  runMs = 0;            // --run-ms N：输入用尽后继续跑 N 毫秒（供外部注入）
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
            qInfo().noquote()
                << "usage: test_cli <游戏目录> [--script 0,1,0] [--log] [--model] [--check] [--frames N]\n"
                << "  --log     追加式日志（旧行为；不反映 CLEARLINE）\n"
                << "  --model   屏幕快照同时打印 span/button 结构\n"
                << "  --frames N  最多打印 N 帧屏幕快照（默认 30，0=不限）\n"
                << "  --check   渲染自检（未展开格式/%/无按钮的 [n]/空行过多）→ 可疑时退出码 2\n"
                << "  --dbus           注册 DBus 服务 io.yigekuyou.emuera.testcli (/testcli)\n"
                << "  --socket [路径]  Unix socket（默认 /tmp/emuera-test-cli.sock）\n"
                << "  --tcp <端口>      TCP socket\n"
                << "  --run-ms N       输入用尽后继续跑 N 毫秒（给外部注入留时间）\n"
                << "  输入源：stdin / --script / DBus / socket，行协议见文件头注释";
            return 0;
        }
        if (a == QLatin1String("--dbus")) {
            useDBus = true;
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
        if (a == QLatin1String("--log")) {   // 追加式日志（旧行为）
            appendLog = true;
            continue;
        }
        if (a == QLatin1String("--model")) { // 屏幕快照同时打印显示模型
            withModel = true;
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

    QObject::connect(machine, &SystemStateMachine::errorOccurred, [](const QString& m) {
        std::cout << "[状态机错误] " << m.toStdString() << "\n";
    });

    engine.getScriptRunner()->setStepLimit(2000000);   // 死循环诊断：超限即报错并给出位置

    // ---- 外部输入通道：stdin / --script / DBus / socket 统一走「命令队列」----
    QList<QString> pending;
    bool stdinOpen = interactive;
    ControlObject control;
    control.onCommand = [&pending](const QString& cmd) { pending.append(cmd); };
    control.screenProvider = [&]() {
        QList<QString> snap = screenLines(console, true, withModel);
        return QStringList(snap.begin(), snap.end());
    };
    control.suspectsProvider = [&]() { return renderSuspects(console); };

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
        if (renderScreen(console, lastScreen, /*annotation*/ true, withModel, tag)) ++framesShown;
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

    int step = 0;

    for (;;) {
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
        if (timerWait) {
            static bool bannerShown = false;
            if (!bannerShown) {
                std::cout << "\n--- 实时等待（AWAIT / 限时输入，跑定时器）"
                             "---（外部通道：DBus/socket/stdin 的 'k t r1 r2 r3 r4' 注入鼠标键）\n";
                bannerShown = true;
            }
            // 只跑一小段就让出，回到主循环检查外部命令（DBus / socket / --script）
            for (int i = 0; i < 20; ++i) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                QThread::msleep(5);
                if (state->getExecState() != st) break;
                if (console && !console->inputKind().isEmpty()) break;
                if (!pending.isEmpty()) break;
            }
            showScreen(QStringLiteral("第 %1 帧").arg(framesShown + 1));
            drainLog();
            pushScreen();
            if (!pending.isEmpty()) continue;              // 有外部命令 -> 回主循环处理
            if (state->getExecState() != st) {
                bannerShown = false;
                continue;                                   // 状态变了 -> 重新判断
            }
            const bool external = !localClients.isEmpty() || !tcpClients.isEmpty()
                                  || useDBus || runMs > 0;
            if (!interactive && scripted.isEmpty() && !external) {
                std::cout << "\n（输入源用尽，停止实时循环）\n";
                break;
            }
            static int idleMs = 0;
            if (runMs > 0 && (idleMs += 100) > runMs) {
                std::cout << "\n（--run-ms 到期，退出）\n";
                break;
            }
            continue;
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
        } else if (stdinOpen) {
            // stdin 由 QSocketNotifier 非阻塞喂进 pending；这里只做一次事件循环让数据到达
            for (int i = 0; i < 20 && pending.isEmpty(); ++i) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                QThread::msleep(5);
            }
            if (!pending.isEmpty()) {
                cmd = pending.takeFirst();
                haveCmd = true;
                std::cout << "> " << cmd.toStdString() << "   (stdin/外部)\n";
            }
        }
        if (!haveCmd) {
            // 没有输入源了：若开了外部通道/--run-ms，继续跑让外部注入；否则收尾
            const bool external = !localClients.isEmpty() || !tcpClients.isEmpty() || useDBus || runMs > 0;
            if (!external) {
                std::cout << "（输入源用尽，退出）\n";
                break;
            }
            static int idleRounds = 0;
            if (runMs > 0 && ++idleRounds * 50 > runMs) {
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
        if (line == ":state") {
            std::cout << "  systemState=" << stName.toStdString()
                      << " execState=" << int(st)
                      << " 调用栈深度=" << (engine.getParseTable() ? engine.getParseTable()->depth() : -1)
                      << " 当前脚本=" << (engine.getParseTable() ? engine.getParseTable()->currentScript().toStdString() : std::string())
                      << " 行=" << (engine.getParseTable() ? engine.getParseTable()->currentLine() : -1) << "\n";
            continue;
        }
        if (line == ":screen") {
            QList<QString> dummy;
            renderScreen(console, dummy, true, withModel, QStringLiteral("当前屏幕"));
            continue;
        }
        if (line == ":model") {
            withModel = !withModel;
            QList<QString> dummy;
            renderScreen(console, dummy, true, withModel, QStringLiteral("当前屏幕（模型）"));
            continue;
        }
        if (line == ":check") {
            const QStringList sus = renderSuspects(console);
            std::cout << "  渲染自检：" << (sus.isEmpty() ? "未发现可疑项" : "") << "\n";
            for (const QString& s : sus) std::cout << "  [!] " << s.toStdString() << "\n";
            continue;
        }
        if (line == ":vars") {
            VariableStorage* vs = engine.getVariableStorage();
            std::cout << "  RESULT=" << (vs ? vs->getSystemVariable(QStringLiteral("RESULT"), 0) : 0)
                      << " DAY=" << (vs ? vs->getSystemVariable(QStringLiteral("DAY"), 0) : 0)
                      << " MONEY=" << (vs ? vs->getSystemVariable(QStringLiteral("MONEY"), 0) : 0)
                      << " RESULTS=\"" << (vs ? vs->getLocalStr(0).toStdString() : std::string()) << "\"\n";
            continue;
        }

        applyCommand(cmd);
        ++step;
        pushScreen();
    }

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
