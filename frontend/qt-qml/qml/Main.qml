import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import NekoTune 1.0
import "components"
import "dialogs"
import "shell"
import "shell/PageRegistry.js" as PageRegistry

// The shell coordinates navigation and persistent application controllers.
// Browsing, now-playing and the queue drawer have separate state.
// Opening playback detail must not erase the previously browsed collection.
// Pages are registered in PageRegistry and remain controller consumers.
// Dialogs emit feature actions rather than touching storage or playback.
// Import destination is chosen before the file picker opens.
// Keyboard shortcuts follow active text editing and popup ownership.
ApplicationWindow {
    id: root
    width: 1360
    height: 860
    minimumWidth: 1000
    minimumHeight: 640
    visible: true
    title: "NekoTune"
    color: Theme.bgCanvas
    property var app: controllers
    property var transport: ipcClient
    property var translator: i18n
    property bool debugEnabled: Boolean(lyricsDebugEnabled)
    property string viewMode: "home"
    property string settingsSection: ""
    property bool nowPlayingOpen: false
    property bool queueOpen: false
    property int currentPlaylist: 0
    property var extensions: root.app.extensions || null
    readonly property bool hasBrowserSources: extensions !== null && extensions.browserSources.length > 0
    property var pages: PageRegistry.pages.concat(extensions ? extensions.pages : [])
    readonly property var theme: Theme
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

    // The same chooser serves three explicit collection destinations.
    // Library import only registers songs; playlist import also adds membership.
    // Queue import appends occurrences without automatically switching tracks.
    // Freeze the target/playlist before opening rather than reading whichever
    // page happens to be visible when an asynchronous selection is accepted.
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
        const loader = pageRepeater.itemAt(index)
        return loader ? loader.status : -1
    }
    function pageItem(id) {
        const index = root.pages.findIndex(page => page.id === id)
        const loader = pageRepeater.itemAt(index)
        return loader ? loader.item : null
    }
    function queuePage() {
        const index = root.pages.findIndex(page => page.id === "queue")
        const loader = pageRepeater.itemAt(index)
        return loader ? loader.item : null
    }
    // Navigation closes temporary playback surfaces before changing browse state.
    // The queue page also represents saved playlists, keyed by currentPlaylist.
    // Playlist identity is set before selecting that page so bindings agree.
    // No navigation action itself starts or replaces playback.
    function navigate(id, playlistId) {
        nowPlayingOpen = false
        queueOpen = false
        if (id === "music_sources" && !hasBrowserSources) id = "home"
        if (root.extensions && root.extensions.settingsPages.some(page => page.id === id)) {
            settingsSection = id
            viewMode = "settings"
            return
        }
        if (id === "settings") settingsSection = ""
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
    // Walk focus ancestors because a composite editor may own the focused item.
    // Transport shortcuts must not consume a Space typed into those editors.
    // Popup backgrounds opt into shortcut blocking by a shared object name.
    // This includes menus whose overlay parents are outside the page tree.
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
    Connections {
        target: root.extensions
        function onChanged() {
            Qt.callLater(() => {
                if (!root.pages.some(page => page.id === root.viewMode)
                    || root.viewMode === "music_sources" && !root.hasBrowserSources)
                    root.navigate("home")
            })
        }
    }
    onCurrentPlaylistChanged: if (!currentPlaylist && viewMode === "queue") viewMode = "library"
    Shortcut { sequence: "Ctrl+Shift+E"; onActivated: if (root.extensions) root.extensions.openManager() }
    Repeater {
        model: root.extensions ? root.extensions.commands : []
        delegate: Item {
            id: commandItem
            required property var modelData
            Shortcut { sequence: commandItem.modelData.shortcut || ""; enabled: sequence !== "" && !root.editingText && !root.popupActive; onActivated: root.extensions.execute(commandItem.modelData.id) }
        }
    }
    ExtensionView {
        id: fullShell; objectName: "shellExtension"; anchors.fill: parent
        descriptor: root.extensions ? root.extensions.activeSlots.shell || ({}) : ({})
        controllers: root.app; translator: root.translator; hostWindow: root; visible: ready
    }
    ColumnLayout {
        visible: !fullShell.ready
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            visible: toolbarRepeater.count > 0
            Repeater {
                id: toolbarRepeater
                model: root.extensions ? root.extensions.toolbars : []
                delegate: ExtensionView {
                    required property var modelData
                    Layout.fillWidth: true; Layout.preferredHeight: modelData.height || 48
                    descriptor: modelData; controllers: root.app; translator: root.translator; hostWindow: root
                }
            }
        }
        Item {
            id: workspace
            Layout.fillWidth: true
            Layout.fillHeight: true
            RowLayout {
                anchors.fill: parent
                spacing: 0
                Sidebar {
                    shell: root; controllers: root.app; transport: root.transport; translator: root.translator
                    pages: root.pages.filter(page => page.id !== "music_sources" || root.hasBrowserSources)
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
                            color: Theme.statusErrorBg
                            Label {
                                id: errorLabel
                                anchors.fill: parent; anchors.margins: 10
                                text: root.transport.error; textFormat: Text.PlainText
                                color: Theme.statusError; wrapMode: Text.WordWrap; font.pixelSize: 12
                            }
                        }
                        Item {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            Repeater {
                                id: pageRepeater
                                model: root.pages
                                delegate: PageHost {
                                    id: pageLoader
                                    anchors.fill: parent
                                    active: !modelData.debug || root.debugEnabled
                                    visible: modelData.id === "lyrics" ? root.nowPlayingOpen || root.viewMode === "lyrics"
                                             : !root.nowPlayingOpen && root.viewMode === modelData.id
                                    objectName: modelData.id === "lyrics_debug" ? "lyricsDebugPageLoader" : modelData.id + "PageLoader"
                                    shell: root; controllers: root.app; transport: root.transport; translator: root.translator
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
                anchors.rightMargin: root.queueOpen ? 0 : -8
                width: root.width < 1200 ? 360 : 400
                // Keep rendering through the exit animation, but stop accepting input immediately.
                visible: root.queueOpen || opacity > 0
                enabled: root.queueOpen
                opacity: root.queueOpen ? 1 : 0
                color: Theme.bgRaised
                border.color: Theme.borderSubtle
                border.width: 1
                z: 10
                Behavior on opacity {
                    NumberAnimation { duration: Theme.reducedMotion ? 0 : Theme.durationDrawer; easing.type: Easing.OutCubic }
                }
                Behavior on anchors.rightMargin {
                    NumberAnimation { duration: Theme.reducedMotion ? 0 : Theme.durationDrawer; easing.type: Easing.OutCubic }
                }
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
                    playbackController: root.app.playback
                    playbackMode: root.app.playback.playbackMode
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
        Item {
            Layout.fillWidth: true; Layout.preferredHeight: Theme.bottomBarHeight
            BottomPlayer { anchors.fill: parent; visible: !customPlayer.ready; shell: root; controller: root.app.playback; translator: root.translator; onEditRequested: song => metadataEditor.openForSong(song) }
            ExtensionView { id: customPlayer; anchors.fill: parent; descriptor: root.extensions ? root.extensions.activeSlots.bottomPlayer || ({}) : ({}); controllers: root.app; translator: root.translator; hostWindow: root; visible: ready }
        }
    }
}
