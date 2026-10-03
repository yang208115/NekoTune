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
    // Anchor coordinates may come from a button or a right-click position.
    // Map them into the overlay so page clipping cannot crop the popup.
    // Lay out repeated actions before measuring the menu's implicit height.
    // Flip at right/bottom edges, then clamp within the window's safe margin.
    // The popup blocks shell shortcuts while keyboard focus belongs to it.
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
    background: Rectangle { objectName: "shortcutBlocker"; color: Theme.bgRaised; radius: Theme.radiusMd; border.color: Theme.borderSubtle }
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
                subtleText: modelData.destructive ? Theme.statusError : Theme.textSecondary
                onClicked: { menu.close(); menu.chosen(modelData.key) }
            }
        }
    }
}
