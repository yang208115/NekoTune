import QtQuick
import "../components"
Item {
    id: page
    required property var shell
    required property var controllers
    required property var translator
    required property var transport
    signal importRequested(int playlistId)
    signal editRequested(var song)
    SettingsPanel { databasePath: page.shell.databasePath; anchors.fill: parent; anchors.margins: 20; client: page.controllers.settings; account: page.controllers.kugou.account; connected: page.transport.connected; translator: page.translator }
}
