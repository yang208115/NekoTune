import QtQuick
import QtQuick.Controls

Button {
    id: button

    property bool subtle: false
    property color lavenderColor: "#cbb8ff"
    property color subtleBg: "#201c2b"
    property color subtleHoverBg: "#2b253a"
    property color subtleBorder: "#38314a"
    property color subtleText: "#d8d2e4"
    property color primaryText: "#161220"
    property real cornerRadius: 10

    hoverEnabled: true
    implicitHeight: 34
    leftPadding: 14
    rightPadding: 14
    font.pixelSize: 12
    font.weight: Font.DemiBold

    contentItem: Text {
        text: button.text
        color: !button.enabled ? "#524b61" : button.subtle ? button.subtleText : button.primaryText
        font: button.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: button.cornerRadius
        border.width: button.subtle ? 1 : 0
        border.color: !button.enabled ? "#282335"
                      : button.hovered ? "#4f4566"
                      : button.subtleBorder

        color: button.subtle ? (!button.enabled ? "#161320"
                                : button.down ? "#1b1724"
                                : button.hovered ? button.subtleHoverBg
                                : button.subtleBg)
                             : (!button.enabled ? "#332c42"
                                : button.down ? "#bba4f2"
                                : button.hovered ? "#d8c9ff"
                                : button.lavenderColor)

        Behavior on color {
            ColorAnimation { duration: 120 }
        }
    }
}
