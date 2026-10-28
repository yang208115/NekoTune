pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/shell" as Shell

Item {
    id: scene
    width: 640
    height: 480

    QtObject {
        id: fakeShell
        property int created: 0
        property int destroyed: 0
    }
    QtObject {
        id: fakeControllers
        property var extensions: null
        property var theme: null
    }
    Component {
        id: hostComponent
        Shell.PageHost {
            anchors.fill: parent
            visible: false
            modelData: ({id: "fixture", source: "data:text/plain," + encodeURIComponent(
                "import QtQuick; Item { required property var shell; required property var controllers; "
                + "required property var transport; required property var translator; "
                + "property string draft: ''; property real contentY: 0; "
                + "Component.onCompleted: shell.created++; Component.onDestruction: shell.destroyed++ }")})
            shell: fakeShell
            controllers: fakeControllers
            transport: null
            translator: testTranslator
        }
    }
    TestCase {
        name: "PageHost"
        when: windowShown
        property var host: null

        function init() {
            fakeShell.created = 0
            fakeShell.destroyed = 0
            host = createTemporaryObject(hostComponent, scene)
            verify(host !== null)
        }
        function test_firstVisitRetainsInstanceAndLocalState() {
            compare(host.item, null)
            compare(host.status, Loader.Null)
            compare(fakeShell.created, 0)
            host.visible = true
            tryCompare(host, "status", Loader.Ready)
            const page = host.item
            page.draft = "Unsaved input"
            page.contentY = 120
            host.visible = false
            compare(host.item, page)
            compare(fakeShell.destroyed, 0)
            host.visible = true
            compare(host.item, page)
            compare(page.draft, "Unsaved input")
            compare(page.contentY, 120)
            compare(fakeShell.created, 1)
        }
        function test_hiddenActionLoadsOnceWithoutNavigation() {
            host.activate()
            tryCompare(host, "status", Loader.Ready)
            const page = host.item
            compare(host.visible, false)
            host.activate()
            host.visible = true
            compare(host.item, page)
            compare(fakeShell.created, 1)
        }
        function test_debugGateBlocksActivationUntilAllowed() {
            host.active = false
            host.visible = true
            host.activate()
            compare(host.item, null)
            compare(fakeShell.created, 0)
            host.visible = false
            host.active = true
            compare(host.item, null)
            host.visible = true
            tryCompare(host, "status", Loader.Ready)
            compare(fakeShell.created, 1)
            host.active = false
            tryCompare(host, "item", null)
            tryCompare(fakeShell, "destroyed", 1)
        }
    }
}
