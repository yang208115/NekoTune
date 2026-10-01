import QtQuick
import "../components"
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
        onDeleteRequested: ids => page.controllers.library.deleteLibrarySongs(ids)
        Connections {
            target: page.controllers.playlists
            function onRequestSucceeded(method) { if (method === "playlist.add") panel.playlistSucceeded() }
            function onRequestFailed(method, message) { if (method === "playlist.add") panel.playlistFailed(message) }
        }
        Connections {
            target: page.controllers.library
            function onLibraryPlaybackSkipped(count) { panel.skippedCount = count }
            function onRequestSucceeded(method) { if (method === "library.delete") panel.deleteSucceeded() }
            function onRequestFailed(method, message) { if (method === "library.delete") panel.deleteFailed(message) }
        }
    }
}
