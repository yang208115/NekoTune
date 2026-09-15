import QtQuick
import QtQuick.Controls

Slider {
    id: slider

    property color activeColor: "#f2c86b"
    property color baseColor: "#343840"

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
        color: "#fff4cf"
        border.color: "#14201f"
        border.width: 2
    }
}
