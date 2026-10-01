import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property var queue: []
    property var queueModel: null
    property var playlists: []
    property var currentSong: ({})
    property real duration: 0
    property var lyrics: ({})
    property int currentPlaylist: 0
    property int editingPlaylist: 0
    property var addingSong: ({})
    property color panelColor: "#17141F"
    property color lineColor: "#332C41"
    property color textStrongColor: "#F5F1FA"
    property color textSoftColor: "#D7CFE2"
    property color textMutedColor: "#AAA0B8"
    property color lavenderColor: "#CBB8FF"
    property color roseColor: "#E8A9C3"
    property color bgHoverColor: "#2A2338"
    property color bgSelectedColor: "#322743"
    property string playbackState: "stopped"

    signal addRequested(int playlistId)
    signal clearRequested()
    signal playRequested(int queueId)
    signal togglePlayPauseRequested()
    signal removeRequested(int queueId)
    signal editRequested(var songData)
    signal playlistRequested(string action, var params)

    readonly property bool isPlaying: playbackState === "playing"

    function formatDuration(itemDuration, isCurrent) {
        var ms = 0
        if (itemDuration !== undefined && itemDuration !== null && !isNaN(itemDuration) && Number(itemDuration) > 0) {
            ms = Number(itemDuration)
        } else if (isCurrent && root.duration > 0) {
            ms = root.duration
        } else {
            return "--:--"
        }
        var totalSec = Math.floor(ms / 1000)
        var m = Math.floor(totalSec / 60)
        var s = totalSec % 60
        return (m < 10 ? "0" + m : String(m)) + ":" + (s < 10 ? "0" + s : String(s))
    }

    function playlistById(id) {
        for (var index = 0; index < playlists.length; ++index)
            if (Number(playlists[index].id) === Number(id)) return playlists[index]
        return null
    }

    function fileName(path) {
        var value = String(path || "").replace(/\\/g, "/")
        var parts = value.split("/")
        return parts.length ? parts[parts.length - 1] : ""
    }

    function placePopup(popup, anchor) {
        if (anchor) {
            var point = anchor.mapToItem(Overlay.overlay, 0, 0)
            var preferredX = point.x + anchor.width - popup.width
            var preferredY = point.y + anchor.height + 8
            if (preferredY + popup.height > Overlay.overlay.height - 12)
                preferredY = point.y - popup.height - 8
            popup.x = Math.max(12, Math.min(preferredX, Overlay.overlay.width - popup.width - 12))
            popup.y = Math.max(12, Math.min(preferredY, Overlay.overlay.height - popup.height - 12))
            return
        }
        popup.x = Math.max(12, (Overlay.overlay.width - popup.width) / 2)
        popup.y = Math.max(12, (Overlay.overlay.height - popup.height) / 2)
    }

    function newPlaylist() {
        editingPlaylist = 0
        playlistName.text = ""
        namePopup.open()
    }

    function renamePlaylist() {
        editingPlaylist = currentPlaylist
        playlistName.text = selectedPlaylist.name
        namePopup.open()
    }

    function addToPlaylist(song) {
        addingSong = song
        addPopup.open()
    }

    function playSong(song) {
        if (currentPlaylist)
            playlistRequested("play", {id: currentPlaylist, song_id: Number(song.song_id)})
        else
            playRequested(Number(song.id))
    }

    function removeSong(song) {
        if (currentPlaylist)
            playlistRequested("remove", {id: currentPlaylist, song_id: Number(song.song_id)})
        else
            removeRequested(Number(song.id))
    }

    readonly property var selectedPlaylist: playlistById(currentPlaylist)
    readonly property var visibleItems: selectedPlaylist ? selectedPlaylist.items : queue
    readonly property var navigationItems: {
        var result = [{id: 0, label: i18n.text("queue", i18n.language)}]
        for (var index = 0; index < playlists.length; ++index)
            result.push({id: Number(playlists[index].id), label: String(playlists[index].name)})
        return result
    }

    onPlaylistsChanged: {
        if (currentPlaylist !== 0 && !playlistById(currentPlaylist)) currentPlaylist = 0
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        // Header Banner for Queue
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            radius: 14
            color: "#16131f"
            border.color: root.lineColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 20

                // Icon Cover Card
                Rectangle {
                    Layout.preferredWidth: 74
                    Layout.preferredHeight: 74
                    radius: 12
                    color: "#17141F"
                    border.color: root.lineColor
                    border.width: 1

                    Canvas {
                        anchors.centerIn: parent
                        width: 32
                        height: 32
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.strokeStyle = root.lavenderColor
                            ctx.lineWidth = 2
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"
                            const w = width, h = height
                            ctx.beginPath()
                            ctx.moveTo(w * 0.2, h * 0.3)
                            ctx.lineTo(w * 0.8, h * 0.3)
                            ctx.moveTo(w * 0.2, h * 0.5)
                            ctx.lineTo(w * 0.8, h * 0.5)
                            ctx.moveTo(w * 0.2, h * 0.7)
                            ctx.lineTo(w * 0.55, h * 0.7)
                            ctx.stroke()
                        }
                    }
                }

                // Info & Action Buttons
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: root.selectedPlaylist ? root.selectedPlaylist.name : i18n.text("queue", i18n.language)
                        color: root.textStrongColor
                        font.pixelSize: 22
                        font.weight: Font.Bold
                    }

                    Label {
                        text: i18n.countText("tracks", root.visibleItems.length, i18n.language) + " · " + i18n.text("local_storage", i18n.language)
                        color: root.textMutedColor
                        font.pixelSize: 12
                    }

                    RowLayout {
                        spacing: 8
                        Layout.topMargin: 4

                        TextButton {
                            text: i18n.text("add_music", i18n.language)
                            implicitHeight: 28
                            onClicked: root.addRequested(root.currentPlaylist)
                        }
                        TextButton {
                            text: i18n.text("new_playlist", i18n.language)
                            implicitHeight: 28
                            subtle: true
                            onClicked: root.newPlaylist()
                        }
                        TextButton {
                            text: i18n.text(root.currentPlaylist ? "play_playlist" : "clear_queue", i18n.language)
                            implicitHeight: 28
                            subtle: true
                            enabled: root.visibleItems.length > 0
                            onClicked: root.currentPlaylist ? root.playlistRequested("play", {id: root.currentPlaylist}) : root.clearRequested()
                        }
                        IconButton {
                            id: playlistActionsButton
                            visible: root.currentPlaylist !== 0
                            kind: "more"
                            tooltipText: i18n.text("playlist_actions", i18n.language)
                            implicitWidth: 28
                            implicitHeight: 28
                            onClicked: playlistActions.open()
                        }
                    }
                }
            }
        }

        // Playlist and playback queue navigation
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                clip: true
                contentWidth: navigationRow.width
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick

                Row {
                    id: navigationRow
                    height: parent.height
                    spacing: 4

                    Repeater {
                        model: root.navigationItems
                        delegate: Button {
                            required property var modelData
                            required property int index
                            implicitHeight: 32
                            leftPadding: 12
                            rightPadding: 12
                            hoverEnabled: true
                            text: modelData.label
                            contentItem: Label {
                                text: parent.text
                                color: Number(modelData.id) === root.currentPlaylist ? root.lavenderColor : root.textMutedColor
                                font.pixelSize: 13
                                font.weight: Number(modelData.id) === root.currentPlaylist ? Font.DemiBold : Font.Normal
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 8
                                color: Number(modelData.id) === root.currentPlaylist ? root.bgSelectedColor : parent.hovered ? root.bgHoverColor : "transparent"
                            }
                            onClicked: root.currentPlaylist = Number(modelData.id)
                        }
                    }
                }
            }
        }

        // Table Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Label {
                    Layout.preferredWidth: 36
                    text: "#"
                    color: root.textMutedColor
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                }
                Item {
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 1
                }
                Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 3
                    text: i18n.text("custom_title", i18n.language)
                    color: root.textMutedColor
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }
                Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 2
                    visible: root.width >= 600
                    text: i18n.text("artist_author", i18n.language)
                    color: root.textMutedColor
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }
                Label {
                    Layout.preferredWidth: 54
                    text: i18n.text("duration", i18n.language)
                    color: root.textMutedColor
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignRight
                }
                Label {
                    Layout.preferredWidth: 128
                    text: i18n.text("actions", i18n.language)
                    color: root.textMutedColor
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: root.lineColor
        }

        // Main List
        ListView {
            id: queueList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: !root.currentPlaylist && root.queueModel ? root.queueModel : root.visibleItems
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: 4
                contentItem: Rectangle { radius: 2; color: "#363044" }
            }

            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                property bool hovered: rowHover.hovered
                property bool currentTrack: root.currentPlaylist ? Number(modelData.song_id) === Number(root.currentSong.song_id || 0) : modelData.state === "current"

                width: queueList.width
                height: 60
                radius: 12

                color: currentTrack ? root.bgSelectedColor : hovered ? root.bgHoverColor : "transparent"
                border.color: currentTrack ? root.lineColor : "transparent"
                border.width: 1

                HoverHandler {
                    id: rowHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: root.playSong(modelData)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 12

                    // Column 1: Index / Playing Icon
                    Item {
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36

                        Label {
                            anchors.centerIn: parent
                            visible: !row.currentTrack
                            text: String(index + 1)
                            color: root.textMutedColor
                            font.pixelSize: 13
                        }

                        Canvas {
                            anchors.centerIn: parent
                            visible: row.currentTrack
                            width: 16
                            height: 16
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = root.lavenderColor
                                if (root.isPlaying) {
                                    ctx.fillRect(2, 2, 4, 12)
                                    ctx.fillRect(9, 2, 4, 12)
                                } else {
                                    ctx.beginPath()
                                    ctx.moveTo(3, 2)
                                    ctx.lineTo(13, 8)
                                    ctx.lineTo(3, 14)
                                    ctx.closePath()
                                    ctx.fill()
                                }
                            }
                        }
                    }

                    // Column 2: 40x40 Thumbnail Cover (Section 5.1 & 5)
                    Rectangle {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        radius: 8
                        clip: true
                        color: "#17141F"
                        border.color: row.currentTrack ? root.lavenderColor : root.lineColor
                        border.width: 1

                        Image {
                            anchors.fill: parent
                            source: "qrc:/artwork/default-cover.png"
                            fillMode: Image.PreserveAspectCrop
                            opacity: 0.85
                        }

                        Image {
                            anchors.fill: parent
                            source: row.currentTrack ? (String(root.currentSong.cover_url || "")
                                    || (root.lyrics && !root.lyrics.offline && root.lyrics.track_id === modelData.song_hash
                                        ? String((root.lyrics.document || {}).cover_url || "") : "")) : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            visible: status === Image.Ready
                        }
                    }

                    // Column 3: Title
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 3
                        text: modelData.title || i18n.text("untitled", i18n.language)
                        color: row.currentTrack ? root.lavenderColor : root.textStrongColor
                        font.pixelSize: 14
                        font.weight: row.currentTrack ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }

                    // Column 4: Artist / Details (Responsive: hidden on narrow window < 600)
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 2
                        visible: root.width >= 600
                        text: modelData.artist ? String(modelData.artist) : root.fileName(modelData.path)
                        color: root.textSoftColor
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }

                    // Column 5: Track Duration (Section 5.1: "--:--" if unknown, monospace)
                    Label {
                        Layout.preferredWidth: 54
                        text: root.formatDuration(modelData.duration, row.currentTrack)
                        color: root.textMutedColor
                        font.pixelSize: 12
                        font.family: "Monospace"
                        horizontalAlignment: Text.AlignRight
                    }

                    // Column 6: Hover Actions (Play, Edit, Move/More, Remove)
                    Item {
                        Layout.preferredWidth: 128
                        Layout.preferredHeight: 36

                        RowLayout {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4
                            visible: row.hovered || row.currentTrack

                            IconButton {
                                kind: row.currentTrack && root.isPlaying ? "pause" : "play"
                                glyphColor: root.lavenderColor
                                implicitWidth: 30
                                implicitHeight: 30
                                iconSize: 15
                                onClicked: row.currentTrack ? root.togglePlayPauseRequested() : root.playSong(modelData)
                            }
                            IconButton {
                                kind: "edit"
                                tooltipText: i18n.text("edit_track_info", i18n.language)
                                glyphColor: root.textMutedColor
                                hoverGlyphColor: root.lavenderColor
                                implicitWidth: 30
                                implicitHeight: 30
                                iconSize: 15
                                onClicked: root.editRequested(modelData)
                            }
                            IconButton {
                                id: itemActionBtn
                                kind: "plus"
                                tooltipText: i18n.text("add_to_playlist", i18n.language)
                                glyphColor: root.textMutedColor
                                implicitWidth: 30
                                implicitHeight: 30
                                iconSize: 15
                                onClicked: root.addToPlaylist(modelData)
                            }
                            IconButton {
                                kind: "trash"
                                tooltipText: i18n.text("remove", i18n.language)
                                glyphColor: root.textMutedColor
                                hoverGlyphColor: root.roseColor
                                implicitWidth: 30
                                implicitHeight: 30
                                iconSize: 15
                                onClicked: root.removeSong(modelData)
                            }
                        }
                    }
                }
            }

            // Empty state (Section 9)
            Column {
                anchors.centerIn: parent
                spacing: 14
                visible: root.visibleItems.length === 0

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 64
                    height: 64
                    radius: 32
                    color: root.bgHoverColor

                    Canvas {
                        anchors.centerIn: parent
                        width: 32
                        height: 32
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.strokeStyle = root.lavenderColor
                            ctx.fillStyle = root.lavenderColor
                            ctx.lineWidth = 2
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"
                            const w = width, h = height
                            ctx.beginPath()
                            ctx.moveTo(w * 0.35, h * 0.72)
                            ctx.lineTo(w * 0.35, h * 0.25)
                            ctx.lineTo(w * 0.75, h * 0.16)
                            ctx.lineTo(w * 0.75, h * 0.62)
                            ctx.stroke()
                            ctx.beginPath()
                            ctx.arc(w * 0.25, h * 0.72, w * 0.12, 0, Math.PI * 2)
                            ctx.arc(w * 0.65, h * 0.62, w * 0.12, 0, Math.PI * 2)
                            ctx.fill()
                        }
                    }
                }

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: i18n.text(root.currentPlaylist ? "empty_playlist" : "empty_queue", i18n.language)
                    color: root.textStrongColor
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                }

                TextButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: i18n.text("add_music", i18n.language)
                    onClicked: root.addRequested(root.currentPlaylist)
                }
            }
        }
    }

    // Name popup
    Popup {
        id: namePopup
        objectName: "playlistNamePopup"
        parent: Overlay.overlay
        modal: true
        focus: true
        width: 320
        height: 170
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: { root.placePopup(namePopup); playlistName.forceActiveFocus() }
        background: Rectangle {
            radius: 16
            color: "#211C2D"
            border.color: root.lineColor
            border.width: 1
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            Label {
                text: i18n.text(root.editingPlaylist ? "rename_playlist" : "new_playlist", i18n.language)
                color: root.textStrongColor
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            TextField {
                id: playlistName
                objectName: "playlistNameInput"
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                maximumLength: 128
                placeholderText: i18n.text("playlist_name", i18n.language)
                color: root.textStrongColor
                placeholderTextColor: root.textMutedColor
                leftPadding: 12
                rightPadding: 12
                background: Rectangle {
                    radius: 8
                    color: "#17141F"
                    border.color: playlistName.activeFocus ? root.lavenderColor : "#8D809F"
                    border.width: 1
                }
                onAccepted: if (text.trim().length > 0) saveButton.clicked()
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                TextButton {
                    text: i18n.text("cancel", i18n.language)
                    subtle: true
                    onClicked: namePopup.close()
                }
                TextButton {
                    id: saveButton
                    objectName: "savePlaylistButton"
                    text: i18n.text("save", i18n.language)
                    enabled: playlistName.text.trim().length > 0
                    onClicked: {
                        root.playlistRequested(root.editingPlaylist ? "rename" : "create", {
                            id: root.editingPlaylist,
                            name: playlistName.text.trim()
                        })
                        namePopup.close()
                    }
                }
            }
        }
    }

    // Add a song to a playlist
    Popup {
        id: addPopup
        parent: Overlay.overlay
        modal: true
        focus: true
        width: 320
        height: 400
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: root.placePopup(addPopup)
        background: Rectangle {
            radius: 16
            color: "#211C2D"
            border.color: root.lineColor
            border.width: 1
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: i18n.text("add_to_playlist", i18n.language)
                    color: root.textStrongColor
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                IconButton {
                    kind: "close"
                    onClicked: addPopup.close()
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.lineColor }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: root.playlists
                Label {
                    anchors.fill: parent
                    visible: root.playlists.length === 0
                    text: i18n.text("no_playlists", i18n.language)
                    wrapMode: Text.WordWrap
                    color: root.textMutedColor
                }
                delegate: Rectangle {
                    id: destItem
                    required property var modelData
                    width: ListView.view.width
                    height: 36
                    radius: 6
                    color: destHover.hovered ? "#221c2e" : "transparent"

                    HoverHandler { id: destHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            var params = {id: Number(destItem.modelData.id)}
                            if (root.addingSong.queue_id) params.queue_id = Number(root.addingSong.queue_id)
                            else params.path = String(root.addingSong.path)
                            root.playlistRequested("add", params)
                            addPopup.close()
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8
                        IconButton {
                            kind: "music"
                            glyphColor: root.lavenderColor
                            enabled: false
                            implicitWidth: 20
                            implicitHeight: 20
                        }
                        Label {
                            Layout.fillWidth: true
                            text: destItem.modelData.name
                            color: root.textSoftColor
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    // Playlist actions menu
    Popup {
        id: playlistActions
        parent: Overlay.overlay
        modal: false
        focus: true
        width: 170
        height: 84
        padding: 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: root.placePopup(playlistActions, playlistActionsButton)
        background: Rectangle {
            radius: 12
            color: "#211C2D"
            border.color: root.lineColor
            border.width: 1
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 2
            TextButton {
                Layout.fillWidth: true
                text: i18n.text("rename_playlist", i18n.language)
                subtle: true
                onClicked: { playlistActions.close(); root.renamePlaylist() }
            }
            TextButton {
                Layout.fillWidth: true
                text: i18n.text("delete_playlist", i18n.language)
                subtle: true
                subtleText: root.roseColor
                onClicked: { playlistActions.close(); deletePopup.open() }
            }
        }
    }

    // Delete playlist confirmation
    Popup {
        id: deletePopup
        parent: Overlay.overlay
        modal: true
        focus: true
        width: 320
        height: 170
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: root.placePopup(deletePopup)
        background: Rectangle {
            radius: 16
            color: "#211C2D"
            border.color: "#FF9BAE"
            border.width: 1
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                text: i18n.text("delete_playlist", i18n.language)
                color: root.textStrongColor
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: i18n.text("delete_playlist_hint", i18n.language)
                color: root.textMutedColor
                wrapMode: Text.WordWrap
                font.pixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                TextButton {
                    text: i18n.text("cancel", i18n.language)
                    subtle: true
                    onClicked: deletePopup.close()
                }
                TextButton {
                    text: i18n.text("delete_playlist", i18n.language)
                    lavenderColor: "#e8a9c3"
                    primaryText: "#181014"
                    onClicked: {
                        root.playlistRequested("delete", { id: root.currentPlaylist })
                        deletePopup.close()
                    }
                }
            }
        }
    }
}
