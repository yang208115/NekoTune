import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Presentation state complements controller-owned filtering/selection.
// Rows identify songs by song_id; current playback is a separate marker.
// Confirmation snapshots selected IDs so refreshes cannot change its target.
// API file cleanup is sent explicitly from the checkbox choice.
// Cleanup leftovers remain visible after an otherwise successful deletion.
// Busy/error state belongs to the particular dialog operation.
Item {
    id: root
    required property var controller
    readonly property var selectedTagIds: controller.selectedTagIds
    property var playlists: []
    property var addingSong: ({})
    readonly property var selectedSongIds: controller.selectedSongIds
    property var deletingSongIds: []
    property bool deletePending: false
    property string deleteError: ""
    property string cleanupWarning: ""
    property bool cleanManagedFiles: true
    property int skippedCount: 0

    signal importRequested()
    signal playRequested(var tagIds, int songId)
    signal editRequested(var song)
    signal playlistRequested(string action, var params)
    signal deleteRequested(var songIds, bool cleanFiles)

    readonly property var filteredSongs: controller.filteredSongs.items
    readonly property int playableCount: filteredSongs.filter(song => Boolean(song.available)).length
    readonly property bool allFilteredSelected: filteredSongs.length > 0
        && filteredSongs.every(song => selectedSongIds.indexOf(Number(song.song_id)) !== -1)

    function toggleSelection(songId) {
        controller.toggleSelection(songId)
    }

    function toggleSelectAll() {
        controller.toggleSelectAll()
    }

    // Copy selection when opening the confirmation rather than reading it later.
    // Library updates can otherwise change which songs a confirmation removes.
    // Each new confirmation starts with the UI's managed-cleanup default.
    // The backend still validates every ID and enforces its transaction rules.
    function openDelete() {
        if (selectedSongIds.length === 0 || deletePending) return
        deletingSongIds = selectedSongIds.slice()
        cleanManagedFiles = true
        deleteError = ""
        cleanupWarning = ""
        deletePopup.open()
    }
    function confirmDelete() {
        if (deletingSongIds.length === 0 || deletePending) return
        deleteError = ""
        deletePending = true
        deleteRequested(deletingSongIds, cleanManagedFiles)
    }

    // The library transaction has already committed when this callback arrives.
    // Residual file cleanup errors are warnings, not grounds to restore selection.
    // Close the dialog and keep the warning available on the page.
    // This differs from deleteFailed, which leaves retry context intact.
    function deleteSucceeded(errors) {
        deletePending = false
        cleanupWarning = errors && errors.length ? i18n.text("cleanup_partial_warning", i18n.language)
            + "\n" + errors.join("\n") : ""
        controller.clearSelection()
        deletingSongIds = []
        deletePopup.close()
    }

    function deleteFailed(message) {
        deletePending = false
        deleteError = message
    }

    function addToPlaylist(song) {
        addingSong = song
        operationError = ""
        addPopup.open()
    }

    function addSongToPlaylist(playlistId) {
        if (addPending) return
        addPending = true
        operationError = ""
        playlistRequested("add", {id: Number(playlistId), song_id: Number(addingSong.song_id)})
    }

    function requestPlay(songId) {
        skippedCount = 0
        playRequested(selectedTagIds, songId)
    }

    property bool connected: true
    property var currentSong: ({})
    property bool playing: false
    property real currentDuration: 0
    property string operationError: ""
    property bool addPending: false
    signal queueAddRequested(string path)
    signal togglePlayPauseRequested()
    signal newTagRequested()
    signal editTagRequested(var tag)
    function focusSearch() { search.forceActiveFocus(); search.selectAll() }
    function songActions(song) {
        return [
            {key: "play", label: i18n.text("play", i18n.language), enabled: Boolean(song.available) && root.connected},
            {key: "queue", label: i18n.text("add_to_queue", i18n.language), enabled: Boolean(song.available) && root.connected},
            {key: "playlist", label: i18n.text("add_to_playlist", i18n.language), enabled: Boolean(song.available) && root.connected},
            {key: "edit", label: i18n.text("edit_info", i18n.language), enabled: root.connected},
            {key: "delete", label: i18n.text("delete", i18n.language), destructive: true, enabled: root.connected && !root.deletePending}
        ]
    }
    function songAction(action, song) {
        if (action === "play") requestPlay(Number(song.song_id))
        else if (action === "queue") queueAddRequested(String(song.path))
        else if (action === "playlist") addToPlaylist(song)
        else if (action === "edit") editRequested(song)
        else if (action === "delete") {
            controller.selectRow(Number(song.song_id))
            openDelete()
        }
    }
    function playlistSucceeded() { addPending = false; addPopup.close() }
    function playlistFailed(message) { addPending = false; operationError = message }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { Layout.fillWidth: true; text: i18n.text("local_music", i18n.language); color: "#F5F1FA"; font.pixelSize: 26; font.weight: Font.DemiBold }
                Label { Layout.fillWidth: true; text: i18n.countText("tracks", root.filteredSongs.length, i18n.language); color: "#AAA0B8"; font.pixelSize: 13 }
            }
            TextButton { objectName: "playFilteredButton"; text: i18n.text("play_all", i18n.language); enabled: root.playableCount > 0 && root.connected; onClicked: root.requestPlay(0) }
            TextButton { text: i18n.text("import_music", i18n.language); subtle: true; enabled: root.connected; onClicked: root.importRequested() }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            InputField {
                id: search
                objectName: "librarySearchField"
                Layout.fillWidth: true
                Layout.maximumWidth: 360
                placeholderText: i18n.text("local_search_hint", i18n.language)
                text: root.controller.searchText
                onTextEdited: root.controller.searchText = text
                Accessible.name: placeholderText
            }
            TextButton {
                id: tagButton
                text: i18n.text("tags", i18n.language) + (root.selectedTagIds.length ? " (" + root.selectedTagIds.length + ")" : "")
                subtle: true
                onClicked: { const point = tagButton.mapToItem(Overlay.overlay, 0, height); tagPopup.x = Math.min(point.x, Overlay.overlay.width - tagPopup.width - 12); tagPopup.y = point.y + 8; tagPopup.open() }
            }
            Item { Layout.fillWidth: true }
            TextButton { objectName: "librarySelectAllButton"; text: i18n.text(root.allFilteredSelected ? "deselect_all" : "select_all_filtered", i18n.language); subtle: true; enabled: root.filteredSongs.length > 0 && !root.deletePending; onClicked: root.toggleSelectAll() }
            IconButton {
                objectName: "libraryDeleteSelectedButton"
                kind: "trash"
                enabled: root.selectedSongIds.length > 0 && !root.deletePending && root.connected
                tooltipText: i18n.text("delete_selected", i18n.language).replace("%1", root.selectedSongIds.length)
                onClicked: root.openDelete()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.selectedTagIds.length > 0 || root.selectedSongIds.length > 0
            Label {
                Layout.fillWidth: true
                text: root.selectedTagIds.length ? root.controller.tags.items.filter(tag => root.selectedTagIds.indexOf(Number(tag.id)) !== -1).map(tag => tag.name).join(" + ") : ""
                color: "#D7CFE2"; font.pixelSize: 12; elide: Text.ElideRight; textFormat: Text.PlainText
            }
            Label { text: root.selectedSongIds.length ? i18n.text("selected_count", i18n.language).replace("%1", root.selectedSongIds.length) : ""; color: "#CBB8FF"; font.pixelSize: 12 }
        }
        Label { Layout.fillWidth: true; visible: root.skippedCount > 0; text: i18n.text("unavailable_skipped", i18n.language).replace("%1", root.skippedCount); color: "#E8C58A"; wrapMode: Text.WordWrap }
        Label {
            objectName: "libraryCleanupWarning"
            Layout.fillWidth: true
            visible: root.cleanupWarning.length > 0
            text: root.cleanupWarning
            textFormat: Text.PlainText
            color: "#E8A9C3"
            wrapMode: Text.WrapAnywhere
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#332C41" }
        ListView {
            id: songList
            objectName: "librarySongList"
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 2
            model: root.controller.filteredSongs
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: TrackRow {
                required property var modelData
                required property int index
                objectName: "libraryRow" + modelData.song_id
                width: songList.width
                song: current && root.currentDuration > 0 ? Object.assign({}, modelData, {duration: root.currentDuration}) : modelData
                rowIndex: index
                available: Boolean(modelData.available); connected: root.connected
                selected: root.selectedSongIds.indexOf(Number(modelData.song_id)) !== -1
                current: Number(root.currentSong.song_id || 0) === Number(modelData.song_id)
                playing: root.playing; showCheckbox: true
                playObjectName: "playLibrarySongButton" + modelData.song_id
                checkboxObjectName: "librarySelectSong" + modelData.song_id
                menuActions: root.songActions(modelData)
                onSelectedRequested: modifiers => root.controller.selectRow(Number(modelData.song_id), Boolean(modifiers & Qt.ControlModifier), Boolean(modifiers & Qt.ShiftModifier))
                onCheckboxRequested: root.toggleSelection(Number(modelData.song_id))
                onActivated: root.requestPlay(Number(modelData.song_id))
                onPlayClicked: current ? root.togglePlayPauseRequested() : root.requestPlay(Number(modelData.song_id))
                onActionRequested: action => root.songAction(action, modelData)
            }
            Column {
                anchors.centerIn: parent; visible: songList.count === 0; spacing: 16
                Label { anchors.horizontalCenter: parent.horizontalCenter; text: i18n.text(root.controller.searchText ? "no_search_results" : root.selectedTagIds.length ? "no_tag_matches" : "empty_library", i18n.language); color: "#AAA0B8" }
                TextButton { anchors.horizontalCenter: parent.horizontalCenter; visible: !root.controller.searchText && !root.selectedTagIds.length; text: i18n.text("import_music", i18n.language); enabled: root.connected; onClicked: root.importRequested() }
            }
        }
    }
    Popup {
        id: tagPopup
        parent: Overlay.overlay
        width: 320; height: Math.min(400, parent.height - 100)
        focus: true; padding: 16
        background: Rectangle { objectName: "shortcutBlocker"; color: "#211C2D"; radius: 12; border.color: "#332C41" }
        ColumnLayout {
            anchors.fill: parent; spacing: 10
            Label { text: i18n.text("tag_filter_all", i18n.language); color: "#AAA0B8"; font.pixelSize: 12 }
            TextButton { Layout.fillWidth: true; text: i18n.text("all_songs", i18n.language); subtle: true; onClicked: root.controller.selectedTagIds = [] }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: root.controller.tags
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    TextButton { Layout.fillWidth: true; text: modelData.name; subtle: root.selectedTagIds.indexOf(Number(modelData.id)) === -1; onClicked: root.controller.toggleTag(Number(modelData.id)) }
                    IconButton { kind: "edit"; tooltipText: i18n.text("rename_tag", i18n.language); enabled: root.connected; onClicked: { tagPopup.close(); root.editTagRequested(modelData) } }
                }
            }
            TextButton { Layout.fillWidth: true; text: i18n.text("new_tag", i18n.language); subtle: true; enabled: root.connected; onClicked: { tagPopup.close(); root.newTagRequested() } }
        }
    }

    Popup {
        id: deletePopup
        objectName: "libraryDeletePopup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(430, parent.width - 40)
        height: Math.min(parent.height - 40, 330 + (root.deleteError ? 64 : 0))
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        padding: 18
        background: Rectangle { objectName: "shortcutBlocker"; color: "#211C2D"; radius: 14; border.color: "#332C41" }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                text: i18n.text("delete_selected_title", i18n.language)
                    .replace("%1", root.deletingSongIds.length)
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
            CheckBox {
                id: cleanupChoice
                objectName: "libraryCleanFilesCheckBox"
                Layout.fillWidth: true
                text: i18n.text("clean_managed_files", i18n.language)
                checked: root.cleanManagedFiles
                enabled: !root.deletePending
                onToggled: root.cleanManagedFiles = checked
                palette.windowText: "#F5F1FA"
                indicator: Rectangle {
                    width: 18; height: 18; radius: 4
                    x: cleanupChoice.leftPadding
                    y: (cleanupChoice.height - height) / 2
                    color: cleanupChoice.checked ? "#CBB8FF" : "#17141F"
                    border.color: "#8D809F"
                    Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 2; color: "#21172F"; visible: cleanupChoice.checked }
                }
            }
            Label {
                Layout.fillWidth: true
                visible: root.cleanManagedFiles
                text: i18n.text("clean_managed_files_hint", i18n.language)
                color: "#E8A9C3"
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
                    enabled: !root.deletePending && root.deletingSongIds.length > 0
                    onClicked: root.confirmDelete()
                }
            }
        }
    }

    Popup {
        id: addPopup
        closePolicy: root.addPending ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        objectName: "libraryPlaylistPopup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 330
        height: Math.min(380, parent.height - 40)
        modal: true
        focus: true
        padding: 16
        background: Rectangle { objectName: "shortcutBlocker"; color: "#211C2D"; radius: 14; border.color: "#332C41" }
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
                    enabled: !root.addPending && root.connected
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
            Label { Layout.fillWidth: true; visible: root.operationError.length > 0; text: root.operationError; color: "#FF9BAE"; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
            TextButton {
                Layout.alignment: Qt.AlignRight
                text: i18n.text("cancel", i18n.language)
                subtle: true
                enabled: !root.addPending
                onClicked: addPopup.close()
            }
        }
    }
}
