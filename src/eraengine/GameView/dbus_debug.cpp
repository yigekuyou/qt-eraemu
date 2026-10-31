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
#include "graphics_store.h"
#include "audio_pipeline_pool.h"
#include "era_parse_table.h"
#include "expression_evaluator.h"
#include "variable_storage.h"

#ifdef EMUERA_HAS_DBUS
#include <QDBusConnection>
#endif
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQuickItem>
#include <functional>
#include <QQuickWindow>
#include <QUrl>

EraDBusDebug::EraDBusDebug(EraEngine* engine, QObject* parent)
    : QObject(parent), m_engine(engine) {}

bool EraDBusDebug::registerOnBus() {
#ifdef EMUERA_HAS_DBUS
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    // UnusualServiceName 不校验；重复注册（理论上只有一个 EraEngine 实例）覆盖即可
    return bus.registerObject(QStringLiteral("/debug"), this,
                              QDBusConnection::ExportAllSlots
                                  | QDBusConnection::ExportAllInvokables
                                  | QDBusConnection::ExportAllProperties);
#else
    // 该平台的 Qt 未提供 QtDBus（例：Android）：本类其余槽函数仍可用
    // （test_cli / QML 侧直接调用），只是不对外暴露 D-Bus 服务。
    return false;
#endif
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
    // 装载期（含收尾窗口）脚本表/告警还在被写入；装载中不读列表，
    // 避免读到半成品（只回一个哨兵值）。
    return QStringLiteral("loading=%1\ngameDir=%2\nscripts=%3\nwarnings=%4")
        .arg(loading)
        .arg(m_engine->getGameDirectory())
        .arg(loading ? -1 : (table ? table->scriptNames().size() : -1))
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

QString EraDBusDebug::dumpRow(int row) {
    if (!m_engine || !m_engine->getConsole()) return QStringLiteral("no console");
    auto* c = m_engine->getConsole();
    QStringList out;
    int n = 0;
    const QVariantList blocks = c->screenBlocks();
    for (const QVariant& v : blocks) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("row")).toInt() != row) continue;
        ++n;
        out << QStringLiteral(
                   "  [%1] col=%2 px=%3 top=%4 cols=%5 rows=%6 kind=%7 color=%8 text=%9")
                   .arg(n)
                   .arg(m.value("col").toInt())
                   .arg(m.value("pxLeft").toInt())
                   .arg(m.value("pxTop").toInt())
                   .arg(m.value("cols").toInt())
                   .arg(m.value("rows").toInt())
                   .arg(m.value("kind").toString())
                   .arg(m.value("color").toString())
                   .arg(QString(m.value("text").toString()).left(60));
    }
    out.prepend(QStringLiteral("row=%1 blocks=%2").arg(row).arg(n));
    return out.join(QLatin1Char('\n'));
}

QString EraDBusDebug::diagAudio() {
    QString out;
    QTextStream ts(&out);
    AudioPipelinePool* audio = m_engine ? m_engine->getAudio() : nullptr;
    if (!audio) {
        ts << QStringLiteral("audioPool=null");
        return out;
    }
    ts << QStringLiteral("bgmSource=%1\n").arg(audio->bgmSource());
    ts << QStringLiteral("bgmPlaying=%1\n").arg(audio->bgmPlaying() ? 1 : 0);
    // QML 侧 AudioOutput.volume = bgmVolume/100；0 就是**数字静音**（Qt 后端做
    // 软件增益，PulseAudio 侧流音量仍是 100%），所以只看 pactl 会被骗。
    ts << QStringLiteral("bgmVolume=%1%%\n").arg(audio->bgmVolume());
    ts << QStringLiteral("soundVolume=%1%%\n").arg(audio->soundVolume());
    ts << QStringLiteral("activeChannels=%1 totalPipelines=%2\n")
              .arg(audio->activeChannels())
              .arg(audio->totalPipelines());
    ts << QStringLiteral("soundDirectory=%1").arg(audio->soundDirectory());
    return out;
}

QString EraDBusDebug::evalVar(const QString& text) {
    QString out;
    QTextStream ts(&out);
    if (!m_engine) return QStringLiteral("no engine");
    EraParseTable* table = m_engine->getParseTable();
    VariableStorage* storage = m_engine->getVariableStorage();
    if (!table || !storage) return QStringLiteral("engine not ready");
    const QString name = text.trimmed();
    // 声明标志位（1=整型 2=字符串 4=常量 8=二维 16=三维；0=不存在）
    ts << QStringLiteral("flags=%1 ").arg(table->variableFlags(name));
    // 与 GETVAR 同一条路径：收尾（markLoadComplete）之后仍可解析，
    // 且不做 #DIM CONST 折叠 —— 下标里的常量名（FLCONFIG:BGMの音量）才解得开。
    const QSharedPointer<ExpressionNode> ast = table->runtimeVariableReference(name);
    if (!ast) {
        ts << QStringLiteral("not-a-variable");
        return out;
    }
    // 必须用**引擎自己那个**求值器：它被注入了常数表/名表 provider
    // （eraengine.cpp 的 setExpressionEvaluator 装配）。临时构造的
    // ExpressionEvaluator 没有这些 provider，`#DIM CONST` 一律读成 0
    // —— 会把「常量=300」误报成 0，取证结论完全失效。
    ExpressionEvaluator* ev = m_engine->getExpressionEvaluator();
    if (!ev) return QStringLiteral("no evaluator");
    const QVariant v = ev->evaluate(*ast, storage, m_engine->gameBaseData(), eravar::kGlobalScope);
    switch (v.typeId()) {
    case QMetaType::QString:
        ts << QStringLiteral("str=\"%1\"").arg(v.toString());
        break;
    case QMetaType::Double:
        ts << QStringLiteral("real=%1").arg(v.toDouble());
        break;
    case QMetaType::LongLong:
    case QMetaType::Int:
        ts << QStringLiteral("int=%1").arg(v.toLongLong());
        break;
    case QMetaType::Bool:
        ts << QStringLiteral("bool=%1").arg(v.toBool() ? 1 : 0);
        break;
    default:
        ts << QStringLiteral("invalid/null (%1)").arg(v.typeName() ? v.typeName() : "?");
        break;
    }
    return out;
}

QString EraDBusDebug::diagBlocks() {
    if (!m_engine || !m_engine->getConsole()) return QStringLiteral("no console");
    auto* c = m_engine->getConsole();
    auto* g = m_engine->getGuiManager();
    QStringList out;
    out << QStringLiteral("gui.fore=%1 back=%2 font=%3 size=%4 lh=%5")
               .arg(g->foreColor().name(), g->backColor().name(), g->fontName())
               .arg(g->fontSize()).arg(g->lineHeight());
    // grid / clientSize / maxLineUnits 三者的关系是验收点：网格由舞台（C++ 配置
    // 画布）推导，GETCLIENTSIZE 读同一份网格，maxLineUnits（折行/DRAWLINE 宽）
    // 也同步到同一个列数 —— 它们必须与窗口尺寸无关。
    const int maxLineUnits = m_engine->getExecutionEngine()
                                 ? m_engine->getExecutionEngine()->maxLineUnits() : -1;
    out << QStringLiteral(
               "console modelRows=%1 buffer=%2 maxSpanReach=%3 grid=%4x%5 clientSize=%4x%5 "
               "maxLineUnits=%6")
               .arg(c->rowCount()).arg(c->buffer().count()).arg(c->maxSpanReach())
               .arg(c->gridColumns()).arg(c->gridRows())
               .arg(maxLineUnits);
    if (QScreen* s = QGuiApplication::primaryScreen()) {
        out << QStringLiteral("screen name=%1 geom=%2x%3 avail=%4x%5 stage=%6x%7")
                   .arg(s->name())
                   .arg(s->geometry().width()).arg(s->geometry().height())
                   .arg(s->availableGeometry().width()).arg(s->availableGeometry().height())
                   .arg(g->windowWidth()).arg(g->windowHeight());
    }
    out << QStringLiteral("platform=%1 topLevelWindows=%2")
               .arg(QGuiApplication::platformName())
               .arg(QGuiApplication::topLevelWindows().size());
    // 顶层窗口实际尺寸（saveScreenshot 抓的是第一个可见 QQuickWindow）
    for (const QWindow* w : QGuiApplication::topLevelWindows()) {
        out << QStringLiteral("window %1x%2 visible=%3 dpr=%4")
                       .arg(w->width()).arg(w->height())
                       .arg(w->isVisible()).arg(w->devicePixelRatio());
        // QML 实际使用的 cell / 字号 / 网格（验收「不重叠」需要真值，
        // 不能用窗口宽 ÷ 网格反推 —— 那是旧实现的口径）。
        if (const auto* qw = qobject_cast<const QQuickWindow*>(w)) {
            // 舞台（逻辑画布）+ 呈现缩放：舞台尺寸来自 C++ 配置，
            // scale/letterbox 来自 Stage.qml 的**唯一**一次变换。
            // 这张表就是「同一舞台、任意窗口 → grid/字号恒定」的证据。
            if (const auto* stage =
                    qobject_cast<const QQuickItem*>(
                        qw->findChild<QObject*>(QStringLiteral("gameStage")))) {
                const QQuickItem* host = stage->parentItem();
                const QSizeF hostSize = host ? QSizeF(host->width(), host->height())
                                             : QSizeF();
                const double scale = stage->property("stageScale").toDouble();
                out << QStringLiteral(
                           "gameStage=%1x%2 scale=%3 box=%4x%5 letterbox=%6x%7 host=%8x%9 "
                           "origin=%10,%11")
                           .arg(stage->width(), 0, 'f', 1)
                           .arg(stage->height(), 0, 'f', 1)
                           .arg(scale, 0, 'f', 4)
                           .arg(stage->width() * scale, 0, 'f', 1)
                           .arg(stage->height() * scale, 0, 'f', 1)
                           .arg(stage->property("letterboxX").toDouble(), 0, 'f', 1)
                           .arg(stage->property("letterboxY").toDouble(), 0, 'f', 1)
                           .arg(hostSize.width(), 0, 'f', 1)
                           .arg(hostSize.height(), 0, 'f', 1)
                           .arg(stage->x(), 0, 'f', 1)
                           .arg(stage->y(), 0, 'f', 1);
            }
            const QObject* root = qw->findChild<QObject*>(QStringLiteral("consoleRoot"));
            if (root) {
                out << QStringLiteral("qmlConsole box=%1x%2 cell=%3x%4 font=%5 grid=%6x%7")
                           .arg(root->property("width").toDouble(), 0, 'f', 1)
                           .arg(root->property("height").toDouble(), 0, 'f', 1)
                           .arg(root->property("cellWidth").toDouble(), 0, 'f', 3)
                           .arg(root->property("cellHeight").toDouble(), 0, 'f', 3)
                           .arg(root->property("renderFontSize").toDouble(), 0, 'f', 2)
                           .arg(root->property("gridColumns").toInt())
                           .arg(root->property("gridRows").toInt());
            }
        }
    }
    const QVariantList blocks = c->screenBlocks();
    out << QStringLiteral("textBlocks=%1").arg(blocks.size());
    auto dump = [&out](int i, const QVariantMap& m) {
        // px/top 是**像素**坐标（pxLeft 含 div 累计原点 + 整行对齐平移，pxTop 含
        // ypos 与 div 的 y）。col/row 是逐段量化后的网格值：核对按钮与图片是否
        // 共用同一个矩形时看 px/top，不要看 col —— 80px/7px 这类除不尽的比例下
        // 两者会差出整数格。
        out << QStringLiteral("  [%1] row=%2 col=%3 px=%4 top=%5 cols=%6 rows=%7 wh=%8x%9 color=%10 font=%11 kind=%12 btn=%13 text=%14")
                   .arg(i)
                   .arg(m.value("row").toInt()).arg(m.value("col").toInt())
                   .arg(m.value("pxLeft").toInt()).arg(m.value("pxTop").toInt())
                   .arg(m.value("cols").toInt()).arg(m.value("rows").toInt())
                   .arg(m.value("pxWidth").toInt()).arg(m.value("pxHeight").toInt())
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
    // QML 侧真相：每个图片委托（blockImage / blockImageButton）实际拿到的
    // source / status / 几何。C++ 只能证明「provider 有像素」，证明不了
    // 「Image 元素有没有加载、放到了哪里」——两者要分开看。
    // 诊断接口也可由无 GUI 的 test_cli/DBus 宿主调用；没有应用或窗口时，
    // C++ 区块诊断仍应返回，QML 侧诊断只跳过，不能解引用空 instance。
    QGuiApplication* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (app && !app->allWindows().isEmpty()) {
        if (QQuickWindow* qw = qobject_cast<QQuickWindow*>(app->allWindows().value(0))) {
            if (const QObject* root = qw->findChild<QObject*>(QStringLiteral("consoleRoot"))) {
            out << QStringLiteral("qmlBlocks=%1 stageStart=%2 stageMode=%3 stageImageBlocks=%4")
                       .arg(root->findChildren<QObject*>(QStringLiteral("consoleBlock")).size())
                       .arg(c->stageStart())
                       .arg(c->stageMode())
                       .arg(c->stageImageBlocks().size());
            // 只列「合成像」这类带 ypos 的图（top != 0）：缩略图每屏几十张，
            // 全列会把诊断刷成一片噪声。
            for (const QVariant& v : c->stageImageBlocks()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("pxTop")).toInt() == 0) continue;
                out << QStringLiteral("  stageImg name=%1 srcRow=%2 px=%3 top=%4")
                           .arg(m.value(QStringLiteral("text")).toString())
                           .arg(m.value(QStringLiteral("sourceRow")).toInt())
                           .arg(m.value(QStringLiteral("pxLeft")).toInt())
                           .arg(m.value(QStringLiteral("pxTop")).toInt());
            }
            for (const QString& viewName : {QStringLiteral("consoleListView"),
                                            QStringLiteral("consoleStageView"),
                                            QStringLiteral("stageImageLayer")}) {
                const auto* v = root->findChild<QQuickItem*>(viewName);
                if (!v) { out << QStringLiteral("view %1: <missing>").arg(viewName); continue; }
                out << QStringLiteral("view %1 count=%2 children=%3 visible=%4 wh=%5x%6")
                           .arg(viewName)
                           .arg(v->property("count").toInt())
                           .arg(v->childItems().size())
                           .arg(v->isVisible())
                           .arg(v->width(), 0, 'f', 1)
                           .arg(v->height(), 0, 'f', 1);
            }
            // 注意：ListView/Repeater 的 delegate **不是** root 的 QObject 子对象
            // （findChildren 找不到），只能沿 childItems() 递归遍历。
            QStringList imgs;
            int blockCount = 0;
            std::function<void(QQuickItem*, int)> walk = [&](QQuickItem* item, int depth) {
                if (!item || depth > 20) return;
                if (item->objectName() == QLatin1String("consoleBlock")) {
                    ++blockCount;
                    const QVariantMap data = item->property("blockData").toMap();
                    if (data.value(QStringLiteral("kind")).toString() == QLatin1String("image")) {
                        const QPointF scenePos = item->mapToScene(QPointF(0, 0));
                        QString desc = QStringLiteral("  img name=%1 xy=%2,%3 wh=%4x%5 vis=%6 scene=%7,%8 swh=%9x%10")
                                           .arg(data.value(QStringLiteral("text")).toString())
                                           .arg(item->x(), 0, 'f', 0).arg(item->y(), 0, 'f', 0)
                                           .arg(item->width(), 0, 'f', 0)
                                           .arg(item->height(), 0, 'f', 0)
                                           .arg(item->isVisible())
                                           .arg(scenePos.x(), 0, 'f', 0).arg(scenePos.y(), 0, 'f', 0)
                                           .arg(item->width() * item->scale() * 1.0, 0, 'f', 0)
                                           .arg(item->height(), 0, 'f', 0);
                        const QQuickItem* imgItem = nullptr;
                        for (QQuickItem* c : item->childItems()) {
                            if (c->objectName() == QLatin1String("blockImage")) imgItem = c;
                        }
                        if (imgItem) {
                            desc += QStringLiteral(" src=%1 status=%2 iwh=%3x%4")
                                        .arg(imgItem->property("source").toUrl().toString())
                                        .arg(imgItem->property("status").toInt())
                                        .arg(imgItem->property("implicitWidth").toDouble(), 0, 'f', 0)
                                        .arg(imgItem->property("implicitHeight").toDouble(), 0, 'f', 0);
                        } else {
                            desc += QStringLiteral(" src=<no blockImage>");
                        }
                        imgs << desc;
                    }
                }
                for (QQuickItem* child : item->childItems()) walk(child, depth + 1);
            };
            if (auto* contentItem = qobject_cast<QQuickItem*>(qw->contentItem()))
                walk(contentItem, 0);
            out << QStringLiteral("qmlImageBlocks=%1 consoleBlocksSeen=%2")
                       .arg(imgs.size()).arg(blockCount);
                out << imgs.join(QLatin1Char('\n'));
            }
        }
    }
    // 运行期精灵诊断：FACE_* 等名称来自 SPRITECREATE，不能只看静态资源表。
    // 同时报告像素尺寸和 alpha，区分“QML 没有 delegate”与“精灵本身为空/透明”。
    for (const QVariant& v : blocks) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("kind")).toString() != QLatin1String("image"))
            continue;
        const QString name = m.value(QStringLiteral("text")).toString();
        const QImage image = GraphicsStore::spriteImage(name);
        if (!image.isNull()) {
            const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
            QRect alphaBounds;
            for (int y = 0; y < argb.height(); ++y) {
                for (int x = 0; x < argb.width(); ++x) {
                    if (qAlpha(argb.pixel(x, y)) != 0)
                        alphaBounds |= QRect(x, y, 1, 1);
                }
            }
            out << QStringLiteral("runtimeImage name=%1 size=%2x%3 null=0 alphaBounds=%4x%5+%6+%7")
                       .arg(name).arg(image.width()).arg(image.height())
                       .arg(alphaBounds.width()).arg(alphaBounds.height())
                       .arg(alphaBounds.x()).arg(alphaBounds.y());
        } else {
            out << QStringLiteral("runtimeImage name=%1 null=1").arg(name);
        }
    }
    return out.join(QLatin1Char('\n'));
}

QString EraDBusDebug::saveSpriteImage(const QString& name, const QString& path) {
    const QImage image = GraphicsStore::spriteImage(name);
    if (image.isNull()) return QStringLiteral("no such sprite: %1").arg(name);
    if (!image.save(path)) return QStringLiteral("save failed: %1").arg(path);
    // 顺带报像素统计：不透明像素数 / 纯黑像素数，一眼看出「有内容但是黑的」。
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    qint64 opaque = 0, black = 0;
    for (int y = 0; y < argb.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            const QRgb p = line[x];
            if (qAlpha(p) == 0) continue;
            ++opaque;
            if ((p & 0x00ffffff) == 0) ++black;
        }
    }
    return QStringLiteral("saved %1 (%2x%3) opaque=%4 black=%5")
        .arg(path).arg(image.width()).arg(image.height()).arg(opaque).arg(black);
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

// 点中按钮：坐标口径与 listButtons 相同（[行,段]），入口与 QML 相同（clickAt）。
// 返回值只回显请求坐标；「点了有没有效果」由随后的 state 判断。
QString EraDBusDebug::clickButton(int lineIndex, int segmentIndex) {
    if (!m_engine || !m_engine->getConsole()) return QStringLiteral("no console");
    m_engine->getConsole()->clickAt(lineIndex, segmentIndex);
    return QStringLiteral("clicked [%1,%2]").arg(lineIndex).arg(segmentIndex);
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

QString EraDBusDebug::resizeWindow(int width, int height) {
    for (QWindow* w : QGuiApplication::topLevelWindows()) {
        if (auto* qw = qobject_cast<QQuickWindow*>(w)) {
            // 诊断专用：WindowController 会把窗口锁在配置尺寸（sizableWindow=false
            // 时 min==max），这里先解除约束再 resize，才能验证「窗口 != 配置」
            // 的极端情况（呈现层必须仍然正确）。不改变任何生产语义。
            qw->setMinimumSize(QSize(1, 1));
            qw->setMaximumSize(QSize(16777215, 16777215));
            qw->resize(qMax(1, width), qMax(1, height));
            return QStringLiteral("resize %1x%2 -> actual %3x%4 (platform %5)")
                .arg(width).arg(height)
                .arg(qw->width()).arg(qw->height())
                .arg(QGuiApplication::platformName());
        }
    }
    return QStringLiteral("no top-level window");
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
