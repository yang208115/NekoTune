import QtQuick
import QtQuick.Controls
import QtTest
import "../../frontend/qt-qml/qml/components" as Components
import "../../frontend/qt-qml/qml/shell" as Shell

Rectangle {
    id: scene
    width: 1000; height: 720; color: "#0E0D14"
    QtObject {
        id: outputController
        property var devices: []
        property var output: ({})
        property bool busy: false
        property string error: ""
        property string requested: "unset"
        property string requestedPort: ""
        property int calls: 0
        function select(id, port) { requested = id; requestedPort = port || ""; ++calls; busy = true; error = "" }
    }
    QtObject {
        id: playlists
        property var model: ({count: 0})
        signal requestSucceeded(string method)
        signal requestFailed(string method, string message)
    }
    QtObject {
        id: transport
        property bool connected: true
    }
    QtObject {
        id: shell
        property int width: scene.width
        property var transport: transport
        property var app: ({audioOutput: outputController, playlists: playlists})
        property var song: ({})
        property var queue: []
        property bool hasSong: false
        property bool isPlaying: false
        property bool nowPlayingOpen: false
        property bool queueOpen: false
        property real volume: .8
        property real duration: 0
        property real position: 0
    }
    Components.SettingsPanel {
        id: settings
        x: 20; y: 16; width: scene.width - 40; height: 580
        client: null; translator: i18n; audioOutput: outputController
        connected: transport.connected
    }
    Shell.BottomPlayer {
        id: bar
        x: 0; y: scene.height - 104; width: scene.width; height: 104
        shell: shell; controller: null; translator: i18n
    }
    TestCase {
        name: "AudioOutput"
        when: windowShown
        function button() { return findChild(bar, "audioOutputButton") }
        function popup() { return findChild(button(), "audioOutputPopup") }
        function settingsPicker() { return findChild(settings, "settingsAudioOutputPicker") }
        function option(picker, index) { return findChild(picker, "audioOutputOption_" + index) }
        function confirm(id) {
            outputController.output = {selected_id: id, selected_name: id ? "USB Headset" : "", active_id: id || "speaker", available: true}
            outputController.busy = false
        }
        function capture(name) {
            waitForRendering(scene)
            const path = testFixtures.screenshotPath(name)
            if (path) grabImage(scene).save(path)
        }
        function init() {
            scene.Window.window.width = 1000
            scene.Window.window.height = 720
            scene.width = 1000
            i18n.language = "zh"
            transport.connected = true
            outputController.devices = [
                {id: "speaker", name: "Built-in Speakers", is_default: true},
                {id: "headset", name: "USB Headset", is_default: false}
            ]
            confirm("")
            outputController.error = ""
            outputController.requested = "unset"
            outputController.requestedPort = ""
            outputController.calls = 0
            waitForRendering(scene)
        }
        function cleanup() {
            popup().close()
            tryCompare(popup(), "visible", false)
        }
        function test_sharedConfirmationBusyAndErrors() {
            const picker = settingsPicker()
            mouseClick(option(picker, 2))
            compare(outputController.requested, "headset")
            verify(option(picker, 0).selected)
            verify(!option(picker, 2).enabled)
            mouseClick(button())
            tryCompare(popup(), "opened", true)
            verify(option(popup().contentItem, 0).selected)
            verify(!option(popup().contentItem, 1).enabled)
            outputController.busy = false
            outputController.error = "audio_output_save_failed"
            verify(findChild(picker, "audioOutputError").visible)
            verify(findChild(popup().contentItem, "audioOutputError").visible)
            verify(option(picker, 0).selected)
            mouseClick(option(popup().contentItem, 2))
            compare(outputController.calls, 2)
            confirm("headset")
            tryVerify(() => option(picker, 2).selected)
            tryVerify(() => option(popup().contentItem, 2).selected)
            capture("audio-output-zh-1000")
        }
        function test_keyboardAndConnectionLoss() {
            button().forceActiveFocus()
            keyClick(Qt.Key_Space)
            tryCompare(popup(), "opened", true)
            keyClick(Qt.Key_Down)
            compare(popup().contentItem.currentIndex, 1)
            keyClick(Qt.Key_Return)
            compare(outputController.requested, "speaker")
            confirm("speaker")
            keyClick(Qt.Key_Escape)
            tryCompare(popup(), "visible", false)
            mouseClick(button())
            tryCompare(popup(), "opened", true)
            transport.connected = false
            tryCompare(popup(), "visible", false)
            verify(!button().enabled)
            verify(!option(settingsPicker(), 0).enabled)
        }
        function test_hotplugAndOfflineSelection() {
            confirm("headset")
            mouseClick(button())
            tryCompare(popup(), "opened", true)
            outputController.devices = [{id: "speaker", name: "Built-in Speakers", is_default: true}]
            outputController.output = {selected_id: "headset", selected_name: "USB Headset", active_id: "", available: false}
            tryCompare(settingsPicker().options, "length", 3)
            tryVerify(() => option(settingsPicker(), 2).selected)
            verify(!option(settingsPicker(), 2).enabled)
            verify(!option(popup().contentItem, 2).enabled)
            verify(findChild(settingsPicker(), "audioOutputStatus").text.indexOf("断开") >= 0)
            capture("audio-output-disconnected")
            outputController.devices = [
                {id: "speaker", name: "Built-in Speakers", is_default: true},
                {id: "headset", name: "USB Headset", is_default: false}
            ]
            confirm("headset")
            tryVerify(() => option(popup().contentItem, 2).enabled)
            compare(outputController.calls, 0)
        }
        function test_longNamesAndResponsiveLayout_data() {
            return [{tag: "narrow", width: 1000}, {tag: "wide", width: 1440}]
        }
        function test_longNamesAndResponsiveLayout(data) {
            scene.Window.window.width = data.width
            scene.width = data.width
            i18n.language = "en"
            outputController.devices = [
                {id: "speaker", name: "Built-in Speakers with a very long descriptive hardware name", is_default: true},
                {id: "headset", name: "USB Headset with a very long descriptive hardware name", is_default: false}
            ]
            waitForRendering(scene)
            tryCompare(popup().parent, "width", data.width)
            mouseClick(button())
            tryCompare(popup(), "opened", true)
            const controls = findChild(bar, "playerSecondaryControls")
            const volume = findChild(bar, "volumeSlider")
            const transportControls = findChild(bar, "transportControls")
            verify(controls.x >= transportControls.x + transportControls.width)
            verify(volume.width >= 72)
            verify(button().mapToItem(bar, button().width, 0).x <= bar.width)
            verify(popup().x >= 0 && popup().x + popup().width <= scene.width)
            verify(popup().y >= 0 && popup().y + popup().height <= scene.height)
            verify(popup().y + popup().height <= button().mapToItem(popup().parent, 0, 0).y)
            capture("audio-output-en-" + data.width)
        }
        function test_emptyDevicesAndManyDevices() {
            outputController.devices = []
            outputController.output = {selected_id: "", active_id: "", available: false}
            compare(settingsPicker().options.length, 1)
            verify(option(settingsPicker(), 0).enabled)
            const devices = []
            for (let i = 0; i < 30; ++i)
                devices.push({id: "device" + i, name: "Audio device " + i, is_default: i === 0})
            mouseClick(button())
            tryCompare(popup(), "opened", true)
            outputController.devices = devices
            waitForRendering(scene)
            verify(popup().height < scene.height)
            verify(popup().y + popup().height <= scene.height)
            keyClick(Qt.Key_Up)
            compare(popup().contentItem.currentIndex, 30)
            keyClick(Qt.Key_Return)
            compare(outputController.requested, "device29")
        }
        function test_headphonesAndSpeakersOfSameCard() {
            outputController.devices = [{id: "card", name: "内置音频 模拟立体声", is_default: true, active_port_id: "headphones", ports: [
                {id: "speakers", name: "Speakers", kind: "speaker", available: true},
                {id: "headphones", name: "Headphones", kind: "headphones", available: true}
            ]}]
            outputController.output = {selected_id: "card", selected_port_id: "headphones", selected_port_name: "Headphones", active_id: "card", active_port_id: "headphones", available: true}
            const picker = settingsPicker()
            compare(picker.options.length, 3)
            compare(option(picker, 1).text, "扬声器 / 音箱")
            compare(option(picker, 2).text, "耳机")
            verify(option(picker, 2).selected)
            verify(!option(picker, 1).selected)
            verify(findChild(picker, "audioOutputSharedPortHint").visible)
            mouseClick(button())
            tryCompare(popup(), "opened", true)
            capture("audio-output-ports-zh")
            mouseClick(option(popup().contentItem, 1))
            compare(outputController.requested, "card")
            compare(outputController.requestedPort, "speakers")
            verify(option(picker, 2).selected)
            outputController.busy = false
            outputController.output = {selected_id: "card", selected_port_id: "speakers", active_id: "card", active_port_id: "speakers", available: true}
            tryVerify(() => option(picker, 1).selected)
            i18n.language = "en"
            compare(option(picker, 2).text, "Headphones")
            capture("audio-output-ports-en")
            outputController.devices = [{id: "card", name: "Internal audio", is_default: true, active_port_id: "speakers", ports: [
                {id: "speakers", name: "Speakers", kind: "speaker", available: true},
                {id: "headphones", name: "Headphones", kind: "headphones", available: false}
            ]}]
            tryVerify(() => !option(popup().contentItem, 2).enabled)
        }
    }
}
