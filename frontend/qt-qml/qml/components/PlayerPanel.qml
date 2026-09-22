import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var song: ({})
    property var lyrics: ({})
    property var queue: []
    property real duration: 0
    property real position: 0
    property real volume: 0.8
    property string playbackState: "stopped"
    property bool connected: false
    property string errorText: ""
    property color panelColor: "#17151e"
    property color lineColor: "#292532"
    property color textStrongColor: "#f7f4fb"
    property color textSoftColor: "#c6c0d1"
    property color textMutedColor: "#9b96a8"
    property color mintColor: "#cbb8ff"
    property color amberColor: "#e8a9c3"
    readonly property bool hasSong: Boolean(root.song && root.song.song_id)
    readonly property int currentIndex: { for (let index = 0; index < root.queue.length; index += 1) if (root.queue[index].state === "current") return index; return -1 }
    signal addRequested()
    signal togglePlayPauseRequested()
    signal nextRequested()
    signal previousRequested()
    signal seekRequested(real positionMs)
    signal volumeRequested(real value)
    signal editMetadataRequested()
    spacing: 14

    RowLayout { Layout.fillWidth: true; spacing: 12
        ColumnLayout { Layout.fillWidth: true; spacing: 2
            Label { text: root.hasSong ? i18n.text("now_playing", i18n.language) : i18n.text("ready", i18n.language); color: root.mintColor; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 1.8 }
            Label { text: root.hasSong ? (root.song.title || i18n.text("untitled", i18n.language)) : i18n.text("no_track_selected", i18n.language); color: root.textStrongColor; font.pixelSize: 26; font.weight: Font.DemiBold; elide: Text.ElideRight }
        }
        TextButton { text: i18n.text("add_music", i18n.language); implicitWidth: 124; onClicked: root.addRequested() }
    }

    Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: root.panelColor; radius: 24; border.color: root.lineColor; border.width: 1
        RowLayout { anchors.fill: parent; anchors.margins: 28; spacing: 30
            AlbumArt { Layout.preferredWidth: Math.min(370, parent.width * 0.46); Layout.fillHeight: true; hasSong: root.hasSong; playbackState: root.playbackState; textMutedColor: root.textMutedColor; mintColor: root.mintColor; amberColor: root.amberColor }
            ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 14
                Label { Layout.fillWidth: true; text: root.hasSong ? (root.song.artist || i18n.text("artist_author", i18n.language)) : i18n.text("choose_audio", i18n.language); color: root.textSoftColor; font.pixelSize: 14; elide: Text.ElideRight }
                Label { Layout.fillWidth: true; text: root.hasSong ? (root.song.album || i18n.text("album", i18n.language)) : ""; color: root.textMutedColor; font.pixelSize: 12; elide: Text.ElideRight }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.lineColor }
                LyricsPanel { Layout.fillWidth: true; Layout.fillHeight: true; visible: root.hasSong; song: root.song; lyrics: root.lyrics; position: root.position; connected: root.connected }
                Item { Layout.fillHeight: true; visible: !root.hasSong }
                RowLayout { Layout.fillWidth: true; visible: root.hasSong; spacing: 8
                    Label { text: formatTime(root.position); color: root.textMutedColor; font.pixelSize: 11 }
                    PlayerSlider { Layout.fillWidth: true; from: 0; to: Math.max(root.duration, 1); value: Math.min(root.position, Math.max(root.duration, 1)); enabled: root.duration > 0; activeColor: root.mintColor; baseColor: "#3a3547"; live: false; onMoved: root.seekRequested(value) }
                    Label { text: formatTime(root.duration); color: root.textMutedColor; font.pixelSize: 11 }
                }
                RowLayout { Layout.fillWidth: true; spacing: 8
                    Item { Layout.fillWidth: true }
                    IconButton { kind: "back"; tooltipText: i18n.text("previous", i18n.language); enabled: root.queue.length > 0; onClicked: root.previousRequested() }
                    IconButton { kind: root.playbackState === "playing" ? "pause" : "play"; tooltipText: root.playbackState === "playing" ? i18n.text("pause", i18n.language) : i18n.text("play", i18n.language); enabled: root.hasSong || root.queue.length > 0; implicitWidth: 52; implicitHeight: 46; fillColor: root.mintColor; hoverColor: "#ddceff"; pressedColor: "#b39de6"; borderColor: root.mintColor; glyphColor: "#1b1525"; onClicked: root.togglePlayPauseRequested() }
                    IconButton { kind: "chevron"; tooltipText: i18n.text("next", i18n.language); enabled: root.queue.length > 0 && root.currentIndex < root.queue.length - 1; onClicked: root.nextRequested() }
                    Item { Layout.fillWidth: true }
                }
                RowLayout { Layout.fillWidth: true; spacing: 10
                    TextButton { text: i18n.text("edit_info", i18n.language); subtle: true; implicitWidth: 90; enabled: root.hasSong; onClicked: root.editMetadataRequested() }
                    Label { text: i18n.text("volume", i18n.language); color: root.textMutedColor; font.pixelSize: 11 }
                    PlayerSlider { Layout.fillWidth: true; from: 0; to: 1; value: root.volume; activeColor: root.amberColor; baseColor: "#3a3547"; onMoved: root.volumeRequested(value) }
                }
                Rectangle { Layout.fillWidth: true; height: root.errorText.length > 0 ? 32 : 0; visible: root.errorText.length > 0; color: "#32202a"; radius: 9; Label { anchors.fill: parent; anchors.margins: 9; text: root.errorText; color: "#f0afbe"; elide: Text.ElideRight; font.pixelSize: 11 } }
            }
        }
    }
    function formatTime(ms) { const seconds = Math.max(0, Math.floor(Number(ms || 0) / 1000)); return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0") }
}
