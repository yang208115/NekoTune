import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as PlayerComponents

Item {
    id: scene
    width: 900
    height: 640

    QtObject {
        id: i18n
        property string language: "en"
        function text(key, language) { return key.replace(/_/g, " ") }
        function countText(key, count, language) { return count + " tracks" }
    }

    Component {
        id: panelComponent
        PlayerComponents.QueuePanel { width: scene.width; height: scene.height }
    }

    TestCase {
        id: tests
        name: "Playlists"
        when: windowShown
        property var panel: null
        SignalSpy { id: playlistSpy; target: tests.panel; signalName: "playlistRequested" }
        SignalSpy { id: queueSpy; target: tests.panel; signalName: "playRequested" }
        SignalSpy { id: removeSpy; target: tests.panel; signalName: "removeRequested" }

        function init() {
            panel = createTemporaryObject(panelComponent, scene, {
                queue: [{id: 7, queue_id: 7, song_id: 3, title: "Queue song", path: "/music/a.wav"}],
                playlists: [
                    {id: 1, name: "Favorites", items: [{song_id: 3, title: "Playlist song", path: "/music/a.wav"}]},
                    {id: 2, name: "Empty playlist", items: []}
                ]
            })
            verify(panel !== null)
            playlistSpy.clear()
            queueSpy.clear()
            removeSpy.clear()
        }

        function cleanup() { panel = null }

        function test_navigationShowsOnlySelectedCollection() {
            compare(panel.visibleItems.length, 1)
            compare(panel.visibleItems[0].title, "Queue song")
            panel.currentPlaylist = 1
            compare(panel.visibleItems[0].title, "Playlist song")
            panel.queue = []
            compare(panel.visibleItems.length, 1)
            panel.currentPlaylist = 2
            compare(panel.visibleItems.length, 0)
            panel.playlists = []
            compare(panel.currentPlaylist, 0)
            compare(panel.visibleItems.length, 0)
        }

        function test_playAndRemoveUseCollectionIdentity() {
            panel.playSong(panel.visibleItems[0])
            compare(queueSpy.signalArguments[0][0], 7)
            panel.removeSong(panel.visibleItems[0])
            compare(removeSpy.signalArguments[0][0], 7)
            panel.currentPlaylist = 1
            panel.playSong(panel.visibleItems[0])
            compare(playlistSpy.signalArguments[0][0], "play")
            compare(playlistSpy.signalArguments[0][1].id, 1)
            compare(playlistSpy.signalArguments[0][1].song_id, 3)
            panel.removeSong(panel.visibleItems[0])
            compare(playlistSpy.signalArguments[1][0], "remove")
            compare(playlistSpy.signalArguments[1][1].song_id, 3)
            compare(removeSpy.count, 1)
        }

        function test_searchAndQueueIdentity() {
            panel.queue = [
                {id: 7, queue_id: 7, song_id: 3, title: "Same song", path: "/music/a.wav"},
                {id: 9, queue_id: 9, song_id: 3, title: "Same song", path: "/music/a.wav"}
            ]
            waitForRendering(panel)
            const row = findChild(panel, "queueRow9")
            mouseClick(row, 180, 32)
            compare(queueSpy.count, 0)
            compare(panel.selectedId, 9)
            wait(500)
            mouseDoubleClickSequence(row, 180, 32)
            compare(queueSpy.count, 1)
            compare(queueSpy.signalArguments[0][0], 9)
            panel.removeSong(panel.visibleItems[1])
            compare(removeSpy.signalArguments[0][0], 9)
            panel.currentPlaylist = 1
            panel.searchText = "PLAYLIST"
            compare(panel.visibleItems.length, 1)
            panel.playSong(panel.visibleItems[0])
            compare(playlistSpy.signalArguments[0][1].song_ids[0], 3)
            panel.searchText = "not present"
            compare(panel.visibleItems.length, 0)
        }

        function test_createPlaylistFromDialog() {
            panel.currentPlaylist = 1
            waitForRendering(panel)
            panel.newPlaylist()
            const popup = findChild(panel, "playlistNamePopup")
            tryCompare(popup, "opened", true)
            const input = findChild(panel, "playlistNameInput")
            input.text = "  New collection  "
            const save = findChild(panel, "savePlaylistButton")
            mouseClick(save)
            compare(playlistSpy.count, 1)
            compare(playlistSpy.signalArguments[0][0], "create")
            compare(playlistSpy.signalArguments[0][1].name, "New collection")
            compare(popup.opened, true)
            compare(panel.pendingMethod, "playlist.create")
            panel.requestFailed("playlist.create", "Could not save")
            compare(input.text, "  New collection  ")
            compare(panel.operationError, "Could not save")
            mouseClick(save)
            compare(playlistSpy.count, 2)
            panel.requestSucceeded("playlist.create")
            tryCompare(popup, "opened", false)
        }
    }
}
