import QtQuick
import QtQuick.Controls

Button {
    id: button

    property string tooltipText: ""
    property color fillColor: "#262a30"
    property color hoverColor: "#30353c"
    property color pressedColor: "#3a4048"
    property color borderColor: "#3b414a"
    property color labelColor: "#f4f0e8"

    hoverEnabled: true
    implicitWidth: 42
    implicitHeight: 38
    leftPadding: 0
    rightPadding: 0
    font.pixelSize: 15
    font.weight: Font.DemiBold

    ToolTip.visible: hovered && tooltipText.length > 0
    ToolTip.delay: 450
    ToolTip.text: tooltipText

    contentItem: Text {
        text: button.text
        color: button.enabled ? button.labelColor : "#666d70"
        font: button.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 8
        color: !button.enabled ? "#1b1e22" : button.down ? button.pressedColor : button.hovered ? button.hoverColor : button.fillColor
        border.color: button.enabled ? button.borderColor : "#272b30"
        border.width: 1
    }
}
