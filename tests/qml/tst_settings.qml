import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as Components

Item {
    id: scene
    width: 900
    height: 700
    QtObject {
        id: i18n
        property string language: "en"
        function text(key, language) { return key.replace(/_/g, " ") }
    }
    QtObject {
        id: ipcClient
        property int calls: 0
        property string track: ""
        property string language: ""
        property string model: ""
        function transcribeLyrics(trackId, revision, selectedLanguage, selectedModel) {
            calls++; track = trackId; language = selectedLanguage; model = selectedModel
        }
        function cancelAsr() { calls++ }
    }
    Component { id: settingsComponent; Components.SettingsPanel { width: 850; height: 650 } }
    Component { id: lyricsComponent; Components.LyricsPanel { width: 500; height: 500 } }
    TestCase {
        id: tests
        name: "AsrSettings"
        when: windowShown
        property var panel: null
        SignalSpy { id: saveSpy; target: tests.panel; signalName: "saveRequested" }
        function init() {
            panel = createTemporaryObject(settingsComponent, scene, {connected: true, settings: {api_key_configured: true}})
            verify(panel !== null)
            wait(100)
            saveSpy.clear()
        }
        function cleanup() { panel = null }
        function test_saveKeepsExistingKeyAndMasksReplacement() {
            const key = findChild(panel, "asrApiKey")
            compare(key.text, "")
            compare(key.echoMode, TextInput.Password)
            compare(findChild(panel, "asrLanguage"), null)
            mouseClick(findChild(panel, "saveAsrSettings"))
            compare(saveSpy.count, 1)
            compare(saveSpy.signalArguments[0][0].api_key, undefined)
            compare(saveSpy.signalArguments[0][0].language, undefined)
            verify(panel.saving)
            panel.finishSaving(true, "")
            key.text = "new-secret"
            mouseClick(findChild(panel, "saveAsrSettings"))
            compare(saveSpy.signalArguments[1][0].api_key, "new-secret")
            panel.finishSaving(false, "Save failed")
            compare(key.text, "new-secret")
            verify(panel.failed)
            panel.finishSaving(true, "")
            compare(key.text, "")
        }
        function test_clearKeyAndDisconnect() {
            mouseClick(findChild(panel, "clearAsrKey"))
            compare(saveSpy.count, 1)
            compare(saveSpy.signalArguments[0][0].api_key, "")
            panel.connected = false
            verify(!panel.saving)
            verify(!findChild(panel, "saveAsrSettings").enabled)
        }
        function test_languageIsPerUploadAndCancelDoesNotSend() {
            const lyrics = createTemporaryObject(lyricsComponent, scene, {
                connected: true, song: {song_hash: "track"},
                lyrics: {track_id: "track", state: "ready", revision: "7"},
                asrSettings: {api_key_configured: true, language: "en"}
            })
            verify(lyrics !== null)
            ipcClient.calls = 0
            wait(100)
            const button = findChild(lyrics, "transcribeLyricsButton")
            const popup = findChild(lyrics, "asrLanguagePopup")
            const language = findChild(lyrics, "asrRequestLanguage")
            mouseClick(button)
            tryCompare(popup, "opened", true)
            compare(language.currentIndex, 0)
            const model = findChild(lyrics, "asrRequestModel")
            compare(model.currentIndex, 0)
            model.currentIndex = 3 // Qwen3 for this upload.
            language.currentIndex = 3 // English for this upload.
            mouseClick(findChild(lyrics, "startAsrUpload"))
            compare(ipcClient.language, "en")
            compare(ipcClient.model, "qwen3-asr-flash-filetrans")
            compare(ipcClient.calls, 1)
            tryCompare(popup, "opened", false)
            mouseClick(button)
            tryCompare(popup, "opened", true)
            compare(language.currentIndex, 0) // Reset even for the same track.
            compare(model.currentIndex, 0)
            language.currentIndex = 1 // Automatic detection.
            mouseClick(findChild(lyrics, "cancelAsrUpload"))
            tryCompare(popup, "opened", false)
            compare(ipcClient.calls, 1)
            mouseClick(button)
            tryCompare(popup, "opened", true)
            lyrics.song = {song_hash: "other"}
            tryCompare(popup, "opened", false)
            compare(ipcClient.calls, 1)
        }
        function test_modelSelection_data() {
            return [
                {tag: "fun", index: 0, model: "fun-asr"},
                {tag: "audio31", index: 1, model: "qwen-audio-3.1-asr-flash-filetrans"},
                {tag: "audio30", index: 2, model: "qwen-audio-3.0-asr-flash-filetrans"},
                {tag: "qwen3", index: 3, model: "qwen3-asr-flash-filetrans"},
                {tag: "paraformer", index: 4, model: "paraformer-v2"}
            ]
        }
        function test_modelSelection(data) {
            const lyrics = createTemporaryObject(lyricsComponent, scene, {
                connected: true, song: {song_hash: "track"},
                lyrics: {track_id: "track", state: "ready", revision: "7"},
                asrSettings: {api_key_configured: true}
            })
            verify(lyrics !== null)
            wait(100)
            mouseClick(findChild(lyrics, "transcribeLyricsButton"))
            tryCompare(findChild(lyrics, "asrLanguagePopup"), "opened", true)
            findChild(lyrics, "asrRequestModel").currentIndex = data.index
            mouseClick(findChild(lyrics, "startAsrUpload"))
            compare(ipcClient.model, data.model)
            compare(ipcClient.language, "ja")
        }
        function test_lyricsRequestsConfigurationBeforeUpload() {
            const lyrics = createTemporaryObject(lyricsComponent, scene, {
                connected: true, song: {song_hash: "track"}, lyrics: {track_id: "track", state: "ready", revision: "7"}
            })
            verify(lyrics !== null)
            let requested = false
            lyrics.settingsRequested.connect(function() { requested = true })
            ipcClient.calls = 0
            wait(100)
            const button = findChild(lyrics, "transcribeLyricsButton")
            mouseClick(button)
            verify(requested)
            compare(ipcClient.calls, 0)
            lyrics.asrSettings = {api_key_configured: true}
            mouseClick(button)
            const popup = findChild(lyrics, "asrLanguagePopup")
            tryCompare(popup, "opened", true)
            compare(ipcClient.calls, 0)
            const language = findChild(lyrics, "asrRequestLanguage")
            compare(language.currentIndex, 0)
            mouseClick(findChild(lyrics, "startAsrUpload"))
            compare(ipcClient.calls, 1)
            compare(ipcClient.track, "track")
            compare(ipcClient.language, "ja")
            compare(ipcClient.model, "fun-asr")
            tryCompare(popup, "opened", false)
            lyrics.asr = {state: "recognizing", track_id: "track"}
            mouseClick(button)
            compare(ipcClient.calls, 2)
        }
    }
}
