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
        name: "AsrLyrics"
        when: windowShown
        property var lyrics: null
        property var timedLines: [
            {time_ms: 560, end_time_ms: 1440, text: "いつか。", words: [
                {time_ms: 560, end_time_ms: 920, start: 0, length: 2},
                {time_ms: 1000, end_time_ms: 1440, start: 2, length: 2}]},
            {time_ms: 2000, text: "Legacy LRC"}
        ]

        SignalSpy { id: seekSpy; target: parent.lyrics; signalName: "seekRequested" }

        function init() {
            lyrics = createTemporaryObject(lyricsComponent, scene, {lines: timedLines})
            verify(lyrics !== null)
            wait(180)
            seekSpy.clear()
        }

        function cleanup() { lyrics = null }

        function test_wordBoundariesAndSeekBack() {
            compare(lyrics.activeIndex, -1)
            lyrics.position = 560
            compare(lyrics.activeIndex, 0)
            const text = findChild(lyrics, "lyricText0")
            verify(text !== null)
            compare(text.textFormat, Text.RichText)
            verify(text.text.includes('<font color="#cbb8ff">いつ</font>'))
            lyrics.position = 920 // Word gap: no currently sung word.
            verify(!text.text.includes("#cbb8ff"))
            lyrics.position = 1000
            verify(text.text.includes('<font color="#cbb8ff">か。</font>'))
            lyrics.position = 1440
            verify(!text.text.includes("#cbb8ff"))
            lyrics.position = 2000
            compare(lyrics.activeIndex, 1)
            const legacy = findChild(lyrics, "lyricText1")
            verify(legacy !== null)
            compare(legacy.textFormat, Text.PlainText)
            lyrics.position = 600
            compare(lyrics.activeIndex, 0)
            verify(text.text.includes('<font color="#cbb8ff">いつ</font>'))
            lyrics.position = 0
            compare(lyrics.activeIndex, -1)
            compare(text.textFormat, Text.PlainText)
        }

        function test_clickSeeksToSentence() {
            lyrics.position = 560
            wait(180)
            const row = findChild(lyrics, "lyricLine0")
            verify(row !== null)
            mouseClick(row, row.width / 2, row.height / 2)
            compare(seekSpy.count, 1)
            compare(seekSpy.signalArguments[0][0], 560)
        }

        function test_escapeRecognizedMarkup() {
            const text = lyrics.highlightedText({text: '<img src="x"> &', words: [
                {time_ms: 0, end_time_ms: 100, start: 0, length: 13}]}, 50)
            verify(!text.includes("<img"))
            verify(text.includes("&lt;img"))
            verify(text.includes("&amp;"))
        }
    }
}
