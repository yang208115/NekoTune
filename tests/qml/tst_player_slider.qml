import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as PlayerComponents

Item {
    id: scene
    width: 640
    height: 160

    Component {
        id: sliderComponent
        PlayerComponents.PlayerSlider {
            x: 20
            y: 20
            width: 400
            height: 40
            from: 0
            to: 1
            value: 0.2
        }
    }

    TestCase {
        id: testCase
        name: "PlayerSlider"
        when: windowShown

        property var slider: null

        SignalSpy {
            id: movedSpy
            target: testCase.slider
            signalName: "moved"
        }

        function init() {
            slider = createTemporaryObject(sliderComponent, scene)
            verify(slider !== null)
            mouseMove(scene, 600, 120)
            wait(120)
            movedSpy.clear()
        }

        function cleanup() {
            if (slider && slider.pressed)
                mouseRelease(slider, slider.width / 2, slider.height / 2)
            slider = null
            movedSpy.clear()
        }

        function test_volumeChangesContinuouslyWhileDragging() {
            const startX = slider.leftPadding + slider.handle.width / 2
                    + 0.2 * (slider.availableWidth - slider.handle.width)
            mousePress(slider, startX, slider.height / 2)
            movedSpy.clear()
            mouseMove(slider, slider.width * 0.75, slider.height / 2)
            verify(slider.pressed)
            verify(slider.value > 0.7)
            verify(movedSpy.count > 0)
            mouseRelease(slider, slider.width * 0.75, slider.height / 2)
            verify(slider.value > 0.7)
        }

        function test_handleGeometryStaysStableWhileHoveringAndPressed() {
            const originalWidth = slider.handle.width
            const originalX = slider.handle.x
            const centerX = originalX + originalWidth / 2
            mouseMove(slider, centerX, slider.height / 2)
            wait(150)
            compare(slider.handle.width, originalWidth)
            compare(slider.handle.x, originalX)
            mousePress(slider, centerX, slider.height / 2)
            wait(150)
            compare(slider.handle.width, originalWidth)
            mouseRelease(slider, centerX, slider.height / 2)
            wait(150)
            compare(slider.handle.width, originalWidth)
        }
    }
}
