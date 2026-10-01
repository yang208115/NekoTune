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
    color: "#0E0D14"

    readonly property var song: ipcClient.status.song || ({})
    readonly property var queue: ipcClient.status.queue || []
    readonly property var library: ipcClient.status.library || ({songs: [], tags: []})
    property var selectedTagIds: []
    readonly property var lyrics: ipcClient.status.lyrics || ({})
    readonly property real duration: Number(ipcClient.status.duration || 0)
    readonly property real position: Number(ipcClient.status.position || 0)
    readonly property real volume: Number(ipcClient.status.volume || 0.8)
    readonly property string playbackState: String(ipcClient.status.state || "stopped")
    readonly property string databasePath: String(ipcClient.status.database_path || "")
    readonly property bool isPlaying: playbackState === "playing"
    readonly property bool hasSong: Boolean(song && song.song_id)

    function librarySongById(id) {
        const songs = root.library.songs || []
        for (let index = 0; index < songs.length; index += 1)
            if (Number(songs[index].song_id) === Number(id)) return songs[index]
        return null
    }

    function toggleTag(id) {
        const next = root.selectedTagIds.slice()
        const index = next.indexOf(Number(id))
        if (index < 0) next.push(Number(id))
        else next.splice(index, 1)
        root.selectedTagIds = next
        root.viewMode = "library"
    }

    onLibraryChanged: {
        const validIds = (root.library.tags || []).map(tag => Number(tag.id))
        root.selectedTagIds = root.selectedTagIds.filter(id => validIds.indexOf(Number(id)) !== -1)
        if (metadataEditor.visible && !metadataEditor.tagsReady) metadataEditor.loadTags()
    }

    readonly property int currentIndex: {
        for (let index = 0; index < root.queue.length; index += 1) {
            if (root.queue[index].state === "current") return index
        }
        return -1
    }

    // High-end subtle palette (Design tokens from docs/ui-design-guidelines.md)
    Theme { id: theme }

    readonly property color ink: "#F5F1FA"           // text.primary
    readonly property color subtle: "#D7CFE2"        // text.secondary
    readonly property color muted: "#AAA0B8"         // text.muted
    readonly property color border: "#332C41"        // border.subtle
    readonly property color borderControl: "#8D809F" // border.control
    readonly property color surface: "#17141F"       // bg.surface
    readonly property color surfaceRaised: "#211C2D" // bg.raised
    readonly property color bgSidebar: "#121019"     // bg.sidebar
    readonly property color bgHover: "#2A2338"       // bg.hover
    readonly property color bgSelected: "#322743"    // bg.selected
    readonly property color lavender: "#CBB8FF"      // accent.primary
    readonly property color rose: "#E8A9C3"          // accent.secondary

    // Main content page: queue, lyrics, or CLI-enabled lyrics diagnostics.
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
        property var tagNames: []
        property bool tagsReady: false
        property bool pending: false
        property string saveError: ""
        function loadTags() {
            const found = root.librarySongById(songId)
            if (!found) return
            tagNames = (found.tags || []).map(tag => tag.name)
            tagsReady = true
        }
        function addTag() {
            const name = tagInput.text.trim()
            if (!name || name.length > 64) return
            if (tagNames.some(tag => tag.toLocaleLowerCase() === name.toLocaleLowerCase())) {
                tagInput.text = ""
                return
            }
            tagNames = tagNames.concat([name])
            tagInput.text = ""
        }
        function openForSong(value) {
            songId = Number(value.song_id || 0)
            titleField.text = value.custom_title || value.title || ""
            artistField.text = value.artist || ""
            lyricsField.text = value.lyrics || ""
            tagInput.text = ""
            pending = false
            saveError = ""
            tagsReady = false
            tagNames = []
            loadTags()
            if (!tagsReady && value.tags) {
                tagNames = value.tags.map(tag => tag.name)
                tagsReady = true
            }
            if (!tagsReady) ipcClient.refreshLibrary()
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(580, parent.width - 48)
        height: Math.min(600, parent.height - 48)
        modal: true
        padding: 24
        background: Rectangle {
            color: root.surfaceRaised
            radius: 16
            border.color: root.border
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
                Layout.preferredHeight: 40
                placeholderText: i18n.text("custom_title", i18n.language)
                color: root.ink
                placeholderTextColor: root.muted
                leftPadding: 12
                rightPadding: 12
                background: Rectangle {
                    color: root.surface
                    radius: 8
                    border.color: titleField.activeFocus ? root.lavender : root.borderControl
                    border.width: 1
                }
            }
            TextField {
                id: artistField
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                placeholderText: i18n.text("artist_author", i18n.language)
                color: root.ink
                placeholderTextColor: root.muted
                leftPadding: 12
                rightPadding: 12
                background: Rectangle {
                    color: root.surface
                    radius: 8
                    border.color: artistField.activeFocus ? root.lavender : root.borderControl
                    border.width: 1
                }
            }
            Label {
                text: i18n.text("tags", i18n.language)
                color: root.subtle
                font.pixelSize: 12
            }
            Flow {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(0, childrenRect.height)
                spacing: 6
                Repeater {
                    model: metadataEditor.tagNames
                    delegate: TextButton {
                        required property var modelData
                        required property int index
                        text: String(modelData) + " ×"
                        subtle: true
                        implicitHeight: 28
                        onClicked: {
                            const next = metadataEditor.tagNames.slice()
                            next.splice(index, 1)
                            metadataEditor.tagNames = next
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: tagInput
                    Layout.fillWidth: true
                    enabled: metadataEditor.tagsReady
                    maximumLength: 64
                    placeholderText: i18n.text("tag_name", i18n.language)
                    color: root.ink
                    placeholderTextColor: root.muted
                    background: Rectangle { color: root.surface; radius: 8; border.color: root.borderControl }
                    onAccepted: metadataEditor.addTag()
                }
                TextButton {
                    text: i18n.text("add_tag", i18n.language)
                    subtle: true
                    enabled: metadataEditor.tagsReady && tagInput.text.trim().length > 0
                    onClicked: metadataEditor.addTag()
                }
            }
            Label {
                visible: !metadataEditor.tagsReady || Boolean(metadataEditor.saveError)
                text: !metadataEditor.tagsReady ? i18n.text("loading_tags", i18n.language)
                                                : metadataEditor.saveError
                color: root.rose
                font.pixelSize: 11
            }
            TextArea {
                id: lyricsField
                Layout.fillWidth: true
                Layout.fillHeight: true
                placeholderText: i18n.text("lyrics", i18n.language)
                color: root.ink
                placeholderTextColor: root.muted
                wrapMode: TextEdit.Wrap
                padding: 12
                background: Rectangle {
                    color: root.surface
                    radius: 8
                    border.color: lyricsField.activeFocus ? root.lavender : root.borderControl
                    border.width: 1
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
                    enabled: metadataEditor.songId > 0 && metadataEditor.tagsReady && !metadataEditor.pending
                    onClicked: {
                        metadataEditor.saveError = ""
                        metadataEditor.pending = true
                        ipcClient.updateSongMetadata(metadataEditor.songId, titleField.text,
                                                     artistField.text, lyricsField.text,
                                                     metadataEditor.tagNames)
                    }
                }
            }
        }
        Connections {
            target: ipcClient
            function onSongMetadataSaved(songId) {
                if (songId === metadataEditor.songId) metadataEditor.close()
            }
            function onRequestFailed(method, message) {
                if (method === "song.update_metadata" && metadataEditor.pending) {
                    metadataEditor.pending = false
                    metadataEditor.saveError = message
                }
            }
        }
    }

    Popup {
        id: tagEditor
        property int tagId: 0
        property bool pending: false
        property string saveError: ""
        function openNew() {
            tagId = 0
            tagNameField.text = ""
            pending = false
            saveError = ""
            open()
        }
        function openForTag(tag) {
            tagId = Number(tag.id)
            tagNameField.text = String(tag.name)
            pending = false
            saveError = ""
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 340
        height: (tagId ? 238 : 180) + (saveError ? 48 : 0)
        modal: true
        focus: true
        padding: 18
        background: Rectangle { color: root.surfaceRaised; radius: 14; border.color: root.border }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                text: i18n.text(tagEditor.tagId ? "rename_tag" : "new_tag", i18n.language)
                color: root.ink
                font.pixelSize: 16
            }
            TextField {
                id: tagNameField
                Layout.fillWidth: true
                maximumLength: 64
                placeholderText: i18n.text("tag_name", i18n.language)
                color: root.ink
                placeholderTextColor: root.muted
                background: Rectangle { color: root.surface; radius: 8; border.color: root.borderControl }
            }
            Label {
                visible: tagEditor.tagId > 0
                text: i18n.text("delete_tag_hint", i18n.language)
                wrapMode: Text.WordWrap
                color: root.muted
                font.pixelSize: 11
                Layout.fillWidth: true
            }
            Label {
                visible: Boolean(tagEditor.saveError)
                text: tagEditor.saveError
                color: root.rose
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                font.pixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                TextButton {
                    visible: tagEditor.tagId > 0
                    text: i18n.text("delete_tag", i18n.language)
                    subtle: true
                    enabled: !tagEditor.pending
                    onClicked: {
                        tagEditor.pending = true
                        tagEditor.saveError = ""
                        ipcClient.manageTag("delete", {id: tagEditor.tagId})
                    }
                }
                Item { Layout.fillWidth: true }
                TextButton {
                    text: i18n.text("cancel", i18n.language)
                    subtle: true
                    onClicked: tagEditor.close()
                }
                TextButton {
                    text: i18n.text("save", i18n.language)
                    enabled: tagNameField.text.trim().length > 0 && !tagEditor.pending
                    onClicked: {
                        tagEditor.pending = true
                        tagEditor.saveError = ""
                        ipcClient.manageTag(tagEditor.tagId ? "rename" : "create", {
                            id: tagEditor.tagId, name: tagNameField.text.trim()
                        })
                    }
                }
            }
        }
        Connections {
            target: ipcClient
            function onRequestSucceeded(method) {
                if (tagEditor.pending && method.startsWith("tag.")) tagEditor.close()
            }
            function onRequestFailed(method, message) {
                if (tagEditor.pending && method.startsWith("tag.")) {
                    tagEditor.pending = false
                    tagEditor.saveError = message
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

            // Left Sidebar (Section 5: 220 wide, 180 compact)
            Rectangle {
                Layout.preferredWidth: root.width < 1200 ? 180 : 220
                Layout.fillHeight: true
                color: root.bgSidebar
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
                            color: root.surface
                            border.color: root.border
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
                                color: root.muted
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

                    // Navigation Items (Section 7.1)
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        // Queue Nav Button
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            radius: 8
                            color: root.viewMode === "queue" ? root.bgSelected : navQueueHover.hovered ? root.bgHover : "transparent"

                            HoverHandler { id: navQueueHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: { queuePanel.currentPlaylist = 0; root.viewMode = "queue" } }

                            // 3px Moonlight purple indicator (Section 7.1)
                            Rectangle {
                                width: 3
                                height: 18
                                anchors.left: parent.left
                                anchors.leftMargin: 2
                                anchors.verticalCenter: parent.verticalCenter
                                radius: 1.5
                                color: root.lavender
                                visible: root.viewMode === "queue"
                            }

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
                                    color: root.bgHover
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
                            color: root.viewMode === "lyrics" ? root.bgSelected : navLyricsHover.hovered ? root.bgHover : "transparent"

                            HoverHandler { id: navLyricsHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: root.viewMode = "lyrics" }

                            // 3px Moonlight purple indicator (Section 7.1)
                            Rectangle {
                                width: 3
                                height: 18
                                anchors.left: parent.left
                                anchors.leftMargin: 2
                                anchors.verticalCenter: parent.verticalCenter
                                radius: 1.5
                                color: root.lavender
                                visible: root.viewMode === "lyrics"
                            }

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
                        Layout.fillWidth: true
                        text: i18n.text("library", i18n.language)
                        subtle: root.viewMode !== "library"
                        onClicked: root.viewMode = "library"
                    }

                    TextButton {
                        Layout.fillWidth: true
                        text: i18n.text("kugou_music", i18n.language)
                        subtle: root.viewMode !== "kugou"
                        onClicked: root.viewMode = "kugou"
                    }

                    TextButton {
                        Layout.fillWidth: true
                        text: i18n.text("settings", i18n.language)
                        subtle: root.viewMode !== "settings"
                        onClicked: root.viewMode = "settings"
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: width
                        contentHeight: sidebarGroups.implicitHeight
                        flickableDirection: Flickable.VerticalFlick
                        Column {
                            id: sidebarGroups
                            width: parent.width
                            spacing: 7
                            Label {
                                text: i18n.text("tags", i18n.language)
                                color: root.muted
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                            TextButton {
                                width: parent.width
                                text: i18n.text("all_songs", i18n.language)
                                subtle: true
                                subtleBg: root.viewMode === "library" && root.selectedTagIds.length === 0
                                    ? root.bgSelected : root.surface
                                implicitHeight: 30
                                onClicked: { root.selectedTagIds = []; root.viewMode = "library" }
                            }
                            TextButton {
                                width: parent.width
                                text: i18n.text("new_tag", i18n.language)
                                subtle: true
                                implicitHeight: 30
                                onClicked: tagEditor.openNew()
                            }
                            Repeater {
                                model: root.library.tags || []
                                delegate: RowLayout {
                                    required property var modelData
                                    width: sidebarGroups.width
                                    spacing: 3
                                    TextButton {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        subtle: true
                                        subtleBg: root.selectedTagIds.indexOf(Number(modelData.id)) !== -1
                                            ? root.bgSelected : root.surface
                                        implicitHeight: 30
                                        onClicked: root.toggleTag(Number(modelData.id))
                                    }
                                    TextButton {
                                        text: "⋯"
                                        subtle: true
                                        implicitWidth: 30
                                        implicitHeight: 30
                                        onClicked: tagEditor.openForTag(modelData)
                                    }
                                }
                            }
                            Rectangle { width: parent.width; height: 1; color: root.border }
                            Label {
                                text: i18n.text("playlists", i18n.language)
                                color: root.muted
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                            TextButton {
                                width: parent.width
                                text: i18n.text("new_playlist", i18n.language)
                                subtle: true
                                implicitHeight: 30
                                onClicked: { root.viewMode = "queue"; queuePanel.newPlaylist() }
                            }
                            Repeater {
                                model: ipcClient.status.playlists || []
                                delegate: TextButton {
                                    required property var modelData
                                    width: sidebarGroups.width
                                    text: modelData.name
                                    subtle: true
                                    subtleBg: root.viewMode === "queue" && queuePanel.currentPlaylist === Number(modelData.id)
                                        ? root.bgSelected : root.surface
                                    implicitHeight: 30
                                    onClicked: {
                                        queuePanel.currentPlaylist = Number(modelData.id)
                                        root.viewMode = "queue"
                                    }
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

                    // Sidebar Bottom Info & Language
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            visible: Boolean(ipcClient.error)
                            text: ipcClient.error
                            color: root.rose
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            // Connection Badge (Section 3.2: statusSuccess #98D8BC, statusError #FF9BAE)
                            Rectangle {
                                Layout.preferredHeight: 24
                                Layout.fillWidth: true
                                radius: 6
                                color: ipcClient.connected ? "#15221C" : "#38202B"
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 5
                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 5
                                        height: 5
                                        radius: 2.5
                                        color: ipcClient.connected ? "#98D8BC" : "#FF9BAE"
                                    }
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: ipcClient.connected ? i18n.text("connected", i18n.language) : i18n.text("offline", i18n.language)
                                        color: ipcClient.connected ? "#98D8BC" : "#FF9BAE"
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

                // Stack / Mode Switcher between Queue Table & Immersive Lyrics
                QueuePanel {
                    id: queuePanel
                    objectName: "queuePanel"
                    anchors.fill: parent
                    anchors.margins: 20
                    visible: root.viewMode === "queue"
                    queue: root.queue
                    playbackState: root.playbackState
                    playlists: ipcClient.status.playlists || []
                    currentSong: root.song
                    duration: root.duration
                    lyrics: root.lyrics

                    onAddRequested: playlistId => { fileDialog.playlistId = playlistId; fileDialog.open() }
                    onClearRequested: ipcClient.clearQueue()
                    onPlayRequested: id => ipcClient.playQueueItem(id)
                    onTogglePlayPauseRequested: ipcClient.togglePlayPause()
                    onRemoveRequested: id => ipcClient.removeQueueItem(id)
                    onEditRequested: songData => metadataEditor.openForSong(songData)
                    onPlaylistRequested: (action, params) => ipcClient.managePlaylist(action, params)
                }

                LibraryPanel {
                    id: libraryPanel
                    objectName: "libraryPanel"
                    anchors.fill: parent
                    anchors.margins: 20
                    visible: root.viewMode === "library"
                    library: root.library
                    selectedTagIds: root.selectedTagIds
                    playlists: ipcClient.status.playlists || []
                    onImportRequested: { fileDialog.playlistId = 0; fileDialog.open() }
                    onPlayRequested: (tagIds, songId) => ipcClient.playLibrary(tagIds, songId)
                    onEditRequested: songData => metadataEditor.openForSong(songData)
                    onPlaylistRequested: (action, params) => ipcClient.managePlaylist(action, params)
                    onDeleteRequested: songIds => ipcClient.deleteLibrarySongs(songIds)
                    Connections {
                        target: ipcClient
                        function onLibraryPlaybackSkipped(count) { libraryPanel.skippedCount = count }
                        function onRequestSucceeded(method) {
                            if (method === "library.delete") libraryPanel.deleteSucceeded()
                        }
                        function onRequestFailed(method, message) {
                            if (method === "library.delete") libraryPanel.deleteFailed(message)
                        }
                    }
                }

                KugouPanel {
                    anchors.fill: parent
                    anchors.margins: 20
                    visible: root.viewMode === "kugou"
                    client: ipcClient
                    translator: i18n
                }

                SettingsPanel {
                    anchors.fill: parent
                    anchors.margins: 20
                    visible: root.viewMode === "settings"
                    client: ipcClient
                    translator: i18n
                }

                PlayerPanel {
                    anchors.fill: parent
                    visible: root.viewMode === "lyrics"
                    song: root.song
                    lyrics: root.lyrics
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

        // Global Bottom Player Bar (Section 5: 96px, robust and tactile)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            color: root.bgSidebar
            border.color: root.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                // Left Section: Mini Cover & Title & Edit Button (220-280px)
                Item {
                    Layout.preferredWidth: root.width < 1200 ? 220 : 280
                    Layout.fillHeight: true

                    RowLayout {
                        anchors.fill: parent
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 48
                            radius: 8
                            clip: true
                            color: root.surface
                            border.color: root.border
                            border.width: 1

                            Image {
                                anchors.fill: parent
                                source: "qrc:/artwork/default-cover.png"
                                fillMode: Image.PreserveAspectCrop
                                opacity: root.hasSong ? 1.0 : 0.4
                            }

                            Image {
                                anchors.fill: parent
                                source: root.hasSong ? (String(root.song.cover_url || "")
                                        || (!root.lyrics.offline && root.lyrics.track_id === root.song.song_hash
                                            ? String((root.lyrics.document || {}).cover_url || "") : "")) : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                visible: status === Image.Ready
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
                    spacing: 4

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
                                color: prevBtn.down ? root.bgSelected : prevBtn.hovered ? root.bgHover : "transparent"
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

                        // Main Play/Pause Button (Section 5: 48x48 Circular, Section 3: textOnAccent #21172F)
                        Button {
                            id: mainPlayButton
                            hoverEnabled: true
                            implicitWidth: 48
                            implicitHeight: 48
                            enabled: root.hasSong || root.queue.length > 0

                            background: Rectangle {
                                id: mainPlayBg
                                radius: 24
                                color: !mainPlayButton.enabled ? "#2A2338"
                                       : mainPlayButton.down ? "#B7A0ED"
                                       : mainPlayButton.hovered ? "#DBCDFF"
                                       : root.lavender

                                // Focus ring (Section 7.1 & 11)
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    radius: parent.radius + 4
                                    color: "transparent"
                                    border.color: root.lavender
                                    border.width: 2
                                    visible: mainPlayButton.activeFocus
                                }

                                Behavior on color { ColorAnimation { duration: 120 } }
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
                                        color: "#21172F"
                                    }
                                    Rectangle {
                                        width: 3.5
                                        height: 16
                                        radius: 1.75
                                        color: "#21172F"
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
                                        fillColor: "#21172F"
                                        strokeColor: "#21172F"
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
                                color: nextBtn.down ? root.bgSelected : nextBtn.hovered ? root.bgHover : "transparent"
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
                                             : timelineSlider.value, false)
                            color: root.subtle
                            font.pixelSize: 11
                            font.family: Theme.fontFamilyMonospace
                        }

                        SeekSlider {
                            id: timelineSlider
                            Layout.fillWidth: true
                            duration: root.duration
                            playbackPosition: root.position
                            activeColor: root.lavender
                            baseColor: root.border
                            onSeekRequested: positionMs => ipcClient.seek(positionMs)
                        }

                        Label {
                            text: formatTime(root.duration, true)
                            color: root.subtle
                            font.pixelSize: 11
                            font.family: Theme.fontFamilyMonospace
                        }
                    }
                }

                // Right Section: Volume & Lyrics Switch (160-220px)
                RowLayout {
                    Layout.preferredWidth: root.width < 1200 ? 180 : 220
                    Layout.fillHeight: true
                    spacing: 12

                    Item { Layout.fillWidth: true }

                    IconButton {
                        kind: "volume"
                        glyphColor: root.muted
                        hoverGlyphColor: root.lavender
                        implicitWidth: 32
                        implicitHeight: 32
                        iconSize: 18
                        onClicked: ipcClient.setVolume(root.volume > 0 ? 0 : 0.8)
                    }

                    PlayerSlider {
                        Layout.preferredWidth: root.width < 1200 ? 72 : 88
                        from: 0
                        to: 1
                        value: root.volume
                        activeColor: root.lavender
                        baseColor: root.border
                        onMoved: ipcClient.setVolume(value)
                    }

                    // Toggle Lyrics View Button
                    IconButton {
                        kind: "lyrics"
                        glyphColor: root.viewMode === "lyrics" ? root.lavender : root.muted
                        hoverGlyphColor: root.lavender
                        fillColor: root.viewMode === "lyrics" ? root.bgSelected : "transparent"
                        hoverColor: root.bgHover
                        implicitWidth: 36
                        implicitHeight: 36
                        iconSize: 18
                        onClicked: root.viewMode = (root.viewMode === "queue" ? "lyrics" : "queue")
                    }
                }
            }
        }
    }

    function formatTime(ms, isDuration) {
        if (isDuration && (!ms || Number(ms) <= 0)) return "--:--"
        const seconds = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
    }
}
