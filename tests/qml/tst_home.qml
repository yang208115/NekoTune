import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/pages" as Pages
import "../../frontend/qt-qml/qml/components" 1.0 as Components

Rectangle {
    id: scene
    width: 1140
    height: 764
    color: "#0E0D14"
    QtObject { id: songModel; property var items: [] }
    QtObject { id: playlistModel; property var items: [] }
    QtObject { id: fakeTransport; property bool connected: true }
    QtObject {
        id: libraryObject
        property var songs: songModel
        property bool loading: false
        property bool loaded: true
        property string searchText: ""
        property var selectedTagIds: []
        property var lastIds: []
        property int lastStart: 0
        property int playCount: 0
        property int refreshCount: 0
        signal libraryPlaybackSkipped(int count)
        signal requestSucceeded(string method)
        signal requestFailed(string method, string message)
        function refreshLibrary() { refreshCount++; loading = true }
        function playSongs(ids, start) {
            playCount++
            lastIds = ids
            lastStart = start
            libraryPlaybackSkipped(ids.filter(id => !songModel.items.find(song => song.song_id === id).available).length)
            requestSucceeded("library.play")
        }
    }
    QtObject {
        id: playlistController
        property var model: playlistModel
        property int playCount: 0
        property int lastId: 0
        function managePlaylist(action, params) { if (action === "play") { playCount++; lastId = params.id } }
    }
    QtObject {
        id: playbackController
        property int toggles: 0
        property int plays: 0
        function togglePlayPause() { toggles++ }
        function play() { plays++ }
    }
    QtObject {
        id: queueController
        property var lastIds: []
        property bool lastNext: false
        function addSongs(ids, next) { lastIds = ids; lastNext = next }
    }
    QtObject { id: app; property var library: libraryObject; property var playlists: playlistController; property var playback: playbackController; property var queue: queueController }
    QtObject {
        id: playlistPageStub
        property int created: 0
        function newPlaylist() { created++ }
    }
    QtObject {
        id: fakeShell
        property var theme: Components.Theme
        property int width: 1360
        property var song: ({})
        property var queue: []
        property string playbackState: "stopped"
        readonly property bool hasSong: Boolean(song.song_id)
        readonly property bool isPlaying: playbackState === "playing"
        property string viewMode: "home"
        property int currentPlaylist: 0
        property bool nowPlayingOpen: false
        function navigate(id, playlistId) { viewMode = id; currentPlaylist = playlistId || 0 }
        function openNowPlaying() { nowPlayingOpen = true }
        function queuePage() { return playlistPageStub }
    }
    Component {
        id: homeComponent
        Pages.HomePage {
            width: scene.width; height: scene.height
            shell: fakeShell; controllers: app; translator: testTranslator; transport: fakeTransport
        }
    }
    TestCase {
        id: tests
        name: "Home"
        when: windowShown
        property var page: null
        SignalSpy { id: importSpy; target: tests.page; signalName: "importRequested" }
        function init() {
            scene.width = 1140; scene.height = 764; fakeShell.width = 1360
            fakeShell.song = {}; fakeShell.queue = []; fakeShell.playbackState = "stopped"
            fakeShell.viewMode = "home"; fakeShell.currentPlaylist = 0; fakeShell.nowPlayingOpen = false
            fakeTransport.connected = true
            libraryObject.loaded = true; libraryObject.loading = false
            libraryObject.searchText = "unrelated filter"; libraryObject.selectedTagIds = [42]
            libraryObject.playCount = 0; libraryObject.lastIds = []; libraryObject.refreshCount = 0
            playlistController.playCount = 0; playbackController.toggles = 0; playbackController.plays = 0; playlistPageStub.created = 0
            const songs = []
            for (let i = 1; i <= 6; ++i)
                songs.push({song_id: i, title: "Song " + i, artist: "Artist " + i, available: i !== 4, duration: 184000})
            songModel.items = songs
            playlistModel.items = [
                {id: 7, name: "Night radio", items: [songs[0], songs[1]]},
                {id: 8, name: "Empty playlist", items: []},
                {id: 9, name: "A very long playlist title with more words than the card can fit", items: [songs[2]]},
                {id: 10, name: "静かな夜", items: [songs[3]]},
                {id: 11, name: "More music", items: []}
            ]
            page = createTemporaryObject(homeComponent, scene)
            verify(page !== null)
            importSpy.clear()
            waitForRendering(page)
        }
        function cleanup() { page = null }
        function button(name) { const result = findChild(page, name); verify(result !== null, name); return result }
        function test_recentSongQueueMenu() {
            const row = button("homeRecentSong6")
            row.actionRequested("next")
            compare(queueController.lastIds.join(","), "6")
            compare(queueController.lastNext, true)
            row.actionRequested("queue")
            compare(queueController.lastNext, false)
        }
        function test_recentPlayedSwitchAndPlaybackScope() {
            const now = Date.now(), day = 24 * 60 * 60 * 1000
            const songs = songModel.items.map(song => Object.assign({}, song))
            songs[0].last_played_at = new Date(now - day).toISOString()
            songs[1].last_played_at = new Date(now - 3 * day).toISOString()
            songs[2].last_played_at = "invalid"
            songs[4].last_played_at = new Date(now - 2 * day).toISOString()
            songModel.items = songs
            const original = songs.map(song => song.last_played_at || "").join(",")
            const picker = button("homeRecentPicker")
            mouseClick(picker)
            tryCompare(picker.popup, "opened", true)
            keyClick(Qt.Key_End)
            keyClick(Qt.Key_Return)
            tryCompare(picker.popup, "opened", false)
            compare(page.recentView, "played")
            compare(page.recentSongs.map(song => song.song_id), [1, 5, 2])
            compare(libraryObject.playCount, 0)
            compare(songModel.items.map(song => song.last_played_at || "").join(","), original)
            page.playRecent(songs[4])
            compare(libraryObject.lastIds, [1, 5, 2])
            compare(libraryObject.lastStart, 5)
            page.recentView = "added"
            compare(page.recentSongs.map(song => song.song_id), [6, 5, 4, 3, 2])
            const updated = songs.map(song => Object.assign({}, song))
            updated[1].last_played_at = new Date(now).toISOString()
            songModel.items = updated
            page.recentView = "played"
            compare(page.recentSongs.map(song => song.song_id), [2, 1, 5])
        }
        function test_recentPlayedHasOwnEmptyState() {
            page.recentView = "played"
            compare(page.recentSongs.length, 0)
            compare(button("homeLibraryState").text, testTranslator.text("home_empty_recent_played"))
            compare(page.primaryAction, "home_play_library")
            page.recentView = "added"
            compare(page.recentSongs.length, 5)
            compare(libraryObject.playCount, 0)
        }

        // Creating the page must not start playback as a side effect of choosing a primary action.
        // The action changes with current song, queue and playback state.
        // Playing the library uses the full home scope while preserving the library page's filters.
        // Opening lyrics is navigation and must remain separate from starting playback.
        function test_primaryActionsNeverAutoplay() {
            compare(libraryObject.playCount, 0)
            compare(playbackController.toggles + playbackController.plays, 0)
            compare(page.primaryAction, "home_play_library")
            mouseClick(button("homePrimaryButton"))
            compare(libraryObject.lastIds, [1, 2, 3, 4, 5, 6])
            compare(libraryObject.lastStart, 0)
            compare(libraryObject.searchText, "unrelated filter")
            compare(libraryObject.selectedTagIds, [42])
            fakeShell.queue = [{id: 10}]
            compare(page.primaryAction, "home_play_queue")
            mouseClick(button("homePrimaryButton"))
            compare(playbackController.plays, 1)
            fakeShell.song = {song_id: 3, title: "Current song"}
            for (const state of ["playing", "paused", "stopped"]) {
                fakeShell.playbackState = state
                compare(page.primaryAction, state === "playing" ? "pause" : state === "paused" ? "home_resume" : "play")
                waitForRendering(page)
                mouseClick(button("homePrimaryButton"))
            }
            compare(playbackController.toggles, 3)
            mouseClick(button("homeLyricsButton"))
            verify(fakeShell.nowPlayingOpen)
        }
        // The recent list supplies its own ordering, independent of library search and tags.
        // A single click selects; double click or Enter starts that visible list at the chosen song.
        // An unavailable entry remains in the submitted context for backend skip reporting.
        // The current-song button can toggle playback without rebuilding the recent queue.
        function test_recentOrderSelectionAndPlaybackScope() {
            compare(page.recentSongs.map(song => song.song_id), [6, 5, 4, 3, 2])
            const row = button("homeRecentSong5")
            mouseClick(row, 180, 30)
            compare(page.selectedSongId, 5)
            compare(libraryObject.playCount, 0)
            mouseDoubleClickSequence(row, 180, 30)
            compare(libraryObject.playCount, 1)
            compare(libraryObject.lastIds, [6, 5, 4, 3, 2])
            compare(libraryObject.lastStart, 5)
            compare(page.skippedCount, 1)
            compare(libraryObject.searchText, "unrelated filter")
            compare(libraryObject.selectedTagIds, [42])
            row.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(libraryObject.playCount, 2)
            fakeShell.song = songModel.items[4]
            fakeShell.playbackState = "playing"
            mouseClick(button("homePlaySong5"))
            compare(playbackController.toggles, 1)
            compare(libraryObject.playCount, 2)
            row.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(libraryObject.playCount, 3)
            compare(playbackController.toggles, 1)
        }
        // Missing audio can still be displayed and selected but cannot start playback.
        // Disconnect disables commands without discarding the visible home snapshot.
        // The direct play helper is checked too, because disabled buttons alone are insufficient.
        // Reconnection restores command availability from the same state.
        function test_unavailableAndDisconnectedCannotPlay() {
            const missing = button("homeRecentSong4")
            mouseClick(missing, 180, 30)
            verify(!button("homePlaySong4").enabled)
            missing.forceActiveFocus()
            keyClick(Qt.Key_Return)
            mouseDoubleClickSequence(missing, 180, 30)
            compare(libraryObject.playCount, 0)
            fakeTransport.connected = false
            verify(!button("homePrimaryButton").enabled)
            verify(!button("homeImportButton").enabled)
            verify(!button("homePlayPlaylist7").enabled)
            verify(!button("homeNewPlaylistButton").enabled)
            compare(page.recentSongs.length, 5)
            page.playRecent(songModel.items[5])
            compare(libraryObject.playCount, 0)
            fakeTransport.connected = true
            verify(button("homePrimaryButton").enabled)
        }
        // The play button inside a playlist card must consume its own interaction.
        // It should play that collection without triggering the card's navigation action.
        // Clicking the card body instead selects the playlist page.
        // Empty collections expose navigation while disabling their play action.
        function test_playlistActionsDoNotBubble() {
            verify(findChild(page, "homePlaylist11") === null)
            mouseClick(button("homePlayPlaylist7"))
            compare(playlistController.playCount, 1)
            compare(playlistController.lastId, 7)
            compare(fakeShell.viewMode, "home")
            verify(!button("homePlayPlaylist8").enabled)
            mouseClick(button("homePlaylist7"), 60, 30)
            compare(fakeShell.viewMode, "queue")
            compare(fakeShell.currentPlaylist, 7)
            mouseClick(button("homeNewPlaylistButton"))
            compare(playlistPageStub.created, 1)
        }
        // View-all deliberately clears library filters before navigating to the full collection.
        // Import is emitted with the home target rather than a currently hidden playlist target.
        // These navigation actions must preserve their distinct scopes.
        function test_viewAllAndImport() {
            mouseClick(button("homeViewAllButton"))
            compare(fakeShell.viewMode, "library")
            compare(libraryObject.searchText, "")
            compare(libraryObject.selectedTagIds, [])
            mouseClick(button("homeImportButton"))
            compare(importSpy.count, 1)
            compare(importSpy.signalArguments[0][0], 0)
        }
        // An empty model alone cannot distinguish pending load, failed load and an empty library.
        // The loaded/loading flags and connection state determine the message and available action.
        // Retry belongs to load failure; import belongs to a successfully loaded empty library.
        // Offline state must not masquerade as a successful empty result.
        function test_loadingFailureEmptyAndOffline() {
            songModel.items = []; playlistModel.items = []
            libraryObject.loaded = false; libraryObject.loading = true
            compare(button("homeLibraryState").text, testTranslator.text("home_loading"))
            verify(!button("homePrimaryButton").enabled)
            verify(!button("homeRetryButton").visible)
            libraryObject.loading = false
            compare(button("homeLibraryState").text, testTranslator.text("home_load_failed"))
            mouseClick(button("homeRetryButton"))
            compare(libraryObject.refreshCount, 1)
            libraryObject.loaded = true; libraryObject.loading = false
            compare(button("homeLibraryState").text, testTranslator.text("home_empty_library"))
            compare(page.primaryAction, "import_music")
            mouseClick(button("homePrimaryButton"))
            compare(importSpy.count, 1)
            fakeTransport.connected = false
            compare(button("homeLibraryState").text, testTranslator.text("home_offline"))
            const path = testFixtures.screenshotPath("home-empty-offline")
            if (path) { waitForRendering(page); grabImage(scene).save(path) }
        }
        // The current song's cover URL must be replaced when song identity changes.
        // Missing or failed images should reveal the fallback instead of retaining old artwork.
        // The fixture includes both an absent URL and an asynchronously failing image.
        // Assertions use Image status so assigning a source is not mistaken for successful loading.
        function test_coverIdentityAndFallback() {
            const cover = button("homeCurrentCover")
            fakeShell.song = {song_id: 1, title: "Song", cover_url: "qrc:/artwork/default-cover.png"}
            tryCompare(cover, "status", Image.Ready)
            verify(cover.visible)
            fakeShell.song = {song_id: 2, title: "No cover"}
            compare(String(cover.source), "")
            verify(!cover.visible)
            ignoreWarning(new RegExp(".*Cannot open: qrc:/artwork/missing-home-cover.png"))
            fakeShell.song = {song_id: 3, title: "Broken cover", cover_url: "qrc:/artwork/missing-home-cover.png"}
            tryCompare(cover, "status", Image.Error)
            verify(!cover.visible)
            const songs = songModel.items.slice()
            songs[1] = Object.assign({}, songs[1], {cover_url: "qrc:/artwork/default-cover.png"})
            songModel.items = songs
            compare(page.playlistCover(playlistModel.items[0]), "qrc:/artwork/default-cover.png")
            compare(page.playlistCover(playlistModel.items[1]), "")
        }
        function test_coversUseDisplayResolutionAfterLayoutChanges() {
            const source = "qrc:/artwork/default-cover.png"
            fakeShell.song = {song_id: 1, title: "Song", cover_url: source}
            const songs = songModel.items.slice()
            songs[0] = Object.assign({}, songs[0], {cover_url: source})
            songs[5] = Object.assign({}, songs[5], {cover_url: source})
            songModel.items = songs
            const covers = [button("homeCurrentCover"),
                            findChild(button("homePlaylist7"), "homePlaylistCover"),
                            findChild(button("homeRecentSong6"), "trackCover")]
            for (const compact of [false, true]) {
                fakeShell.width = compact ? 1000 : 1360
                scene.width = compact ? 820 : 1140
                waitForRendering(page)
                for (const cover of covers) {
                    verify(cover)
                    tryCompare(cover, "status", Image.Ready)
                    compare(cover.sourceSize, Qt.size(Math.ceil(cover.width * cover.decodeScale),
                                                      Math.ceil(cover.height * cover.decodeScale)))
                    verify(cover.implicitWidth < 1254)
                    const fallback = cover.parent.children.find(child => child !== cover && child.sourceSize !== undefined)
                    verify(fallback)
                    tryCompare(fallback, "status", Image.Ready)
                    compare(fallback.sourceSize, cover.sourceSize)
                }
                const path = testFixtures.screenshotPath("image-sizing-home-" + (compact ? "compact" : "wide"))
                if (path) { waitForRendering(page); grabImage(scene).save(path) }
            }
        }
        // Responsive layout changes must retain the user's position in the home scroll area.
        // The compact arrangement still keeps the primary controls within their available width.
        // This guards layout recomputation from becoming an implicit navigation reset.
        function test_compactLayoutAndScrollRetained() {
            fakeShell.width = 1000; scene.width = 820; scene.height = 544
            waitForRendering(page)
            const lists = button("homePlaylistsSection")
            const recent = button("homeRecentSection")
            verify(recent.mapToItem(page, 0, 0).y >= lists.mapToItem(page, 0, lists.height).y)
            const scroll = button("homeScroll")
            verify(scroll.contentHeight > scroll.height)
            page.contentY = 200
            page.visible = false
            page.visible = true
            compare(page.contentY, 200)
            const path = testFixtures.screenshotPath("home-compact-scrolled")
            if (path) { waitForRendering(page); grabImage(scene).save(path) }
        }
    }
}
