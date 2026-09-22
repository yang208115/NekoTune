import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
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

    property color panelColor: "#13111b"
    property color lineColor: "#211c2b"
    property color textStrongColor: "#f6f3fa"
    property color textSoftColor: "#cfc8db"
    property color textMutedColor: "#847d91"
    property color lavenderColor: "#cbb8ff"
    property color roseColor: "#e8a9c3"

    readonly property bool hasSong: Boolean(root.song && root.song.song_id)

    signal addRequested()
    signal togglePlayPauseRequested()
    signal nextRequested()
    signal previousRequested()
    signal seekRequested(real positionMs)
    signal volumeRequested(real value)
    signal editMetadataRequested()

    RowLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 36

        // Left Column: Artwork Card & Track Typography (Centered & Fixed Max Width)
        Item {
            Layout.preferredWidth: Math.max(260, Math.min(320, parent.width * 0.35))
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width
                spacing: 18

                // Artwork container with fixed max height
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 240

                    AlbumArt {
                        anchors.centerIn: parent
                        width: 240
                        height: 240
                        maxDimension: 240
                        hasSong: root.hasSong
                        playbackState: root.playbackState
                        lavenderColor: root.lavenderColor
                        roseColor: root.roseColor
                    }
                }

                // Track Details
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: root.hasSong ? (root.song.title || i18n.text("untitled", i18n.language)) : i18n.text("no_track_selected", i18n.language)
                            color: root.textStrongColor
                            font.pixelSize: 20
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                        IconButton {
                            kind: "edit"
                            tooltipText: i18n.text("edit_info", i18n.language)
                            glyphColor: root.textMutedColor
                            enabled: root.hasSong
                            implicitWidth: 28
                            implicitHeight: 28
                            onClicked: root.editMetadataRequested()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.hasSong ? (root.song.artist || i18n.text("artist_author", i18n.language)) : i18n.text("choose_audio", i18n.language)
                        color: root.textSoftColor
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: root.hasSong && Boolean(root.song.album)
                        text: root.song.album || ""
                        color: root.textMutedColor
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
            }
        }

        // Elegant Vertical Divider
        Rectangle {
            Layout.fillHeight: true
            Layout.topMargin: 20
            Layout.bottomMargin: 20
            width: 1
            color: root.lineColor
        }

        // Right Column: Full-Height Immersive Lyrics
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            LyricsPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                song: root.song
                lyrics: root.lyrics
                position: root.position
                connected: root.connected
                onSeekRequested: pos => root.seekRequested(pos)
            }

            Rectangle {
                Layout.fillWidth: true
                height: root.errorText.length > 0 ? 30 : 0
                visible: root.errorText.length > 0
                color: "#2e1a22"
                radius: 6
                border.color: "#4e2735"
                Label {
                    anchors.fill: parent
                    anchors.margins: 6
                    text: root.errorText
                    color: "#f0afbe"
                    elide: Text.ElideRight
                    font.pixelSize: 11
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
