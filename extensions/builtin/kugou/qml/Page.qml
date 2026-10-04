import QtQuick
import QtQuick.Layouts
import NekoTune 1.0
import "qrc:/qml/components" as UI
Item {
    id: page
    objectName: "kugouPluginPage"
    Controller { id: controller }
    Translator { id: pluginTranslator }
    ExtensionApi { id: api }
    function openSettings() { api.window.navigate(api.ownerId + "/settings") }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 20; spacing: 12
        UI.TextButton {
            objectName: "kugouOpenSettingsButton"
            Layout.alignment: Qt.AlignRight
            text: pluginTranslator.text("open_settings", pluginTranslator.language)
            subtle: true
            onClicked: page.openSettings()
        }
        KugouPanel {
            objectName: "kugouPluginPanel"
            Layout.fillWidth: true; Layout.fillHeight: true
            client: controller; translator: pluginTranslator
            onSettingsRequested: page.openSettings()
        }
    }
}
