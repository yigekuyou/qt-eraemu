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
#pragma once

#include <QObject>
#include <QStringList>
#include <QtGlobal>
#include <QtQml/qqmlregistration.h>

#include <vector>

// ---------------------------------------------------------------------------
// AudioPipelinePool —— 音频播放「管线池」（C++ 侧控制端）
//
// 分工（音频不在 C# 原版内，属 EE 扩展能力）：
//   * **C++ 控制**：本类只决定「第几条管线该播什么、音量多少、循环几次」，
//     并以信号广播给 QML；不直接持有任何音频后端。
//   * **QML 维护播放**：QML 侧按 soundPipelines() 维护这么多条音效管线
//     （+ 1 条 BGM 管线），监听本类信号真正开始/停止播放，音效播完
//     （非循环时）回调 reportFinished(channel) 归还管线。
//
// 数量**不硬编码**：由扩展在注册时给出（`ExtensionRegistry::regAudioPipelines`）。
// 数值对齐 EE 源码（emuera.em）`Instraction.Child.cs`：
//     public static Sound[] sound = new Sound[10];   // 10 条音效(SE)管线
//     public static Sound bgm = new();               // 1 条独立 BGM 管线
// 即：soundPipelines() = 10（音效），BGM 另占 1 条，二者都是 quint32
// （4 字节无符号；没有程序同时播 2^32-1 条 BGM，故不会溢出）。
//
// 管线编号（对齐 EE 的「sound 数组 + bgm 单例」）：
//   0            = BGM（EE 的 `bgm`，PLAYBGM/STOPBGM/SETBGMVOLUME）
//   1..soundPipelines() = 音效 SE（EE 的 `sound[10]`，PLAYSOUND/STOPSOUND/SETSOUNDVOLUME）
// soundPipelines() == 0 表示音频能力被禁用（不产生任何信号）。
// ---------------------------------------------------------------------------
class AudioPipelinePool : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(AudioPipelinePool)

    // 音效(SE)管线数（4 字节无符号；0 = 禁用）。QML 用它决定维护几条 SE 播放器。
    Q_PROPERTY(quint32 soundPipelines READ soundPipelines WRITE setSoundPipelines
                   NOTIFY capacityChanged)
    // 总管线数 = soundPipelines + 1（BGM）
    Q_PROPERTY(quint32 totalPipelines READ totalPipelines NOTIFY capacityChanged)
    Q_PROPERTY(quint32 bgmChannel READ bgmChannel CONSTANT)

    // BGM 状态（QML/状态灯可读）
    Q_PROPERTY(QString bgmSource READ bgmSource NOTIFY stateChanged)
    Q_PROPERTY(bool bgmPlaying READ bgmPlaying NOTIFY stateChanged)
    Q_PROPERTY(int bgmVolume READ bgmVolume WRITE setBgmVolume NOTIFY volumeChanged)
    Q_PROPERTY(int soundVolume READ soundVolume WRITE setSoundVolume NOTIFY volumeChanged)
    // 当前处于「播放中」的管线数（0 表示没有音频在响；状态灯用）
    Q_PROPERTY(int activeChannels READ activeChannels NOTIFY stateChanged)

    // 音频资源检索目录（引擎在 setGameDirectory 之后设置；对应 EE 的 Program.SoundDir）
    Q_PROPERTY(QString soundDirectory READ soundDirectory WRITE setSoundDirectory
                   NOTIFY soundDirectoryChanged)

public:
    explicit AudioPipelinePool(QObject* parent = nullptr);

    [[nodiscard]] quint32 soundPipelines() const { return m_soundPipelines; }
    void setSoundPipelines(quint32 value);
    [[nodiscard]] quint32 totalPipelines() const { return m_soundPipelines + 1; }
    [[nodiscard]] quint32 bgmChannel() const { return kBgmChannel; }

    [[nodiscard]] QString bgmSource() const { return m_bgm.source; }
    [[nodiscard]] bool bgmPlaying() const { return m_bgm.playing; }
    [[nodiscard]] int bgmVolume() const { return m_bgmVolume; }
    [[nodiscard]] int soundVolume() const { return m_soundVolume; }
    [[nodiscard]] int activeChannels() const;

    [[nodiscard]] QString soundDirectory() const { return m_soundDirectory; }
    void setSoundDirectory(const QString& dir);

    // ---- C++ 控制 API（扩展的音频命令调用）--------------------------------
    // 返回「实际使用的资源名」：命中磁盘时是绝对路径，否则原样返回（空串 = 参数为空）。
    QString playBgm(const QString& source);              // 循环播放 BGM
    void stopBgm();                                      // STOPBGM
    // PLAYSOUND：挑一条空闲 SE 管线（全忙则用 0 号，对齐 EE `if (i >= sound.Length) i = 0`）；
    // repeat 为播放次数（EE 的 `Opt`，>=1）。
    QString playSound(const QString& source, int repeat = 1);
    void stopSounds();                                   // STOPSOUND（停全部 SE）
    void setBgmVolume(int volume);                       // 0..100（EE Math.Clamp）
    void setSoundVolume(int volume);                     // 0..100
    [[nodiscard]] bool sourceExists(const QString& source) const;   // EXISTSOUND
    [[nodiscard]] QString resolveSource(const QString& source) const;

    // QML 播放层回调：某条 SE 管线播完了（归还空闲池）
    Q_INVOKABLE void reportFinished(quint32 channel);

signals:
    // 交给 QML 播放维护层（由 QML 真正驱动音频后端）。
    // loops：-1 = 无限循环（BGM / 长音效），否则为播放次数（>=1）。
    void channelPlay(quint32 channel, const QString& source, int volume, int loops);
    void channelStop(quint32 channel);
    void channelVolume(quint32 channel, int volume);

    void capacityChanged();
    void stateChanged();
    void volumeChanged();
    void soundDirectoryChanged();

private:
    struct Channel {
        QString source;
        bool playing = false;
    };

    [[nodiscard]] int findFreeSoundChannel() const;
    [[nodiscard]] QString resolveInDirectory(const QString& source) const;
    void touch() { emit stateChanged(); }

    static constexpr quint32 kBgmChannel = 0;   // BGM 固定占用 0 号管线

    quint32 m_soundPipelines = 0;    // SE 管线数（4 字节无符号；由扩展登记，EE = 10）
    QString m_soundDirectory;
    Channel m_bgm;
    std::vector<Channel> m_sounds;   // 大小 = soundPipelines()
    int m_bgmVolume = 100;
    int m_soundVolume = 100;
};
