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
    function focusSearch() { panel.focusSearch() }
    KugouPanel { id: panel; objectName: "kugouPanel"; onSettingsRequested: page.shell.navigate("settings"); anchors.fill: parent; anchors.margins: 20; client: page.controllers.kugou; translator: page.translator }
}
