import QtQuick
import QtQuick.Controls
import QtTest
import "../../frontend/qt-qml/qml/components" as Components

Rectangle {
    id: scene
    width: 640
    height: 480
    color: "#0E0D14"

    Rectangle {
        id: row
        x: 24; y: anchor.y - 14
        width: scene.width - 48; height: 64; radius: 8
        color: "#322743"
        Label { x: 24; anchors.verticalCenter: parent.verticalCenter; text: "夜樱电台"; color: "#F5F1FA" }
    }
    Components.IconButton {
        id: anchor
        x: 572; y: 80; width: 32; height: 36
        kind: "more"
        fillColor: "#2A2338"
        onClicked: menu.openAt(anchor)
    }
    Components.ActionMenu {
        id: menu
        actions: [
            {key: "play", label: "播放"},
            {key: "queue", label: "加入当前队列"},
            {key: "playlist", label: "加入歌单"}
        ]
    }
    TestCase {
        name: "ActionMenu"
        when: windowShown
        SignalSpy { id: chosenSpy; target: menu; signalName: "chosen" }
        function init() {
            waitForRendering(scene)
            scene.width = menu.parent.width
            scene.height = menu.parent.height
            anchor.x = scene.width - 68
            anchor.y = 80
            chosenSpy.clear()
            waitForRendering(scene)
        }
        function cleanup() { menu.close(); tryCompare(menu, "visible", false) }
        function showMenu() { mouseClick(anchor); tryCompare(menu, "opened", true); waitForRendering(menu.contentItem) }
        function screenshot(name) {
            const path = testFixtures.screenshotPath(name)
            if (path) {
                waitForRendering(menu.contentItem)
                grabImage(menu.parent.parent).save(path)
            }
        }
        function test_buttonAlignsAndActionWorks() {
            showMenu()
            const bottomRight = anchor.mapToItem(menu.parent, anchor.width, anchor.height)
            compare(menu.x + menu.width, bottomRight.x)
            compare(menu.y, bottomRight.y + 6)
            screenshot("action-menu-below")
            mouseClick(findChild(menu.contentItem, "menu_queue"))
            compare(chosenSpy.count, 1)
            compare(chosenSpy.signalArguments[0][0], "queue")
            tryCompare(menu, "visible", false)
        }
        function test_bottomButtonOpensAbove() {
            anchor.y = scene.height - 68
            showMenu()
            const topRight = anchor.mapToItem(menu.parent, anchor.width, 0)
            compare(menu.x + menu.width, topRight.x)
            compare(menu.y + menu.height, topRight.y - 6)
            screenshot("action-menu-above")
            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false)
        }
        function test_leftButtonStaysInWindow() {
            anchor.x = 12
            showMenu()
            compare(menu.x, 8)
            verify(menu.x + menu.width <= menu.parent.width - 8)
        }
        function test_pointerUsesClickPosition() {
            menu.openAt(scene, 180, 160)
            tryCompare(menu, "opened", true)
            const point = scene.mapToItem(menu.parent, 180, 160)
            compare(menu.x, point.x)
            compare(menu.y, point.y)
        }
        function test_bottomRightPointerOpensAwayFromEdges() {
            menu.openAt(scene, scene.width - 20, scene.height - 20)
            tryCompare(menu, "opened", true)
            const point = scene.mapToItem(menu.parent, scene.width - 20, scene.height - 20)
            compare(menu.x + menu.width, point.x)
            compare(menu.y + menu.height, point.y)
            verify(menu.x >= 8 && menu.y >= 8)
        }
    }
}
