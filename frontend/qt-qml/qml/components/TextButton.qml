import QtQuick
import QtQuick.Controls

Button {
    id: button

    property color fillColor: "#f2c86b"
    property color hoverColor: "#ffd978"
    property color pressedColor: "#d7ad50"
    property color borderColor: "#ffe193"
    property color labelColor: "#17140c"

    hoverEnabled: true
    implicitHeight: 42
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
        radius: 8
        color: !button.enabled ? "#25231f" : button.down ? button.pressedColor : button.hovered ? button.hoverColor : button.fillColor
        border.color: button.enabled ? button.borderColor : "#363229"
        border.width: 1
    }
}
