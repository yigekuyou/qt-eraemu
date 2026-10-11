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
}
