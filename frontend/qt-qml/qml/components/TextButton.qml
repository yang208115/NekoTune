import QtQuick
import QtQuick.Controls

Button {
    id: button

    property bool subtle: false
    property color lavenderColor: Theme.accentPrimary
    property color subtleBg: Theme.bgSurface
    property color subtleHoverBg: Theme.bgHover
    property color subtleBorder: Theme.borderSubtle
    property color subtleText: Theme.textSecondary
    property color primaryText: Theme.textOnAccent
    property real cornerRadius: Theme.radiusSm

    hoverEnabled: true
    implicitHeight: 40
    leftPadding: 16
    rightPadding: 16
    font.pixelSize: Theme.fontBody
    font.weight: Font.DemiBold

    Accessible.role: Accessible.Button
    Accessible.name: button.text

    contentItem: Text {
        textFormat: Text.PlainText
        text: button.text
        color: !button.enabled ? Theme.textDisabled : button.subtle ? button.subtleText : button.primaryText
        font: button.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        id: bgRect
        radius: button.cornerRadius
        border.width: button.subtle ? 1 : 0
        border.color: !button.enabled ? Theme.bgHover
                      : button.hovered ? Theme.borderControl
                      : button.subtleBorder

        color: button.subtle ? (!button.enabled ? Theme.bgSurface
                                : button.down ? Theme.bgSelected
                                : button.hovered ? button.subtleHoverBg
                                : button.subtleBg)
                             : (!button.enabled ? Theme.borderSubtle
                                : button.down ? Theme.accentPressed
                                : button.hovered ? Theme.accentHover
                                : button.lavenderColor)

        // Keyboard focus ring (Section 7.1 & 11)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: bgRect.radius + 4
            color: "transparent"
            border.color: Theme.accentPrimary
            border.width: 2
            visible: button.activeFocus
        }

        Behavior on color {
            ColorAnimation { duration: Theme.reducedMotion ? 0 : Theme.durationFast }
        }
    }
}
