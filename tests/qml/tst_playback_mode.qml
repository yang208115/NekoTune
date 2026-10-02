import QtQuick
import QtQuick.Controls
import QtTest
import "../../frontend/qt-qml/qml/components" as Components

Rectangle {
    id: scene
    width: 800
    height: 600
    color: "#0E0D14"
    QtObject {
        id: controller
        property string playbackMode: "sequential"
        property string requestedMode: ""
        signal requestSucceeded(string method)
        signal requestFailed(string method, string message)
        function setPlaybackMode(mode) { requestedMode = mode }
    }
    Components.QueuePanel {
        id: fullPanel
        x: 20; y: 20; width: 360; height: 560
        playbackController: controller
        playbackMode: controller.playbackMode
    }
    Components.QueuePanel {
        id: drawer
        x: 410; y: 20; width: 360; height: 560
        compact: true
        playbackController: controller
        playbackMode: controller.playbackMode
    }
    TestCase {
        name: "PlaybackMode"
        when: windowShown
        function button(panel) { return findChild(panel, "playbackModeButton") }
        function menu(panel) { return findChild(button(panel), "playbackModeMenu") }
        function init() {
            controller.playbackMode = "sequential"
            controller.requestedMode = ""
            fullPanel.connected = true
            drawer.connected = true
            waitForRendering(scene)
        }
        function cleanup() {
            menu(fullPanel).close()
            menu(drawer).close()
            tryCompare(menu(fullPanel), "visible", false)
            tryCompare(menu(drawer), "visible", false)
        }
        function test_menuAndAuthoritativeSelection() {
            const anchor = button(fullPanel)
            const popup = menu(fullPanel)
            verify(anchor.enabled) // Empty queue can choose its future mode.
            mouseClick(anchor)
            tryCompare(popup, "opened", true)
            const modes = ["sequential", "repeat_one", "shuffle", "repeat_all"]
            for (let i = 0; i < modes.length; ++i) {
                const item = findChild(popup.contentItem, "playbackMode_" + modes[i])
                verify(item)
                compare(item.selected, i === 0)
            }
            waitForRendering(popup.contentItem)
            const screenshot = testFixtures.screenshotPath("playback-mode-click")
            if (screenshot) grabImage(popup.parent.parent).save(screenshot)
            mouseClick(findChild(popup.contentItem, "playbackMode_shuffle"))
            compare(controller.requestedMode, "shuffle")
            compare(anchor.kind, "sequential") // Wait for the backend to confirm.
            controller.playbackMode = "shuffle"
            tryCompare(anchor, "kind", "shuffle")
            tryCompare(button(drawer), "kind", "shuffle")
            verify(anchor.tooltipText.indexOf(i18n.text("playback_mode_shuffle", i18n.language)) >= 0)
            mouseClick(button(drawer))
            tryCompare(menu(drawer), "opened", true)
            verify(findChild(menu(drawer).contentItem, "playbackMode_shuffle").selected)
        }
        function test_keyboardAndEscape() {
            const anchor = button(fullPanel)
            const popup = menu(fullPanel)
            anchor.forceActiveFocus()
            keyClick(Qt.Key_Space)
            tryCompare(popup, "opened", true)
            keyClick(Qt.Key_Down)
            compare(popup.currentIndex, 1)
            keyClick(Qt.Key_Return)
            compare(controller.requestedMode, "repeat_one")
            tryCompare(popup, "visible", false)
            keyClick(Qt.Key_Space)
            tryCompare(popup, "opened", true)
            keyClick(Qt.Key_Up)
            compare(popup.currentIndex, 3)
            keyClick(Qt.Key_Escape)
            tryCompare(popup, "visible", false)
            compare(controller.requestedMode, "repeat_one")
        }
        function test_disconnectAndFailure() {
            const anchor = button(drawer)
            const popup = menu(drawer)
            mouseClick(anchor)
            tryCompare(popup, "opened", true)
            drawer.connected = false
            tryCompare(popup, "visible", false)
            verify(!anchor.enabled)
            controller.requestFailed("player.set_playback_mode", "Unable to save playback mode")
            compare(drawer.playbackMode, "sequential")
            compare(drawer.operationError, "Unable to save playback mode")
            controller.requestSucceeded("player.set_playback_mode")
            compare(drawer.operationError, "")
        }
        function test_menuFitsDrawerAndIconsUpdate() {
            for (const mode of ["sequential", "repeat_one", "shuffle", "repeat_all"]) {
                controller.playbackMode = mode
                const anchor = button(drawer)
                tryCompare(anchor, "kind", mode)
                mouseClick(anchor)
                const popup = menu(drawer)
                tryCompare(popup, "opened", true)
                verify(popup.x >= 8 && popup.y >= 8)
                verify(popup.x + popup.width <= popup.parent.width - 8)
                verify(popup.y + popup.height <= popup.parent.height - 8)
                const path = testFixtures.screenshotPath("playback-mode-" + mode)
                if (path) {
                    waitForRendering(popup.contentItem)
                    grabImage(popup.parent.parent).save(path)
                }
                popup.close()
                tryCompare(popup, "visible", false)
            }
        }
    }
}
