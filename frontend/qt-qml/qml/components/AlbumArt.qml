import QtQuick
import QtQuick.Controls

Item {
    id: root

    property bool hasSong: false
    property string playbackState: "stopped"
    property color textMutedColor: "#8e938f"
    property color mintColor: "#7adfc6"
    property color amberColor: "#f2c86b"

    Rectangle {
        id: albumFrame
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height, 360)
        height: width
        radius: 14
        color: "#24282f"
        border.color: "#3b424c"
        border.width: 1

        Rectangle {
            anchors.fill: parent
            anchors.margins: 16
            radius: width / 2
            color: "#101214"
            border.color: "#303842"
            border.width: 2

            Repeater {
                model: 6

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * (0.9 - index * 0.1)
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.color: Qt.rgba(1, 1, 1, 0.035)
                    border.width: 1
                }
            }

            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.42
                height: width
                radius: width / 2
                color: root.hasSong ? root.mintColor : "#454b50"

                Text {
                    anchors.centerIn: parent
                    text: root.hasSong ? "N" : "+"
                    color: "#101214"
                    font.pixelSize: Math.max(44, parent.width * 0.42)
                    font.weight: Font.Black
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 70
            radius: 14
            color: Qt.rgba(0.08, 0.09, 0.1, 0.88)

            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                text: i18n.text(root.playbackState, i18n.language).toUpperCase()
                color: root.hasSong ? root.amberColor : root.textMutedColor
                elide: Text.ElideRight
                font.pixelSize: 12
                font.weight: Font.Black
            }
        }
    }
}
