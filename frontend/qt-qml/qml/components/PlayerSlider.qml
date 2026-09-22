import QtQuick
import QtQuick.Controls

Slider {
    id: slider

    property color activeColor: "#cbb8ff"
    property color baseColor: "#3a3547"

    implicitHeight: 30

    background: Rectangle {
        x: slider.leftPadding
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        width: slider.availableWidth
        height: 6
        radius: 3
        color: slider.baseColor

        Rectangle {
            width: slider.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: slider.activeColor
        }
    }

    handle: Rectangle {
        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        width: slider.pressed ? 19 : 15
        height: width
        radius: width / 2
        color: "#f7f4fb"
        border.color: "#1b1525"
        border.width: 2
    }
}
