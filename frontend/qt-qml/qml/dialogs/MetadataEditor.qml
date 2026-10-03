pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Popup {
    id: metadataEditor
    required property var shell
    required property var controller
    required property var translator
    property var ai: null
    readonly property bool generating: ai ? ai.generating : false
    readonly property bool editable: tagsReady && !pending && !generating
    property string aiMessage: ""
    function t(key) {
        return translator.text(key, translator.language) || key;
    }
    property var original: ({})
    objectName: "metadataEditor"
    property int songId: 0
    property var tagNames: []
    property bool tagsReady: false
    property bool pending: false
    property string saveError: ""
    function addTag() {
        if (!editable)
            return;
        const name = tagInput.editText.trim();
        if (!name || name.length > 64)
            return;
        if (tagNames.some(tag => tag.toLocaleLowerCase() === name.toLocaleLowerCase())) {
            tagInput.editText = "";
            return;
        }
        tagNames = tagNames.concat([name]);
        tagInput.currentIndex = -1;
        tagInput.editText = "";
    }
    // Start a fresh editing session instead of copying lightweight list metadata.
    // Wait for full song.metadata so absent list lyrics cannot become an empty save.
    // Discard AI generation from the previous editor before loading another song.
    // Reset original/draft state to avoid displaying stale fields during lookup.
    // Saving remains disabled until the matching full metadata has arrived.
    function openForSong(value) {
        if (ai) {
            ai.discardSuggestion();
            ai.refreshConfiguration();
        }
        aiMessage = "";
        songId = Number(value.song_id || 0);
        tagsReady = false;
        pending = false;
        saveError = "";
        original = ({});
        titleField.text = "";
        artistField.text = "";
        lyricsField.text = "";
        tagNames = [];
        tagInput.currentIndex = -1;
        tagInput.editText = "";
        open();
        metadataEditor.controller.loadMetadata(songId);
    }
    function save() {
        if (!editable)
            return;
        // Send only changed fields; omission preserves values, while a changed empty field clears it.
        const patch = {};
        if (titleField.text !== String(original.custom_title || ""))
            patch.custom_title = titleField.text;
        if (artistField.text !== String(original.artist || ""))
            patch.artist = artistField.text;
        if (lyricsField.text !== String(original.lyrics || ""))
            patch.lyrics = lyricsField.text;
        const oldTags = (original.tags || []).map(tag => tag.name);
        if (JSON.stringify(tagNames) !== JSON.stringify(oldTags))
            patch.tags = tagNames;
        pending = true;
        saveError = "";
        metadataEditor.controller.saveMetadata(patch);
    }
    // Include unsaved title/artist/lyrics/tag edits as the generation draft.
    // Commit any typed tag into that draft before collecting its snapshot.
    // The suggestion modifies controls only and still requires explicit Save.
    // Disable concurrent draft editing while a generation result is being prepared.
    function generate() {
        if (!editable || !ai)
            return;
        addTag();
        aiMessage = "";
        ai.suggest(songId, {
            custom_title: titleField.text,
            artist: artistField.text,
            lyrics: lyricsField.text,
            tags: tagNames
        });
    }
    onClosed: if (ai)
        ai.discardSuggestion()
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(580, parent.width - 48)
    height: Math.min(600, parent.height - 48)
    modal: true
    padding: 24
    background: Rectangle {
        objectName: "shortcutBlocker"
        color: Theme.bgRaised
        radius: Theme.radiusLg
        border.color: Theme.borderSubtle
        border.width: 1
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: metadataEditor.translator.text("edit_track_info", metadataEditor.translator.language)
                color: Theme.textPrimary
                font.pixelSize: Theme.fontDialogTitle
                font.weight: Font.DemiBold
            }
            IconButton {
                kind: "close"
                tooltipText: metadataEditor.translator.text("close", metadataEditor.translator.language)
                onClicked: metadataEditor.close()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextButton {
                objectName: "aiFillButton"
                text: metadataEditor.t(metadataEditor.generating ? "ai_generating" : metadataEditor.ai && metadataEditor.ai.suggestionError ? "ai_retry" : "ai_fill")
                enabled: metadataEditor.editable && metadataEditor.ai && Boolean(metadataEditor.ai.configuration.configured) && !metadataEditor.ai.configBusy
                onClicked: metadataEditor.generate()
            }
            BusyIndicator {
                running: metadataEditor.generating
                visible: running
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
            }
            Item {
                Layout.fillWidth: true
            }
            TextButton {
                objectName: "aiOpenSettingsButton"
                visible: metadataEditor.ai && !metadataEditor.ai.configuration.configured
                text: metadataEditor.t("ai_open_settings")
                subtle: true
                onClicked: {
                    metadataEditor.close();
                    metadataEditor.shell.navigate("settings");
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: metadataEditor.t(metadataEditor.ai && metadataEditor.ai.suggestionError ? metadataEditor.ai.suggestionError : metadataEditor.aiMessage || (metadataEditor.ai && metadataEditor.ai.configuration.configured ? "ai_fill_help" : "ai_error_configuration"))
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontCaption
            color: metadataEditor.ai && metadataEditor.ai.suggestionError ? Theme.statusError : Theme.textMuted
        }
        ScrollView {
            id: formScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: formScroll.availableWidth
                spacing: 12
                ColumnLayout {
                    id: metadataFields
                    Layout.fillWidth: true
                    spacing: 12
                    TextField {
                        id: titleField
                        objectName: "metadataTitleField"
                        enabled: metadataEditor.editable
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        placeholderText: metadataEditor.translator.text("custom_title", metadataEditor.translator.language)
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textMuted
                        leftPadding: 12
                        rightPadding: 12
                        background: Rectangle {
                            color: Theme.bgSurface
                            radius: Theme.radiusSm
                            border.color: titleField.activeFocus ? Theme.accentPrimary : Theme.borderControl
                            border.width: 1
                        }
                    }
                    TextField {
                        id: artistField
                        objectName: "metadataArtistField"
                        enabled: metadataEditor.editable
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        placeholderText: metadataEditor.translator.text("artist_author", metadataEditor.translator.language)
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textMuted
                        leftPadding: 12
                        rightPadding: 12
                        background: Rectangle {
                            color: Theme.bgSurface
                            radius: Theme.radiusSm
                            border.color: artistField.activeFocus ? Theme.accentPrimary : Theme.borderControl
                            border.width: 1
                        }
                    }
                    Label {
                        text: metadataEditor.translator.text("tags", metadataEditor.translator.language)
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                    Flow {
                        id: tagFlow
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(0, childrenRect.height)
                        spacing: 6
                        Repeater {
                            model: metadataEditor.tagNames
                            delegate: TextButton {
                                required property var modelData
                                required property int index
                                text: String(modelData) + " ×"
                                width: Math.min(implicitWidth, tagFlow.width)
                                subtle: true
                                enabled: metadataEditor.editable
                                implicitHeight: 28
                                onClicked: {
                                    const next = metadataEditor.tagNames.slice();
                                    next.splice(index, 1);
                                    metadataEditor.tagNames = next;
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: tagInput
                            objectName: "metadataTagInput"
                            Layout.fillWidth: true
                            implicitHeight: 40
                            enabled: metadataEditor.editable
                            editable: true
                            currentIndex: -1
                            model: (metadataEditor.controller.tags ? metadataEditor.controller.tags.items : [])
                                .map(tag => String(tag.name))
                                .filter(name => !metadataEditor.tagNames.some(tag => tag.toLocaleLowerCase() === name.toLocaleLowerCase()))
                            onModelChanged: {
                                currentIndex = -1;
                                editText = "";
                            }
                            palette.text: Theme.textPrimary
                            palette.buttonText: Theme.textPrimary
                            palette.base: Theme.bgSurface
                            palette.highlight: Theme.accentPrimary
                            palette.highlightedText: Theme.textOnAccent
                            contentItem: TextField {
                                text: tagInput.editText
                                font: tagInput.font
                                maximumLength: 64
                                selectByMouse: true
                                color: Theme.textPrimary
                                selectionColor: Theme.accentPrimary
                                selectedTextColor: Theme.textOnAccent
                                placeholderText: metadataEditor.translator.text("tag_name", metadataEditor.translator.language)
                                placeholderTextColor: Theme.textMuted
                                verticalAlignment: Text.AlignVCenter
                                background: Item {}
                            }
                            background: Rectangle {
                                color: Theme.bgSurface
                                radius: Theme.radiusSm
                                border.color: tagInput.activeFocus ? Theme.accentPrimary : Theme.borderControl
                            }
                            delegate: ItemDelegate {
                                id: tagOption
                                required property string modelData
                                required property int index
                                width: tagInput.width - 12
                                highlighted: tagInput.highlightedIndex === index
                                contentItem: Label {
                                    text: tagOption.modelData
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    color: Theme.textPrimary
                                }
                                background: Rectangle {
                                    radius: 6
                                    color: tagOption.highlighted ? Theme.bgSelected : "transparent"
                                }
                            }
                            popup: Popup {
                                y: tagInput.height + 4
                                width: tagInput.width
                                padding: 6
                                implicitHeight: Math.min(contentItem.implicitHeight + 12, 240)
                                background: Rectangle {
                                    color: Theme.bgRaised
                                    radius: Theme.radiusSm
                                    border.color: Theme.borderControl
                                }
                                contentItem: ListView {
                                    clip: true
                                    implicitHeight: contentHeight
                                    model: tagInput.popup.visible ? tagInput.delegateModel : null
                                    currentIndex: tagInput.highlightedIndex
                                    ScrollIndicator.vertical: ScrollIndicator {}
                                }
                            }
                            onActivated: metadataEditor.addTag()
                            onAccepted: metadataEditor.addTag()
                        }
                        TextButton {
                            objectName: "metadataAddTagButton"
                            text: metadataEditor.translator.text("add_tag", metadataEditor.translator.language)
                            subtle: true
                            enabled: metadataEditor.editable && tagInput.editText.trim().length > 0
                            onClicked: metadataEditor.addTag()
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        textFormat: Text.PlainText
                        visible: !metadataEditor.tagsReady || Boolean(metadataEditor.saveError)
                        text: !metadataEditor.tagsReady ? metadataEditor.translator.text("loading_tags", metadataEditor.translator.language) : metadataEditor.saveError
                        color: Theme.statusError
                        font.pixelSize: Theme.fontCaption
                    }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(120, formScroll.availableHeight - metadataFields.implicitHeight - 12)
                    clip: true
                    TextArea {
                        id: lyricsField
                        objectName: "metadataLyricsField"
                        enabled: metadataEditor.editable
                        placeholderText: metadataEditor.translator.text("lyrics", metadataEditor.translator.language)
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textMuted
                        wrapMode: TextEdit.Wrap
                        padding: 12
                        background: Rectangle {
                            color: Theme.bgSurface
                            radius: Theme.radiusSm
                            border.color: lyricsField.activeFocus ? Theme.accentPrimary : Theme.borderControl
                            border.width: 1
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true
            }
            TextButton {
                text: metadataEditor.translator.text("cancel", metadataEditor.translator.language)
                subtle: true
                onClicked: metadataEditor.close()
            }
            TextButton {
                objectName: "saveMetadataButton"
                text: metadataEditor.translator.text("save", metadataEditor.translator.language)
                enabled: metadataEditor.songId > 0 && metadataEditor.editable
                onClicked: {
                    metadataEditor.save();
                }
            }
        }
    }
    Connections {
        target: metadataEditor.ai
        function onSuggestionReady(result) {
            if (!metadataEditor.visible || Number(result.song_id) !== metadataEditor.songId)
                return;
            // Suggestions update the draft only; empty guesses preserve edits until the user saves.
            if (result.custom_title)
                titleField.text = result.custom_title;
            if (result.artist)
                artistField.text = result.artist;
            // Keep personal tags while adding suggested categories without case-only duplicates.
            const merged = metadataEditor.tagNames.slice();
            for (const name of result.tags || []) {
                if (!merged.some(tag => tag.toLocaleLowerCase() === String(name).toLocaleLowerCase()))
                    merged.push(String(name));
            }
            metadataEditor.tagNames = merged;
            metadataEditor.aiMessage = result.warning || "ai_filled";
        }
    }
    Connections {
        target: metadataEditor.controller
        function onEditingChanged() {
            if (!metadataEditor.controller.metadataReady || Number(metadataEditor.controller.editing.song_id) !== metadataEditor.songId)
                return;
            metadataEditor.original = metadataEditor.controller.editing;
            titleField.text = String(metadataEditor.controller.editing.custom_title || "");
            artistField.text = String(metadataEditor.controller.editing.artist || "");
            lyricsField.text = String(metadataEditor.controller.editing.lyrics || "");
            metadataEditor.tagNames = (metadataEditor.controller.editing.tags || []).map(tag => tag.name);
            metadataEditor.tagsReady = true;
        }
        function onSongMetadataSaved(songId) {
            if (songId === metadataEditor.songId)
                metadataEditor.close();
        }
        function onRequestFailed(method, message) {
            if (method === "song.metadata" || (method === "song.update_metadata" && metadataEditor.pending)) {
                metadataEditor.pending = false;
                metadataEditor.saveError = message;
            }
        }
    }
}
