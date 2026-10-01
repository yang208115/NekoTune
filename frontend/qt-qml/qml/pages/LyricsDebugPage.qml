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
    LyricsDebugPanel { objectName: "lyricsDebugPage"; anchors.fill: parent; anchors.margins: 20; translator: page.translator; lyrics: page.shell.lyrics; song: page.shell.song; position: page.shell.position; duration: page.shell.duration; playbackState: page.shell.playbackState; onSeekRequested: value => page.controllers.playback.seek(value) }
}
