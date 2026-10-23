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
// Qt 文档（qmllint / ComponentBehavior: Bound）：SE 槽是 Repeater 的委托（嵌套
// 组件），里面引用外层 root 的 id 与属性在 Bound 下走编译期绑定；模型注入的
// index 必须显式声明为 required property。
pragma ComponentBehavior: Bound

import QtQuick
import QtMultimedia
import io.yigekuoyou.eraengine

// 每条管线使用一个 MediaPlayer：0 为 BGM，其余为音效。
Item {
    id: root
    visible: false
    width: 0
    height: 0

    property AudioPipelinePool audio: null
    readonly property int activeChannels: audio ? audio.activeChannels : 0

    function toUrl(s) {
        if (!s)
            return "";
        if (s.indexOf("content:") === 0 || s.indexOf("file:") === 0 || s.indexOf("qrc:") === 0 || s.indexOf("http") === 0)
            return s;
        return "file://" + s;
    }

    Repeater {
        model: root.audio ? root.audio.totalPipelines : 0
        delegate: Item {
            id: slot
            objectName: "audioSlot"
            required property int index
            width: 0
            height: 0

            MediaPlayer {
                id: player
                objectName: "audioPlayer"
                audioOutput: AudioOutput {
                    // 声明式跟随音量属性，避免命令式赋值打断绑定。
                    volume: root.audio ? (slot.index === 0 ? root.audio.bgmVolume : root.audio.soundVolume) / 100 : 1
                }
                onMediaStatusChanged: function(status) {
                    if (!root.audio)
                        return;
                    if (status === MediaPlayer.InvalidMedia && slot.index === 0) {
                        // BGM 是 loops=-1，永远不会 EndOfMedia；坏文件/不存在的文件
                        // 只能靠 InvalidMedia 回收，否则「播放中」状态永久挂着。
                        root.audio.stopBgm();
                        return;
                    }
                    if (slot.index > 0
                        && (status === MediaPlayer.EndOfMedia || status === MediaPlayer.InvalidMedia))
                        root.audio.reportFinished(slot.index);
                }
            }

            Connections {
                target: root.audio
                function onChannelPlay(channel, source, volume, loops) {
                    if (channel !== slot.index)
                        return;
                    const url = root.toUrl(source);
                    player.loops = loops === -1 ? MediaPlayer.Infinite : Math.max(1, loops);
                    // 同一文件再次 PLAY（EE：每次 play() 都先 stop() 再从头播）：
                    // 只把 source 赋成同一个 URL，MediaPlayer 视为「没变化」而不重载，
                    // 表现为「点了重播却接着上一半」。同源时显式回到 0 再播。
                    if (player.source.toString() === url) {
                        player.position = 0;
                        player.play();
                        return;
                    }
                    player.source = url;
                    player.play();
                }
                function onChannelStop(channel) {
                    if (channel !== slot.index)
                        return;
                    player.stop();
                    player.source = "";
                }
            }
        }
    }
}
