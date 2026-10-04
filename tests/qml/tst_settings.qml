import "../../extensions/builtin/kugou/qml" as KugouPlugin
import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as PlayerComponents

Rectangle {
    id: scene
    width: 800
    height: 900
    color: PlayerComponents.Theme.bgCanvas

    QtObject {
        id: fakeTranslator
        property string language: "en"
        function text(key, language) { return testTranslator.text(key, language) }
    }

    QtObject {
        id: fakeClient
        property bool connected: true
        property var status: ({kugou: {configured: false, key_saved: false, busy: false}})
        property string lastKey: ""
        property int clears: 0
        property var lastConfiguration: ({})
        function kugouSaveConfiguration(enabled, url) { lastConfiguration = {enabled: enabled, worker_url: url} }
        signal requestSucceeded(string method)
        signal requestFailed(string method, string reason)
        function kugouSaveKey(key) { lastKey = key }
        function kugouClearKey() { clears++ }
    }

    Component {
        id: panelComponent
        KugouPlugin.SettingsPanel {
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
            fakeClient.lastConfiguration = ({})
            panel = createTemporaryObject(panelComponent, scene)
            verify(panel !== null)
        }

        function cleanup() { panel = null }

        function test_configurationDefaultsSaveAndFailure() {
            const toggle = findChild(panel, "kugouEnabledSwitch")
            const url = findChild(panel, "kugouWorkerUrlField")
            const save = findChild(panel, "kugouSaveConfigButton")
            compare(toggle.checked, false)
            compare(url.text, "")
            verify(!save.enabled)
            mouseClick(toggle)
            verify(!save.enabled)
            url.text = " https://worker.example "
            verify(save.enabled)
            mouseClick(save)
            compare(fakeClient.lastConfiguration.enabled, true)
            compare(fakeClient.lastConfiguration.worker_url, "https://worker.example")
            verify(panel.saving)
            fakeClient.requestFailed("kugou.config.set", "Cannot save")
            verify(!panel.saving)
            verify(panel.failed)
            compare(toggle.checked, true)
            compare(url.text, " https://worker.example ")
            mouseClick(save)
            fakeClient.status = {kugou: {enabled: true, worker_url: "https://worker.example", busy: false}}
            fakeClient.requestSucceeded("kugou.config.set")
            verify(!panel.saving)
            verify(!panel.configDirty)
            compare(url.text, "https://worker.example")
            url.text = "https://new.example"
            fakeClient.status = {kugou: {enabled: true, worker_url: "https://worker.example", key_saved: true, busy: false}}
            compare(url.text, "https://new.example")
            mouseClick(toggle)
            mouseClick(save)
            compare(fakeClient.lastConfiguration.enabled, false)
        }

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
            for (const language of ["zh", "en"]) {
                fakeTranslator.language = language
                const screenshot = testFixtures.screenshotPath("kugou-settings-" + language)
                if (screenshot) {
                    waitForRendering(panel)
                    grabImage(scene).save(screenshot)
                }
            }
        }
    }
}
