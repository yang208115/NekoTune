import QtQuick
import QtQuick.Controls
import NekoTune 1.0
Rectangle {
    color: "#183747"
    ExtensionApi { id: api }
    Label { anchors.centerIn: parent; text: "Extension toolbar · " + (api.app.playback.song.title || "NekoTune"); color: "#8BD5EC" }
}
