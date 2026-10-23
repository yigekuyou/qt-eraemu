import QtQuick
import QtTest

TestCase {
    id: tc
    name: "AudioPlayers"
    when: windowShown

    function test_pipelineCountAndVolumeBindings() {
        const audio = consoleFixture.createAudio(tc);
        const component = Qt.createComponent("../../../qml/AudioPlayers.qml");
        verify(component.status === Component.Ready, component.errorString());
        const players = component.createObject(tc, { audio: audio });
        verify(players !== null);
        function slots() {
            return players.children.filter(child => child.objectName === "audioSlot");
        }
        tryVerify(() => slots().length === audio.totalPipelines);
        audio.bgmVolume = 25;
        audio.soundVolume = 70;
        for (const slot of slots()) {
            const player = findChild(slot, "audioPlayer");
            verify(player !== null);
            compare(player.audioOutput.volume, slot.index === 0 ? 0.25 : 0.7);
        }
        audio.bgmVolume = 80;
        compare(findChild(slots()[0], "audioPlayer").audioOutput.volume, 0.8);
        audio.soundPipelines = 0;
        tryVerify(() => slots().length === 1);
        players.audio = null;
        tryVerify(() => slots().length === 0);
        players.destroy();
        audio.destroy();
    }

    // BGM 是 loops=-1，不存在「自然播完」；只有 InvalidMedia 能证明这条流已经废了。
    // 若不回收，池的 bgmPlaying/activeChannels 会永久为真（状态灯一直亮「播放中」）。
    function test_invalidMediaReleasesBgm() {
        const audio = consoleFixture.createAudio(tc);
        const component = Qt.createComponent("../../../qml/AudioPlayers.qml");
        verify(component.status === Component.Ready, component.errorString());
        const players = component.createObject(tc, { audio: audio });
        verify(players !== null);
        try {
            consoleFixture.playBgm(audio, "file:///nonexistent-emuera-audio-file.wav");
            verify(audio.bgmPlaying, "池先接受这次播放请求");
            tryVerify(() => !audio.bgmPlaying, 5000);
            compare(audio.activeChannels, 0);
        } finally {
            players.destroy();
            audio.destroy();
        }
    }
}
