import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NekoTune 1.0
Rectangle {
    color: "#091923"
    ExtensionApi { id: api }
    ColumnLayout {
        anchors.centerIn: parent; spacing: 20
        Label { text: "NekoTune · Extension studio"; color: "#ECF7FC"; font.pixelSize: 30 }
        Label { id: reply; text: "JS/TS + QML"; color: "#8BD5EC" }
        Button { text: "Call extension service"; onClicked: api.invoke("hello").then(result => reply.text = result.message).catch(error => reply.text = String(error)) }
        Button { text: "Extension manager"; onClicked: api.app.extensions.openManager() }
    }
}
