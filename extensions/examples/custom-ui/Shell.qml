import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NekoTune 1.0
Rectangle {
    color: "#091923"
    ExtensionApi { id: api }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 40; spacing: 24
        RowLayout {
            Label { Layout.fillWidth: true; text: "NekoTune / Ocean"; color: "#ECF7FC"; font.pixelSize: 28 }
            Button { text: "Extensions"; onClicked: api.app.extensions.openManager() }
            Button { text: "Restore default"; onClicked: api.app.extensions.resetInterface() }
        }
        Item { Layout.fillHeight: true }
        Label { Layout.fillWidth: true; text: api.app.playback.song.title || "Choose a track from your queue"; color: "#ECF7FC"; font.pixelSize: 32; elide: Text.ElideRight }
        Label { text: api.app.playback.song.artist || ""; color: "#8BD5EC"; font.pixelSize: 18 }
        RowLayout {
            Button { text: "Previous"; onClicked: api.call("player.previous") }
            Button { text: "Play / pause"; onClicked: api.app.playback.togglePlayPause() }
            Button { text: "Next"; onClicked: api.call("player.next") }
        }
        ListView {
            Layout.fillWidth: true; Layout.preferredHeight: 220; clip: true; model: api.app.queue.model
            delegate: ItemDelegate {
                required property var modelData
                width: ListView.view.width; text: modelData.title
                onClicked: api.app.queue.playQueueItem(modelData.queue_id)
            }
        }
    }
}
