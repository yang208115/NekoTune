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
        menuContent.forceLayout()
        const atPointer = px !== undefined || py !== undefined
        const gap = atPointer ? 0 : 6
        const point = anchor.mapToItem(parent, px === undefined ? anchor.width : px,
                                       py === undefined ? anchor.height : py)
        const top = atPointer ? point.y : anchor.mapToItem(parent, 0, 0).y
        const menuHeight = implicitHeight
        let targetX = atPointer ? point.x : point.x - width
        let targetY = point.y + gap
        if (atPointer && targetX + width > parent.width - 8)
            targetX = point.x - width
        if (targetY + menuHeight > parent.height - 8)
            targetY = top - gap - menuHeight
        x = Math.max(8, Math.min(targetX, parent.width - width - 8))
        y = Math.max(8, Math.min(targetY, parent.height - menuHeight - 8))
        open()
    }
    background: Rectangle { objectName: "shortcutBlocker"; color: "#211C2D"; radius: 12; border.color: "#332C41" }
    contentItem: Column {
        id: menuContent
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
