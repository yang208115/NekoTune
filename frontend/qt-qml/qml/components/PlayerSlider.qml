import QtQuick
import QtQuick.Controls

// This visual control is shared by continuous volume and deferred seeking.
// Keep hit geometry fixed even when the visible handle animates.
// That prevents the cursor-to-value mapping from shifting during interaction.
// Its consumer decides whether to submit continuously or on release.
// Keyboard focus remains visible independently of hover state.
Slider {
    id: slider

    property color activeColor: Theme.accentPrimary
    property color baseColor: Theme.borderSubtle

    implicitHeight: 32
    hoverEnabled: true

    // Large hit area background (hit area >= 24px, visual track = 4px)
    background: Item {
        x: slider.leftPadding
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        width: slider.availableWidth
        height: 24

        // Visual track: 4px fixed height
        Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: 4
            radius: 2
            color: slider.baseColor

            // Active track
            Rectangle {
                width: Math.max(0, slider.visualPosition * parent.width)
                height: parent.height
                radius: 2
                color: slider.activeColor
            }
        }
    }

    // Always visible, beautifully tactile handle: fixed 16px geometry, 10px / 14px dot
    handle: Item {
        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        // Keep input geometry stable while the visible handle grows on hover.
        width: 16
        height: width

        Rectangle {
            anchors.centerIn: parent
            width: slider.pressed || slider.hovered ? 14 : 10
            height: width
            radius: width / 2
            color: Theme.textPrimary
            border.color: Theme.textOnAccent
            border.width: 2

            // Focus ring (Section 7.1 & 11)
            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                radius: parent.radius + 4
                color: "transparent"
                border.color: Theme.accentPrimary
                border.width: 2
                visible: slider.activeFocus
            }

            Behavior on width {
                NumberAnimation { duration: 100 }
            }
        }
    }
}
