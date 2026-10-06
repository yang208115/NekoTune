import QtQuick
import QtQuick.Controls
import QtTest
import "../../frontend/qt-qml/qml/dialogs" as Dialogs

Rectangle {
    id: scene
    width: 900
    height: 700
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
    }
    QtObject {
        id: library
        property var tags: testLibrary.tags
        property var editing: ({})
        property bool metadataReady: false
        property var savedPatch: null
        signal songMetadataSaved(int id)
        signal requestFailed(string method, string message)
        function loadMetadata(id) {
            metadataReady = true;
            editing = {song_id: id, custom_title: "夜空", artist: "示例歌手", tags: [{name: "收藏"}], import_source: "example.music", play_count: 12};
        }
        function saveMetadata(patch) { savedPatch = patch; }
    }
    Component {
        id: editorComponent
        Dialogs.MetadataEditor { shell: fakeShell; controller: library; translator: testTranslator }
    }
    TestCase {
        name: "MetadataTags"
        when: windowShown
        property var editor: null
        property var combo: null
        function init() {
            testTranslator.language = "zh";
            testFixtures.seedLibrary({tags: [
                {id: 1, name: "收藏"}, {id: 2, name: "中文"},
                {id: 3, name: "Rock"}, {id: 4, name: "夜晚"}
            ], songs: []});
            library.metadataReady = false;
            library.savedPatch = null;
            editor = createTemporaryObject(editorComponent, scene);
            verify(editor !== null);
            editor.openForSong({song_id: 1});
            tryCompare(editor, "opened", true);
            combo = findChild(editor, "metadataTagInput");
            verify(combo !== null);
        }
        function cleanup() { editor.close(); editor = null; combo = null; }
        function test_statisticsAreReadOnlyAndLocalized() {
            const source = findChild(editor, "metadataImportSource");
            const count = findChild(editor, "metadataPlayCount");
            compare(source.text, "入库来源: example.music");
            compare(count.text, "播放次数: 12");
            testTranslator.language = "en";
            compare(source.text, "Import source: example.music");
            compare(count.text, "Play count: 12");
            editor.save();
            verify(!("import_source" in library.savedPatch));
            verify(!("play_count" in library.savedPatch));
            const path = testFixtures.screenshotPath("metadata-statistics");
            if (path) {
                waitForRendering(scene);
                grabImage(editor.background.parent).save(path);
            }
        }
        // The catalog omits tags already present in the draft.
        // Selecting another tag updates the draft and available choices without writing metadata yet.
        // The save button submits the complete selected tag names explicitly.
        function test_selectExistingAndSave() {
            compare(combo.count, 3);
            compare(combo.editText, "");
            mouseClick(combo, combo.width - 15, combo.height / 2);
            tryCompare(combo.popup, "opened", true);
            const path = testFixtures.screenshotPath("metadata-tag-dropdown");
            if (path) {
                waitForRendering(scene);
                wait(150);
                grabImage(editor.background.parent).save(path);
            }
            const list = combo.popup.contentItem;
            tryVerify(() => list.itemAtIndex(0) !== null);
            mouseClick(list.itemAtIndex(0));
            compare(editor.tagNames, ["收藏", "中文"]);
            compare(combo.count, 2);
            compare(combo.editText, "");
            compare(library.savedPatch, null);
            mouseClick(findChild(editor, "saveMetadataButton"));
            compare(library.savedPatch.tags, ["收藏", "中文"]);
        }
        function typeText(text) { for (const character of text) keyClick(character); }
        // New tag names may be entered without a matching catalog record.
        // Case-insensitive duplicate detection preserves the first spelling.
        // Button and Enter submission share the same add-and-clear behavior.
        function test_typeNewAndPreventDuplicates() {
            combo.contentItem.forceActiveFocus();
            typeText("Jazz");
            mouseClick(findChild(editor, "metadataAddTagButton"));
            compare(editor.tagNames, ["收藏", "Jazz"]);
            combo.contentItem.forceActiveFocus();
            typeText("jazz");
            keyClick(Qt.Key_Return);
            compare(editor.tagNames, ["收藏", "Jazz"]);
            compare(combo.editText, "");
            typeText("Live");
            keyClick(Qt.Key_Return);
            compare(editor.tagNames, ["收藏", "Jazz", "Live"]);
        }
        // Keyboard selection must work through the popup's focused item.
        // An empty catalog must still permit creating a new tag in the draft.
        // Reopening for another song resets the previous input and selected-tag additions.
        function test_keyboardSelectionEmptyCatalogAndReopen() {
            combo.forceActiveFocus();
            mouseClick(combo, combo.width - 15, combo.height / 2);
            tryCompare(combo.popup, "opened", true);
            keyClick(Qt.Key_Down);
            keyClick(Qt.Key_Return);
            compare(editor.tagNames.length, 2);
            testFixtures.seedLibrary({tags: [], songs: []});
            compare(combo.count, 0);
            combo.contentItem.forceActiveFocus();
            typeText("New");
            keyClick(Qt.Key_Return);
            compare(editor.tagNames[2], "New");
            editor.close();
            editor.openForSong({song_id: 2});
            tryCompare(editor, "opened", true);
            compare(editor.tagNames, ["收藏"]);
            compare(combo.editText, "");
        }
    }
}
