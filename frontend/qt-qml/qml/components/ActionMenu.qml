import QtQuick
import QtQuick.Controls

Popup {
    id: menu
    property var actions: []
    signal chosen(string action)
    parent: Overlay.overlay
    width: 260
    padding: 6
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    function openAt(anchor, px, py) {
        const point = anchor.mapToItem(parent, px === undefined ? 0 : px,
                                       py === undefined ? anchor.height : py)
        x = Math.max(8, Math.min(point.x, parent.width - width - 8))
        y = Math.max(8, Math.min(point.y, parent.height - implicitHeight - 8))
        open()
    }
    background: Rectangle { objectName: "shortcutBlocker"; color: "#211C2D"; radius: 12; border.color: "#332C41" }
    contentItem: Column {
        spacing: 2
        Repeater {
            model: menu.actions
            delegate: TextButton {
                required property var modelData
                width: parent.width
                objectName: modelData.objectName || "menu_" + modelData.key
                text: modelData.label
                enabled: modelData.enabled === undefined || Boolean(modelData.enabled)
                subtle: true
                subtleBg: "transparent"
                subtleBorder: "transparent"
                subtleText: modelData.destructive ? "#FF9BAE" : "#D7CFE2"
                onClicked: { menu.close(); menu.chosen(modelData.key) }
            }
        }
    }
}
