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
// test_audio_pipeline.cpp
//
// 音频播放「管线池」回归（C# 原版没有音频；整块能力住在 EE 扩展）。
// 语义对齐权威源码 emuera.em `Instraction.Child.cs`：
//     public static Sound[] sound = new Sound[10];   // 10 条音效(SE)管线
//     public static Sound bgm = new();               // 1 条 BGM 管线
//   1. 数量由扩展登记、是 4 字节无符号（quint32），核心不硬编码「10」；
//   2. 管线布局：0 = BGM，1..N = SE（N = soundPipelines）；
//   3. PLAYSOUND：空闲优先，全忙用 0 号（对齐 EE `if (i >= sound.Length) i = 0`）；
//   4. 音量 Math.Clamp(0..100)；资源名解析（<dir>/sound/ + 自动补扩展名）；
//   5. 扩展侧：登记数量 + PLAYBGM/PLAYSOUND/STOPBGM/STOPSOUND/SET*VOLUME + EXISTSOUND。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <random>

#include "audio_pipeline_pool.h"
#include "extension_registry.h"
#include "ast/logical_line.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

// 造一个扩展语句行：functionName + 原文（实现按 line.raw 取「命令后的剩余部分」，
// 与引擎里语句型扩展一致）。
static LogicalLine stmtLine(const QString& name, const QString& remainder) {
    LogicalLine line;
    line.kind = LineKind::Instruction;
    line.functionName = name.toUpper();
    line.raw = remainder.isEmpty() ? name : name + QLatin1Char(' ') + remainder;
    return line;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    qDebug() << "Audio pipeline pool test";
    qDebug() << "========================";

    QTemporaryDir tmp;
    check(tmp.isValid(), "临时音频目录");
    QDir().mkpath(tmp.path() + QStringLiteral("/sound"));
    // 随机合成素材：真实写一段随机 PCM 的 WAV（8kHz 单声道 8bit）——
    // 「随机生成音频」而非空壳文件（名字随机、样本随机）。
    std::mt19937 rng(20261004u);
    const auto synthWav = [&rng](int samples, int freq) {
        QByteArray frames;
        frames.reserve(samples);
        for (int i = 0; i < samples; ++i) {
            const double phase = 2.0 * 3.14159265358979 * freq * i / 8000.0;
            int v = 127 + static_cast<int>(100.0 * std::sin(phase))
                    + static_cast<int>(rng() % 7) - 3;
            frames.append(static_cast<char>(qBound(0, v, 255)));
        }
        QByteArray wav = QByteArrayLiteral("RIFF");
        const quint32 dataLen = static_cast<quint32>(frames.size());
        auto le32 = [](quint32 v) {
            QByteArray b(4, '\0');
            b[0] = char(v & 0xFF); b[1] = char((v >> 8) & 0xFF);
            b[2] = char((v >> 16) & 0xFF); b[3] = char((v >> 24) & 0xFF);
            return b;
        };
        auto le16 = [](quint16 v) { QByteArray b(2, '\0'); b[0] = char(v & 0xFF); b[1] = char((v >> 8) & 0xFF); return b; };
        wav += le32(36 + dataLen) + QByteArrayLiteral("WAVEfmt ")
             + le32(16) + le16(1) + le16(1) + le32(8000) + le32(8000) + le16(1) + le16(8)
             + QByteArrayLiteral("data") + le32(dataLen) + frames;
        return wav;
    };
    int seq = 0;
    for (const QString& f : {QStringLiteral("bgm.ogg"), QStringLiteral("se1.wav"),
                             QStringLiteral("se2.mp3")}) {
        QFile file(tmp.path() + QStringLiteral("/sound/") + f);
        if (!file.open(QIODevice::WriteOnly)) continue;
        file.write(synthWav(200 + static_cast<int>(rng() % 400), 220 + 40 * (seq++)));
    }

    // =====================================================================
    qDebug() << "\n1) 管线布局（uint32 数量；0=BGM，1..N=SE）";
    {
        AudioPipelinePool pool;
        check(pool.soundPipelines() == 0 && pool.totalPipelines() == 1,
              "默认 soundPipelines=0（未登记 = 禁用），仅 BGM 管线骨架");
        pool.setSoundPipelines(10u);
        check(pool.soundPipelines() == 10u, "soundPipelines 为 quint32（4 字节无符号）");
        check(pool.bgmChannel() == 0u, "0 号管线 = BGM");
        check(pool.totalPipelines() == 11u, "总管线 = 1 BGM + 10 SE = 11（EE Sound[10]）");
    }

    // =====================================================================
    qDebug() << "\n2) BGM：循环 (loops=-1) / 同曲不重启 / 停止";
    {
        AudioPipelinePool pool;
        pool.setSoundPipelines(10u);
        pool.setSoundDirectory(tmp.path());
        int plays = 0, stops = 0;
        quint32 lastChannel = 99; int lastLoops = 0;
        QObject::connect(&pool, &AudioPipelinePool::channelPlay,
                         [&](quint32 ch, const QString&, int, int loops) {
                             ++plays; lastChannel = ch; lastLoops = loops;
                         });
        QObject::connect(&pool, &AudioPipelinePool::channelStop, [&](quint32) { ++stops; });
        const QString used = pool.playBgm(QStringLiteral("bgm"));
        check(used.endsWith(QStringLiteral("bgm.ogg")) && QFileInfo::exists(used),
              "PLAYBGM bgm -> 在 <dir>/sound/ 解析到 bgm.ogg");
        check(plays == 1 && lastChannel == 0u && lastLoops == -1,
              "BGM 走 0 号管线且无限循环（EE repeat=-1）");
        pool.playBgm(QStringLiteral("bgm"));
        check(plays == 1, "同曲在播 -> 不重启");
        pool.stopBgm();
        check(stops == 1 && !pool.bgmPlaying() && pool.activeChannels() == 0, "STOPBGM 回落");
    }

    // =====================================================================
    qDebug() << "\n3) SE：空闲分配 / 全忙用 0 号 / reportFinished 归还 / repeat";
    {
        AudioPipelinePool pool;
        pool.setSoundPipelines(2u);      // 1 BGM + 2 SE
        pool.setSoundDirectory(tmp.path());
        QHash<quint32, int> plays;
        QHash<quint32, int> loops;
        QObject::connect(&pool, &AudioPipelinePool::channelPlay,
                         [&](quint32 ch, const QString&, int, int l) { plays[ch] += 1; loops[ch] = l; });
        pool.playSound(QStringLiteral("se1"));
        pool.playSound(QStringLiteral("se2"));
        check(plays.value(1) == 1 && plays.value(2) == 1, "两条 SE 各占 1、2 号管线");
        check(loops.value(1) == 1, "PLAYSOUND 默认 repeat=1");
        check(pool.activeChannels() == 2, "activeChannels=2");
        pool.playSound(QStringLiteral("se1"), 3);   // 全忙 -> EE 用 0 号（1 号管线）
        check(plays.value(1) == 2 && loops.value(1) == 3,
              "全忙 -> 用 0 号 SE（管线 1）且 repeat=3");
        pool.reportFinished(2u);
        check(pool.activeChannels() == 1, "reportFinished(2) 归还一条");
        pool.playSound(QStringLiteral("se2"));       // 有空闲（2）-> 复用
        check(plays.value(2) == 2 && pool.activeChannels() == 2, "空闲管线被复用");
    }

    // =====================================================================
    qDebug() << "\n4) 音量夹取 + 探测";
    {
        AudioPipelinePool pool;
        pool.setSoundPipelines(5u);
        pool.setSoundDirectory(tmp.path());
        pool.setBgmVolume(150);
        pool.setSoundVolume(-3);
        check(pool.bgmVolume() == 100 && pool.soundVolume() == 0, "音量夹取到 0..100");
        check(pool.sourceExists(QStringLiteral("bgm")), "EXISTSOUND 命中（sound/ + 补扩展名）");
        check(!pool.sourceExists(QStringLiteral("nope")), "EXISTSOUND 未命中");
    }

    // =====================================================================
    qDebug() << "\n5) 扩展侧：登记数量 + 音频命令";
    {
        AudioPipelinePool pool;
        ExtensionRegistry ext;
        // 注入最小「表达式求值」服务（引擎装配时注入真求值器）：
        // 让 PLAYBGM 变量式 / SETBGMVOLUME 表达式走「求值」而非字面量回退。
        {
            ExtensionRegistry::Services sv;
            sv.evaluate = [](const QString& e) -> QVariant {
                const QString t = e.trimmed();
                if (t == QLatin1String("F")) return QVariant(QStringLiteral("bgm"));
                if (t == QLatin1String("30 + 12")) return QVariant(42);
                return {};
            };
            ext.setServices(sv);
        }
        check(ext.audioPipelines() == 10u,
              "扩展登记 SE 管线数 = 10（对齐 EE Sound[10]；核心不含此常量）");
        check(ext.hasStatement(QStringLiteral("PLAYBGM"))
                  && ext.hasStatement(QStringLiteral("PLAYSOUND"))
                  && ext.hasStatement(QStringLiteral("STOPBGM"))
                  && ext.hasStatement(QStringLiteral("STOPSOUND"))
                  && ext.hasStatement(QStringLiteral("SETBGMVOLUME"))
                  && ext.hasStatement(QStringLiteral("SETSOUNDVOLUME")),
              "音频命令在扩展注册类中可见");
        ext.setAudioPool(&pool);
        pool.setSoundPipelines(ext.audioPipelines());
        pool.setSoundDirectory(tmp.path());
        check(pool.soundPipelines() == 10u && pool.totalPipelines() == 11u,
              "引擎按扩展数量装配播放池（1 BGM + 10 SE）");

        check(ext.runStatement(QStringLiteral("PLAYBGM"),
                               stmtLine(QStringLiteral("PLAYBGM"), QStringLiteral("\"bgm\"")))
                  && pool.bgmPlaying(), "PLAYBGM \"bgm\" -> 池开始播 BGM");
        pool.stopBgm();
        check(ext.runStatement(QStringLiteral("PLAYBGM"),
                               stmtLine(QStringLiteral("PLAYBGM"), QStringLiteral("F")))
                  && pool.bgmPlaying()
                  && pool.bgmSource().endsWith(QStringLiteral("bgm.ogg")),
              "PLAYBGM F（变量式，经求值服务）-> bgm.ogg");
        ext.runStatement(QStringLiteral("PLAYSOUND"),
                         stmtLine(QStringLiteral("PLAYSOUND"), QStringLiteral("\"se1\", 2")));
        check(pool.activeChannels() == 2, "PLAYSOUND \"se1\", 2 -> 一条 SE 在响");
        ext.runStatement(QStringLiteral("SETBGMVOLUME"),
                         stmtLine(QStringLiteral("SETBGMVOLUME"), QStringLiteral("30 + 12")));
        check(pool.bgmVolume() == 42, "SETBGMVOLUME 30 + 12（整数式经求值服务 -> 42）");
        ext.runStatement(QStringLiteral("STOPBGM"), stmtLine(QStringLiteral("STOPBGM"), {}));
        ext.runStatement(QStringLiteral("STOPSOUND"), stmtLine(QStringLiteral("STOPSOUND"), {}));
        check(pool.activeChannels() == 0, "STOPBGM + STOPSOUND -> 全部停");

        QVariant out;
        check(ext.runExpression(QStringLiteral("EXISTSOUND"),
                                {QVariant(QStringLiteral("bgm"))}, {}, out)
                  && out.toLongLong() == 1, "式中 EXISTSOUND(\"bgm\") -> 1");
        out = QVariant();
        check(ext.runExpression(QStringLiteral("EXISTSOUND"),
                                {QVariant(QStringLiteral("nope"))}, {}, out)
                  && out.toLongLong() == 0, "式中 EXISTSOUND(\"nope\") -> 0");
    }

    qDebug() << "\n========================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] audio pipeline tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
