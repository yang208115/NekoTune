import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: sidebar
    required property var shell
    required property var controllers
    required property var transport
    required property var translator
    required property var pages
    property int currentPlaylist: 0
    signal playlistRequested(int id)
    signal newPlaylistRequested()
    Layout.preferredWidth: shell.width < 1200 ? 180 : 220
    Layout.fillHeight: true
    color: shell.bgSidebar
    function t(key) { return translator.text(key, translator.language) }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 20
        RowLayout {
            Layout.topMargin: 10; Layout.bottomMargin: 8
            Rectangle {
                Layout.preferredWidth: 32; Layout.preferredHeight: 32; radius: 10; color: "#322743"
                Label { anchors.centerIn: parent; text: "N"; color: "#CBB8FF"; font.pixelSize: 20; font.weight: Font.Bold }
            }
            Label { text: "NekoTune"; color: "#F5F1FA"; font.pixelSize: 19; font.weight: Font.DemiBold }
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 6
            Repeater {
                model: sidebar.pages.filter(page => page.group === "primary")
                delegate: TextButton {
                    required property var modelData
                    Layout.fillWidth: true
                    objectName: "nav_" + modelData.id
                    text: sidebar.t(modelData.title)
                    subtle: sidebar.shell.viewMode !== modelData.id
                    subtleBg: "transparent"; subtleBorder: "transparent"
                    onClicked: sidebar.shell.navigate(modelData.id)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: sidebar.t("playlists"); color: "#AAA0B8"; font.pixelSize: 12 }
            IconButton { kind: "plus"; implicitWidth: 32; implicitHeight: 32; tooltipText: sidebar.t("new_playlist"); enabled: sidebar.transport.connected; onClicked: sidebar.newPlaylistRequested() }
        }
        ListView {
            id: playlists
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
            model: sidebar.controllers.playlists.model
            ScrollBar.vertical: ScrollBar {}
            onCountChanged: Qt.callLater(() => {
                const index = sidebar.controllers.playlists.model.items.findIndex(list => Number(list.id) === sidebar.currentPlaylist)
                if (index >= 0) playlists.positionViewAtIndex(index, ListView.Contain)
            })
            delegate: TextButton {
                required property var modelData
                width: playlists.width
                objectName: "nav_playlist_" + modelData.id
                text: modelData.name
                subtle: !(sidebar.shell.viewMode === "queue" && sidebar.currentPlaylist === Number(modelData.id))
                subtleBg: "transparent"; subtleBorder: "transparent"
                onClicked: sidebar.playlistRequested(Number(modelData.id))
                ToolTip.visible: hovered || activeFocus
                ToolTip.text: modelData.name
            }
            Label { anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; anchors.topMargin: 8; visible: playlists.count === 0; text: sidebar.t("no_playlist_short"); color: "#AAA0B8"; font.pixelSize: 12 }
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 6
            Repeater {
                model: sidebar.pages.filter(page => page.group === "utility" || page.debug && sidebar.shell.debugEnabled)
                delegate: TextButton {
                    required property var modelData
                    Layout.fillWidth: true
                    objectName: modelData.debug ? "lyricsDebugNavigation" : "nav_" + modelData.id
                    text: sidebar.t(modelData.title)
                    subtle: sidebar.shell.viewMode !== modelData.id
                    subtleBg: "transparent"; subtleBorder: "transparent"
                    onClicked: sidebar.shell.navigate(modelData.id)
                }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 6
                Rectangle { Layout.preferredWidth: 6; Layout.preferredHeight: 6; radius: 3; color: sidebar.transport.connected ? "#98D8BC" : "#FF9BAE" }
                Label { text: sidebar.t(sidebar.transport.connected ? "connected" : "offline"); color: "#AAA0B8"; font.pixelSize: 12 }
            }
        }
    }
}
