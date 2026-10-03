import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
// Edits the reusable global tag catalog, not one song's tag-assignment draft.
// Zero tagId selects creation; positive IDs select rename/delete of that stable identity.
// Request completion, rather than the button click, determines whether the dialog closes.
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
    // Capture the catalog ID and display spelling at dialog opening.
    // Reset old pending/error state so a previous failed operation does not disable this edit.
    // Renaming keeps song associations by ID; it does not create a replacement tag.
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
    // Prevent dismissal while a catalog mutation is awaiting its backend result.
    // The same pending state disables inputs so the submitted ID/name remain coherent.
    // Failure releases the controls and preserves the user's text for retry.
    closePolicy: pending ? Popup.NoAutoClose : Popup.CloseOnEscape
    padding: 18
    background: Rectangle {
        objectName: "shortcutBlocker"
        color: Theme.bgRaised
        radius: Theme.radiusLg
        border.color: Theme.borderSubtle
        border.width: 1
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        Label {
            text: tagEditor.translator.text(tagEditor.tagId ? "rename_tag" : "new_tag", tagEditor.translator.language)
            color: Theme.textPrimary
            font.pixelSize: Theme.fontDialogTitle
            font.weight: Font.DemiBold
        }
        InputField {
            id: tagNameField
            Layout.fillWidth: true
            enabled: !tagEditor.pending
            maximumLength: 64
            placeholderText: tagEditor.translator.text("tag_name", tagEditor.translator.language)
            color: Theme.textPrimary
            placeholderTextColor: Theme.textMuted
            background: Rectangle { color: Theme.bgSurface; radius: Theme.radiusSm; border.color: tagNameField.activeFocus ? Theme.accentPrimary : Theme.borderControl }
        }
        Label {
            visible: tagEditor.tagId > 0
            text: tagEditor.translator.text("delete_tag_hint", tagEditor.translator.language)
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.pixelSize: 11
            Layout.fillWidth: true
        }
        Label {
            visible: Boolean(tagEditor.saveError)
            text: tagEditor.saveError
            color: Theme.statusError
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
        // Controller signals also include unrelated operations, so only tag methods settle this popup.
        // The pending flag prevents an idle dialog from closing on background catalog activity.
        // The controller owns request serialization; this popup owns presentation state.
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
