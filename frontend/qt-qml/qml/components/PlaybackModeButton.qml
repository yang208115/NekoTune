pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

// The icon/checkmark follows the backend-confirmed playbackMode property.
// Selecting an option only emits a request, never assumes persistence success.
// Menu keyboard selection is temporary state separate from confirmed mode.
// Disable closes the popup when transport availability changes.
// Closing returns focus to the button for predictable keyboard navigation.
IconButton {
    id: button
    property string playbackMode: "sequential"
    required property var translator
    readonly property var modes: ["sequential", "repeat_one", "shuffle", "repeat_all"]
    signal modeRequested(string mode)
    objectName: "playbackModeButton"
    kind: playbackMode
    tooltipText: t("playback_order") + ": " + t("playback_mode_" + playbackMode)
    function t(key) { return translator.text(key, translator.language) }
    onClicked: {
        modeMenu.currentIndex = Math.max(0, modes.indexOf(playbackMode))
        const point = button.mapToItem(modeMenu.parent, button.width, button.height)
        const top = button.mapToItem(modeMenu.parent, 0, 0).y
        modeMenu.x = Math.max(8, Math.min(point.x - modeMenu.width, modeMenu.parent.width - modeMenu.width - 8))
        modeMenu.y = point.y + modeMenu.implicitHeight + 6 <= modeMenu.parent.height - 8
            ? point.y + 6 : Math.max(8, top - modeMenu.implicitHeight - 6)
        modeMenu.open()
    }
    onEnabledChanged: if (!enabled) modeMenu.close()
    Popup {
        id: modeMenu
        objectName: "playbackModeMenu"
        property int currentIndex: 0
        parent: Overlay.overlay
        width: 220
        padding: 6
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: contentItem.forceActiveFocus()
        onClosed: button.forceActiveFocus()
        function choose(index) {
            if (!button.enabled) return
            close()
            button.modeRequested(button.modes[index])
        }
        background: Rectangle {
            objectName: "shortcutBlocker"
            color: "#211C2D"; radius: 12; border.color: "#332C41"
        }
        contentItem: Column {
            spacing: 2
            focus: true
            Keys.onDownPressed: modeMenu.currentIndex = (modeMenu.currentIndex + 1) % button.modes.length
            Keys.onUpPressed: modeMenu.currentIndex = (modeMenu.currentIndex + button.modes.length - 1) % button.modes.length
            Keys.onReturnPressed: modeMenu.choose(modeMenu.currentIndex)
            Keys.onEnterPressed: modeMenu.choose(modeMenu.currentIndex)
            Keys.onSpacePressed: modeMenu.choose(modeMenu.currentIndex)
            Repeater {
                model: button.modes
                delegate: TextButton {
                    required property string modelData
                    required property int index
                    readonly property bool selected: button.playbackMode === modelData
                    objectName: "playbackMode_" + modelData
                    width: parent.width
                    text: (selected ? "✓  " : "    ") + button.t("playback_mode_" + modelData)
                    subtle: true
                    subtleBg: modeMenu.currentIndex === index ? "#322743" : "transparent"
                    subtleBorder: "transparent"
                    subtleText: selected ? "#CBB8FF" : "#D7CFE2"
                    Accessible.role: Accessible.MenuItem
                    Accessible.name: button.t("playback_mode_" + modelData)
                    Accessible.checkable: true
                    Accessible.checked: selected
                    onHoveredChanged: if (hovered) modeMenu.currentIndex = index
                    onClicked: modeMenu.choose(index)
                }
            }
        }
    }
}
