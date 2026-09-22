import QtQuick
import QtQuick.Controls

Button {
    id: button

    property bool subtle: false
    property color fillColor: subtle ? "#201c2b" : "#cbb8ff"
    property color hoverColor: subtle ? "#2d2740" : "#ddceff"
    property color pressedColor: subtle ? "#393052" : "#b39de6"
    property color borderColor: subtle ? "#3a334b" : "#cbb8ff"
    property color labelColor: subtle ? "#d0c9dc" : "#1b1525"

    hoverEnabled: true
    implicitHeight: 40
    leftPadding: 16
    rightPadding: 16
    font.pixelSize: 14
    font.weight: Font.DemiBold

    contentItem: Text {
        text: button.text
        color: button.enabled ? button.labelColor : "#6a6255"
        font: button.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 12
        color: !button.enabled ? "#191720" : button.down ? button.pressedColor : button.hovered ? button.hoverColor : button.fillColor
        border.color: button.enabled ? button.borderColor : "#2b2635"
        border.width: 1
    }
}
