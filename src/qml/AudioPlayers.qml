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
    //
    // 后端选型（Qt 文档）：SoundEffect 只支持**未压缩 WAV**（低延迟），
    // 而 EE 的 Sound 支持 wav/ogg/mp3/…（MF 解码）—— 用 SoundEffect 播
    // ogg 会 status=Error 且**从不发声**，管线还被占住不放。这里统一用
    // MediaPlayer（Qt Multimedia 文档：格式更全、资源占用更低），格式对齐 EE。
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
                se.loops = (loops === -1 ? MediaPlayer.Infinite : Math.max(1, loops));
                se.source = u;
                se.play();
            }
            function stopSource() {
                se.stop();
                se.source = "";
            }

            MediaPlayer {
                id: se
                audioOutput: AudioOutput {
                    volume: root.audio ? Math.max(0, Math.min(1, root.audio.soundVolume / 100)) : 1
                }
                // 播放结束（自然播完）或解码失败（InvalidMedia）-> 归还管线。
                // 换源不会误报：换 source 后 status 走 Loading，不会发 EndOfMedia
                //（此前 SoundEffect 的 onPlayingChanged(false) 在「替换管线上的
                // 旧音效」时会把刚排上的新音效误标成已结束，导致管线被重复占用）。
                onMediaStatusChanged: {
                    if ((status === MediaPlayer.EndOfMedia || status === MediaPlayer.InvalidMedia)
                        && root.audio)
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
            // SE 的音量由 delegate 里的声明式绑定跟随 soundVolume 属性自动更新
            //（此前 setVolume 用命令式赋值**打断绑定**，之后改全局音量不再生效）。
            if (channel === 0) {
                bgmPlayer.audioOutput.volume = Math.max(0, Math.min(1, volume / 100));
            }
        }
    }
}
