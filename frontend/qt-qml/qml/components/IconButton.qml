import QtQuick
import QtQuick.Controls

Button {
    id: button
    property string kind: "dot"
    property string tooltipText: ""
    property color fillColor: "#201c2b"
    property color hoverColor: "#2d2740"
    property color pressedColor: "#393052"
    property color borderColor: "#3a334b"
    property color glyphColor: "#c6c0d1"
    hoverEnabled: true
    implicitWidth: 38
    implicitHeight: 38
    ToolTip.visible: hovered && tooltipText.length > 0
    ToolTip.delay: 450
    ToolTip.text: tooltipText
    contentItem: Canvas {
        anchors.centerIn: parent
        width: 19; height: 19
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset(); ctx.strokeStyle = button.enabled ? button.glyphColor : "#625d6b"; ctx.fillStyle = ctx.strokeStyle; ctx.lineWidth = 1.8; ctx.lineCap = "round"; ctx.lineJoin = "round"
            const w = width, h = height
            if (button.kind === "play") { ctx.beginPath(); ctx.moveTo(6, 3); ctx.lineTo(16, h / 2); ctx.lineTo(6, h - 3); ctx.closePath(); ctx.fill() }
            else if (button.kind === "pause") { ctx.fillRect(4, 3, 4, h - 6); ctx.fillRect(11, 3, 4, h - 6) }
            else if (button.kind === "back") { ctx.beginPath(); ctx.moveTo(16, h / 2); ctx.lineTo(4, h / 2); ctx.moveTo(8, 4); ctx.lineTo(4, h / 2); ctx.lineTo(8, h - 4); ctx.stroke() }
            else if (button.kind === "chevron") { ctx.beginPath(); ctx.moveTo(7, 4); ctx.lineTo(13, h / 2); ctx.lineTo(7, h - 4); ctx.stroke() }
            else if (button.kind === "close") { ctx.beginPath(); ctx.moveTo(5, 5); ctx.lineTo(14, 14); ctx.moveTo(14, 5); ctx.lineTo(5, 14); ctx.stroke() }
            else if (button.kind === "plus") { ctx.beginPath(); ctx.moveTo(w / 2, 4); ctx.lineTo(w / 2, h - 4); ctx.moveTo(4, h / 2); ctx.lineTo(w - 4, h / 2); ctx.stroke() }
            else if (button.kind === "folder") { ctx.beginPath(); ctx.moveTo(2, 6); ctx.lineTo(8, 6); ctx.lineTo(10, 8); ctx.lineTo(17, 8); ctx.lineTo(16, 16); ctx.lineTo(2, 16); ctx.closePath(); ctx.stroke() }
            else { ctx.beginPath(); ctx.arc(w / 2, h / 2, 2, 0, Math.PI * 2); ctx.fill() }
        }
        Connections { target: button; function onKindChanged() { button.contentItem.requestPaint() } function onGlyphColorChanged() { button.contentItem.requestPaint() } function onEnabledChanged() { button.contentItem.requestPaint() } }
    }
    background: Rectangle { radius: 12; color: !button.enabled ? "#17151d" : button.down ? button.pressedColor : button.hovered ? button.hoverColor : button.fillColor; border.color: button.enabled ? button.borderColor : "#292532"; border.width: 1 }
}
