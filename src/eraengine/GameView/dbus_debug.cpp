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
#include "dbus_debug.h"

#include "console_backend.h"
#include "eraengine.h"
#include "eraengine_log.h"

#include <QDBusConnection>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QUrl>

EraDBusDebug::EraDBusDebug(EraEngine* engine, QObject* parent)
    : QObject(parent), m_engine(engine) {}

bool EraDBusDebug::registerOnBus() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    // UnusualServiceName 不校验；重复注册（理论上只有一个 EraEngine 实例）覆盖即可
    return bus.registerObject(QStringLiteral("/debug"), this,
                              QDBusConnection::ExportAllSlots
                                  | QDBusConnection::ExportAllInvokables
                                  | QDBusConnection::ExportAllProperties);
}

QString EraDBusDebug::ping() {
    return QStringLiteral("pong %1").arg(QDateTime::currentMSecsSinceEpoch());
}

QString EraDBusDebug::state() {
    if (!m_engine) return QStringLiteral("no engine");
    const auto* state = m_engine->getProcessState();
    const auto* table = m_engine->getParseTable();
    const auto* console = m_engine->getConsole();
    return QStringLiteral(
               "systemState=%1\nexecState=%2\nwaitingInput=%3 inputKind=%4\n"
               "script=%5 line=%6 depth=%7")
        .arg(state ? SystemStateMachine::stateName(state->getSystemState())
                   : QStringLiteral("-"))
        .arg(state ? static_cast<int>(state->getExecState()) : -1)
        .arg(console ? console->waitingInput() : false)
        .arg(console ? console->inputKind() : QString())
        .arg(table ? table->currentScript() : QString())
        .arg(table ? table->currentLine() : -1)
        .arg(table ? table->depth() : -1);
}

QString EraDBusDebug::consoleStats() {
    if (!m_engine || !m_engine->getConsole()) return QStringLiteral("no console");
    const auto* c = m_engine->getConsole();
    return QStringLiteral(
               "bufferLines=%1\nlogicalLines=%2\nmodelRows=%3\n"
               "maxSpanReach=%4\ngeneration=%5\ngridColumns=%6 gridRows=%7")
        .arg(c->buffer().count())
        .arg(c->buffer().logicalLineCount())
        .arg(c->rowCount())
        .arg(c->maxSpanReach())
        .arg(c->generation())
        .arg(c->gridColumns())
        .arg(c->gridRows());
}

QString EraDBusDebug::perf() {
    if (!m_engine || !m_engine->getConsole()) return QStringLiteral("no console");
    return m_engine->getConsole()->perfReport();
}

void EraDBusDebug::resetPerf() {
    if (m_engine && m_engine->getConsole()) m_engine->getConsole()->resetPerfCounters();
}

QString EraDBusDebug::loadState() {
    if (!m_engine) return QStringLiteral("no engine");
    const auto* table = m_engine->getParseTable();
    const bool loading = m_engine->isLoadingScripts();
    // 语义阶段在后台线程跑，会并发往 m_parseWarnings 追加；装载中不读列表，
    // 避免与后台写者竞争（只回一个哨兵值）。
    return QStringLiteral("loading=%1\ngameDir=%2\nscripts=%3\nwarnings=%4")
        .arg(loading)
        .arg(m_engine->getGameDirectory())
        .arg(table ? table->scriptNames().size() : -1)
        .arg(loading ? -1 : (table ? table->parseWarningCount() : -1));
}

QString EraDBusDebug::dumpScreen(int lastLines) {
    if (!m_engine || !m_engine->getConsole()) return QStringLiteral("no console");
    const QList<ConsoleDisplayLine>& lines = m_engine->getConsole()->buffer().lines();
    const int n = lines.size();
    const int from = qMax(0, n - qMax(1, lastLines));
    QStringList out;
    for (int i = from; i < n; ++i) out << lines.at(i).plainText();
    return out.join(QLatin1Char('\n'));
}

QString EraDBusDebug::diagBlocks() {
    if (!m_engine || !m_engine->getConsole()) return QStringLiteral("no console");
    auto* c = m_engine->getConsole();
    auto* g = m_engine->getGuiManager();
    QStringList out;
    out << QStringLiteral("gui.fore=%1 back=%2 font=%3 size=%4 lh=%5")
               .arg(g->foreColor().name(), g->backColor().name(), g->fontName())
               .arg(g->fontSize()).arg(g->lineHeight());
    out << QStringLiteral("console modelRows=%1 buffer=%2 maxSpanReach=%3 grid=%4x%5")
               .arg(c->rowCount()).arg(c->buffer().count()).arg(c->maxSpanReach())
               .arg(c->gridColumns()).arg(c->gridRows());
    if (QScreen* s = QGuiApplication::primaryScreen()) {
        out << QStringLiteral("screen name=%1 geom=%2x%3 avail=%4x%5 stage=%6x%7")
                   .arg(s->name())
                   .arg(s->geometry().width()).arg(s->geometry().height())
                   .arg(s->availableGeometry().width()).arg(s->availableGeometry().height())
                   .arg(g->windowWidth()).arg(g->windowHeight());
    }
    const QVariantList blocks = c->screenBlocks();
    out << QStringLiteral("textBlocks=%1").arg(blocks.size());
    auto dump = [&out](int i, const QVariantMap& m) {
        out << QStringLiteral("  [%1] row=%2 col=%3 cols=%4 rows=%5 color=%6 font=%7 kind=%8 btn=%9 text=%10")
                   .arg(i)
                   .arg(m.value("row").toInt()).arg(m.value("col").toInt())
                   .arg(m.value("cols").toInt()).arg(m.value("rows").toInt())
                   .arg(m.value("color").toString()).arg(m.value("fontName").toString())
                   .arg(m.value("kind").toString()).arg(m.value("isButton").toBool())
                   .arg(QString(m.value("text").toString()).left(40));
    };
    for (int i = 0; i < qMin(3, blocks.size()); ++i) dump(i, blocks.at(i).toMap());
    for (int i = qMax(0, blocks.size() - 2); i < blocks.size(); ++i) dump(i, blocks.at(i).toMap());
    // 非文本区块（图/形）全量输出 —— 立絵坐标核对用
    for (int i = 0; i < blocks.size(); ++i) {
        const QVariantMap m = blocks.at(i).toMap();
        if (m.value(QStringLiteral("kind")).toString() != QLatin1String("text"))
            dump(i, m);
    }
    return out.join(QLatin1Char('\n'));
}

QString EraDBusDebug::listButtons() {
    if (!m_engine || !m_engine->getConsole()) return QStringLiteral("no console");
    QStringList out;
    const QVariantList blocks = m_engine->getConsole()->screenBlocks();
    for (const QVariant& v : blocks) {
        const QVariantMap m = v.toMap();
        if (!m.value(QStringLiteral("isButton")).toBool()
            || !m.value(QStringLiteral("clickable")).toBool())
            continue;
        const QString value = m.value(QStringLiteral("isInteger")).toBool()
                                  ? m.value(QStringLiteral("btnValue")).toString()
                                  : m.value(QStringLiteral("btnValue")).toString();
        out << QStringLiteral("[%1,%2]=%3")
                   .arg(m.value(QStringLiteral("lineIndex")).toInt())
                   .arg(m.value(QStringLiteral("segmentIndex")).toInt())
                   .arg(value);
    }
    return out.join(QLatin1Char('\n'));
}

// 「文件 > 打开目录…」（Main.qml 的 FolderDialog 赋值 eraEngine.gameDirectory）
// 的脚本等价物：装载该目录并走 scriptsLoaded -> runSystem()（标题画面）。
// 有了它，无参启动的 GUI 也能被 D-Bus 指到任意游戏目录（自动化/性能测试用）。
QString EraDBusDebug::openDirectory(const QString& path) {
    if (!m_engine) return QStringLiteral("error: no engine");
    QString dir = path;
    if (dir.startsWith(QLatin1String("file://"))) dir = QUrl(dir).toLocalFile();
    dir = dir.trimmed();
    if (dir.isEmpty()) return QStringLiteral("error: empty path");
    const QFileInfo info(dir);
    if (!info.exists() || !info.isDir()) {
        return QStringLiteral("error: not a directory: %1").arg(dir);
    }
    const QString abs = info.absoluteFilePath();
    // 与 GUI（Main.qml）走同一条路径：异步装载（后台解析不阻塞界面）。
    // 调用立即返回，用 loadState / state 轮询装载是否结束。
    const bool same = (m_engine->getGameDirectory() == abs);
    m_engine->loadAsync(abs);
    return QStringLiteral("%1 %2 (async)").arg(same ? QStringLiteral("reloading")
                                                    : QStringLiteral("opening"), abs);
}

void EraDBusDebug::sendInput(qint64 value) {
    if (m_engine && m_engine->getConsole()) m_engine->getConsole()->submitInput(value);
}

void EraDBusDebug::sendInputString(const QString& text) {
    if (m_engine && m_engine->getConsole()) m_engine->getConsole()->submitInputString(text);
}

void EraDBusDebug::sendAnyKey() {
    if (m_engine && m_engine->getConsole()) m_engine->getConsole()->submitAnyKey();
}

void EraDBusDebug::sendMouseKey(int type, int r1, int r2, int r3, int r4) {
    if (m_engine && m_engine->getConsole())
        m_engine->getConsole()->submitMouseKey(type, r1, r2, r3, r4);
}

void EraDBusDebug::scroll(int lines) {
    if (m_engine && m_engine->getConsole()) m_engine->getConsole()->requestScrollBy(lines);
}

void EraDBusDebug::scrollToBottom() {
    if (m_engine && m_engine->getConsole()) m_engine->getConsole()->requestScrollToBottom();
}

void EraDBusDebug::setLoggingRules(const QString& rules) {
    QLoggingCategory::setFilterRules(rules);
}

QString EraDBusDebug::saveScreenshot(const QString& path) {
    // 主路径：QQuickWindow::grabWindow()（Qt 文档：把窗口场景渲染成 QImage，
    // 同步、不依赖桌面截屏 —— 桌面截屏工具抓不到 QML 合成内容）。
    // 同时广播给 QML（Item.grabToImage -> ItemGrabResult.saveToFile）作双保险。
    QQuickWindow* win = nullptr;
    const auto windows = QGuiApplication::topLevelWindows();
    for (QWindow* w : windows) {
        if (w->isVisible() && (win = qobject_cast<QQuickWindow*>(w)))
            break;
    }
    if (!win)
        return QStringLiteral("no QQuickWindow (visible top-levels: %1)").arg(windows.size());
    const QImage img = win->grabWindow();
    if (img.isNull())
        return QStringLiteral("grabWindow returned null image");
    if (!img.save(path))
        return QStringLiteral("save failed: %1").arg(path);
    emit screenshotRequested(path);
    return QStringLiteral("saved %1 (%2x%3)").arg(path).arg(img.width()).arg(img.height());
}

QString EraDBusDebug::startFrameCapture(const QString& prefix, int limit) {
    // 真正的抓取在 QML 侧完成（Item.grabToImage 才能拿到合成后的画面）：
    // 广播信号 -> Main.qml 逐帧落盘 <prefix>00000.png …
    if (prefix.isEmpty())
        return QStringLiteral("frame capture needs a non-empty prefix");
    emit frameCaptureRequested(prefix, limit);
    return QStringLiteral("frame capture started: %1####.png (limit=%2)")
        .arg(prefix)
        .arg(limit > 0 ? QString::number(limit) : QStringLiteral("∞"));
}

QString EraDBusDebug::stopFrameCapture() {
    emit frameCaptureStopRequested();
    return QStringLiteral("frame capture stopped");
}
