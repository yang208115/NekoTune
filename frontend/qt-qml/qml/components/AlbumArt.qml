import QtQuick
import QtQuick.Controls

Item {
    id: root

    property bool hasSong: false
    property string coverUrl: ""
    property string playbackState: "stopped"
    property real maxDimension: 260
    property color lavenderColor: "#cbb8ff"
    property color roseColor: "#e8a9c3"

    readonly property bool isPlaying: playbackState === "playing"
    readonly property real artSize: Math.min(Math.min(parent.width, parent.height), root.maxDimension)

    Rectangle {
        id: coverCard
        anchors.centerIn: parent
        width: Math.max(140, root.artSize)
        height: width
        radius: 16
        color: "#1a1724"
        clip: true

        // Image if has song
        Image {
            anchors.fill: parent
            source: "qrc:/artwork/default-cover.png"
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
            visible: root.hasSong
        }

        Image {
            anchors.fill: parent
            source: root.hasSong ? root.coverUrl : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            smooth: true
            mipmap: true
            visible: root.hasSong && status === Image.Ready
        }

        // Placeholder artwork if no song
        Item {
            anchors.fill: parent
            visible: !root.hasSong

            Rectangle {
                anchors.fill: parent
                color: "#16131f"
            }

            Canvas {
                anchors.centerIn: parent
                width: 48
                height: 48
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = "#383147"
                    ctx.lineWidth = 2
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    const w = width, h = height
                    // Music note icon
                    ctx.beginPath()
                    ctx.moveTo(w * 0.7, h * 0.2)
                    ctx.lineTo(w * 0.7, h * 0.65)
                    ctx.moveTo(w * 0.7, h * 0.28)
                    ctx.lineTo(w * 0.9, h * 0.22)
                    ctx.stroke()
                    ctx.beginPath()
                    ctx.arc(w * 0.5, h * 0.7, w * 0.2, 0, Math.PI * 2)
                    ctx.stroke()
                }
            }
        }

        // Inner subtle border highlight
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.color: Qt.rgba(1, 1, 1, 0.07)
            border.width: 1
        }
    }
}
