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
    property string viewMode: "queue"
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
        title: root.translator.text("open_music", root.translator.language)
        fileMode: FileDialog.OpenFiles
        nameFilters: [root.translator.text("audio_files", root.translator.language) + " (*.mp3 *.m4a *.aac *.wav *.flac *.ogg)", root.translator.text("all_files", root.translator.language) + " (*)"]
        onAccepted: {
            for (let index = 0; index < selectedFiles.length; ++index) {
                if (playlistId) root.app.playlists.managePlaylist("add", {id: playlistId, path: String(selectedFiles[index])})
                else if (index === 0 && !root.hasSong) root.app.playback.playPath(String(selectedFiles[index]))
                else root.app.queue.addPath(String(selectedFiles[index]))
            }
        }
    }
    MetadataEditor { id: metadataEditor; shell: root; controller: root.app.library; translator: root.translator }
    TagEditor { id: tagEditor; shell: root; controller: root.app.tags; translator: root.translator }
    function pageStatus(id) {
        const index = root.pages.findIndex(page => page.id === id)
        const loader = pageRepeater.itemAt(index) as Loader
        return loader ? loader.status : -1
    }
    function queuePage() {
        const index = root.pages.findIndex(page => page.id === "queue")
        const loader = pageRepeater.itemAt(index) as Loader
        return loader ? loader.item : null
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Sidebar {
                shell: root; controllers: root.app; transport: root.transport; translator: root.translator
                pages: root.pages
                currentPlaylist: root.currentPlaylist
                onPlaylistRequested: id => { root.currentPlaylist = id; root.viewMode = "queue"; if (root.queuePage()) root.queuePage().currentPlaylist = id }
                onNewPlaylistRequested: { root.viewMode = "queue"; if (root.queuePage()) root.queuePage().newPlaylist() }
                onNewTagRequested: tagEditor.openNew()
                onEditTagRequested: tag => tagEditor.openForTag(tag)
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#110f17"
                Repeater {
                    id: pageRepeater
                    model: root.pages
                    delegate: Loader {
                        id: pageLoader
                        required property var modelData
                        anchors.fill: parent
                        active: !modelData.debug || root.debugEnabled
                        visible: root.viewMode === modelData.id
                        objectName: modelData.id === "lyrics_debug" ? "lyricsDebugPageLoader" : modelData.id + "PageLoader"
                        Component.onCompleted: if (active) setSource(Qt.resolvedUrl(modelData.source), {shell: root, controllers: root.app, transport: root.transport, translator: root.translator})
                        Connections {
                            target: pageLoader.item
                            function onImportRequested(id) { files.playlistId = id; files.open() }
                            function onEditRequested(song) { metadataEditor.openForSong(song) }
                        }
                    }
                }
            }
        }
        BottomPlayer { shell: root; controller: root.app.playback; translator: root.translator; onEditRequested: song => metadataEditor.openForSong(song) }
    }
}
