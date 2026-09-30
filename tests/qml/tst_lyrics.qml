import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as PlayerComponents

Item {
    id: scene
    width: 500
    height: 480

    Component {
        id: lyricsComponent
        PlayerComponents.LrcLyrics { width: 460; height: 440 }
    }

    TestCase {
        name: "LrcLyrics"
        when: windowShown
        property var lyrics: null
        property var timedLines: [
            {time_ms: 560, text: "いつか。"},
            {time_ms: 2000, text: "<plain text> &"}
        ]

        SignalSpy { id: seekSpy; target: parent.lyrics; signalName: "seekRequested" }

        function init() {
            lyrics = createTemporaryObject(lyricsComponent, scene, {lines: timedLines})
            verify(lyrics !== null)
            wait(180)
            seekSpy.clear()
        }

        function cleanup() { lyrics = null }

        function test_lineTimingAndPlainText() {
            compare(lyrics.activeIndex, -1)
            lyrics.position = 560
            compare(lyrics.activeIndex, 0)
            const first = findChild(lyrics, "lyricText0")
            verify(first !== null)
            compare(first.textFormat, Text.PlainText)
            compare(first.text, "いつか。")
            lyrics.position = 2000
            compare(lyrics.activeIndex, 1)
            const second = findChild(lyrics, "lyricText1")
            compare(second.text, "<plain text> &")
            compare(second.textFormat, Text.PlainText)
            lyrics.position = 0
            compare(lyrics.activeIndex, -1)
        }

        function test_clickSeeksToLine() {
            lyrics.position = 560
            wait(180)
            const row = findChild(lyrics, "lyricLine0")
            verify(row !== null)
            mouseClick(row, row.width / 2, row.height / 2)
            compare(seekSpy.count, 1)
            compare(seekSpy.signalArguments[0][0], 560)
        }
    }
}
