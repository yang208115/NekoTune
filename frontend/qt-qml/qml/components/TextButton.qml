import QtQuick
import QtQuick.Controls

Button {
    id: button

    property bool subtle: false
    property color lavenderColor: "#CBB8FF"
    property color subtleBg: "#17141F"
    property color subtleHoverBg: "#2A2338"
    property color subtleBorder: "#332C41"
    property color subtleText: "#D7CFE2"
    property color primaryText: "#21172F"
    property real cornerRadius: 8

    hoverEnabled: true
    implicitHeight: 40
    leftPadding: 16
    rightPadding: 16
    font.pixelSize: 13
    font.weight: Font.DemiBold

    Accessible.role: Accessible.Button
    Accessible.name: button.text

    contentItem: Text {
        textFormat: Text.PlainText
        text: button.text
        color: !button.enabled ? "#736A83" : button.subtle ? button.subtleText : button.primaryText
        font: button.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        id: bgRect
        radius: button.cornerRadius
        border.width: button.subtle ? 1 : 0
        border.color: !button.enabled ? "#2A2338"
                      : button.hovered ? "#8D809F"
                      : button.subtleBorder

        color: button.subtle ? (!button.enabled ? "#17141F"
                                : button.down ? "#322743"
                                : button.hovered ? button.subtleHoverBg
                                : button.subtleBg)
                             : (!button.enabled ? "#332C41"
                                : button.down ? "#B7A0ED"
                                : button.hovered ? "#DBCDFF"
                                : button.lavenderColor)

        // Keyboard focus ring (Section 7.1 & 11)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: bgRect.radius + 4
            color: "transparent"
            border.color: "#CBB8FF"
            border.width: 2
            visible: button.activeFocus
        }

        Behavior on color {
            ColorAnimation { duration: 120 }
        }
    }
}
