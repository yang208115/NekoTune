import QtQuick
import QtQuick.Controls

TextField {
    id: field
    implicitHeight: 40
    leftPadding: 12
    rightPadding: 12
    color: Theme.textPrimary
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accentPrimary
    selectedTextColor: Theme.textOnAccent
    font.pixelSize: Theme.fontBody
    selectByMouse: true
    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.bgSurface
        border.color: field.activeFocus ? Theme.accentPrimary : Theme.borderControl
        border.width: field.activeFocus ? 2 : 1
    }
}
