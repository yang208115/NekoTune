import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as PlayerComponents

Item {
    id: scene
    width: 640
    height: 160

    Component {
        id: seekSliderComponent
        PlayerComponents.SeekSlider {
            x: 20
            y: 20
            width: 400
            height: 40
        }
    }

    TestCase {
        id: testCase
        name: "SeekSlider"
        when: windowShown

        property var slider: null

        SignalSpy {
            id: seekSpy
            target: testCase.slider
            signalName: "seekRequested"
        }

        function init() {
            slider = createTemporaryObject(seekSliderComponent, scene, {
                duration: 120000,
                playbackPosition: 12000
            })
            verify(slider !== null)
            mouseMove(scene, 600, 120)
            wait(120)
            seekSpy.clear()
        }

        function cleanup() {
            if (slider && slider.pressed)
                mouseRelease(slider, slider.width / 2, slider.height / 2)
            slider = null
            seekSpy.clear()
        }

        function xAt(fraction) {
            return slider.leftPadding + slider.handle.width / 2
                    + fraction * (slider.availableWidth - slider.handle.width)
        }

        function comparePosition(actual, expected) {
            // At this width one physical pixel is about 320 ms.
            verify(Math.abs(actual - expected) < 700,
                   "Expected position near " + expected + ", got " + actual)
        }

        function test_singleClickSeeksOnceOnRelease() {
            const targetX = xAt(0.75)
            mousePress(slider, targetX, slider.height / 2)
            verify(slider.pressed)
            compare(seekSpy.count, 0)
            mouseRelease(slider, targetX, slider.height / 2)
            compare(seekSpy.count, 1)
            comparePosition(seekSpy.signalArguments[0][0], 90000)
            compare(slider.value, seekSpy.signalArguments[0][0])
        }

        function test_dragIgnoresPlaybackUpdatesUntilRelease() {
            mousePress(slider, xAt(0.1), slider.height / 2)
            wait(120)
            const targetX = xAt(0.7)
            mouseMove(slider, targetX, slider.height / 2)
            const draggedPosition = slider.position
            comparePosition(slider.valueAt(draggedPosition), 84000)

            for (const position of [13000, 15000, 18000]) {
                slider.playbackPosition = position
                wait(20)
                compare(slider.position, draggedPosition)
                compare(seekSpy.count, 0)
            }

            mouseRelease(slider, targetX, slider.height / 2)
            compare(seekSpy.count, 1)
            comparePosition(seekSpy.signalArguments[0][0], 84000)
            compare(slider.value, seekSpy.signalArguments[0][0])
        }

        function test_pressThenHoldAndReleaseWithoutMoving() {
            const targetX = xAt(0.6)
            mousePress(slider, targetX, slider.height / 2)
            const pressedPosition = slider.position
            slider.playbackPosition = 22000
            wait(180)
            compare(slider.position, pressedPosition)
            compare(seekSpy.count, 0)

            mouseRelease(slider, targetX, slider.height / 2)
            compare(seekSpy.count, 1)
            comparePosition(seekSpy.signalArguments[0][0], 72000)
        }

        function test_playbackUpdatesResumeAfterSeek() {
            mouseClick(slider, xAt(0.75), slider.height / 2)
            compare(seekSpy.count, 1)
            const requestedPosition = seekSpy.signalArguments[0][0]
            slider.playbackPosition = requestedPosition
            compare(slider.value, requestedPosition)
            slider.playbackPosition = 91000
            compare(slider.value, 91000)
            slider.playbackPosition = 92000
            compare(slider.value, 92000)
            compare(seekSpy.count, 1)
        }

        function test_stalePlaybackResponseDoesNotUndoSeek() {
            mouseClick(slider, xAt(0.75), slider.height / 2)
            const requestedPosition = seekSpy.signalArguments[0][0]
            for (const position of [12500, 13000, 13500]) {
                slider.playbackPosition = position
                wait(20)
                compare(slider.value, requestedPosition)
            }
            slider.playbackPosition = requestedPosition + 100
            compare(slider.value, requestedPosition + 100)
            slider.playbackPosition = requestedPosition + 1100
            compare(slider.value, requestedPosition + 1100)
            compare(seekSpy.count, 1)
        }

        function test_unconfirmedSeekEventuallyResumesPlayback() {
            mouseClick(slider, xAt(0.75), slider.height / 2)
            slider.playbackPosition = 13000
            comparePosition(slider.value, 90000)
            tryCompare(slider, "value", 13000, 1500)
            slider.playbackPosition = 14000
            compare(slider.value, 14000)
            compare(seekSpy.count, 1)
        }

        function test_newDragCanReplaceUnconfirmedSeek() {
            mouseClick(slider, xAt(0.75), slider.height / 2)
            mousePress(slider, xAt(0.5), slider.height / 2)
            slider.playbackPosition = 14000
            mouseMove(slider, xAt(0.4), slider.height / 2)
            const targetX = xAt(0.4)
            compare(seekSpy.count, 1)
            mouseRelease(slider, targetX, slider.height / 2)
            compare(seekSpy.count, 2)
            comparePosition(seekSpy.signalArguments[1][0], 48000)
            slider.playbackPosition = seekSpy.signalArguments[1][0]
            compare(slider.value, slider.playbackPosition)
        }

        function test_endpoints_data() {
            return [
                {tag: "start", end: false, expected: 0},
                {tag: "end", end: true, expected: 120000}
            ]
        }

        function test_endpoints(data) {
            const targetX = data.end ? slider.width - 1 : 1
            mouseClick(slider, targetX, slider.height / 2)
            compare(seekSpy.count, 1)
            compare(seekSpy.signalArguments[0][0], data.expected)
            compare(slider.value, data.expected)
        }

        function test_keyboardSeeksOnce() {
            slider.stepSize = 1000
            slider.forceActiveFocus()
            verify(slider.activeFocus)
            keyClick(Qt.Key_Right)
            compare(seekSpy.count, 1)
            compare(seekSpy.signalArguments[0][0], 13000)
            keyClick(Qt.Key_Left)
            compare(seekSpy.count, 2)
            compare(seekSpy.signalArguments[1][0], 12000)
        }

        function test_wheelSeeksWithoutMousePress() {
            slider.wheelEnabled = true
            slider.stepSize = 1000
            mouseWheel(slider, slider.width / 2, slider.height / 2, 0, 120)
            verify(!slider.pressed)
            compare(seekSpy.count, 1)
            verify(seekSpy.signalArguments[0][0] > 12000)
            compare(slider.value, seekSpy.signalArguments[0][0])
        }

        function test_durationChangeResetsUnconfirmedSeek() {
            mouseClick(slider, xAt(0.75), slider.height / 2)
            slider.playbackPosition = 0
            slider.duration = 60000
            compare(slider.value, 0)
            slider.playbackPosition = 1000
            compare(slider.value, 1000)
            compare(seekSpy.count, 1)
        }

        function test_noDurationDisablesSeeking() {
            slider.duration = 0
            verify(!slider.enabled)
            mouseClick(slider, slider.width * 0.75, slider.height / 2)
            compare(seekSpy.count, 0)
        }
    }
}
