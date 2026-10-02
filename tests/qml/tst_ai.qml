import QtQuick
import QtQuick.Controls
import QtTest
import "../../frontend/qt-qml/qml/components" as Components
import "../../frontend/qt-qml/qml/dialogs" as Dialogs

Rectangle {
    id: scene
    width: 1000
    height: 640
    color: "#0E0D14"
    QtObject {
        id: fakeShell
        property color ink: "#F5F1FA"
        property color muted: "#AAA0B8"
        property color subtle: "#D7CFE2"
        property color surface: "#17141F"
        property color surfaceRaised: "#211C2D"
        property color border: "#332C41"
        property color borderControl: "#8D809F"
        property color lavender: "#CBB8FF"
        property color rose: "#E8A9C3"
        property string page: ""
        function navigate(value) {
            page = value;
        }
    }
    QtObject {
        id: library
        property var editing: ({})
        property bool metadataReady: false
        property var savedPatch: null
        signal songMetadataSaved(int id)
        signal requestFailed(string method, string message)
        function loadMetadata(id) {
            metadataReady = true;
            editing = {
                song_id: id,
                custom_title: "原歌名",
                artist: "原歌手",
                lyrics: "[00:01.00]仰望夜空\n[00:02.00]星光闪烁",
                tags: [
                    {
                        name: "收藏"
                    },
                    {
                        name: "中文"
                    }
                ]
            };
        }
        function saveMetadata(patch) {
            savedPatch = patch;
        }
    }
    QtObject {
        id: fakeAi
        property var configuration: ({})
        property bool configBusy: false
        property bool generating: false
        property string suggestionError: ""
        property var lastDraft: null
        property var lastConfig: null
        property int discards: 0
        property int connectionTests: 0
        property int clears: 0
        signal suggestionReady(var result)
        signal requestSucceeded(string method)
        signal requestFailed(string method, string reason)
        function refreshConfiguration() {
        }
        function saveConfiguration(config) {
            lastConfig = config;
            configBusy = true;
        }
        function clearKey() {
            clears++;
            configBusy = true;
        }
        function testConnection() {
            connectionTests++;
            configBusy = true;
        }
        function discardSuggestion() {
            discards++;
            generating = false;
            suggestionError = "";
        }
        function suggest(id, draft) {
            lastDraft = draft;
            suggestionError = "";
            generating = true;
        }
        function resolve(id, title, artist, tags) {
            generating = false;
            suggestionReady({
                song_id: id,
                custom_title: title,
                artist: artist,
                tags: tags,
                warning: ""
            });
        }
    }
    QtObject {
        id: settingsClient
        signal requestSucceeded(string method)
        signal requestFailed(string method, string reason)
        function kugouSaveKey(key) {
        }
        function kugouClearKey() {
        }
    }
    Component {
        id: editorComponent
        Dialogs.MetadataEditor {
            shell: fakeShell
            controller: library
            translator: testTranslator
            ai: fakeAi
        }
    }
    Component {
        id: settingsComponent
        Components.SettingsPanel {
            x: 20
            y: 20
            width: scene.width - 40
            height: scene.height - 40
            client: settingsClient
            ai: fakeAi
            translator: testTranslator
            account: ({
                    configured: false
                })
        }
    }
    TestCase {
        id: tests
        name: "AiMetadata"
        when: windowShown
        property var editor: null
        function init() {
            testTranslator.language = "zh";
            library.savedPatch = null;
            library.metadataReady = false;
            fakeAi.configuration = {
                base_url: "http://127.0.0.1:1234/v1",
                model: "local-text-model",
                configured: true,
                key_saved: false,
                credential_error: ""
            };
            fakeAi.configBusy = false;
            fakeAi.generating = false;
            fakeAi.suggestionError = "";
            fakeAi.lastDraft = null;
            fakeAi.lastConfig = null;
            fakeAi.connectionTests = 0;
            fakeAi.clears = 0;
            fakeShell.page = "";
            editor = createTemporaryObject(editorComponent, scene);
            verify(editor !== null);
        }
        function cleanup() {
            editor.close();
            editor = null;
        }
        function openEditor() {
            editor.openForSong({
                song_id: 1
            });
            tryCompare(editor, "opened", true);
            tryCompare(editor, "tagsReady", true);
        }
        function screenshot(item, name) {
            const path = testFixtures.screenshotPath(name);
            if (path.length > 0) {
                waitForRendering(item);
                wait(180);
                grabImage(item).save(path);
            }
        }
        // Generation uses the current unsaved draft rather than only the stored metadata.
        // While pending, editing and saving are disabled to keep that request's input coherent.
        // The suggestion merges tags and preserves fields for which no replacement was suggested.
        // Applying the preview still requires an explicit save, and untouched lyrics stay absent from the patch.
        // A save failure keeps the generated draft available for correction or retry.
        function test_previewMergesTagsAndSavesOnlyOnClick() {
            openEditor();
            const title = findChild(editor, "metadataTitleField");
            const artist = findChild(editor, "metadataArtistField");
            const lyrics = findChild(editor, "metadataLyricsField");
            const save = findChild(editor, "saveMetadataButton");
            title.text = "草稿歌名";
            mouseClick(findChild(editor, "aiFillButton"));
            compare(fakeAi.lastDraft.custom_title, "草稿歌名");
            compare(title.enabled, false);
            compare(artist.enabled, false);
            compare(lyrics.enabled, false);
            compare(save.enabled, false);
            compare(library.savedPatch, null);
            fakeAi.resolve(1, "夜空", "", ["中文", "抒情", "夜晚"]);
            compare(title.text, "夜空");
            compare(artist.text, "原歌手");
            compare(editor.tagNames, ["收藏", "中文", "抒情", "夜晚"]);
            compare(library.savedPatch, null);
            compare(lyrics.text, library.editing.lyrics);
            screenshot(editor.background.parent, "ai-metadata-editor-zh");
            testTranslator.language = "en";
            screenshot(editor.background.parent, "ai-metadata-editor-en");
            mouseClick(save);
            compare(library.savedPatch.custom_title, "夜空");
            compare(library.savedPatch.tags, ["收藏", "中文", "抒情", "夜晚"]);
            verify(library.savedPatch.lyrics === undefined);
            library.requestFailed("song.update_metadata", "Save failed");
            compare(editor.pending, false);
            compare(editor.visible, true);
            compare(title.text, "夜空");
        }
        // Generation failure must preserve the current draft and re-enable retry.
        // Closing the editor discards the active suggestion context before another song opens.
        // An unconfigured service guides the user to settings rather than sending an unusable request.
        function test_failureRetryAndUnconfiguredSettings() {
            openEditor();
            const title = findChild(editor, "metadataTitleField");
            title.text = "保留草稿";
            editor.generate();
            fakeAi.generating = false;
            fakeAi.suggestionError = "ai_error_timeout";
            compare(title.text, "保留草稿");
            compare(findChild(editor, "aiFillButton").enabled, true);
            compare(findChild(editor, "aiFillButton").text, "重试 AI 填写");
            const before = fakeAi.discards;
            editor.close();
            tryCompare(editor, "opened", false);
            verify(fakeAi.discards > before);
            fakeAi.configuration = {
                configured: false,
                key_saved: false
            };
            openEditor();
            compare(findChild(editor, "aiFillButton").enabled, false);
            mouseClick(findChild(editor, "aiOpenSettingsButton"));
            compare(fakeShell.page, "settings");
        }
        // Public configuration initializes endpoint and model but never fills the secret input.
        // The temporary password is trimmed and cleared as soon as it is submitted.
        // Connection testing waits for the backend to confirm the configuration save.
        // The compact English layout exercises the longer labels without exposing the password.
        function test_settingsKeyAndTestFlow() {
            const panel = createTemporaryObject(settingsComponent, scene);
            verify(panel !== null);
            const base = findChild(panel, "aiBaseUrlField");
            const model = findChild(panel, "aiModelField");
            const key = findChild(panel, "aiKeyField");
            const save = findChild(panel, "aiSaveConfigButton");
            const test = findChild(panel, "aiTestButton");
            compare(base.text, fakeAi.configuration.base_url);
            compare(model.text, fakeAi.configuration.model);
            compare(key.echoMode, TextInput.Password);
            compare(key.text, "");
            key.text = " temporary-secret ";
            compare(test.enabled, false);
            save.clicked();
            compare(fakeAi.lastConfig.api_key, "temporary-secret");
            compare(key.text, "");
            fakeAi.configuration = {
                base_url: base.text,
                model: model.text,
                configured: true,
                key_saved: true,
                credential_error: ""
            };
            fakeAi.configBusy = false;
            fakeAi.requestSucceeded("ai.config.set");
            compare(test.enabled, true);
            test.clicked();
            compare(fakeAi.connectionTests, 1);
            fakeAi.configBusy = false;
            fakeAi.requestSucceeded("ai.test");
            screenshot(scene, "ai-settings-zh");
            screenshot(base.parent.parent, "ai-settings-card-zh");
            testTranslator.language = "en";
            screenshot(scene, "ai-settings-en");
            panel.width = 700;
            waitForRendering(panel);
            verify(base.width <= 664);
            verify(base.x + base.width <= base.parent.width);
            const buttonRow = test.parent;
            verify(buttonRow.childrenRect.width <= buttonRow.width);
            screenshot(base.parent.parent, "ai-settings-card-en-small");
            base.text = "https://different.example/v1";
            compare(test.enabled, false);
            compare(findChild(panel, "aiClearKeyButton").enabled, false);
            save.clicked();
            verify(fakeAi.lastConfig.api_key === undefined);
            key.text = "must-clear-when-hidden";
            panel.visible = false;
            compare(key.text, "");
        }
    }
}
