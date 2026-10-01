import QtQuick
import "../components"
PlayerPanel {
    id: page
    required property var shell
    required property var controllers
    required property var translator
    required property var transport
    signal importRequested(int playlistId)
    signal editRequested(var song)
    lyricsController: controllers.lyrics
    song: shell.song
    lyrics: shell.lyrics
    queue: shell.queue
    duration: shell.duration
    position: shell.position
    volume: shell.volume
    playbackState: shell.playbackState
    connected: transport.connected
    errorText: transport.error
    onAddRequested: page.importRequested(0)
    onTogglePlayPauseRequested: controllers.playback.togglePlayPause()
    onNextRequested: controllers.playback.next()
    onPreviousRequested: controllers.playback.previous()
    onSeekRequested: value => controllers.playback.seek(value)
    onVolumeRequested: value => controllers.playback.setVolume(value)
    onEditMetadataRequested: page.editRequested(shell.song)
}
