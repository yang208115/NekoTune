import QtQuick
import QtQuick.Controls

Slider {
    id: slider

    property color activeColor: "#cbb8ff"
    property color baseColor: "#2b253a"

    implicitHeight: 32
    hoverEnabled: true

    // Large hit area background
    background: Item {
        x: slider.leftPadding
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        width: slider.availableWidth
        height: slider.hovered || slider.pressed ? 7 : 5

        Behavior on height {
            NumberAnimation { duration: 100; easing.type: Easing.OutQuad }
        }

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: slider.baseColor

            // Active track
            Rectangle {
                width: Math.max(0, slider.visualPosition * parent.width)
                height: parent.height
                radius: height / 2
                color: slider.activeColor
            }
        }
    }

    // Always visible, beautifully tactile handle
    handle: Item {
        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        // Keep input geometry stable while the visible handle grows on hover.
        width: 16
        height: width

        Rectangle {
            anchors.centerIn: parent
            width: slider.pressed ? 16 : slider.hovered ? 15 : 13
            height: width
            radius: width / 2
            color: "#ffffff"
            border.color: "#181424"
            border.width: 2

            Behavior on width {
                NumberAnimation { duration: 100 }
            }
        }
    }
}
