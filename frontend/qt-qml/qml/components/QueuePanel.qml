import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property var queue: []
    property var playlists: []
    property var currentSong: ({})
    property int currentPlaylist: 0
    property int editingPlaylist: 0
    property var addingSong: ({})

    property color panelColor: "#13111b"
    property color lineColor: "#211c2b"
    property color textStrongColor: "#f6f3fa"
    property color textSoftColor: "#cfc8db"
    property color textMutedColor: "#847d91"
    property color lavenderColor: "#cbb8ff"
    property color roseColor: "#e8a9c3"
    property string playbackState: "stopped"

    signal addRequested(int playlistId)
    signal clearRequested()
    signal playRequested(int queueId)
    signal togglePlayPauseRequested()
    signal removeRequested(int queueId)
    signal editRequested(var songData)
    signal playlistRequested(string action, var params)

    readonly property bool isPlaying: playbackState === "playing"

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
                    color: "#201c2b"
                    border.color: "#342d44"
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
                            implicitHeight: 26
                            leftPadding: 8
                            rightPadding: 8
                            hoverEnabled: true
                            text: modelData.label
                            contentItem: Label {
                                text: parent.text
                                color: Number(modelData.id) === root.currentPlaylist ? root.lavenderColor : root.textMutedColor
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 6
                                color: Number(modelData.id) === root.currentPlaylist ? "#201c2c" : "transparent"
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
                    Layout.preferredWidth: 32
                    text: "#"
                    color: root.textMutedColor
                    font.pixelSize: 11
                    font.weight: Font.Medium
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
                    text: i18n.text("artist_author", i18n.language)
                    color: root.textMutedColor
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }
                Label {
                    Layout.preferredWidth: 104
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
            model: root.visibleItems
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
                height: 44
                radius: 8

                color: currentTrack ? "#201c2c" : hovered ? "#181522" : "transparent"

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
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 20

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !row.currentTrack
                            text: String(index + 1)
                            color: root.textMutedColor
                            font.pixelSize: 12
                        }

                        Canvas {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.currentTrack
                            width: 14
                            height: 14
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = root.lavenderColor
                                if (root.isPlaying) {
                                    ctx.fillRect(2, 2, 3, 10)
                                    ctx.fillRect(8, 2, 3, 10)
                                } else {
                                    ctx.beginPath()
                                    ctx.moveTo(3, 2)
                                    ctx.lineTo(12, 7)
                                    ctx.lineTo(3, 12)
                                    ctx.closePath()
                                    ctx.fill()
                                }
                            }
                        }
                    }

                    // Column 2: Title
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 3
                        text: modelData.title || i18n.text("untitled", i18n.language)
                        color: row.currentTrack ? root.lavenderColor : root.textStrongColor
                        font.pixelSize: 13
                        font.weight: row.currentTrack ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }

                    // Column 3: Artist / Details
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 2
                        text: modelData.artist ? String(modelData.artist) : root.fileName(modelData.path)
                        color: root.textMutedColor
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    // Column 4: Hover Actions (Play, Edit, Move/More, Remove)
                    Item {
                        Layout.preferredWidth: 104
                        Layout.preferredHeight: 32

                        RowLayout {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            visible: row.hovered || row.currentTrack

                            IconButton {
                                kind: row.currentTrack && root.isPlaying ? "pause" : "play"
                                glyphColor: root.lavenderColor
                                implicitWidth: 26
                                implicitHeight: 26
                                iconSize: 14
                                onClicked: row.currentTrack ? root.togglePlayPauseRequested() : root.playSong(modelData)
                            }
                            IconButton {
                                kind: "edit"
                                tooltipText: i18n.text("edit_track_info", i18n.language)
                                glyphColor: root.textMutedColor
                                hoverGlyphColor: root.lavenderColor
                                implicitWidth: 26
                                implicitHeight: 26
                                iconSize: 14
                                onClicked: root.editRequested(modelData)
                            }
                            IconButton {
                                id: itemActionBtn
                                kind: "plus"
                                tooltipText: i18n.text("add_to_playlist", i18n.language)
                                glyphColor: root.textMutedColor
                                implicitWidth: 26
                                implicitHeight: 26
                                iconSize: 14
                                onClicked: root.addToPlaylist(modelData)
                            }
                            IconButton {
                                kind: "trash"
                                tooltipText: i18n.text("remove", i18n.language)
                                glyphColor: root.textMutedColor
                                hoverGlyphColor: root.roseColor
                                implicitWidth: 26
                                implicitHeight: 26
                                iconSize: 14
                                onClicked: root.removeSong(modelData)
                            }
                        }
                    }
                }
            }

            // Empty state
            Column {
                anchors.centerIn: parent
                spacing: 10
                visible: root.visibleItems.length === 0

                Label {
                    text: i18n.text(root.currentPlaylist ? "empty_playlist" : "empty_queue", i18n.language)
                    color: root.textMutedColor
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
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
            radius: 14
            color: "#181422"
            border.color: "#342d44"
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
                Layout.preferredHeight: 36
                maximumLength: 128
                placeholderText: i18n.text("playlist_name", i18n.language)
                color: root.textStrongColor
                placeholderTextColor: root.textMutedColor
                leftPadding: 10
                rightPadding: 10
                background: Rectangle {
                    radius: 8
                    color: "#110e18"
                    border.color: playlistName.activeFocus ? root.lavenderColor : "#272235"
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
            radius: 14
            color: "#181422"
            border.color: "#342d44"
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
            radius: 10
            color: "#1b1726"
            border.color: "#352e46"
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
            radius: 14
            color: "#181422"
            border.color: "#462b35"
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
