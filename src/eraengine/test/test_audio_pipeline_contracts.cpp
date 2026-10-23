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
// test_audio_pipeline_contracts.cpp
//
// 音频管线池的**播放契约**（不依赖声卡：只断言「池对 QML 发出了什么」）。
// 与 test_audio_pipeline.cpp 的分工：
//   * 那边测「布局 / 分配 / 解析 / 扩展命令接线」；
//   * 这边测「重复播放 / 切换 / 容量收缩 / BGM 与 SE 的归还边界」这些
//     只有真实播放序列才会走到的状态机契约。
//
// 权威语义来自 emuera.em `Runtime/Utils/Sound.NAudio.cs`：
//     public void play(string filename, int repeat = 1)
//     {
//         stop();                 // <- 每次都先停，同一文件再 PLAY 会从头开始
//         ...
//     }
// 以及 `Instraction.Child.cs` 的 PLAYBGM（repeat = -1）/ PLAYSOUND（Opt 次）。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "audio_pipeline_pool.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

namespace {

QByteArray tinyWav() {
    // 8 kHz 单声道 8bit 的极小 WAV（内容不重要：本测试只走池，不解码）。
    QByteArray frames(80, '\x80');
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
}

struct Recorder {
    struct Play { quint32 channel; QString source; int volume; int loops; };
    QList<Play> plays;
    QList<quint32> stops;
    QList<QPair<quint32, int>> volumes;
    void attach(AudioPipelinePool& pool) {
        QObject::connect(&pool, &AudioPipelinePool::channelPlay,
                         [this](quint32 c, const QString& s, int v, int l) {
                             plays.append({c, s, v, l});
                         });
        QObject::connect(&pool, &AudioPipelinePool::channelStop,
                         [this](quint32 c) { stops.append(c); });
        QObject::connect(&pool, &AudioPipelinePool::channelVolume,
                         [this](quint32 c, int v) { volumes.append({c, v}); });
    }
    [[nodiscard]] int playsOn(quint32 ch) const {
        int n = 0;
        for (const Play& p : plays) if (p.channel == ch) ++n;
        return n;
    }
};

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    qDebug() << "Audio pipeline contracts test";
    qDebug() << "============================";

    QTemporaryDir tmp;
    check(tmp.isValid(), "临时音频目录");
    QDir().mkpath(tmp.path() + QStringLiteral("/sound"));
    for (const QString& name : {QStringLiteral("a.wav"), QStringLiteral("b.wav"),
                                QStringLiteral("se1.wav"), QStringLiteral("se2.wav"),
                                QStringLiteral("se3.wav")}) {
        QFile f(tmp.path() + QStringLiteral("/sound/") + name);
        if (f.open(QIODevice::WriteOnly)) f.write(tinyWav());
    }

    // =====================================================================
    qDebug() << "\n1) PLAYBGM 同一文件两次：每次都要重新开始（EE Sound.play 先 stop()）";
    {
        AudioPipelinePool pool;
        pool.setSoundPipelines(10u);
        pool.setSoundDirectory(tmp.path());
        Recorder rec;
        rec.attach(pool);

        const QString first = pool.playBgm(QStringLiteral("a"));
        check(first.endsWith(QStringLiteral("a.wav")), "首次 PLAYBGM 解析到 a.wav");
        check(rec.plays.size() == 1 && rec.plays.last().channel == 0u
                  && rec.plays.last().loops == -1,
              "首次：0 号管线、loops=-1");

        // 第二次请求同一资源：EE 会 stop() + 重新打开文件 -> 从头播放。
        const QString second = pool.playBgm(QStringLiteral("a"));
        check(second == first, "第二次解析到同一路径");
        check(rec.playsOn(0u) == 2,
              "同一 BGM 再次 PLAYBGM 必须再发一次 channelPlay（从头发起，而不是忽略）");
        check(pool.bgmPlaying() && pool.activeChannels() == 1,
              "状态仍为「播放中」，且只有一条管线在响");
    }

    // =====================================================================
    qDebug() << "\n2) PLAYBGM 换曲 / STOP 后再播";
    {
        AudioPipelinePool pool;
        pool.setSoundPipelines(10u);
        pool.setSoundDirectory(tmp.path());
        Recorder rec;
        rec.attach(pool);
        pool.playBgm(QStringLiteral("a"));
        pool.playBgm(QStringLiteral("b"));
        check(rec.playsOn(0u) == 2 && pool.bgmSource().endsWith(QStringLiteral("b.wav")),
              "换曲：0 号管线再发一次 channelPlay，源切到 b.wav");
        pool.stopBgm();
        check(rec.stops.count(0u) == 1 && !pool.bgmPlaying(), "STOPBGM：发 channelStop(0)");
        pool.playBgm(QStringLiteral("b"));
        check(rec.playsOn(0u) == 3 && pool.bgmPlaying(), "STOP 后同一首再播：重新发 channelPlay");
    }

    // =====================================================================
    qDebug() << "\n3) reportFinished 的边界：BGM（0 号）不受理，SE 归还空闲池";
    {
        AudioPipelinePool pool;
        pool.setSoundPipelines(2u);
        pool.setSoundDirectory(tmp.path());
        Recorder rec;
        rec.attach(pool);
        pool.playBgm(QStringLiteral("a"));
        pool.reportFinished(0u);   // QML 对 BGM 的 EndOfMedia 不应走到这里，但也不能误清状态
        check(pool.bgmPlaying() && pool.activeChannels() == 1,
              "reportFinished(0) 不得清掉 BGM（BGM 是 loops=-1，永不自然结束）");
        pool.playSound(QStringLiteral("se1"));
        pool.playSound(QStringLiteral("se2"));
        check(pool.activeChannels() == 3, "BGM + 2 SE = 3 条在响");
        pool.reportFinished(1u);
        check(pool.activeChannels() == 2, "SE 播完归还：只减 SE，不减 BGM");
        pool.reportFinished(99u);   // 越界：忽略
        pool.reportFinished(0u);
        check(pool.activeChannels() == 2 && pool.bgmPlaying(),
              "越界/0 号 reportFinished 被忽略，状态不变");
        check(rec.stops.isEmpty(), "整段没有多余的 channelStop（归还不是 Stop）");
    }

    // =====================================================================
    qDebug() << "\n4) 容量收缩：只停被裁掉的 SE，保留仍在容量内的管线";
    {
        AudioPipelinePool pool;
        pool.setSoundPipelines(3u);
        pool.setSoundDirectory(tmp.path());
        Recorder rec;
        rec.attach(pool);
        pool.playSound(QStringLiteral("se1"));   // 1
        pool.playSound(QStringLiteral("se2"));   // 2
        pool.playSound(QStringLiteral("se3"));   // 3
        check(pool.activeChannels() == 3, "3 条 SE 全在响");
        pool.setSoundPipelines(1u);              // 3、2 被裁掉
        check(rec.stops.contains(3u) && rec.stops.contains(2u),
              "被裁掉的 2、3 号管线收到 channelStop");
        check(!rec.stops.contains(1u), "保留的 1 号管线没有被停");
        check(pool.totalPipelines() == 2u && pool.activeChannels() == 1,
              "容量 = 1 SE + 1 BGM；activeChannels 只计保留的那条");
        check(pool.soundPipelines() == 1u, "soundPipelines 已更新");
    }

    // =====================================================================
    qDebug() << "\n5) 0 条 SE 管线（音频禁用）：PLAYSOUND 不产生任何播放";
    {
        AudioPipelinePool pool;
        pool.setSoundPipelines(0u);   // 只剩 BGM
        pool.setSoundDirectory(tmp.path());
        Recorder rec;
        rec.attach(pool);
        const QString used = pool.playSound(QStringLiteral("se1"));
        check(used.endsWith(QStringLiteral("se1.wav")), "资源仍可解析（供 EXISTSOUND 用）");
        check(rec.plays.isEmpty() && pool.activeChannels() == 0,
              "但没有 SE 管线 -> 不发 channelPlay（不假装在播）");
        pool.playBgm(QStringLiteral("a"));
        check(rec.playsOn(0u) == 1 && pool.activeChannels() == 1,
              "BGM 管线独立于 SE 数量，仍然可用");
    }

    // =====================================================================
    qDebug() << "\n6) 音量：BGM/SE 分开，SE 广播到所有管线，0..100 夹取";
    {
        AudioPipelinePool pool;
        pool.setSoundPipelines(3u);
        Recorder rec;
        rec.attach(pool);
        pool.setBgmVolume(40);
        check(rec.volumes.count({0u, 40}) == 1, "SETBGMVOLUME -> 只发给 0 号");
        rec.volumes.clear();
        pool.setSoundVolume(70);
        check(rec.volumes.size() == 3, "SETSOUNDVOLUME -> 发给 1..3 全部 SE 管线");
        bool all = true;
        for (int ch = 1; ch <= 3; ++ch) {
            bool hit = false;
            for (const auto& v : rec.volumes) if (v.first == quint32(ch) && v.second == 70) hit = true;
            all = all && hit;
        }
        check(all, "每条 SE 管线都收到 70");
        rec.volumes.clear();
        pool.setSoundVolume(70);   // 不变 -> 不重复广播
        check(rec.volumes.isEmpty(), "同值不重复广播（避免无谓的属性写入）");
        pool.setBgmVolume(1000);
        check(pool.bgmVolume() == 100, "上限夹取到 100");
    }

    qDebug() << "\n============================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] audio pipeline contract tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}