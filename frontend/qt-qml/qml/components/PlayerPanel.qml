import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property int topInset: 0
    property var lyricsController: null
    property var song: ({})
    property var lyrics: ({})
    property var queue: []
    property real duration: 0
    property real position: 0
    property real volume: 0.8
    property string playbackState: "stopped"
    property bool connected: false
    property string errorText: ""

    property color panelColor: Theme.bgSurface
    property color lineColor: Theme.borderSubtle
    property color textStrongColor: Theme.textPrimary
    property color textSoftColor: Theme.textSecondary
    property color textMutedColor: Theme.textMuted
    property color lavenderColor: Theme.accentPrimary
    property color roseColor: Theme.accentSecondary

    readonly property bool hasSong: Boolean(root.song && root.song.song_id)
    readonly property string coverUrl: String(root.song.cover_url || "")
    readonly property real targetArtSize: root.width < 1200 ? Theme.artworkCompact : Theme.artworkWide

    signal addRequested()
    signal togglePlayPauseRequested()
    signal nextRequested()
    signal previousRequested()
    signal seekRequested(real positionMs)
    signal volumeRequested(real value)
    signal editMetadataRequested()

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        anchors.topMargin: 24 + root.topInset
        spacing: 28

        // Left Column: Artwork Card & Track Typography (Centered & Fixed Max Width)
        Item {
            Layout.preferredWidth: Math.max(260, Math.min(320, parent.width * 0.35))
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width
                spacing: 16

                // Artwork container with adaptive size (224 compact, 280 wide)
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.targetArtSize

                    AlbumArt {
                        anchors.centerIn: parent
                        width: root.targetArtSize
                        height: root.targetArtSize
                        maxDimension: root.targetArtSize
                        hasSong: root.hasSong
                        coverUrl: root.coverUrl
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
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        IconButton {
                            kind: "edit"
                            tooltipText: i18n.text("edit_info", i18n.language)
                            glyphColor: root.textMutedColor
                            hoverGlyphColor: root.lavenderColor
                            enabled: root.hasSong
                            implicitWidth: 32
                            implicitHeight: 32
                            onClicked: root.editMetadataRequested()
                        }
                    }

                    ArtistNames {
                        objectName: "playerArtists"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        artist: root.hasSong ? String(root.song.artist || "") : ""
                        fallbackText: i18n.text(root.hasSong ? "artist_author" : "choose_audio", i18n.language)
                        color: root.textSoftColor
                        font.pixelSize: 13
                        font.weight: Font.Normal
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
            Layout.preferredWidth: 1
            color: root.lineColor
        }

        // Right Column: Full-Height Immersive Lyrics
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            LyricsPanel {
                controller: root.lyricsController
                Layout.fillWidth: true
                Layout.fillHeight: true
                song: root.song
                lyrics: root.lyrics
                position: root.position
                playing: root.playbackState === "playing"
                connected: root.connected
                onSeekRequested: pos => root.seekRequested(pos)
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.errorText.length > 0 ? 32 : 0
                visible: root.errorText.length > 0
                color: Theme.statusErrorBg
                radius: Theme.radiusSm
                border.color: Theme.statusError
                border.width: 1
                Label {
                    anchors.fill: parent
                    anchors.margins: 6
                    text: root.errorText
                    color: Theme.statusError
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontCaption
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
