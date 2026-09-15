import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "components"

ApplicationWindow {
    id: root

    width: 1280
    height: 800
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    title: "NekoTune"
    color: bg

    readonly property var song: ipcClient.status.song || ({})
    readonly property var queue: ipcClient.status.queue || []
    readonly property real duration: Number(ipcClient.status.duration || 0)
    readonly property real position: Number(ipcClient.status.position || 0)
    readonly property real volume: Number(ipcClient.status.volume || 0.8)
    readonly property string playbackState: String(ipcClient.status.state || "stopped")
    readonly property string databasePath: String(ipcClient.status.database_path || "")

    readonly property color bg: "#0b0e12"
    readonly property color panel: "#151a20"
    readonly property color line: "#2a333c"
    readonly property color textStrong: "#f7f4ed"
    readonly property color textSoft: "#c8c9c3"
    readonly property color textMuted: "#89929a"
    readonly property color mint: "#7be0c7"
    readonly property color amber: "#f4c96d"
    readonly property color coral: "#f39a91"

    FileDialog {
        id: fileDialog
        title: i18n.text("open_music", i18n.language)
        fileMode: FileDialog.OpenFiles
        nameFilters: [i18n.text("audio_files", i18n.language) + " (*.mp3 *.m4a *.aac *.wav *.flac *.ogg)",
                      i18n.text("all_files", i18n.language) + " (*)"]
        onAccepted: {
            for (let index = 0; index < selectedFiles.length; index += 1) {
                if (index === 0) {
                    ipcClient.playPath(selectedFiles[index])
                } else {
                    ipcClient.addPath(selectedFiles[index])
                }
            }
        }
    }

    Popup {
        id: metadataEditor

        property int songId: 0

        function openForSong(song) {
            songId = Number(song.song_id || 0)
            titleField.text = song.custom_title || song.title || ""
            artistField.text = song.artist || ""
            lyricsField.text = song.lyrics || ""
            open()
        }

        modal: true
        focus: true
        width: Math.min(root.width - 80, 620)
        height: Math.min(root.height - 80, 560)
        anchors.centerIn: parent
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: 18
            color: "#171d23"
            border.color: root.line
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 28
            spacing: 16

            RowLayout {
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    text: i18n.text("edit_track_info", i18n.language)
                    color: root.textStrong
                    font.pixelSize: 24
                    font.weight: Font.Bold
                }

                IconButton {
                    text: "×"
                    tooltipText: i18n.text("close", i18n.language)
                    implicitWidth: 38
                    implicitHeight: 38
                    onClicked: metadataEditor.close()
                }
            }

            Label {
                Layout.fillWidth: true
                text: root.databasePath
                      ? (i18n.text("database", i18n.language) + ": " + root.databasePath)
                      : i18n.text("database_unavailable", i18n.language)
                color: root.textMuted
                elide: Text.ElideMiddle
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            TextField {
                id: titleField
                Layout.fillWidth: true
                placeholderText: i18n.text("custom_title", i18n.language)
                color: root.textStrong
                placeholderTextColor: root.textMuted
                selectionColor: root.mint
                selectedTextColor: "#10201b"
                leftPadding: 14
                rightPadding: 14
                background: Rectangle {
                    radius: 11
                    color: "#10161b"
                    border.color: titleField.activeFocus ? root.mint : root.line
                    border.width: titleField.activeFocus ? 2 : 1
                }
            }

            TextField {
                id: artistField
                Layout.fillWidth: true
                placeholderText: i18n.text("artist_author", i18n.language)
                color: root.textStrong
                placeholderTextColor: root.textMuted
                selectionColor: root.mint
                selectedTextColor: "#10201b"
                leftPadding: 14
                rightPadding: 14
                background: Rectangle {
                    radius: 11
                    color: "#10161b"
                    border.color: artistField.activeFocus ? root.mint : root.line
                    border.width: artistField.activeFocus ? 2 : 1
                }
            }

            TextArea {
                id: lyricsField
                Layout.fillWidth: true
                Layout.fillHeight: true
                placeholderText: i18n.text("lyrics", i18n.language)
                wrapMode: TextEdit.Wrap
                color: root.textStrong
                placeholderTextColor: root.textMuted
                selectionColor: root.mint
                selectedTextColor: "#10201b"
                padding: 14
                background: Rectangle {
                    radius: 11
                    color: "#10161b"
                    border.color: lyricsField.activeFocus ? root.mint : root.line
                    border.width: lyricsField.activeFocus ? 2 : 1
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                TextButton {
                    text: i18n.text("cancel", i18n.language)
                    implicitWidth: 90
                    subtle: true
                    onClicked: metadataEditor.close()
                }

                TextButton {
                    text: i18n.text("save", i18n.language)
                    implicitWidth: 90
                    enabled: metadataEditor.songId > 0
                    onClicked: {
                        ipcClient.updateSongMetadata(metadataEditor.songId,
                                                     titleField.text,
                                                     artistField.text,
                                                     lyricsField.text)
                        metadataEditor.close()
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.bg

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#0c1015" }
                GradientStop { position: 0.56; color: "#111820" }
                GradientStop { position: 1.0; color: "#17151a" }
            }
        }

        Rectangle {
            width: 420
            height: 420
            x: parent.width - 250
            y: -170
            radius: width / 2
            color: Qt.rgba(0.48, 0.88, 0.78, 0.055)
        }

        Rectangle {
            width: 300
            height: 300
            x: -150
            y: parent.height - 90
            radius: width / 2
            color: Qt.rgba(0.96, 0.68, 0.32, 0.035)
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 26
            spacing: 22

            QueuePanel {
                Layout.fillHeight: true
                Layout.preferredWidth: 370
                Layout.minimumWidth: 330

                queue: root.queue
                playbackState: root.playbackState
                panelColor: root.panel
                lineColor: root.line
                textStrongColor: root.textStrong
                textSoftColor: root.textSoft
                textMutedColor: root.textMuted
                mintColor: root.mint
                amberColor: root.amber
                coralColor: root.coral

                onAddRequested: fileDialog.open()
                onClearRequested: ipcClient.clearQueue()
                onPlayRequested: queueId => ipcClient.playQueueItem(queueId)
                onTogglePlayPauseRequested: ipcClient.togglePlayPause()
                onRemoveRequested: queueId => ipcClient.removeQueueItem(queueId)
            }

            PlayerPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true

                song: root.song
                queue: root.queue
                duration: root.duration
                position: root.position
                volume: root.volume
                playbackState: root.playbackState
                connected: ipcClient.connected
                errorText: ipcClient.error

                panelColor: root.panel
                lineColor: root.line
                textStrongColor: root.textStrong
                textSoftColor: root.textSoft
                textMutedColor: root.textMuted
                mintColor: root.mint
                amberColor: root.amber

                onAddRequested: fileDialog.open()
                onTogglePlayPauseRequested: ipcClient.togglePlayPause()
                onNextRequested: ipcClient.next()
                onPreviousRequested: ipcClient.previous()
                onSeekRequested: positionMs => ipcClient.seek(positionMs)
                onVolumeRequested: value => ipcClient.setVolume(value)
                onEditMetadataRequested: metadataEditor.openForSong(root.song)
            }
        }
    }
}
