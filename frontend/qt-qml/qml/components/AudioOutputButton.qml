pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

IconButton {
    id: button
    required property var controller
    required property var translator
    property bool connected: true
    objectName: "audioOutputButton"
    kind: "audio_output"
    enabled: connected && controller !== null
    glyphColor: controller && controller.output.available ? Theme.textMuted : Theme.statusWarning
    tooltipText: translator.text("audio_output", translator.language)
    function positionPopup() {
        if (!outputPopup.parent) return
        const at = mapToItem(outputPopup.parent, width, height)
        const top = mapToItem(outputPopup.parent, 0, 0).y
        outputPopup.x = Math.max(8, Math.min(at.x - outputPopup.width, outputPopup.parent.width - outputPopup.width - 8))
        outputPopup.y = at.y + outputPopup.implicitHeight + 6 < outputPopup.parent.height - 8
            ? at.y + 6 : Math.max(8, top - outputPopup.implicitHeight - 6)
    }
    onClicked: { positionPopup(); outputPopup.open() }
    onEnabledChanged: if (!enabled) outputPopup.close()
    Popup {
        id: outputPopup
        objectName: "audioOutputPopup"
        parent: Overlay.overlay
        width: parent ? Math.min(360, parent.width - 16) : 360
        padding: 12; focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: { button.positionPopup(); picker.focusList() }
        onHeightChanged: if (visible) button.positionPopup()
        onWidthChanged: if (visible) button.positionPopup()
        onClosed: button.forceActiveFocus()
        background: Rectangle {
            objectName: "shortcutBlocker"
            color: Theme.bgRaised; radius: Theme.radiusMd; border.color: Theme.borderSubtle
        }
        contentItem: AudioOutputPicker {
            id: picker
            controller: button.controller; translator: button.translator; connected: button.connected
        }
        Connections {
            target: outputPopup.parent
            function onWidthChanged() { if (outputPopup.visible) button.positionPopup() }
            function onHeightChanged() { if (outputPopup.visible) button.positionPopup() }
        }
    }
}
