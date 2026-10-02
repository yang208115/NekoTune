import QtQuick
import QtQuick.Controls

// The backend supplies one canonical artwork URL for this song.
// Render the local placeholder underneath the asynchronous image.
// Show the replacement only after Image.Ready to avoid empty/error frames.
// No-song state has its own artwork rather than reusing a stale URL.
// Component sizing follows its container without changing cover precedence.
Item {
    id: root

    property bool hasSong: false
    property string coverUrl: ""
    property string playbackState: "stopped"
    property real maxDimension: 280
    property color lavenderColor: "#CBB8FF"
    property color roseColor: "#E8A9C3"

    readonly property bool isPlaying: playbackState === "playing"
    readonly property real artSize: Math.min(Math.min(parent.width, parent.height), root.maxDimension)

    // Ambient glow behind cover (Section 3.2: 8% moonlight purple)
    Rectangle {
        anchors.centerIn: coverCard
        width: coverCard.width + 20
        height: coverCard.height + 20
        radius: coverCard.radius + 6
        color: root.hasSong ? Qt.rgba(0.796, 0.722, 1.0, 0.08) : "transparent"
        z: -1
    }

    Rectangle {
        id: coverCard
        anchors.centerIn: parent
        width: Math.max(140, root.artSize)
        height: width
        radius: 16
        color: "#17141F"
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
                color: "#121019"
            }

            Canvas {
                anchors.centerIn: parent
                width: 48
                height: 48
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = "#332C41"
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
