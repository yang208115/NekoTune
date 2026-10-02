import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Confirmation admission and backend completion are separate moments.
// busy prevents duplicate submission and premature automatic dismissal.
// completed closes only after the owning operation reports success.
// failed restores controls and retains the explanation for retry.
// The shortcut blocker keeps transport keys out of the modal interaction.
Popup {
    id: dialog
    property string title: ""
    property string message: ""
    property string errorText: ""
    property bool busy: false
    signal confirmed()
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(440, parent.width - 40)
    padding: 24
    modal: true
    focus: true
    closePolicy: busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    function completed() { busy = false; close() }
    function failed(message) { busy = false; errorText = message }
    onOpened: errorText = ""
    background: Rectangle { objectName: "shortcutBlocker"; color: "#211C2D"; radius: 16; border.color: "#332C41" }
    contentItem: ColumnLayout {
        spacing: 20
        Label { Layout.fillWidth: true; text: dialog.title; color: "#F5F1FA"; font.pixelSize: 18; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        Label { Layout.fillWidth: true; text: dialog.message; color: "#D7CFE2"; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        Label { Layout.fillWidth: true; visible: dialog.errorText.length > 0; text: dialog.errorText; color: "#FF9BAE"; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            TextButton { text: i18n.text("cancel", i18n.language); subtle: true; enabled: !dialog.busy; onClicked: dialog.close() }
            TextButton { objectName: "confirmActionButton"; text: i18n.text("confirm_delete", i18n.language); lavenderColor: "#FF9BAE"; enabled: !dialog.busy; onClicked: { dialog.busy = true; dialog.confirmed() } }
        }
    }
}
