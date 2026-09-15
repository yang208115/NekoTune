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
        width: Math.min(parent.width, parent.height, 380)
        height: width
        radius: 22
        color: "#1b252b"
        border.color: "#34444b"
        border.width: 1

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: parent.radius - 1
            color: "transparent"
            border.color: Qt.rgba(0.48, 0.88, 0.78, 0.14)
            border.width: 1
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 18
            radius: width / 2
            color: "#0d1216"
            border.color: "#27323a"
            border.width: 3

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
                color: root.hasSong ? root.mintColor : "#4a555d"

                Text {
                    anchors.centerIn: parent
                    text: root.hasSong ? "N" : "+"
                    color: "#101214"
                    font.pixelSize: Math.max(44, parent.width * 0.38)
                    font.weight: Font.Black
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 76
            radius: 22
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0.04, 0.06, 0.07, 0.15) }
                GradientStop { position: 1.0; color: Qt.rgba(0.04, 0.06, 0.07, 0.95) }
            }

            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 22
                anchors.rightMargin: 22
                text: (root.hasSong ? "•  " : "+  ") + i18n.text(root.playbackState, i18n.language).toUpperCase()
                color: root.hasSong ? root.amberColor : root.textMutedColor
                elide: Text.ElideRight
                font.pixelSize: 11
                font.weight: Font.Black
                font.letterSpacing: 1.2
            }
        }
    }
}
