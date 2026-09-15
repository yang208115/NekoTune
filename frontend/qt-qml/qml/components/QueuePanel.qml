import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var queue: []
    property color panelColor: "#191b1f"
    property color lineColor: "#30343b"
    property color textStrongColor: "#f4f0e8"
    property color textSoftColor: "#c5beb2"
    property color textMutedColor: "#8e938f"
    property color mintColor: "#7adfc6"
    property color amberColor: "#f2c86b"
    property color coralColor: "#f0948e"

    signal addRequested()
    signal clearRequested()
    signal playRequested(int queueId)
    signal togglePlayPauseRequested()
    signal removeRequested(int queueId)

    readonly property bool isPlaying: root.playbackState === "playing"
    property string playbackState: "stopped"

    radius: 20
    color: "#141b21"
    border.color: lineColor
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Label {
                    text: i18n.text("queue", i18n.language)
                    color: root.textStrongColor
                    font.pixelSize: 24
                    font.weight: Font.Bold
                    font.letterSpacing: 0.3
                }

                Label {
                    text: i18n.countText("tracks", root.queue.length, i18n.language)
                    color: root.textMutedColor
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }

            IconButton {
                text: "+"
                tooltipText: i18n.text("add_music", i18n.language)
                fillColor: "#2c281d"
                hoverColor: "#383222"
                pressedColor: "#433a28"
                borderColor: "#594d31"
                labelColor: root.amberColor
                onClicked: root.addRequested()
            }

            IconButton {
                text: "×"
                tooltipText: i18n.text("clear_queue", i18n.language)
                enabled: root.queue.length > 0
                fillColor: "#2b2223"
                hoverColor: "#37292a"
                pressedColor: "#442f30"
                borderColor: "#50383a"
                labelColor: root.coralColor
                onClicked: root.clearRequested()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: root.lineColor
            opacity: 0.75
        }

        ListView {
            id: queueList

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            model: root.queue

            delegate: Rectangle {
                required property var modelData

                width: queueList.width
                id: delegateRoot
                height: 74
                radius: 14
                property bool hoveredRow: false
                color: modelData.state === "current" ? "#1d3833" : (hoveredRow ? "#202b34" : "#1a2229")
                border.color: modelData.state === "current" ? root.mintColor : (hoveredRow ? "#3a4d59" : "#27323b")
                border.width: 1

                Behavior on color { ColorAnimation { duration: 130 } }
                Behavior on border.color { ColorAnimation { duration: 130 } }

                HoverHandler {
                    onHoveredChanged: delegateRoot.hoveredRow = hovered
                }

                Rectangle {
                    visible: modelData.state === "current"
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 3
                    radius: 2
                    color: root.mintColor
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 10
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                        radius: 12
                        color: modelData.state === "current" ? root.mintColor : "#25303a"

                        Text {
                            anchors.centerIn: parent
                            text: modelData.state === "current" ? "♪" : String(Number(modelData.position || 0) + 1)
                            color: modelData.state === "current" ? "#10201b" : root.textSoftColor
                            font.pixelSize: modelData.state === "current" ? 19 : 12
                            font.weight: Font.Black
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3

                        Label {
                            Layout.fillWidth: true
                            text: modelData.title || i18n.text("untitled", i18n.language)
                            color: root.textStrongColor
                            elide: Text.ElideRight
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }

                        Label {
                            Layout.fillWidth: true
                            text: (modelData.artist ? (modelData.artist + " · ") : "")
                                  + (modelData.song_id ? (i18n.text("song_number", i18n.language).arg(modelData.song_id)
                                                         + " · " + String(modelData.song_hash || "").slice(0, 12) + " · ") : "")
                                  + (modelData.path || "")
                            color: root.textMutedColor
                            elide: Text.ElideMiddle
                            font.pixelSize: 10
                        }
                    }

                    IconButton {
                        text: modelData.state === "current" && root.isPlaying ? "⏸" : "▶"
                        tooltipText: modelData.state === "current" && root.isPlaying
                                     ? i18n.text("pause", i18n.language)
                                     : i18n.text("play_this_track", i18n.language)
                        implicitWidth: 34
                        implicitHeight: 34
                        fillColor: "#20332f"
                        hoverColor: "#2b4941"
                        pressedColor: "#35564d"
                        borderColor: "#3b665b"
                        labelColor: root.mintColor
                        onClicked: modelData.state === "current"
                                   ? root.togglePlayPauseRequested()
                                   : root.playRequested(Number(modelData.id))
                    }

                    IconButton {
                        text: "×"
                        tooltipText: i18n.text("remove", i18n.language)
                        implicitWidth: 34
                        implicitHeight: 34
                        fillColor: "#292124"
                        hoverColor: "#3c292d"
                        pressedColor: "#4b3137"
                        borderColor: "#583a40"
                        labelColor: root.coralColor
                        onClicked: root.removeRequested(Number(modelData.id))
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: root.queue.length === 0
                text: i18n.text("drop_tracks", i18n.language)
                color: "#7e8b91"
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
        }
    }
}
