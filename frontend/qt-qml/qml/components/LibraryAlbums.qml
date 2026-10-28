pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridView {
    id: root
    required property var controller
    required property var translator
    property bool connected: true
    signal queueRequested(var songIds, bool next)
    function t(key) { return translator.text(key, translator.language) }
    objectName: "libraryAlbumGrid"
    clip: true
    cellWidth: width / Math.max(1, Math.floor(width / 190))
    cellHeight: 250
    model: controller.albums
    ScrollBar.vertical: ScrollBar {}
    delegate: TextButton {
        id: card
        required property var modelData
        required property int index
        objectName: "libraryAlbum" + index
        width: root.cellWidth - 12
        height: root.cellHeight - 12
        subtle: true
        text: modelData.title || root.t("library_unknown_album")
        onClicked: root.controller.openAlbum(modelData.album_id)
        contentItem: ColumnLayout {
            spacing: Theme.spaceSm
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                SizedImage { anchors.fill: parent; source: "qrc:/artwork/default-cover.png"; fillMode: Image.PreserveAspectCrop }
                SizedImage { anchors.fill: parent; source: String(card.modelData.cover_url || ""); fillMode: Image.PreserveAspectCrop; asynchronous: true; visible: status === Image.Ready }
            }
            Label { Layout.fillWidth: true; text: card.text; textFormat: Text.PlainText; color: Theme.textPrimary; elide: Text.ElideRight; font.weight: Font.DemiBold }
            Label { Layout.fillWidth: true; text: card.modelData.artist || root.t("unknown_artist"); textFormat: Text.PlainText; color: Theme.textMuted; elide: Text.ElideRight; font.pixelSize: Theme.fontCaption }
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: root.translator.countText("tracks", card.modelData.song_count, root.translator.language); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
                IconButton {
                    id: albumMore
                    objectName: "albumMore" + card.index
                    kind: "more"
                    tooltipText: root.t("track_actions")
                    onClicked: albumMenu.openAt(albumMore)
                }
            }
        }
        ActionMenu {
            id: albumMenu
            objectName: "albumActionMenu"
            actions: [
                {key: "next", label: root.t("play_next"), enabled: root.connected},
                {key: "queue", label: root.t("add_to_queue_end"), enabled: root.connected}
            ]
            onChosen: action => {
                const ids = root.controller.albumSongIds(card.modelData.album_id)
                if (ids.length) root.queueRequested(ids, action === "next")
            }
        }
        ToolTip.visible: hovered
        ToolTip.text: text + (modelData.artist ? "\n" + modelData.artist : "")
    }
    Label { anchors.centerIn: parent; visible: root.count === 0; text: root.t("library_no_filter_matches"); color: Theme.textMuted }
}
