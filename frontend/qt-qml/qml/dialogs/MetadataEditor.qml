import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
Popup {
    id: metadataEditor
    required property var shell
    required property var controller
    required property var translator
    property var original: ({})
    objectName: "metadataEditor"
    property int songId: 0
    property var tagNames: []
    property bool tagsReady: false
    property bool pending: false
    property string saveError: ""
    function addTag() {
        const name = tagInput.text.trim()
        if (!name || name.length > 64) return
        if (tagNames.some(tag => tag.toLocaleLowerCase() === name.toLocaleLowerCase())) {
            tagInput.text = ""
            return
        }
        tagNames = tagNames.concat([name])
        tagInput.text = ""
    }
    function openForSong(value) {
        songId = Number(value.song_id || 0)
        tagsReady = false
        pending = false
        saveError = ""
        original = ({})
        titleField.text = ""
        artistField.text = ""
        lyricsField.text = ""
        tagNames = []
        tagInput.text = ""
        open()
        metadataEditor.controller.loadMetadata(songId)
    }
    function save() {
        if (!tagsReady || pending) return
        const patch = {}
        if (titleField.text !== String(original.custom_title || "")) patch.custom_title = titleField.text
        if (artistField.text !== String(original.artist || "")) patch.artist = artistField.text
        if (lyricsField.text !== String(original.lyrics || "")) patch.lyrics = lyricsField.text
        const oldTags = (original.tags || []).map(tag => tag.name)
        if (JSON.stringify(tagNames) !== JSON.stringify(oldTags)) patch.tags = tagNames
        pending = true
        saveError = ""
        metadataEditor.controller.saveMetadata(patch)
    }
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(580, parent.width - 48)
    height: Math.min(600, parent.height - 48)
    modal: true
    padding: 24
    background: Rectangle {
            objectName: "shortcutBlocker"
        color: metadataEditor.shell.surfaceRaised
        radius: 16
        border.color: metadataEditor.shell.border
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
                color: metadataEditor.shell.ink
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
            IconButton {
                kind: "close"
                tooltipText: metadataEditor.translator.text("close", metadataEditor.translator.language)
                onClicked: metadataEditor.close()
            }
        }
        TextField {
            id: titleField
            objectName: "metadataTitleField"
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            placeholderText: metadataEditor.translator.text("custom_title", metadataEditor.translator.language)
            color: metadataEditor.shell.ink
            placeholderTextColor: metadataEditor.shell.muted
            leftPadding: 12
            rightPadding: 12
            background: Rectangle {
                color: metadataEditor.shell.surface
                radius: 8
                border.color: titleField.activeFocus ? metadataEditor.shell.lavender : metadataEditor.shell.borderControl
                border.width: 1
            }
        }
        TextField {
            id: artistField
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            placeholderText: metadataEditor.translator.text("artist_author", metadataEditor.translator.language)
            color: metadataEditor.shell.ink
            placeholderTextColor: metadataEditor.shell.muted
            leftPadding: 12
            rightPadding: 12
            background: Rectangle {
                color: metadataEditor.shell.surface
                radius: 8
                border.color: artistField.activeFocus ? metadataEditor.shell.lavender : metadataEditor.shell.borderControl
                border.width: 1
            }
        }
        Label {
            text: metadataEditor.translator.text("tags", metadataEditor.translator.language)
            color: metadataEditor.shell.subtle
            font.pixelSize: 12
        }
        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(0, childrenRect.height)
            spacing: 6
            Repeater {
                model: metadataEditor.tagNames
                delegate: TextButton {
                    required property var modelData
                    required property int index
                    text: String(modelData) + " ×"
                    subtle: true
                    implicitHeight: 28
                    onClicked: {
                        const next = metadataEditor.tagNames.slice()
                        next.splice(index, 1)
                        metadataEditor.tagNames = next
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: tagInput
                Layout.fillWidth: true
                enabled: metadataEditor.tagsReady
                maximumLength: 64
                placeholderText: metadataEditor.translator.text("tag_name", metadataEditor.translator.language)
                color: metadataEditor.shell.ink
                placeholderTextColor: metadataEditor.shell.muted
                background: Rectangle { color: metadataEditor.shell.surface; radius: 8; border.color: metadataEditor.shell.borderControl }
                onAccepted: metadataEditor.addTag()
            }
            TextButton {
                text: metadataEditor.translator.text("add_tag", metadataEditor.translator.language)
                subtle: true
                enabled: metadataEditor.tagsReady && tagInput.text.trim().length > 0
                onClicked: metadataEditor.addTag()
            }
        }
        Label {
            visible: !metadataEditor.tagsReady || Boolean(metadataEditor.saveError)
            text: !metadataEditor.tagsReady ? metadataEditor.translator.text("loading_tags", metadataEditor.translator.language)
                                            : metadataEditor.saveError
            color: metadataEditor.shell.rose
            font.pixelSize: 11
        }
        TextArea {
            id: lyricsField
            objectName: "metadataLyricsField"
            Layout.fillWidth: true
            Layout.fillHeight: true
            placeholderText: metadataEditor.translator.text("lyrics", metadataEditor.translator.language)
            color: metadataEditor.shell.ink
            placeholderTextColor: metadataEditor.shell.muted
            wrapMode: TextEdit.Wrap
            padding: 12
            background: Rectangle {
                color: metadataEditor.shell.surface
                radius: 8
                border.color: lyricsField.activeFocus ? metadataEditor.shell.lavender : metadataEditor.shell.borderControl
                border.width: 1
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            TextButton {
                text: metadataEditor.translator.text("cancel", metadataEditor.translator.language)
                subtle: true
                onClicked: metadataEditor.close()
            }
            TextButton {
                objectName: "saveMetadataButton"
                text: metadataEditor.translator.text("save", metadataEditor.translator.language)
                enabled: metadataEditor.songId > 0 && metadataEditor.tagsReady && !metadataEditor.pending
                onClicked: {
                    metadataEditor.save()
                }
            }
        }
    }
    Connections {
        target: metadataEditor.controller
        function onEditingChanged() {
            if (!metadataEditor.controller.metadataReady || Number(metadataEditor.controller.editing.song_id) !== metadataEditor.songId) return
            metadataEditor.original = metadataEditor.controller.editing
            titleField.text = String(metadataEditor.controller.editing.custom_title || "")
            artistField.text = String(metadataEditor.controller.editing.artist || "")
            lyricsField.text = String(metadataEditor.controller.editing.lyrics || "")
            metadataEditor.tagNames = (metadataEditor.controller.editing.tags || []).map(tag => tag.name)
            metadataEditor.tagsReady = true
        }
        function onSongMetadataSaved(songId) {
            if (songId === metadataEditor.songId) metadataEditor.close()
        }
        function onRequestFailed(method, message) {
            if (method === "song.metadata" || (method === "song.update_metadata" && metadataEditor.pending)) {
                metadataEditor.pending = false
                metadataEditor.saveError = message
            }
        }
    }
}
