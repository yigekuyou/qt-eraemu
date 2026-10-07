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
// ee_extension.cpp —— EE 扩展实现
//
// 全部经 ExtensionRegistry 的简单函数（reg / services()）——实现不直接
// 依赖引擎：变量写入走 services().storage，存档目录走 services().savDirectory
// （惰性 provider，setGameDirectory 之后才可知）。
//
// 本轮真实现：EE 存档系（对齐 C# Creator.Method.cs）：
//   · CHKVARDATA "<文件名>"     CheckdataStrMethod(EraSaveFileType.Var)
//   · CHKGLOBALDATA <编号>      CheckdataMethod(EraSaveFileType.Global)
//   · FIND_VARDATA ["<通配符>"] FindFilesMethod(EraSaveFileType.Var)
// 其余 EE 扩展命令（test/data/emuera_ee_cmds.txt）保持「留痕跳过」桩。
// ---------------------------------------------------------------------------

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QCoreApplication>
#include <QEvent>

#include "ee_extension.h"
#include "extension_registry.h"
#include "variable_storage.h"
#include "audio_pipeline_pool.h"

namespace {

// 对齐 C# EraDataState：0=OK / 1=FILENOTFOUND；RESULTS = 状态说明文本
constexpr int kDataOk = 0;
constexpr int kDataFileNotFound = 1;

// ---- 内存用量 / 回收（EE GETMEMORYUSAGE / CLEARMEMORY）------------------------
// C# 用 Process.WorkingSet64 与 GC.Collect()。Qt 侧核实（qt_documentation_search /
// qcoreapplication.html，Qt 6.12）：
//   · **QtCore 没有进程内存用量的可移植 API** —— QCoreApplication 只有
//     applicationPid()，QSysInfo 只有 CPU/系统标识，QStorageInfo 是磁盘；
//     即「用量」这一半 Qt 给不了，必须落到操作系统。
//   · 「回收」这一半 Qt 有原语：QCoreApplication::sendPostedEvents(nullptr,
//     QEvent::DeferredDelete) —— 文档明说 DeferredDelete 事件「只由事件循环或
//     sendPostedEvents 分派」（aboutToQuit 一节）：正在排队的 deleteLater()
//     对象只有冲刷了才真正析构、内存才归还。这是 Qt 里最接近 GC.Collect() 的动作。
// 因此实现：用量 = OS 探针（Linux /proc/self/statm 的 RSS，语义最接近工作集）；
// 回收 = 冲刷 DeferredDelete（Qt 原生） + glibc malloc_trim（把空闲堆还给 OS）。
#ifdef Q_OS_LINUX
#  include <cstdio>
#  include <unistd.h>
#  ifdef __GLIBC__
#    include <malloc.h>
#  endif
#endif

// 当前进程内存用量（字节）。
//   Linux：/proc/self/statm 的驻留页数 × 页大小（RSS）—— 真实值。
//   其他平台：Qt 没有探针，直接「假装」返回一个固定数（64MB）。
qint64 eeCurrentMemoryUsage() {
#if defined(Q_OS_LINUX)
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long total = 0, resident = 0;
    const int n = std::fscanf(f, "%ld %ld", &total, &resident);
    std::fclose(f);
    if (n < 2 || resident <= 0) return 0;
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return 0;
    return static_cast<qint64>(resident) * static_cast<qint64>(pageSize);
#else
    qCDebug(eraTrace) << "[ee-ext] GETMEMORYUSAGE：本平台无内存探针，返回固定值";
    return 64LL * 1024 * 1024;
#endif
}

// 强制回收（C# GC.Collect 的 Qt 对应物）：
//   ① 冲刷 DeferredDelete 队列 —— 让 deleteLater() 排队的 QObject 真正析构
//      （QCoreApplication 文档：这类事件只由事件循环/sendPostedEvents 分派）；
//   ② glibc：malloc_trim(0) 把空闲堆归还 OS。
void eeTrimHeap() {
    if (QCoreApplication* app = QCoreApplication::instance()) {
        Q_UNUSED(app);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
#ifdef __GLIBC__
    malloc_trim(0);
#endif
}

// ---- UPDATECHECK（EE v11；C# UPDATECHECK_Instruction，Instraction.Child.cs:2833）--
// 检查游戏是否有新版本，结果写 RESULT：
//   0 已是最新版 / 1 玩家选「否」 / 2 玩家选「是」（已开链接）
//   3 各种失败（URL 未配置、读不到版本名/链接、打开失败） / 4 配置禁止 / 5 无网络
// C# 用的环境：GameBase.csv 的「バージョン名」「バージョン情報URL」+ 网络 WebClient
// + MessageBox + 浏览器。本移植的确定性部分全部落地：
//   · 配置「UPDATECHECKを許可しない」-> 4（C# Config.ForbidUpdateCheck 先行判断）；
//   · URL 未配置 -> 3（C# url == null || url == "" 分支）；
//   · URL 有配置 -> 本移植**未接入网络栈**，无法取版本文件，按 C# 的失败路径
//     记 3（C# 里这对应 catch 块）——并把原因留痕，便于将来接网络后替换。
int updateCheck(const ExtensionRegistry& ext) {
    const ExtensionRegistry::Services& sv = ext.services();
    if (sv.forbidUpdateCheck && sv.forbidUpdateCheck()) return 4;

    const QString url = sv.gameBaseValue ? sv.gameBaseValue(QStringLiteral("バージョン情報URL"))
                                         : QString();
    if (url.isEmpty()) {
        qCDebug(eraTrace) << "[ee-ext] UPDATECHECK: GameBase.csv 未配置「バージョン情報URL」-> RESULT=3";
        return 3;
    }
    qCDebug(eraTrace) << "[ee-ext] UPDATECHECK: 已配置 URL 但本移植未接入网络栈 -> RESULT=3（C# 的失败路径）";
    return 3;
}

// ---- 存档探测（注册类的服务桥接；实现不直接依赖引擎）-----------------------

// CHKVARDATA "<文件名>"（对齐 C# CheckdataStrMethod: Var 型、字符串文件名版）：
//   RESULT = EraDataState；RESULTS = 状态说明。
bool chkVarData(const LogicalLine& line, const QList<Operand>& args,
                const ExtensionRegistry& ext) {
    const ExtensionRegistry::Services& sv = ext.services();
    const QString file = args.isEmpty() ? QString() : args.first().raw.trimmed();
    if (file.isEmpty()) return false;   // 参数不足：交给通用路径留痕

    const QString dir = sv.savDirectory ? sv.savDirectory() : QString();
    const QString path = dir.isEmpty() ? file : dir + QLatin1Char('/') + file;
    const bool exists = QFile::exists(path);
    if (sv.storage) {
        sv.storage->setSystemVariable(QStringLiteral("RESULT"), 0,
                                      exists ? kDataOk : kDataFileNotFound);
        sv.storage->setGlobalStr1D(QStringLiteral("RESULTS"), 0,
            exists ? QStringLiteral("ＯＫ") : QStringLiteral("ファイルが存在しません"));
    }
    qDebug() << "[ee-ext] CHKVARDATA" << path << (exists ? "存在" : "不存在")
             << "行" << line.position.toString();
    return true;
}

// CHKGLOBALDATA <编号>（对齐 C# CheckdataMethod: Global 型、编号版）：
//   全局存档文件（save_global.dat）存在性 -> RESULT / RESULTS。
bool chkGlobalData(const LogicalLine& line, const QList<Operand>& args,
                   const ExtensionRegistry& ext) {
    Q_UNUSED(line);
    Q_UNUSED(args);   // C# Global 型只有一个全局存档文件；编号不参与探测
    const ExtensionRegistry::Services& sv = ext.services();
    const QString dir = sv.savDirectory ? sv.savDirectory() : QString();
    const bool exists = !dir.isEmpty() && QFile::exists(dir + QStringLiteral("/save_global.dat"));
    if (sv.storage) {
        sv.storage->setSystemVariable(QStringLiteral("RESULT"), 0,
                                      exists ? kDataOk : kDataFileNotFound);
        sv.storage->setGlobalStr1D(QStringLiteral("RESULTS"), 0,
            exists ? QStringLiteral("ＯＫ") : QStringLiteral("ファイルが存在しません"));
    }
    qDebug() << "[ee-ext] CHKGLOBALDATA" << (exists ? "存在" : "不存在");
    return true;
}

// FIND_VARDATA ["<通配符>"]（对齐 C# FindFilesMethod: Var 型文件探索）：
//   命中的文件路径逐个写入 RESULTS:0,1,2…（本移植数组动态增长，无需截断），
//   段数写 RESULT:0。
bool findVarData(const LogicalLine& line, const QList<Operand>& args,
                 const ExtensionRegistry& ext) {
    Q_UNUSED(line);
    const ExtensionRegistry::Services& sv = ext.services();
    const QString dir = sv.savDirectory ? sv.savDirectory() : QString();
    if (dir.isEmpty()) return true;
    const QString pattern = args.isEmpty() ? QStringLiteral("*")
                                           : args.first().raw.trimmed();
    const QFileInfoList files = QDir(dir).entryInfoList(
        QStringList{ pattern }, QDir::Files, QDir::Name);
    if (sv.storage) {
        for (int i = 0; i < files.size(); ++i)
            sv.storage->setGlobalStr1D(QStringLiteral("RESULTS"), i, files.at(i).fileName());
        sv.storage->setSystemVariable(QStringLiteral("RESULT"), 0, files.size());
    }
    qDebug() << "[ee-ext] FIND_VARDATA" << pattern << "->" << files.size() << "个文件";
    return true;
}

// ---------------------------------------------------------------------------
// EE 音频（C# 原版没有音频，整块能力住在扩展）------------------------------
//
// 权威来源：emuera.em（EE）`Runtime/Script/Statements/Instraction.Child.cs`
//     public static Sound[] sound = new Sound[10];   // 10 条音效(SE)管线
//     public static Sound bgm = new();               // 1 条独立 BGM 管线
//   命令：PLAYSOUND(SP_HTML_PRINT: 字符串式 + 可选重复次数) / STOPSOUND /
//         PLAYBGM(STR_EXPRESSION) / STOPBGM / SETSOUNDVOLUME / SETBGMVOLUME(INT)；
//   音量 Math.Clamp(vol,0,100)；文件路径 `Program.SoundDir + 名`（<游戏目录>/sound/）。
//
// 分工（对齐 prd）：
//   * **C++ 控制**：本扩展把命令变成对 AudioPipelinePool 的控制调用
//     （第几条管线播什么、音量、循环次数）；
//   * **QML 维护播放**：池把「音效管线数」（4 字节无符号，扩展登记）交给 QML，
//     由 QML 维护对应数量的 SE 播放器 + 1 条 BGM 播放器真正出声。
//
// 数量**不硬编码在核心** —— 核心只搬这个 quint32 常量，改这里即可调整。
// ---------------------------------------------------------------------------
constexpr quint32 kEeAudioPipelines = 10;   // 对齐 EE `Sound[10]`（4 字节无符号）

// 顶层逗号切分（尊重引号 / 括号 / 方括号）：PLAYSOUND 的第 2 段是可选重复次数。
QStringList splitTopLevelComma(const QString& text) {
    QStringList out;
    QString cur;
    int depth = 0;
    QChar quote;
    for (const QChar c : text) {
        if (!quote.isNull()) {
            cur += c;
            if (c == quote) quote = {};
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; cur += c; continue; }
        if (c == QLatin1Char('(') || c == QLatin1Char('[')) ++depth;
        else if (c == QLatin1Char(')') || c == QLatin1Char(']')) { if (depth > 0) --depth; }
        if (c == QLatin1Char(',') && depth == 0) { out.append(cur); cur.clear(); continue; }
        cur += c;
    }
    out.append(cur);
    return out;
}

// 语句「去掉命令名后的剩余部分」（EE 的 STR_EXPRESSION / SP_HTML_PRINT 都取整段）。
QString statementRemainder(const LogicalLine& line, const QString& command) {
    QString rest = line.raw.trimmed();
    if (rest.left(command.length()).compare(command, Qt::CaseInsensitive) == 0)
        rest = rest.mid(command.length());
    return rest.trimmed();
}

// 求值一个表达式文本（扩展实参）；未注入求值服务时返回无效 QVariant。
QVariant audioEval(const QString& expr, const ExtensionRegistry& ext) {
    const ExtensionRegistry::Services& sv = ext.services();
    if (!sv.evaluate || expr.isEmpty()) return {};
    return sv.evaluate(expr);
}

// 去掉一层外层引号（兜底：求值失败时把文本原样当字符串用）。
QString stripOuterQuotes(QString s) {
    s = s.trimmed();
    if (s.size() >= 2) {
        const QChar first = s.front(), last = s.back();
        if ((first == QLatin1Char('"') && last == QLatin1Char('"'))
            || (first == QLatin1Char('\'') && last == QLatin1Char('\''))) {
            s = s.mid(1, s.size() - 2);
        }
    }
    return s;
}

// 字符串式实参：优先求值（变量 / 拼接 / 函数），失败退回去引号的原文。
QString audioStrExpr(const QString& expr, const ExtensionRegistry& ext) {
    const QVariant v = audioEval(expr, ext);
    if (v.isValid()) return v.toString();
    return stripOuterQuotes(expr);
}

// PLAYBGM <字符串式>：循环播放 BGM（0 号管线）。对齐 EE PLAYBGM_Instruction。
bool audioPlayBgm(const LogicalLine& line, const QList<Operand>&, const ExtensionRegistry& ext) {
    AudioPipelinePool* pool = ext.audioPool();
    if (!pool) return true;   // 无音频池：静默（等同未装音频扩展）
    const QString source = audioStrExpr(statementRemainder(line, QStringLiteral("PLAYBGM")), ext);
    const QString used = pool->playBgm(source);
    qDebug() << "[ee-ext] PLAYBGM" << source << "->" << used;
    return true;
}

// PLAYSOUND <字符串式>{, <重复次数>}：一次性音效。对齐 EE PLAYSOUND_Instruction。
bool audioPlaySound(const LogicalLine& line, const QList<Operand>&, const ExtensionRegistry& ext) {
    AudioPipelinePool* pool = ext.audioPool();
    if (!pool) return true;
    const QStringList parts =
        splitTopLevelComma(statementRemainder(line, QStringLiteral("PLAYSOUND")));
    const QString source = audioStrExpr(parts.value(0), ext);
    int repeat = 1;
    if (parts.size() >= 2 && !parts.at(1).trimmed().isEmpty()) {
        const QVariant v = audioEval(parts.at(1), ext);
        repeat = static_cast<int>(v.isValid() ? v.toLongLong() : parts.at(1).trimmed().toLongLong());
    }
    const QString used = pool->playSound(source, repeat);
    qDebug() << "[ee-ext] PLAYSOUND" << source << "x" << repeat << "->" << used;
    return true;
}

// STOPBGM：停止 BGM。
bool audioStopBgm(const LogicalLine&, const QList<Operand>&, const ExtensionRegistry& ext) {
    AudioPipelinePool* pool = ext.audioPool();
    if (pool) pool->stopBgm();
    return true;
}

// STOPSOUND：停掉全部音效（EE 语义；不带资源名）。
bool audioStopSound(const LogicalLine&, const QList<Operand>&, const ExtensionRegistry& ext) {
    AudioPipelinePool* pool = ext.audioPool();
    if (pool) pool->stopSounds();
    return true;
}

// SETBGMVOLUME / SETSOUNDVOLUME <整数式>：设置音量（EE Math.Clamp 在池内做）。
bool audioSetVolume(const LogicalLine& line, const QString& command,
                    const ExtensionRegistry& ext, bool bgm) {
    AudioPipelinePool* pool = ext.audioPool();
    if (!pool) return true;
    const QString expr = statementRemainder(line, command);
    const QVariant v = audioEval(expr, ext);
    const int volume = static_cast<int>(v.isValid() ? v.toLongLong() : expr.toLongLong());
    if (bgm) pool->setBgmVolume(volume);
    else pool->setSoundVolume(volume);
    return true;
}

// 音频扩展登记：声明维护数量（EE Sound[10]）+ 注册命令实现 + 式中函数 EXISTSOUND。
void registerEeAudio(ExtensionRegistry& ext) {
    ext.regAudioPipelines(kEeAudioPipelines);

    ext.reg(QStringLiteral("PLAYBGM"),
            [&ext](const LogicalLine& l, const QList<Operand>& a) { return audioPlayBgm(l, a, ext); });
    ext.reg(QStringLiteral("PLAYSOUND"),
            [&ext](const LogicalLine& l, const QList<Operand>& a) { return audioPlaySound(l, a, ext); });
    ext.reg(QStringLiteral("STOPBGM"),
            [&ext](const LogicalLine& l, const QList<Operand>& a) { return audioStopBgm(l, a, ext); });
    ext.reg(QStringLiteral("STOPSOUND"),
            [&ext](const LogicalLine& l, const QList<Operand>& a) { return audioStopSound(l, a, ext); });
    ext.reg(QStringLiteral("SETBGMVOLUME"),
            [&ext](const LogicalLine& l, const QList<Operand>&) {
                return audioSetVolume(l, QStringLiteral("SETBGMVOLUME"), ext, true);
            });
    ext.reg(QStringLiteral("SETSOUNDVOLUME"),
            [&ext](const LogicalLine& l, const QList<Operand>&) {
                return audioSetVolume(l, QStringLiteral("SETSOUNDVOLUME"), ext, false);
            });

    // EXISTSOUND(<字符串式>)：sound 目录下是否存在该资源（0/1）。
    // 对齐 EE ExistSoundMethod（ReturnType=long，实参 string）。
    ext.regExpr(QStringLiteral("EXISTSOUND"), OperandType::Int, 1, 1,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            AudioPipelinePool* pool = ext.audioPool();
            const QString name = a.isEmpty() ? QString() : a.at(0).toString();
            out = QVariant::fromValue<qint64>(pool && pool->sourceExists(name) ? 1 : 0);
            return true;
        });
}

// ---------------------------------------------------------------------------
// EM 私家版拡張（グラフィック系）------------------------------------------
//
// 权威来源：emuera.em（EE）`Runtime/Script/Statements/Function/Creator.Method.cs`
//     GraphicsClearMethod: argumentTypeArrayEx = [ {Int,Int}, {Int ×6} ]
//   即 GCLEAR 有两个形态（同一方法）：
//     GCLEAR id, cARGB                     -> 全图清除（核心 BuiltinOp::GClear）
//     GCLEAR id, cARGB, x, y, w, h         -> 只清除该矩形（SetClip + Clear + ResetClip）
//
//   核心命令本身不能被扩展覆盖（注册类 fail-fast 拒绝），所以这里用
//   regCoreArgRange 只**放宽核心命令的实参个数区间** —— 对齐 C# 给同一个
//   方法补第二个 argumentTypeArrayEx 形态；求值仍在核心的 GClear 分派处。
// ---------------------------------------------------------------------------
void registerEeGraphics(ExtensionRegistry& ext) {
    ext.regCoreArgRange(QStringLiteral("GCLEAR"), 2, 6);
}

}  // namespace

// ---------------------------------------------------------------------------
// EE 式中函数（ee readme）—— 实现住扩展侧；引擎只提供 services()
//
//   EXISTFUNCTION("関数名"{, 大文字小文字無視})
//       通常関数=1 / #FUNCTION=2 / #FUNCTIONS=3 / 未定義・システム組み込み=0
//   GETDOINGFUNCTION()  -> 現在実行中の関数名（__FUNCTION__ と同義）
//   GETDISPLAYLINE(<行番号>) -> 表示済み行の内容（0 起算、LINECOUNT は空）
//
// 三者都经注册类 regExpr() 登记：声明（返回类型/参数个数）注入运行期扩展函数表，
// 求值经 ExpressionEvaluator 的扩展回调转回 ExtensionRegistry::runExpression。
// ---------------------------------------------------------------------------
static void registerEeExpressionFunctions(ExtensionRegistry& ext)
{
    ext.regExpr(QStringLiteral("EXISTFUNCTION"), OperandType::Int, 1, 2,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const ExtensionRegistry::Services& sv = ext.services();
            const QString name = a.isEmpty() ? QString() : a.at(0).toString();
            const bool caseInsensitive = a.size() >= 2 && a.at(1).toLongLong() != 0;
            const int kind = sv.functionExists ? sv.functionExists(name, caseInsensitive) : 0;
            out = QVariant::fromValue<qint64>(kind);
            return true;
        });

    ext.regExpr(QStringLiteral("GETDOINGFUNCTION"), OperandType::Str, 0, 0,
        [&ext](const QList<QVariant>&, const QList<const ExpressionNode*>&, QVariant& out) {
            const ExtensionRegistry::Services& sv = ext.services();
            out = sv.doingFunction ? sv.doingFunction() : QString();
            return true;
        });

    ext.regExpr(QStringLiteral("GETDISPLAYLINE"), OperandType::Str, 1, 1,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const ExtensionRegistry::Services& sv = ext.services();
            const int lineNo = a.isEmpty() ? 0 : static_cast<int>(a.at(0).toLongLong());
            out = sv.displayLine ? sv.displayLine(lineNo) : QString();
            return true;
        });

    // ---- EE 内存族（C# Creator.Method.cs:7277/7294）--------------------------------
    // GETMEMORYUSAGE()：当前进程**工作集**字节数（C# Process.WorkingSet64）。
    //   本移植在 Linux 上取 /proc/self/statm 的驻留页数 × 页大小（RSS，语义最接近
    //   工作集）；其他平台回退 0（Qt 无跨平台工作集 API）。
    ext.regExpr(QStringLiteral("GETMEMORYUSAGE"), OperandType::Int, 0, 0,
        [](const QList<QVariant>&, const QList<const ExpressionNode*>&, QVariant& out) {
            out = QVariant::fromValue<qint64>(eeCurrentMemoryUsage());
            return true;
        });

    // CLEARMEMORY()：强制回收并返回**释放掉的字节数**
    //   （C#：GC.Collect() 前后各取 WorkingSet64 相减）。
    //   本移植用 glibc 的 malloc_trim(0) 把空闲堆还给 OS，再取 RSS 差值；
    //   无 malloc_trim 的平台回退 0（语义仍成立：**没有可释放的内存**）。
    ext.regExpr(QStringLiteral("CLEARMEMORY"), OperandType::Int, 0, 0,
        [](const QList<QVariant>&, const QList<const ExpressionNode*>&, QVariant& out) {
            const qint64 before = eeCurrentMemoryUsage();
            eeTrimHeap();
            const qint64 after = eeCurrentMemoryUsage();
            out = QVariant::fromValue<qint64>(qMax<qint64>(0, before - after));
            return true;
        });

    // ---- EE 文本框族（C# Creator.Method.cs:7318/7331）------------------------------
    // GETTEXTBOX：输入框当前内容（执行时点读一次）。
    ext.regExpr(QStringLiteral("GETTEXTBOX"), OperandType::Str, 0, 0,
        [&ext](const QList<QVariant>&, const QList<const ExpressionNode*>&, QVariant& out) {
            const ExtensionRegistry::Services& sv = ext.services();
            out = sv.textboxText ? sv.textboxText() : QString();
            return true;
        });
    // SETTEXTBOX(s)：整体替换输入框内容，**恒返回 1**（C# ChangeTextBoxMethod）。
    ext.regExpr(QStringLiteral("SETTEXTBOX"), OperandType::Int, 1, 1,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const ExtensionRegistry::Services& sv = ext.services();
            const QString text = a.isEmpty() ? QString() : a.at(0).toString();
            if (sv.setTextbox) sv.setTextbox(text);
            out = QVariant::fromValue<qint64>(1);   // C# 恒 1
            return true;
        });

    // ---- EE 系统输入扩展：FLOWINPUT / FLOWINPUTS（C# Creator.Method.cs:7423/7446）--
    // FLOWINPUT(<缺省值>{, <启用>, <MesSkip 可跳过>, <强制跳过>}) -> 恒返回 0；
    // FLOWINPUTS(<是否字符串模式>{, "<缺省字符串>"})             -> 恒返回 0。
    // 选项写进 ProcessState 的 flowinput*（**不自动复位**），由系统层输入消费。
    ext.regExpr(QStringLiteral("FLOWINPUT"), OperandType::Int, 1, 4,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const ExtensionRegistry::Services& sv = ext.services();
            const qint64 def = a.isEmpty() ? 0 : a.at(0).toLongLong();
            const bool enable  = a.size() > 1 && a.at(1).toLongLong() != 0;
            const bool canSkip = a.size() > 2 && a.at(2).toLongLong() != 0;
            const bool forceSkip = a.size() > 3 && a.at(3).toLongLong() != 0;
            if (sv.setFlowInput) sv.setFlowInput(def, enable, canSkip, forceSkip);
            out = QVariant::fromValue<qint64>(0);
            return true;
        });
    ext.regExpr(QStringLiteral("FLOWINPUTS"), OperandType::Int, 1, 2,
        [&ext](const QList<QVariant>& a, const QList<const ExpressionNode*>&, QVariant& out) {
            const ExtensionRegistry::Services& sv = ext.services();
            const bool isString = !a.isEmpty() && a.at(0).toLongLong() != 0;
            const QString defStr = a.size() > 1 ? a.at(1).toString() : QString();
            if (sv.setFlowInputString) sv.setFlowInputString(isString, defStr);
            out = QVariant::fromValue<qint64>(0);
            return true;
        });
}

// ---------------------------------------------------------------------------
// EE 扩展系统变量（fork 专有 CSV 变量）—— 实现住扩展侧；经注册类登记
//
// EmueraEM+EE readme：「・DAY、TIME、MONEYにCSVを適用可能に。各『DAY.csv』
// 『TIME.csv』『MONEY.csv』に対応しています。DAYNAME、TIMENAME、MONEYNAMEも実装」。
// 原版 Emuera 无 DAY/TIME/MONEY 的名表，也無 DAYNAME/TIMENAME/MONEYNAME ——
// 这些 CSV 变量专属于 fork（本移植此前把 DAYNAME / DAY→DAY.CSV 写死进核心：
// system_variables.h / variable_config.cpp / constant_table.cpp）。现移到扩展：
//   · regVariable  —— 登记变量本身（类型 + 默认一维长度）；
//   · regNameTable —— 为原生基础变量补名表（`DAY:天気` -> DAY.CSV）。
// 解析期经 systemVariableTypeDyn、名表经 ConstantTable::csvForVariable、
// 尺寸经 VariableConfig::getSize1D 分别兜底查这张扩展表。
// ---------------------------------------------------------------------------
static void registerEeVariables(ExtensionRegistry& ext)
{
    // DAY/TIME/MONEY 的 NAME 名表（EE readme；默认长度与 VariableSize.csv 惯例一致）。
    ext.regVariable(QStringLiteral("DAYNAME"), OperandType::Str, 1000);
    ext.regVariable(QStringLiteral("TIMENAME"), OperandType::Str, 1000);
    ext.regVariable(QStringLiteral("MONEYNAME"), OperandType::Str, 1000);
    // 基础变量补名表：`DAY:天気` / `TIME:…` / `MONEY:…` 的「名字 -> 下标」。
    ext.regNameTable(QStringLiteral("DAY"), QStringLiteral("DAY.CSV"));
    ext.regNameTable(QStringLiteral("TIME"), QStringLiteral("TIME.CSV"));
    ext.regNameTable(QStringLiteral("MONEY"), QStringLiteral("MONEY.CSV"));
}

// ---------------------------------------------------------------------------
// EE 扩展登记（注册类构造时一次调用；默认全启用）
// ---------------------------------------------------------------------------
void registerEeExtensions(ExtensionRegistry& ext)
{
    // fork 专有 CSV 系统变量（DAYNAME/TIMENAME/MONEYNAME + 基础变量名表）
    registerEeVariables(ext);

    // 式中函数（实现住本文件；见上）
    registerEeExpressionFunctions(ext);

    // 音频（C# 原版没有；C++ 控制 + QML 维护播放，数量由扩展登记不硬编码）
    registerEeAudio(ext);

    // EM 私家版拡張（GCLEAR 2/6 参：放宽核心命令的实参个数区间）
    registerEeGraphics(ext);

    // ---- EE 存档系：真实现（reg 带实现的重载；经注册类 services() 取用）----
    // 实现是三参函数（line, args, ext）——经 lambda 绑定注册类实例
    // （注册类与 lambda 同生命周期：lambda 存在注册类自己的表里）。
    ext.reg(QStringLiteral("CHKVARDATA"),
            [&ext](const LogicalLine& l, const QList<Operand>& a) { return chkVarData(l, a, ext); });
    ext.reg(QStringLiteral("CHKGLOBALDATA"),
            [&ext](const LogicalLine& l, const QList<Operand>& a) { return chkGlobalData(l, a, ext); });
    ext.reg(QStringLiteral("FIND_VARDATA"),
            [&ext](const LogicalLine& l, const QList<Operand>& a) { return findVarData(l, a, ext); });

    // ---- UPDATECHECK：真实现（结果写 RESULT；见 updateCheck）----
    ext.reg(QStringLiteral("UPDATECHECK"),
            [&ext](const LogicalLine&, const QList<Operand>&) -> bool {
                const int result = updateCheck(ext);
                if (ext.services().storage) {
                    ext.services().storage->setSystemVariable(QStringLiteral("RESULT"), 0, result);
                }
                return true;
            });

    // ---- EE 扩展命令（源: test/data/emuera_ee_cmds.txt，C# 权威源码导出）----
    //
    // 分两组登记（本轮的实现状态盘点）：
    //   ① kRunnerImplemented：名字在此登记（解析期认名 + 运行期兜底桩），
    //      **真实现住 ScriptRunner**（语句级流程/输入/内存语义，需要挂起/改
    //      执行状态，注册类拿不到那些能力，所以在 ScriptRunner 的指令分派里）。
    //      经 regExpr 登记过的名字不在此列（regExpr 自带解析登记）。
    //   ② kStubs：只有名字、真实现待补全 —— 统一「留痕一次 + 跳过」，不报错。
    //      （对齐 EE 的容错语义：未知/未实现的 EE 名字不应中断脚本。）
    static const QStringList kRunnerImplemented = {
        // BINPUT 族（组15/35）：EE v31fix 的「只接受已按钮化值」输入
        QStringLiteral("BINPUT"),
        QStringLiteral("BINPUTS"),
        QStringLiteral("ONEBINPUT"),        // BINPUT 的单字符版（C# OneInput）
        QStringLiteral("ONEBINPUTS"),
        // 流程控制（组12/12c/18/29）
        QStringLiteral("FORCE_BEGIN"),      // BEGIN + force（跳过 __CAN_BEGIN__）
        QStringLiteral("FORCE_QUIT"),       // 立即结束
        QStringLiteral("FORCE_QUIT_AND_RESTART"),
        QStringLiteral("QUIT_AND_RESTART"),
        // 输入（组15）
        QStringLiteral("INPUTANY"),         // 整数/字符串双通道输入
        // 显示/日志（组17）
        QStringLiteral("SKIPLOG"),          // MesSkip（MESSKIP()/WAIT 自动放行）
        // 调用族（组14/35）：TRY 版 CALLF
        QStringLiteral("TRYCALLF"),
        QStringLiteral("TRYCALLFORMF"),
    };
    for (const QString& name : kRunnerImplemented) {
        ext.reg(name);
    }

    static const QStringList kStubs = {
        // COLUMN 族（11 条）：EE 发行版附带的 **ERB 库 COLUMN_LIB** 的函数
        // （作者 Enter，基于 GDRAWTEXT），不是引擎内建命令 —— 真实游戏用
        // `CALL COLUMNCREATE, …` 调用自己附带的 ERB 实现（loader 像普通脚本一样
        // 扫描 ERB/ 目录，引擎不必登记）；`CALL` 找不到函数就报 label not found，
        // 与本登记无关。此处登记只为**裸写**形态（`COLUMNCREATE 0`）不报未知命令。
        // 语义见 test/data/commands/COLUMN*.md；CALL 形态的手写回归见**组 38**
        // （test/example/ERB/38_COLUMN_LIB.ERB + 夹具 COLUMN_LIB.ERB/ERH），
        // 覆盖报告把它单列「ERB 库（引擎无需实现）」，不计入引擎桩完成度。
        QStringLiteral("COLUMNBGCOLOR"),
        QStringLiteral("COLUMNCLEAR"),
        QStringLiteral("COLUMNCOLOR"),
        QStringLiteral("COLUMNCREATE"),
        QStringLiteral("COLUMNDIRECTION"),
        QStringLiteral("COLUMNMOVE"),
        QStringLiteral("COLUMNPRINT"),
        QStringLiteral("COLUMNPRINTL"),
        QStringLiteral("COLUMNPRINTW"),
        QStringLiteral("COLUMNRESIZE"),
        QStringLiteral("COLUMNWAIT"),
        // GUI 专属（本移植暂无对应后端：背景图 / 按钮世代 / 工具提示自绘 /
        // HTML 浮岛）。真实现需要 QML 侧能力，登记为桩以保持解析期容忍。
        QStringLiteral("BREAKBUTTON"),         // 作废当前画面全部按钮世代
        QStringLiteral("SETBGIMAGE"),          // 背景图（C# ConsoleBackground）
        QStringLiteral("CLEARBGIMAGE"),
        QStringLiteral("REMOVEBGIMAGE"),
        QStringLiteral("TOOLTIP_CUSTOM"),      // 工具提示 OwnerDraw 总开关
        QStringLiteral("TOOLTIP_FORMAT"),
        QStringLiteral("TOOLTIP_SETFONT"),
        QStringLiteral("TOOLTIP_SETFONTSIZE"),
        QStringLiteral("TOOLTIP_EXTENSION"),
        QStringLiteral("TOOLTIP_IMG"),
        QStringLiteral("HTML_PRINT_ISLAND"),   // HTML 浮岛（可滚动 HTML 层）
        QStringLiteral("HTML_PRINT_ISLAND_CLEAR"),
        // CALLSHARP / DT_COLUMN_OPTIONS：**已在 fork_extension.cpp 真实现**
        // （原生插件 ABI + `DEFAULT =` 解析），不再在此登记桩 —— 避免
        // first-wins 抢註（注册类后者会被拒绝）。
        // 作用域变量声明（EM+EE：VARI/VARS 声明函数私有变量）。C# 里由配置
        // 「VAR系命令を利用可能にする」门控，**默认关闭**（默认写 VARI 在解析期
        // 就报错）。本移植暂无该配置项，按「不可用」处理成静默跳过桩。
        QStringLiteral("VARI"),
        QStringLiteral("VARS"),
        // 图形族里尚未实现的部分（G 系画笔/字体/虚线与 G 绘图；见 test/data/commands/
        // GDRAWLINE.md 等）。真实现需要 Graphics 画布后端 + QML 绘制。
        QStringLiteral("GDASHSTYLE"),
        QStringLiteral("GDRAWGWITHROTATE"),
        QStringLiteral("GDRAWLINE"),
        QStringLiteral("GDRAWTEXT"),
        QStringLiteral("GGETFONT"),
        QStringLiteral("GGETFONTSIZE"),
        QStringLiteral("GGETPEN"),
        QStringLiteral("GGETPENWIDTH"),
        QStringLiteral("GGETTEXTSIZE"),
        QStringLiteral("SPRITEDISPOSEALL"),
        // 清单收录但**无据可考 / 疑似伪名**（文档已在 test/data/commands/ 记录）：
        QStringLiteral("GETTEXTSIZE"),          // EE readme 的笔误（实为 GETTEXTBOX）
        QStringLiteral("LCSVISASSI"),           // 任何文档/源码均无记载
        QStringLiteral("OCLEARLINE"),           // 仅名字，语义无权威来源
        QStringLiteral("FSTRJOIN"),             // 「F+STRJOIN」伪名（分栏场景）
        QStringLiteral("STRJOIN1"),             // 同上
        QStringLiteral("FTOOLTIP_SETDURATION"), // 「F+TOOLTIP_SETDURATION」伪名
        QStringLiteral("TINPUTAWAIT"),          // 「TINPUT+AWAIT」伪名
    };
    for (const QString& name : kStubs) {
        ext.reg(name);
    }
}
