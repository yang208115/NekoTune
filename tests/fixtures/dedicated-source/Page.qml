import QtQuick
import QtQuick.Controls
import NekoTune 1.0

Item {
    objectName: "fixturePluginPage"
    ExtensionApi { id: api }
    Item { objectName: "fixturePluginPanel" }
    Button {
        objectName: "fixtureOpenSettingsButton"
        text: "Open settings"
        onClicked: api.window.navigate(api.ownerId + "/settings")
    }
}
