import QtQuick
import QtQuick.Controls
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
    topInset: 52
    IconButton {
        anchors.left: parent.left; anchors.top: parent.top; anchors.leftMargin: 24; anchors.topMargin: 8
        kind: "return"; tooltipText: page.translator.text("return_to_browse", page.translator.language)
        onClicked: page.shell.closeNowPlaying()
    }
    onAddRequested: page.importRequested(0)
    onTogglePlayPauseRequested: controllers.playback.togglePlayPause()
    onNextRequested: controllers.playback.next()
    onPreviousRequested: controllers.playback.previous()
    onSeekRequested: value => controllers.playback.seek(value)
    onVolumeRequested: value => controllers.playback.setVolume(value)
    onEditMetadataRequested: page.editRequested(shell.song)
}
