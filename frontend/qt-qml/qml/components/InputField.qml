import QtQuick
import QtQuick.Controls

TextField {
    id: field
    implicitHeight: 40
    leftPadding: 12
    rightPadding: 12
    color: "#F5F1FA"
    placeholderTextColor: "#AAA0B8"
    selectionColor: "#CBB8FF"
    selectedTextColor: "#21172F"
    font.pixelSize: 14
    selectByMouse: true
    background: Rectangle {
        radius: 8
        color: "#17141F"
        border.color: field.activeFocus ? "#CBB8FF" : "#8D809F"
        border.width: field.activeFocus ? 2 : 1
    }
}
