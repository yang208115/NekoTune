import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
Item {
    id: page
    required property var shell
    required property var controllers
    required property var translator
    required property var transport
    property var tracks: []
    property string sourceId: ""
    property string cursor: ""
    property string message: ""
    property bool loading: false
    property int searchRequest: -1
    property bool appendResults: false
    property var playlistTrack: ({})
    function t(key) { return translator.text(key, translator.language) }
    function search(more) {
        if (!source.currentValue || loading) return
        loading = true; message = ""; appendResults = more
        sourceId = source.currentValue
        searchRequest = controllers.extensions.request("music.search", {source: sourceId, query: query.text, cursor: more ? cursor : ""})
    }
    function act(method, track, extra) { controllers.extensions.request("music." + method, Object.assign({source: sourceId, track: track}, extra || {})) }
    function focusSearch() { query.forceActiveFocus() }
    Connections {
        target: page.controllers.extensions
        function onCompleted(id, data, error) {
            if (id !== page.searchRequest) { if (error) page.message = error; return }
            page.loading = false
            if (error) { page.message = error; return }
            page.tracks = page.appendResults ? page.tracks.concat(data.tracks || []) : data.tracks || []
            page.cursor = data.cursor || ""
        }
        function onEventReceived(event) { if (event.event === "music.download") page.message = page.t("extension_download_" + event.state) + (event.message ? ": " + event.message : "") }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 16
        Label { text: page.t("music_sources"); color: Theme.textPrimary; font.pixelSize: Theme.fontTitle }
        RowLayout {
            Layout.fillWidth: true
            ChoiceField { id: source; objectName: "musicSourcePicker"; Layout.preferredWidth: 220; model: page.controllers.extensions.browserSources; textRole: "name"; valueRole: "id"; onCurrentValueChanged: { page.tracks = []; page.cursor = ""; page.searchRequest = -1; page.loading = false; page.sourceId = currentValue || "" } }
            InputField { id: query; Layout.fillWidth: true; placeholderText: page.t("search"); onAccepted: page.search(false) }
            TextButton { text: page.t("search"); enabled: !page.loading && source.count > 0; onClicked: page.search(false) }
        }
        Label { Layout.fillWidth: true; visible: text !== ""; text: page.message; color: Theme.textSecondary; wrapMode: Text.Wrap; textFormat: Text.PlainText }
        Label { visible: source.count === 0; text: page.t("music_sources_empty"); color: Theme.textMuted }
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 8; model: page.tracks
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                width: ListView.view.width; height: 78; radius: Theme.radiusSm; color: Theme.bgSurface
                RowLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: row.modelData.title; color: Theme.textPrimary; textFormat: Text.PlainText; elide: Text.ElideRight }
                        Label { Layout.fillWidth: true; text: row.modelData.artist || ""; color: Theme.textMuted; textFormat: Text.PlainText; elide: Text.ElideRight }
                    }
                    TextButton { text: page.t("extension_play"); onClicked: page.act("enqueue", row.modelData, {play: true}) }
                    TextButton { text: page.t("extension_enqueue"); subtle: true; onClicked: page.act("enqueue", row.modelData) }
                    TextButton { text: page.t("extension_playlist"); subtle: true; enabled: page.controllers.playlists.model.items.length > 0; onClicked: { page.playlistTrack = row.modelData; playlistDialog.open() } }
                    TextButton { text: page.t("extension_download"); subtle: true; onClicked: page.act("download", row.modelData) }
                }
            }
        }
        TextButton { Layout.alignment: Qt.AlignHCenter; visible: page.cursor !== ""; enabled: !page.loading; text: page.t("extension_more"); onClicked: page.search(true) }
    }
    Dialog {
        id: playlistDialog; title: page.t("extension_playlist"); modal: true; anchors.centerIn: Overlay.overlay; parent: Overlay.overlay; width: 420
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ChoiceField { id: playlistPicker; model: page.controllers.playlists.model.items; textRole: "name"; valueRole: "id" }
        onAccepted: page.act("add_to_playlist", page.playlistTrack, {playlist_id: playlistPicker.currentValue})
    }
}
