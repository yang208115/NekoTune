import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Shapes
import "components"

ApplicationWindow {
    id: root
    width: 1360
    height: 860
    minimumWidth: 1000
    minimumHeight: 640
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
    readonly property bool isPlaying: playbackState === "playing"
    readonly property bool hasSong: Boolean(song && song.song_id)

    readonly property int currentIndex: {
        for (let index = 0; index < root.queue.length; index += 1) {
            if (root.queue[index].state === "current") return index
        }
        return -1
    }

    // High-end subtle palette (low-saturation dark lavender/rose system)
    readonly property color ink: "#f6f3fa"
    readonly property color muted: "#8e879c"
    readonly property color subtle: "#645e73"
    readonly property color border: "#211c2c"
    readonly property color surface: "#13111b"
    readonly property color surfaceRaised: "#1b1826"
    readonly property color lavender: "#cbb8ff"
    readonly property color rose: "#e8a9c3"

    // Main content page: queue, lyrics, settings, or CLI-enabled lyrics diagnostics.
    property string viewMode: "queue"

    FileDialog {
        id: fileDialog
        property int playlistId: 0
        title: i18n.text("open_music", i18n.language)
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            i18n.text("audio_files", i18n.language) + " (*.mp3 *.m4a *.aac *.wav *.flac *.ogg)",
            i18n.text("all_files", i18n.language) + " (*)"
        ]
        onAccepted: {
            for (let index = 0; index < selectedFiles.length; index += 1) {
                if (playlistId) ipcClient.managePlaylist("add", {id: playlistId, path: String(selectedFiles[index])})
                else if (index === 0 && !root.hasSong) ipcClient.playPath(selectedFiles[index])
                else ipcClient.addPath(selectedFiles[index])
            }
        }
    }

    // Metadata Editor Popup
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
        width: Math.min(580, parent.width - 48)
        height: Math.min(520, parent.height - 48)
        modal: true
        padding: 24
        background: Rectangle {
            color: root.surfaceRaised
            radius: 16
            border.color: "#352e46"
            border.width: 1
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: i18n.text("edit_track_info", i18n.language)
                    color: root.ink
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                IconButton {
                    kind: "close"
                    onClicked: metadataEditor.close()
                }
            }
            Label {
                Layout.fillWidth: true
                text: root.databasePath ? i18n.text("database", i18n.language) + ": " + root.databasePath : i18n.text("database_unavailable", i18n.language)
                color: root.subtle
                elide: Text.ElideMiddle
                font.pixelSize: 11
            }
            TextField {
                id: titleField
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                placeholderText: i18n.text("custom_title", i18n.language)
                color: root.ink
                placeholderTextColor: root.subtle
                leftPadding: 12
                rightPadding: 12
                background: Rectangle {
                    color: "#110e18"
                    radius: 8
                    border.color: titleField.activeFocus ? root.lavender : root.border
                }
            }
            TextField {
                id: artistField
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                placeholderText: i18n.text("artist_author", i18n.language)
                color: root.ink
                placeholderTextColor: root.subtle
                leftPadding: 12
                rightPadding: 12
                background: Rectangle {
                    color: "#110e18"
                    radius: 8
                    border.color: artistField.activeFocus ? root.lavender : root.border
                }
            }
            TextArea {
                id: lyricsField
                Layout.fillWidth: true
                Layout.fillHeight: true
                placeholderText: i18n.text("lyrics", i18n.language)
                color: root.ink
                placeholderTextColor: root.subtle
                wrapMode: TextEdit.Wrap
                padding: 12
                background: Rectangle {
                    color: "#110e18"
                    radius: 8
                    border.color: lyricsField.activeFocus ? root.lavender : root.border
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                TextButton {
                    text: i18n.text("cancel", i18n.language)
                    subtle: true
                    onClicked: metadataEditor.close()
                }
                TextButton {
                    text: i18n.text("save", i18n.language)
                    enabled: metadataEditor.songId > 0
                    onClicked: {
                        ipcClient.updateSongMetadata(metadataEditor.songId, titleField.text, artistField.text, lyricsField.text)
                        metadataEditor.close()
                    }
                }
            }
        }
    }

    // Main App Layout
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Workspace (Sidebar + Main Stage)
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Left Sidebar
            Rectangle {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                color: "#0d0b12"
                border.color: root.border
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 16

                    // Brand Section
                    RowLayout {
                        spacing: 10
                        Rectangle {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            radius: 8
                            color: "#1f1b2b"
                            border.color: "#352e46"
                            Label {
                                anchors.centerIn: parent
                                text: "N"
                                color: root.lavender
                                font.pixelSize: 16
                                font.weight: Font.Black
                            }
                        }
                        ColumnLayout {
                            spacing: 0
                            Label {
                                text: "NEKOTUNE"
                                color: root.ink
                                font.pixelSize: 14
                                font.weight: Font.Bold
                                font.letterSpacing: 1.6
                            }
                            Label {
                                text: "LOCAL LISTENING"
                                color: root.subtle
                                font.pixelSize: 8
                                font.letterSpacing: 0.8
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.border
                    }

                    // Navigation Items
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        // Queue Nav Button
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            radius: 8
                            color: root.viewMode === "queue" ? "#1e1a2b" : navQueueHover.hovered ? "#161320" : "transparent"

                            HoverHandler { id: navQueueHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: root.viewMode = "queue" }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 10

                                Label {
                                    text: "♫"
                                    color: root.viewMode === "queue" ? root.lavender : root.muted
                                    font.pixelSize: 14
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: i18n.text("queue", i18n.language)
                                    color: root.viewMode === "queue" ? root.ink : root.muted
                                    font.pixelSize: 13
                                    font.weight: root.viewMode === "queue" ? Font.DemiBold : Font.Normal
                                }
                                Rectangle {
                                    Layout.preferredWidth: countTextLabel.implicitWidth + 10
                                    Layout.preferredHeight: 18
                                    radius: 9
                                    color: "#272236"
                                    visible: root.queue.length > 0
                                    Label {
                                        id: countTextLabel
                                        anchors.centerIn: parent
                                        text: String(root.queue.length)
                                        color: root.muted
                                        font.pixelSize: 10
                                    }
                                }
                            }
                        }

                        // Lyrics Nav Button
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            radius: 8
                            color: root.viewMode === "lyrics" ? "#1e1a2b" : navLyricsHover.hovered ? "#161320" : "transparent"

                            HoverHandler { id: navLyricsHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: root.viewMode = "lyrics" }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 10

                                Label {
                                    text: "≡"
                                    color: root.viewMode === "lyrics" ? root.lavender : root.muted
                                    font.pixelSize: 15
                                    font.weight: Font.Bold
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: i18n.text("lyrics", i18n.language)
                                    color: root.viewMode === "lyrics" ? root.ink : root.muted
                                    font.pixelSize: 13
                                    font.weight: root.viewMode === "lyrics" ? Font.DemiBold : Font.Normal
                                }
                            }
                        }
                    }

                    TextButton {
                        objectName: "lyricsDebugNavigation"
                        Layout.fillWidth: true
                        visible: Boolean(lyricsDebugEnabled)
                        text: i18n.text("lyrics_debug_page", i18n.language)
                        subtle: root.viewMode !== "lyrics_debug"
                        onClicked: root.viewMode = "lyrics_debug"
                    }

                    TextButton {
                        objectName: "settingsNavigation"
                        Layout.fillWidth: true
                        text: i18n.text("settings", i18n.language)
                        subtle: root.viewMode !== "settings"
                        onClicked: root.viewMode = "settings"
                    }

                    Item { Layout.fillHeight: true }

                    // Sidebar Bottom Info & Language
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            // Connection Badge
                            Rectangle {
                                Layout.preferredHeight: 24
                                Layout.fillWidth: true
                                radius: 6
                                color: ipcClient.connected ? "#15201c" : "#22171d"
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 5
                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 4
                                        height: 4
                                        radius: 2
                                        color: ipcClient.connected ? "#7adfc6" : root.rose
                                    }
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: ipcClient.connected ? i18n.text("connected", i18n.language) : i18n.text("offline", i18n.language)
                                        color: ipcClient.connected ? "#a5e8d6" : root.rose
                                        font.pixelSize: 10
                                    }
                                }
                            }

                            // Language Switch
                            TextButton {
                                text: i18n.language === "zh" ? "EN" : "中"
                                implicitWidth: 38
                                implicitHeight: 24
                                cornerRadius: 6
                                subtle: true
                                onClicked: i18n.language = (i18n.language === "zh" ? "en" : "zh")
                            }
                        }
                    }
                }
            }

            // Main Stage Area
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#110f17"

                Loader {
                    objectName: "lyricsDebugPageLoader"
                    anchors.fill: parent
                    anchors.margins: 20
                    active: Boolean(lyricsDebugEnabled)
                    visible: active && root.viewMode === "lyrics_debug"
                    sourceComponent: LyricsDebugPanel {
                        objectName: "lyricsDebugPage"
                        translator: i18n
                        lyrics: root.lyrics
                        song: root.song
                        position: root.position
                        duration: root.duration
                        playbackState: root.playbackState
                        onSeekRequested: positionMs => ipcClient.seek(positionMs)
                    }
                }

                SettingsPanel {
                    id: settingsPanel
                    objectName: "settingsPanel"
                    anchors.fill: parent
                    visible: root.viewMode === "settings"
                    settings: ipcClient.status.settings || ({})
                    connected: ipcClient.connected
                    onSaveRequested: values => ipcClient.updateSettings(values)
                }
                Connections {
                    target: ipcClient
                    function onSettingsSaved(success, message) { settingsPanel.finishSaving(success, message) }
                }

                // Stack / Mode Switcher between Queue Table & Immersive Lyrics
                QueuePanel {
                    objectName: "queuePanel"
                    anchors.fill: parent
                    anchors.margins: 20
                    visible: root.viewMode === "queue"
                    queue: root.queue
                    playbackState: root.playbackState
                    playlists: ipcClient.status.playlists || []
                    currentSong: root.song

                    onAddRequested: playlistId => { fileDialog.playlistId = playlistId; fileDialog.open() }
                    onClearRequested: ipcClient.clearQueue()
                    onPlayRequested: id => ipcClient.playQueueItem(id)
                    onTogglePlayPauseRequested: ipcClient.togglePlayPause()
                    onRemoveRequested: id => ipcClient.removeQueueItem(id)
                    onEditRequested: songData => metadataEditor.openForSong(songData)
                    onPlaylistRequested: (action, params) => ipcClient.managePlaylist(action, params)
                }

                PlayerPanel {
                    anchors.fill: parent
                    visible: root.viewMode === "lyrics"
                    song: root.song
                    lyrics: root.lyrics
                    asr: ipcClient.status.asr || ({})
                    asrSettings: ipcClient.status.settings || ({})
                    onSettingsRequested: root.viewMode = "settings"
                    queue: root.queue
                    duration: root.duration
                    position: root.position
                    volume: root.volume
                    playbackState: root.playbackState
                    connected: ipcClient.connected
                    errorText: ipcClient.error

                    onAddRequested: playlistId => { fileDialog.playlistId = playlistId; fileDialog.open() }
                    onTogglePlayPauseRequested: ipcClient.togglePlayPause()
                    onNextRequested: ipcClient.next()
                    onPreviousRequested: ipcClient.previous()
                    onSeekRequested: val => ipcClient.seek(val)
                    onVolumeRequested: val => ipcClient.setVolume(val)
                    onEditMetadataRequested: metadataEditor.openForSong(root.song)
                }
            }
        }

        // Global Bottom Player Bar (Robust, Spacious, Tactile)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 82
            color: "#14111c"
            border.color: root.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                // Left Section: Mini Cover & Title & Edit Button
                Item {
                    Layout.preferredWidth: 280
                    Layout.fillHeight: true

                    RowLayout {
                        anchors.fill: parent
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 48
                            radius: 8
                            clip: true
                            color: "#1c1826"
                            border.color: "#2e283b"
                            border.width: 1

                            Image {
                                anchors.fill: parent
                                source: "qrc:/artwork/default-cover.png"
                                fillMode: Image.PreserveAspectCrop
                                opacity: root.hasSong ? 1.0 : 0.4
                            }

                            TapHandler {
                                enabled: root.hasSong
                                onTapped: root.viewMode = (root.viewMode === "queue" ? "lyrics" : "queue")
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Label {
                                Layout.fillWidth: true
                                text: root.hasSong ? (root.song.title || i18n.text("untitled", i18n.language)) : i18n.text("no_track_selected", i18n.language)
                                color: root.ink
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: root.hasSong ? (root.song.artist || i18n.text("artist_author", i18n.language)) : i18n.text("choose_audio", i18n.language)
                                color: root.muted
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }

                        // Edit Button for current song
                        IconButton {
                            kind: "edit"
                            tooltipText: i18n.text("edit_track_info", i18n.language)
                            glyphColor: root.muted
                            hoverGlyphColor: root.lavender
                            enabled: root.hasSong
                            visible: root.hasSong
                            implicitWidth: 32
                            implicitHeight: 32
                            iconSize: 16
                            onClicked: metadataEditor.openForSong(root.song)
                        }
                    }
                }

                // Center Section: Standard Media Controls & Generous Progress Slider
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 2

                    // Media Control Buttons
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 20

                        // Previous Track (|◀)
                        Button {
                            id: prevBtn
                            implicitWidth: 36
                            implicitHeight: 36
                            hoverEnabled: true
                            enabled: root.queue.length > 0
                            background: Rectangle {
                                radius: 18
                                color: prevBtn.down ? "#2d283c" : prevBtn.hovered ? "#221c2e" : "transparent"
                            }
                            contentItem: Item {
                                anchors.centerIn: parent
                                width: 16; height: 14
                                opacity: prevBtn.enabled ? 1.0 : 0.35

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 2.5; height: 13; radius: 1.25
                                    color: root.ink
                                }
                                Shape {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 11; height: 13
                                    ShapePath {
                                        fillColor: root.ink
                                        strokeColor: root.ink
                                        strokeWidth: 1
                                        joinStyle: ShapePath.RoundJoin
                                        startX: 11; startY: 0
                                        PathLine { x: 0; y: 6.5 }
                                        PathLine { x: 11; y: 13 }
                                        PathLine { x: 11; y: 0 }
                                    }
                                }
                            }
                            onClicked: ipcClient.previous()
                        }

                        // Main Play/Pause Button (44px, Absolute Precision Centering)
                        Button {
                            id: mainPlayButton
                            hoverEnabled: true
                            implicitWidth: 44
                            implicitHeight: 44
                            enabled: root.hasSong || root.queue.length > 0

                            background: Rectangle {
                                radius: 22
                                color: !mainPlayButton.enabled ? "#383244"
                                       : mainPlayButton.down ? "#bba4f2"
                                       : mainPlayButton.hovered ? "#d8c9ff"
                                       : root.lavender

                                Behavior on color { ColorAnimation { duration: 100 } }
                            }

                            contentItem: Item {
                                anchors.fill: parent

                                // Pause State (Two perfect rounded bars)
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 4.5
                                    visible: root.isPlaying

                                    Rectangle {
                                        width: 3.5
                                        height: 16
                                        radius: 1.75
                                        color: "#14111c"
                                    }
                                    Rectangle {
                                        width: 3.5
                                        height: 16
                                        radius: 1.75
                                        color: "#14111c"
                                    }
                                }

                                // Play State (Optically centered solid triangle)
                                Shape {
                                    anchors.centerIn: parent
                                    anchors.horizontalCenterOffset: 1.5
                                    width: 14
                                    height: 16
                                    visible: !root.isPlaying

                                    ShapePath {
                                        fillColor: "#14111c"
                                        strokeColor: "#14111c"
                                        strokeWidth: 1
                                        joinStyle: ShapePath.RoundJoin
                                        capStyle: ShapePath.RoundCap
                                        startX: 0; startY: 0
                                        PathLine { x: 14; y: 8 }
                                        PathLine { x: 0; y: 16 }
                                        PathLine { x: 0; y: 0 }
                                    }
                                }
                            }

                            onClicked: ipcClient.togglePlayPause()
                        }

                        // Next Track (▶|)
                        Button {
                            id: nextBtn
                            implicitWidth: 36
                            implicitHeight: 36
                            hoverEnabled: true
                            enabled: root.queue.length > 0 && root.currentIndex < root.queue.length - 1
                            background: Rectangle {
                                radius: 18
                                color: nextBtn.down ? "#2d283c" : nextBtn.hovered ? "#221c2e" : "transparent"
                            }
                            contentItem: Item {
                                anchors.centerIn: parent
                                width: 16; height: 14
                                opacity: nextBtn.enabled ? 1.0 : 0.35

                                Shape {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 11; height: 13
                                    ShapePath {
                                        fillColor: root.ink
                                        strokeColor: root.ink
                                        strokeWidth: 1
                                        joinStyle: ShapePath.RoundJoin
                                        startX: 0; startY: 0
                                        PathLine { x: 11; y: 6.5 }
                                        PathLine { x: 0; y: 13 }
                                        PathLine { x: 0; y: 0 }
                                    }
                                }
                                Rectangle {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 2.5; height: 13; radius: 1.25
                                    color: root.ink
                                }
                            }
                            onClicked: ipcClient.next()
                        }
                    }

                    // Progress Slider Row (Spacious & Tactile)
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 640
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 12

                        Label {
                            text: formatTime(timelineSlider.pressed
                                             ? timelineSlider.valueAt(timelineSlider.position)
                                             : timelineSlider.value)
                            color: root.subtle
                            font.pixelSize: 11
                            font.family: "Monospace"
                        }

                        SeekSlider {
                            id: timelineSlider
                            Layout.fillWidth: true
                            duration: root.duration
                            playbackPosition: root.position
                            activeColor: root.lavender
                            baseColor: "#272233"
                            onSeekRequested: positionMs => ipcClient.seek(positionMs)
                        }

                        Label {
                            text: formatTime(root.duration)
                            color: root.subtle
                            font.pixelSize: 11
                            font.family: "Monospace"
                        }
                    }
                }

                // Right Section: Volume & Lyrics Switch
                RowLayout {
                    Layout.preferredWidth: 260
                    Layout.fillHeight: true
                    spacing: 12

                    Item { Layout.fillWidth: true }

                    IconButton {
                        kind: "volume"
                        glyphColor: root.muted
                        implicitWidth: 32
                        implicitHeight: 32
                        iconSize: 18
                        onClicked: ipcClient.setVolume(root.volume > 0 ? 0 : 0.8)
                    }

                    PlayerSlider {
                        Layout.preferredWidth: 88
                        from: 0
                        to: 1
                        value: root.volume
                        activeColor: root.lavender
                        baseColor: "#272233"
                        onMoved: ipcClient.setVolume(value)
                    }

                    // Toggle Lyrics View Button
                    IconButton {
                        kind: "lyrics"
                        glyphColor: root.viewMode === "lyrics" ? root.lavender : root.muted
                        fillColor: root.viewMode === "lyrics" ? "#221c2e" : "transparent"
                        implicitWidth: 34
                        implicitHeight: 34
                        iconSize: 18
                        onClicked: root.viewMode = (root.viewMode === "queue" ? "lyrics" : "queue")
                    }
                }
            }
        }
    }

    function formatTime(ms) {
        const seconds = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
    }
}
