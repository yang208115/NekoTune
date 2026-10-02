import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as PlayerComponents

Item {
    id: scene
    width: 800
    height: 520

    QtObject {
        id: fakeTranslator
        property string language: "en"
        function text(key, language) { return key }
    }

    QtObject {
        id: fakeClient
        property bool connected: true
        property var status: ({kugou: {configured: false, key_saved: false, busy: false}})
        property string lastKey: ""
        property int clears: 0
        signal requestSucceeded(string method)
        signal requestFailed(string method, string reason)
        function kugouSaveKey(key) { lastKey = key }
        function kugouClearKey() { clears++ }
    }

    Component {
        id: panelComponent
        PlayerComponents.SettingsPanel {
            width: scene.width
            height: scene.height
            client: fakeClient
            account: fakeClient.status.kugou
            connected: fakeClient.connected
            translator: fakeTranslator
        }
    }

    TestCase {
        id: testCase
        name: "Settings"
        when: windowShown
        property var panel: null

        function init() {
            fakeClient.status = {kugou: {configured: false, key_saved: false, busy: false}}
            fakeClient.lastKey = ""
            fakeClient.clears = 0
            panel = createTemporaryObject(panelComponent, scene)
            verify(panel !== null)
        }

        function cleanup() { panel = null }

        // The password field holds only newly entered secret text and clears immediately on submit.
        // Saved status arrives from the backend rather than revealing or refilling the key.
        // The pending state lasts through save/clear confirmation and guards duplicate submissions.
        // An empty field cannot accidentally request replacement with an empty secret.
        function test_saveAndClearKey() {
            const field = findChild(panel, "kugouKeyField")
            const save = findChild(panel, "kugouSaveKeyButton")
            const clear = findChild(panel, "kugouClearKeyButton")
            verify(field !== null && save !== null && clear !== null)
            compare(field.echoMode, TextInput.Password)
            compare(save.enabled, false)
            field.text = " test-secret "
            waitForRendering(panel)
            mouseClick(save)
            compare(fakeClient.lastKey, "test-secret")
            compare(field.text, "")
            compare(panel.saving, true)
            fakeClient.status = {kugou: {configured: true, key_saved: true, busy: false}}
            fakeClient.requestSucceeded("kugou.save_key")
            compare(panel.saving, false)
            compare(clear.visible, true)
            mouseClick(clear)
            compare(fakeClient.clears, 1)
            compare(panel.saving, true)
            fakeClient.status = {kugou: {configured: false, key_saved: false, busy: false}}
            fakeClient.requestSucceeded("kugou.clear_key")
            compare(clear.visible, false)
        }
    }
}
