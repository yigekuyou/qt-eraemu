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
import QtQuick
import QtMultimedia
import io.yigekuoyou.eraengine

// ---------------------------------------------------------------------------
// AudioPlayers —— 音频「播放维护层」（QML 侧）
//
// 分工：C++ 的 AudioPipelinePool 只**决定**第几条管线该播什么（控制端），
// 真正出声在这里：QML 按 audio.capacity 维护对应数量的播放器 ——
//   * 0 号管线 = BGM：MediaPlayer（循环）；
//   * 1..capacity-1 = SE：SoundEffect（一次性，播完回调 reportFinished 归还）。
//
// 数量来自 C++（扩展登记的 4 字节无符号数），QML 不硬编码「10」——
// 改扩展的登记值，这里维护的播放器数量随之变化。
// ---------------------------------------------------------------------------
Item {
    id: root
    visible: false
    width: 0
    height: 0

    // 由外层注入（eraEngine.audio）
    property AudioPipelinePool audio: null
    // 当前正在出声的管线数（供状态灯显示音频活动）
    readonly property int activeChannels: audio ? audio.activeChannels : 0

    function toUrl(s) {
        if (!s)
            return "";
        if (s.indexOf("file:") === 0 || s.indexOf("qrc:") === 0 || s.indexOf("http") === 0)
            return s;
        return "file://" + s;
    }

    // ---- BGM（0 号管线，EE 的 `Sound bgm`）：循环 / 指定次数 ----
    MediaPlayer {
        id: bgmPlayer
        audioOutput: AudioOutput {
            volume: root.audio ? Math.max(0, Math.min(1, root.audio.bgmVolume / 100)) : 1
        }
        loops: MediaPlayer.Infinite
    }

    // ---- SE（1..N 号管线，EE 的 `Sound[10]`）：每条一个播放器 ----
    // 数量来自 C++（扩展登记的 4 字节无符号数），QML 不硬编码「10」。
    // Repeater 的 delegate 必须是 Item，故用隐藏 Item 包一层 SoundEffect。
    Repeater {
        id: seRepeater
        model: root.audio ? root.audio.soundPipelines : 0
        delegate: Item {
            id: seSlot
            // index 从 0 起，对应管线 = index + 1
            readonly property int pipelineChannel: index + 1
            width: 0
            height: 0
            visible: false

            function playSource(u, loops) {
                se.loops = (loops === -1 ? SoundEffect.Infinite : Math.max(1, loops));
                se.source = u;
                se.play();
            }
            function stopSource() {
                se.stop();
                se.source = "";
            }
            function setVolume(v) {
                se.volume = v;
            }

            SoundEffect {
                id: se
                volume: root.audio ? Math.max(0, Math.min(1, root.audio.soundVolume / 100)) : 1
                // 播完（自然结束）-> 归还管线
                onPlayingChanged: {
                    if (!playing && source !== "" && root.audio)
                        root.audio.reportFinished(seSlot.pipelineChannel);
                }
            }
        }
    }

    // ---- 监听 C++ 控制端的播放请求 ----
    Connections {
        target: root.audio
        enabled: root.audio !== null

        function onChannelPlay(channel, source, volume, loops) {
            if (channel === 0) {
                bgmPlayer.loops = (loops === -1 ? MediaPlayer.Infinite : Math.max(1, loops));
                bgmPlayer.source = root.toUrl(source);
                bgmPlayer.play();
            } else {
                const item = seRepeater.itemAt(channel - 1);
                if (item)
                    item.playSource(root.toUrl(source), loops);
            }
        }

        function onChannelStop(channel) {
            if (channel === 0) {
                bgmPlayer.stop();
                bgmPlayer.source = "";
            } else {
                const item = seRepeater.itemAt(channel - 1);
                if (item)
                    item.stopSource();
            }
        }

        function onChannelVolume(channel, volume) {
            if (channel === 0) {
                bgmPlayer.audioOutput.volume = Math.max(0, Math.min(1, volume / 100));
            } else {
                const item = seRepeater.itemAt(channel - 1);
                if (item)
                    item.setVolume(Math.max(0, Math.min(1, volume / 100)));
            }
        }
    }
}
