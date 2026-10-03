import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Only render a structured lyric snapshot belonging to the current audio hash.
// Manual search can retain the existing document while showing new candidates.
// The popup binds its target identity when it opens and closes on track change.
// Song-version and lyric-candidate stages share one selection interface.
// Candidate requests carry the published revision to reject stale choices.
// Cache warnings do not suppress an otherwise usable document.
Item {
    id: root
    property var controller: null
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

    onSongChanged: {
        if (searchPopup.trackId !== String(song.song_hash || "")) searchPopup.close()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Label {
            Layout.fillWidth: true
            text: i18n.text("lyrics", i18n.language) + (document.source ? " · " + document.source : "")
            color: Theme.textMuted
            font.pixelSize: 12
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        TextButton {
            text: i18n.text("lyrics_search", i18n.language) + (root.candidates.length ? " (" + root.candidates.length + ")" : "")
            implicitHeight: 40
            subtle: true
            enabled: root.connected
            onClicked: searchPopup.openForSong()
        }

        IconButton {
            id: lyricsMore
            kind: "more"
            tooltipText: i18n.text("track_actions", i18n.language)
            onClicked: lyricsMenu.openAt(lyricsMore)
        }
        ActionMenu {
            id: lyricsMenu
            actions: [
                {key: "refresh", label: i18n.text("lyrics_refresh", i18n.language), enabled: root.connected && !root.busy && !root.current.offline},
                {key: "offline", label: i18n.text(root.current.offline ? "lyrics_online_mode" : "lyrics_offline_mode", i18n.language), enabled: root.connected}
            ]
            onChosen: action => {
                if (action === "refresh") root.controller.refreshLyrics(String(root.song.song_hash || ""))
                else root.controller.setLyricsOffline(!root.current.offline)
            }
        }
    }

    // Status warning
    Label {
        Layout.fillWidth: true
        visible: root.current.state !== "ready" || Boolean(root.current.cache_warning)
        text: root.current.cache_warning ? i18n.text("lyrics_cache_warning", i18n.language)
             : root.current.state === "error" ? i18n.text("lyrics_error_" + root.current.error, i18n.language)
             : i18n.text("lyrics_" + (root.current.state || "loading"), i18n.language)
        color: root.current.state === "error" ? Theme.statusError : Theme.textMuted
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

    }

    // Search Lyrics Modal Popup
    Popup {
        id: searchPopup
        objectName: "lyricsSearchPopup"
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
            objectName: "shortcutBlocker"
            color: Theme.bgRaised
            radius: Theme.radiusLg
            border.color: Theme.borderSubtle
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
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontDialogTitle
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
                color: Theme.textPrimary
                placeholderTextColor: Theme.textMuted
                leftPadding: 12
                rightPadding: 12
                maximumLength: 500
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: Theme.bgSurface
                    border.color: titleField.activeFocus ? Theme.accentPrimary : Theme.borderControl
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
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textMuted
                    leftPadding: 12
                    rightPadding: 12
                    maximumLength: 500
                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: Theme.bgSurface
                        border.color: artistField.activeFocus ? Theme.accentPrimary : Theme.borderControl
                        border.width: 1
                    }
                }
                TextField {
                    id: albumField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    placeholderText: i18n.text("album", i18n.language)
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textMuted
                    leftPadding: 12
                    rightPadding: 12
                    maximumLength: 500
                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: Theme.bgSurface
                        border.color: albumField.activeFocus ? Theme.accentPrimary : Theme.borderControl
                        border.width: 1
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: sourceBox
                    objectName: "lyricsSearchSource"
                    model: root.controller ? root.controller.sources : []
                    textRole: "name"
                    valueRole: "id"
                    Layout.preferredWidth: 160
                    implicitHeight: 40
                    palette.buttonText: Theme.textPrimary
                    palette.text: Theme.textPrimary
                    palette.base: Theme.bgSurface
                    palette.highlight: Theme.accentPrimary
                    palette.highlightedText: Theme.textOnAccent
                    background: Rectangle { color: Theme.bgSurface; radius: Theme.radiusSm; border.color: sourceBox.activeFocus ? Theme.accentPrimary : Theme.borderControl }
                    delegate: ItemDelegate {
                        id: sourceDelegate
                        required property var modelData
                        required property int index
                        width: sourceBox.width - 12
                        text: String(modelData.name)
                        highlighted: sourceBox.highlightedIndex === index
                        contentItem: Label { text: sourceDelegate.text; textFormat: Text.PlainText; color: Theme.textSecondary; font.pixelSize: Theme.fontBodySecondary }
                        background: Rectangle { radius: 6; color: sourceDelegate.highlighted ? Theme.bgSelected : "transparent" }
                    }
                    popup: Popup {
                        y: sourceBox.height + 4
                        width: sourceBox.width
                        padding: 6
                        implicitHeight: Math.min(contentItem.implicitHeight + 12, 240)
                        background: Rectangle { objectName: "shortcutBlocker"; color: Theme.bgRaised; radius: Theme.radiusSm; border.color: Theme.borderSubtle }
                        contentItem: ListView { clip: true; implicitHeight: contentHeight; model: sourceBox.popup.visible ? sourceBox.delegateModel : null; currentIndex: sourceBox.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
                    }
                }
                CheckBox {
                    id: offlineCheckbox
                    implicitHeight: 40
                    indicator: Rectangle { width: 18; height: 18; anchors.verticalCenter: parent.verticalCenter; radius: 4; color: offlineCheckbox.checked ? Theme.accentPrimary : Theme.bgSurface; border.color: Theme.borderControl; Rectangle { width: 8; height: 8; anchors.centerIn: parent; radius: 2; color: Theme.textOnAccent; visible: offlineCheckbox.checked } }
                    text: i18n.text("lyrics_offline_mode", i18n.language)
                    checked: Boolean(root.current.offline)
                    enabled: root.connected
                    onClicked: root.controller.setLyricsOffline(checked)
                    contentItem: Text {
                        text: offlineCheckbox.text
                        font.pixelSize: 12
                        color: Theme.textSecondary
                        leftPadding: offlineCheckbox.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Item { Layout.fillWidth: true }
                TextButton {
                    text: i18n.text("lyrics_search", i18n.language)
                    implicitWidth: Math.max(120, implicitContentWidth + leftPadding + rightPadding)
                    enabled: root.connected && titleField.text.trim().length > 0 && !root.current.offline
                    onClicked: root.controller.searchLyrics(searchPopup.trackId, titleField.text, artistField.text,
                                                      albumField.text, String(sourceBox.currentValue || "lrclib"))
                }
            }

            Label {
                Layout.fillWidth: true
                text: root.busy ? i18n.text("lyrics_searching", i18n.language)
                      : root.current.state === "error" ? i18n.text("lyrics_error_" + root.current.error, i18n.language)
                      : root.current.state === "candidates" && root.current.search_stage === "songs"
                        ? i18n.text("lyrics_choose_song", i18n.language)
                        : i18n.text("lyrics_" + (root.current.state || "idle"), i18n.language)
                color: Theme.textMuted
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.borderSubtle
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
                    contentItem: Rectangle { radius: 2; color: Theme.borderSubtle }
                }

                delegate: Rectangle {
                    id: candDelegate
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: 58
                    radius: Theme.radiusSm
                    color: candHover.hovered ? Theme.bgHover : Theme.bgSurface
                    border.color: candHover.hovered ? Theme.borderControl : Theme.borderSubtle
                    border.width: 1

                    HoverHandler { id: candHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            root.controller.selectLyrics(searchPopup.trackId, String(root.current.revision), index)
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
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontBodySecondary
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Label {
                                text: candDelegate.modelData.artist ? "· " + candDelegate.modelData.artist : ""
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
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
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                Layout.preferredHeight: 18
                                Layout.preferredWidth: typeLabel.implicitWidth + 10
                                radius: 4
                                color: candDelegate.modelData.synced ? Theme.bgSelected : Theme.bgRaised
                                border.color: candDelegate.modelData.synced ? Theme.borderControl : Theme.borderSubtle

                                Label {
                                    id: typeLabel
                                    anchors.centerIn: parent
                                    text: i18n.text(candDelegate.modelData.song_result ? "lyrics_song_version" : candDelegate.modelData.instrumental ? "lyrics_instrumental" : candDelegate.modelData.synced ? "lyrics_synced" : "lyrics_plain", i18n.language)
                                    color: candDelegate.modelData.synced ? Theme.accentPrimary : Theme.textMuted
                                    font.pixelSize: Theme.fontCaption
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
                        radius: 6
                        clip: true
                        color: Theme.bgSurface
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
