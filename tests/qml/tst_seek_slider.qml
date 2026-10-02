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

        // Pointer press previews a destination without repeatedly sending seek commands.
        // Only release commits the user's final position to the backend.
        // The signal spy catches duplicate requests from both value changes and release handling.
        // The retained value provides immediate feedback while confirmation is pending.
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

        // Playback notifications can arrive while the user is dragging the handle.
        // They must not move the preview away from the pointer or issue intermediate seeks.
        // The final release should commit the preview once, using the same position shown on screen.
        // Several notifications make the competing update source explicit.
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

        // A stationary press is still an active interaction, not a return to playback tracking.
        // Holding through a playback update must preserve the point selected on press.
        // Release must commit that point even when no drag movement occurred.
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

        // An authoritative update near the requested position acknowledges the pending seek.
        // Subsequent playback positions should then drive the handle normally.
        // Following those updates must not emit another user seek request.
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

        // Queued pre-seek notifications may be delivered after the request was sent.
        // The preview must survive these old positions until the backend reaches the target.
        // A nearby acknowledgment releases the guard and later positions resume normal tracking.
        // This prevents a visible jump back immediately after a user seeks.
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

        // A backend may fail to acknowledge the requested position.
        // The optimistic preview therefore has a bounded lifetime.
        // After that interval, the last known playback position must become visible again.
        // Recovery is passive and must not resend the failed seek.
        function test_unconfirmedSeekEventuallyResumesPlayback() {
            mouseClick(slider, xAt(0.75), slider.height / 2)
            slider.playbackPosition = 13000
            comparePosition(slider.value, 90000)
            tryCompare(slider, "value", 13000, 1500)
            slider.playbackPosition = 14000
            compare(slider.value, 14000)
            compare(seekSpy.count, 1)
        }

        // A pending acknowledgment must not lock the slider against another user action.
        // The second drag owns its preview even while old playback updates continue arriving.
        // Its release produces a new request and replaces the previous pending target.
        // Only confirmation of the new target should restore playback tracking.
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

        // Clicks beyond the usable handle travel clamp to the full media range.
        // Both zero and exact duration must remain reachable despite control padding.
        // The test uses outer control coordinates rather than the idealized fraction helper.
        function test_endpoints(data) {
            const targetX = data.end ? slider.width - 1 : 1
            mouseClick(slider, targetX, slider.height / 2)
            compare(seekSpy.count, 1)
            compare(seekSpy.signalArguments[0][0], data.expected)
            compare(slider.value, data.expected)
        }

        // Keyboard changes have no pointer-release event to commit them.
        // Each directional key must therefore issue exactly one seek immediately.
        // The step is expressed in media milliseconds rather than screen pixels.
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

        // Wheel interaction changes value without entering the pressed state.
        // It must use the same one-request contract as keyboard interaction.
        // The preview must match the emitted destination rather than awaiting a nonexistent release.
        function test_wheelSeeksWithoutMousePress() {
            slider.wheelEnabled = true
            slider.stepSize = 1000
            mouseWheel(slider, slider.width / 2, slider.height / 2, 0, 120)
            verify(!slider.pressed)
            compare(seekSpy.count, 1)
            verify(seekSpy.signalArguments[0][0] > 12000)
            compare(slider.value, seekSpy.signalArguments[0][0])
        }

        // A new duration can indicate a track change while an old seek is still pending.
        // The previous target is no longer valid in that media range.
        // Resetting the guard lets the new track's playback position control the slider immediately.
        function test_durationChangeResetsUnconfirmedSeek() {
            mouseClick(slider, xAt(0.75), slider.height / 2)
            slider.playbackPosition = 0
            slider.duration = 60000
            compare(slider.value, 0)
            slider.playbackPosition = 1000
            compare(slider.value, 1000)
            compare(seekSpy.count, 1)
        }

        // Unknown duration provides no meaningful seek range.
        // The disabled control must also suppress requests, not merely change its appearance.
        function test_noDurationDisablesSeeking() {
            slider.duration = 0
            verify(!slider.enabled)
            mouseClick(slider, slider.width * 0.75, slider.height / 2)
            compare(seekSpy.count, 0)
        }
    }
}
