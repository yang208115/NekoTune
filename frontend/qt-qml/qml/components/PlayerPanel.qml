import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property var song: ({})
    property var queue: []
    property real duration: 0
    property real position: 0
    property real volume: 0.8
    property string playbackState: "stopped"
    property bool connected: false
    property string errorText: ""

    property color panelColor: "#191b1f"
    property color lineColor: "#30343b"
    property color textStrongColor: "#f4f0e8"
    property color textSoftColor: "#c5beb2"
    property color textMutedColor: "#8e938f"
    property color mintColor: "#7adfc6"
    property color amberColor: "#f2c86b"

    readonly property bool hasSong: Boolean(root.song && root.song.song_id)
    readonly property bool hasQueue: root.queue && root.queue.length > 0
    readonly property int currentIndex: {
        if (!root.queue) {
            return -1
        }
        for (let index = 0; index < root.queue.length; index += 1) {
            if (root.queue[index].state === "current") {
                return index
            }
        }
        return -1
    }

    signal addRequested()
    signal togglePlayPauseRequested()
    signal nextRequested()
    signal previousRequested()
    signal seekRequested(real positionMs)
    signal volumeRequested(real value)
    signal editMetadataRequested()

    spacing: 18

    RowLayout {
        Layout.fillWidth: true
        spacing: 14

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                spacing: 10

                Label {
                    text: "NekoTune"
                    color: root.textStrongColor
                    font.pixelSize: 34
                    font.weight: Font.Black
                }

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    width: connectionText.implicitWidth + 18
                    height: 26
                    radius: 13
                    color: root.connected ? "#16362f" : "#35251f"
                    border.color: root.connected ? "#285b50" : "#674136"

                    Label {
                        id: connectionText
                        anchors.centerIn: parent
                        text: root.connected
                              ? i18n.text("connected", i18n.language)
                              : i18n.text("offline", i18n.language)
                        color: root.connected ? root.mintColor : "#ffad9f"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }
            }

            Label {
                text: root.song.title
                      ? (root.playbackState === "playing"
                         ? i18n.text("now_playing", i18n.language)
                         : i18n.text("ready", i18n.language))
                      : i18n.text("no_track_selected", i18n.language)
                color: root.textMutedColor
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
        }

        TextButton {
            text: i18n.text("add_music", i18n.language)
            implicitWidth: 132
            onClicked: root.addRequested()
        }

        TextButton {
            text: i18n.language === "zh" ? "English" : "中文"
            implicitWidth: 76
            onClicked: i18n.language = i18n.language === "zh" ? "en" : "zh"
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: 10
        color: root.panelColor
        border.color: root.lineColor
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.margins: 34
            spacing: 34

            AlbumArt {
                Layout.fillHeight: true
                Layout.preferredWidth: Math.min(360, Math.max(280, parent.width * 0.38))

                hasSong: Boolean(root.song.title)
                playbackState: root.playbackState
                textMutedColor: root.textMutedColor
                mintColor: root.mintColor
                amberColor: root.amberColor
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 22

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            text: root.song.title || i18n.text("no_track_loaded", i18n.language)
                            color: root.textStrongColor
                            elide: Text.ElideRight
                            font.pixelSize: 34
                            font.weight: Font.Black
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.song.artist
                                  ? (root.song.artist + " · " + (root.song.path || ""))
                                  : (root.song.path || i18n.text("choose_audio", i18n.language))
                            color: root.textMutedColor
                            elide: Text.ElideMiddle
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }

                        LrcLyrics {
                            Layout.fillWidth: true
                            Layout.preferredHeight: visible ? 190 : 0
                            lyrics: String(root.song.lyrics || "")
                            position: root.position
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    PlayerSlider {
                        Layout.fillWidth: true
                        enabled: root.hasSong && root.duration > 0
                        from: 0
                        to: Math.max(root.duration, 1)
                        value: Math.min(Math.max(root.position, 0), Math.max(root.duration, 1))
                        live: false
                        onMoved: root.seekRequested(value)
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: formatTime(root.position)
                            color: root.textMutedColor
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Label {
                            text: formatTime(root.duration)
                            color: root.textMutedColor
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    IconButton {
                        text: "⏮"
                        tooltipText: i18n.text("previous", i18n.language)
                        enabled: root.hasQueue
                        implicitWidth: 48
                        implicitHeight: 46
                        onClicked: root.previousRequested()
                    }

                    IconButton {
                        text: root.playbackState === "playing" ? "⏸" : "▶"
                        tooltipText: root.playbackState === "playing"
                                     ? i18n.text("pause", i18n.language)
                                     : i18n.text("play", i18n.language)
                        enabled: root.hasSong || root.hasQueue
                        implicitWidth: 64
                        implicitHeight: 52
                        fillColor: root.amberColor
                        hoverColor: "#ffd978"
                        pressedColor: "#d7ad50"
                        borderColor: "#ffe193"
                        labelColor: "#17140c"
                        font.pixelSize: 20
                        onClicked: root.togglePlayPauseRequested()
                    }

                    IconButton {
                        text: "⏭"
                        tooltipText: i18n.text("next", i18n.language)
                        enabled: root.hasQueue && root.currentIndex >= 0 && root.currentIndex < root.queue.length - 1
                        implicitWidth: 48
                        implicitHeight: 46
                        onClicked: root.nextRequested()
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    TextButton {
                        text: i18n.text("edit_info", i18n.language)
                        implicitWidth: 94
                        enabled: Boolean(root.song.song_id)
                        onClicked: root.editMetadataRequested()
                    }

                    Label {
                        text: i18n.text("volume", i18n.language)
                        color: root.textSoftColor
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    PlayerSlider {
                        Layout.preferredWidth: 170
                        from: 0
                        to: 1
                        value: root.volume
                        activeColor: root.mintColor
                        onMoved: root.volumeRequested(value)
                    }

                    Label {
                        Layout.preferredWidth: 42
                        horizontalAlignment: Text.AlignRight
                        text: Math.round(root.volume * 100) + "%"
                        color: root.textMutedColor
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: visible ? 38 : 0
                    radius: 8
                    color: "#35251f"
                    border.color: "#674136"
                    visible: root.errorText.length > 0

                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: 13
                        anchors.rightMargin: 13
                        verticalAlignment: Text.AlignVCenter
                        text: root.errorText
                        color: "#ffb59f"
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
    }

    function formatTime(ms) {
        const totalSeconds = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        const minutes = Math.floor(totalSeconds / 60)
        const seconds = totalSeconds % 60
        return minutes + ":" + String(seconds).padStart(2, "0")
    }
}
