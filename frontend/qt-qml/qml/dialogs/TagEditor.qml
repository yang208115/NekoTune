import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
Popup {
    id: tagEditor
    required property var shell
    required property var controller
    required property var translator
    property int tagId: 0
    property bool pending: false
    property string saveError: ""
    function openNew() {
        tagId = 0
        tagNameField.text = ""
        pending = false
        saveError = ""
        open()
    }
    function openForTag(tag) {
        tagId = Number(tag.id)
        tagNameField.text = String(tag.name)
        pending = false
        saveError = ""
        open()
    }
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 340
    height: (tagId ? 238 : 180) + (saveError ? 48 : 0)
    modal: true
    focus: true
    closePolicy: pending ? Popup.NoAutoClose : Popup.CloseOnEscape
    padding: 18
    background: Rectangle {
            objectName: "shortcutBlocker"; color: tagEditor.shell.surfaceRaised; radius: 14; border.color: tagEditor.shell.border }
    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        Label {
            text: tagEditor.translator.text(tagEditor.tagId ? "rename_tag" : "new_tag", tagEditor.translator.language)
            color: tagEditor.shell.ink
            font.pixelSize: 16
        }
        InputField {
            id: tagNameField
            Layout.fillWidth: true
            enabled: !tagEditor.pending
            maximumLength: 64
            placeholderText: tagEditor.translator.text("tag_name", tagEditor.translator.language)
            color: tagEditor.shell.ink
            placeholderTextColor: tagEditor.shell.muted
            background: Rectangle { color: tagEditor.shell.surface; radius: 8; border.color: tagEditor.shell.borderControl }
        }
        Label {
            visible: tagEditor.tagId > 0
            text: tagEditor.translator.text("delete_tag_hint", tagEditor.translator.language)
            wrapMode: Text.WordWrap
            color: tagEditor.shell.muted
            font.pixelSize: 11
            Layout.fillWidth: true
        }
        Label {
            visible: Boolean(tagEditor.saveError)
            text: tagEditor.saveError
            color: tagEditor.shell.rose
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            font.pixelSize: 11
        }
        RowLayout {
            Layout.fillWidth: true
            TextButton {
                visible: tagEditor.tagId > 0
                text: tagEditor.translator.text("delete_tag", tagEditor.translator.language)
                subtle: true
                enabled: !tagEditor.pending
                onClicked: {
                    tagEditor.pending = true
                    tagEditor.saveError = ""
                    tagEditor.controller.manageTag("delete", {id: tagEditor.tagId})
                }
            }
            Item { Layout.fillWidth: true }
            TextButton {
                text: tagEditor.translator.text("cancel", tagEditor.translator.language)
                subtle: true
                enabled: !tagEditor.pending
                onClicked: tagEditor.close()
            }
            TextButton {
                text: tagEditor.translator.text("save", tagEditor.translator.language)
                enabled: tagNameField.text.trim().length > 0 && !tagEditor.pending
                onClicked: {
                    tagEditor.pending = true
                    tagEditor.saveError = ""
                    tagEditor.controller.manageTag(tagEditor.tagId ? "rename" : "create", {
                        id: tagEditor.tagId, name: tagNameField.text.trim()
                    })
                }
            }
        }
    }
    Connections {
        target: tagEditor.controller
        function onRequestSucceeded(method) {
            if (tagEditor.pending && method.startsWith("tag.")) { tagEditor.pending = false; tagEditor.close() }
        }
        function onRequestFailed(method, message) {
            if (tagEditor.pending && method.startsWith("tag.")) {
                tagEditor.pending = false
                tagEditor.saveError = message
            }
        }
    }
}
