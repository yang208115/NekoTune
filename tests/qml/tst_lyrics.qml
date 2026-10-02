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

        function test_krcWordTimingAndEscaping() {
            lyrics.lines = [{time_ms: 1000, duration_ms: 1000, text: "<a&b> c", words: [
                {text: "<a&b> ", offset_ms: 0, time_ms: 1000, duration_ms: 300},
                {text: "c", offset_ms: 500, time_ms: 1500, duration_ms: 300}
            ]}]
            lyrics.position = 1100
            compare(lyrics.activeIndex, 0)
            const row = findChild(lyrics, "lyricLine0")
            const first = findChild(lyrics, "krcWord0_0")
            const second = findChild(lyrics, "krcWord0_1")
            const firstReveal = findChild(lyrics, "krcReveal0_0")
            const secondReveal = findChild(lyrics, "krcReveal0_1")
            const base = findChild(lyrics, "krcBaseText0_0")
            verify(row !== null && first !== null && second !== null)
            verify(firstReveal !== null && secondReveal !== null && base !== null)
            compare(base.text, "<a&b> ")
            compare(base.textFormat, Text.PlainText)
            compare(row.activeWordIndex, 0)
            compare(first.progress, 1 / 3)
            compare(second.progress, 0)
            verify(Math.abs(firstReveal.width - first.width / 3) < 0.1)
            const stableHeight = row.height
            lyrics.position = 1150
            compare(first.progress, 0.5)
            verify(Math.abs(firstReveal.width - first.width / 2) < 0.1)
            compare(row.height, stableHeight)
            lyrics.position = 1400
            compare(row.activeWordIndex, -1)
            compare(firstReveal.width, first.width)
            lyrics.position = 1600
            compare(row.activeWordIndex, 1)
            compare(second.progress, 1 / 3)
            verify(Math.abs(secondReveal.width - second.width / 3) < 0.1)
            lyrics.lines = timedLines
            lyrics.position = 2000
            compare(findChild(lyrics, "lyricText1").textFormat, Text.PlainText)
        }

        function test_krcShortLineStaysTogether_data() {
            return [
                {tag: "Japanese20px", width: 460, parts: ["正", "直", "に", "言", "っ", "ち", "ゃ", "え", "ば　"]},
                {tag: "Japanese24px", width: 600, parts: ["正", "直", "に", "言", "っ", "ち", "ゃ", "え", "ば　"]},
                {tag: "LatinKerning", width: 460, parts: ["A", "V", "A", "T", "A", "R   "]},
                {tag: "MixedWords", width: 600, parts: ["Hello ", "世", "界　"]}
            ]
        }

        function test_krcShortLineStaysTogether(data) {
            lyrics.width = data.width
            lyrics.lines = [{time_ms: 1000, text: data.parts.join(""), words: data.parts.map(
                (text, index) => ({text: text, time_ms: 1000 + index * 100, duration_ms: 100}))}]
            wait(50)
            const flow = findChild(lyrics, "krcLine0")
            verify(flow !== null)
            let totalWidth = 0
            for (let index = 0; index < data.parts.length; index += 1) {
                const word = findChild(lyrics, "krcWord0_" + index)
                verify(word !== null)
                compare(word.y, 0)
                totalWidth += word.width
            }
            verify(flow.width >= totalWidth)
            verify(Math.abs(flow.x - (lyrics.width - flow.width) / 2) < 0.1)
            compare(findChild(lyrics, "krcBaseText0_" + (data.parts.length - 1)).text,
                    data.parts[data.parts.length - 1])
        }

        function test_krcLineWrapsAndReflowsOnResize() {
            const parts = ["正", "直", "に", "言", "っ", "ち", "ゃ", "え", "ば　"]
            lyrics.lines = [{time_ms: 1000, text: parts.join(""), words: parts.map(
                (text, index) => ({text: text, time_ms: 1000 + index * 100, duration_ms: 100}))}]
            lyrics.width = 150
            wait(50)
            verify(findChild(lyrics, "krcWord0_8").y > 0)
            compare(findChild(lyrics, "krcLine0").width, lyrics.width - 32)
            lyrics.width = 600
            tryCompare(findChild(lyrics, "krcWord0_8"), "y", 0)
            lyrics.width = 460
            tryCompare(findChild(lyrics, "krcWord0_8"), "y", 0)
            lyrics.lines = [{time_ms: 1000, text: "短句", words: [
                {text: "短", time_ms: 1000, duration_ms: 100},
                {text: "句", time_ms: 1100, duration_ms: 100}
            ]}]
            wait(50)
            compare(findChild(lyrics, "krcWord0_1").y, 0)
            verify(findChild(lyrics, "krcLine0").width < 100)
        }

        function test_krcProgressFollowsPlaybackAndSeek() {
            lyrics.lines = [{time_ms: 1000, duration_ms: 1000, text: "Hello", words: [
                {text: "Hello", time_ms: 1000, duration_ms: 1000}
            ]}]
            lyrics.position = 1100
            lyrics.playing = true
            const word = findChild(lyrics, "krcWord0_0")
            verify(word !== null)
            const start = word.progress
            wait(80)
            verify(word.progress > start)
            lyrics.playing = false
            compare(word.progress, 0.1)
            wait(60)
            compare(word.progress, 0.1)
            lyrics.position = 1600
            compare(word.progress, 0.6)
            lyrics.position = 1020
            compare(word.progress, 0.02)
        }

        function test_adjacentLineScrollsContinuously() {
            const rows = []
            for (let index = 0; index < 14; index += 1)
                rows.push({time_ms: index * 1000, text: "Line " + index})
            lyrics.lines = rows
            const list = findChild(lyrics, "lyricsList")
            verify(list !== null)
            lyrics.position = 5000
            compare(lyrics.activeIndex, 5)
            wait(40)
            const before = list.contentY
            const next = list.itemAtIndex(6)
            verify(next !== null)
            const target = Math.max(0, Math.min(next.y + next.height / 2 - list.height * 0.42,
                                                 Math.max(0, list.contentHeight - list.height)))
            verify(target > before + 10)
            lyrics.position = 6000
            compare(lyrics.activeIndex, 6)
            wait(160)
            verify(list.contentY > before + 1)
            verify(list.contentY < target - 1)
            wait(360)
            verify(Math.abs(list.contentY - target) < 2)
            compare(lyrics.userScrolling, false)
        }
    }
}
