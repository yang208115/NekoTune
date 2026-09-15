import QtQuick
import QtQuick.Controls

Button {
    id: button

    property bool subtle: false
    property color fillColor: subtle ? "#202831" : "#f2c86b"
    property color hoverColor: subtle ? "#2b3742" : "#ffd978"
    property color pressedColor: subtle ? "#34434f" : "#d7ad50"
    property color borderColor: subtle ? "#3b4b57" : "#ffe193"
    property color labelColor: subtle ? "#cbd4d5" : "#17140c"

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
        color: !button.enabled ? "#1d2227" : button.down ? button.pressedColor : button.hovered ? button.hoverColor : button.fillColor
        border.color: button.enabled ? button.borderColor : "#303840"
        border.width: 1
    }
}
