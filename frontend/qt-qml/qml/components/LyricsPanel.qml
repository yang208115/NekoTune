import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: root
    property var song: ({})
    property var lyrics: ({})
    property var asr: ({})
    property var asrSettings: ({})
    readonly property bool asrBusy: ["uploading", "submitting", "recognizing", "downloading", "saving"].indexOf(asr.state) >= 0
    signal settingsRequested()
    property real position: 0
    property bool connected: false

    signal seekRequested(real positionMs)

    readonly property var current: lyrics.track_id === song.song_hash ? lyrics : ({})
    readonly property var document: current.document || ({})
    readonly property var candidates: current.candidates || []
    readonly property bool busy: current.state === "loading" || current.state === "searching" || current.state === "waiting_metadata"
    spacing: 6

    onSongChanged: {
        if (searchPopup.trackId !== String(song.song_hash || "")) searchPopup.close()
        if (importDialog.trackId !== String(song.song_hash || "")) importDialog.close()
        if (asrPopup.trackId !== String(song.song_hash || "")) asrPopup.close()
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
            text: i18n.text("lyrics_import_asr", i18n.language)
            implicitHeight: 28
            subtle: true
            enabled: root.connected && Boolean(root.song.song_hash) && !root.busy
            onClicked: {
                importDialog.trackId = String(root.song.song_hash)
                importDialog.revision = String(root.current.revision)
                importDialog.open()
            }
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

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        TextButton {
            objectName: "transcribeLyricsButton"
            text: i18n.text(root.asrBusy ? "asr_cancel" : "asr_transcribe", i18n.language)
            enabled: root.connected && Boolean(root.song.song_hash) && (!root.current.offline || root.asrBusy)
            onClicked: {
                if (root.asrBusy) ipcClient.cancelAsr()
                else if (!root.asrSettings.api_key_configured) root.settingsRequested()
                else asrPopup.openForSong()
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.asr.track_id === root.song.song_hash && root.asr.state
                  ? i18n.text(root.asr.state === "error" ? "asr_error_" + root.asr.error : "asr_" + root.asr.state, i18n.language)
                  : i18n.text("asr_upload_short", i18n.language)
            color: root.asr.state === "error" ? "#e8a9c3" : "#a59caf"
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            font.pixelSize: 12
        }
    }

    // Status warning
    Label {
        Layout.fillWidth: true
        visible: root.current.state !== "ready" || Boolean(root.current.cache_warning) || Boolean(root.current.storage_warning)
        text: root.current.storage_warning ? i18n.text("asr_error_storage", i18n.language)
             : root.current.cache_warning ? i18n.text("lyrics_cache_warning", i18n.language)
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
        onSeekRequested: pos => root.seekRequested(pos)
    }

    Popup {
        id: asrPopup
        objectName: "asrLanguagePopup"
        property string trackId: ""
        property string revision: ""
        readonly property var languages: ["ja", "auto", "zh", "en", "ko"]
        readonly property var models: ["fun-asr", "qwen-audio-3.1-asr-flash-filetrans",
                                      "qwen-audio-3.0-asr-flash-filetrans", "qwen3-asr-flash-filetrans", "paraformer-v2"]
        function openForSong() {
            trackId = String(root.song.song_hash || "")
            revision = String(root.current.revision)
            asrLanguage.currentIndex = 0
            asrModel.currentIndex = 0
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 560)
        padding: 24
        modal: true
        focus: true
        background: Rectangle { color: "#181423"; radius: 16; border.color: "#342d45" }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 16
            Label {
                text: i18n.text("asr_transcribe", i18n.language)
                color: "#f6f3fa"
                font.pixelSize: 20
                font.weight: Font.Bold
            }
            Label {
                Layout.fillWidth: true
                text: root.song.title || ""
                visible: text.length > 0
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: "#a59caf"
            }
            Label { text: i18n.text("asr_model", i18n.language); color: "#d8d2e4" }
            ComboBox {
                id: asrModel
                objectName: "asrRequestModel"
                Layout.fillWidth: true
                model: ["Fun-ASR", "Qwen-Audio 3.1 ASR Flash Filetrans", "Qwen-Audio 3.0 ASR Flash Filetrans",
                        "Qwen3 ASR Flash Filetrans", "Paraformer v2"]
                currentIndex: 0
                palette.button: "#241e31"
                palette.buttonText: "#f6f3fa"
                palette.text: "#f6f3fa"
                palette.base: "#241e31"
                palette.highlight: "#4b3e67"
            }
            Label { text: i18n.text("asr_language", i18n.language); color: "#d8d2e4" }
            ComboBox {
                id: asrLanguage
                objectName: "asrRequestLanguage"
                Layout.fillWidth: true
                model: ["日本語", i18n.text("asr_language_auto", i18n.language), "中文", "English", "한국어"]
                currentIndex: 0
                palette.button: "#241e31"
                palette.buttonText: "#f6f3fa"
                palette.text: "#f6f3fa"
                palette.base: "#241e31"
                palette.highlight: "#4b3e67"
            }
            Label {
                Layout.fillWidth: true
                text: i18n.text("asr_upload_short", i18n.language)
                wrapMode: Text.Wrap
                color: "#a59caf"
                font.pixelSize: 12
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 10
                TextButton {
                    objectName: "cancelAsrUpload"
                    text: i18n.text("cancel", i18n.language)
                    subtle: true
                    onClicked: asrPopup.close()
                }
                TextButton {
                    objectName: "startAsrUpload"
                    text: i18n.text("asr_start_upload", i18n.language)
                    enabled: root.connected && !root.asrBusy && !root.current.offline
                             && asrPopup.trackId === String(root.song.song_hash || "")
                             && asrPopup.revision === String(root.current.revision)
                    onClicked: {
                        ipcClient.transcribeLyrics(asrPopup.trackId, asrPopup.revision,
                                                   asrPopup.languages[asrLanguage.currentIndex],
                                                   asrPopup.models[asrModel.currentIndex])
                        asrPopup.close()
                    }
                }
            }
        }
    }

    FileDialog {
        id: importDialog
        property string trackId: ""
        property string revision: ""
        title: i18n.text("lyrics_import_asr", i18n.language)
        nameFilters: ["ASR JSON (*.json)"]
        fileMode: FileDialog.OpenFile
        onAccepted: ipcClient.importAsrLyrics(trackId, revision, selectedFile.toString())
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
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 620)
        height: Math.min(parent.height - 48, 540)
        modal: true
        focus: true
        padding: 22
        background: Rectangle {
            color: "#181423"
            radius: 16
            border.color: "#342d45"
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
                    color: "#f6f3fa"
                    font.pixelSize: 18
                    font.weight: Font.Bold
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
                Layout.preferredHeight: 38
                placeholderText: i18n.text("custom_title", i18n.language)
                color: "#f6f3fa"
                placeholderTextColor: "#645e70"
                leftPadding: 12
                rightPadding: 12
                maximumLength: 500
                background: Rectangle {
                    radius: 8
                    color: "#110e18"
                    border.color: titleField.activeFocus ? "#cbb8ff" : "#292436"
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: artistField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    placeholderText: i18n.text("artist_author", i18n.language)
                    color: "#f6f3fa"
                    placeholderTextColor: "#645e70"
                    leftPadding: 12
                    rightPadding: 12
                    maximumLength: 500
                    background: Rectangle {
                        radius: 8
                        color: "#110e18"
                        border.color: artistField.activeFocus ? "#cbb8ff" : "#292436"
                    }
                }
                TextField {
                    id: albumField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    placeholderText: i18n.text("album", i18n.language)
                    color: "#f6f3fa"
                    placeholderTextColor: "#645e70"
                    leftPadding: 12
                    rightPadding: 12
                    maximumLength: 500
                    background: Rectangle {
                        radius: 8
                        color: "#110e18"
                        border.color: albumField.activeFocus ? "#cbb8ff" : "#292436"
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
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
                    onClicked: ipcClient.searchLyrics(searchPopup.trackId, titleField.text, artistField.text, albumField.text)
                }
            }

            Label {
                Layout.fillWidth: true
                text: root.busy ? i18n.text("lyrics_searching", i18n.language)
                      : root.current.state === "error" ? i18n.text("lyrics_error_" + root.current.error, i18n.language)
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
                            searchPopup.close()
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
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
                                    text: i18n.text(candDelegate.modelData.instrumental ? "lyrics_instrumental" : candDelegate.modelData.synced ? "lyrics_synced" : "lyrics_plain", i18n.language)
                                    color: candDelegate.modelData.synced ? "#cbb8ff" : "#8e879c"
                                    font.pixelSize: 10
                                    font.weight: Font.Medium
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
