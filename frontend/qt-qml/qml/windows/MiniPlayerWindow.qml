pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Window {
    id: window
    objectName: "miniPlayerWindow"
    width: 440; height: 160
    minimumWidth: 360; minimumHeight: 144
    visible: false
    color: "transparent"
    title: "NekoTune · " + t("mini_mode")
    required property var manager
    required property var app
    required property var translator
    required property var transport
    readonly property var playback: app.playback
    readonly property var song: playback.song
    readonly property bool isPlaying: playback.state === "playing"
    readonly property bool canToggle: transport.connected && Boolean(playback.capabilities[isPlaying ? "can_pause" : "can_play"])
    function t(key) { return translator.text(key, translator.language) }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusMd
        color: Theme.bgRaised
        border.color: Theme.borderControl
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 8; spacing: 4
            Item {
                Layout.fillWidth: true; Layout.preferredHeight: 28
                WindowDragArea { anchors.fill: parent; controller: window.manager; hostWindow: window }
                Label {
                    anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter
                    text: "NekoTune"; color: Theme.textMuted; font.pixelSize: Theme.fontCaption
                }
                Row {
                    anchors.right: parent.right; spacing: 2
                    IconButton {
                        objectName: "miniPinButton"
                        kind: "pin"; implicitWidth: 28; implicitHeight: 28
                        glyphColor: window.manager.miniAlwaysOnTop ? Theme.accentPrimary : Theme.textMuted
                        tooltipText: window.t("mini_always_on_top") + (window.manager.wayland ? "\n" + window.t("mini_wayland_notice") : "")
                        onClicked: window.manager.miniAlwaysOnTop = !window.manager.miniAlwaysOnTop
                    }
                    IconButton {
                        objectName: "miniReturnButton"
                        kind: "expand"; implicitWidth: 28; implicitHeight: 28
                        tooltipText: window.t("show_main_window")
                        onClicked: window.manager.showMainWindow()
                    }
                    IconButton {
                        objectName: "miniCloseButton"
                        kind: "close"; implicitWidth: 28; implicitHeight: 28
                        tooltipText: window.t("close")
                        onClicked: window.close()
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: 64; spacing: 10
                Item {
                    Layout.preferredWidth: 64; Layout.preferredHeight: 64
                    clip: true
                    SizedImage { anchors.fill: parent; source: "qrc:/artwork/default-cover.png"; fillMode: Image.PreserveAspectCrop }
                    SizedImage { anchors.fill: parent; source: String(window.song.cover_url || ""); asynchronous: true; fillMode: Image.PreserveAspectCrop; visible: status === Image.Ready }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 0
                    Label {
                        Layout.fillWidth: true
                        text: String(window.song.title || window.t("no_track_selected"))
                        textFormat: Text.PlainText; elide: Text.ElideRight
                        color: Theme.textPrimary; font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter; spacing: 14
                        IconButton {
                            objectName: "miniPreviousButton"
                            kind: "previous"; tooltipText: window.t("previous")
                            implicitHeight: 36; implicitWidth: 36
                            enabled: window.transport.connected && Boolean(window.playback.capabilities.can_go_previous)
                            onClicked: window.playback.previous()
                        }
                        IconButton {
                            objectName: "miniPlayButton"
                            kind: window.isPlaying ? "pause" : "play"; tooltipText: window.t(window.isPlaying ? "pause" : "play")
                            implicitHeight: 36; implicitWidth: 36; cornerRadius: 18
                            fillColor: Theme.accentPrimary; glyphColor: Theme.textOnAccent; hoverGlyphColor: Theme.textOnAccent; hoverColor: Theme.accentHover
                            enabled: window.canToggle
                            onClicked: window.playback.togglePlayPause()
                        }
                        IconButton {
                            objectName: "miniNextButton"
                            kind: "next"; tooltipText: window.t("next")
                            implicitHeight: 36; implicitWidth: 36
                            enabled: window.transport.connected && Boolean(window.playback.capabilities.can_go_next)
                            onClicked: window.playback.next()
                        }
                    }
                }
            }
            CompactLyrics {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 28
                song: window.song; lyrics: window.app.lyrics.current
                translator: window.translator; connected: window.transport.connected
                position: window.playback.position; playing: window.isPlaying
                playbackRate: window.playback.playbackRate
                running: window.visible && window.visibility !== Window.Minimized
            }
        }
    }
    WindowDragArea {
        anchors.right: parent.right; anchors.bottom: parent.bottom
        width: 12; height: 12
        controller: window.manager; hostWindow: window
        resizeEdges: Qt.RightEdge | Qt.BottomEdge
    }
    Shortcut {
        sequence: "Space"; context: Qt.WindowShortcut
        enabled: window.active && window.canToggle
        onActivated: window.playback.togglePlayPause()
    }
}
