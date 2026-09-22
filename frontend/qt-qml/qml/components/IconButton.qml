import QtQuick
import QtQuick.Controls

Button {
    id: button

    property string kind: "dot"
    property string tooltipText: ""
    property color fillColor: "transparent"
    property color hoverColor: "#221e2c"
    property color pressedColor: "#2d283b"
    property color borderColor: "transparent"
    property color glyphColor: "#9f99ab"
    property color hoverGlyphColor: "#f6f3fa"
    property real iconSize: 18
    property real cornerRadius: 8

    hoverEnabled: true
    implicitWidth: 32
    implicitHeight: 32

    contentItem: Canvas {
        id: iconCanvas
        anchors.centerIn: parent
        width: button.iconSize
        height: button.iconSize
        antialiasing: true

        readonly property color currentStrokeColor: !button.enabled ? "#464152"
                                                    : button.hovered ? button.hoverGlyphColor
                                                    : button.glyphColor

        onCurrentStrokeColorChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const col = currentStrokeColor
            ctx.strokeStyle = col
            ctx.fillStyle = col
            ctx.lineWidth = 1.6
            ctx.lineCap = "round"
            ctx.lineJoin = "round"

            const w = width
            const h = height
            const k = button.kind

            if (k === "play") {
                // Play triangle (optical center shifted slightly right)
                ctx.beginPath()
                ctx.moveTo(w * 0.36, h * 0.22)
                ctx.lineTo(w * 0.76, h * 0.5)
                ctx.lineTo(w * 0.36, h * 0.78)
                ctx.closePath()
                ctx.fill()
            } else if (k === "pause") {
                const barW = w * 0.2
                const barH = h * 0.56
                const y = (h - barH) / 2
                ctx.fillRect(w * 0.24, y, barW, barH)
                ctx.fillRect(w * 0.56, y, barW, barH)
            } else if (k === "previous" || k === "back") {
                // Standard Media Previous: bar + left triangle |◀
                const barW = 2
                const barH = h * 0.52
                const y = (h - barH) / 2
                ctx.fillRect(w * 0.2, y, barW, barH)

                ctx.beginPath()
                ctx.moveTo(w * 0.78, h * 0.24)
                ctx.lineTo(w * 0.32, h * 0.5)
                ctx.lineTo(w * 0.78, h * 0.76)
                ctx.closePath()
                ctx.fill()
            } else if (k === "next" || k === "chevron") {
                // Standard Media Next: right triangle + bar ▶|
                const barW = 2
                const barH = h * 0.52
                const y = (h - barH) / 2
                ctx.fillRect(w * 0.76, y, barW, barH)

                ctx.beginPath()
                ctx.moveTo(w * 0.22, h * 0.24)
                ctx.lineTo(w * 0.68, h * 0.5)
                ctx.lineTo(w * 0.22, h * 0.76)
                ctx.closePath()
                ctx.fill()
            } else if (k === "close") {
                ctx.beginPath()
                ctx.moveTo(w * 0.28, h * 0.28)
                ctx.lineTo(w * 0.72, h * 0.72)
                ctx.moveTo(w * 0.72, h * 0.28)
                ctx.lineTo(w * 0.28, h * 0.72)
                ctx.stroke()
            } else if (k === "plus") {
                ctx.beginPath()
                ctx.moveTo(w * 0.5, h * 0.24)
                ctx.lineTo(w * 0.5, h * 0.76)
                ctx.moveTo(w * 0.24, h * 0.5)
                ctx.lineTo(w * 0.76, h * 0.5)
                ctx.stroke()
            } else if (k === "music") {
                ctx.beginPath()
                ctx.moveTo(w * 0.65, h * 0.2)
                ctx.lineTo(w * 0.65, h * 0.66)
                ctx.moveTo(w * 0.65, h * 0.26)
                ctx.lineTo(w * 0.85, h * 0.2)
                ctx.arc(w * 0.44, h * 0.7, w * 0.2, 0, Math.PI * 2)
                ctx.stroke()
            } else if (k === "search") {
                ctx.beginPath()
                ctx.arc(w * 0.42, h * 0.42, w * 0.24, 0, Math.PI * 2)
                ctx.moveTo(w * 0.6, h * 0.6)
                ctx.lineTo(w * 0.82, h * 0.82)
                ctx.stroke()
            } else if (k === "edit") {
                ctx.beginPath()
                ctx.moveTo(w * 0.22, h * 0.78)
                ctx.lineTo(w * 0.34, h * 0.76)
                ctx.lineTo(w * 0.76, h * 0.34)
                ctx.lineTo(w * 0.66, h * 0.24)
                ctx.lineTo(w * 0.24, h * 0.66)
                ctx.closePath()
                ctx.stroke()
            } else if (k === "trash") {
                ctx.beginPath()
                ctx.moveTo(w * 0.24, h * 0.32)
                ctx.lineTo(w * 0.76, h * 0.32)
                ctx.moveTo(w * 0.38, h * 0.32)
                ctx.lineTo(w * 0.38, h * 0.22)
                ctx.lineTo(w * 0.62, h * 0.22)
                ctx.lineTo(w * 0.62, h * 0.32)
                ctx.moveTo(w * 0.3, h * 0.32)
                ctx.lineTo(w * 0.34, h * 0.8)
                ctx.lineTo(w * 0.66, h * 0.8)
                ctx.lineTo(w * 0.7, h * 0.32)
                ctx.stroke()
            } else if (k === "more") {
                ctx.beginPath()
                ctx.arc(w * 0.25, h * 0.5, 1.5, 0, Math.PI * 2)
                ctx.arc(w * 0.5, h * 0.5, 1.5, 0, Math.PI * 2)
                ctx.arc(w * 0.75, h * 0.5, 1.5, 0, Math.PI * 2)
                ctx.fill()
            } else if (k === "move") {
                ctx.beginPath()
                ctx.moveTo(w * 0.22, h * 0.4)
                ctx.lineTo(w * 0.54, h * 0.4)
                ctx.lineTo(w * 0.54, h * 0.24)
                ctx.lineTo(w * 0.82, h * 0.5)
                ctx.lineTo(w * 0.54, h * 0.76)
                ctx.lineTo(w * 0.54, h * 0.6)
                ctx.lineTo(w * 0.22, h * 0.6)
                ctx.closePath()
                ctx.stroke()
            } else if (k === "volume") {
                ctx.beginPath()
                ctx.moveTo(w * 0.2, h * 0.4)
                ctx.lineTo(w * 0.36, h * 0.4)
                ctx.lineTo(w * 0.55, h * 0.24)
                ctx.lineTo(w * 0.55, h * 0.76)
                ctx.lineTo(w * 0.36, h * 0.6)
                ctx.lineTo(w * 0.2, h * 0.6)
                ctx.closePath()
                ctx.fill()
                ctx.beginPath()
                ctx.arc(w * 0.52, h * 0.5, w * 0.22, -Math.PI * 0.3, Math.PI * 0.3)
                ctx.stroke()
            } else if (k === "lyrics") {
                ctx.beginPath()
                ctx.moveTo(w * 0.22, h * 0.32)
                ctx.lineTo(w * 0.78, h * 0.32)
                ctx.moveTo(w * 0.22, h * 0.5)
                ctx.lineTo(w * 0.62, h * 0.5)
                ctx.moveTo(w * 0.22, h * 0.68)
                ctx.lineTo(w * 0.74, h * 0.68)
                ctx.stroke()
            } else if (k === "queue") {
                ctx.beginPath()
                ctx.moveTo(w * 0.22, h * 0.32)
                ctx.lineTo(w * 0.78, h * 0.32)
                ctx.moveTo(w * 0.22, h * 0.5)
                ctx.lineTo(w * 0.78, h * 0.5)
                ctx.moveTo(w * 0.22, h * 0.68)
                ctx.lineTo(w * 0.78, h * 0.68)
                ctx.stroke()
            } else {
                ctx.beginPath()
                ctx.arc(w / 2, h / 2, 2, 0, Math.PI * 2)
                ctx.fill()
            }
        }

        Connections {
            target: button
            function onKindChanged() { iconCanvas.requestPaint() }
            function onEnabledChanged() { iconCanvas.requestPaint() }
        }
    }

    background: Rectangle {
        radius: button.cornerRadius
        color: !button.enabled ? "transparent"
               : button.down ? button.pressedColor
               : button.hovered ? button.hoverColor
               : button.fillColor
        border.color: button.enabled ? button.borderColor : "transparent"
        border.width: 1

        Behavior on color {
            ColorAnimation { duration: 100 }
        }
    }
}
