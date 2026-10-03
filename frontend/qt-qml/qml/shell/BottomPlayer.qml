import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// Persistent shell controls consume the same confirmed playback state as the full player page.
// Opening artwork or metadata navigates to now-playing without issuing a play command.
// Queue visibility is independent of collection navigation and current track selection.
Rectangle {
    id: bar
    required property var shell
    required property var controller
    required property var translator
    signal editRequested(var song)
    property bool addPending: false
    property string addError: ""
    readonly property int leftWidth: shell.width < 1200 ? 220 : 280
    readonly property int rightWidth: shell.width < 1200 ? 160 : 220
    readonly property string coverUrl: String(shell.song.cover_url || "")
    Layout.fillWidth: true
    Layout.preferredHeight: Theme.bottomBarHeight
    color: Theme.bgSidebar
    function t(key) { return translator.text(key, translator.language) }
    // An unknown duration needs a placeholder, while a current position of zero is a valid time.
    // Both values arrive in milliseconds and are displayed at whole-second precision.
    // Clamping negative position avoids showing a transient decoder value as a negative clock.
    function formatTime(ms, isDuration) {
        if (isDuration && Number(ms) <= 0) return "--:--"
        const seconds = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        return Math.floor(seconds / 60) + ":" + ("0" + seconds % 60).slice(-2)
    }
    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.borderSubtle }
    RowLayout {
        anchors.left: parent.left; anchors.leftMargin: 16; anchors.verticalCenter: parent.verticalCenter
        width: bar.leftWidth
        spacing: 10
        Button {
            objectName: "nowPlayingCoverButton"
            implicitWidth: 48; implicitHeight: 48
            enabled: bar.shell.hasSong
            Accessible.name: bar.t("now_playing")
            background: Rectangle { color: Theme.bgSurface; radius: Theme.radiusSm }
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
                Label { Layout.fillWidth: true; text: bar.shell.hasSong ? bar.shell.song.title || bar.t("untitled") : bar.t("no_track_selected"); color: Theme.textPrimary; font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
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
        // Reserve the larger side-column width on both sides to keep transport controls centered.
        // Using separate left/right subtraction would shift the visual center as metadata width changes.
        // The maximum central width keeps the timeline bounded on large windows.
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
                fillColor: Theme.accentPrimary; hoverColor: Theme.accentHover; pressedColor: Theme.accentPressed
                glyphColor: Theme.textOnAccent; hoverGlyphColor: Theme.textOnAccent
                enabled: bar.shell.transport.connected && (bar.shell.hasSong || bar.shell.queue.length > 0)
                onClicked: bar.controller.togglePlayPause()
            }
            IconButton { objectName: "nextTrackButton"; kind: "next"; tooltipText: bar.t("next"); enabled: bar.shell.transport.connected && bar.shell.queue.length > 0; onClicked: bar.controller.next() }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { Layout.preferredWidth: 40; text: bar.formatTime(timeline.pressed ? timeline.valueAt(timeline.position) : timeline.value, false); color: Theme.textMuted; font.pixelSize: Theme.fontCaption; horizontalAlignment: Text.AlignRight }
            // The time label above follows the handle's preview while pressed, not stale backend position.
            // SeekSlider keeps that preview stable until release and eventual backend acknowledgment.
            // Only its committed signal sends the millisecond seek request to the controller.
            SeekSlider {
                id: timeline; objectName: "playbackSeekSlider"
                Layout.fillWidth: true
                duration: bar.shell.duration; playbackPosition: bar.shell.position
                onSeekRequested: value => bar.controller.seek(value)
                Accessible.name: bar.t("playback_progress")
            }
            Label { Layout.preferredWidth: 40; text: bar.formatTime(bar.shell.duration, true); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
        }
    }
    Item {
        id: rightControls
        objectName: "playerSecondaryControls"
        anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter
        width: bar.rightWidth
        readonly property bool compact: bar.shell.width < 1200
        height: compact ? 80 : 44
        // Two rows preserve both the volume hit area and view buttons within the 160px compact column.
        RowLayout {
            anchors.left: parent.left
            y: rightControls.compact ? 0 : (parent.height - height) / 2
            width: rightControls.compact ? parent.width : parent.width - viewControls.width - 8
            height: rightControls.compact ? 32 : 40
            spacing: 4
            IconButton { objectName: "volumeMuteButton"; kind: bar.shell.volume > 0 ? "volume" : "mute"; tooltipText: bar.t("volume"); enabled: bar.shell.transport.connected; implicitWidth: 32; implicitHeight: rightControls.compact ? 32 : 40; onClicked: bar.controller.toggleMute() }
            // Volume follows every moved event; seeking retains its separate release-only behavior.
            PlayerSlider {
                objectName: "volumeSlider"
                Layout.fillWidth: true; Layout.minimumWidth: 72
                from: 0; to: 1; value: bar.shell.volume
                enabled: bar.shell.transport.connected
                onMoved: bar.controller.setVolume(value)
                Accessible.name: bar.t("volume")
            }
        }
        RowLayout {
            id: viewControls
            anchors.right: parent.right; anchors.bottom: parent.bottom
            width: implicitWidth
            spacing: 4
            IconButton {
                objectName: "nowPlayingButton"
                kind: "lyrics"; tooltipText: bar.t("now_playing")
                glyphColor: bar.shell.nowPlayingOpen ? Theme.accentPrimary : Theme.textMuted
                enabled: bar.shell.hasSong
                onClicked: bar.shell.nowPlayingOpen ? bar.shell.closeNowPlaying() : bar.shell.openNowPlaying()
            }
            Item {
                Layout.preferredWidth: 44; Layout.preferredHeight: 44
                IconButton { objectName: "queueToggleButton"; anchors.fill: parent; kind: "queue"; tooltipText: bar.t("queue") + " (" + bar.shell.queue.length + ")"; glyphColor: bar.shell.queueOpen ? Theme.accentPrimary : Theme.textMuted; onClicked: bar.shell.queueOpen = !bar.shell.queueOpen }
                Label { anchors.right: parent.right; anchors.top: parent.top; text: String(bar.shell.queue.length); color: Theme.accentPrimary; font.pixelSize: 10; visible: bar.shell.queue.length > 0 }
            }
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
        // Playlist insertion uses the current song's library ID, not its queue occurrence ID.
        // Pending completion keeps the target operation visible and disables duplicate submissions.
        // A failed add retains the picker and shows its error so another attempt can be made.
        id: picker
        parent: Overlay.overlay; anchors.centerIn: parent
        width: 360; height: Math.min(400, parent.height - 40); padding: 20
        modal: true; focus: true
        closePolicy: bar.addPending ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { objectName: "shortcutBlocker"; color: Theme.bgRaised; radius: Theme.radiusLg; border.color: Theme.borderSubtle }
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: bar.t("add_to_playlist"); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
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
                Label { width: parent.width; anchors.centerIn: parent; wrapMode: Text.WordWrap; visible: bar.shell.app.playlists.model.count === 0; text: bar.t("no_playlists"); color: Theme.textMuted }
            }
            Label { Layout.fillWidth: true; text: bar.addError; visible: Boolean(bar.addError); color: Theme.statusError; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
            TextButton { Layout.alignment: Qt.AlignRight; text: bar.t("cancel"); subtle: true; enabled: !bar.addPending; onClicked: picker.close() }
        }
    }
    Connections {
        target: bar.shell.app.playlists
        function onRequestSucceeded(method) { if (method === "playlist.add" && bar.addPending) { bar.addPending = false; picker.close() } }
        function onRequestFailed(method, message) { if (method === "playlist.add" && bar.addPending) { bar.addPending = false; bar.addError = message } }
    }
}
