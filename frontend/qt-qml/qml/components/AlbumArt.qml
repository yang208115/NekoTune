import QtQuick
import QtQuick.Controls

Item {
    id: root
    property bool hasSong: false
    property string playbackState: "stopped"
    property color textMutedColor: "#9b96a8"
    property color mintColor: "#cbb8ff"
    property color amberColor: "#e8a9c3"

    Rectangle { anchors.centerIn: parent; width: Math.min(parent.width, parent.height); height: width; radius: 26; color: "#282332"; border.color: "#403752"; border.width: 1
        Image { anchors.fill: parent; anchors.margins: 7; source: "qrc:/artwork/default-cover.png"; fillMode: Image.PreserveAspectCrop; smooth: true; mipmap: true; opacity: root.hasSong ? 1 : 0.48; layer.enabled: true }
        Rectangle { anchors.fill: parent; radius: parent.radius; color: Qt.rgba(0.07, 0.05, 0.11, root.hasSong ? 0.08 : 0.40) }
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 98; radius: 26; gradient: Gradient { GradientStop { position: 0; color: "#00100c16" } GradientStop { position: 1; color: "#dd100c16" } } }
        Column { anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.leftMargin: 22; anchors.bottomMargin: 19; spacing: 4
            Label { text: root.hasSong ? i18n.text(root.playbackState, i18n.language).toUpperCase() : i18n.text("ready", i18n.language).toUpperCase(); color: root.hasSong ? root.mintColor : root.textMutedColor; font.pixelSize: 10; font.weight: Font.Bold; font.letterSpacing: 1.5 }
            Label { text: "LOCAL SESSION"; color: "#c6bed2"; font.pixelSize: 9; font.letterSpacing: 1.1 }
        }
    }
}
