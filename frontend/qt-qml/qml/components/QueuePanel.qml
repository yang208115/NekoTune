import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One panel displays either live queue occurrences or a saved playlist.
// Source selection changes the identity used by play/remove actions.
// The live drawer uses queue IDs; playlists use their library song IDs.
// Text search filters presentation without mutating either collection.
// Playlist playback sends the visible sequence as the requested order.
// Pending method state keeps dialog outcomes tied to admitted operations.
Item {
    id: root
    property var queue: []
    property var queueModel: null
    property var playlists: []
    property var playlistController: null
    property var queueController: null
    property var playbackController: null
    property string playbackMode: "sequential"
    property var currentSong: ({})
    property real duration: 0
    property var lyrics: ({})
    property int currentPlaylist: 0
    property int editingPlaylist: 0
    property var addingSong: ({})
    property string playbackState: "stopped"
    property bool connected: true
    property bool compact: false
    property string searchText: ""
    property int selectedId: 0
    property string pendingMethod: ""
    property string operationError: ""
    readonly property bool isPlaying: playbackState === "playing"
    readonly property var selectedPlaylist: playlists.find(list => Number(list.id) === currentPlaylist) || null
    readonly property var sourceItems: selectedPlaylist ? selectedPlaylist.items : queue
    readonly property var visibleItems: sourceItems.filter(song => !searchText.trim() ||
        (String(song.title || "") + " " + String(song.artist || "") + " " + fileName(song.path)).toLowerCase().indexOf(searchText.trim().toLowerCase()) !== -1)
    signal addRequested(int playlistId)
    signal clearRequested()
    signal playRequested(int queueId)
    signal togglePlayPauseRequested()
    signal removeRequested(int queueId)
    signal editRequested(var songData)
    signal playlistRequested(string action, var params)
    signal queueAddRequested(string path)
    signal libraryRequested()
    signal closeRequested()
    function fileName(path) { return String(path || "").split("/").pop() }
    // Song IDs cannot distinguish repeated entries in the live queue.
    // Queue IDs do not exist as durable membership identities in a playlist.
    // Resolve this distinction once so row selection and action handlers agree.
    // The backend validates those identities again before changing state.
    function identity(song) { return Number(currentPlaylist ? song.song_id : (song.queue_id || song.id)) }
    function focusSearch() { search.forceActiveFocus(); search.selectAll() }
    function newPlaylist() { editingPlaylist = 0; playlistName.text = ""; operationError = ""; namePopup.open() }
    function renamePlaylist() { editingPlaylist = currentPlaylist; playlistName.text = selectedPlaylist.name; operationError = ""; namePopup.open() }
    function addToPlaylist(song) { addingSong = song; operationError = ""; addPopup.open() }
    function playSong(song) {
        if (currentPlaylist) playlistRequested("play", {id: currentPlaylist, song_id: Number(song.song_id), song_ids: visibleItems.map(item => Number(item.song_id))})
        else playRequested(Number(song.queue_id || song.id))
    }
    function removeSong(song) {
        if (currentPlaylist) playlistRequested("remove", {id: currentPlaylist, song_id: Number(song.song_id)})
        else removeRequested(Number(song.queue_id || song.id))
    }
    function playAll() { playlistRequested("play", {id: currentPlaylist, song_ids: visibleItems.map(item => Number(item.song_id))}) }
    function requestSucceeded(method) {
        if (method !== pendingMethod) return
        pendingMethod = ""
        operationError = ""
        namePopup.close()
        addPopup.close()
        confirmation.completed()
    }
    function requestFailed(method, message) {
        if (method !== pendingMethod) return
        pendingMethod = ""
        operationError = message
        confirmation.failed(message)
    }
    function menuActions(song) {
        const usable = root.connected && (song.available === undefined || Boolean(song.available))
        const result = [
            {key: "play", label: i18n.text("play", i18n.language), enabled: usable},
            {key: "playlist", label: i18n.text("add_to_playlist", i18n.language), enabled: usable},
            {key: "edit", label: i18n.text("edit_info", i18n.language), enabled: root.connected},
            {key: "remove", label: i18n.text(root.currentPlaylist ? "remove_from_playlist" : "remove_from_queue", i18n.language), destructive: true, enabled: root.connected}
        ]
        if (root.currentPlaylist) result.splice(1, 0, {key: "queue", label: i18n.text("add_to_queue", i18n.language), enabled: usable})
        return result
    }
    function songAction(action, song) {
        if (action === "play") playSong(song)
        else if (action === "playlist") addToPlaylist(song)
        else if (action === "queue") {
            if (song.source && song.source.kind === "extension" && root.queueController) root.queueController.addSourceSong(song)
            else queueAddRequested(String(song.path))
        }
        else if (action === "edit") editRequested(song)
        else if (action === "remove") removeSong(song)
    }
    onCurrentPlaylistChanged: { selectedId = 0; searchText = ""; if (songList) songList.positionViewAtBeginning() }
    onSearchTextChanged: selectedId = 0
    onPlaylistsChanged: if (currentPlaylist && !playlists.some(list => Number(list.id) === currentPlaylist)) currentPlaylist = 0
    Connections { target: root.playlistController; function onRequestSucceeded(method) { root.requestSucceeded(method) } function onRequestFailed(method, message) { root.requestFailed(method, message) } }
    Connections {
        target: root.playbackController
        function onRequestSucceeded(method) { if (method === "player.set_playback_mode") root.operationError = "" }
        function onRequestFailed(method, message) { if (method === "player.set_playback_mode") root.operationError = message }
    }
    Connections { target: root.queueController; function onRequestSucceeded(method) { root.requestSucceeded(method) } function onRequestFailed(method, message) { root.requestFailed(method, message) } }

    ColumnLayout {
        anchors.fill: parent
        spacing: root.compact ? 12 : 16
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Label { Layout.fillWidth: true; text: root.selectedPlaylist ? root.selectedPlaylist.name : i18n.text("queue", i18n.language); font.pixelSize: root.compact ? 20 : Theme.fontTitle; font.weight: Font.DemiBold; color: Theme.textPrimary; elide: Text.ElideRight; textFormat: Text.PlainText }
                Label { text: i18n.countText("tracks", root.sourceItems.length, i18n.language); font.pixelSize: Theme.fontCaption; color: Theme.textMuted }
            }
            TextButton { visible: Boolean(root.currentPlaylist); text: i18n.text("play_all", i18n.language); enabled: root.visibleItems.length > 0 && root.connected; onClicked: root.playAll() }
            TextButton { visible: !root.compact; text: i18n.text("import_music", i18n.language); subtle: true; enabled: root.connected; onClicked: root.addRequested(root.currentPlaylist) }
            TextButton {
                objectName: "clearQueueButton"
                visible: !root.currentPlaylist
                text: i18n.text("clear_queue", i18n.language)
                subtle: true
                subtleText: Theme.statusError
                enabled: root.sourceItems.length > 0 && root.connected && !confirmation.busy
                onClicked: confirmation.open()
            }
            IconButton {
                id: collectionMore
                visible: Boolean(root.currentPlaylist)
                kind: "more"
                tooltipText: i18n.text("playlist_actions", i18n.language)
                onClicked: collectionMenu.openAt(collectionMore)
            }
            IconButton { visible: root.compact; kind: "close"; tooltipText: i18n.text("close", i18n.language); onClicked: root.closeRequested() }
        }
        RowLayout {
            Layout.fillWidth: true
            InputField { id: search; objectName: "collectionSearchField"; Layout.fillWidth: true; placeholderText: i18n.text("local_search_hint", i18n.language); text: root.searchText; onTextEdited: root.searchText = text; Accessible.name: placeholderText }
            PlaybackModeButton {
                visible: !root.currentPlaylist
                translator: i18n
                playbackMode: root.playbackMode
                enabled: root.connected
                onModeRequested: mode => { if (root.playbackController) root.playbackController.setPlaybackMode(mode) }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderSubtle }
        ListView {
            id: songList
            objectName: root.currentPlaylist ? "playlistSongList" : "queueSongList"
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 8
            header: Item { height: 4 }
            footer: Item { height: 4 }
            leftMargin: 4
            rightMargin: 4
            model: !root.currentPlaylist && !root.searchText && root.queueModel ? root.queueModel : root.visibleItems
            ScrollBar.vertical: ScrollBar {}
            delegate: TrackRow {
                required property var modelData
                required property int index
                width: songList.width - 8
                objectName: (root.currentPlaylist ? "playlistRow" : "queueRow") + root.identity(modelData)
                song: current && root.duration > 0 ? Object.assign({}, modelData, {duration: root.duration}) : modelData
                rowIndex: index; compact: root.compact
                connected: root.connected
                available: modelData.available === undefined || Boolean(modelData.available)
                selected: root.selectedId === root.identity(modelData)
                current: root.currentPlaylist ? Number(modelData.song_id) === Number(root.currentSong.song_id) : modelData.state === "current"
                playing: root.isPlaying
                playObjectName: (root.currentPlaylist ? "playPlaylistSong" : "playQueueSong") + root.identity(modelData)
                menuActions: root.menuActions(modelData)
                onSelectedRequested: root.selectedId = root.identity(modelData)
                onActivated: root.playSong(modelData)
                onPlayClicked: current ? root.togglePlayPauseRequested() : root.playSong(modelData)
                onActionRequested: action => root.songAction(action, modelData)
            }
            Column {
                anchors.centerIn: parent; width: parent.width; spacing: 16
                visible: songList.count === 0
                Label { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: i18n.text(root.searchText ? "no_search_results" : root.currentPlaylist ? "empty_playlist" : "empty_queue", i18n.language); color: Theme.textMuted }
                TextButton { anchors.horizontalCenter: parent.horizontalCenter; visible: !root.searchText; text: i18n.text("browse_library", i18n.language); onClicked: root.libraryRequested() }
            }
        }
    }
    ActionMenu {
        id: collectionMenu
        extensionContext: "playlist"
        contextData: ({playlist_id: root.currentPlaylist})
        actions: [
            {key: "rename", label: i18n.text("rename_playlist", i18n.language), enabled: root.connected},
            {key: "delete", label: i18n.text("delete_playlist", i18n.language), destructive: true, enabled: root.connected}
        ]
        onChosen: action => {
            if (action === "rename") root.renamePlaylist()
            else confirmation.open()
        }
    }
    ConfirmationDialog {
        id: confirmation
        objectName: "collectionDeletePopup"
        title: i18n.text(root.currentPlaylist ? "delete_playlist" : "clear_queue", i18n.language)
        message: i18n.text(root.currentPlaylist ? "delete_playlist_hint" : "clear_queue_hint", i18n.language)
        onConfirmed: {
            root.pendingMethod = root.currentPlaylist ? "playlist.delete" : "queue.clear"
            if (root.currentPlaylist) root.playlistRequested("delete", {id: root.currentPlaylist})
            else root.clearRequested()
        }
    }
    Popup {
        id: namePopup
        objectName: "playlistNamePopup"
        parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(400, parent.width - 40)
        padding: 24; modal: true; focus: true
        closePolicy: root.pendingMethod ? Popup.NoAutoClose : Popup.CloseOnEscape
        onOpened: playlistName.forceActiveFocus()
        background: Rectangle { objectName: "shortcutBlocker"; color: Theme.bgRaised; radius: Theme.radiusLg; border.color: Theme.borderSubtle }
        contentItem: ColumnLayout {
            spacing: 16
            Label { text: i18n.text(root.editingPlaylist ? "rename_playlist" : "new_playlist", i18n.language); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
            InputField { id: playlistName; objectName: "playlistNameInput"; Layout.fillWidth: true; placeholderText: i18n.text("playlist_name", i18n.language); enabled: !root.pendingMethod; onAccepted: if (saveName.enabled) saveName.clicked() }
            Label { Layout.fillWidth: true; visible: Boolean(root.operationError); text: root.operationError; color: Theme.statusError; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                TextButton { text: i18n.text("cancel", i18n.language); subtle: true; enabled: !root.pendingMethod; onClicked: namePopup.close() }
                TextButton {
                    id: saveName
                    objectName: "savePlaylistButton"; text: i18n.text("save", i18n.language)
                    enabled: root.connected && !root.pendingMethod && playlistName.text.trim().length > 0
                    onClicked: {
                        root.operationError = ""
                        root.pendingMethod = root.editingPlaylist ? "playlist.rename" : "playlist.create"
                        root.playlistRequested(root.editingPlaylist ? "rename" : "create", root.editingPlaylist ? {id: root.editingPlaylist, name: playlistName.text.trim()} : {name: playlistName.text.trim()})
                    }
                }
            }
        }
    }
    Popup {
        id: addPopup
        objectName: "collectionPlaylistPopup"
        parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(360, parent.width - 40); height: Math.min(400, parent.height - 40)
        modal: true; focus: true; padding: 20
        closePolicy: root.pendingMethod ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { objectName: "shortcutBlocker"; color: Theme.bgRaised; radius: Theme.radiusLg; border.color: Theme.borderSubtle }
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: i18n.text("add_to_playlist", i18n.language); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: root.playlists
                ScrollBar.vertical: ScrollBar {}
                delegate: TextButton {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.name; subtle: true
                    enabled: root.connected && !root.pendingMethod
                    onClicked: {
                        root.pendingMethod = "playlist.add"
                        root.operationError = ""
                        root.playlistRequested("add", root.currentPlaylist ? {id: Number(modelData.id), song_id: Number(root.addingSong.song_id)} : {id: Number(modelData.id), queue_id: Number(root.addingSong.queue_id || root.addingSong.id)})
                    }
                }
                Label { anchors.centerIn: parent; visible: root.playlists.length === 0; width: parent.width; wrapMode: Text.WordWrap; text: i18n.text("no_playlists", i18n.language); color: Theme.textMuted }
            }
            Label { Layout.fillWidth: true; visible: Boolean(root.operationError); text: root.operationError; wrapMode: Text.WordWrap; color: Theme.statusError; textFormat: Text.PlainText }
            TextButton { Layout.alignment: Qt.AlignRight; text: i18n.text("cancel", i18n.language); subtle: true; enabled: !root.pendingMethod; onClicked: addPopup.close() }
        }
    }
}
