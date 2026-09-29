import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as Components

Item {
    id: scene
    width: 960
    height: 620
    QtObject {
        id: i18n
        property string language: "en"
        function text(key, language) { return key.replace(/_/g, " ") }
    }
    Component { id: panelComponent; Components.LyricsDebugPanel { translator: i18n; width: 920; height: 600 } }
    TestCase {
        id: tests
        name: "LyricsDebugComparison"
        when: windowShown
        property var panel: null
        property var asrLines: [
            {time_ms: 560, end_time_ms: 1440, text: "ASR first"},
            {time_ms: 2000, end_time_ms: 3000, text: "ASR second <plain text>"}
        ]
        property var lrcLines: [
            {time_ms: 1000, text: "LRC first"},
            {time_ms: 2500, text: "LRC second"}
        ]
        SignalSpy { id: seekSpy; target: tests.panel; signalName: "seekRequested" }
        function init() {
            panel = createTemporaryObject(panelComponent, scene, {
                lyrics: {document: {source: "local", lines: lrcLines}, comparison: {
                    asr: {source: "aliyun_asr", lines: asrLines},
                    lrc: {source: "local", lines: lrcLines}
                }}
            })
            verify(panel !== null)
            seekSpy.clear()
        }
        function cleanup() { panel = null }
        function test_modesAndIndependentTiming() {
            compare(panel.comparisonMode, false)
            mouseClick(findChild(panel, "comparisonMode"))
            compare(panel.comparisonMode, true)
            const asr = findChild(panel, "asrTimeline")
            const lrc = findChild(panel, "lrcTimeline")
            verify(asr.visible && lrc.visible)
            compare(asr.activeIndex, -1)
            compare(lrc.activeIndex, -1)
            panel.position = 560
            compare(asr.activeIndex, 0)
            compare(lrc.activeIndex, -1)
            panel.position = 2200
            compare(asr.activeIndex, 1)
            compare(lrc.activeIndex, 0)
            panel.position = 2500
            compare(lrc.activeIndex, 1)
            panel.position = 500
            compare(asr.activeIndex, -1)
            compare(lrc.activeIndex, -1)
            mouseClick(findChild(panel, "timelineMode"))
            compare(panel.comparisonMode, false)
            verify(!asr.visible && !lrc.visible)
        }
        function test_seekAndClearOnTrackChange() {
            panel.comparisonMode = true
            wait(100)
            const asr = findChild(panel, "asrTimeline")
            const lrc = findChild(panel, "lrcTimeline")
            mouseClick(findChild(asr, "debugLine0"))
            compare(seekSpy.count, 1)
            compare(seekSpy.signalArguments[0][0], 560)
            mouseClick(findChild(lrc, "debugLine1"))
            compare(seekSpy.count, 2)
            compare(seekSpy.signalArguments[1][0], 2500)
            panel.lyrics = {state: "loading", track_id: "next"}
            compare(asr.lines.length, 0)
            compare(lrc.lines.length, 0)
            compare(asr.activeIndex, -1)
            compare(lrc.activeIndex, -1)
            panel.lyrics = {comparison: {asr: {lines: asrLines}}}
            compare(asr.lines.length, 2)
            compare(lrc.lines.length, 0)
        }
    }
}
