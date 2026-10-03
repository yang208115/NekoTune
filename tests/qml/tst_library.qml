import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as PlayerComponents

Rectangle {
    color: "#0E0D14"
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

        function test_focusRingIsNotClippedByList() {
            waitForRendering(panel)
            const list = findChild(panel, "librarySongList")
            const row = findChild(panel, "libraryRow1")
            verify(row !== null)
            row.forceActiveFocus()
            const ring = findChild(row, "trackFocusRing")
            verify(ring.visible)
            const topLeft = ring.mapToItem(list, 0, 0)
            const bottomRight = ring.mapToItem(list, ring.width, ring.height)
            verify(topLeft.x >= 0 && topLeft.y >= 0, "Focus ring must fit at the first row's top/left edge")
            verify(bottomRight.x <= list.width && bottomRight.y <= list.height)
            const path = testFixtures.screenshotPath("library-keyboard-focus")
            if (path) { waitForRendering(panel); grabImage(scene).save(path) }
        }

        // Multiple selected tags filter by intersection, including unavailable visible songs.
        // The playable count and the submitted visible context are therefore different quantities.
        // Starting a row uses its song identity, not its index in the filtered array.
        // The backend receives the context needed to skip unavailable entries consistently.
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

        // Selection and playback are separate desktop interactions.
        // The complete double-click sequence must produce one play command despite its press events.
        // The selection checkbox must not bubble into the row's playback action.
        // The wait separates the initial click from the later double-click gesture.
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

        // Toggle and range selection operate on the current visible order.
        // Changing search or tag filters removes selections that no longer belong to that scope.
        // Trying to select a hidden song must not reintroduce an invisible selected ID.
        // Search is case-insensitive and combines with the selected-tag intersection.
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

        // Select-all targets the filtered list, including entries whose files are unavailable.
        // Opening confirmation must not send the deletion before the user confirms.
        // A database failure retains selection and the dialog for retry.
        // Only successful completion clears selection and closes the confirmation.
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
            verify(findChild(panel, "libraryCleanFilesCheckBox").checked)
            mouseClick(findChild(panel, "libraryConfirmDeleteButton"))
            compare(deleteSpy.count, 1)
            compare(deleteSpy.signalArguments[0][0].length, 1)
            compare(deleteSpy.signalArguments[0][0][0], 1)
            compare(deleteSpy.signalArguments[0][1], true)
            compare(panel.deletePending, true)
            panel.deleteFailed("Database error")
            compare(panel.selectedSongIds.length, 1)
            compare(panel.deleteError, "Database error")
            panel.deleteSucceeded()
            compare(panel.selectedSongIds.length, 0)
            tryCompare(popup, "opened", false)
        }

        // The file-cleanup choice is frozen while the deletion request is pending.
        // A committed database deletion may still report an unlink cleanup warning.
        // That partial success closes confirmation while making the remaining staged path visible.
        // Opening another deletion resets both the warning and the default cleanup choice.
        // The database-only option is sent as a Boolean with the selected target snapshot.
        function test_cleanupChoiceAndPartialFailure() {
            const actions = panel.songActions(panel.filteredSongs[0])
            const deletionActions = actions.filter(action => action.destructive)
            compare(deletionActions.length, 1)
            compare(deletionActions[0].key, "delete")
            compare(deletionActions[0].label, "delete")
            panel.songAction("delete", panel.filteredSongs[0])
            const popup = findChild(panel, "libraryDeletePopup")
            tryCompare(popup, "opened", true)
            const choice = findChild(panel, "libraryCleanFilesCheckBox")
            verify(choice.checked)
            mouseClick(findChild(panel, "libraryConfirmDeleteButton"))
            compare(deleteSpy.count, 1)
            compare(deleteSpy.signalArguments[0][1], true)
            verify(!choice.enabled)
            panel.deleteSucceeded(["/managed/.song.removing"])
            tryCompare(popup, "opened", false)
            verify(findChild(panel, "libraryCleanupWarning").visible)
            verify(panel.cleanupWarning.indexOf("/managed/.song.removing") !== -1)
            panel.songAction("delete", panel.filteredSongs[0])
            tryCompare(popup, "opened", true)
            compare(panel.deletingSongIds.length, 1)
            verify(findChild(panel, "libraryConfirmDeleteButton").enabled)
            verify(choice.checked)
            compare(panel.cleanupWarning, "")
            mouseClick(choice)
            verify(!panel.cleanManagedFiles)
            verify(!panel.deletePending)
            verify(findChild(panel, "libraryConfirmDeleteButton").enabled)
            mouseClick(findChild(panel, "libraryConfirmDeleteButton"))
            compare(deleteSpy.count, 2)
            compare(deleteSpy.signalArguments[1][1], false)
            panel.deleteSucceeded()
            tryCompare(popup, "opened", false)
            panel.songAction("delete", panel.filteredSongs[0])
            tryCompare(popup, "opened", true)
            verify(choice.checked)
            const path = testFixtures.screenshotPath("library-cleanup-confirmation")
            if (path) {
                wait(160)
                waitForRendering(panel)
                grabImage(popup.parent.parent).save(path)
            }
        }

        // Rows display stored durations even when they have never been selected or played.
        // Only the active song may temporarily use the live backend duration.
        // Switching active identity restores the previous row's persisted value.
        // Unknown durations use the placeholder rather than an invented zero-length time.
        function test_persistedDurationDoesNotDependOnSelectionOrPlayback() {
            testFixtures.seedLibrary({tags: [], songs: [
                {song_id: 1, title: "夜空", artist: "示例歌手", available: true, duration_ms: 192000},
                {song_id: 2, title: "Magical Lights", artist: "示例歌手", available: true, duration_ms: 245000},
                {song_id: 3, title: "未知时长", available: false, duration_ms: 0}
            ]})
            waitForRendering(panel)
            const first = findChild(findChild(panel, "libraryRow1"), "trackDuration")
            const second = findChild(findChild(panel, "libraryRow2"), "trackDuration")
            const missing = findChild(findChild(panel, "libraryRow3"), "trackDuration")
            compare(first.text, "3:12")
            compare(second.text, "4:05")
            compare(missing.text, "--:--")
            const path = testFixtures.screenshotPath("library-persisted-durations")
            if (path) {
                waitForRendering(panel)
                wait(150)
                grabImage(scene).save(path)
            }
            testLibrary.selectRow(2)
            compare(first.text, "3:12")
            compare(second.text, "4:05")
            panel.currentSong = {song_id: 1}
            panel.currentDuration = 193000
            compare(first.text, "3:13")
            compare(second.text, "4:05")
            panel.currentSong = {song_id: 2}
            panel.currentDuration = 245000
            compare(first.text, "3:12")
        }

        // A library row has a song ID and need not have any queue occurrence.
        // Adding it to a playlist must submit that song identity with the target playlist ID.
        // A failed save retains the popup and target so the user can retry.
        // Closing the popup is deferred until the matching successful operation completes.
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
