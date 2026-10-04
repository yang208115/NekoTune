import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NekoTune 1.0
Rectangle {
    color: "#112B3B"
    ExtensionApi { id: api }
    RowLayout {
        anchors.fill: parent; anchors.margins: 16
        Label { Layout.fillWidth: true; text: api.app.playback.song.title || "NekoTune"; color: "#ECF7FC" }
        Button { text: "Play / pause"; onClicked: api.app.playback.togglePlayPause() }
        Button { text: "Next"; onClicked: api.call("player.next") }
    }
}
