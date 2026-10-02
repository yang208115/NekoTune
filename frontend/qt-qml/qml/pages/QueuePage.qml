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
    property alias currentPlaylist: panel.currentPlaylist
    onCurrentPlaylistChanged: if (shell) shell.currentPlaylist = currentPlaylist
    function focusSearch() { panel.focusSearch() }
    function newPlaylist() { panel.newPlaylist() }
    QueuePanel {
        id: panel
        currentPlaylist: page.shell.currentPlaylist
        objectName: "queuePanel"
        anchors.fill: parent
        anchors.margins: 20
        playlistController: page.controllers.playlists
        queueController: page.controllers.queue
        playbackController: page.controllers.playback
        playbackMode: page.controllers.playback.playbackMode
        connected: page.transport.connected
        onQueueAddRequested: path => page.controllers.queue.addPath(path)
        onLibraryRequested: page.shell.navigate("library")
        queue: page.controllers.queue.model.items
        queueModel: page.controllers.queue.model
        playlists: page.controllers.playlists.model.items
        currentSong: page.shell.song
        playbackState: page.shell.playbackState
        duration: page.shell.duration
        lyrics: page.shell.lyrics
        onAddRequested: id => page.importRequested(id)
        onClearRequested: page.controllers.queue.clearQueue()
        onPlayRequested: id => page.controllers.queue.playQueueItem(id)
        onTogglePlayPauseRequested: page.controllers.playback.togglePlayPause()
        onRemoveRequested: id => page.controllers.queue.removeQueueItem(id)
        onEditRequested: song => page.editRequested(song)
        onPlaylistRequested: (action, params) => page.controllers.playlists.managePlaylist(action, params)
    }
}
