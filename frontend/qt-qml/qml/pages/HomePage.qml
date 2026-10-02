pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// Home consumes library and playlist state without inheriting library filters.
// Recent rows are sorted from a copied array, preserving the source model order.
// Its primary action prioritizes current track, existing queue, then library.
// Loading/failure is distinct from a successfully loaded empty library.
// Playing recent songs sends exactly that displayed recent sequence.
// One pending play guard prevents repeated requests before completion.
Item {
    id: page
    required property var shell
    required property var controllers
    required property var translator
    required property var transport
    signal importRequested(int playlistId)

    readonly property var theme: shell.theme
    readonly property bool compact: shell.width < 1200
    readonly property var songs: controllers.library.songs.items
    readonly property var playlists: controllers.playlists.model.items
    readonly property var recentSongs: songs.slice().sort((a, b) => Number(b.song_id) - Number(a.song_id)).slice(0, 5)
    readonly property bool hasPlayableSongs: songs.some(song => song.available)
    readonly property bool initialLoading: !controllers.library.loaded && controllers.library.loading
    readonly property bool initialFailed: !controllers.library.loaded && !controllers.library.loading
    readonly property string primaryAction: shell.hasSong ? (shell.isPlaying ? "pause" : shell.playbackState === "paused" ? "home_resume" : "play")
                                           : shell.queue.length ? "home_play_queue"
                                           : hasPlayableSongs ? "home_play_library" : "import_music"
    property int selectedSongId: 0
    property int skippedCount: 0
    property bool playbackPending: false
    property alias contentY: scroll.contentY

    function t(key) { return translator.text(key, translator.language) }
    function showLibrary() {
        controllers.library.searchText = ""
        controllers.library.selectedTagIds = []
        shell.navigate("library")
    }
    function newPlaylist() {
        if (transport.connected && shell.queuePage()) shell.queuePage().newPlaylist()
    }
    function playSongs(items, startSongId) {
        skippedCount = 0
        playbackPending = true
        controllers.library.playSongs(items.map(song => Number(song.song_id)), startSongId)
    }
    function activatePrimary() {
        if (!transport.connected) return
        if (shell.hasSong) controllers.playback.togglePlayPause()
        else if (shell.queue.length) controllers.playback.play()
        else if (hasPlayableSongs) playSongs(songs, 0)
        else if (controllers.library.loaded) importRequested(0)
    }
    function playRecent(song, toggleCurrent) {
        if (!transport.connected || !song.available || playbackPending) return
        if (toggleCurrent && Number(song.song_id) === Number(shell.song.song_id)) controllers.playback.togglePlayPause()
        else playSongs(recentSongs, Number(song.song_id))
    }
    // Resolve representative artwork from items already in this collection.
    // Prefer the current library snapshot's URL for the same song identity.
    // Fallback to the playlist snapshot's enriched URL while updates settle.
    // Return empty when no item has artwork so the normal placeholder appears.
    function playlistCover(playlist) {
        for (const item of playlist.items || []) {
            const record = songs.find(song => Number(song.song_id) === Number(item.song_id))
            if (record && record.cover_url) return String(record.cover_url)
            if (item.cover_url) return String(item.cover_url)
        }
        return ""
    }
    onRecentSongsChanged: {
        if (!recentSongs.some(song => Number(song.song_id) === selectedSongId)) selectedSongId = 0
    }
    Connections {
        target: page.controllers.library
        function onLibraryPlaybackSkipped(count) { if (page.playbackPending) page.skippedCount = count }
        function onRequestSucceeded(method) { if (method === "library.play") page.playbackPending = false }
        function onRequestFailed(method, message) { if (method === "library.play") page.playbackPending = false }
    }

    Flickable {
        id: scroll
        objectName: "homeScroll"
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: content.implicitHeight + content.y * 2
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: content
            x: page.compact ? 16 : 24
            y: x
            width: scroll.width - x * 2
            spacing: 24

            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label { text: page.t("home"); color: page.theme.textPrimary; font.pixelSize: 24; font.weight: Font.DemiBold }
                    Label {
                        objectName: "homeSummary"
                        Layout.fillWidth: true
                        text: page.controllers.library.loaded ? page.t("home_counts").replace("%1", page.songs.length).replace("%2", page.playlists.length)
                              : page.t(!page.transport.connected ? "home_offline" : page.initialFailed ? "home_load_failed" : "home_loading")
                        color: page.theme.textMuted
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }
                TextButton {
                    objectName: "homeImportButton"
                    text: page.t("import_music")
                    subtle: true
                    enabled: page.transport.connected
                    onClicked: page.importRequested(0)
                }
            }

            Rectangle {
                objectName: "homeNowPlaying"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(page.compact ? 176 : 192, heroContent.implicitHeight + 48)
                radius: 16
                color: page.theme.bgRaised
                RowLayout {
                    id: heroContent
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 24
                    Item {
                        Layout.preferredWidth: page.compact ? 128 : 144
                        Layout.preferredHeight: Layout.preferredWidth
                        Image { anchors.fill: parent; source: "qrc:/artwork/default-cover.png"; fillMode: Image.PreserveAspectCrop; mipmap: true }
                        Image {
                            objectName: "homeCurrentCover"
                            anchors.fill: parent
                            source: page.shell.hasSong ? String(page.shell.song.cover_url || "") : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            mipmap: true
                            visible: status === Image.Ready
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 8
                        Label {
                            text: page.t(page.shell.hasSong ? (page.shell.isPlaying ? "now_playing" : page.shell.playbackState) : "home_welcome")
                            color: page.theme.accentPrimary
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        Label {
                            id: heroTitle
                            Layout.fillWidth: true
                            text: page.shell.hasSong ? String(page.shell.song.title || page.t("untitled")) : page.t("home_make_time")
                            textFormat: Text.PlainText
                            color: page.theme.textPrimary
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            HoverHandler { id: titleHover }
                            ToolTip.visible: titleHover.hovered
                            ToolTip.text: heroTitle.text
                        }
                        ArtistNames {
                            objectName: "homeCurrentArtists"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            artist: page.shell.hasSong ? String(page.shell.song.artist || "") : ""
                            fallbackText: page.shell.hasSong ? page.t("unknown_artist")
                                  : page.t(page.shell.queue.length ? "home_queue_ready" : "home_choose_music")
                            color: page.theme.textSecondary
                            font.pixelSize: 14
                        }
                        RowLayout {
                            Layout.topMargin: 8
                            spacing: 12
                            TextButton {
                                objectName: "homePrimaryButton"
                                text: page.t(page.primaryAction)
                                enabled: page.transport.connected && !page.playbackPending
                                         && (page.shell.hasSong || page.shell.queue.length > 0 || page.controllers.library.loaded)
                                onClicked: page.activatePrimary()
                            }
                            TextButton {
                                objectName: "homeLyricsButton"
                                text: page.t("home_view_lyrics")
                                subtle: true
                                subtleBg: "transparent"
                                visible: page.shell.hasSong
                                onClicked: page.shell.openNowPlaying()
                            }
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: page.skippedCount > 0
                text: page.t("unavailable_skipped").replace("%1", page.skippedCount)
                color: page.theme.statusWarning
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            GridLayout {
                id: collections
                Layout.fillWidth: true
                columns: page.compact ? 1 : 2
                columnSpacing: 24
                rowSpacing: 24

                ColumnLayout {
                    objectName: "homePlaylistsSection"
                    Layout.fillWidth: true
                    Layout.preferredWidth: page.compact ? collections.width : (collections.width - 24) * 0.4
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignTop
                    spacing: 16
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: page.t("home_my_playlists"); color: page.theme.textPrimary; font.pixelSize: 18; font.weight: Font.DemiBold }
                        IconButton {
                            objectName: "homeNewPlaylistButton"
                            kind: "plus"
                            tooltipText: page.t("new_playlist")
                            enabled: page.transport.connected
                            onClicked: page.newPlaylist()
                        }
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 12
                        Repeater {
                            model: page.playlists.slice(0, 4)
                            delegate: Button {
                                id: card
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.minimumWidth: 0
                                implicitHeight: 124
                                padding: 12
                                hoverEnabled: true
                                objectName: "homePlaylist" + modelData.id
                                Accessible.name: String(modelData.name)
                                onClicked: page.shell.navigate("queue", Number(modelData.id))
                                ToolTip.visible: hovered || activeFocus
                                ToolTip.text: String(modelData.name)
                                background: Rectangle {
                                    radius: 12
                                    color: card.hovered ? page.theme.bgHover : page.theme.bgSurface
                                    border.color: card.activeFocus ? page.theme.accentPrimary : "transparent"
                                }
                                contentItem: ColumnLayout {
                                    spacing: 8
                                    RowLayout {
                                        spacing: 12
                                        Item {
                                            Layout.preferredWidth: 48; Layout.preferredHeight: 48
                                            Image { anchors.fill: parent; source: "qrc:/artwork/default-cover.png"; fillMode: Image.PreserveAspectCrop; mipmap: true }
                                            Image { objectName: "homePlaylistCover"; anchors.fill: parent; source: page.playlistCover(card.modelData); fillMode: Image.PreserveAspectCrop; asynchronous: true; visible: status === Image.Ready }
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            text: String(card.modelData.name)
                                            textFormat: Text.PlainText
                                            color: page.theme.textPrimary
                                            font.pixelSize: 14
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                            maximumLineCount: 2
                                            wrapMode: Text.Wrap
                                        }
                                    }
                                    RowLayout {
                                        Label {
                                            Layout.fillWidth: true
                                            text: page.translator.countText("tracks", (card.modelData.items || []).length, page.translator.language)
                                            color: page.theme.textMuted; font.pixelSize: 12; elide: Text.ElideRight
                                        }
                                        IconButton {
                                            objectName: "homePlayPlaylist" + card.modelData.id
                                            kind: "play"
                                            implicitWidth: 36; implicitHeight: 36
                                            tooltipText: page.t("play")
                                            enabled: page.transport.connected && (card.modelData.items || []).length > 0
                                            onClicked: page.controllers.playlists.managePlaylist("play", {id: Number(card.modelData.id)})
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Label {
                        objectName: "homeEmptyPlaylists"
                        Layout.fillWidth: true
                        visible: page.playlists.length === 0
                        text: page.t(page.transport.connected ? "no_playlists" : "home_offline")
                        color: page.theme.textMuted; font.pixelSize: 14; wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: page.playlists.length > 4
                        text: page.t("home_more_playlists")
                        color: page.theme.textMuted; font.pixelSize: 12; wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    objectName: "homeRecentSection"
                    Layout.fillWidth: true
                    Layout.preferredWidth: page.compact ? collections.width : (collections.width - 24) * 0.6
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignTop
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: page.t("home_recent"); color: page.theme.textPrimary; font.pixelSize: 18; font.weight: Font.DemiBold }
                        TextButton {
                            objectName: "homeViewAllButton"
                            text: page.t("home_view_all")
                            subtle: true; subtleBg: "transparent"; subtleBorder: "transparent"
                            onClicked: page.showLibrary()
                        }
                    }
                    Repeater {
                        model: page.recentSongs
                        delegate: TrackRow {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            Layout.minimumWidth: 0
                            objectName: "homeRecentSong" + modelData.song_id
                            song: modelData
                            rowIndex: index
                            selected: page.selectedSongId === Number(modelData.song_id)
                            current: Number(page.shell.song.song_id) === Number(modelData.song_id)
                            playing: page.shell.isPlaying
                            available: Boolean(modelData.available)
                            connected: page.transport.connected && !page.playbackPending
                            playObjectName: "homePlaySong" + modelData.song_id
                            onSelectedRequested: modifiers => page.selectedSongId = Number(modelData.song_id)
                            onActivated: page.playRecent(modelData)
                            onPlayClicked: page.playRecent(modelData, true)
                        }
                    }
                    Label {
                        objectName: "homeLibraryState"
                        Layout.fillWidth: true
                        Layout.topMargin: 16
                        visible: page.songs.length === 0
                        text: page.t(!page.transport.connected ? "home_offline" : page.initialLoading ? "home_loading" : page.initialFailed ? "home_load_failed" : "home_empty_library")
                        color: page.theme.textMuted; font.pixelSize: 14; wrapMode: Text.WordWrap
                    }
                    TextButton {
                        objectName: "homeRetryButton"
                        visible: page.initialFailed && page.transport.connected
                        text: page.t("home_retry")
                        subtle: true
                        onClicked: page.controllers.library.refreshLibrary()
                    }
                }
            }
        }
    }
}
