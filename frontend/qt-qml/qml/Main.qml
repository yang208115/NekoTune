import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "components"
import "dialogs"
import "shell"
import "shell/PageRegistry.js" as PageRegistry

ApplicationWindow {
    id: root
    width: 1360
    height: 860
    minimumWidth: 1000
    minimumHeight: 640
    visible: true
    title: "NekoTune"
    color: "#0E0D14"
    property var app: controllers
    property var transport: ipcClient
    property var translator: i18n
    property bool debugEnabled: Boolean(lyricsDebugEnabled)
    property string viewMode: "library"
    property bool nowPlayingOpen: false
    property bool queueOpen: false
    property int currentPlaylist: 0
    property var pages: PageRegistry.pages
    readonly property var theme: palette
    readonly property var song: app.playback.song
    readonly property var queue: app.queue.model.items
    readonly property var lyrics: app.lyrics.current
    readonly property var selectedTagIds: app.library.selectedTagIds
    readonly property real position: app.playback.position
    readonly property real duration: app.playback.duration
    readonly property real volume: app.playback.volume
    readonly property string playbackState: app.playback.state
    readonly property string databasePath: app.databasePath
    readonly property bool isPlaying: playbackState === "playing"
    readonly property bool hasSong: Boolean(song.song_id)
    readonly property int currentIndex: queue.findIndex(item => item.state === "current")
    Theme { id: palette }
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


    FileDialog {
        id: files
        property int playlistId: 0
        property string targetCollection: "library"
        title: root.translator.text("open_music", root.translator.language)
        fileMode: FileDialog.OpenFiles
        nameFilters: [root.translator.text("audio_files", root.translator.language) + " (*.mp3 *.m4a *.aac *.wav *.flac *.ogg)", root.translator.text("all_files", root.translator.language) + " (*)"]
        onAccepted: {
            for (let index = 0; index < selectedFiles.length; ++index) {
                const path = String(selectedFiles[index])
                if (targetCollection === "playlist") root.app.playlists.managePlaylist("add", {id: playlistId, path: path})
                else if (targetCollection === "queue") root.app.queue.addPath(path)
                else root.app.library.importLibraryPath(path)
            }
        }
    }
    MetadataEditor { id: metadataEditor; shell: root; controller: root.app.library; ai: root.app.ai; translator: root.translator }
    TagEditor { id: tagEditor; shell: root; controller: root.app.tags; translator: root.translator }
    function pageStatus(id) {
        const index = root.pages.findIndex(page => page.id === id)
        const loader = pageRepeater.itemAt(index) as Loader
        return loader ? loader.status : -1
    }
    function pageItem(id) {
        const index = root.pages.findIndex(page => page.id === id)
        const loader = pageRepeater.itemAt(index) as Loader
        return loader ? loader.item : null
    }
    function queuePage() {
        const index = root.pages.findIndex(page => page.id === "queue")
        const loader = pageRepeater.itemAt(index) as Loader
        return loader ? loader.item : null
    }
    function navigate(id, playlistId) {
        nowPlayingOpen = false
        queueOpen = false
        if (id === "queue") currentPlaylist = Number(playlistId || 0)
        viewMode = id
    }
    function openNowPlaying() { if (hasSong) { nowPlayingOpen = true; queueOpen = false } }
    function closeNowPlaying() {
        nowPlayingOpen = false
        queueOpen = false
        if (viewMode === "lyrics") viewMode = "library"
    }
    function openImport(target, playlistId) { files.targetCollection = target; files.playlistId = Number(playlistId || 0); files.open() }
    function currentPage() { return pageItem(viewMode) }
    readonly property bool editingText: {
        let item = activeFocusItem
        while (item) {
            if (item instanceof TextInput || item instanceof TextEdit) return true
            item = item.parent
        }
        return false
    }
    readonly property bool popupActive: Boolean(Overlay.overlay && Overlay.overlay.children.some(item => item.visible && item.background && item.background.objectName === "shortcutBlocker"))
    Shortcut { sequence: "Space"; enabled: root.transport.connected && (root.hasSong || root.queue.length > 0) && !root.editingText && !root.popupActive; onActivated: root.app.playback.togglePlayPause() }
    Shortcut {
        sequence: "Ctrl+F"
        enabled: !root.nowPlayingOpen && !root.popupActive
        onActivated: {
            const page = root.queueOpen ? queuePanel : root.currentPage()
            if (page && typeof page.focusSearch === "function") page.focusSearch()
        }
    }
    Shortcut {
        sequence: "Ctrl+O"
        enabled: root.transport.connected && !root.popupActive
        onActivated: root.openImport(root.queueOpen ? "queue" : root.viewMode === "queue" && root.currentPlaylist ? "playlist" : "library", root.currentPlaylist)
    }
    Shortcut { sequence: "Escape"; enabled: !root.popupActive; onActivated: { if (root.queueOpen) root.queueOpen = false; else root.closeNowPlaying() } }
    Connections {
        target: root.app.playlists
        function onPlaylistCreated(id) { root.navigate("queue", id) }
    }
    onViewModeChanged: queueOpen = false
    onCurrentPlaylistChanged: if (!currentPlaylist && viewMode === "queue") viewMode = "library"
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Item {
            id: workspace
            Layout.fillWidth: true
            Layout.fillHeight: true
            RowLayout {
                anchors.fill: parent
                spacing: 0
                Sidebar {
                    shell: root; controllers: root.app; transport: root.transport; translator: root.translator
                    pages: root.pages
                    currentPlaylist: root.currentPlaylist
                    visible: !root.nowPlayingOpen && root.viewMode !== "lyrics"
                    onPlaylistRequested: id => root.navigate("queue", id)
                    onNewPlaylistRequested: if (root.queuePage()) root.queuePage().newPlaylist()
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.color
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: errorLabel.implicitHeight + 20
                            visible: Boolean(root.transport.error)
                            color: "#38202B"
                            Label {
                                id: errorLabel
                                anchors.fill: parent; anchors.margins: 10
                                text: root.transport.error; textFormat: Text.PlainText
                                color: "#FF9BAE"; wrapMode: Text.WordWrap; font.pixelSize: 12
                            }
                        }
                        Item {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            Repeater {
                                id: pageRepeater
                                model: root.pages
                                delegate: Loader {
                                    id: pageLoader
                                    required property var modelData
                                    anchors.fill: parent
                                    active: !modelData.debug || root.debugEnabled
                                    visible: modelData.id === "lyrics" ? root.nowPlayingOpen || root.viewMode === "lyrics"
                                             : !root.nowPlayingOpen && root.viewMode === modelData.id
                                    objectName: modelData.id === "lyrics_debug" ? "lyricsDebugPageLoader" : modelData.id + "PageLoader"
                                    Component.onCompleted: if (active) setSource(Qt.resolvedUrl(modelData.source), {shell: root, controllers: root.app, transport: root.transport, translator: root.translator})
                                    Connections {
                                        target: pageLoader.item
                                        ignoreUnknownSignals: true
                                        function onImportRequested(id) { root.openImport(id ? "playlist" : modelData.id === "queue" ? "queue" : "library", id) }
                                        function onEditRequested(song) { metadataEditor.openForSong(song) }
                                        function onNewTagRequested() { tagEditor.openNew() }
                                        function onEditTagRequested(tag) { tagEditor.openForTag(tag) }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Rectangle {
                id: drawer
                objectName: "queueDrawer"
                anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
                width: root.width < 1200 ? 360 : 400
                visible: root.queueOpen
                color: root.surfaceRaised
                border.color: root.border
                z: 10
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.AllButtons
                    onWheel: wheel => wheel.accepted = true
                }
                QueuePanel {
                    id: queuePanel
                    objectName: "queueDrawerPanel"
                    anchors.fill: parent; anchors.margins: 16
                    compact: true
                    connected: root.transport.connected
                    queueController: root.app.queue
                    playlistController: root.app.playlists
                    queue: root.queue
                    queueModel: root.app.queue.model
                    playlists: root.app.playlists.model.items
                    duration: root.duration
                    currentSong: root.song; playbackState: root.playbackState
                    onAddRequested: root.openImport("queue", 0)
                    onClearRequested: root.app.queue.clearQueue()
                    onPlayRequested: id => root.app.queue.playQueueItem(id)
                    onTogglePlayPauseRequested: root.app.playback.togglePlayPause()
                    onRemoveRequested: id => root.app.queue.removeQueueItem(id)
                    onEditRequested: song => metadataEditor.openForSong(song)
                    onPlaylistRequested: (action, params) => root.app.playlists.managePlaylist(action, params)
                    onLibraryRequested: root.navigate("library")
                    onCloseRequested: root.queueOpen = false
                }
            }
        }
        BottomPlayer { shell: root; controller: root.app.playback; translator: root.translator; onEditRequested: song => metadataEditor.openForSong(song) }
    }
}
