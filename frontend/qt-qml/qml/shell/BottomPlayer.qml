import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: bar
    required property var shell
    required property var controller
    required property var translator
    signal editRequested(var song)
    property bool addPending: false
    property string addError: ""
    readonly property int leftWidth: shell.width < 1200 ? 220 : 280
    readonly property int rightWidth: shell.width < 1200 ? 220 : 240
    readonly property string coverUrl: String(shell.song.cover_url || "")
    Layout.fillWidth: true
    Layout.preferredHeight: 96
    color: shell.bgSidebar
    function t(key) { return translator.text(key, translator.language) }
    function formatTime(ms, isDuration) {
        if (isDuration && Number(ms) <= 0) return "--:--"
        const seconds = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        return Math.floor(seconds / 60) + ":" + ("0" + seconds % 60).slice(-2)
    }
    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: "#332C41" }
    RowLayout {
        anchors.left: parent.left; anchors.leftMargin: 16; anchors.verticalCenter: parent.verticalCenter
        width: bar.leftWidth
        spacing: 10
        Button {
            objectName: "nowPlayingCoverButton"
            implicitWidth: 48; implicitHeight: 48
            enabled: bar.shell.hasSong
            Accessible.name: bar.t("now_playing")
            background: Rectangle { color: "#17141F"; radius: 8 }
            contentItem: Item {
                Image { anchors.fill: parent; source: "qrc:/artwork/default-cover.png"; fillMode: Image.PreserveAspectCrop; mipmap: true }
                Image { objectName: "nowPlayingCover"; anchors.fill: parent; source: bar.shell.hasSong ? bar.coverUrl : ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; visible: status === Image.Ready }
            }
            onClicked: bar.shell.openNowPlaying()
            ToolTip.visible: hovered || activeFocus
            ToolTip.text: bar.t("now_playing")
        }
        Button {
            Layout.fillWidth: true
            enabled: bar.shell.hasSong
            padding: 0
            Accessible.name: bar.shell.hasSong ? String(bar.shell.song.title) : bar.t("no_track_selected")
            background: Item {}
            contentItem: ColumnLayout {
                spacing: 4
                Label { Layout.fillWidth: true; text: bar.shell.hasSong ? bar.shell.song.title || bar.t("untitled") : bar.t("no_track_selected"); color: "#F5F1FA"; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                ArtistNames {
                    id: nowPlayingArtists
                    objectName: "nowPlayingArtists"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    summarizeOverflow: true
                    remainingTextTemplate: bar.t("additional_artists")
                    artist: bar.shell.hasSong ? String(bar.shell.song.artist || "") : ""
                    fallbackText: bar.t(bar.shell.hasSong ? "unknown_artist" : "browse_library")
                }
            }
            onClicked: bar.shell.openNowPlaying()
            ToolTip.visible: hovered || activeFocus
            ToolTip.text: String(bar.shell.song.title || "")
                + (nowPlayingArtists.names.length ? "\n" + nowPlayingArtists.names.join(" · ") : "")
        }
        IconButton {
            id: songMore
            kind: "more"; implicitWidth: 32; implicitHeight: 36
            enabled: bar.shell.hasSong
            tooltipText: bar.t("track_actions")
            onClicked: songMenu.openAt(songMore)
        }
    }
    ColumnLayout {
        objectName: "transportControls"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(640, bar.width - 2 * (Math.max(bar.leftWidth, bar.rightWidth) + 32))
        spacing: 4
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 16
            IconButton { objectName: "previousTrackButton"; kind: "previous"; tooltipText: bar.t("previous"); enabled: bar.shell.transport.connected && bar.shell.queue.length > 0; onClicked: bar.controller.previous() }
            IconButton {
                objectName: "mainPlayButton"
                kind: bar.shell.isPlaying ? "pause" : "play"
                tooltipText: bar.t(bar.shell.isPlaying ? "pause" : "play")
                implicitWidth: 48; implicitHeight: 48; iconSize: 26; cornerRadius: 24
                fillColor: "#CBB8FF"; hoverColor: "#DBCDFF"; pressedColor: "#B7A0ED"
                glyphColor: "#21172F"; hoverGlyphColor: "#21172F"
                enabled: bar.shell.transport.connected && (bar.shell.hasSong || bar.shell.queue.length > 0)
                onClicked: bar.controller.togglePlayPause()
            }
            IconButton { objectName: "nextTrackButton"; kind: "next"; tooltipText: bar.t("next"); enabled: bar.shell.transport.connected && bar.shell.queue.length > 0; onClicked: bar.controller.next() }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { Layout.preferredWidth: 40; text: bar.formatTime(timeline.pressed ? timeline.valueAt(timeline.position) : timeline.value, false); color: "#AAA0B8"; font.pixelSize: 12; horizontalAlignment: Text.AlignRight }
            SeekSlider {
                id: timeline; objectName: "playbackSeekSlider"
                Layout.fillWidth: true
                duration: bar.shell.duration; playbackPosition: bar.shell.position
                onSeekRequested: value => bar.controller.seek(value)
                Accessible.name: bar.t("playback_progress")
            }
            Label { Layout.preferredWidth: 40; text: bar.formatTime(bar.shell.duration, true); color: "#AAA0B8"; font.pixelSize: 12 }
        }
    }
    RowLayout {
        anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter
        width: bar.rightWidth
        spacing: 4
        IconButton { kind: bar.shell.volume > 0 ? "volume" : "mute"; tooltipText: bar.t("volume"); enabled: bar.shell.transport.connected; implicitWidth: 32; onClicked: bar.controller.toggleMute() }
        PlayerSlider {
            Layout.fillWidth: true; Layout.minimumWidth: 60; Layout.maximumWidth: 90
            from: 0; to: 1; value: bar.shell.volume
            enabled: bar.shell.transport.connected
            onMoved: bar.controller.setVolume(value)
            Accessible.name: bar.t("volume")
        }
        IconButton {
            objectName: "nowPlayingButton"
            kind: "lyrics"; tooltipText: bar.t("now_playing")
            glyphColor: bar.shell.nowPlayingOpen ? "#CBB8FF" : "#AAA0B8"
            enabled: bar.shell.hasSong
            onClicked: bar.shell.nowPlayingOpen ? bar.shell.closeNowPlaying() : bar.shell.openNowPlaying()
        }
        Item {
            Layout.preferredWidth: 44; Layout.preferredHeight: 44
            IconButton { objectName: "queueToggleButton"; anchors.fill: parent; kind: "queue"; tooltipText: bar.t("queue") + " (" + bar.shell.queue.length + ")"; glyphColor: bar.shell.queueOpen ? "#CBB8FF" : "#AAA0B8"; onClicked: bar.shell.queueOpen = !bar.shell.queueOpen }
            Label { anchors.right: parent.right; anchors.top: parent.top; text: String(bar.shell.queue.length); color: "#CBB8FF"; font.pixelSize: 10; visible: bar.shell.queue.length > 0 }
        }
    }
    ActionMenu {
        id: songMenu
        actions: [
            {key: "edit", label: bar.t("edit_info"), enabled: bar.shell.transport.connected},
            {key: "playlist", label: bar.t("add_to_playlist"), enabled: bar.shell.transport.connected},
            {key: "stop", label: bar.t("stop"), enabled: bar.shell.transport.connected}
        ]
        onChosen: action => {
            if (action === "edit") bar.editRequested(bar.shell.song)
            else if (action === "stop") bar.controller.stop()
            else { bar.addError = ""; picker.open() }
        }
    }
    Popup {
        id: picker
        parent: Overlay.overlay; anchors.centerIn: parent
        width: 360; height: Math.min(400, parent.height - 40); padding: 20
        modal: true; focus: true
        closePolicy: bar.addPending ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { objectName: "shortcutBlocker"; color: "#211C2D"; radius: 16; border.color: "#332C41" }
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: bar.t("add_to_playlist"); color: "#F5F1FA"; font.pixelSize: 18 }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: bar.shell.app.playlists.model
                ScrollBar.vertical: ScrollBar {}
                delegate: TextButton {
                    required property var modelData
                    width: ListView.view.width; text: modelData.name; subtle: true
                    enabled: !bar.addPending && bar.shell.transport.connected
                    onClicked: { bar.addPending = true; bar.shell.app.playlists.managePlaylist("add", {id: Number(modelData.id), song_id: Number(bar.shell.song.song_id)}) }
                }
                Label { width: parent.width; anchors.centerIn: parent; wrapMode: Text.WordWrap; visible: bar.shell.app.playlists.model.count === 0; text: bar.t("no_playlists"); color: "#AAA0B8" }
            }
            Label { Layout.fillWidth: true; text: bar.addError; visible: Boolean(bar.addError); color: "#FF9BAE"; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
            TextButton { Layout.alignment: Qt.AlignRight; text: bar.t("cancel"); subtle: true; enabled: !bar.addPending; onClicked: picker.close() }
        }
    }
    Connections {
        target: bar.shell.app.playlists
        function onRequestSucceeded(method) { if (method === "playlist.add" && bar.addPending) { bar.addPending = false; picker.close() } }
        function onRequestFailed(method, message) { if (method === "playlist.add" && bar.addPending) { bar.addPending = false; bar.addError = message } }
    }
}
