import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var song: ({})
    property var lyrics: ({})
    property real position: 0
    property bool connected: false
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
        Label {
            Layout.fillWidth: true
            text: i18n.text("lyrics", i18n.language) + (document.source ? " · " + document.source : "")
            color: "#89929a"
            elide: Text.ElideRight
        }
        TextButton {
            text: i18n.text("lyrics_search", i18n.language) + (root.candidates.length ? " (" + root.candidates.length + ")" : "")
            implicitHeight: 30
            implicitWidth: 100
            subtle: true
            enabled: root.connected
            onClicked: searchPopup.openForSong()
        }
        TextButton {
            text: i18n.text("lyrics_refresh", i18n.language)
            implicitHeight: 30
            implicitWidth: 66
            subtle: true
            enabled: root.connected && !root.busy && !root.current.offline
            onClicked: ipcClient.refreshLyrics(String(root.song.song_hash || ""))
        }
    }
    Label {
        Layout.fillWidth: true
        visible: root.current.state !== "ready" || Boolean(root.current.cache_warning)
        text: root.current.cache_warning ? i18n.text("lyrics_cache_warning", i18n.language)
             : root.current.state === "error" ? i18n.text("lyrics_error_" + root.current.error, i18n.language)
             : i18n.text("lyrics_" + (root.current.state || "loading"), i18n.language)
        color: root.current.state === "error" ? "#f39a91" : "#89929a"
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
    }

    Popup {
        id: searchPopup
        property string trackId: ""
        function openForSong() {
            trackId = String(root.song.song_hash || "")
            titleField.text = root.song.title || ""
            artistField.text = root.song.artist || ""
            albumField.text = root.song.album || ""
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 60, 640)
        height: Math.min(parent.height - 60, 560)
        modal: true
        focus: true
        padding: 22
        background: Rectangle { color: "#171d23"; radius: 18; border.color: "#2a333c" }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label { text: i18n.text("lyrics_search", i18n.language); color: "#f7f4ed"; font.pixelSize: 22 }
            TextField { id: titleField; Layout.fillWidth: true; placeholderText: i18n.text("custom_title", i18n.language); maximumLength: 500 }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: artistField; Layout.fillWidth: true; placeholderText: i18n.text("artist_author", i18n.language); maximumLength: 500 }
                TextField { id: albumField; Layout.fillWidth: true; placeholderText: i18n.text("album", i18n.language); maximumLength: 500 }
            }
            RowLayout {
                Layout.fillWidth: true
                CheckBox {
                    text: i18n.text("lyrics_offline_mode", i18n.language)
                    checked: Boolean(root.current.offline)
                    enabled: root.connected
                    onClicked: ipcClient.setLyricsOffline(checked)
                }
                Item { Layout.fillWidth: true }
                TextButton {
                    text: i18n.text("lyrics_search", i18n.language)
                    enabled: root.connected && titleField.text.trim().length > 0 && !root.current.offline
                    onClicked: ipcClient.searchLyrics(searchPopup.trackId, titleField.text, artistField.text, albumField.text)
                }
            }
            Label {
                Layout.fillWidth: true
                text: root.busy ? i18n.text("lyrics_searching", i18n.language)
                      : root.current.state === "error" ? i18n.text("lyrics_error_" + root.current.error, i18n.language)
                      : i18n.text("lyrics_" + (root.current.state || "idle"), i18n.language)
                color: "#89929a"
                wrapMode: Text.Wrap
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: root.candidates
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: 72
                    contentItem: Column {
                        spacing: 4
                        Label {
                            width: parent.width
                            text: modelData.title + " · " + modelData.artist
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: "#f7f4ed"
                        }
                        Label {
                            width: parent.width
                            text: modelData.album + " · " + Math.round(modelData.duration / 1000) + "s · "
                                  + i18n.text(modelData.instrumental ? "lyrics_instrumental" : modelData.synced ? "lyrics_synced" : "lyrics_plain", i18n.language)
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: "#89929a"
                        }
                    }
                    onClicked: {
                        ipcClient.selectLyrics(searchPopup.trackId, String(root.current.revision), index)
                        searchPopup.close()
                    }
                }
            }
            TextButton { Layout.alignment: Qt.AlignRight; text: i18n.text("close", i18n.language); subtle: true; onClicked: searchPopup.close() }
        }
    }
}
