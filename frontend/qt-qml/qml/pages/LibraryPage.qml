import QtQuick
import "../components"
// Adapter page connects reusable panel signals to feature controllers.
// Panel confirmation state is settled by operation-specific outcome signals.
// Playback-skipped and cleanup-leftover notices have different meanings.
// Navigation/edit dialogs remain owned by the shell rather than this panel.
Item {
    id: page
    required property var shell
    required property var controllers
    required property var translator
    required property var transport
    signal importRequested(int playlistId)
    signal editRequested(var song)
    signal newTagRequested()
    signal editTagRequested(var tag)
    function focusSearch() { panel.focusSearch() }
    LibraryPanel {
        id: panel
        objectName: "libraryPanel"
        anchors.fill: parent
        anchors.margins: 20
        controller: page.controllers.library
        connected: page.transport.connected
        currentSong: page.shell.song
        playing: page.shell.isPlaying
        currentDuration: page.shell.duration
        onQueueAddRequested: path => page.controllers.queue.addPath(path)
        onTogglePlayPauseRequested: page.controllers.playback.togglePlayPause()
        onNewTagRequested: page.newTagRequested()
        onEditTagRequested: tag => page.editTagRequested(tag)
        playlists: page.controllers.playlists.model.items
        onImportRequested: page.importRequested(0)
        onPlayRequested: (tags, song) => page.controllers.library.playLibrary(tags, song)
        onEditRequested: song => page.editRequested(song)
        onPlaylistRequested: (action, params) => page.controllers.playlists.managePlaylist(action, params)
        onDeleteRequested: (ids, cleanFiles) => page.controllers.library.deleteLibrarySongs(ids, cleanFiles)
        Connections {
            target: page.controllers.playlists
            function onRequestSucceeded(method) { if (method === "playlist.add") panel.playlistSucceeded() }
            function onRequestFailed(method, message) { if (method === "playlist.add") panel.playlistFailed(message) }
        }
        Connections {
            target: page.controllers.library
            function onLibraryPlaybackSkipped(count) { panel.skippedCount = count }
            function onLibraryDeletionFinished(errors) { panel.deleteSucceeded(errors) }
            function onRequestFailed(method, message) { if (method === "library.delete") panel.deleteFailed(message) }
        }
    }
}
