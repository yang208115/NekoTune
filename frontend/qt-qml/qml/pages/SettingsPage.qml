import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NekoTune 1.0
import "../components"
Item {
    id: page
    required property var shell
    required property var controllers
    required property var translator
    required property var transport
    readonly property var extensions: controllers.extensions || null
    readonly property var settingsPages: extensions ? extensions.settingsPages : []
    readonly property var selected: settingsPages.find(item => item.id === shell.settingsSection) || ({})
    onSettingsPagesChanged: {
        if (shell && shell.settingsSection && !settingsPages.some(item => item.id === shell.settingsSection))
            shell.settingsSection = ""
    }
    signal importRequested(int playlistId)
    signal editRequested(var song)
    SettingsPanel {
        anchors.fill: parent; anchors.margins: 20
        visible: !page.selected.id
        databasePath: page.shell.databasePath
        client: page.controllers.settings; ai: page.controllers.ai
        connected: page.transport.connected; translator: page.translator
        extensionSettings: page.settingsPages.map(item => ({id: item.id, title: page.extensions.label(item.title, page.translator.language)}))
        onExtensionSettingsRequested: function(id) { page.shell.navigate(id) }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 20; spacing: 12
        visible: Boolean(page.selected.id)
        RowLayout {
            Layout.fillWidth: true
            TextButton {
                objectName: "settingsBackButton"
                text: "‹ " + page.translator.text("settings", page.translator.language)
                subtle: true
                onClicked: page.shell.navigate("settings")
            }
            Label { text: " / "; color: Theme.textMuted }
            Label {
                objectName: "settingsSubpageTitle"
                Layout.fillWidth: true
                text: page.extensions ? page.extensions.label(page.selected.title || "", page.translator.language) : ""
                color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle
                textFormat: Text.PlainText; elide: Text.ElideRight
            }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            ExtensionView {
                id: extension; anchors.fill: parent
                descriptor: page.selected
                controllers: page.controllers; translator: page.translator; hostWindow: page.shell
            }
            Label {
                anchors.centerIn: parent; width: parent.width
                visible: extension.error !== ""; text: extension.error
                color: Theme.statusError; wrapMode: Text.Wrap; textFormat: Text.PlainText
            }
        }
    }
}
