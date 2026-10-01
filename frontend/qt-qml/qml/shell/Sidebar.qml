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
    signal editTagRequested(var tag)
    signal newTagRequested()

    Layout.preferredWidth: sidebar.shell.width < 1200 ? 180 : 220
    Layout.fillHeight: true
    color: sidebar.shell.bgSidebar
    border.color: sidebar.shell.border
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 16

        // Brand Section
        RowLayout {
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: 8
                color: sidebar.shell.surface
                border.color: sidebar.shell.border
                Label {
                    anchors.centerIn: parent
                    text: "N"
                    color: sidebar.shell.lavender
                    font.pixelSize: 16
                    font.weight: Font.Black
                }
            }
            ColumnLayout {
                spacing: 0
                Label {
                    text: "NEKOTUNE"
                    color: sidebar.shell.ink
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    font.letterSpacing: 1.6
                }
                Label {
                    text: "LOCAL LISTENING"
                    color: sidebar.shell.muted
                    font.pixelSize: 8
                    font.letterSpacing: 0.8
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: sidebar.shell.border
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: sidebar.pages.filter(page => !page.debug || sidebar.shell.debugEnabled)
                delegate: TextButton {
                    required property var modelData
                    Layout.fillWidth: true
                    text: sidebar.translator.text(modelData.title, sidebar.translator.language)
                    subtle: sidebar.shell.viewMode !== modelData.id
                    objectName: modelData.id === "lyrics_debug" ? "lyricsDebugNavigation" : "nav_" + modelData.id
                    onClicked: {
                        if (modelData.id === "queue") sidebar.playlistRequested(0)
                        else sidebar.shell.viewMode = modelData.id
                    }
                }
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: sidebarGroups.implicitHeight
            flickableDirection: Flickable.VerticalFlick
            Column {
                id: sidebarGroups
                width: parent.width
                spacing: 7
                Label {
                    text: sidebar.translator.text("tags", sidebar.translator.language)
                    color: sidebar.shell.muted
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                TextButton {
                    width: parent.width
                    text: sidebar.translator.text("all_songs", sidebar.translator.language)
                    subtle: true
                    subtleBg: sidebar.shell.viewMode === "library" && sidebar.shell.selectedTagIds.length === 0
                        ? sidebar.shell.bgSelected : sidebar.shell.surface
                    implicitHeight: 30
                    onClicked: { sidebar.controllers.library.selectedTagIds = []; sidebar.shell.viewMode = "library" }
                }
                TextButton {
                    width: parent.width
                    text: sidebar.translator.text("new_tag", sidebar.translator.language)
                    subtle: true
                    implicitHeight: 30
                    onClicked: sidebar.newTagRequested()
                }
                Repeater {
                    model: sidebar.controllers.library.tags
                    delegate: RowLayout {
                        required property var modelData
                        width: sidebarGroups.width
                        spacing: 3
                        TextButton {
                            Layout.fillWidth: true
                            text: modelData.name
                            subtle: true
                            subtleBg: sidebar.shell.selectedTagIds.indexOf(Number(modelData.id)) !== -1
                                ? sidebar.shell.bgSelected : sidebar.shell.surface
                            implicitHeight: 30
                            onClicked: { sidebar.controllers.library.toggleTag(Number(modelData.id)); sidebar.shell.viewMode = "library" }
                        }
                        TextButton {
                            text: "⋯"
                            subtle: true
                            implicitWidth: 30
                            implicitHeight: 30
                            onClicked: sidebar.editTagRequested(modelData)
                        }
                    }
                }
                Rectangle { width: parent.width; height: 1; color: sidebar.shell.border }
                Label {
                    text: sidebar.translator.text("playlists", sidebar.translator.language)
                    color: sidebar.shell.muted
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                TextButton {
                    width: parent.width
                    text: sidebar.translator.text("new_playlist", sidebar.translator.language)
                    subtle: true
                    implicitHeight: 30
                    onClicked: { sidebar.newPlaylistRequested() }
                }
                Repeater {
                    model: sidebar.controllers.playlists.model
                    delegate: TextButton {
                        required property var modelData
                        width: sidebarGroups.width
                        text: modelData.name
                        subtle: true
                        subtleBg: sidebar.shell.viewMode === "queue" && sidebar.currentPlaylist === Number(modelData.id)
                            ? sidebar.shell.bgSelected : sidebar.shell.surface
                        implicitHeight: 30
                        onClicked: {
                            sidebar.playlistRequested(Number(modelData.id))
                        }
                    }
                }
            }
        }

        // Sidebar Bottom Info & Language
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                visible: Boolean(sidebar.transport.error)
                text: sidebar.transport.error
                color: sidebar.shell.rose
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                // Connection Badge (Section 3.2: statusSuccess #98D8BC, statusError #FF9BAE)
                Rectangle {
                    Layout.preferredHeight: 24
                    Layout.fillWidth: true
                    radius: 6
                    color: sidebar.transport.connected ? "#15221C" : "#38202B"
                    Row {
                        anchors.centerIn: parent
                        spacing: 5
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 5
                            height: 5
                            radius: 2.5
                            color: sidebar.transport.connected ? "#98D8BC" : "#FF9BAE"
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: sidebar.transport.connected ? sidebar.translator.text("connected", sidebar.translator.language) : sidebar.translator.text("offline", sidebar.translator.language)
                            color: sidebar.transport.connected ? "#98D8BC" : "#FF9BAE"
                            font.pixelSize: 10
                        }
                    }
                }

                // Language Switch
                TextButton {
                    text: sidebar.translator.language === "zh" ? "EN" : "中"
                    implicitWidth: 38
                    implicitHeight: 24
                    cornerRadius: 6
                    subtle: true
                    onClicked: sidebar.translator.language = (sidebar.translator.language === "zh" ? "en" : "zh")
                }
            }
        }
    }
}
