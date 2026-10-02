import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as Components

Rectangle {
    id: scene
    width: 640
    height: 260
    color: "#0E0D14"
    Components.TrackRow {
        id: track
        x: 16; y: 24; width: scene.width - 32
        song: ({title: "空奏列車", artist: "Orangestar,初音ミク", duration: 253000})
        selected: true
    }
    Components.ArtistNames {
        id: artists
        x: 24; y: 120; width: 560
        fallbackText: "Unknown artist"
    }
    TestCase {
        name: "Artists"
        when: windowShown
        SignalSpy { id: selectionSpy; target: track; signalName: "selectedRequested" }
        SignalSpy { id: playSpy; target: track; signalName: "activated" }
        function init() {
            artists.width = 560
            artists.artist = ""
            artists.summarizeOverflow = false
            artists.remainingTextTemplate = "+%1"
            track.song = {title: "空奏列車", artist: "Orangestar,初音ミク", duration: 253000}
            track.available = true
            track.compact = false
            track.width = scene.width - 32
            selectionSpy.clear()
            playSpy.clear()
        }
        // Artist delegates remain part of the row's selection surface.
        // Clicking a displayed name selects the row without accidentally starting playback.
        // The names remain separate literal labels rather than one joined text string.
        function test_independentNamesAndRowSelection() {
            const names = findChild(track, "trackArtists")
            compare(names.names, ["Orangestar", "初音ミク"])
            const first = findChild(names, "artistName0")
            const second = findChild(names, "artistName1")
            compare(first.text, "Orangestar")
            compare(second.text, "初音ミク")
            waitForRendering(track)
            mouseClick(second)
            compare(selectionSpy.count, 1)
            compare(playSpy.count, 0)
        }
        // Only explicit list delimiters split artist metadata.
        // Slashes, ampersands and spaces may belong to a group's actual name.
        // Case-insensitive duplicates collapse while the first spelling and literal text are retained.
        function test_explicitSeparatorsAndDuplicates_data() {
            return [
                {tag: "comma", input: "Orangestar,初音ミク", expected: ["Orangestar", "初音ミク"]},
                {tag: "mixed", input: " A ， B、 C；D;E\nF", expected: ["A", "B", "C", "D", "E", "F"]},
                {tag: "empty and duplicate", input: " A , a ,, B, ", expected: ["A", "B"]},
                {tag: "group", input: "AC/DC, Guns N' Roses, Earth Wind & Fire", expected: ["AC/DC", "Guns N' Roses", "Earth Wind & Fire"]},
                {tag: "no delimiter", input: "初音 ミク", expected: ["初音 ミク"]},
                {tag: "literal text", input: "<b>A</b>, B", expected: ["<b>A</b>", "B"]}
            ]
        }
        function test_explicitSeparatorsAndDuplicates(data) {
            artists.artist = data.input
            compare(artists.names, data.expected)
        }
        // Long artist names share the available row width instead of overlapping one another.
        // Both may elide, but each must keep positive width on the same baseline.
        // The geometry assertions catch overflow that a text-value comparison cannot detect.
        function test_longNamesStayInsideSingleLine() {
            artists.width = 120
            artists.artist = "A very long name for the first artist, Another very long second artist"
            waitForRendering(artists)
            const first = findChild(artists, "artistName0")
            const second = findChild(artists, "artistName1")
            verify(first.truncated)
            verify(second.truncated)
            verify(first.width > 0 && second.width > 0)
            compare(first.mapToItem(artists, 0, 0).y, second.mapToItem(artists, 0, 0).y)
            verify(first.mapToItem(artists, first.width, 0).x <= second.mapToItem(artists, 0, 0).x)
            verify(second.mapToItem(artists, second.width, 0).x <= artists.width)
        }
        // Changing metadata must replace the derived names instead of appending to old delegates.
        // Compact mode reserves room for an overflow count; full mode exposes all names.
        // Unavailable audio replaces artist metadata with the file-availability explanation.
        function test_metadataSwitchAndUnavailableFallback() {
            artists.artist = "A, B"
            compare(artists.names.length, 2)
            artists.artist = "C"
            compare(artists.names, ["C"])
            artists.artist = " , ; "
            compare(artists.names.length, 0)
            compare(artists.Accessible.name, artists.fallbackText)
            track.compact = true
            track.width = 328
            track.song = {title: "群青讃歌", artist: "初音ミク、星乃一歌、小豆沢こはね、天馬司、宵崎奏、花里みのり"}
            waitForRendering(track)
            const compactArtists = findChild(track, "trackArtists")
            verify(compactArtists.remainingCount > 0)
            verify(!findChild(compactArtists, "artistSummary").truncated)
            track.compact = false
            waitForRendering(track)
            compare(compactArtists.remainingCount, 0)
            track.available = false
            const names = findChild(track, "trackArtists")
            compare(names.names.length, 0)
            compare(names.fallbackText, i18n.text("file_unavailable", i18n.language))
        }

        // Summary fitting reserves space for the localized remaining-artist count.
        // Growing the width should reveal more complete names before hiding the count entirely.
        // An individually long name may elide while the count remains within bounds.
        // Accessible text retains the full list even when the visual summary is shortened.
        function test_summaryFitsNamesAndCountsAcrossWidths() {
            artists.summarizeOverflow = true
            artists.remainingTextTemplate = "+%1位"
            artists.artist = "初音ミク、星乃一歌、小豆沢こはね、天馬司、宵崎奏、花里みのり"
            artists.width = 120
            waitForRendering(artists)
            const summary = findChild(artists, "artistSummary")
            const remaining = findChild(artists, "artistRemainingCount")
            compare(summary.text, "初音ミク")
            compare(remaining.text, "+5位")
            verify(!summary.truncated)
            verify(remaining.mapToItem(artists, remaining.width, 0).x <= artists.width)
            compare(artists.Accessible.name, artists.names.join(", "))
            artists.width = 200
            waitForRendering(artists)
            compare(summary.text, "初音ミク · 星乃一歌")
            compare(remaining.text, "+4位")
            verify(!summary.truncated)
            artists.width = 560
            waitForRendering(artists)
            compare(summary.text, artists.names.join(" · "))
            verify(!remaining.visible)
            verify(!summary.truncated)
            artists.width = 120
            artists.artist = "A very long name for the first artist, Second artist"
            waitForRendering(artists)
            verify(summary.truncated)
            compare(remaining.text, "+1位")
            verify(remaining.mapToItem(artists, remaining.width, 0).x <= artists.width)
            artists.artist = ""
            waitForRendering(artists)
            verify(!summary.visible)
            verify(!remaining.visible)
            compare(artists.Accessible.name, artists.fallbackText)
        }
        function test_renderPreview() {
            artists.artist = "AC/DC, Guns N' Roses"
            const path = testFixtures.screenshotPath("artist-names")
            if (path) {
                waitForRendering(scene)
                grabImage(scene).save(path)
            }
        }
    }
}
