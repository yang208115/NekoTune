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
    Layout.preferredWidth: shell.width < 1200 ? Theme.sidebarWidthCompact : Theme.sidebarWidthWide
    Layout.fillHeight: true
    color: Theme.bgSidebar
    function t(key) { return translator.text(key, translator.language) }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 20
        RowLayout {
            Layout.topMargin: 10; Layout.bottomMargin: 8
            Rectangle {
                Layout.preferredWidth: 32; Layout.preferredHeight: 32; radius: 10; color: Theme.bgSelected
                Label { anchors.centerIn: parent; text: "N"; color: Theme.accentPrimary; font.pixelSize: 20; font.weight: Font.Bold }
            }
            Label { text: "NekoTune"; color: Theme.textPrimary; font.pixelSize: 19; font.weight: Font.DemiBold }
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 6
            Repeater {
                model: sidebar.pages.filter(page => page.group === "primary")
                delegate: TextButton {
                    id: primaryNav
                    required property var modelData
                    Layout.fillWidth: true
                    objectName: "nav_" + modelData.id
                    text: modelData.extensionId && sidebar.controllers.extensions ? sidebar.controllers.extensions.label(modelData.title || modelData.id, sidebar.translator.language) : sidebar.t(modelData.title)
                    readonly property bool isCurrent: sidebar.shell.viewMode === modelData.id
                    subtle: true
                    subtleBg: isCurrent ? Theme.bgSelected : "transparent"
                    subtleText: isCurrent ? Theme.accentPrimary : Theme.textSecondary
                    subtleBorder: "transparent"
                    Accessible.selected: isCurrent
                    onClicked: sidebar.shell.navigate(modelData.id)

                    Rectangle {
                        anchors.left: parent.left
                        anchors.leftMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: 18
                        radius: 1.5
                        color: Theme.accentPrimary
                        visible: primaryNav.isCurrent
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: sidebar.t("playlists"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
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
                id: playlistNav
                required property var modelData
                width: playlists.width
                objectName: "nav_playlist_" + modelData.id
                text: modelData.name
                readonly property bool isCurrent: sidebar.shell.viewMode === "queue" && sidebar.currentPlaylist === Number(modelData.id)
                subtle: true
                subtleBg: isCurrent ? Theme.bgSelected : "transparent"
                subtleText: isCurrent ? Theme.accentPrimary : Theme.textSecondary
                subtleBorder: "transparent"
                Accessible.selected: isCurrent
                onClicked: sidebar.playlistRequested(Number(modelData.id))
                ToolTip.visible: hovered || activeFocus
                ToolTip.text: modelData.name

                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 3
                    height: 18
                    radius: 1.5
                    color: Theme.accentPrimary
                    visible: playlistNav.isCurrent
                }
            }
            Label { anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; anchors.topMargin: 8; visible: playlists.count === 0; text: sidebar.t("no_playlist_short"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 6
            Repeater {
                model: sidebar.pages.filter(page => page.group === "utility" || page.debug && sidebar.shell.debugEnabled)
                delegate: TextButton {
                    id: utilityNav
                    required property var modelData
                    Layout.fillWidth: true
                    objectName: modelData.debug ? "lyricsDebugNavigation" : "nav_" + modelData.id
                    text: modelData.extensionId && sidebar.controllers.extensions ? sidebar.controllers.extensions.label(modelData.title || modelData.id, sidebar.translator.language) : sidebar.t(modelData.title)
                    readonly property bool isCurrent: sidebar.shell.viewMode === modelData.id
                    subtle: true
                    subtleBg: isCurrent ? Theme.bgSelected : "transparent"
                    subtleText: isCurrent ? Theme.accentPrimary : Theme.textSecondary
                    subtleBorder: "transparent"
                    Accessible.selected: isCurrent
                    onClicked: sidebar.shell.navigate(modelData.id)

                    Rectangle {
                        anchors.left: parent.left
                        anchors.leftMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: 18
                        radius: 1.5
                        color: Theme.accentPrimary
                        visible: utilityNav.isCurrent
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 6
                Rectangle { Layout.preferredWidth: 6; Layout.preferredHeight: 6; radius: 3; color: sidebar.transport.connected ? Theme.statusSuccess : Theme.statusError }
                Label { text: sidebar.t(sidebar.transport.connected ? "connected" : "offline"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
            }
        }
    }
}
