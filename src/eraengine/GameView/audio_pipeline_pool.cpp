#include "../GameData/game_paths.h"
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
#include "audio_pipeline_pool.h"

#include <QDir>
#include <QFileInfo>

namespace {

// 名称无扩展名时按常见音频格式依次探测（EE 的 Sound 支持 wav/ogg/其它 MF 格式）。
const QStringList& audioSuffixes() {
    static const QStringList kSuffixes = {
        QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("mp3"),
        QStringLiteral("wma"), QStringLiteral("m4a"), QStringLiteral("opus"),
        QStringLiteral("flac"),
    };
    return kSuffixes;
}

int clampVolume(int v) { return qBound(0, v, 100); }   // EE: Math.Clamp(volume, 0, 100)

}  // namespace

AudioPipelinePool::AudioPipelinePool(QObject* parent) : QObject(parent) {}

int AudioPipelinePool::activeChannels() const {
    int n = m_bgm.playing ? 1 : 0;
    for (const Channel& c : m_sounds)
        if (c.playing) ++n;
    return n;
}

void AudioPipelinePool::setSoundPipelines(quint32 value) {
    if (m_soundPipelines == value) return;
    const quint32 old = m_soundPipelines;
    m_soundPipelines = value;
    m_sounds.resize(value);
    // 缩小容量时停掉被裁掉的管线（并让 QML 释放它们）
    for (quint32 ch = value + 1; ch <= old; ++ch) emit channelStop(ch);
    emit capacityChanged();
    emit stateChanged();
}

void AudioPipelinePool::setSoundDirectory(const QString& dir) {
    if (m_soundDirectory == dir) return;
    m_soundDirectory = dir;
    emit soundDirectoryChanged();
}

QString AudioPipelinePool::resolveInDirectory(const QString& source) const {
    if (source.isEmpty()) return {};
    const QFileInfo direct(source);
    if (GamePaths::isContent(source) || direct.isAbsolute()) return direct.exists() ? direct.absoluteFilePath() : QString();

    // EE 的下载目录是 `<游戏目录>/sound/`（Program.SoundDir）；这里以它为**首选**，
    // 顺带兼容直接放在游戏根目录 / 常见子目录的情况。
    static const QStringList kSubdirs = {
        QStringLiteral("sound"), QStringLiteral("Sound"),
        QString(), QStringLiteral("BGM"), QStringLiteral("bgm"),
        QStringLiteral("SE"), QStringLiteral("se"),
    };
    const QString base = m_soundDirectory;
    const bool hasNameSuffix = !QFileInfo(source).suffix().isEmpty();
    const auto tryOne = [](const QString& path) -> QString {
        const QFileInfo fi(path);
        return fi.exists() && fi.isFile() ? fi.absoluteFilePath() : QString();
    };
    for (const QString& sub : kSubdirs) {
        const QString dir = sub.isEmpty() ? base : (base.isEmpty() ? sub : GamePaths::join(base, sub));
        if (dir.isEmpty()) continue;
        const QString prefix = GamePaths::join(dir, source);
        if (hasNameSuffix) {
            if (const QString hit = tryOne(prefix); !hit.isEmpty()) return hit;
        } else {
            for (const QString& ext : audioSuffixes()) {
                if (const QString hit = tryOne(GamePaths::join(dir, source + QLatin1Char('.') + ext)); !hit.isEmpty())
                    return hit;
            }
        }
    }
    return {};
}

QString AudioPipelinePool::resolveSource(const QString& source) const {
    const QString hit = resolveInDirectory(source);
    // 找不到磁盘文件时原样返回：让 QML 后端自己再报错（留痕），而不是静默丢弃请求。
    return hit.isEmpty() ? source.trimmed() : hit;
}

bool AudioPipelinePool::sourceExists(const QString& source) const {
    return !resolveInDirectory(source).isEmpty();
}

QString AudioPipelinePool::playBgm(const QString& source) {
    const QString resolved = resolveSource(source);
    if (resolved.isEmpty()) return {};
    if (m_bgm.playing && m_bgm.source == resolved) return resolved;   // 同曲在播 -> 不重启
    m_bgm.source = resolved;
    m_bgm.playing = true;
    emit channelPlay(kBgmChannel, resolved, m_bgmVolume, /*loops*/ -1);   // EE: repeat=-1
    touch();
    return resolved;
}

void AudioPipelinePool::stopBgm() {
    if (!m_bgm.playing && m_bgm.source.isEmpty()) return;
    m_bgm.playing = false;
    m_bgm.source.clear();
    emit channelStop(kBgmChannel);
    touch();
}

int AudioPipelinePool::findFreeSoundChannel() const {
    for (int i = 0; i < static_cast<int>(m_sounds.size()); ++i)
        if (!m_sounds.at(i).playing) return i;
    return -1;
}

QString AudioPipelinePool::playSound(const QString& source, int repeat) {
    const QString resolved = resolveSource(source);
    if (resolved.isEmpty()) return {};
    if (m_sounds.empty()) return resolved;   // 没有 SE 管线（audioPipelines=0）：不播
    int slot = findFreeSoundChannel();
    // EE：找不到空闲就用 0 号（覆盖最旧的一条），不报错
    if (slot < 0) slot = 0;
    const quint32 channel = static_cast<quint32>(slot) + 1;
    Channel& c = m_sounds.at(slot);
    c.source = resolved;
    c.playing = true;
    const int loops = repeat < 1 ? 1 : repeat;   // EE: Math.Max(opt, 1)
    emit channelPlay(channel, resolved, m_soundVolume, loops);
    touch();
    return resolved;
}

void AudioPipelinePool::stopSounds() {
    for (int i = 0; i < static_cast<int>(m_sounds.size()); ++i) {
        if (!m_sounds.at(i).playing) continue;
        m_sounds.at(i).playing = false;
        m_sounds.at(i).source.clear();
        emit channelStop(static_cast<quint32>(i) + 1);
    }
    touch();
}

void AudioPipelinePool::setBgmVolume(int volume) {
    const int v = clampVolume(volume);
    if (m_bgmVolume == v) return;
    m_bgmVolume = v;
    emit channelVolume(kBgmChannel, v);
    emit volumeChanged();
}

void AudioPipelinePool::setSoundVolume(int volume) {
    const int v = clampVolume(volume);
    if (m_soundVolume == v) return;
    m_soundVolume = v;
    for (quint32 ch = 1; ch <= m_soundPipelines; ++ch) emit channelVolume(ch, v);
    emit volumeChanged();
}

void AudioPipelinePool::reportFinished(quint32 channel) {
    if (channel <= kBgmChannel) return;   // BGM 循环，不会自然结束
    const int idx = static_cast<int>(channel) - 1;
    if (idx < 0 || idx >= static_cast<int>(m_sounds.size())) return;
    Channel& c = m_sounds.at(idx);
    if (!c.playing) return;
    c.playing = false;
    c.source.clear();
    touch();
}
