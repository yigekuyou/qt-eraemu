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

#include <QDBusConnection>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
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
               "bufferLines=%1\nlogicalLines=%2\nvisibleCount=%3\nscrollOffset=%4\n"
               "followTail=%5\ngeneration=%6\ngridColumns=%7 gridRows=%8")
        .arg(c->buffer().count())
        .arg(c->buffer().logicalLineCount())
        .arg(c->visibleCount())
        .arg(c->scrollOffset())
        .arg(c->followTail())
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

QString EraDBusDebug::dumpScreen(int lastLines) {
    if (!m_engine || !m_engine->getConsole()) return QStringLiteral("no console");
    const QList<ConsoleDisplayLine>& lines = m_engine->getConsole()->buffer().lines();
    const int n = lines.size();
    const int from = qMax(0, n - qMax(1, lastLines));
    QStringList out;
    for (int i = from; i < n; ++i) out << lines.at(i).plainText();
    return out.join(QLatin1Char('\n'));
}

QString EraDBusDebug::listButtons() {
    if (!m_engine || !m_engine->getConsole()) return QStringLiteral("no console");
    QStringList out;
    const QVariantList blocks = m_engine->getConsole()->textBlocks();
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
    if (m_engine->getGameDirectory() == abs) {
        // 同一目录：setGameDirectory 会提前返回（QML 的属性 setter 语义），
        // 但自动化/性能测试需要「重跑一遍」，所以显式 reload()。
        m_engine->reload();
        return QStringLiteral("reloaded %1").arg(abs);
    }
    m_engine->setGameDirectory(abs);
    return QStringLiteral("opened %1").arg(m_engine->getGameDirectory());
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

void EraDBusDebug::scroll(int lines) {
    if (m_engine && m_engine->getConsole()) m_engine->getConsole()->scrollBy(lines);
}

void EraDBusDebug::scrollToBottom() {
    if (m_engine && m_engine->getConsole()) m_engine->getConsole()->scrollToBottom();
}

void EraDBusDebug::setLoggingRules(const QString& rules) {
    QLoggingCategory::setFilterRules(rules);
}
