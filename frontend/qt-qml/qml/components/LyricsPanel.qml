import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var song: ({})
    property var lyrics: ({})
    property real position: 0
    property bool playing: false
    property bool connected: false

    signal seekRequested(real positionMs)

    readonly property var current: lyrics.track_id === song.song_hash ? lyrics : ({})
    readonly property var document: current.document || ({})
    readonly property var candidates: current.candidates || []
    readonly property bool busy: current.state === "loading" || current.state === "searching" || current.state === "waiting_metadata"
    spacing: 6

    onSongChanged: {
        if (searchPopup.trackId !== String(song.song_hash || "")) searchPopup.close()
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Label {
            Layout.fillWidth: true
            text: i18n.text("lyrics", i18n.language) + (document.source ? " · " + document.source : "")
            color: "#837c91"
            font.pixelSize: 12
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        TextButton {
            text: i18n.text("lyrics_search", i18n.language) + (root.candidates.length ? " (" + root.candidates.length + ")" : "")
            implicitHeight: 28
            subtle: true
            enabled: root.connected
            onClicked: searchPopup.openForSong()
        }

        TextButton {
            text: i18n.text("lyrics_refresh", i18n.language)
            implicitHeight: 28
            implicitWidth: 64
            subtle: true
            enabled: root.connected && !root.busy && !root.current.offline
            onClicked: ipcClient.refreshLyrics(String(root.song.song_hash || ""))
        }
    }

    // Status warning
    Label {
        Layout.fillWidth: true
        visible: root.current.state !== "ready" || Boolean(root.current.cache_warning)
        text: root.current.cache_warning ? i18n.text("lyrics_cache_warning", i18n.language)
             : root.current.state === "error" ? i18n.text("lyrics_error_" + root.current.error, i18n.language)
             : i18n.text("lyrics_" + (root.current.state || "loading"), i18n.language)
        color: root.current.state === "error" ? "#e8a9c3" : "#8e879c"
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        font.pixelSize: 12
    }

    LrcLyrics {
        Layout.fillWidth: true
        Layout.fillHeight: true
        lines: root.document.lines || []
        plainText: String(root.document.plain_text || "")
        position: root.position
        playing: root.playing
        onSeekRequested: pos => root.seekRequested(pos)
    }

    // Search Lyrics Modal Popup
    Popup {
        id: searchPopup
        property string trackId: ""
        function openForSong() {
            trackId = String(root.song.song_hash || "")
            titleField.text = root.song.title || ""
            artistField.text = root.song.artist || ""
            albumField.text = root.song.album || ""
            sourceBox.currentIndex = 0
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 680)
        height: Math.min(parent.height - 48, 540)
        modal: true
        focus: true
        padding: 22
        background: Rectangle {
            color: "#211C2D"
            radius: 16
            border.color: "#332C41"
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: i18n.text("lyrics_search", i18n.language)
                    color: "#F5F1FA"
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                IconButton {
                    kind: "close"
                    tooltipText: i18n.text("close", i18n.language)
                    onClicked: searchPopup.close()
                }
            }

            TextField {
                id: titleField
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                placeholderText: i18n.text("custom_title", i18n.language)
                color: "#F5F1FA"
                placeholderTextColor: "#AAA0B8"
                leftPadding: 12
                rightPadding: 12
                maximumLength: 500
                background: Rectangle {
                    radius: 8
                    color: "#17141F"
                    border.color: titleField.activeFocus ? "#CBB8FF" : "#8D809F"
                    border.width: 1
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: artistField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    placeholderText: i18n.text("artist_author", i18n.language)
                    color: "#F5F1FA"
                    placeholderTextColor: "#AAA0B8"
                    leftPadding: 12
                    rightPadding: 12
                    maximumLength: 500
                    background: Rectangle {
                        radius: 8
                        color: "#17141F"
                        border.color: artistField.activeFocus ? "#CBB8FF" : "#8D809F"
                        border.width: 1
                    }
                }
                TextField {
                    id: albumField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    placeholderText: i18n.text("album", i18n.language)
                    color: "#F5F1FA"
                    placeholderTextColor: "#AAA0B8"
                    leftPadding: 12
                    rightPadding: 12
                    maximumLength: 500
                    background: Rectangle {
                        radius: 8
                        color: "#17141F"
                        border.color: albumField.activeFocus ? "#CBB8FF" : "#8D809F"
                        border.width: 1
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: sourceBox
                    objectName: "lyricsSearchSource"
                    model: ["LRCLIB", i18n.text("lyrics_kugou", i18n.language)]
                    Layout.preferredWidth: 120
                    palette.button: "#241e31"
                    palette.buttonText: "#f6f3fa"
                    palette.text: "#f6f3fa"
                    palette.base: "#241e31"
                    palette.highlight: "#4b3e67"
                }
                CheckBox {
                    text: i18n.text("lyrics_offline_mode", i18n.language)
                    checked: Boolean(root.current.offline)
                    enabled: root.connected
                    onClicked: ipcClient.setLyricsOffline(checked)
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 12
                        color: "#b0a8bd"
                        leftPadding: parent.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Item { Layout.fillWidth: true }
                TextButton {
                    text: i18n.text("lyrics_search", i18n.language)
                    implicitWidth: 90
                    enabled: root.connected && titleField.text.trim().length > 0 && !root.current.offline
                    onClicked: ipcClient.searchLyrics(searchPopup.trackId, titleField.text, artistField.text,
                                                      albumField.text, sourceBox.currentIndex === 1 ? "kugou" : "lrclib")
                }
            }

            Label {
                Layout.fillWidth: true
                text: root.busy ? i18n.text("lyrics_searching", i18n.language)
                      : root.current.state === "error" ? i18n.text("lyrics_error_" + root.current.error, i18n.language)
                      : root.current.state === "candidates" && root.current.search_stage === "songs"
                        ? i18n.text("lyrics_choose_song", i18n.language)
                        : i18n.text("lyrics_" + (root.current.state || "idle"), i18n.language)
                color: "#8e879c"
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#282335"
            }

            // Candidates List
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: root.candidates

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    width: 4
                    contentItem: Rectangle { radius: 2; color: "#363044" }
                }

                delegate: Rectangle {
                    id: candDelegate
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: 58
                    radius: 8
                    color: candHover.hovered ? "#241f33" : "#1a1626"
                    border.color: candHover.hovered ? "#3d3452" : "#262135"
                    border.width: 1

                    HoverHandler { id: candHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            ipcClient.selectLyrics(searchPopup.trackId, String(root.current.revision), index)
                            if (!candDelegate.modelData.song_result) searchPopup.close()
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        anchors.leftMargin: candDelegate.modelData.cover_url ? 66 : 10
                        spacing: 3

                        // Line 1: Title and Artist
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                text: candDelegate.modelData.title || i18n.text("untitled", i18n.language)
                                color: "#f6f3fa"
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Label {
                                text: candDelegate.modelData.artist ? "· " + candDelegate.modelData.artist : ""
                                color: "#cfc8db"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        // Line 2: Album, Duration, and Type Badge
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                text: (candDelegate.modelData.album ? candDelegate.modelData.album + " · " : "")
                                      + (candDelegate.modelData.duration ? Math.round(Number(candDelegate.modelData.duration) / 1000) + "s" : "")
                                color: "#8e879c"
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                Layout.preferredHeight: 18
                                Layout.preferredWidth: typeLabel.implicitWidth + 10
                                radius: 4
                                color: candDelegate.modelData.synced ? "#231c36" : "#1c1828"
                                border.color: candDelegate.modelData.synced ? "#3f335e" : "#2c263d"

                                Label {
                                    id: typeLabel
                                    anchors.centerIn: parent
                                    text: i18n.text(candDelegate.modelData.song_result ? "lyrics_song_version" : candDelegate.modelData.instrumental ? "lyrics_instrumental" : candDelegate.modelData.synced ? "lyrics_synced" : "lyrics_plain", i18n.language)
                                    color: candDelegate.modelData.synced ? "#cbb8ff" : "#8e879c"
                                    font.pixelSize: 10
                                    font.weight: Font.Medium
                                }
                            }
                        }
                    }

                    Rectangle {
                        visible: !root.current.offline && Boolean(candDelegate.modelData.cover_url)
                        anchors.left: parent.left
                        anchors.leftMargin: 9
                        anchors.verticalCenter: parent.verticalCenter
                        width: 42
                        height: 42
                        radius: 5
                        clip: true
                        color: "#211b2c"
                        Image {
                            anchors.fill: parent
                            source: "qrc:/artwork/default-cover.png"
                            fillMode: Image.PreserveAspectCrop
                        }
                        Image {
                            anchors.fill: parent
                            source: !root.current.offline ? (candDelegate.modelData.cover_url || "") : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            visible: status === Image.Ready
                        }
                    }
                }
            }
        }
    }
}
