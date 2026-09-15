import QtQuick
import QtQuick.Controls

Slider {
    id: slider

    property color activeColor: "#f2c86b"
    property color baseColor: "#343840"

    implicitHeight: 28

    background: Rectangle {
        x: slider.leftPadding
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        width: slider.availableWidth
        height: 5
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
        width: slider.pressed ? 18 : 14
        height: width
        radius: width / 2
        color: "#fff2c2"
        border.color: "#1a1710"
        border.width: 2
    }
}
