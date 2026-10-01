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
        PlayerComponents.LibraryPanel { width: scene.width; height: scene.height }
    }

    TestCase {
        id: tests
        name: "Library"
        when: windowShown
        property var panel: null
        SignalSpy { id: playSpy; target: tests.panel; signalName: "playRequested" }
        SignalSpy { id: playlistSpy; target: tests.panel; signalName: "playlistRequested" }
        SignalSpy { id: deleteSpy; target: tests.panel; signalName: "deleteRequested" }

        function init() {
            testFixtures.seedLibrary({
                    tags: [{id: 1, name: "Rock"}, {id: 2, name: "Live"}],
                    songs: [
                        {song_id: 1, title: "Both", available: true, tags: [{id: 1, name: "Rock"}, {id: 2, name: "Live"}]},
                        {song_id: 2, title: "Rock only", available: true, tags: [{id: 1, name: "Rock"}]},
                        {song_id: 3, title: "Missing", available: false, tags: [{id: 1, name: "Rock"}, {id: 2, name: "Live"}]}
                    ]
            })
            panel = createTemporaryObject(panelComponent, scene, {
                controller: testLibrary,
                playlists: [{id: 8, name: "Favorites", items: []}]
            })
            verify(panel !== null)
            playSpy.clear()
            playlistSpy.clear()
            deleteSpy.clear()
        }

        function cleanup() { panel = null }

        function test_intersectionAndPlaybackIdentity() {
            compare(panel.filteredSongs.length, 3)
            testLibrary.selectedTagIds = [1, 2]
            compare(panel.filteredSongs.length, 2)
            compare(panel.filteredSongs[0].song_id, 1)
            compare(panel.playableCount, 1)
            waitForRendering(panel)
            mouseClick(findChild(panel, "playFilteredButton"))
            compare(playSpy.count, 1)
            compare(playSpy.signalArguments[0][0].length, 2)
            compare(playSpy.signalArguments[0][1], 0)
            testLibrary.selectRow(1)
            waitForRendering(panel)
            mouseClick(findChild(panel, "playLibrarySongButton1"))
            compare(playSpy.count, 2)
            compare(playSpy.signalArguments[1][1], 1)
        }

        function test_clickSelectsAndDoubleClickPlaysOnce() {
            waitForRendering(panel)
            const row = findChild(panel, "libraryRow1")
            verify(row !== null)
            mouseClick(row, 180, 32)
            compare(playSpy.count, 0)
            compare(panel.selectedSongIds.length, 1)
            compare(panel.selectedSongIds[0], 1)
            wait(500)
            mouseDoubleClickSequence(row, 180, 32)
            compare(playSpy.count, 1)
            compare(playSpy.signalArguments[0][1], 1)
            mouseClick(findChild(panel, "librarySelectSong1"))
            compare(playSpy.count, 1)
        }

        function test_searchAndDesktopSelection() {
            testLibrary.selectRow(1)
            testLibrary.selectRow(2, true)
            compare(panel.selectedSongIds.length, 2)
            testLibrary.selectRow(3, false, true)
            compare(panel.selectedSongIds.length, 2)
            compare(panel.selectedSongIds[0], 2)
            compare(panel.selectedSongIds[1], 3)
            testLibrary.searchText = "ROCK ONLY"
            compare(panel.filteredSongs.length, 1)
            compare(panel.filteredSongs[0].song_id, 2)
            compare(panel.selectedSongIds.length, 0)
            testLibrary.selectRow(1)
            compare(panel.selectedSongIds.length, 0)
            testLibrary.searchText = ""
            testLibrary.selectedTagIds = [1, 2]
            testLibrary.searchText = "both"
            compare(panel.filteredSongs.length, 1)
            testLibrary.searchText = "not present"
            compare(panel.filteredSongs.length, 0)
            compare(panel.playableCount, 0)
        }

        function test_selectFilteredAndDeleteConfirmation() {
            testLibrary.selectedTagIds = [1, 2]
            compare(panel.filteredSongs.length, 2)
            waitForRendering(panel)
            mouseClick(findChild(panel, "librarySelectAllButton"))
            compare(panel.selectedSongIds.length, 2)
            compare(panel.selectedSongIds[0], 1)
            compare(panel.selectedSongIds[1], 3)
            mouseClick(findChild(panel, "librarySelectSong3"))
            compare(panel.selectedSongIds.length, 1)
            compare(panel.selectedSongIds[0], 1)
            mouseClick(findChild(panel, "libraryDeleteSelectedButton"))
            const popup = findChild(panel, "libraryDeletePopup")
            tryCompare(popup, "opened", true)
            compare(deleteSpy.count, 0)
            mouseClick(findChild(panel, "libraryConfirmDeleteButton"))
            compare(deleteSpy.count, 1)
            compare(deleteSpy.signalArguments[0][0].length, 1)
            compare(deleteSpy.signalArguments[0][0][0], 1)
            compare(panel.deletePending, true)
            panel.deleteFailed("Database error")
            compare(panel.selectedSongIds.length, 1)
            compare(panel.deleteError, "Database error")
            panel.deleteSucceeded()
            compare(panel.selectedSongIds.length, 0)
            tryCompare(popup, "opened", false)
        }

        function test_addFromLibraryUsesSongId() {
            waitForRendering(panel)
            panel.songAction("playlist", panel.filteredSongs[0])
            const popup = findChild(panel, "libraryPlaylistPopup")
            tryCompare(popup, "opened", true)
            panel.addSongToPlaylist(8)
            compare(playlistSpy.count, 1)
            compare(playlistSpy.signalArguments[0][0], "add")
            compare(playlistSpy.signalArguments[0][1].id, 8)
            compare(playlistSpy.signalArguments[0][1].song_id, 1)
            compare(panel.addPending, true)
            panel.playlistFailed("Storage unavailable")
            compare(panel.operationError, "Storage unavailable")
            compare(popup.opened, true)
            panel.addSongToPlaylist(8)
            panel.playlistSucceeded()
            tryCompare(popup, "opened", false)
        }
    }
}
