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
        name: "LyricsDebugTimeline"
        when: windowShown
        property var panel: null
        property var lines: [
            {time_ms: 1000, text: "LRC first"},
            {time_ms: 2500, text: "LRC second"}
        ]
        SignalSpy { id: seekSpy; target: tests.panel; signalName: "seekRequested" }
        function init() {
            panel = createTemporaryObject(panelComponent, scene, {lyrics: {document: {source: "local", lines: lines}}})
            verify(panel !== null)
            seekSpy.clear()
        }
        function cleanup() { panel = null }
        function test_timingAndTrackChange() {
            compare(panel.activeIndex, -1)
            panel.position = 1000
            compare(panel.activeIndex, 0)
            panel.position = 2500
            compare(panel.activeIndex, 1)
            panel.lyrics = {state: "loading", track_id: "next"}
            compare(panel.lines.length, 0)
            compare(panel.activeIndex, -1)
        }
        function test_clickSeeksToLine() {
            wait(100)
            const row = findChild(panel, "debugLine0")
            verify(row !== null)
            mouseClick(row)
            compare(seekSpy.count, 1)
            compare(seekSpy.signalArguments[0][0], 1000)
        }
    }
}
