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

#include "ee_extension.h"
#include "extension_registry.h"
#include "variable_storage.h"
#include "audio_pipeline_pool.h"

namespace {

// 对齐 C# EraDataState：0=OK / 1=FILENOTFOUND；RESULTS = 状态说明文本
constexpr int kDataOk = 0;
constexpr int kDataFileNotFound = 1;

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

    // ---- EE 存档系：真实现（reg 带实现的重载；经注册类 services() 取用）----
    // 实现是三参函数（line, args, ext）——经 lambda 绑定注册类实例
    // （注册类与 lambda 同生命周期：lambda 存在注册类自己的表里）。
    ext.reg(QStringLiteral("CHKVARDATA"),
            [&ext](const LogicalLine& l, const QList<Operand>& a) { return chkVarData(l, a, ext); });
    ext.reg(QStringLiteral("CHKGLOBALDATA"),
            [&ext](const LogicalLine& l, const QList<Operand>& a) { return chkGlobalData(l, a, ext); });
    ext.reg(QStringLiteral("FIND_VARDATA"),
            [&ext](const LogicalLine& l, const QList<Operand>& a) { return findVarData(l, a, ext); });

    // ---- EE 扩展命令（源: test/data/emuera_ee_cmds.txt，C# 权威源码导出）----
    // 只登记名字 -> 注册类统一「留痕一次 + 跳过」桩（待补全，不报错）。
    static const QStringList kEeCommands = {
        QStringLiteral("BINPUT"),
        QStringLiteral("BINPUTS"),
        QStringLiteral("CLEARMEMORY"),
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
        QStringLiteral("FLOWINPUT"),
        QStringLiteral("FORCE_BEGIN"),
        QStringLiteral("FORCE_QUIT"),
        QStringLiteral("FORCE_QUIT_AND_RESTART"),
        QStringLiteral("FSTRJOIN"),
        QStringLiteral("FTOOLTIP_SETDURATION"),
        QStringLiteral("GDASHSTYLE"),
        QStringLiteral("GDRAWGWITHROTATE"),
        QStringLiteral("GDRAWLINE"),
        QStringLiteral("GDRAWTEXT"),
        QStringLiteral("GETMEMORYUSAGE"),
        QStringLiteral("GETTEXTBOX"),
        QStringLiteral("GETTEXTSIZE"),
        QStringLiteral("GGETFONT"),
        QStringLiteral("GGETFONTSIZE"),
        QStringLiteral("GGETPEN"),
        QStringLiteral("GGETPENWIDTH"),
        QStringLiteral("GGETTEXTSIZE"),
        QStringLiteral("INPUTANY"),
        QStringLiteral("LCSVISASSI"),
        QStringLiteral("OCLEARLINE"),
        QStringLiteral("QUIT_AND_RESTART"),
        QStringLiteral("SETTEXTBOX"),
        QStringLiteral("SKIPLOG"),
        QStringLiteral("SPRITEDISPOSEALL"),
        QStringLiteral("STRJOIN1"),
        QStringLiteral("TINPUTAWAIT"),
        QStringLiteral("TOOLTIP_EXTENSION"),
        QStringLiteral("TOOLTIP_IMG"),
        QStringLiteral("TRYCALLF"),
        QStringLiteral("TRYCALLFORMF"),
        QStringLiteral("UPDATECHECK"),
    };
    for (const QString& name : kEeCommands) {
        ext.reg(name);
    }
}
