import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "components"

ApplicationWindow {
    id: root
    width: 1440
    height: 900
    minimumWidth: 1080
    minimumHeight: 700
    visible: true
    title: "NekoTune"
    color: "#0e0d12"

    readonly property var song: ipcClient.status.song || ({})
    readonly property var queue: ipcClient.status.queue || []
    readonly property var lyrics: ipcClient.status.lyrics || ({})
    readonly property real duration: Number(ipcClient.status.duration || 0)
    readonly property real position: Number(ipcClient.status.position || 0)
    readonly property real volume: Number(ipcClient.status.volume || 0.8)
    readonly property string playbackState: String(ipcClient.status.state || "stopped")
    readonly property string databasePath: String(ipcClient.status.database_path || "")
    readonly property color ink: "#f7f4fb"
    readonly property color muted: "#9b96a8"
    readonly property color subtle: "#676275"
    readonly property color border: "#292532"
    readonly property color surface: "#17151e"
    readonly property color surfaceRaised: "#1e1b28"
    readonly property color lavender: "#cbb8ff"
    readonly property color rose: "#e8a9c3"

    FileDialog {
        id: fileDialog
        title: i18n.text("open_music", i18n.language)
        fileMode: FileDialog.OpenFiles
        nameFilters: [i18n.text("audio_files", i18n.language) + " (*.mp3 *.m4a *.aac *.wav *.flac *.ogg)", i18n.text("all_files", i18n.language) + " (*)"]
        onAccepted: {
            for (let index = 0; index < selectedFiles.length; index += 1) {
                if (index === 0) ipcClient.playPath(selectedFiles[index])
                else ipcClient.addPath(selectedFiles[index])
            }
        }
    }

    Popup {
        id: metadataEditor
        property int songId: 0
        function openForSong(value) {
            songId = Number(value.song_id || 0)
            titleField.text = value.custom_title || value.title || ""
            artistField.text = value.artist || ""
            lyricsField.text = value.lyrics || ""
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(620, parent.width - 48)
        height: Math.min(570, parent.height - 48)
        modal: true
        padding: 0
        background: Rectangle { color: root.surfaceRaised; radius: 24; border.color: root.border }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 28; spacing: 16
            RowLayout { Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: i18n.text("edit_track_info", i18n.language); color: root.ink; font.pixelSize: 24; font.weight: Font.DemiBold }
                IconButton { kind: "close"; tooltipText: i18n.text("close", i18n.language); onClicked: metadataEditor.close() }
            }
            Label { Layout.fillWidth: true; text: root.databasePath ? i18n.text("database", i18n.language) + ": " + root.databasePath : i18n.text("database_unavailable", i18n.language); color: root.subtle; elide: Text.ElideMiddle; font.pixelSize: 11 }
            TextField { id: titleField; Layout.fillWidth: true; placeholderText: i18n.text("custom_title", i18n.language); color: root.ink; placeholderTextColor: root.subtle; leftPadding: 14; rightPadding: 14; background: Rectangle { color: "#121019"; radius: 12; border.color: titleField.activeFocus ? root.lavender : root.border } }
            TextField { id: artistField; Layout.fillWidth: true; placeholderText: i18n.text("artist_author", i18n.language); color: root.ink; placeholderTextColor: root.subtle; leftPadding: 14; rightPadding: 14; background: Rectangle { color: "#121019"; radius: 12; border.color: artistField.activeFocus ? root.lavender : root.border } }
            TextArea { id: lyricsField; Layout.fillWidth: true; Layout.fillHeight: true; placeholderText: i18n.text("lyrics", i18n.language); color: root.ink; placeholderTextColor: root.subtle; wrapMode: TextEdit.Wrap; padding: 14; background: Rectangle { color: "#121019"; radius: 12; border.color: lyricsField.activeFocus ? root.lavender : root.border } }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true }
                TextButton { text: i18n.text("cancel", i18n.language); subtle: true; onClicked: metadataEditor.close() }
                TextButton { text: i18n.text("save", i18n.language); enabled: metadataEditor.songId > 0; onClicked: { ipcClient.updateSongMetadata(metadataEditor.songId, titleField.text, artistField.text, lyricsField.text); metadataEditor.close() } }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.color
        Rectangle { width: parent.width * 0.56; height: parent.height * 0.8; x: parent.width * 0.42; y: -parent.height * 0.35; radius: width / 2; color: Qt.rgba(0.53, 0.42, 0.82, 0.08) }
        Rectangle { width: parent.width * 0.35; height: parent.height * 0.5; x: -parent.width * 0.14; y: parent.height * 0.58; radius: width / 2; color: Qt.rgba(0.82, 0.42, 0.55, 0.045) }

        ColumnLayout { anchors.fill: parent; anchors.margins: 22; spacing: 18
            RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 48; spacing: 14
                Rectangle { width: 38; height: 38; radius: 13; color: root.lavender; Label { anchors.centerIn: parent; text: "N"; color: "#1a1422"; font.pixelSize: 20; font.weight: Font.Black } }
                ColumnLayout { spacing: 0; Layout.fillWidth: true; Label { text: "NEKOTUNE"; color: root.ink; font.pixelSize: 18; font.weight: Font.Black; font.letterSpacing: 2.4 } Label { text: "YOUR LOCAL LISTENING ROOM"; color: root.subtle; font.pixelSize: 9; font.letterSpacing: 1.2 } }
                Rectangle { width: connectionLabel.implicitWidth + 22; height: 30; radius: 15; color: ipcClient.connected ? "#1d2b29" : "#2c2026"; border.color: ipcClient.connected ? "#3c6259" : "#633b4e"; Label { id: connectionLabel; anchors.centerIn: parent; text: ipcClient.connected ? i18n.text("connected", i18n.language) : i18n.text("offline", i18n.language); color: ipcClient.connected ? "#a5e8d6" : root.rose; font.pixelSize: 11 } }
                TextButton { text: i18n.language === "zh" ? "EN" : "中"; implicitWidth: 50; implicitHeight: 34; subtle: true; onClicked: i18n.language = i18n.language === "zh" ? "en" : "zh" }
            }

            RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 18
                QueuePanel {
                    Layout.preferredWidth: 350; Layout.minimumWidth: 300; Layout.fillHeight: true
                    queue: root.queue; playbackState: root.playbackState; folders: ipcClient.status.folders || []
                    panelColor: root.surface; lineColor: root.border; textStrongColor: root.ink; textSoftColor: "#c6c0d1"; textMutedColor: root.muted; mintColor: root.lavender; amberColor: root.rose; coralColor: "#eb879f"
                    onAddRequested: fileDialog.open(); onClearRequested: ipcClient.clearQueue(); onPlayRequested: id => ipcClient.playQueueItem(id); onTogglePlayPauseRequested: ipcClient.togglePlayPause(); onRemoveRequested: id => ipcClient.removeQueueItem(id); onOrganizeRequested: (action, params) => ipcClient.organizeQueue(action, params)
                }
                PlayerPanel {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    song: root.song; lyrics: root.lyrics; queue: root.queue; duration: root.duration; position: root.position; volume: root.volume; playbackState: root.playbackState; connected: ipcClient.connected; errorText: ipcClient.error
                    panelColor: root.surface; lineColor: root.border; textStrongColor: root.ink; textSoftColor: "#c6c0d1"; textMutedColor: root.muted; mintColor: root.lavender; amberColor: root.rose
                    onAddRequested: fileDialog.open(); onTogglePlayPauseRequested: ipcClient.togglePlayPause(); onNextRequested: ipcClient.next(); onPreviousRequested: ipcClient.previous(); onSeekRequested: value => ipcClient.seek(value); onVolumeRequested: value => ipcClient.setVolume(value); onEditMetadataRequested: metadataEditor.openForSong(root.song)
                }
            }
        }
    }

    LyricsDebugPanel {
        visible: Boolean(lyricsDebugEnabled)
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 22
        width: Math.min(620, parent.width - 44)
        height: Math.min(470, parent.height - 44)
        lyrics: root.lyrics
        song: root.song
        position: root.position
        duration: root.duration
        playbackState: root.playbackState
    }
}
