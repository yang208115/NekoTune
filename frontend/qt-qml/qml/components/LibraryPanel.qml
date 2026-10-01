import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var library: ({songs: [], tags: []})
    property var selectedTagIds: []
    property var playlists: []
    property var addingSong: ({})
    property var selectedSongIds: []
    property var deletingSongIds: []
    property bool deletePending: false
    property string deleteError: ""
    property int skippedCount: 0

    signal importRequested()
    signal playRequested(var tagIds, int songId)
    signal editRequested(var song)
    signal playlistRequested(string action, var params)
    signal deleteRequested(var songIds)

    readonly property var filteredSongs: {
        const songs = root.library.songs || []
        return songs.filter(song => {
            const assigned = (song.tags || []).map(tag => Number(tag.id))
            return root.selectedTagIds.every(id => assigned.indexOf(Number(id)) !== -1)
        })
    }
    readonly property int playableCount: filteredSongs.filter(song => Boolean(song.available)).length
    readonly property bool allFilteredSelected: filteredSongs.length > 0
        && filteredSongs.every(song => selectedSongIds.indexOf(Number(song.song_id)) !== -1)

    onSelectedTagIdsChanged: selectedSongIds = []
    onLibraryChanged: {
        const validIds = (library.songs || []).map(song => Number(song.song_id))
        selectedSongIds = selectedSongIds.filter(id => validIds.indexOf(id) !== -1)
    }

    function toggleSelection(songId) {
        const id = Number(songId)
        const next = selectedSongIds.slice()
        const index = next.indexOf(id)
        if (index < 0) next.push(id)
        else next.splice(index, 1)
        selectedSongIds = next
    }

    function toggleSelectAll() {
        selectedSongIds = allFilteredSelected ? [] : filteredSongs.map(song => Number(song.song_id))
    }

    function confirmDelete() {
        if (selectedSongIds.length === 0 || deletePending) return
        deletingSongIds = selectedSongIds.slice()
        deleteError = ""
        deletePending = true
        deleteRequested(deletingSongIds)
    }

    function deleteSucceeded() {
        deletePending = false
        selectedSongIds = []
        deletingSongIds = []
        deletePopup.close()
    }

    function deleteFailed(message) {
        deletePending = false
        deleteError = message
    }

    function addToPlaylist(song) {
        addingSong = song
        addPopup.open()
    }

    function addSongToPlaylist(playlistId) {
        playlistRequested("add", {id: Number(playlistId), song_id: Number(addingSong.song_id)})
        addPopup.close()
    }

    function requestPlay(songId) {
        skippedCount = 0
        playRequested(selectedTagIds, songId)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 116
            radius: 14
            color: "#16131F"
            border.color: "#332C41"
            RowLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Label {
                        text: i18n.text("library", i18n.language)
                        color: "#F5F1FA"
                        font.pixelSize: 22
                        font.weight: Font.Bold
                    }
                    Label {
                        text: i18n.countText("tracks", root.filteredSongs.length, i18n.language)
                              + " · " + i18n.text("tag_filter_all", i18n.language)
                        color: "#AAA0B8"
                        font.pixelSize: 12
                    }
                    Label {
                        visible: root.skippedCount > 0
                        text: i18n.text("unavailable_skipped", i18n.language).replace("%1", root.skippedCount)
                        color: "#E8A9C3"
                        font.pixelSize: 12
                    }
                }
                TextButton {
                    text: i18n.text("add_music", i18n.language)
                    subtle: true
                    onClicked: root.importRequested()
                }
                TextButton {
                    objectName: "playFilteredButton"
                    text: i18n.text("play_filtered", i18n.language)
                    enabled: root.playableCount > 0
                    onClicked: root.requestPlay(0)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: root.selectedTagIds.length === 0
                    ? i18n.text("all_songs", i18n.language)
                    : i18n.text("selected_tags", i18n.language) + ": "
                        + (root.library.tags || []).filter(tag => root.selectedTagIds.indexOf(Number(tag.id)) !== -1)
                            .map(tag => tag.name).join(" + ")
                color: "#D7CFE2"
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            TextButton {
                objectName: "librarySelectAllButton"
                text: i18n.text(root.allFilteredSelected ? "deselect_all" : "select_all_filtered", i18n.language)
                subtle: true
                enabled: root.filteredSongs.length > 0 && !root.deletePending
                onClicked: root.toggleSelectAll()
            }
            TextButton {
                objectName: "libraryDeleteSelectedButton"
                text: i18n.text("delete_selected", i18n.language).replace("%1", root.selectedSongIds.length)
                subtle: true
                enabled: root.selectedSongIds.length > 0 && !root.deletePending
                onClicked: { root.deleteError = ""; deletePopup.open() }
            }
        }

        ListView {
            id: songList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: root.filteredSongs
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: songRow
                required property var modelData
                required property int index
                width: songList.width
                height: 68
                radius: 10
                color: root.selectedSongIds.indexOf(Number(songRow.modelData.song_id)) !== -1
                    ? "#322743" : rowHover.hovered ? "#2A2338" : "#17141F"
                border.color: root.selectedSongIds.indexOf(Number(songRow.modelData.song_id)) !== -1
                    ? "#CBB8FF" : "#332C41"
                HoverHandler { id: rowHover }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10
                    CheckBox {
                        objectName: "librarySelectSong" + songRow.modelData.song_id
                        checked: root.selectedSongIds.indexOf(Number(songRow.modelData.song_id)) !== -1
                        text: songRow.modelData.title || i18n.text("untitled", i18n.language)
                        Accessible.name: i18n.text("select_song", i18n.language).replace("%1", text)
                        Layout.preferredWidth: 28
                        implicitHeight: 36
                        onClicked: root.toggleSelection(songRow.modelData.song_id)
                        contentItem: Item {}
                        indicator: Rectangle {
                            width: 18
                            height: 18
                            anchors.centerIn: parent
                            radius: 4
                            color: parent.checked ? "#CBB8FF" : "#17141F"
                            border.color: parent.checked ? "#CBB8FF" : "#8D809F"
                            Label {
                                anchors.centerIn: parent
                                text: "✓"
                                color: "#21172F"
                                visible: parent.parent.checked
                                font.pixelSize: 13
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: songRow.modelData.title || i18n.text("untitled", i18n.language)
                            color: songRow.modelData.available ? "#F5F1FA" : "#AAA0B8"
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: (songRow.modelData.artist || "")
                                + ((songRow.modelData.tags || []).length
                                    ? "  ·  " + songRow.modelData.tags.map(tag => tag.name).join(" · ") : "")
                            color: "#AAA0B8"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                    Label {
                        visible: !songRow.modelData.available
                        text: i18n.text("file_unavailable", i18n.language)
                        color: "#E8A9C3"
                        font.pixelSize: 11
                    }
                    TextButton {
                        objectName: "playLibrarySongButton" + songRow.modelData.song_id
                        text: i18n.text("play", i18n.language)
                        subtle: true
                        enabled: Boolean(songRow.modelData.available)
                        onClicked: root.requestPlay(Number(songRow.modelData.song_id))
                    }
                    TextButton {
                        text: i18n.text("edit_info", i18n.language)
                        subtle: true
                        onClicked: root.editRequested(songRow.modelData)
                    }
                    TextButton {
                        objectName: "libraryAddSongButton" + songRow.modelData.song_id
                        text: i18n.text("add_to_playlist", i18n.language)
                        subtle: true
                        enabled: Boolean(songRow.modelData.available)
                        onClicked: root.addToPlaylist(songRow.modelData)
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: songList.count === 0
                text: i18n.text(root.selectedTagIds.length ? "no_tag_matches" : "empty_library", i18n.language)
                color: "#AAA0B8"
            }
        }
    }

    Popup {
        id: deletePopup
        objectName: "libraryDeletePopup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(430, parent.width - 40)
        height: 210 + (root.deleteError ? 42 : 0)
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        onClosed: if (!root.deletePending) root.deletingSongIds = []
        padding: 18
        background: Rectangle { color: "#211C2D"; radius: 14; border.color: "#332C41" }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                text: i18n.text("delete_selected_title", i18n.language)
                    .replace("%1", root.deletingSongIds.length || root.selectedSongIds.length)
                color: "#F5F1FA"
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                text: i18n.text("delete_selected_hint", i18n.language)
                color: "#AAA0B8"
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                visible: root.deleteError.length > 0
                text: root.deleteError
                color: "#E8A9C3"
                wrapMode: Text.WordWrap
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                TextButton {
                    text: i18n.text("cancel", i18n.language)
                    subtle: true
                    enabled: !root.deletePending
                    onClicked: deletePopup.close()
                }
                TextButton {
                    objectName: "libraryConfirmDeleteButton"
                    text: i18n.text("confirm_delete", i18n.language)
                    enabled: !root.deletePending && root.selectedSongIds.length > 0
                    onClicked: root.confirmDelete()
                }
            }
        }
    }

    Popup {
        id: addPopup
        objectName: "libraryPlaylistPopup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 330
        height: Math.min(380, parent.height - 40)
        modal: true
        focus: true
        padding: 16
        background: Rectangle { color: "#211C2D"; radius: 14; border.color: "#332C41" }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                text: i18n.text("add_to_playlist", i18n.language)
                color: "#F5F1FA"
                font.pixelSize: 16
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: root.playlists
                clip: true
                delegate: TextButton {
                    required property var modelData
                    objectName: "libraryDestination" + modelData.id
                    width: ListView.view.width
                    text: modelData.name
                    subtle: true
                    onClicked: root.addSongToPlaylist(modelData.id)
                }
                Label {
                    anchors.centerIn: parent
                    visible: root.playlists.length === 0
                    text: i18n.text("no_playlists", i18n.language)
                    color: "#AAA0B8"
                    wrapMode: Text.WordWrap
                }
            }
            TextButton {
                Layout.alignment: Qt.AlignRight
                text: i18n.text("cancel", i18n.language)
                subtle: true
                onClicked: addPopup.close()
            }
        }
    }
}
